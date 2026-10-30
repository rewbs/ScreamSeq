import AppKit

final class SampleOperationFixture {
  let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 1000, height: 1800))
  var revision = "original"
  var calls = [(String, [String: Any], String?)](), pending = [(String, [String: Any], ([String: Any]) -> Void)]()
  var info: [String: Any] = ["index": 1, "name": "Captured", "frames": 1024, "channels": 2, "rate": 44100, "volume": 64, "pan": 128,
    "loop": true, "loopStart": 0, "loopEnd": 512, "pingpong": false, "reverseLoop": false,
    "sustainLoop": true, "sustainStart": 256, "sustainEnd": 900, "sustainPingpong": false, "sustainReverse": false]
  var samples: [[String: Any]] = [["index": 1, "id": "sample-one", "name": "Captured"], ["index": 2, "id": "sample-two", "name": "Other"]]
  func load() {
    editor.onRequest = nil; editor.onSettingsRequest = nil
    editor.update(info, samples: samples, revision: revision)
    editor.waveformRevision = revision; editor.waveformDocumentID = "song"
    editor.waveform.peaksRange = 0..<1024; editor.waveform.peaks = Array(repeating: -0.25, count: 2048)
    editor.waveform.selection = 10...20
  }
  init() {
    load()
    editor.onSampleRevision = { [weak self] in self?.revision }
    connect()
  }
  func connect() {
    editor.onRequest = { [weak self] method, params, reply in self?.enqueue(method, params, nil, reply) }
    editor.onSettingsRequest = { [weak self] method, params, identity, reply in self?.enqueue(method, params, identity, reply) }
  }
  func enqueue(_ method: String, _ params: [String: Any], _ identity: String?, _ reply: @escaping ([String: Any]) -> Void) {
    calls.append((method, params, identity)); pending.append((method, params, reply))
  }
  func answer(_ data: [String: Any], revision: String? = nil, document: String = "song") throws {
    try InterfaceTests.require(!pending.isEmpty, "Expected a pending sample request")
    let request = pending.removeFirst(), token = revision ?? self.revision
    if !request.0.hasSuffix(".get"), request.1["dryRun"] as? Bool != true { self.revision = token }
    request.2(["result": ["revision": token, "documentId": document, "data": data]])
  }
  func fail(_ outcome: String? = nil) throws {
    try InterfaceTests.require(!pending.isEmpty, "Expected a request to reject")
    pending.removeFirst().2(["error": ["code": -32002, "message": "Unavailable", "data": outcome.map { ["writeOutcome": $0] } ?? [:]]])
  }
  func clipboard(_ id: String = "clipboard-A") -> [String: Any] { ["available": true, "clipboardId": id, "frames": 10, "channels": 2, "rate": 44100] }
  func finishRead(frames: Int = 1024, clipboardID: String = "clipboard-A", targetPresent: Bool = true) throws {
    var steps = 0
    while let request = pending.first {
      steps += 1; try InterfaceTests.require(steps <= 5, "Readback must be a bounded read-only sequence")
      switch request.0 {
      case "document.get": try answer(["samples": targetPresent ? samples : []])
      case "sample.get": var value = info; value["index"] = request.1["sample"]; value["frames"] = frames; try answer(value)
      case "sample.waveform.get": var value = request.1; value["peaks"] = Array(repeating: 0.5, count: (value["bins"] as! Int) * 2); try answer(value)
      case "sample.clipboard.get": try answer(clipboard(clipboardID))
      default: throw InterfaceFailure(message: "Unexpected write during readback: \(request.0)")
      }
    }
  }
  func processResult() -> [String: Any] { ["sample": 1, "changedFrames": 10, "peakAfter": 0.5, "clippedSamples": 0] }
}

