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

final class MixerControl: NSView {
  let slider = MixerSlider(), value = NSTextField(string: "0")
  let key: String, scale: Double
  var onChange: ((String, Double, Bool) -> Void)?
  var onFinish: (() -> Void)?
  // The text this control last displayed. A field's action also fires when
  // editing ends without typing; only text that differs is an edit.
  private var shown = ""
  init(_ title: String, key: String, min: Double, max: Double, scale: Double = 1) {
    self.key = key; self.scale = scale
    super.init(frame: .zero)
    slider.minValue = min * scale; slider.maxValue = max * scale; slider.isContinuous = true
    slider.target = self; slider.action = #selector(changed)
    slider.onFinish = { [weak self] in self?.onFinish?() }
    slider.setAccessibilityLabel(title)
    value.target = self; value.action = #selector(entered); value.fixed(width: 68)
    value.setAccessibilityLabel("\(title) value")
    let label = Theme.label(title, size: 12); label.fixed(width: 90)
    stack(.horizontal, [label, slider, value]).fill(self)
    fixed(height: 28)
  }
  required init?(coder: NSCoder) { fatalError() }
  func set(_ number: Double) {
    slider.doubleValue = number * scale; shown = String(format: "%.2f", number * scale)
    if value.stringValue != shown { value.stringValue = shown }
  }
  @objc private func changed() {
    shown = String(format: "%.2f", slider.doubleValue); value.stringValue = shown
    onChange?(key, slider.doubleValue / scale, !slider.trackingGesture)
  }
  @objc func entered() {
    // Committing the rounded display would turn −3.27 dB into −3.30 and add an Undo step.
    guard value.stringValue != shown else { return }
    guard let number = Double(value.stringValue), number.isFinite,
      number >= slider.minValue, number <= slider.maxValue else {
      shown = String(format: "%.2f", slider.doubleValue); value.stringValue = shown; return
    }
    slider.doubleValue = number; shown = value.stringValue; onChange?(key, number / scale, true)
  }
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

final class MixerEditor: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let table = NSTableView(), status = Theme.label("", size: 12, color: Theme.muted)
  let heading = Theme.label("Mixer", size: 20, weight: .semibold)
  let strips = MixerStrips(frame: .zero)
  let viewMode = NSSegmentedControl(labels: ["Strips", "Routing"], trackingMode: .selectOne, target: nil, action: nil)
  let name = NSTextField(string: ""), color = NSTextField(string: "000000"), timing = NSTextField(string: "0")
  let output = NSPopUpButton(), insert = NSPopUpButton(), effect = NSPopUpButton()
  let send = NSPopUpButton(), sendTarget = NSPopUpButton(), sendGain = NSTextField(string: "-12")
  let preSend = NSButton(checkboxWithTitle: "Pre-fader", target: nil, action: nil)
  let sendEnabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
  let source = NSPopUpButton()
  let mute = NSButton(checkboxWithTitle: "Mute", target: nil, action: nil)
  let solo = NSButton(checkboxWithTitle: "Solo", target: nil, action: nil)
  let meter = MixerMeterView(frame: .zero), peak = Theme.label("−∞ dB", size: 11, mono: true)
  let controls = [MixerControl("Pre gain · dB", key: "preGainDB", min: -96, max: 24),
    MixerControl("Pre balance · %", key: "prePan", min: -1, max: 1, scale: 100),
    MixerControl("Fader · dB", key: "gainDB", min: -96, max: 24),
    MixerControl("Balance · %", key: "pan", min: -1, max: 1, scale: 100),
    MixerControl("Width · %", key: "width", min: 0, max: 2, scale: 100)]
  var buses = [[String: Any]](), plugins = [[String: Any]](), sources = [[String: Any]]()
  var selectedID: String?, revision = "", active = false, loading = false
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onSidechains: (() -> Void)?
  var onConfigurePlugin: ((Int) -> Void)?
  var onOpenPlugin: ((String) -> Void)?, onPluginControls: ((String) -> Void)?
  private var destinations = [[String: Any]](), effects = [[String: Any]](), instruments = [[String: Any]]()
  private var draft = [String: Any](), commitWanted = false
  private var controlRequestInFlight = false
  private var committingControls = [String: Any]()
  // "The document is busy" (-32002) is a request to retry, not a rejection.
  // The draft and a pending fader commit survive it.
  var busyRetryDelay = 0.25, busyRetryLimit = 12
  private var deferredRetry = Date.distantFuture
  var hasPendingControls: Bool { !draft.isEmpty || !committingControls.isEmpty }
  // Text and popup choices last written for the selected bus. Anything that
  // differs is an uncommitted edit and survives refreshes of that same bus.
  private var shownBus: String?, shownText = [ObjectIdentifier: String]()
  private var shownChoice = [ObjectIdentifier: String](), shownSend: (key: String, value: NSDictionary)?
  private var routingView: NSView!
  let inspector = NSView(), routingScroll = verticalScrollView()
  private var enableButton: ActionButton!
  var selected: [String: Any]? { buses.first { $0["id"] as? String == selectedID } }
  override init(frame: NSRect) {
    super.init(frame: frame)
    viewMode.selectedSegment = 0; viewMode.target = self; viewMode.action = #selector(changeViewMode)
    strips.onControl = { [weak self] id, key, value, final in self?.stripControl(id, key: key, value: value, final: final) }
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
    color.fixed(width: 80); timing.fixed(width: 65); timing.setAccessibilityLabel("Track timing offset in milliseconds")
    for (picker, label) in [(output, "Bus output"), (insert, "Insert chain"), (effect, "Available effect"),
      (send, "Existing send"), (sendTarget, "Send destination"), (source, "Plugin instrument source")] {
      picker.setAccessibilityLabel(label)
    }
    sendGain.fixed(width: 65); sendGain.setAccessibilityLabel("Send gain in decibels")
    send.target = self; send.action = #selector(selectSend)
    mute.target = self; mute.action = #selector(changeMute); solo.target = self; solo.action = #selector(changeSolo)
    for control in controls {
      control.onChange = { [weak self] key, value, final in self?.control(key, value: value, final: final) }
      control.onFinish = { [weak self] in self?.finishGesture() }
    }
    meter.fixed(height: 20); peak.fixed(width: 90)
    let controlStack = stack(.vertical, controls); controlStack.stretchAcrossAxis()
    let inspectorContent = stack(.vertical, [
      stack(.horizontal, [name, Theme.label("Color", size: 11), color,
        ActionButton("Rename") { [weak self] in self?.rename() }]),
      stack(.horizontal, [meter, peak, mute, solo]),
      controlStack,
      stack(.horizontal, [Theme.label("Output", size: 12), output,
        ActionButton("Route") { [weak self] in self?.route() }, Theme.label("Timing · ms", size: 11), timing,
        ActionButton("Set") { [weak self] in self?.setTiming() }]),
      stack(.horizontal, [Theme.label("INSERT EFFECTS", size: 10, color: Theme.muted, weight: .semibold), NSView(), ActionButton("Sidechains…") { [weak self] in self?.onSidechains?() }]),
      stack(.horizontal, [insert, ActionButton("Controls") { [weak self] in self?.openInsert(controls: true) },
        ActionButton("Open UI") { [weak self] in self?.openInsert() }, ActionButton("↑") { [weak self] in self?.moveInsert(-1) },
        ActionButton("↓") { [weak self] in self?.moveInsert(1) }, ActionButton("Remove") { [weak self] in self?.removeInsert() }]),
      stack(.horizontal, [effect, ActionButton("Add effect") { [weak self] in self?.addInsert() }]),
      Theme.label("SENDS", size: 10, color: Theme.muted, weight: .semibold),
      stack(.horizontal, [send, ActionButton("Remove send") { [weak self] in self?.removeSend() }]),
      stack(.horizontal, [sendTarget, sendGain, Theme.label("dB", size: 11), preSend, sendEnabled,
        ActionButton("Set send") { [weak self] in self?.setSend() }]),
      Theme.label("INSTRUMENT INPUT", size: 10, color: Theme.muted, weight: .semibold),
      stack(.horizontal, [source, ActionButton("Audio buses…") { [weak self] in self?.configureInstrument() }, ActionButton("Route here") { [weak self] in self?.routeInstrument() }]),
      Theme.label("Unassigned effects process on Master. Each enabled instrument output can use its own mixer destination.", size: 11, color: Theme.muted),
      NSView()
    ], spacing: 9)
    inspectorContent.stretchAcrossAxis(); inspectorContent.fill(inspector, inset: 3)
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
    let content = stack(.vertical, [
      stack(.horizontal, [heading, NSView(), viewMode, enableButton,
        ActionButton("Add group") { [weak self] in self?.mutate("mixer.bus.add", ["kind": "group"]) },
        ActionButton("Add return") { [weak self] in self?.mutate("mixer.bus.add", ["kind": "return"]) },
        ActionButton("Remove bus") { [weak self] in self?.removeBus() },
        ActionButton("Reload") { [weak self] in self?.reload() }]),
      body, status,
      Theme.label("Faders audition smoothly and save one Undo per gesture. Routing and insert changes stop playback.", size: 11, color: Theme.muted)
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
      guard !loading, draft.isEmpty else { status.stringValue = "Finish the current mixer edit before changing buses."; return false }
      selectedID = id
      if let index = buses.firstIndex(where: { $0["id"] as? String == id }) { table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false) }
      showSelected(); refreshStrips()
    }
    return true
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
  func load() {
    guard !loading, draft.isEmpty else { return }
    request("mixer.get", [:]) { self.update($0) }
  }
  /// Reload and drop uncommitted fader, text and popup edits.
  func reload() {
    guard !loading else { return }
    shownBus = nil; deferredRetry = .distantFuture
    let abandoned = Set(draft.keys); draft = [:]; commitWanted = false
    if abandoned.isEmpty { load() } else { showSelected(); refreshStrips(); resyncEngine(abandoned) { [weak self] in self?.load() } }
  }
  func synchronize(_ token: String) {
    if !loading, !draft.isEmpty, commitWanted, deferredRetry <= Date() { deferredRetry = .distantFuture; sendControls(); return }
    if token != revision && !loading && draft.isEmpty { load() }
  }
  // After a rejected edit the engine still plays the auditioned values.
  // Preview the saved ones so that sound and document agree again.
  private func resyncEngine(_ keys: Set<String>, then: (() -> Void)? = nil) {
    guard !loading, !keys.isEmpty, let selectedID, let bus = selected else { then?(); return }
    var p: [String: Any] = ["bus": selectedID, "expectedRevision": revision, "preview": true]
    for key in keys { p[key] = bus[key] ?? (key == "width" ? 1.0 : key == "mute" || key == "solo" ? false : 0.0) }
    controlRequestInFlight = true
    request("mixer.bus.set", p, resync: true) { [weak self] _ in
      then?(); guard let self, !self.loading, !self.draft.isEmpty else { return }; self.sendControls()
    }
  }
  func update(_ data: [String: Any]) {
    active = data["active"] as? Bool ?? false; buses = data["buses"] as? [[String: Any]] ?? []
    plugins = data["plugins"] as? [[String: Any]] ?? []; sources = data["instruments"] as? [[String: Any]] ?? []
    enableButton.isEnabled = !active; inspector.isHidden = !active
    if !buses.contains(where: { $0["id"] as? String == selectedID }) { selectedID = buses.first?["id"] as? String }
    table.reloadData()
    if let index = buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
      table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
    }
    showSelected()
    refreshStrips()
    status.stringValue = active ? "\(buses.count) buses · choose a track, group or return" : "Enable the mixer to add groups, sends and track effects."
  }
  private func request(_ method: String, _ params: [String: Any], attempt: Int = 0, resync: Bool = false, done: @escaping ([String: Any]) -> Void) {
    guard !loading, let onRequest else { return }; loading = true
    onRequest(method, params) { [weak self] reply in
      guard let self else { return }
      let failure = reply["error"] as? [String: Any]
      if reply["result"] as? [String: Any] == nil, failure?["code"] as? Int == -32002 {
        if attempt < self.busyRetryLimit {
          // Still loading: the bus stays pinned and later control changes queue in the draft.
          self.status.stringValue = "The document is busy · retrying…"
          DispatchQueue.main.asyncAfter(deadline: .now() + self.busyRetryDelay) { [weak self] in
            guard let self else { return }; self.loading = false
            self.request(method, params, attempt: attempt + 1, resync: resync, done: done)
          }
          return
        }
        self.loading = false; self.controlRequestInFlight = false
        if resync { done([:]); return }
        if !self.committingControls.isEmpty {
          self.draft = self.committingControls.merging(self.draft) { _, newer in newer }; self.commitWanted = true
        }
        self.committingControls = [:]
        if self.draft.isEmpty { self.status.stringValue = failure?["message"] as? String ?? "The document is busy. Try again shortly." }
        else {
          self.deferredRetry = Date().addingTimeInterval(1)
          self.status.stringValue = "The document is still busy. Your mixer change is kept and is saved when it is free · Reload discards it."
        }
        self.refreshStrips(); return
      }
      self.loading = false; self.controlRequestInFlight = false
      let committing = self.committingControls; self.committingControls = [:]
      guard let result = reply["result"] as? [String: Any] else {
        if resync { done([:]); return }
        let abandoned = Set(committing.keys).union(self.draft.keys)
        self.draft = [:]; self.commitWanted = false; self.deferredRetry = .distantFuture
        self.status.stringValue = failure?["message"] as? String ?? "Mixer edit failed. Reload and try again."
        self.showSelected(); self.refreshStrips(); self.resyncEngine(abandoned); return
      }
      self.revision = result["revision"] as? String ?? self.revision
      done(result["data"] as? [String: Any] ?? [:])
    }
  }
  func mutate(_ method: String, _ values: [String: Any]) {
    guard !loading, draft.isEmpty else { return }
    var p = values; p["expectedRevision"] = revision
    request(method, p) { data in
      if method == "mixer.bus.add" { self.selectedID = data["bus"] as? String }
      self.load()
    }
  }
  func control(_ key: String, value: Any, final: Bool) {
    guard selected != nil else { return }
    guard !loading || controlRequestInFlight else {
      status.stringValue = "Wait for the mixer to finish loading before adjusting controls."; showSelected(); refreshStrips(); return
    }
    draft[key] = value; commitWanted = commitWanted || final; refreshStrips(); sendControls()
  }
  func finishGesture() { guard !draft.isEmpty else { return }; commitWanted = true; sendControls() }
  private func sendControls() {
    guard !loading, !draft.isEmpty, let selectedID else { return }
    let values = draft, final = commitWanted
    var p = values; p["bus"] = selectedID; p["expectedRevision"] = revision; p["preview"] = !final
    if final { committingControls = values; draft = [:]; commitWanted = false }
    controlRequestInFlight = true
    request("mixer.bus.set", p) { _ in
      if final {
        if let index = self.buses.firstIndex(where: { $0["id"] as? String == selectedID }) {
          for (key, value) in values { self.buses[index][key] = value }
        }
        self.table.reloadData()
        self.refreshStrips()
        self.status.stringValue = "Mixer controls saved · one document Undo"
        if !self.draft.isEmpty { self.sendControls() } else { self.showSelected() }
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
    for control in controls { control.set((bus[control.key] as? NSNumber)?.doubleValue ?? (control.key == "width" ? 1 : 0)) }
    destinations = buses.filter { $0["kind"] as? String != "track" && $0["id"] as? String != selectedID }
    let targets = destinations.map { (title: $0["name"] as? String ?? "Bus", id: $0["id"] as? String ?? "") }
    fill(output, [(title: "Disconnected", id: "")] + targets, saved: bus["output"] as? String ?? "", sameBus: sameBus)
    fill(sendTarget, targets, saved: nil, sameBus: sameBus)
    output.isEnabled = bus["kind"] as? String != "master"
    let inserts = bus["inserts"] as? [String] ?? []
    fill(insert, inserts.map { id in (title: plugins.first { $0["id"] as? String == id }?["name"] as? String ?? "Unavailable plugin", id: id) }, saved: nil, sameBus: sameBus)
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
    let sends = bus["sends"] as? [[String: Any]] ?? []
    // A send has no ID of its own; its position in this bus's list, paired
    // with its destination, identifies it within one snapshot.
    let previousSend = sameBus && send.numberOfItems > 0 ? chosen(send) : nil
    fill(send, [(title: "New send", id: "")] + sends.enumerated().map { index, s in
      (title: buses.first { $0["id"] as? String == s["target"] as? String }?["name"] as? String ?? "Bus", id: "\(index):\(s["target"] as? String ?? "")")
    })
    if !choose(send, previousSend) { send.selectItem(at: 0) }
    showSend(refreshing: true)
  }
  func showMeters(_ meters: [[AnyHashable: Any]]) {
    strips.showMeters(meters)
    let current = meters.first { $0["bus"] as? String == selectedID }
    let left = (current?["left"] as? NSNumber)?.doubleValue ?? 0, right = (current?["right"] as? NSNumber)?.doubleValue ?? 0
    // This runs on every display tick: leave the views alone unless a level moved.
    guard left != meter.left || right != meter.right else { return }
    meter.left = left; meter.right = right; meter.needsDisplay = true
    let level = max(left, right)
    let text = level > 0.00001 ? String(format: "%.1f dB", 20 * log10(level)) : "−∞ dB"
    if peak.stringValue != text { peak.stringValue = text }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { buses.count }
  func tableView(_ tableView: NSTableView, shouldSelectRow row: Int) -> Bool { !loading && draft.isEmpty }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard buses.indices.contains(table.selectedRow), draft.isEmpty else { return }
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
  private func rename() {
    guard let selectedID, let color = Int(color.stringValue, radix: 16), (0...0xffffff).contains(color) else { status.stringValue = "Use a six-digit hexadecimal color."; return }
    mutate("mixer.bus.set", ["bus": selectedID, "name": name.stringValue, "color": color])
  }
  private func route() {
    guard let selectedID, selected?["kind"] as? String != "master" else { return }
    guard let target = chosen(output) else { return }
    guard target.isEmpty || destinations.contains(where: { $0["id"] as? String == target }) else {
      status.stringValue = "That destination is no longer available. Choose another output."; return
    }
    mutate("mixer.bus.set", ["bus": selectedID, "output": target.isEmpty ? NSNull() : target])
  }
  private func setTiming() {
    guard let selectedID, let value = Double(timing.stringValue), value.isFinite else { return }
    mutate("mixer.bus.set", ["bus": selectedID, "timingMS": value])
  }
  private func removeBus() {
    guard let selectedID else { return }; mutate("mixer.bus.remove", ["bus": selectedID])
  }
  private func setInserts(_ values: [String]) {
    guard let selectedID else { return }; mutate("mixer.bus.set", ["bus": selectedID, "inserts": values])
  }
  func openInsert(controls: Bool = false) {
    guard !loading, draft.isEmpty else { return }
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
  @objc private func selectSend() { showSend(refreshing: false) }
  // A refresh leaves the send fields alone unless the chosen send or its saved
  // settings changed; choosing a send in the popup always shows its settings.
  private func showSend(refreshing: Bool) {
    let current = chosenSend, key = "\(selectedID ?? ""):\(chosen(send) ?? "")", value = NSDictionary(dictionary: current?.value ?? [:])
    if refreshing, let shownSend, shownSend.key == key, shownSend.value.isEqual(value) { return }
    shownSend = (key, value)
    guard let current else { sendGain.stringValue = "-12"; preSend.state = .off; sendEnabled.state = .on; return }
    choose(sendTarget, current.value["target"] as? String)
    sendGain.stringValue = String(format: "%.12g", (current.value["gainDB"] as? NSNumber)?.doubleValue ?? -12)
    preSend.state = current.value["preFader"] as? Bool == true ? .on : .off; sendEnabled.state = current.value["enabled"] as? Bool == true ? .on : .off
  }
  private func setSend() {
    guard let selectedID, let target = chosen(sendTarget), destinations.contains(where: { $0["id"] as? String == target }),
      let gain = Double(sendGain.stringValue), gain.isFinite else { return }
    var values = selected?["sends"] as? [[String: Any]] ?? []
    let value: [String: Any] = ["target": target, "gainDB": gain, "preFader": preSend.state == .on, "enabled": sendEnabled.state == .on]
    if send.indexOfSelectedItem > 0 {
      guard let current = chosenSend else { status.stringValue = "That send changed. Choose it again."; return }
      values[current.index] = value
    } else { values.append(value) }
    mutate("mixer.sends.set", ["bus": selectedID, "sends": values])
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
