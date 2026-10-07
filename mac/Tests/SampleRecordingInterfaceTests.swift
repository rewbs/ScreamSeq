import AppKit

extension InterfaceTests {
  static func sampleRecordingChecks() throws {
    let view = SampleRecordingView(frame: NSRect(x: 0, y: 0, width: 650, height: 540))
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), permissionRequests = 0
    var permissionReply: ((String?) -> Void)?, revision = "song:1", added = [(Int, Int)]()
    view.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    view.onPermission = { reply in permissionRequests += 1; permissionReply = reply }
    var document = "document-a"
    view.onDocument = { document }
    view.onRevision = { revision }; view.onAdded = { added.append(($0, $1)) }
    func answer(_ data: [String: Any]) { replies.removeFirst()(["result": ["revision": revision, "data": data]]) }
    view.loadDevices()
    answer(["devices": [["id": "microphone", "name": "Test input", "channels": 2, "default": true]]])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(permissionRequests == 0 && calls.allSatisfy { $0.0 != "sample.recording.start" }, "Opening sampler does not request permission or start capturing")
    view.name.stringValue = "A name being edited"
    view.poll()
    try require(!view.pending && view.name.isEnabled && view.record.isEnabled, "A background take poll never disables the name editor or Record")
    answer(["take": "", "capturing": false, "frames": 0])
    try require(view.name.stringValue == "A name being edited", "Polling preserves the name draft")
    view.channels.selectItem(at: 1)
    view.loadDevices(); answer(["devices": [["id": "microphone", "name": "Test input", "channels": 2, "default": true]]]); answer(["take": "", "capturing": false, "frames": 0])
    try require(view.channels.indexOfSelectedItem == 1, "Refreshing idle inputs preserves the chosen stereo pair")
    view.start(); view.start()
    try require(permissionRequests == 1, "Repeated Record clicks cannot duplicate a pending permission request")
    permissionReply?("Denied")
    try require(view.status.stringValue == "Denied" && !view.pending && view.take.isEmpty, "Denied permission leaves no take and explains the problem")
    view.start(); document = "document-b"; permissionReply?(nil)
    try require(view.take.isEmpty && calls.allSatisfy { $0.0 != "sample.recording.start" } && view.status.stringValue.contains("song changed"), "Permission completion cannot start a take in a replacement song")
    view.poll(); view.start(); permissionReply?(nil); document = "document-c"
    answer(["take": "", "capturing": false, "frames": 0])
    try require(!view.pending && calls.allSatisfy { $0.0 != "sample.recording.start" } && view.status.stringValue.contains("song changed"), "Queued start rechecks its captured document after a delayed poll")
    view.start(); permissionReply?(nil)
    try require(calls.last?.0 == "sample.recording.start" && calls.last?.1["device"] as? String == "microphone", "Explicit Record passes chosen stable input identity")
    answer(["take": "one", "capturing": true, "frames": 48000, "seconds": 1.0, "sampleRate": 48000, "channels": 1])
    try require(view.stop.isEnabled && !view.add.isEnabled && !view.record.isEnabled, "Capturing enables Stop and prevents a second take or premature import")
    let window = NSWindow(contentRect: view.frame, styleMask: [.titled, .closable], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = view
    view.poll()
    try require(view.stop.isEnabled, "Polling never disables Stop")
    try require(!view.windowShouldClose(window) && calls.last?.0 == "sample.recording.get" && view.pending, "Close queues its Stop after an in-flight read")
    answer(["take": "old-read", "capturing": true, "frames": 1])
    try require(calls.last?.0 == "sample.recording.stop" && calls.last?.1["take"] as? String == "one" && view.take == "one", "A queued Stop keeps its exact take; obsolete poll data cannot retarget it")
    answer(["take": "one", "capturing": false, "frames": 48000, "seconds": 1.0])
    try require(view.take == "one" && view.add.isEnabled, "Stopping/closing preserves recorded audio")
    revision = "song:2"; view.name.stringValue = "Voice"; view.instrument.state = .on; view.poll(); view.commit()
    revision = "song:2-after-poll"
    answer(["take": "obsolete", "capturing": false, "frames": 5])
    try require(calls.last?.1["expectedRevision"] as? String == "song:2-after-poll" && calls.last?.1["take"] as? String == "one", "Commit guards latest append revision without rejecting unrelated edits during recording")
    replies.removeFirst()(["error": ["code": -32001, "message": "Song changed"]])
    try require(view.take == "one" && view.frames == 48000 && view.add.isEnabled, "Rejected commit retains the complete take for retry")
    revision = "song:3"; view.commit(); answer(["sample": 4, "instrument": 2])
    try require(view.take.isEmpty && added.count == 1 && added[0].0 == 4 && added[0].1 == 2, "Successful commit clears only the consumed take and selects both created assets")
    view.apply(["take": "limit", "capturing": false, "frames": 32, "limitReached": true,
      "device": "retained-interface", "deviceName": "Retained interface", "firstChannel": 2, "channels": 2])
    try require(view.devices.selectedItem?.representedObject as? String == "retained-interface" && view.channels.titleOfSelectedItem == "Stereo · Inputs 3–4", "A retained take displays its actual device and channel pair even after device removal")
    try require(view.status.stringValue.contains("limit reached") && view.add.isEnabled, "Bounded capture completion stays importable")
    view.discardTake(); answer(["take": "", "capturing": false, "frames": 0])
    try require(view.take.isEmpty && !view.add.isEnabled, "Discard retires take state")
  }
}
