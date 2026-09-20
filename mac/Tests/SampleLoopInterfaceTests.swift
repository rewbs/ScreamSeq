import AppKit
extension InterfaceTests {
  static func sampleLoopChecks() throws {
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    let info: [String: Any] = ["frames": 2048, "loop": true, "loopStart": 64, "loopEnd": 1024,
      "sustainLoop": false, "sustainStart": 0, "sustainEnd": 0]
    let inventory: [[String: Any]] = [["index": 1, "name": "Loops"]]
    editor.update(info, samples: inventory, revision: "source")
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    func answer(_ revision: String, changed: Bool = true) {
      replies.removeFirst()(["result": ["revision": revision, "data": ["loopsChanged": changed]]])
    }
    editor.sustaining.state = .on; editor.sustainPingpong.state = .on
    editor.sustainStart.stringValue = "128"; editor.sustainEnd.stringValue = "800"
    editor.setLoops(dryRun: true)
    let sustain = calls.first?.1["sustain"] as? [String: Any]
    try require(calls.count == 1 && calls[0].0 == "sample.loops.set" && sustain?["start"] as? Int == 128 &&
      sustain?["end"] as? Int == 800 && sustain?["pingpong"] as? Bool == true && calls[0].1["expectedRevision"] as? String == "source",
      "Native sustain edits dispatch full normal/sustain settings through the revision-checked API")
    editor.setLoops(dryRun: false); try require(calls.count == 1 && !editor.loopsApplyButton.isEnabled, "Pending loop requests cannot duplicate")
    answer("source")
    editor.update(info, samples: inventory, revision: "newer")
    try require(editor.sustainStart.integerValue == 128 && editor.loopDraftRevision == "source", "Concurrent refresh retains draft fields and original revision")
    editor.setLoops(dryRun: false)
    try require(calls.last?.1["expectedRevision"] as? String == "source", "Reviewed loop intent cannot silently rebase")
    replies.removeFirst()(["error": ["message": "Song changed"]])
    try require(editor.loopsStatus.stringValue.contains("Reload loops") && editor.loopPreviewRevision == nil, "Rejected loop edit explains explicit refresh")
    editor.reloadLoops(); try require(editor.sustainStart.integerValue == 0 && editor.loopDraftRevision == "newer", "Reload retires stale draft explicitly")
    editor.loopStart.stringValue = "72"; editor.setLoops(dryRun: true); editor.loopStart.stringValue = "73"; answer("newer")
    try require(editor.loopPreviewRevision == nil && editor.loopsStatus.stringValue.contains("discarded"), "Changed fields retire delayed loop preview")
    editor.loopStart.stringValue = "-1"; let count = calls.count; editor.setLoops(dryRun: false)
    try require(calls.count == count, "Invalid loop fields send no request")
    editor.loopStart.stringValue = "72"; editor.setLoops(dryRun: false); editor.loopStart.stringValue = "80"; answer("applied")
    try require(editor.loopStart.integerValue == 80 && editor.loopDraftRevision == "applied", "A second local gesture survives the first commit")
    editor.setLoops(dryRun: false); try require(calls.last?.1["expectedRevision"] as? String == "applied", "Next gesture uses successful commit revision")
    editor.index = 2; answer("other")
    try require(editor.loopDraftRevision == nil && !editor.loopsBusy, "Changing sample retires old loop replies")
    editor.update(info, samples: inventory, revision: "second")
    editor.loopReverse.state = .on; editor.loopModeChanged(editor.loopReverse)
    try require(editor.pingpong.state == .off, "Selecting reverse excludes ping-pong")
    editor.setLoops(dryRun: true)
    try require((calls.last?.1["normal"] as? [String: Any])?["reverse"] as? Bool == true, "Native reverse mode is exposed through the shared loop API")
    answer("second")
    editor.pingpong.state = .on; editor.loopModeChanged(editor.pingpong)
    try require(editor.loopReverse.state == .off, "Selecting ping-pong excludes reverse")
    var settings: [String: Any] = [:]; editor.onSettings = { settings = $0 }; editor.apply()
    try require(settings["loop"] == nil && settings["loopStart"] == nil && settings["name"] != nil, "General sample properties do not submit unreviewed loop drafts")
  }
}
