import AppKit
extension InterfaceTests {
  static func automationTargetChecks() throws {
    let old: [String: Any] = ["pattern": 0, "patterns": [["index": 0, "id": "n42"]],
      "nativePlugins": [["name": "Original gain", "instanceID": "gain"]]]
    let fresh: [String: Any] = ["patterns": [["index": 4, "id": "n42"]],
      "nativePlugins": [["name": "Other", "instanceID": "other"], ["name": "Original gain", "instanceID": "gain"]]]
    let learned: [String: Any] = ["token": "song:7", "target": ["available": true, "slot": 1,
      "plugin": "gain", "pluginName": "Original gain", "parameter": 7, "name": "Gain"]]
    let envelope: [String: Any] = ["patternID": "n42", "rows": 32, "lanes": [
      ["id": "lane", "plugin": "gain", "parameter": 7, "enabled": false,
       "points": [["position": 32, "value": 0.4, "curve": "smooth"]]]]]
    let parameters: [[String: Any]] = [["id": 1, "name": "Mix"], ["id": 7, "name": "Gain"]]
    let editor = PatternAutomationEditor(frame: .zero)
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    func reset() {
      editor.model = PatternModel(old); editor.revision = "song:0"; editor.pluginIndex = 0
      editor.parameterID = 1; editor.laneID = nil; editor.hasDraft = false
      editor.canvas.points = [EnvelopePoint(position: 0, value: 0.2, curve: "step")]
      calls.removeAll(); replies.removeAll()
    }
    func answer(_ data: Any, revision: String = "song:1") {
      replies.removeFirst()(["result": ["revision": revision, "data": data]])
    }
    reset(); editor.useLastTouched(); editor.useLastTouched()
    try require(calls.count == 1 && editor.loading, "Learning cannot duplicate pending requests")
    answer(fresh); answer(learned)
    try require(calls.last?.0 == "automation.pattern.get" && calls.last?.1["pattern"] as? Int == 4, "Learning follows the displayed pattern identity through structural changes")
    answer(envelope)
    try require(calls.last?.1["slot"] as? Int == 1, "Learning resolves the current rack position by saved plugin identity")
    answer(parameters)
    try require(!editor.loading && !editor.hasDraft && editor.parameterID == 7 && editor.pluginIndex == 1 && editor.model.pattern == 4,
      "Learning selects the requested parameter without making an edit")
    try require(editor.laneID == "lane" && editor.enabled.state == .off && editor.canvas.points == [EnvelopePoint(position: 32, value: 0.4, curve: "smooth")],
      "Existing lane points and enabled state load without replacement")
    try require(editor.revision == "song:1" && calls.allSatisfy { $0.0.hasSuffix(".get") }, "Only revision-consistent reads learn the target")
    editor.ramp(false); editor.apply()
    try require(calls.last?.0 == "automation.pattern.set" && calls.last?.1["pattern"] as? Int == 4 &&
      calls.last?.1["plugin"] as? String == "gain" && calls.last?.1["parameter"] as? Int == 7 &&
      calls.last?.1["expectedRevision"] as? String == "song:1", "Subsequent Apply uses the learned stable target and checked revision")
    replies.removeFirst()(["error": ["message": "test rejection"]])

    reset(); editor.markDraft(); editor.useLastTouched()
    try require(calls.isEmpty && editor.hasDraft, "Unsaved draft prevents retargeting")
    reset(); editor.useLastTouched(); editor.markDraft(); answer(fresh)
    try require(!editor.loading && editor.hasDraft && editor.parameterID == 1 && editor.canvas.points[0].value == 0.2 && editor.revision == "song:0",
      "Gesture during a pending learn keeps the original target, revision and draft")
    for step in 0..<4 {
      reset(); editor.useLastTouched()
      let responses: [Any] = [fresh, learned, envelope, parameters]
      for index in 0...step { answer(responses[index], revision: index == step ? (step == 0 ? "other-song:0" : "song:2") : "song:1") }
      try require(!editor.loading && editor.parameterID == 1 && editor.pluginIndex == 0 && editor.revision == "song:0" && editor.canvas.points[0].value == 0.2,
        "Changed document/revision at every read stage preserves original editor")
    }
    reset(); editor.useLastTouched(); answer(fresh); answer(["target": NSNull()])
    try require(!editor.loading && editor.status.stringValue.contains("Move a plugin"), "No previous gesture gives actionable status")
    reset(); editor.useLastTouched(); answer(fresh); answer(["target": ["available": false, "reason": "Plugin was removed"]])
    try require(editor.status.stringValue == "Plugin was removed" && editor.parameterID == 1, "Removed target never substitutes another plugin")
    reset(); editor.useLastTouched(); answer(fresh); answer(learned); answer(envelope); answer([["id": 1, "name": "Mix"]])
    try require(!editor.loading && editor.parameterID == 1 && editor.revision == "song:0", "Disappeared parameter preserves the previous target")
    reset(); editor.useLastTouched(); answer(fresh); answer(learned); answer(["patternID": "n42", "rows": 32, "lanes": []]); answer(parameters)
    try require(editor.laneID == nil && editor.canvas.points.isEmpty && !editor.hasDraft, "An unautomated target creates no implicit points or lane")
  }
}
