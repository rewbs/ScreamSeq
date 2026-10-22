import AppKit

extension AppController {
  func synchronizeManualPluginGesture() {
    guard !shuttingDown, !busy, !sessionReading, !pluginRecordingInput.protectsDocument,
      pluginManualGestureApplied != pluginParameterGestureActive else { return }
    session.parameterGesture(pluginParameterGestureActive)
    pluginManualGestureApplied = pluginParameterGestureActive
  }
  func schedulePluginControlPump() {
    guard !pluginControlPumpScheduled, !shuttingDown else { return }
    pluginControlPumpScheduled = true
    DispatchQueue.main.async { [weak self] in
      guard let self else { return }; self.pluginControlPumpScheduled = false
      self.servicePluginControlInput()
    }
  }
  func servicePluginControlInput() {
    guard !shuttingDown, !busy, !sessionReading, window?.attachedSheet == nil, NSApp.modalWindow == nil else { return }
    pluginRecordingInput.pump { [weak self] method, params, reply in
      guard let self else { reply(PluginRecordingInput.refused("The application closed before input delivery.")); return }
      self.handleAutomation(method, params: params, reply: reply, recordingInputAdmitted: true)
    }
  }
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
    view.onDelivery = { [weak self] in self?.pluginRecordingInput.delivery.summary ?? [:] }
    view.onDeliveryLoss = { [weak self] in self?.pluginRecordingInput.delivery.lossCensus ?? [:] }
    view.onInputProtected = { [weak self] in self?.pluginRecordingInput.protectsDocument ?? false }
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
