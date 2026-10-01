import AppKit
extension AppController {
  @objc func showArrangementMatrix() {
    guard !busy else { return }
    if let matrixWindow {
      matrixEditor.totalChannels = model.channels
      matrixEditor.load()
      matrixWindow.makeKeyAndOrderFront(nil)
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
    matrixEditor.onRequest = { [weak self] method, params, reply in
      self?.handleAutomation(method, params: params, reply: reply)
    }
    matrixEditor.onNavigate = { [weak self] order, channel in
      guard let self, !self.busy, order < self.model.orders.count, channel < self.model.channels else { return }
      self.selectedOrder = order
      self.model.pattern = self.model.orders[order]
      self.patternView.cursorChannel = channel
      self.patternView.firstChannel = channel
      self.patternView.cursorRow = 0; self.patternView.firstRow = 0
      self.patternView.isFollowing = false
      self.showEditor(0); self.refreshPattern(); self.refreshOrders()
      self.window.makeKeyAndOrderFront(nil)
    }
    win.center(); win.makeKeyAndOrderFront(nil)
    matrixEditor.load()
  }
}
