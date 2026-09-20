import AppKit

struct PatternCommand {
  let command: Int, mask: Int, value: Int, suggested: Int
  let minimum: Int, maximum: Int
  let label: String, name: String, family: String, hint: String
  init(_ data: [String: Any]) {
    command = data["command"] as? Int ?? 0; mask = data["parameterMask"] as? Int ?? 0
    value = data["parameterValue"] as? Int ?? 0; suggested = data["suggestedParameter"] as? Int ?? 0
    minimum = data["minimum"] as? Int ?? 0; maximum = data["maximum"] as? Int ?? 255
    label = data["label"] as? String ?? "?"; name = data["name"] as? String ?? "Unknown"
    family = data["family"] as? String ?? "sound"; hint = data["description"] as? String ?? ""
  }
  var rgba: SIMD4<Float> {
    switch family {
    case "pitch": return SIMD4(0.54, 0.74, 1, 1)
    case "volume": return SIMD4(0.53, 0.83, 0.60, 1)
    case "panning": return SIMD4(0.96, 0.73, 0.42, 1)
    case "timing": return SIMD4(0.94, 0.58, 0.60, 1)
    default: return SIMD4(0.77, 0.57, 0.86, 1)
    }
  }
  var color: NSColor { let c = rgba; return NSColor(srgbRed: CGFloat(c.x), green: CGFloat(c.y), blue: CGFloat(c.z), alpha: 1) }
}

final class PatternCommandCatalog {
  let effects: [PatternCommand], volumes: [PatternCommand]
  private var effectLookup = [Int: PatternCommand](), volumeLookup = [Int: PatternCommand]()
  static let empty = PatternCommandCatalog([:])
  init(_ data: [String: Any]) {
    effects = (data["effect"] as? [[String: Any]] ?? []).map(PatternCommand.init)
    volumes = (data["volume"] as? [[String: Any]] ?? []).map(PatternCommand.init)
    for entry in effects { for digit in 0..<16 where (digit * 16 & entry.mask) == entry.value { effectLookup[entry.command * 16 + digit] = entry } }
    for entry in volumes { volumeLookup[entry.command] = entry }
  }
  func entry(command: Int, parameter: Int, volume: Bool = false) -> PatternCommand? {
    volume ? volumeLookup[command] : effectLookup[command * 16 + (parameter >> 4)]
  }
}

extension PatternView {
  var currentCommandHelp: String {
    let precise=model.notes(cursorRow,cursorChannel)
    if column<=2 && !precise.isEmpty {return "\(precise.count) precise note events · Return or double-click to edit fractional timing"}
    if column>=5 {return model.nativeCommand(cursorRow,cursorChannel,column-5)?.description ?? "Extra effect: press Return or double-click to edit a parameter command"}
    let cell = model.drawCell(cursorRow, cursorChannel)
    if cell.note == 251 || cell.note == 252 { return "Imported parameter-control note: these columns store a plugin parameter and value." }
    guard column >= 2 else { return "Z–M notes · Arrows navigate · Space plays" }
    let volume = column == 2, command = Int(volume ? cell.volumeCommand : cell.effect)
    let parameter = Int(volume ? cell.volume : cell.parameter)
    guard command != 0 else { return "Choose a command with Pattern → Command Picker…" }
    guard let entry = model.commands.entry(command: command, parameter: parameter, volume: volume) else { return "Unknown command for \(model.format); its stored value is preserved." }
    return "\(entry.name) · \(entry.hint)"
  }
}

