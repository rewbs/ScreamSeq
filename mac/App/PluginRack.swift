import AppKit

final class PluginRack: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let table = DirectActionTable()
  private let dragType = NSPasteboard.PasteboardType("org.screamseq.plugin-rack")
  private var draggingID: String?, dragOrder = [String]()
  private var updating = false
  private(set) var items = [[String: Any]]()
  var selectedID: String? { items.indices.contains(table.selectedRow) ? items[table.selectedRow]["instanceID"] as? String : nil }
  var onSelect: ((String) -> Void)?, onOpen: ((String) -> Void)?, onBypass: ((String, Bool) -> Void)?
  var onDrop: ((String, String?, String?) -> Void)?, onRemove: ((String) -> Void)?
  var menuForItem: ((String) -> NSMenu)?
  private var height: NSLayoutConstraint!
  override init(frame: NSRect) {
    super.init(frame: frame)
    table.dataSource = self; table.delegate = self; table.headerView = nil; table.rowHeight = 34
    table.backgroundColor = Theme.bg; table.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    let enabled = NSTableColumn(identifier: .init("enabled")); enabled.width = 30; enabled.minWidth = 30; enabled.maxWidth = 30
    let name = NSTableColumn(identifier: .init("name")); name.width = 270
    table.addTableColumn(enabled); table.addTableColumn(name)
    table.registerForDraggedTypes([dragType]); table.setDraggingSourceOperationMask(.move, forLocal: true)
    table.setAccessibilityLabel("Plugin rack · double-click or Return to open · drag to reorder")
    table.activate = { [weak self] in guard let self, let id = self.selectedID else { return }; self.onOpen?(id) }
    table.remove = { [weak self] in guard let self, let id = self.selectedID else { return }; self.onRemove?(id) }
    table.actions = { [weak self] in guard let self, let id = self.selectedID else { return NSMenu() }; return self.menuForItem?(id) ?? NSMenu() }
    let scroll = verticalScrollView(); scroll.documentView = table; scroll.fill(self)
    height = heightAnchor.constraint(equalToConstant: 72); height.isActive = true
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ items: [[String: Any]], selected: String?) {
    updating = true; self.items = items; table.reloadData()
    if let selected, let row = items.firstIndex(where: { $0["instanceID"] as? String == selected }) {
      table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
    } else { table.deselectAll(nil) }
    height.constant = max(42, min(174, CGFloat(items.count * 34 + 4))); updating = false
  }
  func numberOfRows(in tableView: NSTableView) -> Int { items.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard items.indices.contains(row), let id = items[row]["instanceID"] as? String else { return nil }
    let entry = items[row], name = entry["name"] as? String ?? "Plugin", bypass = entry["bypass"] as? Bool == true
    if column?.identifier.rawValue == "enabled" {
      let button = ActionButton("") { [weak self] in self?.onBypass?(id, !bypass) }
      button.setButtonType(.switch); button.state = bypass ? .off : .on
      button.setAccessibilityLabel("Enable " + name); button.toolTip = "Bypass / enable " + name
      return button
    }
    let label = Theme.label(name + ((entry["ownerName"] as? String).map { "  ·  " + $0 } ?? "") , size: 12)
    label.lineBreakMode = .byTruncatingTail; label.textColor = bypass ? Theme.muted : Theme.text
    label.toolTip = name + " · Double-click to open; right-click for presets, routing and removal"
    return label
  }
  func tableViewSelectionDidChange(_ notification: Notification) { if !updating, let id = selectedID { onSelect?(id) } }
  func tableView(_ tableView: NSTableView, pasteboardWriterForRow row: Int) -> NSPasteboardWriting? {
    guard items.indices.contains(row), let id = items[row]["instanceID"] as? String else { return nil }
    draggingID = id; dragOrder = items.compactMap { $0["instanceID"] as? String }
    let item = NSPasteboardItem(); item.setString(id, forType: dragType); return item
  }
  func tableView(_ tableView: NSTableView, validateDrop info: NSDraggingInfo, proposedRow row: Int, proposedDropOperation operation: NSTableView.DropOperation) -> NSDragOperation {
    guard info.draggingSource as? NSTableView === table, draggingID != nil,
      dragOrder == items.compactMap({ $0["instanceID"] as? String }), (0...items.count).contains(row) else { return [] }
    table.setDropRow(row, dropOperation: .above); return .move
  }
  func tableView(_ tableView: NSTableView, acceptDrop info: NSDraggingInfo, row: Int, dropOperation operation: NSTableView.DropOperation) -> Bool {
    guard validateDrop(info, row: row), let id = draggingID else { return false }
    let anchor = items.indices.contains(row) ? items[row] : items.last
    let before = items.indices.contains(row) ? anchor?["instanceID"] as? String : nil
    if before != id { onDrop?(id, before, anchor?["ownerID"] as? String) }
    draggingID = nil; return true
  }
  private func validateDrop(_ info: NSDraggingInfo, row: Int) -> Bool {
    info.draggingSource as? NSTableView === table && (0...items.count).contains(row) && dragOrder == items.compactMap { $0["instanceID"] as? String }
  }
}
