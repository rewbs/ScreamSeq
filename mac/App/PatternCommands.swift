import AppKit

struct PatternCommand {
  let command: Int, mask: Int, value: Int, suggested: Int
  let minimum: Int, maximum: Int
  let label: String, name: String, family: String, hint: String
  let nativeKind: String?
  var displayCode: String {
    if nativeKind != nil {return label}
    if command==0 {return ".."}
    return mask==0 ? "0"+String(label.prefix(1)) : String(label.prefix(2))
  }
  static let native: [PatternCommand] = [
    ["label":"PS","name":"Set plugin parameter","kind":"parameter-set","description":"Set a plugin parameter to a precise value at this row or a fractional row offset."],
    ["label":"PL","name":"Slide plugin parameter","kind":"parameter-slide","description":"Glide from the current parameter value to a precise target over a duration, with sample-resolution timing."],
    ["label":"BS","name":"Set pitch bend","kind":"pitch-set","description":"Set an absolute pitch offset in semitones. Native samples or a plugin MIDI pitch wheel."],
    ["label":"BL","name":"Slide pitch bend","kind":"pitch-slide","description":"Glide to an absolute pitch offset over a duration, with fractional row timing."]
  ].map { PatternCommand($0.merging(["family":"native"]){first,_ in first}) }
  init(_ data: [String: Any]) {
    nativeKind = data["kind"] as? String
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
    if column>=5 {return model.nativeCommand(cursorRow,cursorChannel,column-5)?.description ?? "PS / PL parameter · BS / BL pitch · ? find effects · Return edits this cell"}
    let cell = model.drawCell(cursorRow, cursorChannel)
    if cell.note == 251 || cell.note == 252 { return "Imported parameter-control note: these columns store a plugin parameter and value." }
    guard column >= 2 else { return "Z–M notes · Arrows navigate · Space plays" }
    let volume = column == 2, command = Int(volume ? cell.volumeCommand : cell.effect)
    let parameter = Int(volume ? cell.volume : cell.parameter)
    guard command != 0 else { return "? finds effects · Right-click for effect columns and pattern tools" }
    guard let entry = model.commands.entry(command: command, parameter: parameter, volume: volume) else { return "Unknown command for \(model.format); its stored value is preserved." }
    return "\(entry.name) · \(entry.hint)"
  }
}

