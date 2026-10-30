import AppKit

extension SampleEditor {
  func reloadLoops() {
    guard !protectsSampleOperation else { operationStatus.stringValue = "Review the sample operation before reloading loops."; return }
    guard !protectsDrawingInput, waveform.stroke.isEmpty else { drawingStatus.stringValue = "Finish or discard drawing input before reloading loops."; return }
    guard !sampleOperationPending, !waveformInFlight, !sampleSelectionInFlight, !waveform.draggingLoop, !hasSampleComposition else {
      loopsStatus.stringValue = "Finish the current sample action before reloading loops."; return
    }
    guard !settingsIdentity.isEmpty, catalogue.contains(where: { $0.index == index && $0.identity == settingsIdentity }),
      (onSampleRevision?() ?? sampleRevision)?.isEmpty == false, waveformDocumentID?.isEmpty == false,
      onSettingsRequest != nil || onRequest != nil else {
      loopsStatus.stringValue = "The captured sample is unavailable. Reload the sample before its loops."; return
    }
    loopSaveWork?.cancel(); loopSaveWork = nil
    cancelSampleSelection(); cancelWaveformQuery()
    let query = SampleSelectionQuery(self, sample: index, identity: settingsIdentity, kind: .reload, selectInput: false)
    loopReloadQuery = query; loopsBusy = true; loopsStatus.stringValue = "Loading current loops and waveform…"
    requestLoopReload(query, "sample.get", ["sample": query.sample]) { [weak self] response in
      guard let self, self.continueLoopReload(query) else { return }
      guard let result = self.loopReloadResult(response, query), let revision = result["revision"] as? String,
        let info = result["data"] as? [String: Any], self.validLoopReloadInfo(info, query: query),
        let frames = info["frames"] as? Int, query.channels != "right" || info["channels"] as? Int == 2 else {
        self.failLoopReload(query, "Loop metadata changed, was incomplete or no longer supports this channel"); return
      }
      let region = query.region.clamped(to: frames), range = region.range
      func adopt(_ peaks: [Float]) {
        self.adoptLoopReload(SampleSelectionRead(info: info, revision: revision, documentID: query.documentID, region: region, channels: query.channels, peaks: peaks), query: query)
      }
      if range.isEmpty { adopt([]); return }
      let bins = range.count <= 4096 ? range.count : 2048
      self.requestLoopReload(query, "sample.waveform.get", ["sample": query.sample, "start": range.lowerBound, "end": range.upperBound, "channels": query.channels, "bins": bins]) { [weak self] response in
        guard let self, self.continueLoopReload(query) else { return }
        guard let result = self.loopReloadResult(response, query),
          let peaks = self.validWaveformPeaks(result, sample: query.sample, range: range, channels: query.channels, bins: bins) else {
          self.failLoopReload(query, "Loop waveform changed or was incomplete"); return
        }
        adopt(peaks)
      }
    }
  }
  func ownsLoopReload(_ query: SampleSelectionQuery) -> Bool {
    loopReloadQuery === query && index == query.sample && sampleGeneration == query.generation && settingsIdentity == query.identity
  }
  func loopReloadUnchanged(_ query: SampleSelectionQuery) -> Bool {
    (onSampleRevision?() ?? sampleRevision) == query.revision && waveformDocumentID == query.documentID &&
      catalogue.contains(where: { $0.index == query.sample && $0.identity == query.identity }) &&
      settingsEditGeneration == query.settingsGeneration && loopEditGeneration == query.loopGeneration &&
      drawingFields == query.fields && sampleSelectionOptions == query.options && !hasSampleComposition &&
      SampleViewRegion(self) == query.originalRegion && waveformChannels == query.originalChannels && waveform.stroke == query.stroke &&
      window?.firstResponder === query.responder && focusedSampleField == query.focusedField &&
      focusedSampleField.flatMap({ (sampleEditingFields[$0].currentEditor() as? NSTextView)?.selectedRange() }) == query.caret
  }
  func continueLoopReload(_ query: SampleSelectionQuery) -> Bool {
    guard ownsLoopReload(query) else { return false }
    guard loopReloadUnchanged(query) else { failLoopReload(query, "Sample, fields or focus changed while loading loops"); return false }
    return true
  }
  func failLoopReload(_ query: SampleSelectionQuery, _ message: String) {
    guard ownsLoopReload(query) else { return }
    loopReloadQuery = nil; loopsBusy = false; loopNeedsReload = true
    loopsStatus.stringValue = message + " · Previous loops, waveform and fields kept."
    finishLoopWaiters(false)
  }
  func loopReloadResult(_ response: [String: Any], _ query: SampleSelectionQuery) -> [String: Any]? {
    guard let result = response["result"] as? [String: Any], result["revision"] as? String == query.revision,
      result["documentId"] as? String == query.documentID else { return nil }
    return result
  }
  func validLoopReloadInfo(_ info: [String: Any], query: SampleSelectionQuery) -> Bool {
    guard validDrawingInfo(info, query.sample), let frames = info["frames"] as? Int, (0...Int(UInt32.max)).contains(frames),
      info["id"] == nil || info["id"] as? String == query.identity else { return false }
    for keys in [("loopStart", "loopEnd", "loop", "pingpong", "reverseLoop"),
      ("sustainStart", "sustainEnd", "sustainLoop", "sustainPingpong", "sustainReverse")] {
      guard let start = info[keys.0] as? Int, let end = info[keys.1] as? Int,
        let enabled = info[keys.2] as? Bool, let pingpong = info[keys.3] as? Bool, let reverse = info[keys.4] as? Bool,
        start >= 0, start <= end, end <= frames, !enabled || start < end, !pingpong || !reverse else { return false }
    }
    return true
  }
  func requestLoopReload(_ query: SampleSelectionQuery, _ method: String, _ params: [String: Any], _ reply: @escaping ([String: Any]) -> Void) {
    if let onSettingsRequest { onSettingsRequest(method, params, query.identity, reply) }
    else if let onRequest { onRequest(method, params, reply) }
    else { failLoopReload(query, "Sample requests are unavailable") }
  }
  func adoptLoopReload(_ read: SampleSelectionRead, query: SampleSelectionQuery) {
    guard continueLoopReload(query) else { return }
    let keepRangeFields = hasSelectionInput, operation = operationPicker.indexOfSelectedItem
    loopReloadQuery = nil
    waveform.frames = read.region.frames; waveform.setViewport(read.region.viewport, notify: false)
    waveform.peaksRange = read.range; waveform.peaks = read.peaks
    waveformRevision = read.revision; waveformDocumentID = read.documentID; sampleRevision = read.revision
    preserveSelectionFields = keepRangeFields; waveform.selection = read.region.selection; preserveSelectionFields = false
    updateSampleDescription(read.info)
    // Loop-only Reload does not authorize replacing an unrelated operation choice.
    operationPicker.selectItem(at: operation); updateProcessingOptions()
    loadLoopFields(read.info); loopDraftBaseline = rawLoopDraft; loopDraftRevision = read.revision
    loopPreviewSignature = nil; loopPreviewRevision = nil; loopNeedsReload = false; savedLoopInfo = read.info
    if !settingsDraft.hasDraft, !settingsNeedsReload, !settingsFields.contains(where: { $0.currentEditor() != nil }) {
      adoptSettingsRead(read.info, revision: read.revision)
    } else if settingsRevision != read.revision {
      settingsNeedsReload = true; settingsStatus.stringValue = "Settings kept at their captured revision. Reload settings to review."
    }
    syncLoopMarkers(); updateCrossfadeInfo(read.info); updateViewportStatus()
    loopsBusy = false; loopsStatus.stringValue = "Loaded current loop settings and waveform. Other raw fields kept."
    onSamplePresentation?(); finishLoopWaiters(true)
  }
}
