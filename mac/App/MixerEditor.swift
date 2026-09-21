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
    slider.doubleValue = number * scale; value.stringValue = String(format: "%.2f", number * scale)
  }
  @objc private func changed() {
    value.stringValue = String(format: "%.2f", slider.doubleValue)
    onChange?(key, slider.doubleValue / scale, !slider.trackingGesture)
  }
  @objc private func entered() {
    guard let number = Double(value.stringValue), number.isFinite,
      number >= slider.minValue, number <= slider.maxValue else { set(slider.doubleValue / scale); return }
    slider.doubleValue = number; onChange?(key, number / scale, true)
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
        ActionButton("Reload") { [weak self] in self?.load() }]),
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
  func synchronize(_ token: String) {
    if token != revision && !loading && draft.isEmpty { load() }
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
  private func request(_ method: String, _ params: [String: Any], done: @escaping ([String: Any]) -> Void) {
    guard !loading, let onRequest else { return }; loading = true
    onRequest(method, params) { [weak self] reply in
      guard let self else { return }; self.loading = false; self.controlRequestInFlight = false; self.committingControls = [:]
      guard let result = reply["result"] as? [String: Any] else {
        self.draft = [:]; self.commitWanted = false
        self.status.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Mixer edit failed. Reload and try again."
        self.showSelected(); self.refreshStrips(); return
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
  private func showSelected() {
    guard let bus = selected else { return }
    heading.stringValue = bus["name"] as? String ?? "Mixer"
    name.stringValue = bus["name"] as? String ?? ""; color.stringValue = String(format: "%06X", bus["color"] as? Int ?? 0)
    timing.doubleValue = (bus["timingMS"] as? NSNumber)?.doubleValue ?? 0
    timing.isEnabled = bus["kind"] as? String == "track"
    mute.state = bus["mute"] as? Bool == true ? .on : .off; solo.state = bus["solo"] as? Bool == true ? .on : .off
    for control in controls { control.set((bus[control.key] as? NSNumber)?.doubleValue ?? (control.key == "width" ? 1 : 0)) }
    destinations = buses.filter { $0["kind"] as? String != "track" && $0["id"] as? String != selectedID }
    for picker in [output, sendTarget] { picker.removeAllItems(); picker.addItems(withTitles: destinations.map { $0["name"] as? String ?? "Bus" }) }
    output.insertItem(withTitle:"Disconnected",at:0);output.selectItem(at:0)
    if let index = destinations.firstIndex(where: { $0["id"] as? String == bus["output"] as? String }) { output.selectItem(at: index+1) }
    output.isEnabled = bus["kind"] as? String != "master"
    let inserts = bus["inserts"] as? [String] ?? []
    insert.removeAllItems(); insert.addItems(withTitles: inserts.map { id in plugins.first { $0["id"] as? String == id }?["name"] as? String ?? "Unavailable plugin" })
    effects = plugins.filter { $0["instrument"] as? Bool != true }
    effect.removeAllItems(); effect.addItems(withTitles: effects.map { $0["name"] as? String ?? "Effect" })
    instruments = plugins.filter { $0["instrument"] as? Bool == true }.flatMap { plugin -> [[String: Any]] in
      let ports = plugin["audioBuses"] as? [[String: Any]] ?? [["index": 0, "direction": "output", "active": true]]
      return ports.filter { $0["direction"] as? String == "output" && $0["active"] as? Bool == true }.map { port in
        var item = plugin; item["output"] = port["index"]; item["outputName"] = port["name"]; return item
      }
    }
    source.removeAllItems(); source.addItems(withTitles: instruments.map { p in
      let output = p["output"] as? Int ?? 0
      let target = sources.first { $0["plugin"] as? String == p["id"] as? String && ($0["output"] as? Int ?? 0) == output }?["target"] as? String
      let destination = buses.first { $0["id"] as? String == target }?["name"] as? String ?? (output == 0 ? "Master" : "Unrouted")
      return "\(p["name"] as? String ?? "Instrument") · out \(output + 1) → \(destination)"
    })
    let sends = bus["sends"] as? [[String: Any]] ?? []
    send.removeAllItems(); send.addItem(withTitle: "New send")
    send.addItems(withTitles: sends.map { s in buses.first { $0["id"] as? String == s["target"] as? String }?["name"] as? String ?? "Bus" })
    selectSend()
  }
  func showMeters(_ meters: [[AnyHashable: Any]]) {
    strips.showMeters(meters)
    let current = meters.first { $0["bus"] as? String == selectedID }
    meter.left = (current?["left"] as? NSNumber)?.doubleValue ?? 0
    meter.right = (current?["right"] as? NSNumber)?.doubleValue ?? 0; meter.needsDisplay = true
    let level = max(meter.left, meter.right)
    peak.stringValue = level > 0.00001 ? String(format: "%.1f dB", 20 * log10(level)) : "−∞ dB"
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
    let index=output.indexOfSelectedItem-1
    guard index == -1 || destinations.indices.contains(index) else{return}
    mutate("mixer.bus.set", ["bus": selectedID, "output": index == -1 ? NSNull() : destinations[index]["id"]!])
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
    guard values.indices.contains(insert.indexOfSelectedItem) else { return }
    let id = values[insert.indexOfSelectedItem]
    guard plugins.contains(where: { $0["id"] as? String == id }) else { return }
    if controls { onPluginControls?(id) } else { onOpenPlugin?(id) }
  }
  private func addInsert() {
    guard effects.indices.contains(effect.indexOfSelectedItem), let id = effects[effect.indexOfSelectedItem]["id"] as? String else { return }
    var values = selected?["inserts"] as? [String] ?? []; values.append(id); setInserts(values)
  }
  private func removeInsert() {
    var values = selected?["inserts"] as? [String] ?? []; guard values.indices.contains(insert.indexOfSelectedItem) else { return }
    values.remove(at: insert.indexOfSelectedItem); setInserts(values)
  }
  private func moveInsert(_ direction: Int) {
    var values = selected?["inserts"] as? [String] ?? []; let index = insert.indexOfSelectedItem
    guard values.indices.contains(index), values.indices.contains(index + direction) else { return }
    values.swapAt(index, index + direction); setInserts(values)
  }
  @objc private func selectSend() {
    let values = selected?["sends"] as? [[String: Any]] ?? [], index = send.indexOfSelectedItem - 1
    guard values.indices.contains(index) else { sendGain.stringValue = "-12"; preSend.state = .off; sendEnabled.state = .on; return }
    let value = values[index]
    if let target = destinations.firstIndex(where: { $0["id"] as? String == value["target"] as? String }) { sendTarget.selectItem(at: target) }
    sendGain.doubleValue = (value["gainDB"] as? NSNumber)?.doubleValue ?? -12
    preSend.state = value["preFader"] as? Bool == true ? .on : .off; sendEnabled.state = value["enabled"] as? Bool == true ? .on : .off
  }
  private func setSend() {
    guard let selectedID, destinations.indices.contains(sendTarget.indexOfSelectedItem), let gain = Double(sendGain.stringValue), gain.isFinite else { return }
    var values = selected?["sends"] as? [[String: Any]] ?? []
    let target = destinations[sendTarget.indexOfSelectedItem]["id"] as! String
    let value: [String: Any] = ["target": target, "gainDB": gain, "preFader": preSend.state == .on, "enabled": sendEnabled.state == .on]
    let index = send.indexOfSelectedItem - 1
    if values.indices.contains(index) { values[index] = value } else { values.append(value) }
    mutate("mixer.sends.set", ["bus": selectedID, "sends": values])
  }
  private func removeSend() {
    guard let selectedID else { return }; var values = selected?["sends"] as? [[String: Any]] ?? []
    let index = send.indexOfSelectedItem - 1; guard values.indices.contains(index) else { return }
    values.remove(at: index); mutate("mixer.sends.set", ["bus": selectedID, "sends": values])
  }
  private func configureInstrument() {
    guard instruments.indices.contains(source.indexOfSelectedItem), let slot = instruments[source.indexOfSelectedItem]["slot"] as? Int else { return }
    onConfigurePlugin?(slot)
  }
  private func routeInstrument() {
    guard let selectedID, instruments.indices.contains(source.indexOfSelectedItem) else { return }
    mutate("mixer.instrument.route", ["plugin": instruments[source.indexOfSelectedItem]["id"]!, "target": selectedID, "output": instruments[source.indexOfSelectedItem]["output"] ?? 0])
  }
}
