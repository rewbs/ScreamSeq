import AppKit

private func midiNoteNumber(_ text: String) -> Double? {
  let normalized = text.uppercased().replacingOccurrences(of: "♯", with: "#").replacingOccurrences(of: "♭", with: "B")
  let letters = Array(normalized)
  guard let first = letters.first, let pitch = ["C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11][String(first)] else { return nil }
  var position = 1, accidental = 0
  if letters.count > 1 && (letters[1] == "#" || letters[1] == "B") { accidental = letters[1] == "#" ? 1 : -1; position += 1 }
  guard let octave = Int(String(letters.dropFirst(position))), (-1...9).contains(octave) else { return nil }
  return Double((octave + 1) * 12 + pitch + accidental)
}

final class ParameterSlider: NSSlider {
  var changed: ((Double) -> Void)?
  var gesture: ((Bool) -> Void)?
  private var dragging=false,grabOffset:CGFloat=0,initialValue=0.0
  override var acceptsFirstResponder:Bool {isEnabled}
  override func mouseDown(with event: NSEvent) {
    guard isEnabled else{return}
    // NSSlider's cell uses a nested tracking loop. Keep the normal application
    // event loop running so asynchronous parameter replies, playback displays
    // and Undo grouping continue throughout a drag.
    guard sliderType == .linear,!isVertical,let cell=cell as? NSSliderCell else{gesture?(true);defer{gesture?(false)};super.mouseDown(with:event);return}
    window?.makeFirstResponder(self)
    let point=convert(event.locationInWindow,from:nil),thumb=cell.knobRect(flipped:isFlipped)
    grabOffset=thumb.contains(point) ? point.x-thumb.midX:0
    initialValue=doubleValue;dragging=true;gesture?(true);track(event)
  }
  override func mouseDragged(with event:NSEvent){if dragging{track(event)}else{super.mouseDragged(with:event)}}
  override func mouseUp(with event:NSEvent){guard dragging else{super.mouseUp(with:event);return};track(event);dragging=false;gesture?(false)}
  override func keyDown(with event:NSEvent){
    if dragging,event.keyCode==53 {doubleValue=initialValue;sendAction(action,to:target);dragging=false;gesture?(false);return}
    super.keyDown(with:event)
  }
  private func track(_ event:NSEvent){
    guard let cell=cell as? NSSliderCell,maxValue>minValue else{return}
    let thumb=cell.knobRect(flipped:isFlipped),bar=cell.barRect(flipped:isFlipped)
    let width=max(1,bar.width-thumb.width),x=convert(event.locationInWindow,from:nil).x-grabOffset
    var fraction=Double(max(0,min(1,(x-bar.minX-thumb.width/2)/width)))
    if allowsTickMarkValuesOnly,numberOfTickMarks>1{fraction=(fraction*Double(numberOfTickMarks-1)).rounded()/Double(numberOfTickMarks-1)}
    let value=minValue+fraction*(maxValue-minValue)
    if abs(value-doubleValue)>max(1,abs(value))*1e-12 {doubleValue=value;sendAction(action,to:target)}
  }
  init(value: Double, min: Double, max: Double, changed: @escaping (Double) -> Void) {
    super.init(frame: .zero)
    minValue = min
    maxValue = max
    doubleValue = value
    self.changed = changed
    target = self
    action = #selector(update)
    isContinuous = true
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func update() { changed?(doubleValue) }
}
final class ParameterValueField: NSTextField, NSTextFieldDelegate {
  var commit: ((String) -> Void)?
  var editingText: (() -> String)?
  init() {
    super.init(frame: .zero)
    font = .monospacedSystemFont(ofSize: 12, weight: .regular)
    textColor = Theme.accent
    backgroundColor = Theme.raised
    drawsBackground = true
    isBezeled = true
    bezelStyle = .roundedBezel
    delegate = self
    target = self
    action = #selector(submit)
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func submit() { commit?(stringValue) }
  func prepareEditing() { if currentEditor() == nil, let text = editingText?() { stringValue = text } }
  override func becomeFirstResponder() -> Bool { prepareEditing(); return super.becomeFirstResponder() }
  override func mouseDown(with event: NSEvent) { prepareEditing(); super.mouseDown(with: event) }
  // This notification arrives after the first keystroke. Replacing stringValue
  // here used to discard that digit (typing 12 became 2).
  func controlTextDidBeginEditing(_ notification: Notification) {}
  func controlTextDidEndEditing(_ notification: Notification) { submit() }
}
final class PluginParameterRow: NSTableCellView {
  let name = Theme.label("", size: 12)
  let reading = ParameterValueField()
  private(set) lazy var readingWidth = reading.widthAnchor.constraint(equalToConstant: 105)
  let slider = ParameterSlider(value: 0, min: 0, max: 1) { _ in }
  let actions = ActionMenuButton("…") { NSMenu() }
  override init(frame: NSRect) {
    super.init(frame: frame)
    name.widthAnchor.constraint(greaterThanOrEqualToConstant: 80).isActive = true
    name.widthAnchor.constraint(lessThanOrEqualToConstant: 180).isActive = true
    name.setContentHuggingPriority(.defaultLow, for: .horizontal)
    name.lineBreakMode = .byTruncatingTail
    reading.translatesAutoresizingMaskIntoConstraints = false
    readingWidth.isActive = true
    slider.setContentHuggingPriority(.defaultLow, for: .horizontal)
    actions.fixed(width: 28)
    stack(.horizontal, [name, slider, reading, actions], spacing: 8).fill(self, inset: 5)
  }
  required init?(coder: NSCoder) { fatalError() }
}

final class DynamicsMeterView: NSView {
  private(set) var active = false
  private(set) var reduction = [0.0, 0.0], detector = [-160.0, -160.0]
  override init(frame: NSRect) {
    super.init(frame: frame)
    fixed(height: 58)
    isHidden = true
    setAccessibilityElement(true)
    setAccessibilityRole(.levelIndicator)
    setAccessibilityLabel("Dynamics detector and gain reduction")
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ values: [AnyHashable: Any]) {
    isHidden = values["supported"] as? Bool != true
    guard !isHidden else { return }
    let running = values["active"] as? Bool ?? false
    let r = (values["reductionDB"] as? [Double]) ?? [0, 0]
    let d = (values["detectorDB"] as? [Double]) ?? [-160, -160]
    guard r.count == 2 && d.count == 2 && (r + d).allSatisfy({ $0.isFinite }) else { return }
    if running != active || r != reduction || d != detector {
      active = running; reduction = r; detector = d; needsDisplay = true
    }
    setAccessibilityValue(String(format: "Gain reduction left %.1f, right %.1f decibels. Detector left %.1f, right %.1f dBFS. %@", r[0], r[1], d[0], d[1], running ? "Running" : "Stopped"))
  }
  override func draw(_ dirtyRect: NSRect) {
    Theme.bg.setFill(); NSBezierPath(roundedRect: bounds, xRadius: 6, yRadius: 6).fill()
    let value = String(format: "Gain reduction  L %.1f · R %.1f dB", reduction[0], reduction[1])
    let input = active ? String(format: "Detector  L %.1f · R %.1f dBFS", detector[0], detector[1]) : "Stopped"
    let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .medium), .foregroundColor: Theme.text]
    (value as NSString).draw(at: NSPoint(x: 10, y: 35), withAttributes: attributes)
    let inputWidth = (input as NSString).size(withAttributes: attributes).width
    (input as NSString).draw(at: NSPoint(x: max(10, bounds.width - inputWidth - 10), y: 35), withAttributes: attributes)
    for channel in 0..<2 {
      let track = NSRect(x: 10, y: CGFloat(19 - channel * 10), width: max(0, bounds.width - 20), height: 5)
      Theme.muted.withAlphaComponent(0.25).setFill(); NSBezierPath(roundedRect: track, xRadius: 2, yRadius: 2).fill()
      let fill = NSRect(x: track.minX, y: track.minY, width: track.width * min(1, max(0, reduction[channel]) / 48), height: track.height)
      Theme.accent.setFill(); NSBezierPath(roundedRect: fill, xRadius: 2, yRadius: 2).fill()
    }
  }
}

final class PluginEditor: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate
{
  var selected = 0, plugins = [[String: Any]]()
  private var parameterGeneration: UInt64 = 0
  var parameterValues = [[AnyHashable: Any]](), filteredValues = [[AnyHashable: Any]]()
  let search = NSSearchField()
  let dynamicsMeter = DynamicsMeterView(frame: .zero)
  private var meterTime = -Double.infinity
  let picker = NSPopUpButton(), parameters = NSTableView(),
    note = Theme.label(
      "Use the Mixer to place effects on tracks, groups and returns.", size: 11, color: Theme.muted)
  let record = NSButton(checkboxWithTitle: "Record automation", target: nil, action: nil)
  let assignment = NSPopUpButton()
  var instrumentsButton: ActionButton!, createInstrumentButton:ActionButton!
  var onNewInstrument:((Int)->Void)?
  var onInstruments: ((Int) -> Void)?
  var onOpen: ((Int) -> Void)?, onAssign: ((Int, Int) -> Void)?
  var undoButton: ActionButton!, redoButton: ActionButton!
  var openButton: ActionButton!
  var onSelect: ((Int) -> Void)?, onAdd: (() -> Void)?, onRemove: ((Int) -> Void)?,
    onMove: ((Int, Int) -> Void)?, onBypass: ((Int, Bool) -> Void)?,
    onParameter: ((Int, Int, Double, Bool) -> Void)?, onClearAutomation: (() -> Void)?
  var onUndo: (() -> Void)?, onRedo: (() -> Void)?
  var onAddBuiltIn: (() -> Void)?
  var onPorts: ((Int) -> Void)?
  var onPrograms: ((Int) -> Void)?
  var programsButton: ActionButton!
  var onSavePreset: ((Int) -> Void)?, onLoadPreset: ((Int) -> Void)?
  var savePresetButton: ActionButton!, loadPresetButton: ActionButton!
  var onActivity:(()->Void)?
  var onPatternAutomation: (() -> Void)?
  let rack = PluginRack(frame: .zero)
  var onGesture: ((Bool) -> Void)?, onGraph: ((String) -> Void)?
  var refreshGeneration = 0
  var onAutomate: ((String, Int) -> Void)?, onInspectParameter: ((String, Int) -> Void)?
  var onReorder: ((String, String?, String?) -> Void)?
  private var instrumentActions: NSStackView!
  private var rackBuses = [[String: Any]]()
  var selectedIdentity: String? { plugins.indices.contains(selected) ? plugins[selected]["instanceID"] as? String : nil }
  override init(frame: NSRect) {
    super.init(frame: frame)
    undoButton = ActionButton("Undo") { [weak self] in self?.onUndo?() }
    redoButton = ActionButton("Redo") { [weak self] in self?.onRedo?() }
    picker.target = self; picker.action = #selector(selectPlugin)
    assignment.target = self; assignment.action = #selector(assignInstrument)
    openButton = ActionButton("Open interface…") { [weak self] in guard let self else { return }; self.onOpen?(self.selected) }
    instrumentsButton = ActionButton("Assign instruments…") { [weak self] in guard let self else { return }; self.onInstruments?(self.selected) }
    createInstrumentButton = ActionButton("New trigger instrument…", prominent: true) { [weak self] in guard let self else { return }; self.onNewInstrument?(self.selected) }
    programsButton = ActionButton("Programs…") { [weak self] in guard let self else { return }; self.onPrograms?(self.selected) }
    savePresetButton = ActionButton("Save preset…") { [weak self] in guard let self else { return }; self.onSavePreset?(self.selected) }
    loadPresetButton = ActionButton("Load preset…") { [weak self] in guard let self else { return }; self.onLoadPreset?(self.selected) }
    rack.onSelect = { [weak self] id in guard let self, let index = self.plugins.firstIndex(where: { $0["instanceID"] as? String == id }) else { return }; self.selected = index; self.onSelect?(index) }
    savePresetButton.isEnabled = false; loadPresetButton.isEnabled = false
    rack.onOpen = { [weak self] id in self?.act(id) { self?.onOpen?($0) } }
    rack.onBypass = { [weak self] id, bypass in self?.act(id) { self?.onBypass?($0, bypass) } }
    rack.onRemove = { [weak self] id in self?.act(id) { self?.onRemove?($0) } }
    rack.onDrop = { [weak self] id, before, owner in self?.onReorder?(id, before, owner) }
    rack.menuForItem = { [weak self] id in self?.actionMenu(id) ?? NSMenu() }
    search.placeholderString = "Find a parameter"; search.delegate = self; search.setAccessibilityLabel("Find plugin parameter")
    parameters.headerView = nil; parameters.rowHeight = 40; parameters.backgroundColor = Theme.bg
    parameters.selectionHighlightStyle = .none; parameters.dataSource = self; parameters.delegate = self
    parameters.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle; parameters.setAccessibilityLabel("Plugin parameters")
    let column = NSTableColumn(identifier: .init("parameter")); column.resizingMask = .autoresizingMask; parameters.addTableColumn(column)
    let scroll = verticalScrollView(); scroll.documentView = parameters; parameters.autoresizingMask = [.width]
    let more = ActionMenuButton { [weak self] in self?.actionMenu() ?? NSMenu() }
    instrumentActions = stack(.horizontal, [createInstrumentButton!, instrumentsButton!, NSView()], spacing: 8)
    let title = stack(.horizontal, [Theme.label("Plugins", size: 18, weight: .semibold), NSView(),
      ActionButton("Add plugin…", symbol: "plus", prominent: true) { [weak self] in self?.onAdd?() }, more], spacing: 8)
    let content = stack(.vertical, [title, note, rack, instrumentActions!, dynamicsMeter, search, scroll], spacing: 10)
    content.stretchAcrossAxis(); content.fill(self, inset: 12)
    update(model: PatternModel([:]), values: [])
  }
  private func act(_ identity: String, _ action: (Int) -> Void) {
    guard let index = plugins.firstIndex(where: { $0["instanceID"] as? String == identity }) else { return }; action(index)
  }
  func actionMenu(_ identity: String? = nil) -> NSMenu {
    let menu = NSMenu(title: "Plugin"); menu.autoenablesItems = false
    menu.addItem(ContextAction("Add built-in effect…") { [weak self] in self?.onAddBuiltIn?() })
    guard let id = identity ?? selectedIdentity, let slot = plugins.firstIndex(where: { $0["instanceID"] as? String == id }) else {
      for title in ["Open interface / controls", "Bypass / enable plugin", "Show in Graph…", "Audio buses…", "Programs…", "Save preset…", "Load preset…", "Automation envelopes…", "Parameter activity…", "New trigger instrument…", "Assign tracker instruments…", "Move earlier", "Move later", "Remove plugin"] {
        let item=ContextAction(title,enabled:false){};item.toolTip="Select a plugin in Plugin controls first";menu.addItem(item)
      }
      return menu
    }
    func action(_ name: String, _ body: @escaping (Int) -> Void) { menu.addItem(ContextAction(name) { [weak self] in self?.act(id, body) }) }
    menu.addItem(.separator())
    action("Open interface / controls", { [weak self] in self?.onOpen?($0) })
    action("Bypass / enable plugin", { [weak self] index in guard let self else { return }; self.onBypass?(index, !(self.plugins[index]["bypass"] as? Bool ?? false)) })
    menu.addItem(ContextAction("Show in Graph…") { [weak self] in self?.onGraph?(id) })
    action("Audio buses…", { [weak self] in self?.onPorts?($0) })
    action("Programs…", { [weak self] in self?.onPrograms?($0) })
    action("Save preset…", { [weak self] in self?.onSavePreset?($0) })
    action("Load preset…", { [weak self] in self?.onLoadPreset?($0) })
    menu.addItem(.separator())
    menu.addItem(ContextAction("Automation envelopes…") { [weak self] in self?.selected = slot; self?.onPatternAutomation?() })
    menu.addItem(ContextAction("Parameter activity…") { [weak self] in self?.selected = slot; self?.onActivity?() })
    for (title, handler) in [("New trigger instrument…", onNewInstrument), ("Assign tracker instruments…", onInstruments)] {
      menu.addItem(ContextAction(title,enabled:plugins[slot]["isInstrument"] as? Bool == true){[weak self] in self?.act(id){handler?($0)}})
    }
    menu.addItem(.separator())
    action("Move earlier", { [weak self] in self?.moveRack($0, -1) })
    action("Move later", { [weak self] in self?.moveRack($0, 1) })
    action("Remove plugin", { [weak self] in self?.onRemove?($0) })
    return menu
  }
  private func moveRack(_ slot: Int, _ direction: Int) {
    guard plugins.indices.contains(slot), let id = plugins[slot]["instanceID"] as? String,
      let index = rack.items.firstIndex(where: { $0["instanceID"] as? String == id }), rack.items.indices.contains(index + direction) else { return }
    let target = index + direction, owner = rack.items[target]["ownerID"] as? String
    let beforeIndex = direction > 0 ? target + 1 : target
    let before = rack.items.indices.contains(beforeIndex) && rack.items[beforeIndex]["ownerID"] as? String == owner ? rack.items[beforeIndex]["instanceID"] as? String : nil
    onReorder?(id, before, owner)
  }
  func updateRoutes(_ data: [String: Any]) { rackBuses = data["buses"] as? [[String: Any]] ?? []; updateRack() }
  private func updateRack() {
    let assigned = Set(rackBuses.flatMap { $0["inserts"] as? [String] ?? [] })
    var rows = [[String: Any]]()
    func append(_ plugin: [String: Any], bus: [String: Any]?) {
      var row = plugin; row["ownerID"] = bus?["id"]
      row["ownerName"] = plugin["isInstrument"] as? Bool == true ? "Instrument" : bus?["name"] as? String ?? "Master"
      rows.append(row)
    }
    for bus in rackBuses {
      for id in bus["inserts"] as? [String] ?? [] { if let plugin = plugins.first(where: { $0["instanceID"] as? String == id }) { append(plugin, bus: bus) } }
      if bus["kind"] as? String == "master" { for plugin in plugins where !assigned.contains(plugin["instanceID"] as? String ?? "") { append(plugin, bus: bus) } }
    }
    if rackBuses.isEmpty { for plugin in plugins { append(plugin, bus: nil) } }
    rack.update(rows, selected: selectedIdentity)
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func assignInstrument() {
    onAssign?(selected, assignment.selectedTag())
  }
  @objc func selectPlugin() {
    selected = picker.indexOfSelectedItem
    onSelect?(selected)
  }
  func update(model: PatternModel, values: [[AnyHashable: Any]], selectedSlot: Int? = nil) {
    dynamicsMeter.isHidden = true
    let identity = selectedIdentity
    plugins = model.nativePlugins
    if let selectedSlot { selected = selectedSlot }
    else if let identity, let index = plugins.firstIndex(where: { $0["instanceID"] as? String == identity }) { selected = index }
    undoButton.isEnabled = model.canUndoEffect
    redoButton.isEnabled = model.canRedoEffect
    selected = max(0, min(selected, plugins.count - 1))
    picker.removeAllItems()
    for (i, p) in plugins.enumerated() {
      picker.addItem(
        withTitle:
          "\(i+1). \(p["name"] ?? "Audio Unit")\((p["bypass"] as? Bool ?? false) ? " · bypassed":"")"
      )
    }
    if !plugins.isEmpty { picker.selectItem(at: selected) }
    note.stringValue =
      model.pluginError.isEmpty
      ? "Built-in · AU · VST3  ·  \(model.automationPoints) automation points" : model.pluginError
    assignment.removeAllItems()
    assignment.addItem(withTitle: "Unassigned")
    assignment.lastItem?.tag = 0
    for instrument in model.instruments {
      let index = instrument["index"] as? Int ?? 0
      assignment.addItem(withTitle: "\(index). \(instrument["name"] as? String ?? "Instrument")")
      assignment.lastItem?.tag = index
    }
    let chosen = plugins.isEmpty ? [:] : plugins[selected]
    programsButton.isEnabled = !plugins.isEmpty
    savePresetButton.isEnabled = !plugins.isEmpty; loadPresetButton.isEnabled = !plugins.isEmpty
    let builtIn = chosen["format"] as? String == "Built-in"
    openButton.isEnabled = !plugins.isEmpty && !builtIn
    openButton.title = builtIn ? "Controls below" : "Open interface…"
    assignment.selectItem(withTag: chosen["instrument"] as? Int ?? 0)
    assignment.isEnabled = chosen["isInstrument"] as? Bool ?? false
    instrumentsButton.isEnabled = assignment.isEnabled
    createInstrumentButton.isEnabled=assignment.isEnabled
    instrumentActions.isHidden = !assignment.isEnabled
    updateRack()
    let count = (chosen["instrumentAssignments"] as? [[String: Any]])?.count ?? ((chosen["instrument"] as? Int ?? 0) > 0 ? 1 : 0)
    instrumentsButton.title = count > 0 ? "Assigned instruments (\(count))…" : "Assign tracker instruments…"
    if !assignment.isEnabled {
      assignment.removeAllItems()
      assignment.addItem(withTitle: "Effect · route in Mixer")
    }
    parameterValues = plugins.isEmpty ? [] : values
    filterParameters()
    if plugins.isEmpty { note.stringValue = "Add a built-in effect, AU or VST3 to begin." }
  }
  func controlTextDidChange(_ notification: Notification) { filterParameters() }
  func showMeters(_ values: [AnyHashable: Any]) { dynamicsMeter.update(values) }
  func meterRefreshDue(_ time: Double) -> Bool {
    if time - meterTime < 0.05 { return false }
    meterTime = time; return true
  }
  func filterParameters() {
    parameterGeneration &+= 1
    let query = search.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
    filteredValues =
      query.isEmpty
      ? parameterValues
      : parameterValues.filter {
        ($0["name"] as? String ?? "").localizedCaseInsensitiveContains(query)
      }
    parameters.reloadData()
  }
  func numberOfRows(in tableView: NSTableView) -> Int { filteredValues.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView?
  {
    guard filteredValues.indices.contains(row) else { return nil }
    let identifier = NSUserInterfaceItemIdentifier("parameter-row")
    let view =
      tableView.makeView(withIdentifier: identifier, owner: self) as? PluginParameterRow
      ?? PluginParameterRow(frame: .zero)
    view.identifier = identifier
    let parameter = filteredValues[row]
    let generation = parameterGeneration, slot = selected
    let id = parameter["id"] as? Int ?? 0
    let identity = selectedIdentity
    let name = parameter["name"] as? String ?? "Parameter"
    let value = parameter["value"] as? Double ?? 0
    view.name.stringValue = name
    view.name.toolTip = name
    view.actions.setAccessibilityLabel(name + " actions")
    view.actions.actions = { [weak self] in
      let menu = NSMenu(title: name); menu.autoenablesItems = false
      guard let self, let identity else { return menu }
      menu.addItem(ContextAction("Automate this parameter…") { [weak self] in self?.onAutomate?(identity, id) })
      menu.addItem(ContextAction("Inspect parameter activity…") { [weak self] in self?.onInspectParameter?(identity, id) })
      return menu
    }
    let minimum = parameter["min"] as? Double ?? 0
    let maximum = parameter["max"] as? Double ?? 1
    let choices = parameter["choices"] as? [String] ?? []
    let step = parameter["step"] as? Double ?? 0
    let validRange = minimum.isFinite && maximum.isFinite && maximum >= minimum && (maximum - minimum).isFinite
    let validValue = validRange && value.isFinite && value >= minimum && value <= maximum && step.isFinite && step >= 0
    // Vendor metadata is not trusted. Neither AppKit sliders nor integer
    // conversions can accept NaN/infinity; don't turn invalid values into edits.
    let logSpan = minimum > 0 && maximum > minimum ? log(maximum) - log(minimum) : 0
    let logarithmic = validRange && parameter["displayScale"] as? String == "logarithmic" && logSpan > 0 && choices.isEmpty
    let toSlider: (Double) -> Double = { logarithmic ? (log($0) - log(minimum)) / logSpan : $0 }
    let fromSlider: (Double) -> Double = { logarithmic ? exp(log(minimum) + $0 * logSpan) : $0 }
    view.slider.minValue = logarithmic || !validRange ? 0 : minimum
    view.slider.maxValue = logarithmic || !validRange ? 1 : maximum
    view.slider.doubleValue = validValue ? toSlider(value) : view.slider.minValue
    view.slider.isEnabled = validValue && maximum > minimum
    view.reading.isEditable = validValue && choices.isEmpty
    let choiceWidth = choices.map { ($0 as NSString).size(withAttributes: [.font: view.reading.font ?? NSFont.systemFont(ofSize: 12)]).width + 18 }.max() ?? 105
    view.readingWidth.constant = min(260, max(105, ceil(choiceWidth)))
    view.reading.setAccessibilityLabel(name + " value")
    view.slider.numberOfTickMarks = choices.count
    view.slider.allowsTickMarkValuesOnly = !choices.isEmpty
    view.slider.setAccessibilityLabel(name)
    let label = parameter["unitLabel"] as? String ?? ""
    view.reading.placeholderString = label == "MIDI" ? "C4 or 60" : nil
    let reading: (Double) -> String = { value in
      guard value.isFinite else { return "Unavailable" }
      if let index = Int(exactly: value.rounded()) {
        if choices.indices.contains(index) { return choices[index] }
        if label == "MIDI", (0...127).contains(index) {
          let names = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"]
          return "\(names[index % 12])\(index / 12 - 1) (\(index))"
        }
      }
      let format = step >= 1 ? "%.0f" : label == "Hz" ? (value.rounded() == value ? "%.0f" : "%.2f") : "%.3g"
      return String(format: format, value) + (label.isEmpty ? "" : " " + label)
    }
    view.reading.stringValue = reading(value)
    view.reading.toolTip = validValue ? reading(value) : "The plugin reported an invalid value or range. This control is unavailable."
    view.reading.lineBreakMode = .byTruncatingTail
    var currentValue = value
    let apply: (Double) -> Void = { [weak self, weak view] requested in
      guard let self, self.selected == slot, self.parameterGeneration == generation, validValue, requested.isFinite else { return }
      var value = max(minimum, min(maximum, requested))
      if step > 0 {
        let steps = ((value - minimum) / step).rounded()
        let snapped = minimum + steps * step
        // A step smaller than Double precision need not overflow the control.
        if snapped.isFinite { value = max(minimum, min(maximum, snapped)) }
      }
      view?.slider.doubleValue = toSlider(value)
      view?.reading.stringValue = reading(value)
      view?.reading.toolTip = reading(value)
      if let index = self.parameterValues.firstIndex(where: { ($0["id"] as? Int) == id }) {
        self.parameterValues[index]["value"] = value
      }
      if let index = self.filteredValues.firstIndex(where: { ($0["id"] as? Int) == id }) {
        self.filteredValues[index]["value"] = value
      }
      if value != currentValue {
        currentValue = value
        self.onParameter?(self.selected, id, value, self.record.state == .on)
      }
    }
    view.slider.gesture = { [weak self] in self?.onGesture?($0) }
    view.slider.changed = { if $0.isFinite { apply(fromSlider($0)) } }
    view.reading.editingText = { String(currentValue) }
    view.reading.commit = { [weak view] text in
      if text == reading(currentValue) { return }
      var text = text.trimmingCharacters(in: .whitespacesAndNewlines)
      if !label.isEmpty && text.hasSuffix(label) { text = String(text.dropLast(label.count)).trimmingCharacters(in: .whitespaces) }
      let parsed = Double(text) ?? (label == "MIDI" ? midiNoteNumber(text) : nil)
      guard choices.isEmpty, let number = parsed, number.isFinite, number >= minimum, number <= maximum else {
        view?.reading.stringValue = reading(currentValue)
        return
      }
      apply(number)
    }
    return view
  }
}
