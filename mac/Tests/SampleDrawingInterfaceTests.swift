import AppKit

extension InterfaceTests {
  static func drawingRetentionChecks() throws {
    let info: [String: Any] = ["index": 1, "name": "Captured", "frames": 1024, "channels": 2, "rate": 44100, "volume": 64, "pan": 128,
      "loop": false, "loopStart": 0, "loopEnd": 0, "pingpong": false, "reverseLoop": false,
      "sustainLoop": false, "sustainStart": 0, "sustainEnd": 0, "sustainPingpong": false, "sustainReverse": false]
    let samples: [[String: Any]] = [["index": 1, "id": "sample-one", "name": "Captured"], ["index": 2, "id": "sample-two", "name": "Other"]]
    func makeEditor() -> SampleEditor {
      let editor = SampleEditor(frame: .zero)
      editor.update(info, samples: samples, revision: "original")
      editor.waveform.frame = NSRect(x: 0, y: 0, width: 512, height: 180)
      editor.waveform.setViewport(256..<288); editor.waveform.peaksRange = 256..<288
      editor.waveform.peaks = Array(repeating: 0.25, count: 64)
      editor.waveformRevision = "original"; editor.waveformDocumentID = "original-song"
      editor.drawToggle.state = .on; editor.waveform.drawing = true
      return editor
    }
    func begin(_ editor: SampleEditor) throws {
      try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 260, value: -0.5)), "Loaded sample accepts an owned drawing gesture")
      editor.waveform.extendStroke(SampleStrokePoint(frame: 264, value: 0.5)); editor.waveform.finishStroke()
    }
    func response(_ data: [String: Any], _ revision: String = "accepted") -> [String: Any] {
      ["result": ["revision": revision, "documentId": "original-song", "data": data]]
    }
    func wave(_ params: [String: Any]) -> [String: Any] {
      var data = params; data["peaks"] = Array(repeating: 0.5, count: (params["bins"] as? Int ?? 0) * 2); return data
    }
    for failure in ["notCommitted", "unknown", "readback"] {
      let editor = makeEditor()
      var calls = [(String, [String: Any], String)](), replies = [([String: Any]) -> Void](), finished = [Bool]()
      editor.onSettingsRequest = { method, params, identity, reply in calls.append((method, params, identity)); replies.append(reply) }
      try begin(editor)
      let stroke = editor.waveform.stroke, submitted = calls[0].1 as NSDictionary, peaks = editor.waveform.peaks
      editor.finishAutomaticEdit { finished.append($0) }
      editor.waveform.setViewport(nil); editor.waveform.cancelStroke(); editor.drawToggle.state = .off; editor.toggleDrawing()
      editor.channelPicker.selectItem(at: 2); editor.channelChanged()
      editor.update(["frames": 10], samples: [["index": 1, "id": "replacement"]], revision: "replacement")
      try require(editor.waveform.stroke == stroke && editor.waveform.visibleRange == 256..<288 && editor.waveform.frames == 1024 && editor.selectedChannels == "both" && editor.waveform.drawing && finished.isEmpty,
        "A submitted stroke retains its view, channels, mode and completion during navigation and refresh")
      if failure == "readback" {
        replies.removeFirst()(response(["changedFrames": 5]))
        try require(calls.last?.0 == "sample.get" && !editor.waveform.stroke.isEmpty && finished.isEmpty, "Accepted drawing waits for staged metadata readback")
        replies.removeFirst()(["error": ["code": -32003, "message": "Read failed"]])
      } else {
        let data: [String: Any] = failure == "notCommitted" ? ["writeOutcome": "notCommitted"] : [:]
        replies.removeFirst()(["error": ["code": -32002, "message": "Busy", "data": data]])
      }
      try require(finished == [false] && editor.waveform.stroke == stroke && editor.waveform.peaks == peaks && !editor.drawingBusy, "Refused or unverified drawing settles Save without dropping the draft")
      let count = calls.count
      editor.retryDrawing()
      if failure == "notCommitted" {
        try require(calls.count == count + 1 && submitted.isEqual(to: calls.last!.1) && calls.last?.2 == "sample-one", "Explicit retry preserves the exact original payload and sample identity")
        replies.removeFirst()(response(["changedFrames": 5]))
        replies.removeFirst()(response(info))
        let pendingWave = calls.last!.1
        try require(editor.waveform.stroke == stroke && editor.waveform.peaks == peaks, "Metadata alone cannot clear a stroke or replace its waveform")
        replies.removeFirst()(response(wave(pendingWave)))
        try require(editor.waveform.stroke.isEmpty && !editor.hasDraft && editor.waveformRevision == "accepted", "Verified owned readback completes the retry")
      } else {
        try require(calls.count == count, "Unknown or accepted drawing outcomes cannot be retried")
        editor.name.stringValue = "Newer name"; editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: editor.name))
        editor.reviewDrawing(); var incomplete = info; incomplete.removeValue(forKey: "sustainLoop")
        replies.removeFirst()(response(incomplete, "reviewed"))
        try require(editor.drawingDraft?.reviewed == nil && editor.waveform.stroke == stroke && replies.isEmpty, "Missing loop metadata cannot be adopted as an empty loop")
        editor.reviewDrawing(); replies.removeFirst()(response(info, "reviewed"))
        var malformed = wave(calls.last!.1); malformed["sample"] = 2
        replies.removeFirst()(response(malformed, "reviewed"))
        try require(editor.drawingDraft?.reviewed == nil && editor.waveform.stroke == stroke && editor.name.stringValue == "Newer name", "Wrong-target waveform readback retains the stroke and newer fields")
        editor.reviewDrawing(); replies.removeFirst()(response(info, "reviewed"))
        replies.removeFirst()(response(wave(calls.last!.1), "reviewed"))
        editor.acceptDrawingReview(); replies.removeFirst()(response(info, "later"))
        try require(editor.drawingDraft?.reviewed == nil && editor.waveform.stroke == stroke, "Accept refuses a review that no longer matches the song")
        editor.reviewDrawing(); replies.removeFirst()(response(info, "later"))
        replies.removeFirst()(response(wave(calls.last!.1), "later"))
        editor.acceptDrawingReview(); replies.removeFirst()(response(info, "later"))
        try require(editor.waveform.stroke.isEmpty && editor.name.stringValue == "Newer name" && editor.settingsNeedsReload && editor.settingsRevision == "original" && calls.filter { $0.0 == "sample.draw" }.count == 1,
          "Accepting inspected state never repeats the write or rebases newer settings")
        editor.settingsWork?.cancel()
      }
    }
    let editor = makeEditor()
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onSettingsRequest = { method, params, _, reply in calls.append((method, params)); replies.append(reply) }
    try begin(editor); let retired = replies.removeFirst()
    editor.resetDocumentContext(); editor.index = 2
    var other = info; other["index"] = 2
    editor.update(other, samples: samples, revision: "other")
    editor.waveform.frame = NSRect(x: 0, y: 0, width: 512, height: 180)
    editor.waveform.setViewport(256..<288); editor.waveform.peaksRange = 256..<288; editor.waveform.peaks = Array(repeating: 0.25, count: 64)
    editor.waveformRevision = "other"; try begin(editor)
    retired(response(["changedFrames": 5]))
    try require(editor.drawingBusy && editor.drawingDraft?.sample == 2 && replies.count == 1, "A retired sample callback cannot release a newer stroke's busy state")
    editor.resetDocumentContext()
    let composing = makeEditor(); var writes = 0
    composing.onRequest = { method, _, _ in if method == "sample.draw" { writes += 1 } }
    try require(composing.waveform.beginStroke(SampleStrokePoint(frame: 260, value: 0.5)), "Begin gesture before newer field input")
    composing.name.stringValue = "Typed during gesture"
    composing.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: composing.name))
    composing.waveform.finishStroke()
    try require(writes == 0 && composing.waveform.stroke.isEmpty && composing.name.stringValue == "Typed during gesture", "Newer field input cancels an unsubmitted gesture without writing or erasing that input")
    composing.settingsWork?.cancel()
  }
}
