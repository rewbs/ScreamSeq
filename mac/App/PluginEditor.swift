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
  func controlTextDidBeginEditing(_ notification: Notification) { if let text = editingText?() { stringValue = text } }
  func controlTextDidEndEditing(_ notification: Notification) { submit() }
}
final class PluginParameterRow: NSTableCellView {
  let name = Theme.label("", size: 12)
  let reading = ParameterValueField()
  private(set) lazy var readingWidth = reading.widthAnchor.constraint(equalToConstant: 105)
  let slider = ParameterSlider(value: 0, min: 0, max: 1) { _ in }
  override init(frame: NSRect) {
    super.init(frame: frame)
    name.fixed(width: 220)
    name.lineBreakMode = .byTruncatingTail
    reading.translatesAutoresizingMaskIntoConstraints = false
    readingWidth.isActive = true
    slider.setContentHuggingPriority(.defaultLow, for: .horizontal)
    stack(.horizontal, [name, slider, reading], spacing: 16).fill(self, inset: 5)
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
  var instrumentsButton: ActionButton!
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
  var onPatternAutomation: (() -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    picker.fixed(width: 300)
    picker.target = self
    picker.action = #selector(selectPlugin)
    undoButton = ActionButton("Undo effect change") { [weak self] in self?.onUndo?() }
    redoButton = ActionButton("Redo") { [weak self] in self?.onRedo?() }
    let title = stack(
      .horizontal,
      [
        Theme.label("Plugins", size: 20, weight: .semibold), NSView(),
        ActionButton("Add built-in…") { [weak self] in self?.onAddBuiltIn?() },
        ActionButton("Add plugin…", symbol: "plus") { [weak self] in self?.onAdd?() },
      ], spacing: 14)
    let controls = stack(
      .horizontal,
      [
        picker,
        ActionButton("↑") { [weak self] in
          guard let self else { return }
          self.onMove?(self.selected, -1)
        },
        ActionButton("↓") { [weak self] in
          guard let self else { return }
          self.onMove?(self.selected, 1)
        },
        ActionButton("Bypass") { [weak self] in
          guard let self, self.selected < self.plugins.count else { return }
          self.onBypass?(self.selected, !(self.plugins[self.selected]["bypass"] as? Bool ?? false))
        },
        ActionButton("Remove") { [weak self] in
          guard let self else { return }
          self.onRemove?(self.selected)
        },
        NSView(),
      ], spacing: 10)
    assignment.target = self
    assignment.action = #selector(assignInstrument)
    assignment.setAccessibilityLabel("Tracker instrument assignment")
    openButton = ActionButton("Open interface…") { [weak self] in
      guard let self else { return }; self.onOpen?(self.selected)
    }
    instrumentsButton = ActionButton("Assign tracker instruments…") { [weak self] in guard let self else { return }; self.onInstruments?(self.selected) }
    instrumentsButton.isEnabled = false
    let routing = stack(
      .horizontal,
      [
        openButton!,
        ActionButton("Audio buses…") { [weak self] in guard let self else { return }; self.onPorts?(self.selected) },
        instrumentsButton!, NSView(),
        ActionButton("Pattern automation…") { [weak self] in self?.onPatternAutomation?() },
      ], spacing: 12)
    let automation = stack(
      .horizontal,
      [
        record, NSView(), undoButton!, redoButton!,
        ActionButton("Clear automation") { [weak self] in self?.onClearAutomation?() },
      ], spacing: 16)
    search.placeholderString = "Find a parameter"
    search.delegate = self
    search.setAccessibilityLabel("Find plugin parameter")
    programsButton = ActionButton("Programs…") { [weak self] in guard let self else { return }; self.onPrograms?(self.selected) }
    programsButton.isEnabled = false
    savePresetButton = ActionButton("Save preset…") { [weak self] in guard let self else { return }; self.onSavePreset?(self.selected) }
    loadPresetButton = ActionButton("Load preset…") { [weak self] in guard let self else { return }; self.onLoadPreset?(self.selected) }
    savePresetButton.isEnabled = false; loadPresetButton.isEnabled = false
    parameters.headerView = nil
    parameters.rowHeight = 44
    parameters.backgroundColor = Theme.bg
    parameters.selectionHighlightStyle = .none
    parameters.dataSource = self
    parameters.delegate = self
    parameters.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    parameters.setAccessibilityLabel("Plugin parameters")
    let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("parameter"))
    column.resizingMask = .autoresizingMask
    parameters.addTableColumn(column)
    let scroll = verticalScrollView()
    scroll.documentView = parameters
    parameters.autoresizingMask = [.width]
    let content = stack(
      .vertical,
      [
        title, note, controls, routing, automation, dynamicsMeter,
        stack(.horizontal, [search, programsButton!, savePresetButton!, loadPresetButton!]), scroll,
        Theme.label(
          "Plugins, instrument assignments, and automation are saved in .screamseq projects.\nPlugins run inside this app; a faulty plug-in can interrupt playback or crash it.",
          size: 11, color: Theme.muted),
      ], spacing: 18)
    content.alignment = .leading
    content.fill(self, inset: 24)
    for row in content.arrangedSubviews {
      row.widthAnchor.constraint(equalTo: content.widthAnchor).isActive = true
    }
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func assignInstrument() {
    onAssign?(selected, assignment.selectedTag())
  }
  @objc func selectPlugin() {
    selected = picker.indexOfSelectedItem
    onSelect?(selected)
  }
  func update(model: PatternModel, values: [[AnyHashable: Any]]) {
    dynamicsMeter.isHidden = true
    plugins = model.nativePlugins
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
    let name = parameter["name"] as? String ?? "Parameter"
    let value = parameter["value"] as? Double ?? 0
    view.name.stringValue = name
    view.name.toolTip = name
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
