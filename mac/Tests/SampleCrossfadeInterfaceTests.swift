import AppKit
extension InterfaceTests {
  static func sampleCrossfadeChecks() throws {
    let f = SampleOperationFixture(), editor = f.editor
    editor.crossfadeMode.selectItem(at: 1); editor.crossfadeCurve.selectItem(at: 1); editor.crossfadeFrames.stringValue = "64"
    let data: [String: Any] = ["sample": 1, "loopBefore": ["frames": 512], "loopAfter": ["frames": 448], "changedFrames": 62, "clippedSamples": 4]
    editor.crossfade(dryRun: true)
    try require(f.calls.count == 1 && f.calls[0].1["mode"] as? String == "overlap" && f.calls[0].1["curve"] as? String == "equal-power" && f.calls[0].1["frames"] as? Int == 64 && f.calls[0].1["expectedRevision"] as? String == "original", "Native crossfade exposes exact modes, curves and length at the captured revision")
    editor.crossfade(dryRun: false); try require(f.calls.count == 1 && !editor.crossfadeApplyButton.isEnabled, "Pending crossfade cannot submit twice")
    try f.answer(data)
    try require(editor.crossfadeStatus.stringValue.contains("512 → 448") && editor.crossfadeStatus.stringValue.contains("4 clipped"), "Preview makes shortening and clipping explicit")
    f.revision = "later"; editor.crossfade(dryRun: false)
    try require(f.calls.count == 1 && editor.crossfadePreviewRevision == "original", "A preview is never rebased onto a changed song")
    f.revision = "original"; editor.crossfadeLoop.selectItem(at: 1); editor.crossfadeMode.selectItem(at: 0); editor.crossfade(dryRun: true)
    try require(f.calls.last?.1["loop"] as? String == "sustain", "The native control can select the saved sustain loop")
    editor.crossfadeFrames.stringValue = "128"; try f.answer(data)
    try require(editor.crossfadePreviewRevision == nil && editor.sampleOperation == nil, "Newer controls retire a preview without leaving an uncertain write")
    editor.crossfadeFrames.stringValue = "1"; let before = f.calls.count; editor.crossfade(dryRun: false)
    try require(f.calls.count == before, "Invalid fade length sends no request")
    try require(editor.crossfadeInfo.stringValue.contains("sustain: 256 → 900"), "Imported sustain geometry is visible")
    editor.operationPicker.selectItem(at: 2); editor.curvePicker.selectItem(at: 2); editor.exponent.stringValue = "2.5"
    editor.process(dryRun: true)
    try require(f.calls.last?.1["exponent"] as? Double == 2.5 && f.calls.last?.1["start"] as? Int == 10, "Native fades preserve exact controls")
    try f.answer(f.processResult()); editor.process(dryRun: false)
    try require(f.calls.last?.1["expectedRevision"] as? String == "original", "Apply freezes its preview revision")
    try f.fail(); editor.resetDocumentContext(); f.load(); f.connect()
    editor.operationPicker.selectItem(at: 6); editor.smoothing.stringValue = "4"; let count = f.calls.count; editor.process(dryRun: true)
    try require(f.calls.count == count && editor.processingStatus.stringValue.contains("odd"), "Invalid smoothing is rejected before dispatch")
  }
}
