import AppKit

final class MatrixBlock: NSButton, NSDraggingSource {
  static let dragType = NSPasteboard.PasteboardType("org.resonance.arrangement-block")
  var bins = [Int](), notes = 0, rows = 64, selectedBlock = false
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
    (notes == 0 ? "—" : "\(notes) notes").draw(at: NSPoint(x: 8, y: 5), withAttributes: [
      .font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular),
      .foregroundColor: selectedBlock ? Theme.accent : Theme.muted,
    ])
  }
}

final class ArrangementMatrix: NSView, NSTableViewDataSource, NSTableViewDelegate {
  typealias Reply = ([String: Any]) -> Void
  let table = NSTableView(), status = Theme.label("", size: 12, color: Theme.muted)
  let page = Theme.label("", size: 12)
  let mode = NSPopUpButton()
  var onRequest: ((String, [String: Any], @escaping Reply) -> Void)?
  var onNavigate: ((Int, Int) -> Void)?
  var orders = [[String: Any]](), tracks = [[String: Any]]()
  var startOrder = 0, startChannel = 0, totalOrders = 0, totalChannels = 1
  var selectedOrder = 0, selectedChannel = 0, revision = "", loading = false
  var copied: (order: Int, channel: Int, revision: String)?
  private let dragOrigin = UUID().uuidString
  override init(frame: NSRect) {
    super.init(frame: frame)
    table.delegate = self; table.dataSource = self
    table.rowHeight = 50
    table.target = self; table.doubleAction = #selector(openSelection)
    table.setAccessibilityLabel("Arrangement matrix")
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
      Theme.label("Drag or paste to copy a block. Shared destinations become independent. One Undo restores the operation.", size: 11, color: Theme.muted),
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
    guard !loading else { return }
    loading = true
    onRequest?("arrangement.matrix", ["startOrder": startOrder, "orderCount": 64,
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
      self.page.stringValue = "Orders \(self.startOrder + 1)–\(self.startOrder + self.orders.count) of \(self.totalOrders)  ·  Tracks \(self.startChannel + 1)–\(self.startChannel + self.tracks.count) of \(self.totalChannels)"
      self.status.stringValue = self.copied == nil ? "Choose a block to edit or copy." : "Choose a destination block, then Paste block."
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
      text.stringValue = String(format: "%03d · P%03d", order, entry["pattern"] as? Int ?? 0) + "\n" + (section.isEmpty ? pattern : section)
      text.maximumNumberOfLines = 2
      return text
    }
    guard let channel = Int(column.identifier.rawValue),
      let block = (entry["blocks"] as? [[String: Any]])?.first(where: { $0["channel"] as? Int == channel }) else { return nil }
    let view = tableView.makeView(withIdentifier: .init("block"), owner: self) as? MatrixBlock ?? MatrixBlock(frame: .zero)
    view.identifier = .init("block")
    view.bins = block["bins"] as? [Int] ?? []; view.notes = block["notes"] as? Int ?? 0
    view.rows = entry["rows"] as? Int ?? 64
    view.selectedBlock = order == selectedOrder && channel == selectedChannel
    view.setAccessibilityLabel("Order \(order + 1), track \(channel + 1), \(view.notes) notes")
    view.onChoose = { [weak self] in self?.choose(order: order, channel: channel) }
    view.onDrag = { [weak self] in self?.dragPayload(order: order, channel: channel) }
    view.onDrop = { [weak self] payload, apply in
      self?.drop(payload, order: order, channel: channel, apply: apply) ?? false
    }
    view.needsDisplay = true
    return view
  }
  func choose(order: Int, channel: Int) {
    selectedOrder = order; selectedChannel = channel
    table.reloadData()
    status.stringValue = "Selected order \(order + 1), track \(channel + 1)."
  }
  @objc func openSelection() { onNavigate?(selectedOrder, selectedChannel) }
  func copyBlock() {
    guard !revision.isEmpty else { return }
    copied = (selectedOrder, selectedChannel, revision)
    status.stringValue = "Copied order \(selectedOrder + 1), track \(selectedChannel + 1). Choose a destination."
  }
  func dragPayload(order: Int, channel: Int) -> String? {
    guard !loading, !revision.isEmpty else { return nil }
    let payload: [String: Any] = ["origin": dragOrigin, "revision": revision, "order": order, "channel": channel]
    guard let data = try? JSONSerialization.data(withJSONObject: payload) else { return nil }
    return String(data: data, encoding: .utf8)
  }
  func drop(_ payload: String, order: Int, channel: Int, apply: Bool) -> Bool {
    guard !loading, payload.utf8.count < 2048, let data = payload.data(using: .utf8),
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
    guard !loading, let copied else { return }
    loading = true
    onRequest?("arrangement.copyBlock", ["sourceOrder": copied.order, "sourceChannel": copied.channel,
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
