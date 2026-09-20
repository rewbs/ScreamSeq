import AppKit

extension InterfaceTests {
  static func mixerStripsChecks() throws {
    let buses: [[String: Any]] = (0..<240).map { index -> [String: Any] in
      let name = index == 239 ? "Master" : "Track \(index + 1)"
      let kind = index == 239 ? "master" : ["track", "group", "return", "track"][index % 4]
      return ["id": "bus\(index)", "name": name, "kind": kind,
       "gainDB": -Double(index % 12), "preGainDB": 0.0, "pan": 0.0, "width": 1.0,
       "inserts": [], "sends": []]
    }
    let editor = MixerEditor(frame: .zero)
    editor.widthAnchor.constraint(equalToConstant: 1040).isActive = true
    editor.heightAnchor.constraint(equalToConstant: 650).isActive = true
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1040, height: 650), styleMask: [.titled], backing: .buffered, defer: false)
    window.contentView = editor; window.setContentSize(NSSize(width: 1040, height: 650))
    editor.revision = "song:0"; editor.update(["active": true, "buses": buses]); editor.layoutSubtreeIfNeeded()
    let strips = editor.strips
    try require(editor.bounds.width == 1040 && strips.visible[0]?.busID == "bus0", "Mixer uses a real constrained compact viewport")
    try require(strips.visible.count >= 6 && strips.visible.count <= 9 && strips.createdCount <= 9, "Maximum graph only allocates visible controls and neighboring strips")
    for strip in strips.visible.values {
      strip.layoutSubtreeIfNeeded()
      func check(_ view: NSView) throws {
        if let control = view as? NSControl {
          let rect = control.convert(control.bounds, to: strip)
          try require(rect.width > 0 && rect.height > 0 && rect.minX >= -1 && rect.maxX <= strip.bounds.width + 1 && rect.minY >= -1 && rect.maxY <= strip.bounds.height + 1,
            "Strip control fits: \(type(of: control)) \(rect) within \(strip.bounds)")
        }
        for child in view.subviews { try check(child) }
      }
      try check(strip)
    }
    let first = strips.visible[0]!
    first.fader.trackingGesture = true
    strips.reveal("bus239")
    try require(strips.visible[239]?.busID == "bus239" && strips.visible[0] === first && first.busID == "bus0", "Scrolling to Master preserves a live gesture's bus identity")
    first.fader.trackingGesture = false; first.fader.onFinish?()
    try require(strips.visible[0] == nil && strips.visible.count <= 9 && strips.createdCount <= 10, "Ending an offscreen gesture releases its pinned strip")
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    func answer(_ revision: String) { replies.removeFirst()(["result": ["revision": revision, "data": [:]]]) }
    let master = strips.visible[239]!
    master.pan.doubleValue = 0.25; master.slide(master.pan)
    try require(calls.last?.1["bus"] as? String == "bus239" && calls.last?.1["pan"] as? Double == 0.25, "Recycled strip action targets its current stable bus")
    answer("song:1")
    master.onInspect?(master.busID)
    try require(editor.selectedID == "bus239" && editor.viewMode.selectedSegment == 1 && editor.strips.isHidden, "Routing action preserves selected bus in inspector")
    editor.viewMode.selectedSegment = 0; editor.changeViewMode(); strips.reveal("bus0")
    try require(editor.selectBus("bus0"), "Select the first track after committed edit")
    let strip = strips.visible[0]!
    strip.fader.trackingGesture = true; strip.fader.doubleValue = -3; strip.slide(strip.fader)
    strip.fader.doubleValue = -6; strip.slide(strip.fader)
    strip.fader.trackingGesture = false; strip.fader.onFinish?()
    try require(calls.count == 2 && calls.last?.1["preview"] as? Bool == true, "Fast strip drag coalesces into one pending preview")
    answer("song:1")
    try require(calls.count == 3 && calls.last?.1["preview"] as? Bool == false && calls.last?.1["gainDB"] as? Double == -6, "Strip release commits the final gain once")
    strip.pan.doubleValue = -0.5; strip.slide(strip.pan)
    try require(strip.fader.doubleValue == -6 && strip.pan.doubleValue == -0.5, "Pending commit stays visible while a second gesture begins")
    try require(!editor.selectBus("bus1"), "Pending controls block retargeting another bus")
    answer("song:2")
    try require(calls.last?.1["expectedRevision"] as? String == "song:2" && calls.last?.1["pan"] as? Double == -0.5, "Queued gesture uses the preceding commit revision")
    answer("song:3")
    strip.width.doubleValue = 1.7; strip.slide(strip.width)
    replies.removeFirst()(["error": ["message": "Song changed; reload"]])
    try require(strip.width.doubleValue == 1 && strip.fader.doubleValue == -6 && editor.status.stringValue.contains("Song changed"), "Rejected gesture restores saved controls without losing prior commits")
    editor.load(); let count = calls.count
    strip.pan.doubleValue = 0.9; strip.slide(strip.pan)
    try require(calls.count == count && strip.pan.doubleValue == -0.5, "A pending graph reload rejects control changes and restores displayed values")
    replies.removeFirst()(["result": ["revision": "song:4", "data": ["active": true, "buses": buses]]])
    strip.preGain.stringValue = "-9.5"
    strip.controlTextDidEndEditing(Notification(name: NSControl.textDidEndEditingNotification, object: strip.preGain))
    try require(calls.last?.1["preGainDB"] as? Double == -9.5 && calls.last?.1["bus"] as? String == "bus0", "Typed strip gain reaches the same revision-checked API")
    answer("song:5")
    strip.preGain.stringValue = "NaN"; let beforeInvalid = calls.count
    strip.controlTextDidEndEditing(Notification(name: NSControl.textDidEndEditingNotification, object: strip.preGain))
    try require(calls.count == beforeInvalid && strip.preGain.stringValue == "-9.5", "Invalid numeric input restores the saved gain")
    strip.prePan.doubleValue = -0.6; strip.slide(strip.prePan)
    try require(calls.last?.1["prePan"] as? Double == -0.6 && calls.last?.1["pan"] == nil, "Pre-insert balance is independent of output balance in the API")
    answer("song:6")
    try require(editor.controls.first(where: { $0.key == "prePan" })?.value.doubleValue == -60, "The routing inspector shows independent input balance as percent")
    let levels: [[AnyHashable: Any]] = buses.enumerated().map { ["bus": $0.element["id"]!, "left": Double($0.offset + 1) / 240, "right": Double($0.offset + 1) / 480] }
    let allocations = strips.createdCount, begin = CFAbsoluteTimeGetCurrent()
    for _ in 0..<600 { editor.showMeters(levels) }
    let elapsed = (CFAbsoluteTimeGetCurrent() - begin) * 1000
    try require(strips.createdCount == allocations && strip.meter.left == 1.0 / 240 && strip.meter.right == 1.0 / 480, "Meter refresh never reallocates controls and keeps independent stereo peaks")
    strips.reveal("bus239")
    try require(strips.visible[239]?.meter.left == 1 && strips.visible[239]?.peak.textColor == .systemRed, "Newly exposed strips immediately show their cached peaks and clipping")
    for index in stride(from: 239, through: 0, by: -7) { strips.reveal("bus\(index)") }
    try require(strips.createdCount <= 10 && strips.visible.count <= 9, "Full-graph traversal reuses a bounded strip pool")
    strips.reveal("bus0")
    let pinned = strips.visible[0]!; pinned.fader.trackingGesture = true
    var reordered = buses; reordered.swapAt(0, 1); strips.update(reordered, selected: "bus0")
    try require(pinned.busID == "bus0", "Structural refresh cannot retarget a held fader")
    pinned.fader.trackingGesture = false; pinned.fader.onFinish?()
    try require(strips.visible[0]?.busID == "bus1" && strips.visible[0]?.meter.left == 2.0 / 240, "Released strip binds new identity and matching meter together")
    let traversalAllocations = strips.createdCount
    strips.update([], selected: nil); strips.showMeters([])
    try require(strips.visible.isEmpty, "An empty graph releases all idle strips")
    var withInsert = buses; withInsert[0]["inserts"] = ["stable-effect"]
    editor.update(["active": true, "buses": withInsert, "plugins": [["id": "stable-effect", "name": "Gain", "slot": 3]]])
    var opened = "", generic = ""
    editor.onOpenPlugin = { opened = $0 }; editor.onPluginControls = { generic = $0 }
    editor.openInsert(); editor.openInsert(controls: true)
    try require(opened == "stable-effect" && generic == "stable-effect", "Custom and generic effect interfaces use stable identity instead of a stale rack slot")
    editor.loading = true; opened = ""; editor.openInsert()
    try require(opened.isEmpty, "Pending graph edits cannot open a retargeted effect")
    print(String(format: "Mixer strip diagnostic: 240 buses, 600 meter updates %.1f ms, %d control sets allocated through full traversal (not display cadence)", elapsed, traversalAllocations))
  }
}
