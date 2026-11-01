import AppKit

extension PatternSampleRenderView {
  func apply(dryRun: Bool) {
    guard !hasUnresolvedResult, let onRequest else { return }
    guard otherHasUnresolvedResult?() != true else { showStatus("Review the other retained selection render before rendering again."); return }
    syncOptions()
    guard current, let target else { showStatus("Song changed. Options kept; choose Use current selection before rendering."); return }
    guard !composing else { showStatus("Finish composing the render options before checking or rendering."); return }
    let options = raw
    guard let seconds = Double(options.tail.trimmingCharacters(in: .whitespacesAndNewlines)), seconds.isFinite, (0...60).contains(seconds) else {
      showStatus("Tail must be between 0 and 60 seconds."); return
    }
    var params: [String: Any] = target.range
    params.merge(["name": options.name, "tailSeconds": seconds, "createInstrument": options.instrument,
      "dryRun": dryRun, "expectedRevision": target.context.revision]) { _, new in new }
    let owner = PatternSampleOperation(target: target, options: options, generation: generation, params: params)
    if !dryRun { operation = owner }
    pending = true; updateControls()
    showStatus(dryRun ? "Checking captured selection…" : "Rendering captured rows and channels with their effects…")
    onRequest("sample.renderSelection", params) { [weak self] response in
      guard let self, dryRun || self.operation === owner else { return }
      self.syncOptions(); self.pending = false
      if dryRun {
        if let data = SampleRecordingRead.result(response, context: owner.target.context), self.matchesReceipt(data, owner: owner, dryRun: true),
          self.onTarget?()?.context == owner.target.context, self.generation == owner.generation, self.raw == owner.options, !self.composing {
          self.showStatus("Selection validated. Render adds a new sample with one Undo.")
        } else {
          self.showStatus((response["error"] as? [String: Any])?["message"] as? String ?? "Selection or options changed, or the check could not be verified. Check again before rendering.")
        }
        self.updateControls(); return
      }
      if let error = response["error"] as? [String: Any],
        (error["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted" {
        self.operation = nil
        self.showStatus(error["message"] as? String ?? "The render was not committed. Selection and options kept.")
        self.updateControls(); return
      }
      guard response["error"] == nil, let result = response["result"] as? [String: Any],
        result["documentId"] as? String == owner.target.context.document,
        let revision = result["revision"] as? String, !revision.isEmpty,
        let data = result["data"] as? [String: Any], self.matchesReceipt(data, owner: owner, dryRun: false) else {
        self.retain(owner, "The render outcome could not be verified. Review the current samples before another render."); return
      }
      let sample = SampleRecordingRead.integer(data["sample"], maximum: 65535, minimum: 1)!
      let instrument = SampleRecordingRead.integer(data["instrument"], maximum: 65535)!
      let context = SampleRecordingContext(document: owner.target.context.document, revision: revision)
      var message = "Created sample \(sample)" + (instrument > 0 ? " + mapped instrument \(instrument)" : "") + " · ⌘Z undoes it."
      // Numeric slots are meaningful only at this receipt's document/revision.
      if self.onTarget?()?.context == context { self.onAdded?(sample, instrument) }
      else { message += " Song changed since completion; selection kept." }
      if self.generation == owner.generation, self.raw == owner.options, !self.composing { self.baseline = owner.options }
      self.operation = nil; self.needsCapture = true; self.showStatus(message); self.updateControls()
    }
  }
  func matchesReceipt(_ data: [String: Any], owner: PatternSampleOperation, dryRun: Bool) -> Bool {
    guard SampleRecordingRead.boolean(data["dryRun"]) == dryRun,
      SampleRecordingRead.integer(data["sample"], maximum: 65535, minimum: 1) != nil,
      let instrument = SampleRecordingRead.integer(data["instrument"], maximum: 65535),
      (instrument > 0) == owner.options.instrument, data["name"] as? String == owner.options.name,
      SampleRecordingRead.boolean(data["tailSeconds"]) == nil,
      let seconds = data["tailSeconds"] as? Double, seconds.isFinite, seconds == owner.params["tailSeconds"] as? Double,
      let frames = SampleRecordingRead.integer(data["frames"]), dryRun ? frames == 0 : frames > 0 else { return false }
    for (key, value) in owner.target.range {
      if SampleRecordingRead.integer(data[key], maximum: 65535) != value { return false }
    }
    return true
  }
  func retain(_ owner: PatternSampleOperation, _ message: String) {
    guard operation === owner else { return }
    pending = false; showStatus(message); updateControls(); onReviewNeeded?()
  }
  func reviewResult() {
    guard !pending, let owner = operation, let onRequest else { return }
    syncOptions(); owner.observation = nil
    guard !composing, let context = onTarget?()?.context, context.document == owner.target.context.document else {
      showStatus("Return to the original song and finish composing options before reviewing this render."); updateControls(); return
    }
    let reviewedGeneration = generation, reviewedOptions = raw
    pending = true; updateControls()
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner else { return }
      self.syncOptions(); self.pending = false
      guard self.onTarget?()?.context == context, self.generation == reviewedGeneration, self.raw == reviewedOptions, !self.composing,
        let data = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(data) else {
        self.retain(owner, "Could not verify the current sample and instrument lists. The earlier render is retained; Review again."); return
      }
      owner.observation = .init(context: context, generation: reviewedGeneration, options: reviewedOptions, assets: data)
      let samples = (data["samples"] as? [[String: Any]])?.count ?? 0, instruments = (data["instruments"] as? [[String: Any]])?.count ?? 0
      self.showStatus("Inspected \(samples) samples and \(instruments) instruments. The earlier render is unverified. Accept observed state acknowledges this without rendering again or changing selection.")
      self.updateControls()
    }
  }
  func acceptObservation() {
    guard !pending, let owner = operation, let observation = owner.observation else { return }
    syncOptions()
    guard onTarget?()?.context == observation.context, generation == observation.generation, raw == observation.options, !composing else {
      owner.observation = nil; showStatus("Song or options changed. Review the current state again before accepting."); updateControls(); return
    }
    operation = nil; needsCapture = true
    showStatus(direct ? "Unverified result acknowledged. Choose Record selection again to render a new selection." :
      "Unverified result acknowledged. Options kept; Use current selection before a new render.")
    updateControls()
  }
}
