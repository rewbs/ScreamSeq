import AppKit

final class MixerSlider: NSSlider {
  var trackingGesture = false
  var onFinish: (() -> Void)?
  override func mouseDown(with event: NSEvent) {
    trackingGesture = true
    super.mouseDown(with: event)
    trackingGesture = false
    onFinish?()
  }
}

final class MixerControl: NSView, NSTextFieldDelegate {
  let slider = MixerSlider(), value = NSTextField(string: "0")
  let key: String, scale: Double
  var onChange: ((String, Double, Bool) -> Void)?
  var onFinish: (() -> Void)?
  var onRawChange: (() -> Void)?, onEnter: (() -> Void)?
  var canAdjust: (() -> Bool)?
  var hasDraft: Bool { !shown.isEmpty && value.stringValue != shown }
  func accept(_ text: String) { shown = text; editingValue = false }
  func discardDraft() { shown = ""; editingValue = false }
  // The text this control last displayed. A field's action also fires when
  // editing ends without typing; only text that differs is an edit.
  private var shown = "", shownNumber = 0.0
  private(set) var editingValue=false
  init(_ title: String, key: String, min: Double, max: Double, scale: Double = 1) {
    self.key = key; self.scale = scale
    super.init(frame: .zero)
    slider.minValue = min * scale; slider.maxValue = max * scale; slider.isContinuous = true
    slider.target = self; slider.action = #selector(changed)
    slider.onFinish = { [weak self] in self?.onFinish?() }
    slider.setAccessibilityLabel(title)
    value.target = self; value.action = #selector(entered); value.delegate = self; value.fixed(width: 68)
    value.setAccessibilityLabel("\(title) value")
    let label = Theme.label(title, size: 12); label.fixed(width: 90)
    stack(.horizontal, [label, slider, value]).fill(self)
    fixed(height: 28)
  }
  required init?(coder: NSCoder) { fatalError() }
  func set(_ number: Double) {
    guard !hasDraft || onEnter == nil else { return }
    shownNumber = number * scale; slider.doubleValue = shownNumber; shown = String(format: "%.2f", number * scale)
    if value.stringValue != shown { value.stringValue = shown }
  }
  @objc private func changed() {
    guard canAdjust?() != false else { slider.doubleValue = shownNumber; return }
    shownNumber = slider.doubleValue
    shown = String(format: "%.2f", slider.doubleValue); value.stringValue = shown
    onChange?(key, slider.doubleValue / scale, !slider.trackingGesture)
  }
  @objc func entered() {
    editingValue = false
    guard hasDraft else { return }
    if let onEnter { onEnter(); return }
    // Compact graph controls still own their original immediate transaction.
    // Retained Mixer forms acknowledge raw text only through their completion.
    guard let number = Double(value.stringValue), number.isFinite,
      (slider.minValue...slider.maxValue).contains(number) else { value.stringValue = shown; return }
    slider.doubleValue = number; shownNumber = number; shown = value.stringValue
    onChange?(key, number / scale, true)
  }
  func controlTextDidEndEditing(_ notification: Notification) { entered() }
  func controlTextDidChange(_ notification: Notification) { editingValue = true; onRawChange?() }

}

final class MixerMeterView: NSView {
  var left = 0.0, right = 0.0
  override func draw(_ dirtyRect: NSRect) {
    Theme.bg.setFill(); bounds.fill()
    for (row, value) in [left, right].enumerated() {
      let fraction = max(0, min(1, (20 * log10(max(0.00001, value)) + 60) / 60))
      (value >= 1 ? NSColor.systemRed : Theme.accent).setFill()
      NSRect(x: 0, y: CGFloat(row) * bounds.height / 2 + 1,
        width: bounds.width * fraction, height: bounds.height / 2 - 2).fill()
    }
  }
}