final class PatternCommandPicker: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let columnPicker = NSPopUpButton(), search = NSSearchField(), table = NSTableView()
  let parameter = NSTextField(string: "00"), valueLabel = Theme.label("Parameter (hex)", size: 12)
  let target = Theme.label("", size: 12), detail = Theme.label("", size: 12), status = Theme.label("", size: 12, color: Theme.muted)
  var onContext: (() -> (PatternModel, Int, Int, Int))?
  var onNativeCommand: ((String, PatternModel, Int, Int, Int) -> Void)?
  var onDismiss: (() -> Void)?
  private(set) var capturedColumn=3
  func focusSearch() { window?.makeFirstResponder(search) }
  override func cancelOperation(_ sender: Any?) { onDismiss?() }
  var onRequest: (([String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  private(set) var captured = PatternModel([:]), row = 0, channel = 0, filtered = [PatternCommand](), pending = false
  private var suppressSelection = false
  var isVolume: Bool { columnPicker.indexOfSelectedItem == 1 }
  override init(frame: NSRect) {
    super.init(frame: frame)
    columnPicker.addItems(withTitles: ["All effects", "Volume column", "Native FX column"])
    columnPicker.target = self; columnPicker.action = #selector(changeColumn)
    columnPicker.fixed(width: 160)
    search.placeholderString = "Find a command, family or explanation"; search.delegate = self
    table.delegate = self; table.dataSource = self; table.rowHeight = 30; table.headerView = nil
    let command = NSTableColumn(identifier: .init("command")); command.width = 90; command.minWidth = 80; command.maxWidth = 90
    table.addTableColumn(command); table.addTableColumn(NSTableColumn(identifier: .init("name")))
    table.setAccessibilityLabel("Commands supported by this song format")
    let list = verticalScrollView(); list.documentView = table; list.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
    parameter.delegate=self;parameter.target=self;parameter.action=#selector(submit)
    table.target=self;table.doubleAction=#selector(choose)
    parameter.fixed(width: 80); parameter.setAccessibilityLabel("Command parameter")
    detail.maximumNumberOfLines = 4; detail.lineBreakMode = .byWordWrapping
    detail.heightAnchor.constraint(greaterThanOrEqualToConstant: 62).isActive = true
    status.maximumNumberOfLines = 2; status.lineBreakMode = .byWordWrapping
    let controls = stack(.horizontal, [valueLabel, parameter, ActionButton("Apply to cell") { [weak self] in self?.apply() }, NSView()])
    let top = stack(.horizontal, [columnPicker, search, ActionButton("Close · Esc") { [weak self] in self?.onDismiss?() }])
    let content = stack(.vertical, [Theme.label("Find an effect", size: 16, weight: .semibold), target, top, list, detail, controls, status], spacing: 12)
    content.stretchAcrossAxis(); content.fill(self, inset: 20)
  }
  required init?(coder: NSCoder) { fatalError() }
  func capture() {
    guard !pending, let context = onContext?() else { return }
    (captured, row, channel, _) = context
    capturedColumn=context.3
    columnPicker.selectItem(at: context.3 == 2 ? 1 : context.3>=5 ? 2 : 0)
    search.stringValue = ""
    target.stringValue = "\(captured.format) · Pattern \(captured.pattern) · Row \(row) · Channel \(channel + 1)"
    status.stringValue = "Changes only this cell's selected command and value. One Undo step."
    reload(selectCurrent: true)
  }
  @objc func changeColumn() { reload(selectCurrent: true) }
  func controlTextDidChange(_ obj: Notification) { if obj.object as AnyObject? === search {reload(selectCurrent: false)} }
  func reload(selectCurrent: Bool) {
    valueLabel.stringValue = isVolume ? "Value (decimal)" : "Parameter (hex)"
    let query = search.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
    let source = isVolume ? captured.commands.volumes : columnPicker.indexOfSelectedItem==2 ? PatternCommand.native : captured.commands.effects + PatternCommand.native
    let words=query.split(whereSeparator:{$0.isWhitespace}).map(String.init)
    filtered = source.filter { entry in words.allSatisfy { "\(entry.displayCode) \(entry.label) \(entry.name) \(entry.family) \(entry.hint)".localizedCaseInsensitiveContains($0) } }
    suppressSelection = true; table.reloadData(); table.deselectAll(nil)
    let cell = captured.drawCell(row, channel)
    let code = Int(isVolume ? cell.volumeCommand : cell.effect), amount = Int(isVolume ? cell.volume : cell.parameter)
    let index = selectCurrent ? filtered.firstIndex(where: { $0.nativeKind == nil && $0.command == code && amount & $0.mask == $0.value }) : nil
    if !filtered.isEmpty { table.selectRowIndexes(IndexSet(integer: index ?? 0), byExtendingSelection: false) }
    suppressSelection = false; selectedCommand()
    if index != nil { parameter.stringValue = isVolume ? String(amount) : String(format: "%02X", amount) }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { filtered.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard filtered.indices.contains(row) else { return nil }
    let entry = filtered[row]
    let label = Theme.label(tableColumn?.identifier.rawValue == "command" ? entry.displayCode : "\(entry.name) · \(entry.family)", size: 12, color: entry.color)
    label.toolTip = entry.hint; return label
  }
  func tableViewSelectionDidChange(_ notification: Notification) { if !suppressSelection { selectedCommand() } }
  private func selectedCommand() {
    guard filtered.indices.contains(table.selectedRow) else { detail.stringValue = "No matching commands."; return }
    let entry = filtered[table.selectedRow]
    detail.stringValue = "\(entry.name) — \(entry.hint)"
    parameter.isEnabled=entry.nativeKind==nil
    if entry.nativeKind != nil {parameter.stringValue="Edit…";return}
    parameter.stringValue = isVolume ? String(entry.suggested) : String(format: "%02X", entry.suggested)
  }
  func control(_ control: NSControl, textView: NSTextView, doCommandBy command: Selector) -> Bool {
    if command == #selector(NSResponder.cancelOperation(_:)) {onDismiss?();return true}
    if control===search {
      if command == #selector(NSResponder.moveDown(_:)) || command == #selector(NSResponder.moveUp(_:)) {
        guard !filtered.isEmpty else{return true}
        let delta=command == #selector(NSResponder.moveDown(_:)) ? 1 : -1
        let row=max(0,min(filtered.count-1,table.selectedRow+delta))
        table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false);table.scrollRowToVisible(row);return true
      }
      if command == #selector(NSResponder.insertNewline(_:)) {choose();return true}
    }
    return false
  }
  @objc func submit(){apply()}
  @objc func choose(){
    guard filtered.indices.contains(table.selectedRow) else{return}
    if filtered[table.selectedRow].nativeKind != nil {apply()} else {window?.makeFirstResponder(parameter);parameter.selectText(nil)}
  }
  func apply() {
    guard !pending, captured.editable, filtered.indices.contains(table.selectedRow), let onRequest else { return }
    if let kind=filtered[table.selectedRow].nativeKind {onNativeCommand?(kind,captured,row,channel,capturedColumn);return}
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
      if let error = response["error"] as? [String: Any] { self.status.stringValue = (error["message"] as? String ?? "Edit failed") + " Close and reopen to reload."; return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String else { return }
      self.captured.revisionToken = revision
      var cell = self.captured.cell(self.row, self.channel)
      cell[volume ? 2 : 4] = UInt8(entry.command); cell[volume ? 3 : 5] = UInt8(amount)
      self.captured.replaceCell(self.row, self.channel, with: cell)
      self.status.stringValue = "Applied \(entry.name). Undo restores the previous cell."
      self.onDismiss?()
    }
  }
}

/// A keyboard-capable floating finder. It does not become the document's main
/// window, and dismisses when the musician focuses another window or the grid.
final class EffectFinderPanel: NSPanel {
  init(picker:PatternCommandPicker) {
    super.init(contentRect:NSRect(x:0,y:0,width:620,height:460),styleMask:[.titled,.fullSizeContentView],backing:.buffered,defer:false)
    title="Find an effect";titleVisibility = .hidden;titlebarAppearsTransparent=true
    isReleasedWhenClosed=false;isFloatingPanel=true;hidesOnDeactivate=true
    backgroundColor=Theme.panel;hasShadow=true;contentView=picker
  }
  override var canBecomeKey:Bool {true}
  override var canBecomeMain:Bool {false}
  override func resignKey(){super.resignKey();orderOut(nil)}
}
