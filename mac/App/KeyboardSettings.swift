import AppKit

/// Native menu tracking runs a nested event loop before the selected action is
/// sent. Its unmodified keys belong to that menu, not the old window's transport.
final class MenuKeyboardFocus {
  private let center: NotificationCenter
  private let tracking = NSHashTable<NSMenu>.weakObjects()
  private var observers = [NSObjectProtocol]()
  init(center: NotificationCenter = .default) {
    self.center = center
    observers.append(center.addObserver(forName: NSMenu.didBeginTrackingNotification, object: nil, queue: .main) { [weak self] note in
      if let menu = note.object as? NSMenu { self?.tracking.add(menu) }
    })
    observers.append(center.addObserver(forName: NSMenu.didEndTrackingNotification, object: nil, queue: .main) { [weak self] note in
      if let menu = note.object as? NSMenu { self?.tracking.remove(menu) }
    })
  }
  func ownsUnmodifiedKey(_ event: NSEvent) -> Bool {
    event.type == .keyDown && !tracking.allObjects.isEmpty && KeyboardSettings.isDataTyping(event)
  }
  deinit { for observer in observers { center.removeObserver(observer) } }
}

final class KeyboardSettings {
  static let menuFocus = MenuKeyboardFocus()
  static func installInputContext() { _ = menuFocus; installInputTrace() }
  private static let tracesInput = CommandLine.arguments.contains("--keyboard-trace")
  private static var traceObservers = [NSObjectProtocol]()
  private static var traceMonitor: Any?
  private static var traceCount = 0
  static func installInputTrace() {
    guard tracesInput, traceMonitor == nil else { return }
    for name in [NSMenu.didBeginTrackingNotification, NSMenu.didEndTrackingNotification,
      NSMenu.willSendActionNotification, NSMenu.didSendActionNotification,
      NSWindow.didBecomeKeyNotification, NSWindow.didResignKeyNotification] {
      traceObservers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { notification in
        traceInput(notification.name.rawValue, detail: "object=\(String(describing: notification.object)) info=\(String(describing: notification.userInfo))")
      })
    }
    traceMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .leftMouseDown, .leftMouseUp]) { event in
      traceInput("event", event: event); return event
    }
  }
  static func traceInput(_ stage: String, event: NSEvent? = nil, detail: String = "") {
    guard tracesInput, traceCount < 8192 else { return }
    traceCount += 1
    let event = event ?? NSApp.currentEvent
    func window(_ value: NSWindow?) -> [String: Any] {
      guard let value else { return [:] }
      return ["number":value.windowNumber, "title":value.title, "key":value.isKeyWindow,
        "focus":value.firstResponder.map{String(describing:type(of:$0))} ?? "none"]
    }
    var record: [String: Any] = ["stage":stage, "time":Date().timeIntervalSince1970,
      "keyWindow":window(NSApp.keyWindow), "eventWindow":window(event?.window), "detail":String(detail.prefix(1024))]
    if let event {
      record["eventType"]=event.type.rawValue; record["eventTime"]=event.timestamp
      if event.type == .keyDown || event.type == .keyUp {
        record["code"]=event.keyCode; record["text"]=String((event.characters ?? "").prefix(64)); record["modifiers"]=event.modifierFlags.rawValue
      }
    }
    if let data=try? JSONSerialization.data(withJSONObject:record,options:[.sortedKeys]),let line=String(data:data,encoding:.utf8) {
      FileHandle.standardError.write(Data(("KEYBOARD_TRACE "+line+"\n").utf8))
    }
  }
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
  // One rule for every musical/data typing path (notes, instrument, volume,
  // FX codes and values): Command, Control and Option chords are shortcuts.
  static func isDataTyping(_ flags: NSEvent.ModifierFlags) -> Bool {
    flags.intersection([.command, .control, .option]).isEmpty
  }
  static func isDataTyping(_ event: NSEvent) -> Bool { isDataTyping(event.modifierFlags) }
  // Opening a panel can leave already-queued keystrokes addressed to its owner.
  // Keyboard focus belongs to the current key window, even for those events.
  static func focusWindow(for event: NSEvent, keyWindow: NSWindow?) -> NSWindow? {
    keyWindow ?? event.window
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
