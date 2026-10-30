import AppKit

final class SampleWaveformQuery {
  let sample: Int, generation: Int, frames: Int, settingsGeneration: Int, loopGeneration: Int
  let range: Range<Int>, channels: String, identity: String, revision: String?, documentID: String?
  let fields: [String: String]
  init(_ editor: SampleEditor, range: Range<Int>, channels: String) {
    sample = editor.index; generation = editor.sampleGeneration; frames = editor.waveform.frames
    self.range = range; self.channels = channels; identity = editor.settingsIdentity
    revision = editor.sampleRevision; documentID = editor.waveformDocumentID
    fields = editor.drawingFields; settingsGeneration = editor.settingsEditGeneration; loopGeneration = editor.loopEditGeneration
  }
}

extension SampleEditor {
  func makeWaveformControls() -> NSView {
    func button(_ title:String,_ label:String,_ action:@escaping ()->Void) -> ActionButton {
      let result=ActionButton(title,action:action);result.setAccessibilityLabel(label);result.toolTip=label;return result
    }
    drawToggle.target=self; drawToggle.action=#selector(toggleDrawing)
    drawToggle.setAccessibilityHelp("Zoom to individual frames. Drawing with Both selected replaces both channels with the same shape.")
    drawingStatus.lineBreakMode = .byWordWrapping; drawingStatus.maximumNumberOfLines=4
    drawingStatus.preferredMaxLayoutWidth = 600; drawingStatus.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    return stack(.horizontal,[
      ActionButton("Whole") { [weak self] in self?.waveform.setViewport(nil) },
      ActionButton("Selection") { [weak self] in self?.waveform.zoomSelection() },
      button("−","Zoom out") { [weak self] in self?.waveform.zoom(0.5) },
      button("+","Zoom in") { [weak self] in self?.waveform.zoom(2) },
      button("←","Pan left") { [weak self] in guard let self else { return }; self.waveform.panFrames(-max(1,self.waveform.navigationRange.count/2)) },
      button("→","Pan right") { [weak self] in guard let self else { return }; self.waveform.panFrames(max(1,self.waveform.navigationRange.count/2)) },
      NSView(),drawToggle],spacing:10)
  }
  func viewportContext(frames:Int,revision:String) -> [String:Any] {
    let matchingLength=frames==waveform.frames
    let range=matchingLength ? waveform.visibleRange : 0..<max(0,frames)
    let loaded=matchingLength && waveformRevision==revision && !waveform.peaks.isEmpty
    return ["sample":index,"start":range.lowerBound,"end":range.upperBound,"channels":selectedChannels,
      "drawing":waveform.drawing,"loaded":loaded,"precise":loaded && waveform.precise]
  }
  func updateViewportStatus() {
    let range=waveform.visibleRange
    viewportStatus.stringValue="Showing \(range.lowerBound) → \(range.upperBound) · \(range.count) frames" + (waveformQuery == nil ? "" : " · loading…")
  }
  @objc func toggleDrawing() {
    if drawingDraft?.submitted == true { drawToggle.state = waveform.drawing ? .on : .off; drawingStatus.stringValue = "Review or discard the retained stroke before changing drawing mode."; return }
    waveform.drawing=drawToggle.state == .on
    drawingStatus.stringValue=waveform.drawing ? "Zoom to individual frames, then drag to draw. Both channels receive the same shape." : "Drag to select audio. Zoom with + / − or Option-scroll."
    if waveform.drawing { refreshWaveform() }
  }
  func cancelWaveformQuery() {
    waveformPending = false; waveformQuery = nil; waveform.requestedRange = nil
    waveformRetry?.cancel(); waveformRetry = nil; waveformAttempts = 0
  }
  func retireWaveform() {
    cancelWaveformQuery()
    waveformRevision = nil; waveformDocumentID = nil; sampleRevision = nil; snapPending = false; waveform.cancelStroke()
  }
  func refreshWaveform() { requestWaveform(range: waveformQuery?.range ?? waveform.viewport, channels: waveformQuery?.channels ?? waveformChannels) }
  func requestWaveform(range: Range<Int>?, channels: String? = nil) {
    guard drawingDraft?.submitted != true, waveform.stroke.isEmpty else { return }
    guard !hasSampleComposition else { drawingStatus.stringValue = "Finish composing before changing the sample view."; return }
    cancelSampleSelection()
    let range = range ?? 0..<max(0, waveform.frames), channels = channels ?? waveformQuery?.channels ?? waveformChannels
    cancelWaveformQuery()
    if onRequest == nil && onSettingsRequest == nil {
      waveform.setViewport(range, notify: false); waveformChannels = channels
      channelPicker.selectItem(at: ["both", "left", "right"].firstIndex(of: channels) ?? 0)
      updateViewportStatus(); return
    }
    let query = SampleWaveformQuery(self, range: range, channels: channels)
    waveformQuery = query; waveform.requestedRange = range; waveformPending = true
    updateViewportStatus(); pumpWaveform()
  }
  func waveformQueryUnchanged(_ query: SampleWaveformQuery) -> Bool {
    query.sample == index && query.generation == sampleGeneration && query.frames == waveform.frames &&
      query.identity == settingsIdentity && query.revision == sampleRevision && query.fields == drawingFields &&
      query.settingsGeneration == settingsEditGeneration && query.loopGeneration == loopEditGeneration && !hasSampleComposition
  }
  func finishWaveformQuery(_ message: String? = nil) {
    cancelWaveformQuery(); updateViewportStatus()
    if let message { drawingStatus.stringValue = message + " · Previous view kept." }
  }
  func pumpWaveform() {
    guard waveformPending, !waveformInFlight, let query = waveformQuery else { return }
    guard waveformQueryUnchanged(query) else { finishWaveformQuery("Sample or fields changed while loading"); return }
    let range = query.range, bins = range.count <= 4096 ? range.count : 2048
    guard !range.isEmpty else {
      waveform.setViewport(nil, notify: false); waveform.peaks = []; waveform.peaksRange = nil
      waveformChannels = query.channels; waveformRevision = query.revision
      channelPicker.selectItem(at: ["both", "left", "right"].firstIndex(of: query.channels) ?? 0)
      finishWaveformQuery(); return
    }
    waveformPending = false; waveformInFlight = true
    let receive: ([String: Any]) -> Void = { [weak self] response in
      guard let self else { return }; self.waveformInFlight = false
      guard self.waveformQuery === query else { self.pumpWaveform(); return }
      guard self.waveformQueryUnchanged(query) else { self.finishWaveformQuery("Sample or fields changed while loading"); return }
      if let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
        query.revision == nil || query.revision == revision,
        query.documentID == nil || query.documentID == result["documentId"] as? String,
        let peaks = self.validWaveformPeaks(result, sample: query.sample, range: range, channels: query.channels, bins: bins) {
        // The visible range, channel menu and waveform move together only after
        // the entire reply and the retained field generation have been checked.
        self.waveform.setViewport(range, notify: false); self.waveformChannels = query.channels
        self.channelPicker.selectItem(at: ["both", "left", "right"].firstIndex(of: query.channels) ?? 0)
        self.waveform.peaksRange = range; self.waveform.peaks = peaks; self.waveformRevision = revision
        self.waveformDocumentID = result["documentId"] as? String
        self.finishWaveformQuery()
      } else if NativeRequestOutcome.canRetryRead(response), self.waveformAttempts < 6 {
        self.waveformAttempts += 1; self.waveformPending = true
        let retry = DispatchWorkItem { [weak self] in
          guard let self, self.waveformQuery === query else { return }; self.waveformRetry = nil; self.pumpWaveform()
        }
        self.waveformRetry = retry
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05 * pow(2, Double(self.waveformAttempts - 1)), execute: retry)
      } else {
        self.finishWaveformQuery((response["error"] as? [String: Any])?["message"] as? String ?? "Waveform readback changed or was incomplete")
      }
    }
    let params: [String: Any] = ["sample": query.sample, "start": range.lowerBound, "end": range.upperBound, "channels": query.channels, "bins": bins]
    if let onSettingsRequest { onSettingsRequest("sample.waveform.get", params, query.identity, receive) }
    else if let onRequest { onRequest("sample.waveform.get", params, receive) }
    else { waveformInFlight = false; finishWaveformQuery("Sample requests are unavailable") }
  }
  func beginDrawing() -> Bool {
    guard drawingDraft == nil, sampleSelectionQuery == nil, waveformQuery == nil, !waveformInFlight, !hasDraft, !snapBusy, !crossfadeBusy, !processing, !clipboardBusy,
      canBeginDrawing?() ?? true,waveform.precise,let revision=waveformRevision else {
      drawingStatus.stringValue="Zoom in to individual frames and wait for the waveform to load before drawing.";return false
    }
    strokeRevision=revision
    drawingDraft = SampleDrawingDraft(self, revision: revision)
    drawingStatus.stringValue="Drawing \(selectedChannels == "both" ? "both channels" : selectedChannels) · Escape cancels · release to apply"
    return true
  }
  func commitDrawing(_ points:[SampleStrokePoint]) {
    guard !drawingBusy, let draft = drawingDraft, !draft.submitted, ownsDrawing(draft), !points.isEmpty else { return }
    guard drawingFieldsUnchanged(draft) else {
      waveform.cancelStroke(); drawingStatus.stringValue = "Fields changed while drawing. Gesture cancelled; newer fields kept."; return
    }
    draft.points = points; submitDrawing(draft)
  }
}
