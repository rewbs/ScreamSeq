import AppKit

extension SampleOperationFixture {
  func waitForLoopDispatch() {
    let deadline = Date(timeIntervalSinceNow: 2)
    while editor.loopSaveWork != nil && Date() < deadline { InterfaceTests.pumpMainRunLoop(0.01) }
  }
  var loopCalls: [(String, [String: Any], String?)] { calls.filter { $0.0 == "sample.loops.set" } }
  func loopResult() -> [String: Any] {
    let params = pending.first!.1
    return ["sample": params["sample"]!, "dryRun": params["dryRun"]!, "loopsChanged": true,
      "after": ["normal": params["normal"]!, "sustain": params["sustain"]!]]
  }
  func answerLoops(_ revision: String = "accepted") throws {
    try InterfaceTests.require(pending.first?.0 == "sample.loops.set", "Expected a loop write or preview")
    let params = pending.first!.1, report = loopResult()
    if params["dryRun"] as? Bool == false {
      for (kind, keys) in [("normal", ["loopStart", "loopEnd", "loop", "pingpong", "reverseLoop"]),
        ("sustain", ["sustainStart", "sustainEnd", "sustainLoop", "sustainPingpong", "sustainReverse"])] {
        let row = params[kind] as! [String: Any]
        for (input, saved) in zip(["start", "end", "enabled", "pingpong", "reverse"], keys) { info[saved] = row[input] }
      }
    }
    try answer(report, revision: revision)
  }
}

