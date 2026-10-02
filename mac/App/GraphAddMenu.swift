import AppKit

/// A transient, keyboard-first search shared by every graph insertion gesture.
/// A captured revision belongs to the invocation, never to a later selection.
final class GraphAddMenu: NSObject, NSSearchFieldDelegate, NSTableViewDelegate, NSTableViewDataSource, NSWindowDelegate {
  struct Entry {
    var id: String, title: String, detail: String, keywords: String
    var payload: [String: Any] = [:]
    var unavailable: String? = nil
    var toolTip: String? = nil
  }
  let search = NSSearchField(), table = DirectActionTable()
  let context = Theme.label("", size: 11, color: Theme.muted)
  let hint = Theme.label("↑↓ choose · Return adds · Esc cancels", size: 11, color: Theme.muted)
  private(set) var entries = [Entry](), filtered = [Entry]()
  private(set) var panel: NSPanel?
  private weak var owner: NSWindow?
  private weak var responder: NSResponder?
  var onChoose: ((Entry) -> Void)?
  private var keyMonitor: Any?
  private var verb="adds",filteredQuery=""

  func show(in view: NSView, at point: NSPoint, title: String, entries: [Entry], verb:String="adds", choose: @escaping (Entry) -> Void) {
    close(); owner = view.window; responder = view.window?.firstResponder; onChoose = choose
    self.verb=verb
    if panel == nil {
      let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 480, height: 350), styleMask: [.titled, .closable], backing: .buffered, defer: false)
      window.isReleasedWhenClosed = false; window.delegate = self; window.level = .floating; panel = window
      search.placeholderString = "Effects, LFOs, envelopes, returns, groups…"; search.setAccessibilityLabel("Add graph node search"); search.delegate = self
      table.headerView = nil; table.rowHeight = 40; table.delegate = self; table.dataSource = self
      let column = NSTableColumn(identifier: .init("node")); column.width = 450; table.addTableColumn(column)
      table.setAccessibilityLabel("Compatible graph nodes"); table.activate = { [weak self] in self?.choose() }
      let scroll = verticalScrollView(); scroll.documentView = table
      context.maximumNumberOfLines = 2; context.lineBreakMode = .byTruncatingMiddle
      hint.maximumNumberOfLines = 2
      let content = stack(.vertical, [context, search, scroll, hint], spacing: 7); content.stretchAcrossAxis(); content.fill(window.contentView!, inset: 10)
    }
    context.stringValue = title; panel?.title = verb=="adds" ? "Add to graph":title
    search.placeholderString=verb=="adds" ? "Effects, LFOs, envelopes, returns, groups…":"Search targets…"
    search.setAccessibilityLabel(verb=="adds" ? "Add graph node search":"Graph command target search")
    search.stringValue = ""; replace(entries)
    if let parent = view.window, let panel {
      let anchor = parent.convertPoint(toScreen: view.convert(point, to: nil))
      let screen = parent.screen?.visibleFrame ?? NSScreen.main?.visibleFrame ?? parent.frame
      panel.setFrameOrigin(NSPoint(x: max(screen.minX, min(screen.maxX-panel.frame.width, anchor.x)), y: max(screen.minY, min(screen.maxY-panel.frame.height, anchor.y-panel.frame.height))))
      parent.addChildWindow(panel, ordered: .above); panel.makeKeyAndOrderFront(nil); panel.makeFirstResponder(search)
    }
    keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
      guard let self, event.window === self.panel else { return event }
      if event.keyCode == 53 { self.close(); return nil }
      if event.keyCode == 36 { self.choose(); return nil }
      if event.keyCode == 125 || event.keyCode == 126 {
        guard !self.filtered.isEmpty else { return nil }
        let row = max(0, min(self.filtered.count-1, self.table.selectedRow + (event.keyCode == 125 ? 1 : -1)))
        self.table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false); self.table.scrollRowToVisible(row); return nil
      }
      return event
    }
  }
  func replace(_ values: [Entry]) { entries = values; filter() }
  func filter() {
    let query=search.stringValue.lowercased().trimmingCharacters(in:.whitespacesAndNewlines)
    let selected = query==filteredQuery && filtered.indices.contains(table.selectedRow) ? filtered[table.selectedRow].id : nil
    filteredQuery=query
    let terms=query.split(whereSeparator: \.isWhitespace)
    // A musician searching "Track 2" means the visible channel label first.
    // Stable IDs remain searchable, but n2 must not outrank the title Track 2.
    filtered=entries.enumerated().compactMap { index,entry -> (Int,Int,Entry)? in
      let title=entry.title.lowercased(),visible=title+" "+entry.detail.lowercased(),all=visible+" "+entry.keywords.lowercased()
      guard terms.allSatisfy({all.contains($0)})else{return nil}
      let rank=terms.allSatisfy({title.contains($0)}) ? 0:terms.allSatisfy({visible.contains($0)}) ? 1:2
      return (rank,index,entry)
    }.sorted{a,b in if (a.2.unavailable==nil) != (b.2.unavailable==nil){return a.2.unavailable==nil};return a.0==b.0 ? a.1<b.1:a.0<b.0}.map{$0.2}
    table.reloadData()
    if !filtered.isEmpty { table.selectRowIndexes(IndexSet(integer: filtered.firstIndex { $0.id == selected } ?? 0), byExtendingSelection: false) }
    hint.stringValue = filtered.isEmpty ? "No matching targets · Escape returns to the graph" : "↑↓ choose · Return \(verb) · Esc cancels"
  }
  func controlTextDidChange(_ notification: Notification) { filter() }
  func numberOfRows(in tableView: NSTableView) -> Int { filtered.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard filtered.indices.contains(row) else { return nil }
    let item = filtered[row]
    let name = Theme.label(item.title, size: 12, weight: .medium)
    // An unavailable target still needs its owner/role to distinguish copies.
    let summary=[item.detail,item.unavailable].compactMap{$0}.filter{!$0.isEmpty}.joined(separator:" · ")
    let detail = Theme.label(summary, size: 10, color: Theme.muted)
    detail.lineBreakMode = .byTruncatingTail;detail.maximumNumberOfLines=1
    let cell = stack(.vertical, [name, detail], spacing: 1); cell.setAccessibilityLabel(item.title + ". " + summary)
    let tip=[item.toolTip ?? (item.title+"\n"+item.detail),item.unavailable].compactMap{$0}.joined(separator:"\n")
    cell.toolTip=tip;name.toolTip=tip;detail.toolTip=tip
    return cell
  }
  func choose() {
    guard filtered.indices.contains(table.selectedRow) else { return }
    let item = filtered[table.selectedRow]
    if let reason = item.unavailable { hint.stringValue = reason; return }
    let action = onChoose; close(); action?(item)
  }
  func close() {
    if let keyMonitor { NSEvent.removeMonitor(keyMonitor); self.keyMonitor = nil }
    if let panel, panel.isVisible { owner?.removeChildWindow(panel); panel.orderOut(nil); owner?.makeKeyAndOrderFront(nil); owner?.makeFirstResponder(responder) }
    onChoose = nil
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool { close(); return false }
  deinit { if let keyMonitor { NSEvent.removeMonitor(keyMonitor) } }
}
