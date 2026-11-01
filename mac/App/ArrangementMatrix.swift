import AppKit

final class MatrixBlock: NSButton, NSDraggingSource {
  static let dragType = NSPasteboard.PasteboardType("org.resonance.arrangement-block")
  var bins = [Int](), notes = 0, events = 0, rows = 64, selectedBlock = false
  var onChoose: (() -> Void)?
  var onDrag: (() -> String?)?
  var onDrop: ((String, Bool) -> Bool)?
  private var receivingDrop = false
  override init(frame: NSRect) {
    super.init(frame: frame)
    target = self; action = #selector(choose); isBordered = false
    registerForDraggedTypes([Self.dragType])
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc private func choose() { onChoose?() }
  override func mouseDown(with event: NSEvent) {
    // Keep the native drag threshold, leaving an ordinary click as selection.
    guard let window else { return }
    while let next = window.nextEvent(matching: [.leftMouseDragged, .leftMouseUp]) {
      if next.type == .leftMouseUp { onChoose?(); return }
      if hypot(next.locationInWindow.x - event.locationInWindow.x,
               next.locationInWindow.y - event.locationInWindow.y) < 4 { continue }
      guard let payload = onDrag?() else { return }
      let item = NSPasteboardItem()
      item.setString(payload, forType: Self.dragType)
      let drag = NSDraggingItem(pasteboardWriter: item)
      let image = NSImage(size: bounds.size)
      if let bitmap = bitmapImageRepForCachingDisplay(in: bounds) {
        cacheDisplay(in: bounds, to: bitmap); image.addRepresentation(bitmap)
      }
      drag.setDraggingFrame(bounds, contents: image)
      beginDraggingSession(with: [drag], event: next, source: self)
      return
    }
  }
  func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
    context == .withinApplication ? .copy : []
  }
  override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
    let payload = sender.draggingPasteboard.string(forType: Self.dragType)
    receivingDrop = payload.map { onDrop?($0, false) == true } ?? false
    needsDisplay = true
    return receivingDrop ? .copy : []
  }
  override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation { draggingEntered(sender) }
  override func draggingExited(_ sender: NSDraggingInfo?) { receivingDrop = false; needsDisplay = true }
  override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
    receivingDrop = false; needsDisplay = true
    guard let payload = sender.draggingPasteboard.string(forType: Self.dragType) else { return false }
    return onDrop?(payload, true) ?? false
  }
  override func draw(_ dirtyRect: NSRect) {
    (receivingDrop ? Theme.accent.withAlphaComponent(0.4) : selectedBlock ? Theme.accent.withAlphaComponent(0.17) : Theme.raised).setFill()
    NSBezierPath(roundedRect: bounds.insetBy(dx: 2, dy: 2), xRadius: 4, yRadius: 4).fill()
    Theme.accent.setFill()
    let width = max(1, (bounds.width - 14) / 16)
    for (index, count) in bins.enumerated() where count > 0 {
      let height = max(2, min(22, 22 * CGFloat(count) / CGFloat(max(1, (rows + 15) / 16))))
      NSRect(x: 7 + CGFloat(index) * width, y: 21, width: max(1, width - 1), height: height).fill()
    }
    (events == 0 ? "—" : "\(notes)n · \(events)e").draw(at: NSPoint(x: 8, y: 5), withAttributes: [
      .font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular),
      .foregroundColor: selectedBlock ? Theme.accent : Theme.muted,
    ])
  }
}

final class ArrangementTable: NSTableView {
  var onKey: ((NSEvent) -> Bool)?
  override func keyDown(with event: NSEvent) {
    if onKey?(event) != true { super.keyDown(with: event) }
  }
}

