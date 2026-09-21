import AppKit

final class RecoveryBrowser: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let table = NSTableView()
  let message = Theme.label("", size: 12, color: Theme.muted)
  let restore = ActionButton("Restore selected copy", prominent: true) {}
  var onRestore: ((String) -> Void)?
  private(set) var entries = [RecoveryEntry]()
  override init(frame: NSRect) {
    super.init(frame: frame)
    for (id, title, width) in [("date", "Saved", 175.0), ("title", "Song", 200.0), ("source", "Original file", 230.0)] {
      let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
      column.title = title; column.width = width; table.addTableColumn(column)
    }
    table.dataSource = self; table.delegate = self; table.rowHeight = 28
    table.target = self; table.doubleAction = #selector(restoreClickedCopy)
    table.setAccessibilityLabel("Recovery copies, newest first")
    let scroll = NSScrollView(); scroll.documentView = table; scroll.hasVerticalScroller = true
    scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true
    let detail = Theme.label("Autosave keeps up to ten copies per session. Your current unsaved song is protected before restoring. Recovered songs open unsaved, ready for Save As.", size: 12, color: Theme.muted)
    detail.maximumNumberOfLines = 3; detail.lineBreakMode = .byWordWrapping
    message.maximumNumberOfLines = 3; message.lineBreakMode = .byWordWrapping
    restore.handler = { [weak self] in
      guard let self, self.entries.indices.contains(self.table.selectedRow) else { return }
      self.onRestore?(self.entries[self.table.selectedRow].url.lastPathComponent)
    }
    let body = stack(.vertical, [Theme.label("Recover a song", size: 22, weight: .semibold), detail, scroll,
      message, stack(.horizontal, [NSView(), restore])], spacing: 16)
    body.stretchAcrossAxis(); body.fill(self, inset: 24)
    update([])
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ entries: [RecoveryEntry]) {
    self.entries = entries; table.reloadData()
    if !entries.isEmpty { table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false) }
    message.stringValue = entries.isEmpty ? "No recovery copies are available yet. Unsaved edits are protected automatically every 10 seconds." : "Select a copy by time. Older copies remain available if the newest cannot be opened."
    restore.isEnabled = !entries.isEmpty
  }
  @objc func restoreClickedCopy() {
    guard entries.indices.contains(table.clickedRow) else { return }
    table.selectRowIndexes(IndexSet(integer: table.clickedRow), byExtendingSelection: false)
    restore.invoke()
  }
  func numberOfRows(in tableView: NSTableView) -> Int { entries.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard entries.indices.contains(row) else { return nil }
    let entry = entries[row]
    let label: NSTextField
    switch column?.identifier.rawValue {
    case "date": label = Theme.label(DateFormatter.localizedString(from: entry.date, dateStyle: .short, timeStyle: .medium))
    case "title": label = Theme.label(entry.title + (entry.hasRecording ? " · recording take" : ""))
    default: label = Theme.label(entry.source.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "Unsaved song")
    }
    label.toolTip = entry.source ?? entry.url.lastPathComponent
    return label
  }
  func tableViewSelectionDidChange(_ notification: Notification) { restore.isEnabled = entries.indices.contains(table.selectedRow) }
}
