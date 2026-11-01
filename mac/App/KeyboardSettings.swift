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
  static var volatileMapping: (lower:String,upper:String)?
  static var playStopKeysProvider: (() -> [String])?
  static var legacyPlayStopKeys:[String] {UserDefaults.standard.bool(forKey:"returnStartsPlayback") ? ["return"]:["space"]}
  static var playStopKeys:[String] {playStopKeysProvider?() ?? legacyPlayStopKeys}
  static var lowKeys: String {
    volatileMapping?.lower ?? (UserDefaults.standard.dictionary(forKey:"musicalNoteKeys")?["lower"] as? String) ?? UserDefaults.standard.string(forKey:"noteKeysLow") ?? "zsxdcvgbhnjm"
  }
  static var highKeys: String {
    volatileMapping?.upper ?? (UserDefaults.standard.dictionary(forKey:"musicalNoteKeys")?["upper"] as? String) ?? UserDefaults.standard.string(forKey: "noteKeysHigh") ?? "q2w3er5t6y7u"
  }
  static var transportKey: UInt16 {
    playStopKeys == ["space"] ? 49 : playStopKeys == ["return"] ? 36 : UInt16.max
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
  struct Failure:Error {let code:Int,message:String}
  static func validated(_ lower:String,_ upper:String) throws -> (lower:String,upper:String) {
    let a=lower.lowercased(),b=upper.lowercased(),allowed=Set("abcdefghijklmnopqrstuvwxyz0123456789-=[];'`\\,./<")
    guard a.count==12,[12,13].contains(b.count),Set(a+b).count==a.count+b.count,Set(a+b).isSubset(of:allowed) else {
      throw Failure(code:-32602,message:"Use 12 lower keys and 12 or 13 upper keys, all distinct letters, digits or unshifted punctuation.")
    }
    return (a,b)
  }
  static func snapshot(keys:[String])->[String:Any] {
    let fields:[String:Any] = ["lower":lowKeys.uppercased(),"upper":highKeys.uppercased(),"playStopKeys":keys]
    let revision=String(data:try! JSONSerialization.data(withJSONObject:fields,options:[.sortedKeys]),encoding:.utf8)!
    return fields.merging(["revision":revision]){_,new in new}
  }
  static func publish(_ mapping:(lower:String,upper:String),persist:Bool) {
    if persist {UserDefaults.standard.set(["lower":mapping.lower,"upper":mapping.upper],forKey:"musicalNoteKeys");volatileMapping=nil}
    else {volatileMapping=mapping}
  }
  var window:NSWindow?
  let low=NSTextField(),high=NSTextField(),transport=NSPopUpButton(),zoom=NSPopUpButton()
  let status=Theme.label("",size:11,color:Theme.gold)
  var captured=[String:Any](),onRead:(()->[String:Any])?,onWrite:(([String:Any]) throws -> [String:Any])?,onApply:(()->Void)?
  var persistPreferences=true
  func load() {
    guard let state=onRead?() else{return};captured=state
    low.stringValue=state["lower"] as? String ?? "";high.stringValue=state["upper"] as? String ?? ""
    let keys=state["playStopKeys"] as? [String] ?? [];showBinding(keys)
    zoom.selectItem(withTag:Int(Self.rowHeight));status.stringValue="Current preferences loaded. Held notes retain their original pitch."
  }
  func showBinding(_ keys:[String]) {
    transport.removeAllItems();transport.addItems(withTitles:["Space","Return","Keep: "+(keys.isEmpty ? "disabled":keys.joined(separator:" → "))])
    transport.selectItem(at:keys==["space"] ? 0:keys==["return"] ? 1:2)
  }
  func apply() {
    guard let revision=captured["revision"] as? String,let onWrite else{return}
    let keys=transport.indexOfSelectedItem==0 ? ["space"]:transport.indexOfSelectedItem==1 ? ["return"]:captured["playStopKeys"] as? [String] ?? []
    do {
      captured=try onWrite(["expectedKeyboard":revision,"lower":low.stringValue,"upper":high.stringValue,"playStopKeys":keys])
      showBinding(captured["playStopKeys"] as? [String] ?? [])
      if persistPreferences {UserDefaults.standard.set(zoom.selectedTag(),forKey:"patternRowHeight")}
      onApply?();status.stringValue="Keyboard settings saved. Song data and Undo are unchanged."
    } catch let error as Failure {status.stringValue=error.message}
      catch {status.stringValue=error.localizedDescription}
  }
  var snapshot:[String:Any] {["visible":window?.isVisible ?? false,"captured":captured,"draft":["lower":low.stringValue,"upper":high.stringValue,"transport":transport.indexOfSelectedItem],"status":status.stringValue]}
  func show(onApply:@escaping ()->Void) {
    self.onApply=onApply
    if let window {window.makeKeyAndOrderFront(nil);return}
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:590,height:420),styleMask:[.titled,.closable],backing:.buffered,defer:false)
    win.title="Keyboard and display";win.isReleasedWhenClosed=false;window=win
    low.setAccessibilityLabel("Lower octave note keys");high.setAccessibilityLabel("Upper octave note keys, optional top C");transport.setAccessibilityLabel("Play / Stop shortcut")
    for value in [18,22,26,30,34] {zoom.addItem(withTitle:"\(value) points");zoom.lastItem?.tag=value}
    let help=Theme.label("Enter 12 lower-octave keys and 12 upper-octave keys, or 13 including top C. Use distinct letters, digits or unshifted punctuation. Custom Play / Stop bindings are also available in Commands.",size:11,color:Theme.muted)
    help.lineBreakMode = .byWordWrapping;help.maximumNumberOfLines=0;help.preferredMaxLayoutWidth=535
    status.lineBreakMode = .byWordWrapping;status.maximumNumberOfLines=0;status.preferredMaxLayoutWidth=535
    let buttons=stack(.horizontal,[ActionButton("Apply"){[weak self] in self?.apply()},ActionButton("Reload preferences"){[weak self] in self?.load()},ActionButton("Use defaults"){[weak self] in
      self?.low.stringValue="zsxdcvgbhnjm";self?.high.stringValue="q2w3er5t6y7ui";self?.transport.selectItem(at:0);self?.status.stringValue="Defaults in draft; Apply to save."
    },ActionButton("Close"){[weak win] in win?.close()}])
    let content=stack(.vertical,[Theme.label("Note entry",size:16,weight:.semibold),help,labeled("LOWER OCTAVE",low),labeled("UPPER OCTAVE",high),stack(.horizontal,[labeled("PLAY / STOP",transport),labeled("PATTERN ROW HEIGHT",zoom)],spacing:24),status,buttons],spacing:14)
    content.stretchAcrossAxis();content.fill(win.contentView!,inset:24);load();win.center();win.makeKeyAndOrderFront(nil);win.makeFirstResponder(low)
  }
}
