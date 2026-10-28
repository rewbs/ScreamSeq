import AppKit
extension AppController {
  @objc func showArrangementMatrix() {
    guard !busy else { return }
    matrixEditor.currentRevision = { [weak self] in self?.model.revisionToken ?? "" }
    matrixEditor.onReturn = { [weak self] in self?.window.makeKeyAndOrderFront(nil); self?.focusPattern() }
    matrixEditor.startOrder = min(matrixEditor.startOrder, max(0, model.orders.count - 1) / 64 * 64)
    matrixEditor.startChannel = min(matrixEditor.startChannel, max(0, model.channels - 1) / 12 * 12)
    if let matrixWindow {
      matrixEditor.totalChannels = model.channels
      matrixEditor.load()
      matrixWindow.makeKeyAndOrderFront(nil)
      matrixWindow.makeFirstResponder(matrixEditor.table)
      return
    }
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1120, height: 650),
      styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Arrangement matrix"
    win.minSize = NSSize(width: 780, height: 450)
    win.isReleasedWhenClosed = false; win.delegate = self
    matrixWindow = win; win.contentView = matrixEditor
    matrixEditor.totalChannels = model.channels
    matrixEditor.startOrder = selectedOrder / 64 * 64
    matrixEditor.selectedOrder = selectedOrder
    matrixEditor.selectedChannel = patternView.cursorChannel
    matrixEditor.startChannel = patternView.cursorChannel / 12 * 12
    matrixEditor.onRequest = { [weak self] method, params, reply in
      self?.handleAutomation(method, params: params, reply: reply)
    }
    matrixEditor.onNavigate = { [weak self] order, channel in
      guard let self, !self.busy, self.model.orders.indices.contains(order), (0..<self.model.channels).contains(channel),
        self.model.patterns.contains(where: { $0["index"] as? Int == self.model.orders[order] }) else { return }
      self.selectedOrder = order
      self.model.pattern = self.model.orders[order]
      self.patternView.cursorChannel = channel
      self.patternView.firstChannel = channel
      self.patternView.cursorRow = 0; self.patternView.firstRow = 0
      self.patternView.column = 0
      self.patternView.isFollowing = false
      self.showEditor(0); self.refreshPattern(); self.refreshOrders()
      self.window.makeKeyAndOrderFront(nil)
      self.focusPattern()
    }
    win.center(); win.makeKeyAndOrderFront(nil)
    matrixEditor.load()
    win.makeFirstResponder(matrixEditor.table)
  }
}
