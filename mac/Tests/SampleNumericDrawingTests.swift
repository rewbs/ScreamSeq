import AppKit

extension InterfaceTests {
  static func numericDrawingChecks() throws {
    let info: [String: Any] = ["index": 1, "name": "Numeric", "frames": 8192, "channels": 2, "rate": 44100, "volume": 64, "pan": 128,
      "loop": false, "loopStart": 0, "loopEnd": 0, "pingpong": false, "reverseLoop": false,
      "sustainLoop": false, "sustainStart": 0, "sustainEnd": 0, "sustainPingpong": false, "sustainReverse": false]
    let samples: [[String: Any]] = [["index": 1, "id": "numeric", "name": "Numeric"]]
    func makeEditor(frames: Int = 8192) -> SampleEditor {
      let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
      var metadata = info; metadata["frames"] = frames
      editor.update(metadata, samples: samples, revision: "original")
      editor.waveform.peaksRange = 0..<frames; editor.waveform.peaks = Array(repeating: 0.25, count: 4096)
      editor.waveformRevision = "original"; editor.waveformDocumentID = "song"
      return editor
    }
    func stage(_ editor: SampleEditor, _ frame: String, _ value: String) {
      editor.drawingFrame.stringValue = frame; editor.drawingValue.stringValue = value
      editor.numericDrawingInputChanged(); editor.stageDrawingPoint()
    }
    func response(_ data: [String: Any], revision: String = "accepted") -> [String: Any] {
      ["result": ["revision": revision, "documentId": "song", "data": data]]
    }
    for interpolation in ["linear", "step"] {
      let editor = makeEditor()
      var calls = [(String, [String: Any], String)](), replies = [([String: Any]) -> Void]()
      editor.onSettingsRequest = { method, params, identity, done in calls.append((method, params, identity)); replies.append(done) }
      stage(editor, "15", "0.5"); stage(editor, "3", "-0.25"); stage(editor, "15", "0.75")
      editor.drawingInterpolation.selectItem(at: interpolation == "step" ? 1 : 0); editor.numericDrawingInputChanged()
      try require(calls.isEmpty && editor.waveform.stroke == [3: -0.25, 15: 0.75] && editor.drawingDraft?.points.map(\.frame) == [3, 15] && editor.hasDraft && editor.waveform.strokeInterpolation == interpolation,
        "Numeric staging sorts sparse points, replaces an existing frame and changes no music")
      try require(!editor.waveform.precise, "Exact numeric staging does not require individual-frame zoom")
      try require(!editor.name.isEnabled && !editor.loopStart.isEnabled && !editor.looping.isEnabled, "A staged batch prevents a second musical form from accepting conflicting input")
      let points = editor.waveform.stroke
      editor.waveform.cancelStroke(); editor.waveform.setViewport(0..<32); editor.waveform.selection = 0...32
      editor.refreshSample(samples: samples)
      try require(editor.waveform.stroke == points && editor.waveform.visibleRange == 0..<8192 && calls.isEmpty, "Escape, view changes and automatic refresh retain staged numeric points")
      var completed: Bool?
      editor.finishAutomaticEdit { completed = $0 }
      try require(completed == false && calls.isEmpty, "Document Save cannot silently omit staged drawing")
      editor.drawingValue.stringValue = "0.9"; editor.numericDrawingInputChanged(); editor.saveStagedDrawing()
      try require(calls.isEmpty && editor.drawingValue.stringValue == "0.9", "Save batch refuses newer point text that has not been staged")
      editor.stageDrawingPoint(); editor.saveStagedDrawing()
      try require(calls.count == 1 && calls[0].0 == "sample.draw" && calls[0].2 == "numeric" && calls[0].1["interpolation"] as? String == interpolation && calls[0].1["expectedRevision"] as? String == "original",
        "Save staged points submits one guarded batch with the explicit interpolation")
      let original = calls[0].1 as NSDictionary
      replies.removeFirst()(["error": ["code": -32002, "message": "Busy", "data": ["writeOutcome": "notCommitted"]]])
      editor.retryDrawing()
      try require(calls.count == 2 && original.isEqual(to: calls.last!.1), "Numeric retry preserves the exact original points, mode and revision")
      replies.removeFirst()(response(["changedFrames": 13]))
      replies.removeFirst()(response(info))
      var wave = calls.last!.1; wave["peaks"] = Array(repeating: 0.5, count: 4096)
      replies.removeFirst()(response(wave))
      try require(!editor.hasDraft && editor.waveform.stroke.isEmpty && editor.waveformRevision == "accepted" && editor.name.isEnabled && editor.looping.isEnabled,
        "Complete numeric readback retires the batch and releases settings/loop input")
    }
    let invalid = makeEditor(); var writes = 0
    invalid.onSettingsRequest = { _, _, _, _ in writes += 1 }
    for (frame, value) in [("-1", "0"), ("8192", "0"), ("1.5", "0"), ("999999999999999999999999", "0"), ("4", "nan"), ("4", "inf"), ("4", "1.01"), ("4", "-."), ("4", "-1.01")] {
      stage(invalid, frame, value)
      try require(writes == 0 && invalid.waveform.stroke.isEmpty && invalid.drawingFrame.stringValue == frame && invalid.drawingValue.stringValue == value && invalid.hasDraft,
        "Invalid or non-finite numeric input remains visible without staging or writing")
    }
    invalid.discardDrawing()
    try require(!invalid.hasDraft && invalid.drawingFrame.stringValue == "0" && invalid.drawingValue.stringValue == "0", "Explicit discard releases raw numeric input")
    let bounded = makeEditor()
    stage(bounded, "0", "0")
    let full = (0..<4096).map { SampleStrokePoint(frame: $0, value: 0) }
    bounded.drawingDraft?.points = full; bounded.waveform.stagePoints(full, interpolation: "linear")
    stage(bounded, "4096", "0.5")
    try require(bounded.waveform.stroke.count == 4096 && bounded.waveform.stroke[4096] == nil, "A 4097th numeric point is rejected without dropping the batch")
    stage(bounded, "4095", "0.5")
    try require(bounded.waveform.stroke.count == 4096 && bounded.waveform.stroke[4095] == 0.5, "A full batch still permits replacement of an existing frame")
    for first in [0, 1_048_576] {
      let span = makeEditor(frames: 2_097_152)
      stage(span, String(first), "0.25")
      stage(span, String(1_048_576 - first), "-0.5")
      try require(span.waveform.stroke == [first: 0.25] && span.drawingDraft?.submitted == false && span.drawingFrame.stringValue == String(1_048_576 - first) && span.hasUnstagedDrawingPoint,
        "A point exceeding the shared API's inclusive span is refused while the batch remains editable")
      stage(span, String(first == 0 ? 1_048_575 : 1), "-0.5")
      try require(span.waveform.stroke.count == 2 && span.waveform.stroke.keys.max()! - span.waveform.stroke.keys.min()! + 1 == 1_048_576 && !span.hasUnstagedDrawingPoint,
        "Correcting either endpoint to the exact maximum span preserves and completes the staged batch")
    }
    let stale = makeEditor()
    stale.drawingFrame.stringValue = "4"; stale.numericDrawingInputChanged()
    stale.onSampleRevision = { "later" }; stale.stageDrawingPoint()
    try require(stale.waveform.stroke.isEmpty && stale.hasNumericDrawingInput && stale.numericDrawingRevision == "original", "New song revisions do not rebase raw numeric intent")
    let forms = makeEditor(), fieldEditor = NSTextView()
    forms.name.stringValue = "Pending name"
    try require(!forms.control(forms.drawingFrame, textShouldBeginEditing: fieldEditor), "A settings draft refuses a second form's numeric text before accepting input")
    forms.drawingInterpolation.selectItem(at: 1); forms.numericDrawingInterpolationChanged()
    try require(forms.drawingInterpolation.indexOfSelectedItem == 0 && !forms.hasNumericDrawingInput && forms.name.stringValue == "Pending name", "A refused interpolation change preserves the owning settings draft")
    forms.name.stringValue = "Numeric"; forms.drawingValue.stringValue = "-."; forms.numericDrawingInputChanged()
    try require(!forms.control(forms.name, textShouldBeginEditing: fieldEditor) && !forms.control(forms.loopStart, textShouldBeginEditing: fieldEditor) && !forms.looping.isEnabled,
      "Raw numeric fields reserve input ownership even before their first staged point")
    forms.discardDrawing()
    try require(forms.name.isEnabled && forms.looping.isEnabled && forms.control(forms.name, textShouldBeginEditing: fieldEditor), "Discarding raw drawing fields releases other forms")
    try numericDrawingNewerInputChecks(info: info, samples: samples)
    try numericDrawingLoopOwnershipChecks(info: info, samples: samples)
  }
  static func numericDrawingLoopOwnershipChecks(info: [String: Any], samples: [[String: Any]]) throws {
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    var metadata = info; metadata["loop"] = true; metadata["loopStart"] = 10; metadata["loopEnd"] = 1000
    editor.update(metadata, samples: samples, revision: "original"); editor.waveformRevision = "original"
    var requests = [String]()
    editor.onRequest = { method, _, _ in requests.append(method) }; editor.onSelect = { _ in }
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 729, height: 1400), styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = editor; editor.layoutSubtreeIfNeeded()
    defer { window.orderOut(nil); window.contentView = nil }
    editor.drawingValue.stringValue = "0.25"; editor.numericDrawingInputChanged()
    let loops = editor.rawLoopDraft
    try require(editor.drawingDraft == nil && editor.protectsDrawingInput, "Unstaged numeric text participates in the application's audio-write guard")
    let start = editor.waveform.convert(editor.waveform.markerLocation(.start), to: nil)
    func mouse(_ type: NSEvent.EventType, _ location: NSPoint) -> NSEvent {
      NSEvent.mouseEvent(with: type, location: location, modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
    }
    editor.waveform.mouseDown(with: mouse(.leftMouseDown, start))
    try require(editor.waveform.draggingLoop && editor.waveform.selectedLoopMarker == .start, "Loop ownership fixture reaches the real native loop-handle gesture")
    editor.waveform.mouseDragged(with: mouse(.leftMouseDragged, NSPoint(x: start.x + 20, y: start.y)))
    editor.waveform.mouseUp(with: mouse(.leftMouseUp, NSPoint(x: start.x + 20, y: start.y)))
    let right = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber,
      context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: 124)!
    editor.waveform.selectedLoopMarker = .start; editor.waveform.keyDown(with: right)
    editor.waveform.moveLoopMarker(.end, to: 1100, finished: true)
    editor.waveform.onLoopMarker?(.end, 1100, true)
    editor.setLoops(dryRun: false)
    try require(requests.isEmpty && editor.rawLoopDraft.isEqual(loops) && editor.waveform.loopStart == 10 && editor.waveform.loopEnd == 1000 && editor.numericDrawingRevision == "original" && editor.drawingValue.stringValue == "0.25",
      "Mouse drags, keyboard nudges and direct loop actions cannot mutate loop fields or advance an unstaged drawing's revision")
    editor.discardDrawing(); editor.waveform.keyDown(with: right)
    try require(requests == ["sample.loops.set"] && editor.waveform.loopStart == 11, "Discard releases loop-handle editing without rebasing or losing a pending numeric point")
  }
  static func numericDrawingNewerInputChecks(info: [String: Any], samples: [[String: Any]]) throws {
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    editor.update(info, samples: samples, revision: "original")
    editor.waveform.peaksRange = 0..<8192; editor.waveform.peaks = Array(repeating: 0.25, count: 4096); editor.waveformRevision = "original"
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onSettingsRequest = { method, params, _, done in calls.append((method, params)); replies.append(done) }
    editor.drawingFrame.stringValue = "4"; editor.drawingValue.stringValue = "0.5"; editor.stageDrawingPoint()
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 800, height: 900), styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = editor
    defer { window.orderOut(nil); window.contentView = nil }
    let saveKey = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: .command, timestamp: 0,
      windowNumber: window.windowNumber, context: nil, characters: "\r", charactersIgnoringModifiers: "\r", isARepeat: false, keyCode: 36)!
    window.makeFirstResponder(editor.selectionStart)
    try require(!editor.handleNumericDrawingKey(saveKey) && calls.isEmpty, "The drawing shortcut does not claim another form's focus")
    window.makeFirstResponder(editor.waveform)
    try require(editor.performKeyEquivalent(with: saveKey) && calls.count == 1 && calls[0].0 == "sample.draw", "Command-Return from the waveform saves the staged batch once")
    window.makeFirstResponder(editor.drawingValue)
    guard let field = editor.drawingValue.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Numeric drawing fixture needs a native field editor") }
    field.setMarkedText("-.", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    replies.removeFirst()(["result": ["revision": "accepted", "data": ["changedFrames": 1]]])
    try require(editor.drawingDraft?.submitted == true && !editor.drawingBusy && calls.count == 1 && field.hasMarkedText() && field.selectedRange().location == 2,
      "Newer native composition retains accepted numeric writes for review without erasing the input or caret")
    field.unmarkText(); field.string = "0.75"; editor.drawingValue.stringValue = "0.75"
    editor.numericDrawingInputChanged()
    editor.reviewDrawing(); replies.removeFirst()(["result": ["revision": "accepted", "data": info]])
    var wave = calls.last!.1; wave["peaks"] = Array(repeating: 0.5, count: 4096)
    replies.removeFirst()(["result": ["revision": "accepted", "data": wave]])
    editor.acceptDrawingReview(); replies.removeFirst()(["result": ["revision": "accepted", "data": info]])
    try require(editor.waveform.stroke.isEmpty && editor.hasNumericDrawingInput && editor.numericDrawingRevision == "original" && field.string == "0.75" && editor.drawingValue.currentEditor() === field && calls.filter { $0.0 == "sample.draw" }.count == 1,
      "Review acceptance keeps newer numeric input at its original revision and never repeats a write")
    editor.stageDrawingPoint()
    try require(editor.waveform.stroke.isEmpty && editor.hasNumericDrawingInput, "Newer numeric fields require explicit reload/discard before adopting another revision")
  }
}
