import AppKit
extension AppController {
  func showPluginPorts(_ slot: Int) {
    guard !busy else { return }
    if pluginPortsWindow == nil {
      pluginPortsEditor.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
      let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 600),
        styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
      win.title = "Plugin audio buses"; win.minSize = NSSize(width: 600, height: 400)
      win.isReleasedWhenClosed = false; win.delegate = self; win.contentView = pluginPortsEditor
      pluginPortsWindow = win; win.center()
    }
    pluginPortsWindow?.makeKeyAndOrderFront(nil); pluginPortsEditor.open(slot: slot)
  }
}
