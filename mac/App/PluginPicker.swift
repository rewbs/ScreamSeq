import AppKit

enum PluginPicker {
  static func make(_ plugins: [[AnyHashable: Any]], builtInOnly: Bool = false) -> (NSAlert, NSPopUpButton) {
    let alert = NSAlert()
    alert.messageText = builtInOnly ? "Add a built-in effect" : "Add an effect or instrument"
    alert.informativeText =
      builtInOnly ? "Use these effects on tracks, groups, returns or Master. Every parameter supports automation."
      : "Built-in effects are ready immediately. Installed plugins are cached; rescan after installing or updating them."
    let picker = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 400, height: 30))
    picker.setAccessibilityLabel("Installed plugins")
    for plugin in plugins {
      let name = plugin["name"] as? String ?? "Plugin"
      let format = plugin["format"] as? String ?? "AU"
      let kind = (plugin["isInstrument"] as? Bool ?? false) ? "Instrument" : "Effect"
      // NSPopUpButton.addItem(withTitle:) replaces equal titles. Keep every
      // descriptor in order, including different bundles with the same label.
      picker.menu?.addItem(
        NSMenuItem(title: "\(name) · \(format) \(kind)", action: nil, keyEquivalent: ""))
    }
    alert.accessoryView = picker
    alert.addButton(withTitle: "Add").isEnabled = !plugins.isEmpty
    alert.addButton(withTitle: "Cancel")
    if !builtInOnly { alert.addButton(withTitle: "Rescan") }
    return (alert, picker)
  }
}
