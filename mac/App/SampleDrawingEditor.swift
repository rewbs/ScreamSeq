import AppKit

final class SampleDrawingDraft {
  let sample: Int, generation: Int, frames: Int
  let identity: String, revision: String, documentID: String?, channels: String
  let range: Range<Int>, fields: [String: String], settingsGeneration: Int, loopGeneration: Int
  var points = [SampleStrokePoint]()
  var submitted = false, retryable = false
  var acceptedRevision: String?, changedFrames: Int?, reviewed: SampleDrawingRead?
  init(_ editor: SampleEditor, revision: String) {
    sample = editor.index; generation = editor.sampleGeneration; frames = editor.waveform.frames
    identity = editor.settingsIdentity; self.revision = revision; documentID = editor.waveformDocumentID
    channels = editor.selectedChannels; range = editor.waveform.visibleRange; fields = editor.drawingFields
    settingsGeneration = editor.settingsEditGeneration; loopGeneration = editor.loopEditGeneration
  }
  var params: [String: Any] {
    ["sample": sample, "channels": channels, "expectedRevision": revision,
      "points": points.map { ["frame": $0.frame, "value": $0.value] as [String: Any] }]
  }
}

struct SampleDrawingRead {
  let revision: String, info: [String: Any], range: Range<Int>, peaks: [Float]
}

