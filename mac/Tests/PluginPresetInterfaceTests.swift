import AppKit
extension InterfaceTests {
  static func pluginPresetChecks() throws {
    let workflow = PluginPresetWorkflow()
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), finished = [(Bool, String)]()
    workflow.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    workflow.onFinish = { success, message in finished.append((success, message)) }
    let path = "/private/test.resonance-preset"
    workflow.load(path: path, plugin: "original-plugin", revision: "song:1")
    workflow.load(path: path, plugin: "other-plugin", revision: "song:2")
    workflow.save(path: path, name: "Other", plugin: "other-plugin", revision: "song:2")
    try require(workflow.busy && calls.count == 1 && calls[0].0 == "plugin.preset.inspect", "A pending preset operation cannot be duplicated or retargeted")
    replies.removeFirst()(["result": ["revision": "song:1", "data": ["name": "Tone", "presetRevision": "preset:file1"]]])
    try require(calls.count == 2 && calls[1].0 == "plugin.preset.load" && calls[1].1["plugin"] as? String == "original-plugin" &&
      calls[1].1["expectedRevision"] as? String == "song:1" && calls[1].1["expectedPresetRevision"] as? String == "preset:file1", "Load pins song, plugin and inspected file revisions")
    replies.removeFirst()(["result": ["revision": "song:2", "data": ["loaded": true]]])
    try require(!workflow.busy && finished.last?.0 == true && finished.last?.1.contains("Tone") == true, "Successful load reports its preset and releases the workflow")
    workflow.load(path: path, plugin: "original-plugin", revision: "song:2")
    let count = calls.count
    replies.removeFirst()(["result": ["revision": "other-song:2", "data": ["name": "Tone", "presetRevision": "preset:file1"]]])
    try require(calls.count == count && !workflow.busy && finished.last?.0 == false, "Song changed while choosing/reading a preset prevents any load request")
    workflow.load(path: path, plugin: "original-plugin", revision: "song:2")
    replies.removeFirst()(["error": ["message": "File cannot be read"]])
    try require(!workflow.busy && finished.last?.1 == "File cannot be read", "Inspection failure is surfaced without a mutation")
    workflow.load(path: path, plugin: "original-plugin", revision: "song:2")
    replies.removeFirst()(["result": ["revision": "song:2", "data": ["name": "Tone", "presetRevision": "preset:file1"]]])
    replies.removeFirst()(["error": ["message": "Preset changed; inspect it again"]])
    try require(!workflow.busy && finished.last?.1 == "Preset changed; inspect it again", "File races do not silently retry with changed content")
    workflow.save(path: path, name: "Saved tone", plugin: "original-plugin", revision: "song:3")
    try require(calls.last?.0 == "plugin.preset.save" && calls.last?.1["plugin"] as? String == "original-plugin" && calls.last?.1["name"] as? String == "Saved tone" &&
      calls.last?.1["overwrite"] as? Bool == true && calls.last?.1["expectedRevision"] as? String == "song:3", "Confirmed native save panel routes through the shared file/revision API")
    replies.removeFirst()(["result": ["revision": "song:3", "data": ["written": true]]])
    try require(!workflow.busy && finished.last?.0 == true, "Preset saving needs no song mutation")
    workflow.save(path: path, name: "Saved tone", plugin: "original-plugin", revision: "song:3")
    replies.removeFirst()(["error": ["message": "Disk write failed"]])
    try require(!workflow.busy && finished.last?.0 == false && finished.last?.1 == "Disk write failed", "Failed saving does not report success")
    let editor = PluginEditor(frame: .zero)
    try require(!editor.savePresetButton.isEnabled && !editor.loadPresetButton.isEnabled, "Empty plugin editor disables preset actions")
    editor.update(model: PatternModel(["nativePlugins": [["name": "Gain", "format": "Built-in", "instanceID": "original-plugin"]]]), values: [])
    var saved = -1, loaded = -1
    editor.onSavePreset = { saved = $0 }; editor.onLoadPreset = { loaded = $0 }
    editor.savePresetButton.invoke(); editor.loadPresetButton.invoke()
    try require(editor.savePresetButton.isEnabled && editor.loadPresetButton.isEnabled && saved == 0 && loaded == 0, "Native preset buttons select the displayed plugin")
  }
}
