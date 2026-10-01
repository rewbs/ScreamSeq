import AppKit
extension AppController {
  func showSidechains() {
    guard !busy else { return }
    if sidechainWindow == nil {
      sidechainEditor.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
      sidechainEditor.onConfigurePlugin = { [weak self] slot in self?.showPluginPorts(slot) }
      let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 780, height: 590),
        styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
      win.title = "Sidechains"; win.minSize = NSSize(width: 720, height: 500)
      win.isReleasedWhenClosed = false; win.delegate = self; win.contentView = sidechainEditor
      sidechainWindow = win; win.center()
    }
    sidechainWindow?.makeKeyAndOrderFront(nil); sidechainEditor.load()
  }
}