extension InterfaceTests {
  static func sampleOperationChecks() throws {
    for method in ["sample.process", "sample.crossfade", "sample.clipboard.copy", "sample.cut", "sample.delete", "sample.paste", "sample.copyToNew"] {
      let f = SampleOperationFixture(), e = f.editor
      var report = f.processResult(), frames = 1024, finished = [Bool]()
      switch method {
      case "sample.process": e.process(dryRun: false)
      case "sample.crossfade":
        e.crossfadeMode.selectItem(at: 1); e.crossfadeCurve.selectItem(at: 1); e.crossfade(dryRun: false)
        report["loopBefore"] = ["frames": 512]; report["loopAfter"] = ["frames": 448]
      case "sample.clipboard.copy": e.waveform.copy(nil); report = f.clipboard()
      case "sample.cut", "sample.delete":
        e.clipboardAction(method == "sample.cut" ? "cut" : "delete"); frames = 1014
        report = ["sample": 1, "start": 10, "removedFrames": 10, "insertedFrames": 0, "resultFrames": frames]
        if method == "sample.cut" { report["clipboardId"] = "clipboard-A" }
      case "sample.paste":
        e.pasteMode.selectItem(at: 2); e.pasteModeChanged(); e.pasteSourceGain.stringValue = "-3"; e.pasteDestinationGain.stringValue = "-6"
        e.pasteClipboard(dryRun: false); try f.answer(f.clipboard())
        report = ["sample": 1, "start": 10, "removedFrames": 0, "insertedFrames": 10, "resultFrames": frames, "clipboardId": "clipboard-A"]
      default:
        e.clipboardAction("new"); frames = 10
        report = ["sample": 3, "id": "new-sample", "frames": frames]
        f.samples.append(["index": 3, "id": "new-sample", "name": "Copy"])
      }
      try require(f.calls.last?.0 == method && f.calls.last?.1["expectedRevision"] as? String == "original" && f.calls.last?.2 == "sample-one", "Every sample operation captures original revision and stable identity: \(method)")
      let count = f.calls.count, oldPeaks = e.waveform.peaks
      e.finishAutomaticEdit { finished.append($0) }; e.clipboardAction("copy"); e.process(dryRun: false); e.crossfade(dryRun: false)
      e.requestSampleSelection(2); e.reloadSample()
      try require(f.calls.count == count && finished.isEmpty && e.index == 1, "An owned operation blocks overlapping writes, departure and reload")
      try f.answer(report, revision: method == "sample.clipboard.copy" ? "original" : "accepted")
      try require(f.calls.last?.0 == "document.get" && e.waveform.peaks == oldPeaks && finished.isEmpty, "A write receipt alone cannot replace the native sample or finish Save")
      try f.answer(["samples": f.samples])
      var info = f.info; info["frames"] = frames; info["index"] = method == "sample.copyToNew" ? 3 : 1
      try f.answer(info)
      try require(e.waveform.frames == 1024 && e.waveform.peaks == oldPeaks && finished.isEmpty, "Metadata alone cannot adopt new sample geometry")
      try f.finishRead(frames: frames)
      try require(e.sampleOperation == nil && !e.hasDraft && finished == [true] && e.waveform.frames == frames && e.waveform.peaks.first == 0.5, "Matching staged reads adopt the operation and release Save: \(method)")
      if ["sample.cut", "sample.delete"].contains(method) { try require(e.waveform.selection == 10...10, "Erase leaves a verified insertion cursor") }
      if method == "sample.paste" { try require(e.waveform.selection == 10...20 && f.calls.first?.0 == "sample.clipboard.get" && f.calls[1].1["sourceGainDB"] as? Double == -3 && f.calls[1].1["destinationGainDB"] as? Double == -6, "Mix freezes clipboard identity and exact gains") }
      if method == "sample.copyToNew" { try require(e.index == 3 && e.settingsIdentity == "new-sample" && e.waveform.selection == nil && f.calls.contains { $0.0 == "sample.get" && $0.2 == "new-sample" }, "Copy-to-new adopts only its verified stable destination") }
    }
    for failure in ["notCommitted", "unknown", "readback"] {
      let f = SampleOperationFixture(), e = f.editor
      var finished = [Bool]()
      e.process(dryRun: false); let submitted = f.calls[0].1 as NSDictionary
      e.finishAutomaticEdit { finished.append($0) }
      if failure == "readback" { try f.answer(f.processResult(), revision: "accepted"); try f.fail() }
      else { try f.fail(failure == "notCommitted" ? failure : nil) }
      try require(finished == [false] && e.hasDraft && !e.processing && e.waveformRevision == "original", "Refused, uncertain and failed readback operations retain their payload and settle waiting Save")
      let count = f.calls.count; e.retryOperation()
      if failure == "notCommitted" {
        try require(f.calls.count == count + 1 && submitted.isEqual(to: f.calls.last!.1), "Only a definite not-committed busy receipt permits an exact retry")
        try f.answer(f.processResult(), revision: "accepted"); try f.finishRead()
        try require(!e.hasDraft, "Retry finishes only after readback")
      } else {
        try require(f.calls.count == count, "Unknown and accepted writes cannot be replayed")
        e.gain.stringValue = "-"; e.selectionStart.stringValue = "010"; e.operationPicker.selectItem(at: 4)
        f.revision = "reviewed"; e.reviewOperation(); try f.finishRead()
        try require(e.sampleOperation?.reviewed != nil && e.waveformRevision == "original" && e.gain.stringValue == "-", "Review inspects without discarding newer input or adopting audio")
        e.acceptOperationReview(); try f.finishRead()
        try require(e.sampleOperation == nil && e.operationNeedsReload && e.gain.stringValue == "-" && e.selectionStart.stringValue == "010" && e.sampleRevision == "original" && f.calls.filter { $0.0 == "sample.process" }.count == 1, "Accept acknowledges current state without replaying or rebasing retained fields")
        e.finishAutomaticEdit { finished.append($0) }; try require(finished.last == false, "Retained old-revision fields require explicit Reload before document actions")
        e.reloadSample(); try f.finishRead()
        try require(!e.operationNeedsReload && e.sampleRevision == "reviewed", "Explicit full Reload can adopt acknowledged audio")
      }
    }
    // Clipboard identity can change independently of the document revision.
    let clip = SampleOperationFixture(), c = clip.editor
    c.clipboardAction("copy"); try clip.fail(); c.reviewOperation(); try clip.finishRead()
    c.acceptOperationReview(); try clip.finishRead(clipboardID: "clipboard-B")
    try require(c.sampleOperation != nil && c.sampleOperation?.reviewed == nil && !c.operationNeedsReload, "Accept rechecks the clipboard domain even when song revision is unchanged")
    c.reviewOperation(); try clip.finishRead(clipboardID: "clipboard-B", targetPresent: false)
    c.acceptOperationReview(); try clip.finishRead(clipboardID: "clipboard-B", targetPresent: false)
    try require(c.operationNeedsReload && c.index == 1 && c.waveform.frames == 1024, "An absent original target can be acknowledged without adopting another slot")
    for changed in ["revision", "fields", "document", "empty"] {
      let f = SampleOperationFixture(), e = f.editor
      e.pasteClipboard(dryRun: false)
      if changed == "fields" { e.pasteSourceGain.stringValue = "-7" }
      if changed == "revision" { f.revision = "later" }
      try f.answer(changed == "empty" ? ["available": false] : f.clipboard(), document: changed == "document" ? "different-song" : "song")
      try require(f.calls.count == 1 && e.sampleOperation == nil && !e.clipboardBusy, "Clipboard lookup cannot rebase or submit changed intent: \(changed)")
    }
    // A retired lookup/write cannot clear a newer owner's busy flag or waiters.
    for paste in [false, true] {
      let f = SampleOperationFixture(), e = f.editor
      if paste { e.pasteClipboard(dryRun: false) } else { e.process(dryRun: false) }
      let retired = f.pending.removeFirst().2
      e.resetDocumentContext(); f.load(); f.connect(); e.process(dryRun: false)
      var done = [Bool](); e.finishAutomaticEdit { done.append($0) }
      retired(["result": ["revision": "original", "documentId": "song", "data": paste ? f.clipboard() : f.processResult()]])
      try require(e.processing && done.isEmpty && f.pending.count == 1, "Retired sample callbacks cannot release newer work")
      try f.answer(f.processResult(), revision: "accepted"); try f.finishRead(); try require(done == [true], "The current owner alone completes its waiters")
    }
    for stage in ["document", "metadata", "waveform", "clipboard"] {
      let f = SampleOperationFixture(), e = f.editor
      e.clipboardAction("copy"); try f.answer(f.clipboard())
      if stage != "document" { try f.answer(["samples": f.samples]) }
      if ["waveform", "clipboard"].contains(stage) { try f.answer(f.info) }
      if stage == "clipboard" { var wave = f.pending[0].1; wave["peaks"] = Array(repeating: 0.5, count: (wave["bins"] as! Int) * 2); try f.answer(wave) }
      try f.answer([:], revision: "wrong", document: "wrong-song")
      try require(e.hasDraft && e.sampleOperation?.busy == false && e.waveformRevision == "original", "Wrong or incomplete \(stage) readback retains the operation")
    }
    let preview = SampleOperationFixture(), p = preview.editor
    p.pasteClipboard(dryRun: true); try preview.answer(preview.clipboard())
    let paste: [String: Any] = ["sample": 1, "start": 10, "removedFrames": 0, "insertedFrames": 10, "resultFrames": 1034, "clipboardId": "clipboard-A", "clippedSamples": 2]
    try preview.answer(paste)
    try require(p.pastePreviewID == "clipboard-A" && p.clipboardStatus.stringValue.contains("2 clipped"), "A matching paste preview preserves its clipboard identity and clipping report")
    let count = preview.calls.count; p.pasteClipboard(dryRun: false)
    try require(preview.calls.count == count + 1 && preview.calls.last?.0 == "sample.paste" && preview.calls.last?.1["clipboardId"] as? String == "clipboard-A" && preview.calls.last?.1["expectedRevision"] as? String == "original", "Applying a matching preview uses the frozen clipboard and revision without a fresh lookup")
    try preview.answer(paste, revision: "accepted"); try preview.finishRead(frames: 1034)
    p.waveform.selection = 10...10; let after = preview.calls.count; p.clipboardAction("copy")
    try require(preview.calls.count == after && p.clipboardStatus.stringValue.contains("nonempty"), "Copy rejects an insertion cursor before dispatch")
    p.waveform.selection = 10...20; p.channelPicker.selectItem(at: 1); p.clipboardAction("cut")
    try require(preview.calls.count == after && p.clipboardStatus.stringValue.contains("Both"), "Cut cannot silently remove unselected channels")
    let loops = SampleOperationFixture(), l = loops.editor
    l.loopStart.stringValue = "-"
    l.crossfade(dryRun: true)
    try require(loops.calls.last?.0 == "sample.crossfade", "A read-only crossfade preview may inspect the saved loop while an invalid loop draft is retained")
    try loops.answer(["sample": 1, "changedFrames": 1, "loopBefore": ["frames": 512], "loopAfter": ["frames": 512]])
    l.clipboardAction("copy"); try loops.answer(loops.clipboard()); try loops.finishRead()
    try require(l.sampleOperation == nil && l.hasLoopDraft && l.loopStart.stringValue == "-" && l.loopDraftRevision == "original", "Copy may finish without changing audio or rebasing the retained loop draft")
    let beforeWrite = loops.calls.count; l.process(dryRun: false)
    try require(loops.calls.count == beforeWrite, "Audio writes still require the loop draft to finish")
    try sampleOperationFocusChecks()
  }
  static func sampleOperationFocusChecks() throws {
    let f = SampleOperationFixture(), e = f.editor
    let window = NSWindow(contentRect: e.frame, styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = e; defer { window.orderOut(nil) }
    func expand(_ view: NSView) {
      if let section = view as? ToolSection, section.preference == "toolSection.sample.settings" { section.setExpanded(true, persist: false) }
      view.subviews.forEach(expand)
    }
    expand(e); window.layoutIfNeeded()
    e.process(dryRun: false)
    window.makeFirstResponder(e.name)
    guard let field = e.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Missing native sample field editor") }
    field.setMarkedText("Newer", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    let composingText = field.string, composingCaret = field.selectedRange()
    try f.answer(f.processResult(), revision: "accepted")
    try require(e.sampleOperation != nil && !e.processing && field.hasMarkedText() && e.name.currentEditor() === field, "An accepted result keeps newer native composition and focus")
    e.reviewOperation(); try f.finishRead(); e.acceptOperationReview(); try f.finishRead()
    try require(e.operationNeedsReload && field.hasMarkedText() && field.string == composingText && field.selectedRange() == composingCaret && e.name.currentEditor() === field, "Review and acknowledgement preserve composition, caret and original field ownership: reload=\(e.operationNeedsReload), marked=\(field.hasMarkedText()), text=\(field.string), caret=\(field.selectedRange()), before=\(composingCaret)")
    field.unmarkText(); e.settingsWork?.cancel()
  }
}
