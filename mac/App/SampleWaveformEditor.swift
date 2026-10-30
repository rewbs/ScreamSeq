import AppKit

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
      button("←","Pan left") { [weak self] in guard let self else { return }; self.waveform.panFrames(-max(1,self.waveform.visibleRange.count/2)) },
      button("→","Pan right") { [weak self] in guard let self else { return }; self.waveform.panFrames(max(1,self.waveform.visibleRange.count/2)) },
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
    viewportStatus.stringValue="Showing \(range.lowerBound) → \(range.upperBound) · \(range.count) frames"
  }
  func viewportChanged() { updateViewportStatus();refreshWaveform() }
  @objc func toggleDrawing() {
    if drawingDraft?.submitted == true { drawToggle.state = waveform.drawing ? .on : .off; drawingStatus.stringValue = "Review or discard the retained stroke before changing drawing mode."; return }
    waveform.drawing=drawToggle.state == .on
    drawingStatus.stringValue=waveform.drawing ? "Zoom to individual frames, then drag to draw. Both channels receive the same shape." : "Drag to select audio. Zoom with + / − or Option-scroll."
    if waveform.drawing { refreshWaveform() }
  }
  func retireWaveform() {
    waveformRequest+=1;waveformPending=false;waveformRetry?.cancel();waveformRetry=nil;waveformAttempts=0
    waveformRevision=nil;waveformDocumentID=nil;sampleRevision=nil;snapPending=false;waveform.cancelStroke()
  }
  func refreshWaveform() {
    guard drawingDraft?.submitted != true else { return }
    waveformRequest+=1;waveformPending=true;waveformRevision=nil;waveformAttempts=0
    waveformRetry?.cancel();waveformRetry=nil
    pumpWaveform()
  }
  func pumpWaveform() {
    guard waveformPending,!waveformInFlight,let onRequest else { return }
    let range=waveform.visibleRange
    guard !range.isEmpty else { waveformPending=false;waveform.peaks=[];return }
    let request=waveformRequest,sample=index,generation=sampleGeneration,channels=selectedChannels
    waveformPending=false;waveformInFlight=true
    let bins=range.count<=4096 ? range.count : 2048
    onRequest("sample.waveform.get",["sample":sample,"start":range.lowerBound,"end":range.upperBound,"channels":channels,"bins":bins]) { [weak self] response in
      guard let self else { return };self.waveformInFlight=false
      guard request==self.waveformRequest,sample==self.index,generation==self.sampleGeneration,range==self.waveform.visibleRange,channels==self.selectedChannels else {
        self.pumpWaveform();return
      }
      if let result=response["result"] as? [String:Any],let revision=result["revision"] as? String,
        let peaks=self.validWaveformPeaks(result,sample:sample,range:range,channels:channels,bins:bins) {
        self.waveform.peaksRange=range;self.waveform.peaks=peaks;self.waveformRevision=revision
        self.waveformDocumentID=result["documentId"] as? String
        self.waveformAttempts=0
      } else {
        let error=response["error"] as? [String:Any]
        if error?["code"] as? Int == -32002,self.waveformAttempts<6 {
          self.waveformAttempts+=1;self.waveformPending=true
          let retry=DispatchWorkItem { [weak self] in guard let self,request==self.waveformRequest else { return };self.waveformRetry=nil;self.pumpWaveform() }
          self.waveformRetry=retry;DispatchQueue.main.asyncAfter(deadline:.now()+0.05*pow(2,Double(self.waveformAttempts-1)),execute:retry)
        } else { self.drawingStatus.stringValue=error?["message"] as? String ?? "Waveform could not be loaded." }
      }
    }
  }
  func beginDrawing() -> Bool {
    guard drawingDraft == nil, !hasDraft, !snapBusy, !crossfadeBusy, !processing, !clipboardBusy,
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
