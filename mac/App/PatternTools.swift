import AppKit

final class PatternToolsPanel: NSView, NSTextFieldDelegate {
  typealias Reply = ([String: Any]) -> Void
  var onContext: (() -> (PatternModel, [String: Int]))?
  var onRequest: (([String: Any], @escaping Reply) -> Void)?
  let operation = NSPopUpButton(), scope = NSPopUpButton(), target = NSPopUpButton()
  let curve = NSPopUpButton(), filter = NSPopUpButton()
  let from = NSTextField(string: "4"), to = NSTextField(string: "64")
  let amount = NSTextField(string: "2"), seed = NSTextField(string: "1")
  let swap = NSButton(checkboxWithTitle: "Swap the two instruments", target: nil, action: nil)
  let allowLoss = NSButton(checkboxWithTitle: "Allow existing notes or fields to be discarded", target: nil, action: nil)
  let fieldNames = ["note", "instrument", "volume", "effect"]
  var fieldButtons = [NSButton]()
  let summary = Theme.label("Preview a change before applying it.", size: 12)
  let details = NSTextView()
  var previewButton: ActionButton!, applyButton: ActionButton!
  private var rows = [String: NSView]()
  private var prepared: [String: Any]?
  private var generation = 0
  private var requestInFlight = false
  private var previewModel = PatternModel([:])
  private let operationIDs = ["interpolate", "humanize", "randomize", "scale", "fill", "transpose", "remapInstrument", "reverse", "rotate", "expand", "shrink", "clear", "insertRows", "deleteRows"]
  private let scopeIDs = ["selection", "channel", "pattern", "song", "note-track"]
  private let targetIDs = ["volume", "panning", "note", "instrument", "effectParameter"]
  private let filterIDs = ["values", "notes", "all"]

