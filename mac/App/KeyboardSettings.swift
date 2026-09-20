import AppKit

final class KeyboardSettings {
  static var lowKeys: String {
    UserDefaults.standard.string(forKey: "noteKeysLow") ?? "zsxdcvgbhnjm"
  }
  static var highKeys: String {
    UserDefaults.standard.string(forKey: "noteKeysHigh") ?? "q2w3er5t6y7u"
  }
  static var transportKey: UInt16 {
    UserDefaults.standard.bool(forKey: "returnStartsPlayback") ? 36 : 49
  }
  static var rowHeight: Float {
    let value = UserDefaults.standard.float(forKey: "patternRowHeight")
    return value >= 18 && value <= 34 ? value : 22
  }
  static func note(for key: String) -> Int? {
    if let index = Array(lowKeys).firstIndex(where: { String($0) == key }) { return index }
    if let index = Array(highKeys).firstIndex(where: { String($0) == key }) { return index + 12 }
    return nil
  }
  var window: NSWindow?
  func show(onApply: @escaping () -> Void) {
    if let window {
      window.makeKeyAndOrderFront(nil)
      return
    }
    let win = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 510, height: 360), styleMask: [.titled, .closable],
      backing: .buffered, defer: false)
    win.title = "Keyboard and display"
    win.isReleasedWhenClosed = false
    window = win
    let low = NSTextField(string: Self.lowKeys)
    let high = NSTextField(string: Self.highKeys)
    low.setAccessibilityLabel("Lower octave note keys")
    high.setAccessibilityLabel("Upper octave note keys")
    let transport = NSPopUpButton()
    transport.addItems(withTitles: ["Space", "Return"])
    transport.selectItem(at: Self.transportKey == 49 ? 0 : 1)
    let zoom = NSPopUpButton()
    for value in [18, 22, 26, 30, 34] {
      zoom.addItem(withTitle: "\(value) points")
      zoom.lastItem?.tag = value
    }
    zoom.selectItem(withTag: Int(Self.rowHeight))
    let status = Theme.label("", size: 11, color: Theme.gold)
    let apply = ActionButton("Apply") {
      let a = low.stringValue.lowercased()
      let b = high.stringValue.lowercased()
      guard a.count == 12, b.count == 12, Set(a + b).count == 24, !a.contains(" "), !b.contains(" ")
      else {
        status.stringValue = "Choose 24 distinct keys, 12 for each octave."
        return
      }
      UserDefaults.standard.set(a, forKey: "noteKeysLow")
      UserDefaults.standard.set(b, forKey: "noteKeysHigh")
      UserDefaults.standard.set(transport.indexOfSelectedItem == 1, forKey: "returnStartsPlayback")
      UserDefaults.standard.set(zoom.selectedTag(), forKey: "patternRowHeight")
      onApply()
      win.close()
    }
    let content = stack(
      .vertical,
      [
        Theme.label("Note entry", size: 16, weight: .semibold),
        Theme.label(
          "Enter twelve keys in chromatic order, C through B, for each octave.", size: 11,
          color: Theme.muted), labeled("LOWER OCTAVE", low), labeled("UPPER OCTAVE", high),
        stack(
          .horizontal, [labeled("PLAY / STOP", transport), labeled("PATTERN ROW HEIGHT", zoom)],
          spacing: 24), status, apply,
      ], spacing: 14)
    content.fill(win.contentView!, inset: 24)
    win.center()
    win.makeKeyAndOrderFront(nil)
  }
}
