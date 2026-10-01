import AppKit

extension AppController {
  @objc func showPatternTools() {
    guard !busy, model.editable else { return }
    if let patternToolsWindow { patternToolsWindow.makeKeyAndOrderFront(nil); return }
    let panel = PatternToolsPanel(frame: .zero)
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 690),
      styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Pattern tools"
    win.minSize = NSSize(width: 700, height: 650)
    win.isReleasedWhenClosed = false
    win.delegate = self
    win.contentView = panel
    patternToolsWindow = win
    panel.onContext = { [weak self] in
      guard let self else { return (PatternModel([:]), [:]) }
      var selection = self.patternView.automationSelection
      selection["cursorChannel"] = self.patternView.cursorChannel
      return (self.model, selection)
    }
    panel.onRequest = { [weak self] params, reply in
      self?.runPatternCommand("pattern.transform", params: params, reply: reply)
    }
    win.center()
    win.makeKeyAndOrderFront(nil)
  }

  // Native actions use the same revision-checked command boundary as agents.
  // A prepared preview supplies its original revision; never refresh that token.
  func runPatternCommand(_ method: String, params: [String: Any], reply: @escaping PatternToolsPanel.Reply) {
    if params["expectedRevision"] != nil {
      handleAutomation(method, params: params, reply: reply)
    } else {
      handleAutomation("document.get", params: [:]) { [weak self] response in
        guard let self else { return }
        guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String else {
          reply(response); return
        }
        var request = params
        request["expectedRevision"] = revision
        self.handleAutomation(method, params: request, reply: reply)
      }
    }
  }

  @objc func mixPaste() { patternView.pasteSelection(mode: "mix") }
  @objc func mergePaste() { patternView.pasteSelection(mode: "merge") }
}
