import AppKit
extension AppController {
  @objc func showPatternCommands() {
    guard !busy, model.editable else { return }
    if let commandPickerWindow, let picker = commandPickerWindow.contentView as? PatternCommandPicker {
      picker.capture(); commandPickerWindow.makeKeyAndOrderFront(nil); return
    }
    let picker = PatternCommandPicker(frame: .zero)
    picker.onContext = { [weak self] in
      guard let self else { return (PatternModel([:]), 0, 0, 3) }
      return (self.model, self.patternView.cursorRow, self.patternView.cursorChannel, self.patternView.column)
    }
    picker.onRequest = { [weak self] params, reply in self?.handleAutomation("pattern.apply", params: params, reply: reply) }
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 740, height: 560), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Pattern commands"; win.minSize = NSSize(width: 680, height: 500)
    win.isReleasedWhenClosed = false; win.delegate = self; win.contentView = picker
    commandPickerWindow = win; picker.capture(); win.center(); win.makeKeyAndOrderFront(nil)
  }
  func updateCommandHelp() {
    cursorLabel.stringValue = String(format: "EDIT P%02d · R%03d · CH%02d", model.pattern, patternView.cursorRow, patternView.cursorChannel + 1)
    commandHelpLabel.stringValue = patternView.currentCommandHelp
    commandHelpLabel.toolTip = commandHelpLabel.stringValue
  }
}
