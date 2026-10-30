import AppKit

// Retain one immutable submission through its receipt and both read stages.
// Only native handle movement may extend a gesture at the accepted revision.
final class SampleLoopWrite {
  let query: SampleSelectionQuery, params: [String: Any], signature: NSDictionary, dryRun: Bool
  var gestureSignature: NSDictionary
  init(_ editor: SampleEditor, params: [String: Any], signature: NSDictionary, dryRun: Bool) {
    query = SampleSelectionQuery(editor, sample: editor.index, identity: editor.settingsIdentity, kind: .reload, selectInput: false)
    self.params = params; self.signature = signature; self.dryRun = dryRun; gestureSignature = signature
  }
}

extension SampleEditor {
  func ownsLoopWrite(_ write: SampleLoopWrite) -> Bool {
    loopWrite === write && index == write.query.sample && sampleGeneration == write.query.generation
  }
  func loopWriteUnchanged(_ write: SampleLoopWrite) -> Bool {
    let query = write.query
    // Loop boundary text can differ only when onLoopMarker recorded that exact
    // continuation. A typed field, checkbox or Use selection is a new edit.
    func otherFields(_ fields: [String: String]) -> [String: String] {
      fields.filter { !["field4", "field5", "field6", "field7"].contains($0.key) }
    }
    return settingsIdentity == query.identity && waveformDocumentID == query.documentID &&
      catalogue.contains(where: { $0.index == query.sample && $0.identity == query.identity }) &&
      loopEditGeneration == query.loopGeneration && settingsEditGeneration == query.settingsGeneration &&
      rawLoopDraft.isEqual(write.gestureSignature) && otherFields(drawingFields) == otherFields(query.fields) &&
      sampleSelectionOptions == query.options && SampleViewRegion(self) == query.originalRegion &&
      waveformChannels == query.channels && waveform.stroke == query.stroke && !hasSampleComposition
  }
  func failLoopWrite(_ write: SampleLoopWrite, _ message: String) {
    guard ownsLoopWrite(write) else { return }
    loopWrite = nil; loopsBusy = false; loopSaveWork?.cancel(); loopSaveWork = nil
    loopPreviewSignature = nil; loopPreviewRevision = nil
    if !write.dryRun {
      loopNeedsReload = true; settingsNeedsReload = settingsNeedsReload || settingsDraft.hasDraft
    }
    loopsStatus.stringValue = message + (write.dryRun ? " · Preview discarded; fields kept." : " · Fields kept. Reload loops to review the result.")
    finishLoopWaiters(false)
  }
  func requestLoopWrite(_ write: SampleLoopWrite, _ method: String, _ params: [String: Any], _ reply: @escaping ([String: Any]) -> Void) {
    guard ownsLoopWrite(write) else { return }
    if let onSettingsRequest { onSettingsRequest(method, params, write.query.identity, reply) }
    else if let onRequest { onRequest(method, params, reply) }
    else { failLoopWrite(write, "Sample requests are unavailable") }
  }
  func sendLoops(_ write: SampleLoopWrite, attempt: Int = 0) {
    guard ownsLoopWrite(write) else { return }
    let query = write.query
    guard (onSampleRevision?() ?? sampleRevision) == query.revision, waveformDocumentID == query.documentID,
      catalogue.contains(where: { $0.index == query.sample && $0.identity == query.identity }) else {
      failLoopWrite(write, "The captured sample changed before the loop write"); return
    }
    loopsStatus.stringValue = write.dryRun ? "Checking loop settings…" : "Applying loop settings…"
    requestLoopWrite(write, "sample.loops.set", write.params) { [weak self] response in
      guard let self, self.ownsLoopWrite(write) else { return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String, !revision.isEmpty,
        result["documentId"] as? String == query.documentID, let data = result["data"] as? [String: Any],
        data["sample"] as? Int == query.sample, data["dryRun"] as? Bool == write.dryRun, data["loopsChanged"] is Bool,
        let after = data["after"] as? [String: Any], self.loopReportMatches(after, write) else {
        if !write.dryRun, NativeRequestOutcome.canRetryWrite(response), attempt < 5 {
          self.loopsStatus.stringValue = "Waiting to save loop changes…"
          let work = DispatchWorkItem { [weak self] in
            guard let self, self.ownsLoopWrite(write) else { return }
            self.loopSaveWork = nil; self.sendLoops(write, attempt: attempt + 1)
          }
          self.loopSaveWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.035, execute: work)
        } else {
          self.failLoopWrite(write, (response["error"] as? [String: Any])?["message"] as? String ?? "Loop result was incomplete or belonged to another sample")
        }
        return
      }
      guard self.loopWriteUnchanged(write), (self.onSampleRevision?() ?? revision) == revision else {
        self.failLoopWrite(write, "Newer sample fields or revision kept"); return
      }
      if write.dryRun {
        guard revision == query.revision else { self.failLoopWrite(write, "Preview revision changed"); return }
        self.loopWrite = nil; self.loopsBusy = false
        self.loopPreviewSignature = write.signature; self.loopPreviewRevision = revision
        self.loopsStatus.stringValue = data["loopsChanged"] as? Bool == true
          ? "Preview: both loops will be saved together in one Undo step. Audio and history are unchanged."
          : "Preview: both loops are already saved."
        // A waiting Save must continue into the real edit if a draft remains.
        self.finishLoopWaiters(true)
      } else {
        self.readLoopWrite(write, revision: revision, changed: data["loopsChanged"] as? Bool == true)
      }
    }
  }
  func loopReportMatches(_ after: [String: Any], _ write: SampleLoopWrite) -> Bool {
    ["normal", "sustain"].allSatisfy { key in
      guard let row = after[key] as? NSDictionary, let submitted = write.params[key] as? [String: Any] else { return false }
      return row.isEqual(to: submitted)
    }
  }
  func loopInfoMatches(_ info: [String: Any], _ write: SampleLoopWrite) -> Bool {
    guard validLoopReloadInfo(info, query: write.query), info["frames"] as? Int == write.query.region.frames else { return false }
    let rows: [String: Any] = [
      "normal": ["start": info["loopStart"]!, "end": info["loopEnd"]!, "enabled": info["loop"]!,
        "pingpong": info["pingpong"]!, "reverse": info["reverseLoop"]!],
      "sustain": ["start": info["sustainStart"]!, "end": info["sustainEnd"]!, "enabled": info["sustainLoop"]!,
        "pingpong": info["sustainPingpong"]!, "reverse": info["sustainReverse"]!]]
    return loopReportMatches(rows, write)
  }
  func continueLoopWrite(_ write: SampleLoopWrite, revision: String) -> Bool {
    guard ownsLoopWrite(write) else { return false }
    guard loopWriteUnchanged(write), (onSampleRevision?() ?? revision) == revision else {
      failLoopWrite(write, "Sample or fields changed during loop readback"); return false
    }
    return true
  }
  func readLoopWrite(_ write: SampleLoopWrite, revision: String, changed: Bool) {
    let query = write.query
    loopsStatus.stringValue = "Checking saved loops and waveform…"
    func decode(_ response: [String: Any]) -> [String: Any]? {
      guard let result = response["result"] as? [String: Any], result["revision"] as? String == revision,
        result["documentId"] as? String == query.documentID else { return nil }
      return result
    }
    requestLoopWrite(write, "sample.get", ["sample": query.sample]) { [weak self] response in
      guard let self, self.continueLoopWrite(write, revision: revision) else { return }
      guard let result = decode(response), let info = result["data"] as? [String: Any], self.loopInfoMatches(info, write),
        query.channels != "right" || info["channels"] as? Int == 2 else {
        self.failLoopWrite(write, "Saved loop metadata changed or was incomplete"); return
      }
      let range = query.region.range
      func adopt(_ peaks: [Float]) { self.adoptLoopWrite(write, info: info, peaks: peaks, revision: revision, changed: changed) }
      if range.isEmpty { adopt([]); return }
      let bins = range.count <= 4096 ? range.count : 2048
      self.requestLoopWrite(write, "sample.waveform.get", ["sample": query.sample, "start": range.lowerBound, "end": range.upperBound, "channels": query.channels, "bins": bins]) { [weak self] response in
        guard let self, self.continueLoopWrite(write, revision: revision) else { return }
        guard let result = decode(response), let peaks = self.validWaveformPeaks(result, sample: query.sample, range: range, channels: query.channels, bins: bins) else {
          self.failLoopWrite(write, "Saved loop waveform changed or was incomplete"); return
        }
        adopt(peaks)
      }
    }
  }
  func adoptLoopWrite(_ write: SampleLoopWrite, info: [String: Any], peaks: [Float], revision: String, changed: Bool) {
    guard continueLoopWrite(write, revision: revision) else { return }
    waveform.peaksRange = write.query.region.range; waveform.peaks = peaks
    waveformRevision = revision; sampleRevision = revision; settingsRevision = revision
    savedLoopInfo = info; loopDraftBaseline = write.signature; loopDraftRevision = revision
    loopPreviewSignature = nil; loopPreviewRevision = nil
    // No text fields or responders are reassigned here. Tab navigation and
    // caret movement can survive completion without invalidating a saved edit.
    syncLoopMarkers(); updateCrossfadeInfo(info)
    loopWrite = nil; loopsBusy = false
    loopsStatus.stringValue = changed ? "Both loops saved in one Undo step." : "Both loops are already saved."
    onSamplePresentation?()
    if hasLoopDraft { saveLoopsSoon() } else { finishLoopWaiters(true) }
  }
}