final class PatternCommandPicker: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let columnPicker = NSPopUpButton(), search = NSSearchField(), table = NSTableView()
  let parameter = NSTextField(string: "00"), valueLabel = Theme.label("Parameter (hex)", size: 12)
  let target = Theme.label("", size: 12), detail = Theme.label("", size: 12), status = Theme.label("", size: 12, color: Theme.muted)
  var onContext: (() -> (PatternModel, Int, Int, Int))?
  var onRequest: (([String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  private(set) var captured = PatternModel([:]), row = 0, channel = 0, filtered = [PatternCommand](), pending = false
  private var suppressSelection = false
  var isVolume: Bool { columnPicker.indexOfSelectedItem == 1 }
  override init(frame: NSRect) {
    super.init(frame: frame)
    columnPicker.addItems(withTitles: ["Effect column", "Volume column"])
    columnPicker.target = self; columnPicker.action = #selector(changeColumn)
    columnPicker.fixed(width: 160)
    search.placeholderString = "Find a command, family or explanation"; search.delegate = self
    table.delegate = self; table.dataSource = self; table.rowHeight = 30; table.headerView = nil
    let command = NSTableColumn(identifier: .init("command")); command.width = 90; command.minWidth = 80; command.maxWidth = 90
    table.addTableColumn(command); table.addTableColumn(NSTableColumn(identifier: .init("name")))
    table.setAccessibilityLabel("Commands supported by this song format")
    let list = verticalScrollView(); list.documentView = table; list.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
    parameter.fixed(width: 80); parameter.setAccessibilityLabel("Command parameter")
    detail.maximumNumberOfLines = 4; detail.lineBreakMode = .byWordWrapping
    detail.heightAnchor.constraint(greaterThanOrEqualToConstant: 62).isActive = true
    status.maximumNumberOfLines = 2; status.lineBreakMode = .byWordWrapping
    let controls = stack(.horizontal, [valueLabel, parameter, ActionButton("Apply to cell") { [weak self] in self?.apply() }, NSView()])
    let top = stack(.horizontal, [columnPicker, search, ActionButton("Use cursor") { [weak self] in self?.capture() }])
    let content = stack(.vertical, [Theme.label("Pattern commands", size: 20, weight: .semibold), target, top, list, detail, controls, status], spacing: 12)
    content.stretchAcrossAxis(); content.fill(self, inset: 20)
  }
  required init?(coder: NSCoder) { fatalError() }
  func capture() {
    guard !pending, let context = onContext?() else { return }
    (captured, row, channel, _) = context
    columnPicker.selectItem(at: context.3 == 2 ? 1 : 0)
    search.stringValue = ""
    target.stringValue = "\(captured.format) · Pattern \(captured.pattern) · Row \(row) · Channel \(channel + 1)"
    status.stringValue = "Changes only this cell's selected command and value. One Undo step."
    reload(selectCurrent: true)
  }
  @objc func changeColumn() { reload(selectCurrent: true) }
  func controlTextDidChange(_ obj: Notification) { reload(selectCurrent: false) }
  func reload(selectCurrent: Bool) {
    valueLabel.stringValue = isVolume ? "Value (decimal)" : "Parameter (hex)"
    let query = search.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
    let source = isVolume ? captured.commands.volumes : captured.commands.effects
    filtered = source.filter { query.isEmpty || "\($0.label) \($0.name) \($0.family) \($0.hint)".localizedCaseInsensitiveContains(query) }
    suppressSelection = true; table.reloadData(); table.deselectAll(nil)
    let cell = captured.drawCell(row, channel)
    let code = Int(isVolume ? cell.volumeCommand : cell.effect), amount = Int(isVolume ? cell.volume : cell.parameter)
    let index = selectCurrent ? filtered.firstIndex(where: { $0.command == code && amount & $0.mask == $0.value }) : nil
    if !filtered.isEmpty { table.selectRowIndexes(IndexSet(integer: index ?? 0), byExtendingSelection: false) }
    suppressSelection = false; selectedCommand()
    if index != nil { parameter.stringValue = isVolume ? String(amount) : String(format: "%02X", amount) }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { filtered.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard filtered.indices.contains(row) else { return nil }
    let entry = filtered[row]
    let label = Theme.label(tableColumn?.identifier.rawValue == "command" ? entry.label : "\(entry.name) · \(entry.family)", size: 12, color: entry.color)
    label.toolTip = entry.hint; return label
  }
  func tableViewSelectionDidChange(_ notification: Notification) { if !suppressSelection { selectedCommand() } }
  private func selectedCommand() {
    guard filtered.indices.contains(table.selectedRow) else { detail.stringValue = "No matching commands."; return }
    let entry = filtered[table.selectedRow]
    detail.stringValue = "\(entry.name) — \(entry.hint)"
    parameter.stringValue = isVolume ? String(entry.suggested) : String(format: "%02X", entry.suggested)
  }
  func apply() {
    guard !pending, captured.editable, filtered.indices.contains(table.selectedRow), let onRequest else { return }
    let note = captured.drawCell(row, channel).note
    guard note != 251 && note != 252 else { status.stringValue = "This parameter-control note uses different column data. Choose an ordinary note cell."; return }
    let entry = filtered[table.selectedRow]
    guard let amount = Int(parameter.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), radix: isVolume ? 10 : 16), (entry.minimum...entry.maximum).contains(amount), amount & entry.mask == entry.value,
      entry.command != 0 || amount == 0 else {
      let range = isVolume ? "\(entry.minimum)…\(entry.maximum)" : String(format: "%02X…%02X", entry.minimum, entry.maximum)
      status.stringValue = "Enter \(range) matching the command prefix; clear uses zero."; return
    }
    let volume = isVolume
    let patch: [String: Any] = ["pattern": captured.pattern, "row": row, "channel": channel,
      volume ? "volumeCommand" : "effect": entry.command, volume ? "volume" : "parameter": amount]
    pending = true
    onRequest(["expectedRevision": captured.revisionToken, "cells": [patch]]) { response in
      self.pending = false
      if let error = response["error"] as? [String: Any] { self.status.stringValue = (error["message"] as? String ?? "Edit failed") + " Use cursor to reload."; return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String else { return }
      self.captured.revisionToken = revision
      var cell = self.captured.cell(self.row, self.channel)
      cell[volume ? 2 : 4] = UInt8(entry.command); cell[volume ? 3 : 5] = UInt8(amount)
      self.captured.replaceCell(self.row, self.channel, with: cell)
      self.status.stringValue = "Applied \(entry.name). Undo restores the previous cell."
    }
  }
}