extension InterfaceTests {
  static func sampleLoopChecks() throws {
    let f = SampleOperationFixture(), e = f.editor
    var finished = [Bool]()
    e.waveform.moveLoopMarker(.start, to: 100, finished: false); f.waitForLoopDispatch()
    try require(f.loopCalls.count == 1 && f.loopCalls[0].2 == "sample-one" && f.loopCalls[0].1["expectedRevision"] as? String == "original", "Native loop drags capture stable identity and original revision: calls=\(f.calls), status=\(e.loopsStatus.stringValue), drawing=\(e.drawingStatus.stringValue), queued=\(e.loopSaveWork != nil), busy=\(e.loopsBusy), fields=\(e.rawLoopDraft)")
    e.waveform.moveLoopMarker(.start, to: 200, finished: true); e.finishAutomaticEdit { finished.append($0) }
    let peaks = e.waveform.peaks
    try f.answerLoops("first")
    try require(f.pending.first?.0 == "sample.get" && e.loopsBusy && finished.isEmpty && e.waveform.peaks == peaks && e.loopDraftRevision == "original", "A write receipt cannot advance loop drafts or release Save")
    try f.answer(f.info)
    try require(f.pending.first?.0 == "sample.waveform.get" && e.loopDraftRevision == "original" && e.waveformRevision == "original", "Metadata alone cannot advance the accepted loop presentation")
    e.waveform.moveLoopMarker(.start, to: 250, finished: true); try f.finishRead(); f.waitForLoopDispatch()
    try require(f.loopCalls.count == 2 && f.loopCalls[1].1["expectedRevision"] as? String == "first" && (f.loopCalls[1].1["normal"] as? [String: Any])?["start"] as? Int == 250 && finished.isEmpty,
      "A gesture continued during both read stages survives and follows only the verified revision")
    try f.answerLoops("second"); try f.finishRead()
    try require(finished == [true] && !e.hasDraft && e.loopDraftRevision == "second" && e.sampleRevision == "second" && e.waveformRevision == "second" && e.settingsRevision == "second", "Complete loop readback advances all sample revisions and releases Save")
    e.waveform.moveLoopMarker(.start, to: 3000, finished: true)
    try require(e.loopStart.integerValue == 511, "Loop start clamps before the exclusive end")
    try f.answerLoops("third"); try f.finishRead()
    e.sustaining.state = .off; e.sustainStart.stringValue = "0"; e.sustainEnd.stringValue = "0"
    e.sustaining.state = .on; e.loopModeChanged(e.sustaining)
    try require(e.sustainEnd.integerValue == 1024, "Enabling an empty sustain loop selects the full sample")
    try f.answerLoops("fourth"); try f.finishRead()
    e.waveform.moveLoopMarker(.sustainEnd, to: 900, finished: true); try f.answerLoops("fifth"); try f.finishRead()
    try require(e.sustainEnd.integerValue == 900 && e.waveform.sustainEnd == 900, "Sustain handles edit the independent sustain loop")
    e.loopReverse.state = .on; e.loopModeChanged(e.loopReverse)
    try require(e.pingpong.state == .off && (f.loopCalls.last?.1["normal"] as? [String: Any])?["reverse"] as? Bool == true, "Reverse saves immediately and excludes ping-pong")
    try f.answerLoops("sixth"); try f.finishRead()
    e.pingpong.state = .on; e.loopModeChanged(e.pingpong); try require(e.loopReverse.state == .off, "Ping-pong excludes reverse")
    try f.answerLoops("seventh"); try f.finishRead()
    let count = f.calls.count
    for raw in ["-1", "1.5", "999999999999999999999", "-"] { e.loopStart.stringValue = raw; e.loopFieldChanged() }
    try require(f.calls.count == count, "Invalid loop fields cannot modify the song")
    // A loop completion must not require a redundant full sample reload before processing.
    e.loopStart.stringValue = "511"; e.process(dryRun: false)
    try require(f.calls.last?.0 == "sample.process" && f.calls.last?.1["expectedRevision"] as? String == "seventh", "Verified loop completion authorizes the next sample operation at its accepted revision")
    try f.answer(f.processResult(), revision: "processed"); try f.finishRead()
    try sampleLoopRetryCompletionChecks()
    try sampleLoopWriteFailureChecks()
    try sampleLoopPreviewChecks()
    try sampleLoopWriteFocusChecks()
    try sampleLoopWriteFocusMovementChecks()
    try sampleLoopReloadChecks()
  }
  static func sampleLoopRetryCompletionChecks() throws {
    for action in ["Return", "checkbox", "handle release"] {
      let f = SampleOperationFixture(), e = f.editor
      var finished = [Bool]()
      e.waveform.moveLoopMarker(.start, to: 100, finished: true)
      let original = f.loopCalls[0].1 as NSDictionary
      try f.fail("notCommitted"); e.finishAutomaticEdit { finished.append($0) }
      switch action {
      case "Return": e.loopStart.stringValue = "120"; e.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: e.loopStart)); e.loopFieldChanged()
      case "checkbox": e.sustaining.state = .off; e.loopModeChanged(e.sustaining)
      default: e.waveform.moveLoopMarker(.start, to: 120, finished: true)
      }
      f.waitForLoopDispatch()
      try require(f.loopCalls.count == 2 && original.isEqual(to: f.loopCalls[1].1) && e.loopsBusy && finished.isEmpty, "\(action) cannot cancel or alter an owned exact-payload retry")
      try f.answerLoops("retry:1")
      if action == "handle release" {
        try f.finishRead(); f.waitForLoopDispatch()
        try require(f.loopCalls.count == 3 && f.loopCalls[2].1["expectedRevision"] as? String == "retry:1", "Continued handles follow verified readback after retry")
        try f.answerLoops("retry:2"); try f.finishRead()
        try require(finished == [true] && !e.hasDraft, "A completed handle retry releases Save")
      } else {
        try require(finished == [false] && e.loopNeedsReload && !e.loopsBusy && e.loopDraftRevision == "original" && f.pending.isEmpty, "Newer typed fields or modes retain their original revision and settle waiting Save")
      }
    }
    let bounded = SampleOperationFixture(), b = bounded.editor
    b.waveform.moveLoopMarker(.start, to: 100, finished: true)
    for _ in 0..<6 { try bounded.fail("notCommitted"); bounded.waitForLoopDispatch() }
    try require(bounded.loopCalls.count == 6 && bounded.pending.isEmpty && !b.loopsBusy && b.loopSaveWork == nil && b.loopNeedsReload, "Automatic retries stop after six definitely-not-committed attempts")
    let stale = SampleOperationFixture(), s = stale.editor
    s.waveform.moveLoopMarker(.start, to: 100, finished: true); try stale.fail("notCommitted"); stale.revision = "external"; stale.waitForLoopDispatch()
    try require(stale.loopCalls.count == 1 && !s.loopsBusy && s.loopNeedsReload, "External revisions stop a queued retry without rebasing it")
  }
  static func sampleLoopWriteFailureChecks() throws {
    for stage in ["unknown", "receipt", "metadata", "geometry", "loops", "identity", "waveform", "revision", "document", "fields", "options", "selection", "transport"] {
      let f = SampleOperationFixture(), e = f.editor
      var done = [Bool]()
      e.waveform.moveLoopMarker(.start, to: 100, finished: true); e.finishAutomaticEdit { done.append($0) }
      let baseline = e.loopDraftBaseline!, peaks = e.waveform.peaks
      if stage == "unknown" { try f.fail() }
      else if stage == "receipt" { try f.answer(["loopsChanged": true], revision: "accepted") }
      else {
        try f.answerLoops()
        if stage == "fields" { e.gain.stringValue = "-7" }
        if stage == "options" { e.pasteMode.selectItem(at: 2) }
        if stage == "selection" { e.waveform.selection = 20...30 }
        if stage == "transport" { e.onRequest = nil; e.onSettingsRequest = nil }
        var info = f.info
        if stage == "metadata" { info.removeValue(forKey: "rate") }
        if stage == "geometry" { info["frames"] = 2048 }
        if stage == "loops" { info["loopStart"] = 101 }
        if stage == "identity" { info["id"] = "different" }
        try f.answer(info)
        if !f.pending.isEmpty {
          var wave = f.pending[0].1; wave["peaks"] = Array(repeating: 0.5, count: (wave["bins"] as! Int) * 2)
          if stage == "waveform" { wave["peaks"] = [0.5] }
          try f.answer(wave, revision: stage == "revision" ? "later" : nil, document: stage == "document" ? "different-song" : "song")
        }
      }
      try require(done == [false] && !e.loopsBusy && e.loopNeedsReload && e.loopDraftRevision == "original" && e.loopDraftBaseline!.isEqual(baseline) && e.waveformRevision == "original" && e.waveform.peaks == peaks && e.loopStart.stringValue == "100", "\(stage) retains raw loops and the previous complete presentation and settles Save")
      e.setLoops(dryRun: false); pumpMainRunLoop(0.05)
      try require(f.loopCalls.count == 1, "Accepted or uncertain loop writes are never replayed after \(stage)")
    }
    let empty = SampleOperationFixture(), z = empty.editor
    z.resetDocumentContext(); empty.info["frames"] = 0
    for key in ["loopStart", "loopEnd", "sustainStart", "sustainEnd"] { empty.info[key] = 0 }
    for key in ["loop", "sustainLoop"] { empty.info[key] = false }
    empty.load(); empty.connect(); z.waveform.selection = 0...0
    z.setLoops(dryRun: false); try empty.answerLoops(); try empty.finishRead(frames: 0)
    try require(!z.loopsBusy && !z.hasDraft && z.waveform.peaks.isEmpty && z.waveformRevision == "accepted" && !empty.calls.contains { $0.0 == "sample.waveform.get" }, "Empty samples complete with validated empty waveform data without a zero-bin request")
    for stage in ["write", "metadata", "waveform", "preview"] {
      let f = SampleOperationFixture(), e = f.editor
      var old = [Bool](), next = [Bool]()
      e.loopStart.stringValue = "100"; e.setLoops(dryRun: stage == "preview"); e.finishAutomaticEdit { old.append($0) }
      if ["metadata", "waveform"].contains(stage) { try f.answerLoops() }
      if stage == "waveform" { try f.answer(f.info) }
      let retired = f.pending.removeFirst().2
      e.resetDocumentContext(); f.revision = "next"; f.load(); f.connect()
      e.waveform.moveLoopMarker(.start, to: 150, finished: true); e.finishAutomaticEdit { next.append($0) }
      retired(["error": ["message": "Retired request"]])
      try require(old == [false] && next.isEmpty && e.loopsBusy && f.pending.count == 1, "Retired \(stage) callbacks cannot release the current owner or its waiters")
      try f.answerLoops("next:1"); try f.finishRead(); try require(next == [true], "Only the current loop owner completes Save")
    }
  }
  static func sampleLoopPreviewChecks() throws {
    for waiting in [false, true] {
      let f = SampleOperationFixture(), e = f.editor
      var done = [Bool]()
      e.loopStart.stringValue = "100"; e.saveLoopsSoon(); e.loopPreviewButton.handler?(); pumpMainRunLoop(0.08)
      try require(f.loopCalls.count == 1 && f.loopCalls[0].1["dryRun"] as? Bool == true && e.loopSaveWork == nil, "Native Preview cancels queued autosave and sends one dry run")
      if waiting { e.finishAutomaticEdit { done.append($0) } }
      try f.answerLoops("original")
      if !waiting {
        try require(f.pending.isEmpty && e.loopPreviewRevision == "original" && e.hasLoopDraft && !e.loopsBusy && e.loopsStatus.stringValue.contains("Audio and history are unchanged"), "A successful preview keeps its original draft and makes no write")
        e.loopApplyButton.handler?()
      }
      try require(f.loopCalls.count == 2 && f.loopCalls[1].1["dryRun"] as? Bool == false && f.loopCalls[1].1["expectedRevision"] as? String == "original" && done.isEmpty, "Save loops or a waiting document Save applies the captured preview revision")
      try f.answerLoops(); try f.finishRead()
      try require(!e.hasDraft && (!waiting || done == [true]), "Preview completion cannot strand waiting document actions")
    }
    for change in ["text", "gesture", "failed", "revision"] {
      let f = SampleOperationFixture(), e = f.editor
      var done = [Bool]()
      e.loopStart.stringValue = "100"; e.loopPreviewButton.handler?(); e.finishAutomaticEdit { done.append($0) }
      if change == "text" { e.loopStart.stringValue = "120" }
      if change == "gesture" { e.waveform.moveLoopMarker(.start, to: 120, finished: true) }
      if change == "failed" { try f.fail() } else { try f.answerLoops(change == "revision" ? "other" : "original") }
      try require(done == [false] && e.loopPreviewSignature == nil && !e.loopsBusy && f.loopCalls.count == 1 && e.hasLoopDraft, "Changed or refused \(change) preview settles Save without submitting music")
    }
    let settings = SampleOperationFixture(), e = settings.editor
    e.name.stringValue = "Unsubmitted settings"; e.loopStart.stringValue = "100"; e.loopPreviewButton.handler?()
    try require(settings.calls.isEmpty && e.settingsDraft.hasDraft && e.hasLoopDraft, "Loop Preview cannot silently commit unrelated settings and loops")
  }
  static func sampleLoopWriteFocusChecks() throws {
    for change in [false, true] {
      let f = SampleOperationFixture(), e = f.editor
      let window = NSWindow(contentRect: e.frame, styleMask: [.titled], backing: .buffered, defer: false)
      window.isReleasedWhenClosed = false; window.contentView = e
      defer { e.settingsWork?.cancel(); e.loopSaveWork?.cancel(); window.orderOut(nil); window.contentView = nil }
      func expand(_ view: NSView) { if let section = view as? ToolSection { section.setExpanded(true, persist: false) }; view.subviews.forEach(expand) }
      expand(e); window.layoutIfNeeded()
      e.loopStart.stringValue = "100"; window.makeFirstResponder(e.loopPreviewButton); e.loopPreviewButton.handler?()
      try f.answerLoops("original")
      try require(window.firstResponder === e.loopPreviewButton && e.loopPreviewRevision == "original", "Keyboard Preview preserves its native button focus")
      window.makeFirstResponder(e.loopApplyButton); e.loopApplyButton.handler?()
      var done = [Bool](); e.finishAutomaticEdit { done.append($0) }; try f.answerLoops(); try f.answer(f.info)
      if change {
        window.makeFirstResponder(e.name)
        guard let field = e.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Missing loop write field editor") }
        field.setMarkedText("New", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
        let text = field.string, caret = field.selectedRange(); try f.finishRead()
        try require(done == [false] && e.loopDraftRevision == "original" && field.hasMarkedText() && field.string == text && field.selectedRange() == caret && e.name.currentEditor() === field, "Late loop write readback preserves newer native composition, caret and focus")
        field.unmarkText()
      } else {
        try f.finishRead()
        try require(done == [true] && window.firstResponder === e.loopApplyButton && !e.hasDraft, "Keyboard Save loops retains button focus through complete readback and releases Save")
      }
    }
  }
  static func sampleLoopWriteFocusMovementChecks() throws {
    for stage in ["write", "metadata", "waveform", "preview"] {
      for move in ["caret", "next field"] {
        let f = SampleOperationFixture(), e = f.editor
        let window = NSWindow(contentRect: e.frame, styleMask: [.titled], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = e
        defer { e.settingsWork?.cancel(); e.loopSaveWork?.cancel(); window.orderOut(nil); window.contentView = nil }
        func expand(_ view: NSView) { if let section = view as? ToolSection { section.setExpanded(true, persist: false) }; view.subviews.forEach(expand) }
        expand(e); window.layoutIfNeeded(); window.makeFirstResponder(e.loopStart)
        guard let initial = e.loopStart.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Missing loop boundary field editor") }
        initial.insertText("100", replacementRange: NSRange(location: 0, length: initial.string.utf16.count))
        if stage == "preview" { e.setLoops(dryRun: true) } else { f.waitForLoopDispatch() }
        try require(f.loopCalls.count == 1, "A typed loop boundary starts autosave or explicit preview")
        var done = [Bool](); e.finishAutomaticEdit { done.append($0) }
        if ["metadata", "waveform"].contains(stage) { try f.answerLoops() }
        if stage == "waveform" { try f.answer(f.info) }
        // An unordered test window has no navigable AppKit key-view loop.
        // Move its real field editor explicitly and verify the focus change.
        if move == "next field" { window.makeFirstResponder(e.loopEnd) }
        try require(move != "next field" || (e.loopStart.currentEditor() == nil && e.loopEnd.currentEditor() != nil), "Native focus actually leaves the submitted loop field")
        let responder = window.firstResponder
        guard let current = responder as? NSTextView else { throw InterfaceFailure(message: "Navigation should reach the next native field") }
        current.setSelectedRange(NSRange(location: 1, length: 0))
        let text = current.string, caret = current.selectedRange()
        if stage == "write" { try f.answerLoops() }
        if stage == "preview" {
          try f.answerLoops("original")
          try require(f.pending.first?.0 == "sample.loops.set" && done.isEmpty, "Focus movement does not prevent Preview from servicing waiting Save")
          try f.answerLoops()
        }
        try f.finishRead()
        try require(done == [true] && !e.hasDraft && e.loopDraftRevision == "accepted" && window.firstResponder === responder && current.string == text && current.selectedRange() == caret,
          "\(move) during \(stage) preserves current focus/caret and releases Save without Reload")
      }
    }
  }
}
