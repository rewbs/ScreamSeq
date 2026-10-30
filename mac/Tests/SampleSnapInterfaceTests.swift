import AppKit

extension SampleOperationFixture {
  func snapReport(_ first: Int, _ last: Int, matched: Bool = true) -> [String: Any] {
    let params = pending.first!.1, before = params["positions"] as! [Int]
    return ["sample": params["sample"]!, "mode": params["mode"]!, "direction": params["direction"]!,
      "positions": zip(before, [first, last]).map { ["before": $0, "after": $1, "matched": matched] as [String: Any] }]
  }
}

extension InterfaceTests {
  static func sampleSnapChecks() throws {
    for mode in 0...1 { for direction in 0...2 { for target: SampleSnapTarget in [.selection, .normal, .sustain] {
      let f = SampleOperationFixture(), e = f.editor
      e.waveform.selection = 13...97; e.snapMode.selectItem(at: mode); e.updateSnapOptions(); e.snapDirection.selectItem(at: direction)
      e.snapStep.stringValue = "64"; e.snapOrigin.stringValue = "8"
      let fields = e.snapFields(target); fields[0].stringValue = "13"; fields[1].stringValue = "97"
      let loops = e.rawLoopDraft, selection = e.waveform.selection, other = e.snapFields(target == .sustain ? .normal : .sustain).map(\.stringValue)
      e.snapBoundaries(target: target)
      let p = f.calls[0].1
      try require(f.calls[0].0 == "sample.snap.get" && f.calls[0].2 == "sample-one" && p["positions"] as? [Int] == [13, 97] &&
        p["direction"] as? String == ["nearest", "before", "after"][direction] && p["expectedRevision"] == nil,
        "Every target and direction uses the shared read-only snap API with stable sample identity")
      try require(mode == 0 ? p["radius"] as? Int == 2048 && p["channels"] as? String == "both" && p["step"] == nil
        : p["step"] as? Int == 64 && p["origin"] as? Int == 8 && p["channels"] == nil && p["radius"] == nil,
        "Snap modes send only their own channel/search or grid controls")
      e.snapBoundaries(target: target); try require(f.calls.count == 1, "Duplicate pending snaps cannot start a second read")
      var finished = [Bool](); e.finishAutomaticEdit { finished.append($0) }
      let start = direction == 2 ? 72 : 8, end = direction == 2 ? 136 : 72
      try f.answer(f.snapReport(start, end))
      try require(!e.snapBusy && !e.snapPending && fields.map(\.stringValue) == [String(start), String(end)], "Matching replies adopt complete snapped boundaries")
      if target == .selection {
        try require(e.waveform.selection == start...end && e.rawLoopDraft.isEqual(loops) && f.calls.count == 1 && finished == [true], "Selection snapping changes no music and releases waiting Save")
      } else {
        try require(e.waveform.selection == selection && e.snapFields(target == .sustain ? .normal : .sustain).map(\.stringValue) == other &&
          f.calls.last?.0 == "sample.loops.set" && f.calls.last?.1["expectedRevision"] as? String == "original" && finished.isEmpty,
          "Normal/sustain snapping preserves the other loop and selection, then waits for the guarded loop write")
        try f.answerLoops("snapped"); try f.finishRead()
        try require(!e.hasDraft && finished == [true] && e.sampleRevision == "snapped", "Loop snapping releases Save only after both matching readbacks")
      }
    } } }
    for target: SampleSnapTarget in [.normal, .sustain] { for enabled in [false, true] {
      let f = SampleOperationFixture(), e = f.editor, fields = e.snapFields(target)
      fields[0].stringValue = "10"; fields[1].stringValue = "20"
      (target == .normal ? e.looping : e.sustaining).state = enabled ? .on : .off
      e.snapBoundaries(target: target); try f.answer(f.snapReport(16, 16))
      try require(enabled ? fields.map(\.stringValue) == ["10", "20"] && f.calls.count == 1 && e.snapStatus.stringValue.contains("collapse")
        : fields.map(\.stringValue) == ["16", "16"] && f.calls.last?.0 == "sample.loops.set",
        "Collapsed disabled loops remain valid; enabled normal/sustain loops reject collapse")
    } }
    let unmatched = SampleOperationFixture(); unmatched.editor.snapBoundaries(target: .selection)
    try unmatched.answer(unmatched.snapReport(10, 20, matched: false))
    try require(unmatched.editor.snapStatus.stringValue.contains("Matched 0/2") && unmatched.calls.count == 1, "Unmatched positions remain unchanged without a write")
    try sampleSnapMalformedChecks()
    try sampleSnapOwnershipChecks()
    try sampleSnapAutomaticChecks()
    try sampleSnapNativeFocusChecks()
  }
  static func sampleSnapMalformedChecks() throws {
    for bad in ["revision", "document", "sample", "booleanSample", "mode", "direction", "count", "before", "fraction", "boolFrame", "matched", "unmatchedMove", "negative", "overflow", "reversed", "directionMove", "error"] {
      let f = SampleOperationFixture(), e = f.editor
      e.snapDirection.selectItem(at: 1); e.snapBoundaries(target: .selection)
      var done: Bool?; e.finishAutomaticEdit { done = $0 }
      var report = f.snapReport(8, 16), positions = report["positions"] as! [[String: Any]]
      switch bad {
      case "sample": report["sample"] = 2
      case "booleanSample": report["sample"] = true
      case "mode": report["mode"] = "grid"
      case "direction": report["direction"] = "after"
      case "count": positions.removeLast()
      case "before": positions[0]["before"] = 11
      case "fraction": positions[0]["after"] = 8.5
      case "boolFrame": positions[0]["after"] = false
      case "matched": positions[0]["matched"] = 1
      case "unmatchedMove": positions[0]["matched"] = false
      case "negative": positions[0]["after"] = -1
      case "overflow": positions[1]["after"] = 1025
      case "reversed": positions[0]["after"] = 9; positions[1]["after"] = 8
      case "directionMove": positions[0]["after"] = 11
      default: break
      }
      report["positions"] = positions
      if bad == "error" { try f.fail() }
      else { try f.answer(report, revision: bad == "revision" ? "later" : "original", document: bad == "document" ? "other" : "song") }
      try require(!e.snapBusy && e.waveform.selection == 10...20 && e.selectionStart.stringValue == "10" && e.selectionEnd.stringValue == "20" &&
        e.sampleRevision == "original" && e.waveformRevision == "original" && done == false && f.calls.count == 1,
        "Malformed/stale snapping retains all presentation and settles Save: \(bad)")
    }
    for raw in ["0", "-1", "1.5", "268435457", "999999999999999999999999"] {
      let f = SampleOperationFixture(), e = f.editor; e.snapMode.selectItem(at: 1); e.snapStep.stringValue = raw
      e.snapBoundaries(target: .selection); try require(f.calls.isEmpty && !e.snapBusy, "Invalid grid steps issue no query")
    }
    for raw in ["-1", "65537", "nan"] {
      let f = SampleOperationFixture(); f.editor.snapRadius.stringValue = raw; f.editor.snapBoundaries(target: .selection)
      try require(f.calls.isEmpty, "Invalid search radii issue no query")
    }
    for raw in ["-1", "1025", "1.5"] {
      let f = SampleOperationFixture(); f.editor.snapMode.selectItem(at: 1); f.editor.snapOrigin.stringValue = raw
      f.editor.snapBoundaries(target: .selection); try require(f.calls.isEmpty, "Invalid grid origins issue no query")
    }
    let empty = SampleOperationFixture(), emptyEditor = empty.editor
    emptyEditor.waveform.frames = 0; emptyEditor.waveform.selection = 0...0
    emptyEditor.snapBoundaries(target: .selection); try empty.answer(empty.snapReport(0, 0, matched: false))
    try require(!emptyEditor.snapBusy && emptyEditor.waveform.selection == 0...0 && empty.calls.count == 1, "An empty sample supports unchanged insertion-boundary snapping")
    let stale = SampleOperationFixture(); stale.editor.loopDraftRevision = "old"; stale.editor.snapBoundaries(target: .sustain)
    try require(stale.calls.isEmpty && stale.editor.sustainStart.stringValue == "256", "Loop snapping cannot rebase old loop intent")
    let invalid = SampleOperationFixture(); invalid.editor.sustainStart.stringValue = "-"; invalid.editor.snapBoundaries(target: .normal)
    try require(invalid.calls.isEmpty, "Both loop rows must be valid before snapping a loop")
    invalid.editor.snapBoundaries(target: .selection); try invalid.answer(invalid.snapReport(8, 24))
    try require(invalid.calls.count == 1 && invalid.editor.sustainStart.stringValue == "-" && invalid.editor.waveform.selection == 8...24,
      "Read-only selection snapping preserves an unrelated invalid loop draft")
  }
  static func sampleSnapOwnershipChecks() throws {
    for change in ["selection", "loop", "settings", "raw", "direction", "mode", "radius", "channels", "viewport", "identity", "document", "revision"] {
      let f = SampleOperationFixture(), e = f.editor; e.snapBoundaries(target: .selection)
      var done: Bool?; e.finishAutomaticEdit { done = $0 }; let report = f.snapReport(8, 24)
      switch change {
      case "selection": e.waveform.selection = 20...80
      case "loop": e.loopStart.stringValue = "5"
      case "settings": e.name.stringValue = "Newer name"
      case "raw": e.gain.stringValue = "-"
      case "direction": e.snapDirection.selectItem(at: 1)
      case "mode": e.snapMode.selectItem(at: 1)
      case "radius": e.snapRadius.stringValue = "64"
      case "channels": e.channelPicker.selectItem(at: 1)
      case "viewport": e.waveform.setViewport(0..<128, notify: false)
      case "identity": e.updateCatalogue([["index": 1, "id": "replacement", "name": "Changed"]])
      case "document": e.waveformDocumentID = "other"
      default: f.revision = "external"
      }
      let fields = e.drawingFields, region = SampleViewRegion(e)
      try f.answer(report, revision: "original")
      try require(!e.snapBusy && done == false && e.drawingFields == fields && SampleViewRegion(e) == region && f.calls.count == 1,
        "Newer target, revision, raw fields and geometry survive delayed snaps: \(change)")
    }
    let f = SampleOperationFixture(), e = f.editor
    e.snapBoundaries(target: .selection); let old = f.pending.removeFirst(), oldQuery = e.snapQuery!
    var oldDone: Bool?, newDone: Bool?; e.finishAutomaticEdit { oldDone = $0 }
    e.resetDocumentContext(); f.load(); f.connect(); e.waveform.selection = 30...50; e.snapBoundaries(target: .selection)
    let current = e.snapQuery; e.finishAutomaticEdit { newDone = $0 }
    old.2(["result": ["revision": "original", "documentId": "song", "data": ["sample": 1, "mode": "zero", "direction": "nearest", "positions": [["before": 10, "after": 8, "matched": true], ["before": 20, "after": 24, "matched": true]]]]])
    try require(oldDone == false && newDone == nil && e.snapQuery === current && e.snapQuery !== oldQuery && e.snapBusy,
      "A retired snap cannot release a newer query or its waiting Save")
    try f.answer(f.snapReport(32, 48)); try require(newDone == true && !e.snapBusy, "The current snap alone completes its Save")
    for blocker in ["drawing", "rawDrawing", "operation", "loop", "waveform", "selection", "transport"] {
      let f = SampleOperationFixture(), e = f.editor
      switch blocker {
      case "drawing": e.waveform.retainsStroke = true; e.waveform.stagePoints([SampleStrokePoint(frame: 3, value: 0.5)], interpolation: "linear")
      case "rawDrawing": e.drawingValue.stringValue = "-."; e.numericDrawingInputChanged()
      case "operation": e.process(dryRun: false)
      case "loop": e.loopStart.stringValue = "10"; e.setLoops(dryRun: false)
      case "waveform": e.requestWaveform(range: 0..<128)
      case "selection": e.requestSampleSelection(2)
      default: e.onRequest = nil; e.onSettingsRequest = nil
      }
      let count = f.calls.count, stroke = e.waveform.stroke, fields = e.drawingFields
      e.snapBoundaries(target: .normal)
      try require(f.calls.count == count && e.waveform.stroke == stroke && e.drawingFields == fields && !e.snapBusy,
        "Snap cannot cancel retained input or overlap another owner: \(blocker)")
    }
  }
  static func sampleSnapAutomaticChecks() throws {
    for outcome in ["complete", "disabled", "invalid"] {
      let f = SampleOperationFixture(), e = f.editor; e.snapAutomatically.state = .on
      e.snapAfterSelection(); let firstReport = f.snapReport(8, 24)
      e.waveform.selection = 100...180; e.snapAfterSelection(); e.waveform.selection = 200...280; e.snapAfterSelection()
      var done: Bool?; e.finishAutomaticEdit { done = $0 }
      if outcome == "disabled" { e.snapAutomatically.state = .off }
      if outcome == "invalid" { e.selectionStart.stringValue = "-" }
      try require(f.calls.count == 1 && done == nil && e.hasDraft, "Rapid auto selections coalesce while document actions wait")
      try f.answer(firstReport)
      if outcome == "complete" {
        try require(f.calls.count == 2 && f.calls.last?.1["positions"] as? [Int] == [200, 280] && done == nil, "Only the latest pending selection is queried")
        try f.answer(f.snapReport(192, 288))
        try require(done == true && e.waveform.selection == 192...288 && !e.snapBusy && !e.snapPending && f.calls.count == 2, "Latest auto snap settles without recursion")
      } else {
        try require(done == false && !e.snapBusy && !e.snapPending && f.calls.count == 1 && e.waveform.selection == 200...280,
          "Disabled or invalid queued snap releases waiters without replacing newer intent")
      }
    }
    let f = SampleOperationFixture(), e = f.editor
    e.loopStart.stringValue = "10"; e.saveLoopsSoon(); e.snapBoundaries(target: .selection)
    var done: Bool?; e.finishAutomaticEdit { done = $0 }
    try require(e.loopSaveWork == nil && f.calls.count == 1, "Queued automatic loop writes are suspended during geometry reads")
    try f.answer(f.snapReport(8, 24))
    try require(f.calls.last?.0 == "sample.loops.set" && done == nil && f.loopCalls.count == 1, "Snap completion resumes the original guarded loop edit once")
    try f.answerLoops("saved"); try f.finishRead()
    try require(done == true && !e.hasDraft && e.loopStart.stringValue == "10", "Waiting Save includes the resumed automatic edit")
    let newer = SampleOperationFixture(); newer.editor.snapBoundaries(target: .selection)
    newer.editor.loopStart.stringValue = "12"; newer.editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: newer.editor.loopStart))
    let report = newer.snapReport(8, 24); try newer.answer(report)
    try require(newer.editor.waveform.selection == 10...20 && newer.calls.last?.0 == "sample.loops.set" && newer.calls.last?.1["expectedRevision"] as? String == "original",
      "Newer automatic loop input retires snapping and resumes at its original musical revision")
  }
  static func sampleSnapNativeFocusChecks() throws {
    for target: SampleSnapTarget in [.selection, .normal, .sustain] { for scenario in ["target", "otherFocus", "composition"] {
      let f = SampleOperationFixture(), e = f.editor, fields = e.snapFields(target)
      fields[0].stringValue = "10"; fields[1].stringValue = "20"
      func expand(_ view: NSView) { if let section = view as? ToolSection { section.setExpanded(true, persist: false) }; view.subviews.forEach(expand) }
      expand(e)
      let window = NSWindow(contentRect: e.frame, styleMask: [.titled], backing: .buffered, defer: false)
      window.isReleasedWhenClosed = false; window.contentView = e; e.layoutSubtreeIfNeeded()
      defer { window.orderOut(nil); window.contentView = nil }
      window.makeFirstResponder(fields[0])
      guard let text = fields[0].currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Snap fixture needs a native field editor") }
      text.setSelectedRange(NSRange(location: 1, length: 1)); e.snapBoundaries(target: target)
      if scenario == "otherFocus" { window.makeFirstResponder(fields[1]) }
      if scenario == "composition" { text.setMarkedText("-", selectedRange: NSRange(location: 1, length: 0), replacementRange: NSRange(location: 0, length: text.string.utf16.count)) }
      let responder = window.firstResponder
      let field = scenario == "otherFocus" ? fields[1] : fields[0]
      let caret = (field.currentEditor() as? NSTextView)?.selectedRange()
      try f.answer(f.snapReport(12, 24))
      if scenario == "composition" {
        try require(text.hasMarkedText() && text.string == "-" && e.waveform.selection == 10...20 && window.firstResponder === responder,
          "Native composition discards delayed snap adoption without changing composition or focus")
        text.unmarkText()
      } else {
        try require(fields.map(\.stringValue) == ["12", "24"] && window.firstResponder === responder && (field.currentEditor() as? NSTextView)?.selectedRange() == caret,
          "Pure native focus/caret movement survives snapped field replacement")
        if target != .selection { try f.answerLoops("snapped"); try f.finishRead() }
        try require(!e.snapBusy && window.firstResponder === responder, "Loop write readback also preserves the current snap field responder")
      }
    } }
  }
}