final class ArrangementMatrix: NSView, NSTableViewDataSource, NSTableViewDelegate {
  typealias Reply = ([String: Any]) -> Void
  let table = ArrangementTable(), status = Theme.label("", size: 12, color: Theme.muted)
  let page = Theme.label("", size: 12)
  let mode = NSPopUpButton()
  var onRequest: ((String, [String: Any], @escaping Reply) -> Void)?
  var onNavigate: ((Int, Int) -> Void)?
  var onReturn: (() -> Void)?
  var currentRevision: (() -> String)?
  var orders = [[String: Any]](), tracks = [[String: Any]]()
  var startOrder = 0, startChannel = 0, totalOrders = 0, totalChannels = 1
  var selectedOrder = 0, selectedChannel = 0, revision = "", loading = false
  var copied: (order: Int, channel: Int, revision: String)?
  private let dragOrigin = UUID().uuidString
  private var selecting = false
  override init(frame: NSRect) {
    super.init(frame: frame)
    table.delegate = self; table.dataSource = self
    table.rowHeight = 50
    table.target = self; table.doubleAction = #selector(openSelection)
    table.setAccessibilityLabel("Arrangement matrix")
    table.onKey = { [weak self] event in self?.handleKey(event) ?? false }
    table.usesAlternatingRowBackgroundColors = true
    let scroll = NSScrollView()
    scroll.documentView = table; scroll.hasVerticalScroller = true; scroll.hasHorizontalScroller = true
    mode.addItems(withTitles: ["Overwrite", "Merge", "Mix into empty fields"])
    mode.widthAnchor.constraint(greaterThanOrEqualToConstant: 170).isActive = true
    let content = stack(.vertical, [
      stack(.horizontal, [Theme.label("Arrangement matrix", size: 20, weight: .semibold), NSView(),
        ActionButton("Refresh") { [weak self] in self?.load() }]),
      stack(.horizontal, [
        ActionButton("Earlier orders") { [weak self] in self?.movePage(orders: -64, channels: 0) },
        ActionButton("Later orders") { [weak self] in self?.movePage(orders: 64, channels: 0) },
        ActionButton("Previous tracks") { [weak self] in self?.movePage(orders: 0, channels: -12) },
        ActionButton("Next tracks") { [weak self] in self?.movePage(orders: 0, channels: 12) }, NSView(),
      ]), page, scroll,
      stack(.horizontal, [
        ActionButton("Open block") { [weak self] in self?.openSelection() },
        ActionButton("Copy block") { [weak self] in self?.copyBlock() }, mode,
        ActionButton("Paste block") { [weak self] in self?.pasteBlock() }, NSView(),
      ]), status,
      Theme.label("Arrows select · Return opens · ⌘C / ⌘V copy / paste · F5 refreshes · Escape returns · n = notes, e = all events", size: 11, color: Theme.muted),
    ], spacing: 12)
    content.fill(self, inset: 20); content.stretchAcrossAxis()
  }
  required init?(coder: NSCoder) { fatalError() }
  func movePage(orders: Int, channels: Int) {
    guard !loading else { return }
    startOrder = min(max(0, totalOrders - 1) / 64 * 64, max(0, startOrder + orders))
    startChannel = min(max(0, totalChannels - 1) / 12 * 12, max(0, startChannel + channels))
    load()
  }
  func load() {
    guard !loading, let request = onRequest else { return }
    loading = true
    request("arrangement.matrix", ["startOrder": startOrder, "orderCount": 64,
      "startChannel": startChannel, "channelCount": min(12, max(1, totalChannels - startChannel))]) { [weak self] reply in
      guard let self else { return }
      self.loading = false
      guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
        self.status.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Could not read arrangement"
        return
      }
      self.revision = result["revision"] as? String ?? ""
      self.orders = data["orders"] as? [[String: Any]] ?? []
      self.tracks = data["tracks"] as? [[String: Any]] ?? []
      self.totalOrders = data["totalOrders"] as? Int ?? 0
      self.totalChannels = data["totalChannels"] as? Int ?? 1
      self.selectedOrder = max(0, min(self.selectedOrder, self.totalOrders - 1))
      self.selectedChannel = max(0, min(self.selectedChannel, self.totalChannels - 1))
      self.selecting = true
      for column in self.table.tableColumns { self.table.removeTableColumn(column) }
      let order = NSTableColumn(identifier: .init("order")); order.title = "Order / section"; order.width = 165
      self.table.addTableColumn(order)
      for track in self.tracks {
        let channel = track["channel"] as? Int ?? 0
        let column = NSTableColumn(identifier: .init("\(channel)"))
        let name = track["name"] as? String ?? ""
        column.title = name.isEmpty ? "Track \(channel + 1)" : name
        column.width = 100; column.minWidth = 80
        self.table.addTableColumn(column)
      }
      self.table.reloadData()
      self.revealSelection()
      self.selecting = false
      self.page.stringValue = "Orders \(self.startOrder + 1)–\(self.startOrder + self.orders.count) of \(self.totalOrders)  ·  Tracks \(self.startChannel + 1)–\(self.startChannel + self.tracks.count) of \(self.totalChannels)"
      self.status.stringValue = self.copied == nil ? "Choose a block to edit or copy." : self.copied?.revision == self.revision ? "Choose a destination block, then Paste block." : "Copied block is stale. Copy it again before pasting."
    }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { orders.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard row < orders.count, let column else { return nil }
    let entry = orders[row], order = entry["order"] as? Int ?? 0
    if column.identifier.rawValue == "order" {
      let text = tableView.makeView(withIdentifier: column.identifier, owner: self) as? NSTextField ?? Theme.label("", size: 11, mono: true)
      text.identifier = column.identifier
      let section = entry["name"] as? String ?? ""
      let pattern = entry["patternName"] as? String ?? ""
      let index = entry["pattern"] as? Int ?? 65535
      let name = index == 65535 ? "End" : index == 65534 ? "Skip" : String(format: "P%03d", index)
      text.stringValue = String(format: "%03d · ", order) + name + "\n" + (section.isEmpty ? pattern : section)
      text.maximumNumberOfLines = 2
      return text
    }
    guard let channel = Int(column.identifier.rawValue),
      let block = (entry["blocks"] as? [[String: Any]])?.first(where: { $0["channel"] as? Int == channel }) else { return nil }
    let view = tableView.makeView(withIdentifier: .init("block"), owner: self) as? MatrixBlock ?? MatrixBlock(frame: .zero)
    view.identifier = .init("block")
    view.bins = block["bins"] as? [Int] ?? []; view.notes = block["notes"] as? Int ?? 0
    view.events = block["events"] as? Int ?? view.notes
    view.rows = entry["rows"] as? Int ?? 64
    view.selectedBlock = order == selectedOrder && channel == selectedChannel
    let summary = "Order \(order + 1), track \(channel + 1), \(view.notes) notes, \(view.events) events · \(block["trackerEvents"] as? Int ?? 0) tracker, \(block["preciseEvents"] as? Int ?? 0) precise, \(block["nativeFxEvents"] as? Int ?? 0) native FX"
    view.setAccessibilityLabel(summary); view.toolTip = summary
    view.onChoose = { [weak self] in guard let self else{return};self.window?.makeFirstResponder(self.table);self.choose(order: order, channel: channel) }
    view.onDrag = { [weak self] in self?.dragPayload(order: order, channel: channel) }
    view.onDrop = { [weak self] payload, apply in
      self?.drop(payload, order: order, channel: channel, apply: apply) ?? false
    }
    view.needsDisplay = true
    return view
  }
  func choose(order: Int, channel: Int) {
    guard !loading, (0..<totalOrders).contains(order), (0..<totalChannels).contains(channel) else { return }
    selectedOrder = order; selectedChannel = channel
    let orderPage = order / 64 * 64, trackPage = channel / 12 * 12
    if orderPage != startOrder || trackPage != startChannel {
      startOrder = orderPage; startChannel = trackPage; load(); return
    }
    selecting = true
    table.reloadData()
    revealSelection()
    selecting = false
    status.stringValue = "Selected order \(order + 1), track \(channel + 1)."
  }
  private func revealSelection() {
    guard let row = orders.firstIndex(where: { $0["order"] as? Int == selectedOrder }) else { return }
    table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
    table.scrollRowToVisible(row)
    if let column=table.tableColumns.firstIndex(where:{$0.identifier.rawValue==String(selectedChannel)}) {table.scrollColumnToVisible(column)}
    if let column = table.tableColumns.firstIndex(where: { $0.identifier.rawValue == String(selectedChannel) }) { table.scrollColumnToVisible(column) }
  }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard !selecting, !loading, orders.indices.contains(table.selectedRow), let order = orders[table.selectedRow]["order"] as? Int else { return }
    choose(order: order, channel: selectedChannel)
  }
  func handleKey(_ event: NSEvent) -> Bool {
    let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
    if flags == .command {
      if event.charactersIgnoringModifiers?.lowercased() == "c" { copyBlock(); return true }
      if event.charactersIgnoringModifiers?.lowercased() == "v" { pasteBlock(); return true }
    }
    guard flags.isEmpty else { return false }
    switch event.keyCode {
    case 123: choose(order: selectedOrder, channel: max(0, selectedChannel - 1))
    case 124: choose(order: selectedOrder, channel: min(totalChannels - 1, selectedChannel + 1))
    case 125: choose(order: min(totalOrders - 1, selectedOrder + 1), channel: selectedChannel)
    case 126: choose(order: max(0, selectedOrder - 1), channel: selectedChannel)
    case 36, 76: openSelection()
    case 96: load()
    case 53, 97: onReturn?()
    default: return false
    }
    return true
  }
  private func musical(_ order: Int) -> Bool {
    guard let entry = orders.first(where: { $0["order"] as? Int == order }) else { return false }
    return (entry["rows"] as? Int ?? 0) > 0
  }
  private var fresh: Bool { !revision.isEmpty && (currentRevision?() ?? revision) == revision }
  @objc func openSelection() {
    guard !loading, fresh, musical(selectedOrder) else { status.stringValue = "Refresh and select a musical block to open."; return }
    onNavigate?(selectedOrder, selectedChannel)
  }
  func copyBlock() {
    guard !loading, fresh, musical(selectedOrder) else { status.stringValue = "Refresh and select a musical block to copy."; return }
    copied = (selectedOrder, selectedChannel, revision)
    status.stringValue = "Copied order \(selectedOrder + 1), track \(selectedChannel + 1). Choose a destination."
  }
  func dragPayload(order: Int, channel: Int) -> String? {
    guard !loading, fresh, musical(order), (0..<totalChannels).contains(channel) else { return nil }
    let payload: [String: Any] = ["origin": dragOrigin, "revision": revision, "order": order, "channel": channel]
    guard let data = try? JSONSerialization.data(withJSONObject: payload) else { return nil }
    return String(data: data, encoding: .utf8)
  }
  func drop(_ payload: String, order: Int, channel: Int, apply: Bool) -> Bool {
    guard !loading, fresh, musical(order), payload.utf8.count < 2048, let data = payload.data(using: .utf8),
      let item = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
      item["origin"] as? String == dragOrigin, let token = item["revision"] as? String,
      token == revision, let sourceOrder = item["order"] as? Int, let sourceChannel = item["channel"] as? Int,
      sourceOrder >= 0, sourceOrder < totalOrders, sourceChannel >= 0, sourceChannel < totalChannels,
      order >= 0, order < totalOrders, channel >= 0, channel < totalChannels,
      sourceOrder != order || sourceChannel != channel else { return false }
    if apply {
      copied = (sourceOrder, sourceChannel, token)
      choose(order: order, channel: channel)
      pasteBlock()
    }
    return true
  }
  func pasteBlock() {
    guard !loading, let copied, let request = onRequest else { return }
    guard copied.revision == revision, fresh else { status.stringValue = "Copied block is stale. Refresh and copy it again."; return }
    guard musical(selectedOrder) else { status.stringValue = "Select a musical destination block."; return }
    loading = true
    request("arrangement.copyBlock", ["sourceOrder": copied.order, "sourceChannel": copied.channel,
      "targetOrder": selectedOrder, "targetChannel": selectedChannel, "expectedRevision": copied.revision,
      "mode": ["overwrite", "merge", "mix"][max(0, mode.indexOfSelectedItem)], "makeUnique": true]) { [weak self] reply in
      guard let self else { return }
      self.loading = false
      if let error = reply["error"] as? [String: Any] {
        self.status.stringValue = error["message"] as? String ?? "Could not paste block"
      } else {
        self.copied = nil
        self.load()
      }
    }
  }
}