final class MixerEditor: NSView, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate {
  let table = NSTableView(), status = Theme.label("", size: 12, color: Theme.muted)
  let heading = Theme.label("Mixer", size: 20, weight: .semibold)
  let strips = MixerStrips(frame: .zero)
  let viewMode = NSSegmentedControl(labels: ["Strips", "Routing"], trackingMode: .selectOne, target: nil, action: nil)
  let name = NSTextField(string: ""), color = NSTextField(string: "000000"), timing = NSTextField(string: "0")
  let output = NSPopUpButton(), insert = NSPopUpButton(), effect = NSPopUpButton()
  let rack = PluginRack(frame: .zero)
  let send = NSPopUpButton(), sendTarget = NSPopUpButton(), sendGain = NSTextField(string: "-12")
  let preSend = NSButton(checkboxWithTitle: "Pre-fader", target: nil, action: nil)
  let sendEnabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
  let source = NSPopUpButton()
  let mute = NSButton(checkboxWithTitle: "Mute", target: nil, action: nil)
  let solo = NSButton(checkboxWithTitle: "Solo", target: nil, action: nil)
  let meter = MixerMeterView(frame: .zero), peak = Theme.label("−∞ dB", size: 11, mono: true)
  private var meterAvailable: Bool?
  let controls = [MixerControl("Pre gain · dB", key: "preGainDB", min: -96, max: 24),
    MixerControl("Pre balance · %", key: "prePan", min: -1, max: 1, scale: 100),
    MixerControl("Fader · dB", key: "gainDB", min: -96, max: 24),
    MixerControl("Balance · %", key: "pan", min: -1, max: 1, scale: 100),
    MixerControl("Width · %", key: "width", min: 0, max: 2, scale: 100)]
  var buses = [[String: Any]](), plugins = [[String: Any]](), sources = [[String: Any]]()
  var selectedID: String?, revision = "", active = false, loading = false, implicit = false
  private var pendingNavigation: String?
  private var focusCreatedBus: String?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onSidechains: (() -> Void)?
  var onConfigurePlugin: ((Int) -> Void)?
  var onOpenPlugin: ((String) -> Void)?, onPluginControls: ((String) -> Void)?
  var onAddPlugin: ((String) -> Void)?
  private var destinations = [[String: Any]](), effects = [[String: Any]](), instruments = [[String: Any]]()
  private var draft = [String: Any](), commitWanted = false
  private var controlRequestInFlight = false
  private var committingControls = [String: Any]()
  // Only explicit notCommitted admission receipts authorize write retries.
  // Uncertain results retain the target/draft until an explicit fresh read.
  var busyRetryDelay = 0.25, busyRetryLimit = 12
  private var reloadButton: ActionButton?
  private(set) var needsReload = false { didSet { reloadButton?.isHidden = !needsReload } }
  private var cleanupKeys = Set<String>()
  var hasPendingControls: Bool { needsReload || !draft.isEmpty || !committingControls.isEmpty || hasRawDraft || hasSendDraft }
  var needsCompletion: Bool { loading || hasPendingControls || hasMarkedText }
  private var automaticWork: DispatchWorkItem?
  var automaticDelay = 0.6
  private var completionWaiters = [(Bool) -> Void]()
  private var contextGeneration = 0
  private var rawSubmission = [String: String]()
  private var shownSendFields = [String: String]()
  // Text and popup choices last written for the selected bus. Anything that
  // differs is an uncommitted edit and survives refreshes of that same bus.
  private var shownBus: String?, shownText = [ObjectIdentifier: String]()
  private var shownFlags = [String: Bool]()
  private var shownChoice = [ObjectIdentifier: String](), shownSend: (key: String, value: NSDictionary)?
  private var routingView: NSView!
  let inspector = NSView(), routingScroll = verticalScrollView()
  private var enableButton: ActionButton!
  var selected: [String: Any]? { buses.first { $0["id"] as? String == selectedID } }
  override init(frame: NSRect) {
    super.init(frame: frame)
    viewMode.selectedSegment = 0; viewMode.target = self; viewMode.action = #selector(changeViewMode)
    strips.onControl = { [weak self] id, key, value, final in self?.stripControl(id, key: key, value: value, final: final) }
    strips.onRawChange = { [weak self] id in
      guard let self, self.claimRawBus(id) else { return }; self.scheduleAutomaticEdit()
    }
    strips.onRawEnter = { [weak self] id in
      guard let self, self.claimRawBus(id) else { return }; self.finishAutomaticEdit { _ in }
    }
    // Claim the bus before AppKit accepts text, not after textDidChange. A
    // second bus must not acquire a draft while the first bus still owns one.
    strips.canBeginRawEdit = { [weak self] id in self?.selectBus(id) == true }
    strips.canAdjust = { [weak self] in self?.hasRawDraft == false && self?.hasSendDraft == false }
    strips.onFinish = { [weak self] id in guard self?.selectedID == id else { return }; self?.finishGesture() }
    strips.onInspect = { [weak self] id in
      guard let self, self.selectBus(id) else { return }
      self.viewMode.selectedSegment = 1; self.changeViewMode()
    }
    table.delegate = self; table.dataSource = self; table.rowHeight = 34; table.headerView = nil
    table.addTableColumn(NSTableColumn(identifier: .init("bus")))
    table.setAccessibilityLabel("Mixer tracks, groups and returns")
    let list = verticalScrollView(); list.documentView = table; list.fixed(width: 230)
    name.setAccessibilityLabel("Bus name"); color.setAccessibilityLabel("Bus color in hexadecimal")
    for field in [name, color, timing] { field.delegate = self; field.target = self; field.action = #selector(fieldChanged(_:)) }
    output.target = self; output.action = #selector(route)
    rack.onSelect = { [weak self] id in self?.selectInsert(id) }
    rack.onOpen = { [weak self] id in self?.onOpenPlugin?(id) }
    rack.onRemove = { [weak self] id in guard let self, let slot = self.plugins.first(where: { $0["id"] as? String == id })?["slot"] as? Int else { return }; self.mutate("plugin.remove", ["slot": slot]) }
    rack.onBypass = { [weak self] id, bypass in
      guard let self, let slot = self.plugins.first(where: { $0["id"] as? String == id })?["slot"] as? Int else { return }
      self.mutate("plugin.bypass", ["slot": slot, "bypass": bypass])
    }
    rack.onDrop = { [weak self] id, before, _ in
      guard let self, let selectedID = self.selectedID else { return }
      self.mutate("mixer.inserts.move", ["plugins": [id], "target": selectedID, "before": before as Any? ?? NSNull()])
    }
    rack.menuForItem = { [weak self] id in self?.insertMenu(id) ?? NSMenu() }
    color.fixed(width: 80); timing.fixed(width: 65); timing.setAccessibilityLabel("Track timing offset in milliseconds")
    for (picker, label) in [(output, "Bus output"), (insert, "Insert chain"), (effect, "Available effect"),
      (send, "Existing send"), (sendTarget, "Send destination"), (source, "Plugin instrument source")] {
      picker.setAccessibilityLabel(label)
    }
    sendGain.delegate = self
    sendGain.fixed(width: 65); sendGain.setAccessibilityLabel("Send gain in decibels")
    send.target = self; send.action = #selector(selectSend)
    mute.target = self; mute.action = #selector(changeMute); solo.target = self; solo.action = #selector(changeSolo)
    for control in controls {
      control.onChange = { [weak self] key, value, final in self?.control(key, value: value, final: final) }
      control.onRawChange = { [weak self] in self?.scheduleAutomaticEdit() }
      control.onEnter = { [weak self] in self?.finishAutomaticEdit { _ in } }
      control.canAdjust = { [weak self] in self?.hasRawDraft == false && self?.hasSendDraft == false }
      control.onFinish = { [weak self] in self?.finishGesture() }
    }
    meter.fixed(height: 20); peak.fixed(width: 90)
    let controlStack = stack(.vertical, controls); controlStack.stretchAcrossAxis()
    let inspectorContent = stack(.vertical, [
      stack(.horizontal, [name, Theme.label("Color", size: 11), color]),
      stack(.horizontal, [meter, peak, mute, solo]),
      controlStack,
      stack(.horizontal, [Theme.label("Output", size: 12), output, Theme.label("Timing · ms", size: 11), timing]),
      stack(.horizontal, [Theme.label("INSERT EFFECTS", size: 10, color: Theme.muted, weight: .semibold), NSView(), ActionButton("Sidechains…") { [weak self] in self?.onSidechains?() }]),
      rack,
      stack(.horizontal, [ActionButton("Add effect…", symbol: "plus", prominent: true) { [weak self] in guard let self, !self.needsCompletion, let id = self.selectedID else { return }; self.onAddPlugin?(id) },
        ActionMenuButton("Insert actions…") { [weak self] in self?.insertMenu(self?.rack.selectedID) ?? NSMenu() }, NSView()]),
      Theme.label("SENDS", size: 10, color: Theme.muted, weight: .semibold),
      stack(.horizontal, [send, ActionButton("Remove send") { [weak self] in self?.removeSend() }]),
      stack(.horizontal, [sendTarget, sendGain, Theme.label("dB", size: 11), preSend, sendEnabled,
        ActionButton("Set send") { [weak self] in self?.setSend() }]),
      Theme.label("INSTRUMENT INPUT", size: 10, color: Theme.muted, weight: .semibold),
      stack(.horizontal, [source, ActionButton("Audio buses…") { [weak self] in self?.configureInstrument() }, ActionButton("Route here") { [weak self] in self?.routeInstrument() }]),
      Theme.label("Unassigned effects process on Master. Each enabled instrument output can use its own mixer destination.", size: 11, color: Theme.muted),
      NSView()
    ], spacing: 9)
    // AppKit aligns rounded buttons by their alignment rect; the bezel extends
    // beyond that rect. Leave room inside the scrolling document for the full
    // native control bounds, including the right-aligned inspector actions.
    inspectorContent.stretchAcrossAxis(); inspectorContent.fill(inspector, inset: 12)
    routingScroll.documentView = inspector
    inspector.translatesAutoresizingMaskIntoConstraints = false
    let fillHeight = inspector.heightAnchor.constraint(equalTo: routingScroll.contentView.heightAnchor)
    fillHeight.priority = .defaultLow
    NSLayoutConstraint.activate([
      inspector.leadingAnchor.constraint(equalTo: routingScroll.contentView.leadingAnchor),
      inspector.topAnchor.constraint(equalTo: routingScroll.contentView.topAnchor),
      inspector.widthAnchor.constraint(equalTo: routingScroll.contentView.widthAnchor),
      inspector.heightAnchor.constraint(greaterThanOrEqualToConstant: 600), fillHeight,
    ])
    let contentRow = stack(.horizontal, [list, routingScroll], spacing: 20)
    routingView = contentRow
    list.heightAnchor.constraint(equalTo: contentRow.heightAnchor).isActive = true
    routingScroll.heightAnchor.constraint(equalTo: contentRow.heightAnchor).isActive = true
    let body = NSView(); contentRow.fill(body); strips.fill(body)
    enableButton = ActionButton("Enable mixer") { [weak self] in self?.mutate("mixer.enable", [:]) }
    reloadButton = ActionButton("Reload saved state") { [weak self] in self?.reload() }
    reloadButton?.isHidden = true
    reloadButton?.setContentCompressionResistancePriority(.required, for: .horizontal)
    status.lineBreakMode = .byWordWrapping; status.maximumNumberOfLines = 2
    status.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    let content = stack(.vertical, [
      stack(.horizontal, [heading, NSView(), viewMode, ActionMenuButton { [weak self] in self?.busMenu() ?? NSMenu() }]),
      body, stack(.horizontal, [status, NSView(), reloadButton!]),
      Theme.label("Faders audition smoothly · one Undo per gesture. Edit connections in Graph.", size: 11, color: Theme.muted)
    ], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 20)
    changeViewMode()
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func changeViewMode() {
    routingView.isHidden = viewMode.selectedSegment == 0
    strips.isHidden = viewMode.selectedSegment != 0
    if !strips.isHidden { strips.reveal(selectedID ?? "") }
  }
  @discardableResult func selectBus(_ id: String) -> Bool {
    guard buses.contains(where: { $0["id"] as? String == id }) else { return false }
    if id != selectedID {
      guard !loading, !hasPendingControls, !hasMarkedText else { status.stringValue = "Finish the current mixer edit before changing buses."; return false }
      selectedID = id
      if let index = buses.firstIndex(where: { $0["id"] as? String == id }) { table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false) }
      showSelected(); refreshStrips()
    }
    return true
  }
  // Panel bridges may arrive before the asynchronous mixer snapshot. Keep the
  // stable destination until that snapshot lands; never retarget a fader edit.
  func navigate(to id: String) {
    pendingNavigation = id
    if !draft.isEmpty { finishGesture() }
    if !loading { load() }
  }
  func stripControl(_ id: String, key: String, value: Any, final: Bool) {
    guard selectBus(id) else { refreshStrips(); return }
    control(key, value: value, final: final)
  }
  private func refreshStrips() {
    var current = buses
    if let index = current.firstIndex(where: { $0["id"] as? String == selectedID }) {
      for (key, value) in committingControls { current[index][key] = value }
      for (key, value) in draft { current[index][key] = value }
    }
    strips.update(current, selected: selectedID)
  }
  private var textFields: [(String, NSTextField)] { [("name", name), ("color", color), ("timingMS", timing)] }
  private var rawChanges: [String: String] {
    var values = [String: String]()
    for (key, field) in textFields {
      if let baseline = shownText[ObjectIdentifier(field)], field.stringValue != baseline { values[key] = field.stringValue }
    }
    for control in controls where control.hasDraft { values[control.key] = control.value.stringValue }
    for (key, button) in [("mute", mute), ("solo", solo)] {
      if let baseline = shownFlags[key], baseline != (button.state == .on) { values[key] = button.state == .on ? "true" : "false" }
    }
    if let baseline = shownChoice[ObjectIdentifier(output)], let value = chosen(output), value != baseline { values["output"] = value }
    for strip in strips.visible.values {
      for (key, value) in strip.rawChanges { values["strip:\(strip.busID):\(key)"] = value }
    }
    return values
  }
  var hasRawDraft: Bool { !rawChanges.isEmpty }
  private var sendFields: [String: String] {
    ["gain": sendGain.stringValue, "target": chosen(sendTarget) ?? "",
     "pre": String(preSend.state.rawValue), "enabled": String(sendEnabled.state.rawValue)]
  }
  private var hasSendDraft: Bool { !shownSendFields.isEmpty && sendFields != shownSendFields }
  private var hasMarkedText: Bool {
    let fields = textFields.map { $0.1 } + [sendGain] + controls.map(\.value) + strips.visible.values.flatMap { [$0.gain, $0.preGain] }
    return fields.contains { ($0.currentEditor() as? NSTextView)?.hasMarkedText() == true }
  }
  private func rawSnapshot() -> [String: String] {
    var snapshot = rawChanges
    if hasSendDraft { for (key, value) in sendFields { snapshot["send:" + key] = value } }
    return snapshot
  }
  private func claimRawBus(_ id: String) -> Bool {
    if selectedID == id { return true }
    let onlyThisStrip = rawChanges.keys.allSatisfy { $0.hasPrefix("strip:\(id):") }
    guard !loading, !needsReload, draft.isEmpty, committingControls.isEmpty, !hasSendDraft,
      onlyThisStrip, buses.contains(where: { $0["id"] as? String == id }) else {
      status.stringValue = "Finish the retained mixer fields before changing buses."; return false
    }
    selectedID = id; showSelected(); refreshStrips(); return true
  }
  func controlTextDidChange(_ notification: Notification) { scheduleAutomaticEdit() }
  private func scheduleAutomaticEdit() {
    automaticWork?.cancel()
    guard !needsReload else { return }
    let work = DispatchWorkItem { [weak self] in self?.automaticWork = nil; self?.finishAutomaticEdit { _ in } }
    automaticWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + automaticDelay, execute: work)
  }
  private func finishWaiters(_ accepted: Bool) {
    let callbacks = completionWaiters; completionWaiters = []
    for callback in callbacks { callback(accepted) }
  }
  func finishAutomaticEdit(_ done: @escaping (Bool) -> Void) {
    automaticWork?.cancel(); automaticWork = nil
    guard !hasMarkedText else { status.stringValue = "Finish composing the mixer value before continuing."; done(false); return }
    guard !needsReload else { status.stringValue = "Mixer fields kept. Reload saved state before continuing."; done(false); return }
    guard !hasSendDraft else { status.stringValue = "Set or reload the retained send before continuing."; done(false); return }
    guard onRequest != nil else { done(!needsCompletion); return }
    guard selected != nil || !hasPendingControls else { status.stringValue = "The captured bus is unavailable. Reload saved state before continuing."; done(false); return }
    if loading { completionWaiters.append(done); if controlRequestInFlight { commitWanted = true }; return }
    do {
      let values = try parsedRawChanges()
      if values.isEmpty && draft.isEmpty { done(true); return }
      completionWaiters.append(done)
      rawSubmission = rawChanges
      draft.merge(values) { _, new in new }; commitWanted = true; sendControls()
    } catch { status.stringValue = error.localizedDescription; done(false) }
  }
  private func parsedRawChanges() throws -> [String: Any] {
    var values = [String: Any]()
    func invalid(_ message: String) -> NSError { NSError(domain: "ScreamSeq", code: 1, userInfo: [NSLocalizedDescriptionKey: message + " · Fields kept."]) }
    for (rawKey, text) in rawChanges {
      var key = rawKey
      if key.hasPrefix("strip:") {
        guard let selectedID, key.hasPrefix("strip:\(selectedID):") else { throw invalid("Finish the other bus's gain fields first") }
        key = String(key.dropFirst("strip:\(selectedID):".count))
      }
      if key == "name" { values[key] = text; continue }
      if key == "mute" || key == "solo" { values[key] = text == "true"; continue }
      if key == "output" {
        guard text.isEmpty || destinations.contains(where: { $0["id"] as? String == text }) else { throw invalid("That output is no longer available") }
        values[key] = text.isEmpty ? NSNull() : text; continue
      }
      if key == "color" {
        guard text.count == 6, text.allSatisfy({ "0123456789abcdefABCDEF".contains($0) }), let value = Int(text, radix: 16), (0...0xffffff).contains(value) else { throw invalid("Use a six-digit hexadecimal color") }
        values[key] = value; continue
      }
      guard let number = Double(text), number.isFinite else { throw invalid("Enter a finite mixer value") }
      let control = controls.first { $0.key == key }
      // Inspector pan/width fields display percent; strips use native gain dB.
      let value = number / (rawKey.hasPrefix("strip:") ? 1 : control?.scale ?? 1)
      if let control, !(control.slider.minValue / control.scale...control.slider.maxValue / control.scale).contains(value) {
        throw invalid("Mixer value is outside its allowed range")
      }
      if key == "timingMS", !(-500...500).contains(value) { throw invalid("Timing offset must be between −500 and 500 ms") }
      if let previous = values[key] as? Double, previous != value { throw invalid("Inspector and strip values differ; finish one value first") }
      values[key] = value
    }
    return values
  }
  private func acceptRaw(_ submitted: [String: String]) {
    for (key, field) in textFields { if let text = submitted[key] { shownText[ObjectIdentifier(field)] = text } }
    for control in controls { if let text = submitted[control.key] { control.accept(text) } }
    for key in ["mute", "solo"] { if let value = submitted[key] { shownFlags[key] = value == "true" } }
    if let value = submitted["output"] { shownChoice[ObjectIdentifier(output)] = value }
    for strip in strips.visible.values { strip.acceptRaw(submitted) }
  }
  func invalidateDocumentContext() {
    contextGeneration += 1; automaticWork?.cancel(); automaticWork = nil
    draft = committingControls.merging(draft) { _, newer in newer }; committingControls = [:]
    loading = false; controlRequestInFlight = false; commitWanted = false; pendingNavigation = nil
    if hasPendingControls { needsReload = true; status.stringValue = "Document changed. Mixer fields kept; Reload saved state to continue." }
    else { revision = ""; selectedID = nil; shownBus = nil }
    finishWaiters(false)
  }
  func load() {
    guard !loading, !hasPendingControls, !hasMarkedText else { return }
    request("mixer.get", ["includeImplicit": true]) { data, _ in self.update(data) }
  }
  private func reloadDraftSnapshot() -> NSDictionary {
    let snapshot: [String: Any] = ["bus": selectedID ?? "", "draft": draft,
     "text": [name, color, timing, sendGain].map(\.stringValue) + controls.map { $0.value.stringValue },
     "choices": [output, insert, effect, send, sendTarget, source].map { chosen($0) ?? "" },
     "checks": [mute, solo, preSend, sendEnabled].map { $0.state.rawValue }]
    return snapshot as NSDictionary
  }
  /// Explicitly read the accepted state before releasing retained edits or
  /// cancelling an audition. An uncertain commit may already have succeeded.
  func reload() {
    guard !loading, !hasMarkedText else { return }
    automaticWork?.cancel(); automaticWork = nil
    let target = selectedID
    let captured = reloadDraftSnapshot()
    let previewKeys = Set(controls.map(\.key) + ["mute", "solo"])
    cleanupKeys.formUnion(Set(draft.keys).intersection(previewKeys)); cleanupKeys.formUnion(Set(committingControls.keys).intersection(previewKeys))
    needsReload = true
    request("mixer.get", ["includeImplicit": true], adoptRevision: false) { [weak self] data, acceptedRevision in
      guard let self else { return }
      let finish = { [weak self] in
        guard let self else { return }
        guard captured.isEqual(self.reloadDraftSnapshot()) else {
          self.status.stringValue = "Newer mixer fields kept. Reload saved state again to resolve them."; return
        }
        self.revision = acceptedRevision
        self.draft = [:]; self.committingControls = [:]; self.commitWanted = false
        self.cleanupKeys = []; self.needsReload = false; self.shownBus = nil
        self.rawSubmission = [:]; self.shownSendFields = [:]; self.shownSend = nil; self.shownText = [:]; self.shownChoice = [:]; self.shownFlags = [:]
        for control in self.controls { control.discardDraft() }
        for strip in self.strips.visible.values { strip.discardRaw() }
        self.update(data)
      }
      guard let buses = data["buses"] as? [[String: Any]] else {
        self.status.stringValue = "Mixer readback is incomplete. Fields kept; Reload saved state to try again."; return
      }
      let current = buses.first { $0["id"] as? String == target }
      guard data["active"] as? Bool == true, data["implicit"] as? Bool != true, !self.cleanupKeys.isEmpty, let target, let current else { finish(); return }
      var params: [String: Any] = ["bus": target, "expectedRevision": acceptedRevision, "preview": true]
      for key in self.cleanupKeys {
        params[key] = current[key] ?? (key == "width" ? 1.0 : key == "mute" || key == "solo" ? false : 0.0)
      }
      // Use only the newly read baseline. A failed cancellation retains the
      // keys and raw fields for another explicit read, never a blind resend.
      self.request("mixer.bus.set", params, adoptRevision: false) { _, _ in finish() }
    }
  }
  func synchronize(_ token: String) {
    guard !needsReload else { return }
    if token != revision && !loading && draft.isEmpty { load() }
  }
  func update(_ data: [String: Any]) {
    guard !hasMarkedText, !hasRawDraft, !hasSendDraft || shownBus == nil else { return }
    implicit = data["implicit"] as? Bool ?? false
    active = (data["active"] as? Bool ?? false) || implicit; buses = data["buses"] as? [[String: Any]] ?? []
    plugins = data["plugins"] as? [[String: Any]] ?? []; sources = data["instruments"] as? [[String: Any]] ?? []
    enableButton.isHidden = true; inspector.isHidden = !active
    if let target = pendingNavigation {
      pendingNavigation = nil
      if buses.contains(where: { $0["id"] as? String == target }) {
        selectedID = target; viewMode.selectedSegment = 1; changeViewMode()
      }
    }
    if !buses.contains(where: { $0["id"] as? String == selectedID }) { selectedID = buses.first?["id"] as? String }
    table.reloadData()
    if let index = buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
      table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
    }
    showSelected()
    refreshStrips()
    if focusCreatedBus == selectedID, focusCreatedBus != nil { focusCreatedBus = nil; window?.makeFirstResponder(name) }
    status.stringValue = active ? "\(buses.count) buses · choose a track, group or return" : "Enable the mixer to add groups, sends and track effects."
  }
  private func request(_ method: String, _ params: [String: Any], attempt: Int = 0, capturedRaw: [String: String]? = nil, context: Int? = nil, adoptRevision: Bool = true, done: @escaping ([String: Any], String) -> Void) {
    guard !loading, let onRequest else { finishWaiters(false); return }
    let capturedRaw = capturedRaw ?? rawSnapshot(), context = context ?? contextGeneration
    guard context == contextGeneration else { return }; loading = true
    onRequest(method, params) { [weak self] reply in
      guard let self, self.contextGeneration == context else { return }
      let failure = reply["error"] as? [String: Any]
      let readOnly = method == "mixer.get"
      let mayRetry = readOnly ? (failure?["code"] as? Int == -32002) : NativeRequestOutcome.canRetryWrite(reply)
      if reply["result"] as? [String: Any] == nil, mayRetry {
        if attempt < self.busyRetryLimit {
          // Still loading: the bus stays pinned and later control changes queue in the draft.
          self.status.stringValue = "The document is busy · retrying…"
          DispatchQueue.main.asyncAfter(deadline: .now() + self.busyRetryDelay) { [weak self] in
            guard let self, self.contextGeneration == context else { return }; self.loading = false
            self.request(method, params, attempt: attempt + 1, capturedRaw: capturedRaw, context: context, adoptRevision: adoptRevision, done: done)
          }
          return
        }
      }
      self.loading = false; self.controlRequestInFlight = false
      let committing = self.committingControls; self.committingControls = [:]
      guard let result = reply["result"] as? [String: Any] else {
        if !readOnly {
          self.draft = committing.merging(self.draft) { _, newer in newer }
          self.commitWanted = false; self.needsReload = true
        }
        let message = failure?["message"] as? String ?? "Mixer result is uncertain."
        self.status.stringValue = message + (self.needsReload ? " · Edit kept. Reload saved state to continue." : " · Reload to try reading again.")
        self.refreshStrips(); self.finishWaiters(false); return
      }
      guard capturedRaw == self.rawSnapshot(), !self.hasMarkedText else {
        self.draft = committing.merging(self.draft) { _, newer in newer }
        self.needsReload = true; self.commitWanted = false
        self.status.stringValue = "Newer mixer fields kept at their captured revision. Reload saved state to review the result."
        self.finishWaiters(false); return
      }
      let acceptedRevision = result["revision"] as? String ?? self.revision
      if adoptRevision { self.revision = acceptedRevision }
      done(result["data"] as? [String: Any] ?? [:], acceptedRevision)
      if !self.loading { self.finishWaiters(!self.hasPendingControls && !self.hasMarkedText) }
    }
  }
  func mutate(_ method: String, _ values: [String: Any]) {
    guard !loading, !hasPendingControls else { status.stringValue = "Finish or reload the retained mixer fields before this action."; return }
    var p = values; p["expectedRevision"] = revision
    request(method, p) { data, _ in
      if method == "mixer.bus.add", let id = data["bus"] as? String { self.pendingNavigation = id; self.focusCreatedBus = id }
      self.load()
    }
  }
  func control(_ key: String, value: Any, final: Bool) {
    guard selected != nil else { return }
    if hasRawDraft || hasSendDraft {
      guard !loading, !needsReload, !hasSendDraft, !hasMarkedText else { status.stringValue = "Finish or reload the retained mixer fields before adjusting controls."; return }
      do {
        let values = try parsedRawChanges(); rawSubmission = rawChanges
        draft.merge(values) { _, new in new }; draft[key] = value; commitWanted = true; sendControls()
      } catch { status.stringValue = error.localizedDescription }
      return
    }
    if needsReload {
      draft[key] = value; refreshStrips()
      status.stringValue = "Edit kept. Reload saved state to resolve the earlier result before continuing."; return
    }
    guard !loading || controlRequestInFlight else {
      status.stringValue = "Wait for the mixer to finish loading before adjusting controls."; showSelected(); refreshStrips(); return
    }
    draft[key] = value; commitWanted = commitWanted || final || implicit; refreshStrips(); sendControls()
  }
  func finishGesture() { guard !draft.isEmpty, !needsReload else { return }; commitWanted = true; sendControls() }
  private func sendControls() {
    guard !loading, !needsReload, !draft.isEmpty, let selectedID else { return }
    let values = draft, final = commitWanted, submitted = rawSubmission
    var p = values; p["bus"] = selectedID; p["expectedRevision"] = revision; p["preview"] = !final
    if final { committingControls = values; draft = [:]; commitWanted = false }
    controlRequestInFlight = true
    request("mixer.bus.set", p) { _, _ in
      if final {
        self.acceptRaw(submitted); self.rawSubmission = [:]
        if let index = self.buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
          for (key, value) in values { self.buses[index][key] = value }
        }
        self.table.reloadData()
        if let index = self.buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
          self.table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        }
        self.refreshStrips()
        self.status.stringValue = "Mixer controls saved · ⌘Z to undo"
        if !self.draft.isEmpty { self.sendControls() }
        else if self.pendingNavigation != nil { self.load() }
        else { self.showSelected() }
      } else if self.commitWanted || !NSDictionary(dictionary: values).isEqual(to: self.draft) { self.sendControls() }
    }
  }
  // Popups carry a stable ID per item. addItems(withTitles:) removes an earlier
  // item with an equal title, which made every later index point at the wrong
  // bus, plugin or send.
  private func fill(_ popup: NSPopUpButton, _ items: [(title: String, id: String)]) {
    popup.removeAllItems()
    for (index, entry) in items.enumerated() {
      let item = NSMenuItem(title: entry.title, action: nil, keyEquivalent: "")
      item.representedObject = entry.id; item.tag = index; popup.menu?.addItem(item)
    }
  }
  private func chosen(_ popup: NSPopUpButton) -> String? { popup.selectedItem?.representedObject as? String }
  @discardableResult private func choose(_ popup: NSPopUpButton, _ id: String?) -> Bool {
    guard let id else { return false }
    let index = popup.indexOfItem(withRepresentedObject: id)
    if index >= 0 { popup.selectItem(at: index) }; return index >= 0
  }
  /// Rebuilds a popup. `saved` is the document's choice; without one the
  /// previous selection is simply restored. A choice the musician made but has
  /// not applied yet is kept while the same bus stays selected.
  private func fill(_ popup: NSPopUpButton, _ items: [(title: String, id: String)], saved: String?, sameBus: Bool) {
    let key = ObjectIdentifier(popup), previous = popup.numberOfItems > 0 ? chosen(popup) : nil
    let picked = sameBus && previous != nil && (saved == nil || shownChoice[key] != previous)
    fill(popup, items)
    if let saved { shownChoice[key] = saved }
    if !(picked && choose(popup, previous)) && !choose(popup, saved) && popup.numberOfItems > 0 { popup.selectItem(at: 0) }
  }
  private func show(_ field: NSTextField, _ text: String, sameBus: Bool) {
    let key = ObjectIdentifier(field)
    let edited = sameBus && shownText[key].map { field.stringValue != $0 } ?? false
    shownText[key] = text
    if !edited && field.stringValue != text { field.stringValue = text }
  }
  private func showSelected() {
    guard let bus = selected else { return }
    let sameBus = shownBus != nil && shownBus == selectedID; shownBus = selectedID
    heading.stringValue = bus["name"] as? String ?? "Mixer"
    show(name, bus["name"] as? String ?? "", sameBus: sameBus); show(color, String(format: "%06X", bus["color"] as? Int ?? 0), sameBus: sameBus)
    show(timing, String(format: "%.12g", (bus["timingMS"] as? NSNumber)?.doubleValue ?? 0), sameBus: sameBus)
    timing.isEnabled = bus["kind"] as? String == "track"
    mute.state = bus["mute"] as? Bool == true ? .on : .off; solo.state = bus["solo"] as? Bool == true ? .on : .off
    shownFlags = ["mute": mute.state == .on, "solo": solo.state == .on]
    if !sameBus { for control in controls { control.discardDraft() } }
    for control in controls { control.set((bus[control.key] as? NSNumber)?.doubleValue ?? (control.key == "width" ? 1 : 0)) }
    destinations = buses.filter { $0["kind"] as? String != "track" && $0["id"] as? String != selectedID }
    let targets = destinations.map { (title: $0["name"] as? String ?? "Bus", id: $0["id"] as? String ?? "") }
    fill(output, [(title: "Disconnected", id: "")] + targets, saved: bus["output"] as? String ?? "", sameBus: sameBus)
    fill(sendTarget, targets, saved: nil, sameBus: sameBus)
    output.isEnabled = bus["kind"] as? String != "master"
    let inserts = bus["inserts"] as? [String] ?? []
    let previousInsert = rack.selectedID
    fill(insert, inserts.map { id in (title: plugins.first { $0["id"] as? String == id }?["name"] as? String ?? "Unavailable plugin", id: id) }, saved: nil, sameBus: sameBus)
    rack.update(inserts.compactMap { id in
      guard var item = plugins.first(where: { $0["id"] as? String == id }) else { return nil }
      item["instanceID"] = id; item["ownerID"] = selectedID; return item
    }, selected: previousInsert)
    if let previousInsert { selectInsert(previousInsert) }
    effects = plugins.filter { $0["instrument"] as? Bool != true }
    fill(effect, effects.map { (title: $0["name"] as? String ?? "Effect", id: $0["id"] as? String ?? "") }, saved: nil, sameBus: sameBus)
    instruments = plugins.filter { $0["instrument"] as? Bool == true }.flatMap { plugin -> [[String: Any]] in
      let ports = plugin["audioBuses"] as? [[String: Any]] ?? [["index": 0, "direction": "output", "active": true]]
      return ports.filter { $0["direction"] as? String == "output" && $0["active"] as? Bool == true }.map { port in
        var item = plugin; item["output"] = port["index"]; item["outputName"] = port["name"]; return item
      }
    }
    fill(source, instruments.map { p in
      let output = p["output"] as? Int ?? 0
      let target = sources.first { $0["plugin"] as? String == p["id"] as? String && ($0["output"] as? Int ?? 0) == output }?["target"] as? String
      let destination = buses.first { $0["id"] as? String == target }?["name"] as? String ?? (output == 0 ? "Master" : "Unrouted")
      return (title: "\(p["name"] as? String ?? "Instrument") · out \(output + 1) → \(destination)", id: "\(p["id"] as? String ?? ""):\(output)")
    }, saved: nil, sameBus: sameBus)
    showSendList(keeping: sameBus ? chosen(send) : nil)
  }
  private func showSendList(keeping selection: String?) {
    let sends = selected?["sends"] as? [[String: Any]] ?? []
    // A send has no ID of its own; its position in this bus's list, paired
    // with its destination, identifies it within one snapshot.
    fill(send, [(title: "New send", id: "")] + sends.enumerated().map { index, s in
      (title: buses.first { $0["id"] as? String == s["target"] as? String }?["name"] as? String ?? "Bus", id: "\(index):\(s["target"] as? String ?? "")")
    })
    if !choose(send, selection) { send.selectItem(at: 0) }
    showSend(refreshing: true)
  }
  func showMeters(_ meters: [[AnyHashable: Any]]) {
    strips.showMeters(meters)
    let current = meters.first { $0["bus"] as? String == selectedID }
    let left = (current?["left"] as? NSNumber)?.doubleValue ?? 0, right = (current?["right"] as? NSNumber)?.doubleValue ?? 0
    // This runs on every display tick: leave the views alone unless a level moved.
    let available = current != nil
    guard left != meter.left || right != meter.right || meterAvailable != available else { return }
    meterAvailable = available
    meter.left = left; meter.right = right; meter.needsDisplay = true
    let level = max(left, right)
    let text = current == nil ? "—" : level > 0.00001 ? String(format: "%.1f dB", 20 * log10(level)) : "−∞ dB"
    if peak.stringValue != text { peak.stringValue = text }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { buses.count }
  func tableView(_ tableView: NSTableView, shouldSelectRow row: Int) -> Bool { !loading && !hasPendingControls }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard buses.indices.contains(table.selectedRow), !hasPendingControls else { return }
    selectedID = buses[table.selectedRow]["id"] as? String; showSelected(); refreshStrips()
  }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    let cell = tableView.makeView(withIdentifier: .init("mixerBus"), owner: self) as? NSTextField ?? Theme.label("", size: 12)
    cell.identifier = .init("mixerBus")
    let bus = buses[row], db = (buses[row]["gainDB"] as? NSNumber)?.doubleValue ?? 0
    cell.stringValue = "\(bus["name"] as? String ?? "Bus")  ·  \(String(format: "%.1f", db)) dB"
    cell.textColor = bus["mute"] as? Bool == true ? Theme.muted : bus["solo"] as? Bool == true ? Theme.gold : Theme.text
    cell.setAccessibilityLabel("\(bus["kind"] as? String ?? "bus") \(cell.stringValue)")
    return cell
  }
  @objc private func changeMute() { control("mute", value: mute.state == .on, final: true) }
  @objc private func changeSolo() { control("solo", value: solo.state == .on, final: true) }
  func selectInsert(_ id: String) {
    if let index = (selected?["inserts"] as? [String] ?? []).firstIndex(of: id) { insert.selectItem(at: index) }
  }
  func insertMenu(_ id: String?) -> NSMenu {
    let menu = NSMenu(title: "Mixer inserts"); menu.autoenablesItems = false
    if let id, let slot = plugins.first(where: { $0["id"] as? String == id })?["slot"] as? Int {
      menu.addItem(ContextAction("Open interface / controls") { [weak self] in self?.onOpenPlugin?(id) })
      menu.addItem(ContextAction("Plugin controls and presets…") { [weak self] in self?.onPluginControls?(id) })
      for (title, direction) in [("Move earlier", -1), ("Move later", 1)] {
        menu.addItem(ContextAction(title) { [weak self] in self?.selectInsert(id); self?.moveInsert(direction) })
      }
      menu.addItem(ContextAction("Remove plugin") { [weak self] in self?.mutate("plugin.remove", ["slot": slot]) })
    }
    let existing = NSMenu(title: "Move existing effect here"); existing.autoenablesItems = false
    for entry in effects {
      guard let id = entry["id"] as? String, let target = selectedID else { continue }
      existing.addItem(ContextAction(entry["name"] as? String ?? "Effect") { [weak self] in
        self?.mutate("mixer.inserts.move", ["plugins": [id], "target": target])
      })
    }
    ContextActions.appendMenu(existing, to: menu); return menu
  }
  func busMenu() -> NSMenu {
    let menu = NSMenu(title: "Mixer"); menu.autoenablesItems = false
    menu.addItem(ContextAction("Add group") { [weak self] in self?.mutate("mixer.bus.add", ["kind": "group"]) })
    menu.addItem(ContextAction("Add return") { [weak self] in self?.mutate("mixer.bus.add", ["kind": "return"]) })
    menu.addItem(ContextAction("Remove bus", enabled: selected?["kind"] as? String == "group" || selected?["kind"] as? String == "return") { [weak self] in self?.removeBus() })
    menu.addItem(ContextAction("Reload saved state") { [weak self] in self?.reload() })
    return menu
  }
  @objc private func fieldChanged(_ sender: NSTextField) { finishAutomaticEdit { _ in } }
  func controlTextDidEndEditing(_ notification: Notification) {
    if notification.object as? NSTextField !== sendGain { finishAutomaticEdit { _ in } }
  }
  @objc private func route() { finishAutomaticEdit { _ in } }
  private func removeBus() {
    guard let selectedID else { return }; mutate("mixer.bus.remove", ["bus": selectedID])
  }
  private func setInserts(_ values: [String]) {
    guard let selectedID else { return }; mutate("mixer.bus.set", ["bus": selectedID, "inserts": values])
  }
  func openInsert(controls: Bool = false) {
    guard !loading, !hasPendingControls else { return }
    let values = selected?["inserts"] as? [String] ?? []
    guard let id = chosen(insert), values.contains(id) else { return }
    guard plugins.contains(where: { $0["id"] as? String == id }) else { return }
    if controls { onPluginControls?(id) } else { onOpenPlugin?(id) }
  }
  private func addInsert() {
    guard let id = chosen(effect), !id.isEmpty, effects.contains(where: { $0["id"] as? String == id }) else { return }
    var values = selected?["inserts"] as? [String] ?? []; values.append(id); setInserts(values)
  }
  private func removeInsert() {
    var values = selected?["inserts"] as? [String] ?? []
    guard let id = chosen(insert), let index = values.firstIndex(of: id) else { return }
    values.remove(at: index); setInserts(values)
  }
  private func moveInsert(_ direction: Int) {
    var values = selected?["inserts"] as? [String] ?? []
    guard let id = chosen(insert), let index = values.firstIndex(of: id), values.indices.contains(index + direction) else { return }
    values.swapAt(index, index + direction); setInserts(values)
  }
  /// The chosen send, provided the popup still describes the bus's current list.
  private var chosenSend: (index: Int, value: [String: Any])? {
    let values = selected?["sends"] as? [[String: Any]] ?? []
    guard let id = chosen(send), let index = Int(id.prefix { $0 != ":" }), values.indices.contains(index),
      id == "\(index):\(values[index]["target"] as? String ?? "")" else { return nil }
    return (index, values[index])
  }
  @objc private func selectSend() {
    guard !hasSendDraft else {
      if let shownSend { _ = choose(send, String(shownSend.key.dropFirst((selectedID ?? "").count + 1))) }
      status.stringValue = "Set or reload the retained send before choosing another."; return
    }
    showSend(refreshing: false)
  }
  // A refresh leaves the send fields alone unless the chosen send or its saved
  // settings changed; choosing a send in the popup always shows its settings.
  private func showSend(refreshing: Bool) {
    let current = chosenSend, key = "\(selectedID ?? ""):\(chosen(send) ?? "")", value = NSDictionary(dictionary: current?.value ?? [:])
    if refreshing, let shownSend, shownSend.key == key, shownSend.value.isEqual(value) { return }
    shownSend = (key, value)
    defer { shownSendFields = sendFields }
    guard let current else { sendGain.stringValue = "-12"; preSend.state = .off; sendEnabled.state = .on; return }
    choose(sendTarget, current.value["target"] as? String)
    sendGain.stringValue = String(format: "%.12g", (current.value["gainDB"] as? NSNumber)?.doubleValue ?? -12)
    preSend.state = current.value["preFader"] as? Bool == true ? .on : .off; sendEnabled.state = current.value["enabled"] as? Bool == true ? .on : .off
  }
  private func setSend() {
    guard let selectedID, let target = chosen(sendTarget), destinations.contains(where: { $0["id"] as? String == target }),
      let gain = Double(sendGain.stringValue), gain.isFinite else {
      status.stringValue = "Choose an available send destination and a finite gain. Fields kept."; return
    }
    var values = selected?["sends"] as? [[String: Any]] ?? []
    let value: [String: Any] = ["target": target, "gainDB": gain, "preFader": preSend.state == .on, "enabled": sendEnabled.state == .on]
    let index: Int
    if send.indexOfSelectedItem > 0 {
      guard let current = chosenSend else { status.stringValue = "That send changed. Choose it again."; return }
      index = current.index; values[index] = value
    } else { index = values.count; values.append(value) }
    guard !loading, !needsReload, draft.isEmpty, committingControls.isEmpty, !hasMarkedText else {
      status.stringValue = "Finish or reload the other mixer fields before setting this send."; return
    }
    request("mixer.sends.set", ["bus": selectedID, "sends": values, "expectedRevision": revision]) { _, _ in
      // The request guard has checked the complete raw snapshot. Carry other
      // unchanged fields across this owned revision, refreshing only the send.
      if let busIndex = self.buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
        self.buses[busIndex]["sends"] = values
      }
      self.shownSendFields = self.sendFields
      self.showSendList(keeping: "\(index):\(target)"); self.refreshStrips()
      self.status.stringValue = "Send saved · ⌘Z to undo"
      if self.hasRawDraft { self.scheduleAutomaticEdit() }
    }
  }
  private func removeSend() {
    guard let selectedID, let current = chosenSend else { return }; var values = selected?["sends"] as? [[String: Any]] ?? []
    values.remove(at: current.index); mutate("mixer.sends.set", ["bus": selectedID, "sends": values])
  }
  /// The instrument output chosen in the source popup, by plugin ID and port.
  private var chosenInstrument: [String: Any]? {
    guard let id = chosen(source) else { return nil }
    return instruments.first { "\($0["id"] as? String ?? ""):\($0["output"] as? Int ?? 0)" == id }
  }
  private func configureInstrument() {
    guard let slot = chosenInstrument?["slot"] as? Int else { return }
    onConfigurePlugin?(slot)
  }
  private func routeInstrument() {
    guard let selectedID, let instrument = chosenInstrument, let plugin = instrument["id"] as? String else { return }
    mutate("mixer.instrument.route", ["plugin": plugin, "target": selectedID, "output": instrument["output"] ?? 0])
  }
}