extension SampleEditor {
  var drawingFields: [String: String] {
    var values = settingsDraft.snapshot
    for (i, field) in [loopStart, loopEnd, sustainStart, sustainEnd, selectionStart, selectionEnd,
      exponent, gain, target, smoothing, crossfadeFrames, pasteSourceGain, pasteDestinationGain,
      snapRadius, snapStep, snapOrigin].enumerated() { values["field\(i)"] = field.stringValue }
    for (i, control) in [looping, pingpong, loopReverse, sustaining, sustainPingpong, sustainReverse].enumerated() {
      values["loop\(i)"] = String(control.state.rawValue)
    }
    return values
  }
  func makeDrawingActions() -> NSView {
    updateDrawingActions()
    return stack(.horizontal, [retryDrawingButton, reviewDrawingButton, acceptDrawingButton, discardDrawingButton, NSView()], spacing: 8)
  }
  func updateDrawingActions() {
    let submitted = drawingDraft?.submitted == true
    retryDrawingButton.isEnabled = !drawingBusy && drawingDraft?.retryable == true
    reviewDrawingButton.isEnabled = !drawingBusy && submitted
    acceptDrawingButton.isEnabled = !drawingBusy && drawingDraft?.reviewed != nil
    discardDrawingButton.isEnabled = !drawingBusy && (submitted || !waveform.stroke.isEmpty)
  }
  func finishDrawingWaiters(_ accepted: Bool) {
    let callbacks = drawingWaiters; drawingWaiters = []; callbacks.forEach { $0(accepted) }
  }
  func resetDrawingContext() {
    drawingDraft = nil; drawingBusy = false; waveform.retainsStroke = false
    waveform.cancelStroke(force: true); strokeRevision = nil; waveformDocumentID = nil
    updateDrawingActions(); finishDrawingWaiters(false)
  }
  func ownsDrawing(_ draft: SampleDrawingDraft) -> Bool {
    drawingDraft === draft && index == draft.sample && sampleGeneration == draft.generation
  }
  func drawingFieldsUnchanged(_ draft: SampleDrawingDraft) -> Bool {
    drawingFields == draft.fields && settingsEditGeneration == draft.settingsGeneration &&
      loopEditGeneration == draft.loopGeneration && !hasSampleComposition
  }
  func requestDrawing(_ draft: SampleDrawingDraft, _ method: String, _ params: [String: Any], _ reply: @escaping ([String: Any]) -> Void) {
    if let onSettingsRequest { onSettingsRequest(method, params, draft.identity, reply) }
    else if let onRequest { onRequest(method, params, reply) }
    else { reply(["error": ["message": "Sample requests are unavailable."]]) }
  }
  func submitDrawing(_ draft: SampleDrawingDraft) {
    guard ownsDrawing(draft), !drawingBusy else { return }
    draft.submitted = true; draft.retryable = false; draft.reviewed = nil
    waveform.retainsStroke = true; drawingBusy = true; updateDrawingActions()
    drawingStatus.stringValue = "Applying stroke…"
    requestDrawing(draft, "sample.draw", draft.params) { [weak self] response in
      guard let self, self.ownsDrawing(draft) else { return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
        let data = result["data"] as? [String: Any], let count = data["changedFrames"] as? Int, count >= 0 else {
        draft.retryable = NativeRequestOutcome.canRetryWrite(response)
        self.retainDrawing(draft, (response["error"] as? [String: Any])?["message"] as? String ?? "Stroke outcome is uncertain"); return
      }
      draft.acceptedRevision = revision; draft.changedFrames = count
      guard self.drawingFieldsUnchanged(draft) else { self.retainDrawing(draft, "Newer sample fields kept"); return }
      self.readDrawing(draft, revision: revision) { [weak self] read in
        guard let self, self.ownsDrawing(draft) else { return }
        guard let read, self.drawingFieldsUnchanged(draft) else { self.retainDrawing(draft, "Stroke readback failed or newer fields arrived"); return }
        self.adoptDrawing(read, draft: draft)
        self.drawingStatus.stringValue = count == 0 ? "The stroke made no changes." : "Drew \(count) \(count == 1 ? "frame" : "frames") · one Undo step"
        self.finishDrawingWaiters(true)
      }
    }
  }
  func retainDrawing(_ draft: SampleDrawingDraft, _ message: String) {
    guard ownsDrawing(draft) else { return }
    drawingBusy = false; updateDrawingActions()
    drawingStatus.stringValue = message + (draft.retryable ? " · Stroke kept. Retry uses its original revision." : " · Stroke kept. Review or discard; no write will be repeated.")
    finishDrawingWaiters(false)
  }
  func retryDrawing() {
    guard let draft = drawingDraft, draft.retryable, !drawingBusy else { return }
    guard drawingFieldsUnchanged(draft) else { drawingStatus.stringValue = "Newer sample fields kept. Review or discard the stroke."; return }
    submitDrawing(draft)
  }
  func reviewDrawing() {
    guard let draft = drawingDraft, draft.submitted, !drawingBusy else { return }
    drawingBusy = true; draft.reviewed = nil; updateDrawingActions()
    readDrawing(draft, revision: nil) { [weak self] read in
      guard let self, self.ownsDrawing(draft) else { return }
      self.drawingBusy = false; draft.reviewed = read; self.updateDrawingActions(); self.finishDrawingWaiters(false)
      let outcome = draft.changedFrames.map { "Stroke reported \($0) changed frames. " } ?? "Earlier stroke outcome remains unverified. "
      self.drawingStatus.stringValue = read == nil ? "Review failed. Stroke and fields kept." :
        outcome + "Current sample inspected. Accept reviewed state clears the local stroke without repeating its write; other fields are kept."
    }
  }
  func acceptDrawingReview() {
    guard let draft = drawingDraft, let read = draft.reviewed, !drawingBusy else { return }
    drawingBusy = true; updateDrawingActions()
    requestDrawing(draft, "sample.get", ["sample": draft.sample]) { [weak self] response in
      guard let self, self.ownsDrawing(draft) else { return }
      guard let result = response["result"] as? [String: Any], result["revision"] as? String == read.revision,
        self.drawingDocumentMatches(result, draft), let info = result["data"] as? [String: Any], self.validDrawingInfo(info, draft.sample) else {
        draft.reviewed = nil; self.retainDrawing(draft, "The sample changed after review. Review again"); return
      }
      self.adoptDrawing(read, draft: draft)
      self.drawingStatus.stringValue = "Accepted the inspected sample state. No stroke write was repeated."
      self.finishDrawingWaiters(false)
    }
  }
  func discardDrawing() {
    guard !drawingBusy else { return }
    resetDrawingContext(); drawingStatus.stringValue = "Discarded the local stroke. No write was repeated."
    if let onSelect { onSelect(index) } else { refreshWaveform() }
  }
  func drawingDocumentMatches(_ result: [String: Any], _ draft: SampleDrawingDraft) -> Bool {
    draft.documentID == nil || result["documentId"] as? String == draft.documentID
  }
  func validDrawingInfo(_ info: [String: Any], _ sample: Int) -> Bool {
    validSettingsRead(info, sample: sample) && (info["frames"] as? Int).map { $0 >= 0 } == true &&
      [1, 2].contains(info["channels"] as? Int ?? 0) &&
      ["loop", "loopStart", "loopEnd", "pingpong", "reverseLoop", "sustainLoop", "sustainStart", "sustainEnd", "sustainPingpong", "sustainReverse"].allSatisfy { info[$0] is NSNumber }
  }
  func readDrawing(_ draft: SampleDrawingDraft, revision: String?, _ done: @escaping (SampleDrawingRead?) -> Void) {
    requestDrawing(draft, "sample.get", ["sample": draft.sample]) { [weak self] response in
      guard let self, self.ownsDrawing(draft) else { return }
      guard let result = response["result"] as? [String: Any], let current = result["revision"] as? String,
        revision == nil || revision == current, self.drawingDocumentMatches(result, draft),
        let info = result["data"] as? [String: Any], self.validDrawingInfo(info, draft.sample),
        let frames = info["frames"] as? Int, revision == nil || frames == draft.frames,
        draft.channels != "right" || info["channels"] as? Int == 2 else { done(nil); return }
      let size = min(frames, draft.range.count), start = min(draft.range.lowerBound, frames - size), range = start..<(start + size)
      if range.isEmpty { done(SampleDrawingRead(revision: current, info: info, range: range, peaks: [])); return }
      let bins = range.count <= 4096 ? range.count : 2048
      self.requestDrawing(draft, "sample.waveform.get", ["sample": draft.sample, "start": range.lowerBound, "end": range.upperBound, "channels": draft.channels, "bins": bins]) { [weak self] response in
        guard let self, self.ownsDrawing(draft) else { return }
        guard let result = response["result"] as? [String: Any], result["revision"] as? String == current,
          self.drawingDocumentMatches(result, draft), let peaks = self.validWaveformPeaks(result, sample: draft.sample, range: range, channels: draft.channels, bins: bins) else { done(nil); return }
        done(SampleDrawingRead(revision: current, info: info, range: range, peaks: peaks))
      }
    }
  }
  func adoptDrawing(_ read: SampleDrawingRead, draft: SampleDrawingDraft) {
    drawingDraft = nil; drawingBusy = false; waveform.retainsStroke = false; waveform.cancelStroke(force: true)
    // Retire ordinary reads that began before this owned readback.
    waveformRequest += 1; waveformPending = false; waveformRetry?.cancel(); waveformRetry = nil
    waveform.frames = read.info["frames"] as? Int ?? 0
    channelPicker.selectItem(at: ["both", "left", "right"].firstIndex(of: draft.channels) ?? 0)
    updateSampleDescription(read.info)
    waveform.setViewport(read.range.isEmpty ? nil : read.range, notify: false)
    waveform.peaksRange = read.range; waveform.peaks = read.peaks
    waveformRevision = read.revision; waveformDocumentID = draft.documentID; sampleRevision = read.revision
    if let selection = waveform.selection, selection.upperBound > waveform.frames { waveform.selection = nil }
    if !settingsDraft.hasDraft, !settingsNeedsReload, !settingsFields.contains(where: { $0.currentEditor() != nil }) {
      adoptSettingsRead(read.info, revision: read.revision)
    } else if settingsRevision != read.revision {
      settingsNeedsReload = true; settingsStatus.stringValue = "Fields kept at their captured revision. Reload settings to review."
    }
    if !hasLoopDraft, !loopNeedsReload { updateLoopSettings(read.info, revision: read.revision); syncLoopMarkers() }
    else if loopDraftRevision != read.revision { loopNeedsReload = true }
    updateCrossfadeInfo(read.info)
    updateViewportStatus(); updateDrawingActions()
  }
  func validWaveformPeaks(_ result: [String: Any], sample: Int, range: Range<Int>, channels: String, bins: Int) -> [Float]? {
    guard let data = result["data"] as? [String: Any], data["sample"] as? Int == sample,
      data["start"] as? Int == range.lowerBound, data["end"] as? Int == range.upperBound,
      data["channels"] as? String == channels, data["bins"] as? Int == bins,
      let peaks = data["peaks"] as? [NSNumber], peaks.count == bins * 2,
      peaks.allSatisfy({ $0.doubleValue.isFinite && $0.floatValue.isFinite }) else { return nil }
    return peaks.map(\.floatValue)
  }
}
