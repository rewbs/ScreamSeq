import AppKit

extension InterfaceTests {
  static func recordingData(_ data: [String: Any], document: String = "song", revision: String = "song:1") -> [String: Any] {
    var result = data
    if let take = data["take"] as? String, !take.isEmpty {
      result["documentId"] = data["documentId"] ?? document
      result["baseRevision"] = data["baseRevision"] ?? revision
    }
    return result
  }
  static func sampleRecordingChecks() throws {
    let view = SampleRecordingView(frame: NSRect(x: 0, y: 0, width: 650, height: 540))
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), permissionRequests = 0
    var permissionReply: ((String?) -> Void)?, revision = "song:1", added = [(Int, Int)]()
    view.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    view.onPermission = { reply in permissionRequests += 1; permissionReply = reply }
    var document = "document-a"
    view.onDocument = { document }
    view.onRevision = { revision }; view.onAdded = { added.append(($0, $1)) }
    func answer(_ data: [String: Any]) {
      replies.removeFirst()(["result": ["documentId": document, "revision": revision,
        "data": recordingData(data, document: document, revision: revision)]])
    }
    view.loadDevices()
    answer(["devices": [["id": "microphone", "name": "Test input", "channels": 2, "default": true]]])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(!view.needsDocumentCompletion,"An idle sampler does not block document replacement")
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
    try require(view.needsDocumentCompletion,"A pending permission request retains the original document")
    try require(permissionRequests == 1, "Repeated Record clicks cannot duplicate a pending permission request")
    permissionReply?("Denied")
    try require(view.needsDocumentCompletion,"Permission refusal keeps the document-owned name draft")
    view.resetSetup()
    try require(!view.needsDocumentCompletion,"Explicit setup discard releases document admission after permission refusal")
    try require(!view.pending && view.take.isEmpty, "Denied permission leaves no take")
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
    try require(view.needsDocumentCompletion,"A stopped hidden take still protects its song")
    try require(view.take == "one" && view.add.isEnabled, "Stopping/closing preserves recorded audio")
    revision = "song:2"; view.name.stringValue = "Voice"; view.instrument.state = .on; view.poll(); view.commit()
    revision = "song:2-after-poll"
    answer(["take": "obsolete", "capturing": false, "frames": 5])
    try require(calls.last?.1["expectedRevision"] as? String == "song:2" && calls.last?.1["take"] as? String == "one", "Queued commit keeps its original captured revision and exact take")
    replies.removeFirst()(["error": ["code": -32001, "message": "Song changed", "data": ["writeOutcome": "notCommitted"]]])
    try require(view.take == "one" && view.frames == 48000 && view.add.isEnabled, "Rejected commit retains the complete take for retry")
    revision = "song:3"; view.commit(); answer(["take": "one", "sample": 4, "instrument": 2, "dryRun": false])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(view.take.isEmpty && added.count == 1 && added[0].0 == 4 && added[0].1 == 2, "Successful commit clears only the consumed take and selects both created assets")
    view.apply(recordingData(["take": "limit", "capturing": false, "frames": 32, "limitReached": true,
      "device": "retained-interface", "deviceName": "Retained interface", "firstChannel": 2, "channels": 2], document: document, revision: revision))
    try require(view.devices.selectedItem?.representedObject as? String == "retained-interface" && view.channels.titleOfSelectedItem == "Stereo · Inputs 3–4", "A retained take displays its actual device and channel pair even after device removal")
    try require(view.status.stringValue.contains("limit reached") && view.add.isEnabled, "Bounded capture completion stays importable")
    view.discardTake(); answer(["take": "limit", "discarded": true])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(!view.needsDocumentCompletion,"Explicit take discard releases its document protection")
    try require(view.take.isEmpty && !view.add.isEnabled, "Discard retires take state")
    try require(!view.details.stringValue.contains("frames"), "Discard removes the retired take's frame count")
    try require(!view.record.isEnabled, "Discarding a disconnected-device take cannot arm its unavailable input")
    try sampleRecordingDeviceIdentityChecks()
    try sampleRecordingActiveNameChecks()
    try sampleRecordingCompletionChecks()

  }
  static func sampleRecordingActiveNameChecks() throws {
    let view = SampleRecordingView(frame: NSRect(x: 0, y: 0, width: 650, height: 540))
    let window = NSWindow(contentRect: view.frame, styleMask: [.titled], backing: .buffered, defer: false)
    window.contentView = view
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    view.onDocument = { "song" }; view.onRevision = { "song:1" }
    view.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    func answer(_ data: [String: Any]) { replies.removeFirst()(["result": ["documentId": "song", "revision": "song:1", "data": recordingData(data)]]) }
    window.makeFirstResponder(view.name)
    guard let editor = view.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Recorder name field editor did not attach") }
    editor.insertText("Retained live name", replacementRange: NSRange(location: 0, length: editor.string.count))
    editor.setSelectedRange(NSRange(location: 9, length: 4))
    view.loadDevices()
    try require(view.name.currentEditor() === editor && editor.string == "Retained live name" && editor.selectedRange() == NSRange(location: 9, length: 4),
      "Refreshing inputs preserves the active name field editor, text and selection")
    answer(["devices": [["id": "input", "name": "Test input", "channels": 2, "default": true]]])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(view.name.currentEditor() === editor && editor.string == "Retained live name", "Refresh and its take poll never cancel name editing")
    editor.setMarkedText("draft", selectedRange: NSRange(location: 5, length: 0), replacementRange: NSRange(location: 0, length: editor.string.count))
    view.apply(recordingData(["take": "take", "capturing": false, "frames": 32])); let count = calls.count
    view.commit()
    try require(editor.hasMarkedText() && calls.count == count && view.take == "take", "Adding a take waits for name composition to finish")
    editor.unmarkText(); editor.insertText("Captured live name", replacementRange: NSRange(location: 0, length: editor.string.count))
    view.commit()
    try require(calls.last?.0 == "sample.recording.commit" && calls.last?.1["name"] as? String == "Captured live name", "Add captures the active field editor's latest text")
    editor.insertText("Newer setup name", replacementRange: NSRange(location: 0, length: editor.string.count))
    answer(["take": "take", "sample": 4, "instrument": 0, "dryRun": false])
    answer(["take": "", "capturing": false, "frames": 0])
    try require(view.take.isEmpty && view.name.currentEditor() === editor && editor.string == "Newer setup name", "A delayed Add completion preserves newer setup input")
    window.makeFirstResponder(nil)
  }
  static func sampleRecordingDeviceIdentityChecks() throws {
    let view = SampleRecordingView(frame: .zero)
    let a: [String: Any] = ["id": "same-a", "name": "USB Input", "channels": 2]
    let b: [String: Any] = ["id": "same-b", "name": "USB Input", "channels": 4]
    let builtIn: [String: Any] = ["id": "default", "name": "Built-in", "channels": 1, "default": true]
    var devices = [a, b, builtIn], permissions = 0
    view.onPermission = { _ in permissions += 1 }; view.onDocument = { "song" }; view.onRevision = { "song:1" }
    view.onRequest = { method, _, reply in
      reply(["result": ["data": method == "sample.recording.devices" ? ["devices": devices] : ["take": "", "capturing": false, "frames": 0]]])
    }
    view.loadDevices()
    try require(view.devices.numberOfItems == 3 && view.devices.item(at: 0)?.representedObject as? String == "same-a" && view.devices.item(at: 1)?.representedObject as? String == "same-b", "Duplicate input names retain separate hardware identities")
    view.devices.selectItem(at: 1); view.deviceChanged(); view.channels.selectItem(at: 5)
    view.name.stringValue = "Retained setup"; view.limit.selectItem(at: 1); view.instrument.state = .on
    try require(view.channels.titleOfSelectedItem == "Stereo · Inputs 3–4", "The second duplicate input uses its own channel count")
    devices = [builtIn, a]; view.loadDevices(); view.start()
    try require(view.devices.selectedItem?.representedObject as? String == "same-b" && view.devices.titleOfSelectedItem?.contains("disconnected") == true && !view.record.isEnabled && permissions == 0, "Refresh retains a disconnected selected UID and refuses Record instead of silently choosing the default")
    try require(view.channels.titleOfSelectedItem == "Stereo · Inputs 3–4" && view.name.stringValue == "Retained setup" && view.limit.indexOfSelectedItem == 1 && view.instrument.state == .on, "Disconnection preserves the complete configured capture intent")
    devices = [b, builtIn, a]; view.loadDevices()
    try require(view.devices.indexOfSelectedItem == 0 && view.devices.selectedItem?.representedObject as? String == "same-b" && view.record.isEnabled && view.channels.titleOfSelectedItem == "Stereo · Inputs 3–4", "Reconnecting and reordering inputs restores availability without changing the selected UID or channels")
  }

  static func sampleRecordingCompletionChecks() throws {
    final class Harness {
      let view = SampleRecordingView(frame: .zero)
      var document = "song", revision = "song:1", selected = 0
      var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
      init() {
        view.onDocument = { [unowned self] in document }; view.onRevision = { [unowned self] in revision }
        view.onRequest = { [unowned self] method, params, reply in calls.append((method, params)); replies.append(reply) }
        view.onAdded = { [unowned self] _, _ in selected += 1 }
      }
      func answer(_ data: [String: Any]) {
        replies.removeFirst()(["result": ["documentId": document, "revision": revision,
          "data": InterfaceTests.recordingData(data, document: document, revision: revision)]])
      }
      func take(_ id: String = "take", capturing: Bool = false, frames: Int = 32) -> [String: Any] {
        InterfaceTests.recordingData(["take": id, "capturing": capturing, "frames": id.isEmpty ? 0 : frames], document: document, revision: revision)
      }
      func ready() { view.apply(take()) }
      func unknown() { replies.removeFirst()([:]) }
      func catalogue() { answer(["samples": [["id": "sample-id", "index": 1]], "instruments": []]) }
      var writes: Int { calls.filter { !["sample.recording.get", "document.get", "sample.recording.devices"].contains($0.0) }.count }
    }
    do {
      let h = Harness(), v = h.view
      v.name.stringValue = "Owned setup"; v.setupChanged()
      try require(v.needsDocumentCompletion && v.setupContext?.document == "song", "Name/output drafts protect their original song")
      h.document = "different"; h.revision = "different:1"; h.ready(); v.commit()
      try require(h.calls.isEmpty && v.name.stringValue == "Owned setup", "A different song cannot adopt the original setup draft")
      v.resetSetup()
      try require(v.take == "take" && v.name.stringValue == "Recording" && v.needsDocumentCompletion, "Discard setup preserves the retained take")
    }
    do {
      let h = Harness(), v = h.view
      v.loadDevices(); h.answer(["devices": [["id": "input", "name": "Input", "channels": 2]]]); h.answer(h.take(""))
      var permission: ((String?) -> Void)?; v.onPermission = { permission = $0 }
      v.start(); h.revision = "song:2"; v.limit.selectItem(at: 1); permission?(nil)
      try require(h.calls.last?.1["expectedRevision"] as? String == "song:1" && h.calls.last?.1["maxSeconds"] as? Int == 60, "Permission waits retain the original revision and capture settings")
      h.unknown(); let writes = h.writes
      v.start(); v.stopRecording(); v.discardTake(); v.poll(); v.resetSetup()
      try require(v.operation != nil && v.needsDocumentCompletion && h.writes == writes && !v.record.isEnabled, "Missing Start reply retains ownership and prevents another write or poll")
      let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 600), styleMask: [.titled], backing: .buffered, defer: false)
      try require(!v.windowShouldClose(window), "Unknown lifecycle refuses Close because input may still be active")
      v.reviewOperation(); h.answer(h.take("observed", capturing: true))
      try require(v.operation == nil && v.take == "observed" && v.capturing && h.writes == writes, "Lifecycle Review adopts current take without replay or a success claim")
      v.stopRecording(); h.replies.removeFirst()(["error": ["code": -32002, "message": "Uncertain"]])
      try require(v.operation != nil && !v.stop.isEnabled, "An unclassified busy error is not proof of rejection")
      v.reviewOperation(); h.answer(h.take("observed")); v.discardTake()
      h.replies.removeFirst()(["error": ["code": -32602, "message": "Refused", "data": ["writeOutcome": "notCommitted"]]])
      try require(v.operation == nil && v.take == "observed" && v.discard.isEnabled, "Explicit notCommitted releases the exact take for a deliberate new operation")
    }
    do {
      let h = Harness(), v = h.view; h.ready(); v.commit(); h.revision = "song:2"
      h.answer(["take": "take", "sample": 4, "instrument": 0, "dryRun": false]); h.unknown()
      try require(v.operation?.receipt != nil && v.take == "take" && !v.add.isEnabled, "Failed post-commit readback retains the known receipt and consumed take owner")
      v.reviewOperation(); h.answer(h.take("external", capturing: true))
      try require(v.take == "external" && v.capturing && h.selected == 1 && h.writes == 1, "Known Add completes with read-only Review and preserves a different external take")
    }
    do {
      let h = Harness(), v = h.view; h.ready(); v.commit(); h.revision = "song:2"
      h.answer(["take": "take", "sample": 4, "instrument": 0, "dryRun": false])
      h.document = "different"; h.revision = "different:1"; h.answer(h.take(""))
      try require(v.operation == nil && h.selected == 0 && v.status.stringValue.contains("selection was left unchanged"), "Late Add completion never selects numeric slots in a replacement song")
    }
    do {
      let h = Harness(), v = h.view; h.ready(); v.commit()
      h.answer(["take": "wrong", "sample": 4, "instrument": 0, "dryRun": false])
      try require(v.operation != nil && v.operation?.receipt == nil && v.take == "take" && h.selected == 0, "Wrong-take receipt is retained as unverified rather than accepted")
      v.reviewOperation(); h.catalogue(); h.answer(h.take("new", capturing: true))
      try require(v.operation?.observation != nil && v.acceptState.isEnabled, "Unknown Add Review reads validated assets and current take")
      v.name.stringValue = "Newer setup"; v.setupChanged(); v.acceptObservation()
      try require(v.operation?.observation == nil && h.replies.isEmpty && v.name.stringValue == "Newer setup", "Newer setup invalidates acknowledgement without a write")
      v.reviewOperation(); h.catalogue(); h.answer(h.take("new", capturing: true)); v.acceptObservation()
      h.answer(h.take("different", capturing: true))
      try require(v.operation != nil && v.operation?.observation == nil, "A replaced take invalidates acknowledgement")
      v.reviewOperation(); h.catalogue(); h.answer(h.take("new", capturing: true, frames: 33)); v.acceptObservation()
      h.answer(h.take("new", capturing: true, frames: 100))
      try require(v.operation == nil && v.take == "new" && v.frames == 100 && h.selected == 0 && h.writes == 1 && v.name.stringValue == "Newer setup", "Accept permits advancing frames but never replays Add, selects guessed assets or clears newer raw setup")
    }
    do {
      let h = Harness(), v = h.view; h.ready(); v.commit(); h.unknown(); v.reviewOperation()
      h.answer(["samples": [["id": "duplicate", "index": 1], ["id": "duplicate", "index": 2]], "instruments": []])
      try require(v.operation?.observation == nil && h.replies.isEmpty && !v.acceptState.isEnabled, "Duplicate asset identities cannot establish an observation")
      v.reviewOperation(); h.catalogue(); h.answer(["take": "", "capturing": true, "frames": -1])
      try require(v.operation != nil && v.operation?.observation == nil && h.writes == 1, "Malformed take readback leaves unknown Add retained")
    }
    do {
      let h = Harness(), v = h.view; h.ready(); v.poll()
      try require(h.calls.last?.1["take"] as? String == "take", "Background reads remain scoped to the retained take")
      h.answer(h.take("external"))
      try require(v.take == "take", "Background readback cannot silently replace a retained take")
      v.poll(adopt: true); h.answer(h.take("external"))
      try require(v.take == "external", "Explicit reopening can inspect an externally replaced take")
      let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 600), styleMask: [.titled], backing: .buffered, defer: false)
      window.contentView = v; window.autorecalculatesKeyViewLoop = false
      window.makeFirstResponder(v)
      let tab = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
        windowNumber: window.windowNumber, context: nil, characters: "\t", charactersIgnoringModifiers: "\t", isARepeat: false, keyCode: 48)!
      v.keyDown(with: tab)
      try require(window.firstResponder === v.refresh, "Root Tab enters the first available setup control")
      v.focusTabEntry(reverse: true)
      try require(window.firstResponder === v.discardSetup, "Root reverse Tab enters the last available action")
      try require(v.devices.nextKeyView === v.refresh && v.limit.nextKeyView === v.name && v.name.nextKeyView === v.instrument,
        "Recorder Tab order follows capture setup before take actions")
      window.makeFirstResponder(v.name)
      let editor = v.name.currentEditor() as? NSTextView
      editor?.setSelectedRange(NSRange(location: 2, length: 3)); v.focusEntry()
      try require(v.name.currentEditor() === editor && editor?.selectedRange() == NSRange(location: 2, length: 3), "Reopen focus entry leaves a child field editor and selection alone")
      window.makeFirstResponder(nil)
    }
  }

}
