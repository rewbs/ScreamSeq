import AppKit

extension AppController {
  @objc func showPluginRecording() {
    guard !shuttingDown else { return }
    if let window = pluginRecordingWindow, let view = window.contentView as? PluginRecordingView {
      window.makeKeyAndOrderFront(nil); view.activate(); return
    }
    let view = PluginRecordingView(frame: .zero)
    view.onDocument = { [weak self] in
      guard let self, !self.shuttingDown else { return nil }; return self.recoveryID
    }
    view.onPlugins = { [weak self] in
      self?.model.nativePlugins.compactMap { plugin in
        guard let id = plugin["instanceID"] as? String else { return nil }
        return (id: id, name: plugin["name"] as? String ?? id)
      } ?? []
    }
    view.onRequest = { [weak self] method, params, reply in
      guard let self else { reply(AutomationServer.error(-32002, "The application has closed")); return }
      self.handleAutomation(method, params: params, reply: reply)
    }
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 760, height: 540),
      styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    window.title = "Plugin automation take"; window.contentMinSize = NSSize(width: 700, height: 500)
    window.isReleasedWhenClosed = false; window.contentView = view; window.delegate = self
    window.initialFirstResponder = view.table
    pluginRecordingWindow = window; window.center(); window.makeKeyAndOrderFront(nil)
    window.makeFirstResponder(view.table); view.activate()
  }
}