  override init(frame: NSRect) {
    super.init(frame: frame)
    operation.addItems(withTitles: ["Interpolate", "Humanize", "Randomize", "Scale values", "Fill values", "Transpose", "Remap instrument", "Reverse rows", "Rotate rows", "Expand timing", "Shrink timing", "Clear fields", "Insert rows", "Delete rows"])
    scope.addItems(withTitles: ["Selection", "Current channel", "Current pattern", "All patterns (including unused)", "Current note track"])
    target.addItems(withTitles: ["Volume", "Panning", "Note", "Instrument", "Effect parameter"])
    curve.addItems(withTitles: ["Linear", "Exponential", "Logarithmic"])
    filter.addItems(withTitles: ["Existing values", "Rows with notes", "All cells"])
    filter.selectItem(at: 1)
    for popup in [operation, scope, target, curve, filter] {
      popup.target = self; popup.action = #selector(settingsChanged)
    }
    for (popup, width) in [(operation, 210.0), (scope, 290.0), (target, 150.0), (curve, 135.0), (filter, 160.0)] {
      popup.widthAnchor.constraint(greaterThanOrEqualToConstant: width).isActive = true
    }
    for field in [from, to, amount, seed] {
      field.delegate = self
      field.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
      field.fixed(width: 90)
    }
    for button in [swap, allowLoss] { button.target = self; button.action = #selector(settingsChanged) }
    for title in ["Notes", "Instruments", "Volume / pan", "Effects"] {
      let button = NSButton(checkboxWithTitle: title, target: self, action: #selector(settingsChanged))
      button.state = .on; fieldButtons.append(button)
    }
    func row(_ name: String, _ title: String, _ views: [NSView]) -> NSView {
      let label = Theme.label(title, size: 12, color: Theme.muted)
      label.fixed(width: 85)
      let view = stack(.horizontal, [label] + views, spacing: 10)
      rows[name] = view
      return view
    }
    previewButton = ActionButton("Preview") { [weak self] in self?.preview() }
    applyButton = ActionButton("Apply changes") { [weak self] in self?.apply() }
    applyButton.isEnabled = false
    summary.lineBreakMode = .byWordWrapping
    summary.maximumNumberOfLines = 3
    details.isEditable = false
    details.isRichText = false
    details.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
    details.textColor = Theme.text
    details.backgroundColor = Theme.bg
    details.textContainerInset = NSSize(width: 10, height: 10)
    details.setAccessibilityLabel("Pattern change preview")
    let scroll = NSScrollView()
    scroll.hasVerticalScroller = true
    scroll.documentView = details
    details.isVerticallyResizable = true
    details.isHorizontallyResizable = false
    details.minSize = NSSize(width: 0, height: 180)
    details.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
    details.autoresizingMask = [.width]
    details.textContainer?.widthTracksTextView = true
    scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
    let content = stack(.vertical, [
      Theme.label("Pattern tools", size: 19, weight: .semibold),
      row("scope", "Apply to", [scope]),
      row("operation", "Operation", [operation]),
      row("target", "Value", [target, filter]),
      row("range", "From / to", [from, to, curve]),
      row("amount", "Amount", [amount]),
      row("seed", "Seed", [seed, Theme.label("Same seed, same result", size: 11, color: Theme.muted)]),
      row("fields", "Fields", fieldButtons), swap, allowLoss,
      stack(.horizontal, [previewButton, applyButton], spacing: 12), summary, scroll,
    ], spacing: 12)
    content.stretchAcrossAxis()
    content.fill(self, inset: 20)
    settingsChanged()
  }
  required init?(coder: NSCoder) { fatalError() }

  @objc func settingsChanged() {
    invalidate()
    let op = operationIDs[operation.indexOfSelectedItem]
    let numeric = ["interpolate", "humanize", "randomize", "scale", "fill"].contains(op)
    rows["target"]?.isHidden = !numeric
    rows["range"]?.isHidden = !["interpolate", "randomize", "fill", "remapInstrument"].contains(op)
    to.isHidden = op == "fill"
    curve.isHidden = op != "interpolate"
    rows["amount"]?.isHidden = !["scale", "humanize", "transpose", "rotate", "expand", "shrink", "insertRows", "deleteRows"].contains(op)
    rows["seed"]?.isHidden = !["randomize", "humanize"].contains(op)
    rows["fields"]?.isHidden = !["clear", "reverse", "rotate", "expand", "shrink", "insertRows", "deleteRows"].contains(op)
    swap.isHidden = op != "remapInstrument"
    allowLoss.isHidden = !["expand", "shrink", "insertRows", "deleteRows"].contains(op)
  }
  func controlTextDidChange(_ notification: Notification) { invalidate() }
  func invalidate() {
    generation += 1
    prepared = nil
    applyButton.isEnabled = false
    details.string = ""
    summary.stringValue = "Preview a change before applying it."
  }
  private func number(_ field: NSTextField) throws -> Double {
    guard let n = Double(field.stringValue.trimmingCharacters(in: .whitespaces)), n.isFinite else {
      throw NSError(domain: "PatternTools", code: 1, userInfo: [NSLocalizedDescriptionKey: "Enter a valid number in every visible value field."])
    }
    return n
  }
  func parameters() throws -> [String: Any] {
    guard let (model, selection) = onContext?(), model.rows > 0, model.channels > 0 else {
      throw NSError(domain: "PatternTools", code: 1, userInfo: [NSLocalizedDescriptionKey: "Open a pattern first."])
    }
    previewModel = model
    let op = operationIDs[operation.indexOfSelectedItem]
    let selectedScope = scopeIDs[scope.indexOfSelectedItem]
    var p: [String: Any] = ["operation": op, "scope": selectedScope, "dryRun": true]
    if selectedScope != "song" { p["pattern"] = model.pattern }
    if selectedScope == "selection" {
      p["startRow"] = selection["startRow"] ?? 0
      p["rowCount"] = (selection["endRow"] ?? 0) - (selection["startRow"] ?? 0) + 1
      p["startChannel"] = selection["startChannel"] ?? 0
      p["channelCount"] = (selection["endChannel"] ?? 0) - (selection["startChannel"] ?? 0) + 1
    } else if selectedScope == "channel" { p["startChannel"] = selection["cursorChannel"] ?? 0 }
    else if selectedScope == "note-track" {
      guard let track = model.noteTrackByChannel[selection["cursorChannel"] ?? 0] else {
        throw NSError(domain: "PatternTools", code: 1, userInfo: [NSLocalizedDescriptionKey: "Select a column inside a grouped note track first."])
      }
      p["track"] = track.id
    }
    if ["clear", "reverse", "rotate", "expand", "shrink", "insertRows", "deleteRows"].contains(op) {
      p["fields"] = zip(fieldNames, fieldButtons).filter { $0.1.state == .on }.map { $0.0 }
    }
    if ["rotate", "expand", "shrink", "insertRows", "deleteRows", "transpose", "scale", "humanize"].contains(op) { p["amount"] = try number(amount) }
    if ["expand", "shrink", "insertRows", "deleteRows"].contains(op) { p["allowDataLoss"] = allowLoss.state == .on }
    if ["interpolate", "scale", "randomize", "humanize", "fill"].contains(op) {
      p["target"] = targetIDs[target.indexOfSelectedItem]
      p["only"] = filterIDs[filter.indexOfSelectedItem]
    }
    if ["interpolate", "randomize", "fill"].contains(op) { p["from"] = try number(from) }
    if ["interpolate", "randomize"].contains(op) { p["to"] = try number(to) }
    if op == "interpolate" { p["curve"] = ["linear", "exponential", "logarithmic"][curve.indexOfSelectedItem] }
    if ["randomize", "humanize"].contains(op) { p["seed"] = try number(seed) }
    if op == "remapInstrument" {
      p["fromInstrument"] = try number(from); p["toInstrument"] = try number(to); p["swap"] = swap.state == .on
    }
    return p
  }
  func preview() {
    guard !requestInFlight else { return }
    do {
      let p = try parameters()
      invalidate()
      send(p, applying: false)
    } catch { summary.stringValue = error.localizedDescription }
  }
  func apply() {
    guard !requestInFlight, var p = prepared else { return }
    p["dryRun"] = false
    send(p, applying: true)
  }
  private func send(_ p: [String: Any], applying: Bool) {
    guard let onRequest else { return }
    let token = generation
    requestInFlight = true
    previewButton.isEnabled = false
    applyButton.isEnabled = false
    summary.stringValue = applying ? "Applying changes…" : "Preparing preview…"
    onRequest(p) { [weak self] reply in
      guard let self else { return }
      self.requestInFlight = false
      self.previewButton.isEnabled = true
      guard token == self.generation else { return }
      if let error = reply["error"] as? [String: Any] {
        self.prepared = nil
        self.summary.stringValue = error["message"] as? String ?? "Could not prepare this change."
        return
      }
      guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any],
        let revision = result["revision"] as? String else { return }
      let count = data["changedCells"] as? Int ?? 0
      if applying {
        self.prepared = nil
        self.summary.stringValue = "Applied \(count) cell changes. Undo restores the whole operation."
        if result["playbackStopped"] as? Bool == true { self.summary.stringValue += " Playback stopped for this edit." }
      } else {
        var prepared = p
        prepared["expectedRevision"] = revision
        self.prepared = prepared
        self.applyButton.isEnabled = count > 0
        self.summary.stringValue = "\(count) cells will change."
        if data["previewTruncated"] as? Bool == true { self.summary.stringValue += " Showing the first 512 changes." }
        let changes = data["changes"] as? [[String: Any]] ?? []
        self.details.string = changes.map { self.describe($0) }.joined(separator: "\n")
      }
    }
  }
  private func describe(_ change: [String: Any]) -> String {
    let before = change["before"] as? [String: Int] ?? [:], after = change["after"] as? [String: Int] ?? [:]
    func note(_ n: Int) -> String {
      if n == 0 { return "empty" }; if n > 120 { return "off/cut" }
      return ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"][(n - 1) % 12] + "\((n - 1) / 12)"
    }
    func volume(_ c: [String: Int]) -> String {
      let command = c["volumeCommand"] ?? 0
      if command == 0 { return "empty" }
      let letter = previewModel.volumeLetters.indices.contains(command) ? previewModel.volumeLetters[command] : "V"
      return "\(letter)\(c["volume"] ?? 0)"
    }
    func effect(_ c: [String: Int]) -> String {
      let command = c["effect"] ?? 0
      let letter = previewModel.effectLetters.indices.contains(command) ? previewModel.effectLetters[command] : "?"
      return command == 0 ? "empty" : letter + String(format: "%02X", c["parameter"] ?? 0)
    }
    var changes = [String]()
    if before["note"] != after["note"] { changes.append("\(note(before["note"] ?? 0)) → \(note(after["note"] ?? 0))") }
    if before["instrument"] != after["instrument"] { changes.append("instrument \(before["instrument"] ?? 0) → \(after["instrument"] ?? 0)") }
    if before["volume"] != after["volume"] || before["volumeCommand"] != after["volumeCommand"] { changes.append("\(volume(before)) → \(volume(after))") }
    if before["effect"] != after["effect"] || before["parameter"] != after["parameter"] { changes.append("\(effect(before)) → \(effect(after))") }
    return String(format: "P%02d  %03d  Ch%02d  ", change["pattern"] as? Int ?? 0, change["row"] as? Int ?? 0, (change["channel"] as? Int ?? 0) + 1) + changes.joined(separator: " · ")
  }
}
