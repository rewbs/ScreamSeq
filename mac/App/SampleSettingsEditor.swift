import AppKit

extension SampleEditor {
  var settingsFields: [NSTextField] { [name, rate, volume, pan] }
  var hasSampleComposition: Bool {
    sampleEditingFields.contains { ($0.currentEditor() as? NSTextView)?.hasMarkedText() == true }
  }
  func control(_ control: NSControl, textShouldBeginEditing fieldEditor: NSText) -> Bool {
    if drawingDraft?.numeric == true || hasNumericDrawingInput, (settingsFields + [loopStart, loopEnd, sustainStart, sustainEnd]).contains(where: { $0 === control }) {
      drawingStatus.stringValue = "Save or discard staged points before editing sample settings or loops."; return false
    }
    if control === drawingFrame || control === drawingValue {
      guard !hasOtherDrawingForm else { drawingStatus.stringValue = "Finish sample settings and loops before entering drawing fields."; return false }
      if onSettingsRequest != nil, waveformRevision == nil || settingsIdentity.isEmpty {
        drawingStatus.stringValue = "Wait for the sample waveform to load before entering drawing fields."; return false
      }
    }
    if settingsFields.contains(where: { $0 === control }), onSettingsRequest != nil {
      guard settingsRevision != nil, !settingsIdentity.isEmpty else { settingsStatus.stringValue = "Wait for the sample to load before editing."; return false }
    }
    return true
  }
  func resetSettingsContext() {
    settingsWork?.cancel(); settingsWork = nil; settingsEditGeneration += 1
    settingsPending = false; settingsNeedsReload = false; settingsRevision = nil; settingsIdentity = ""
    loopNeedsReload = false; loopEditGeneration += 1
    loopSaveWork?.cancel(); loopSaveWork = nil; loopsBusy = false
    settingsDraft.reset(); finishSettingsWaiters(false); finishLoopWaiters(false)
  }
  func scheduleSettings() {
    settingsEditGeneration += 1; settingsWork?.cancel(); settingsWork = nil
    guard onSettingsRequest != nil, !settingsNeedsReload else { return }
    let work = DispatchWorkItem { [weak self] in self?.settingsWork = nil; self?.finishAutomaticEdit { _ in } }
    settingsWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + settingsDelay, execute: work)
  }
  @objc func settingsFieldChanged() { finishAutomaticEdit { _ in } }
  func controlTextDidEndEditing(_ notification: Notification) {
    guard let field = notification.object as? NSTextField, settingsFields.contains(where: { $0 === field }) else { return }
    if onSettingsRequest != nil { finishAutomaticEdit { _ in } }
  }
  func finishSettingsWaiters(_ accepted: Bool) {
    let callbacks = settingsWaiters; settingsWaiters = []; for callback in callbacks { callback(accepted) }
  }
  func finishLoopWaiters(_ accepted: Bool) {
    let callbacks = loopCompletionWaiters; loopCompletionWaiters = []; for callback in callbacks { callback(accepted) }
  }
  func finishAutomaticEdit(_ done: @escaping (Bool) -> Void) {
    guard !hasSampleComposition else { settingsStatus.stringValue = "Finish composing the sample value before continuing."; done(false); return }
    if sampleOperation?.busy == true {
      operationWaiters.append { [weak self] accepted in if accepted { self?.finishAutomaticEdit(done) } else { done(false) } }; return
    }
    guard !protectsSampleOperation else { operationStatus.stringValue = "Review the retained operation or Reload sample before continuing."; done(false); return }
    if drawingBusy {
      drawingWaiters.append { [weak self] accepted in if accepted { self?.finishAutomaticEdit(done) } else { done(false) } }; return
    }
    guard !drawingBusy, waveform.stroke.isEmpty else { drawingStatus.stringValue = "Finish or discard the retained stroke before continuing."; done(false); return }
    guard !hasNumericDrawingInput else { drawingStatus.stringValue = "Stage and save the drawing point, or discard its fields before continuing."; done(false); return }
    if loopsBusy {
      loopCompletionWaiters.append { [weak self] accepted in if accepted { self?.finishAutomaticEdit(done) } else { done(false) } }; return
    }
    guard !loopNeedsReload else { loopsStatus.stringValue = "Reload loops to review the retained result before continuing."; done(false); return }
    finishSettings { [weak self] accepted in
      guard let self, accepted else { done(false); return }
      guard self.hasLoopDraft else { done(true); return }
      self.loopCompletionWaiters.append(done); self.loopSaveWork?.cancel(); self.loopSaveWork = nil; self.setLoops(dryRun: false)
    }
  }
  private func finishSettings(_ done: @escaping (Bool) -> Void) {
    settingsWork?.cancel(); settingsWork = nil
    guard !settingsNeedsReload else { settingsStatus.stringValue = "Fields kept. Reload settings to review the current sample before continuing."; done(false); return }
    if settingsPending { settingsWaiters.append(done); return }
    guard settingsDraft.hasDraft else { done(true); return }
    guard let onSettingsRequest, let revision = settingsRevision, !settingsIdentity.isEmpty else { done(false); return }
    let values: [String: Any]
    do { values = try changedSettings() } catch { settingsStatus.stringValue = error.localizedDescription; onMessage?(error.localizedDescription); done(false); return }
    let submitted = settingsDraft.snapshot, loops = rawLoopDraft, generation = sampleGeneration, editGeneration = settingsEditGeneration
    let sample = index, identity = settingsIdentity
    settingsWaiters.append(done); settingsPending = true; settingsStatus.stringValue = "Saving sample settings…"
    func current(_ editor: SampleEditor) -> Bool {
      editor.index == sample && editor.sampleGeneration == generation && editor.settingsIdentity == identity
    }
    func unchanged(_ editor: SampleEditor) -> Bool {
      editor.settingsEditGeneration == editGeneration && editor.settingsDraft.snapshot == submitted && editor.rawLoopDraft.isEqual(loops) && !editor.hasSampleComposition
    }
    func fail(_ editor: SampleEditor, _ message: String) {
      editor.settingsPending = false; editor.settingsNeedsReload = true
      editor.settingsStatus.stringValue = message + " · Fields kept. Reload settings to review."; editor.finishSettingsWaiters(false)
    }
    onSettingsRequest("sample.patch", ["sample": sample, "values": values, "expectedRevision": revision], identity) { [weak self] response in
      guard let self, current(self) else { return }
      guard let result = response["result"] as? [String: Any], let acceptedRevision = result["revision"] as? String else {
        fail(self, (response["error"] as? [String: Any])?["message"] as? String ?? "The sample write outcome is uncertain"); return
      }
      guard unchanged(self) else { fail(self, "Newer sample fields kept at their captured revision"); return }
      // A successful write does not authorize dropping the draft if readback
      // fails or a later revision has already replaced the accepted state.
      onSettingsRequest("sample.get", ["sample": sample], identity) { [weak self] response in
        guard let self, current(self) else { return }
        guard unchanged(self), let result = response["result"] as? [String: Any], result["revision"] as? String == acceptedRevision,
          let data = result["data"] as? [String: Any], self.validSettingsRead(data, sample: sample) else {
          fail(self, "Sample readback changed or failed"); return
        }
        self.settingsPending = false; self.settingsRevision = acceptedRevision
        self.settingsDraft.accept(values.keys, submitted: submitted)
        // Only this verified, same-sample settings edit may carry the unchanged
        // loop draft forward. Settings do not resize or rewrite the sample PCM.
        self.loopDraftRevision = acceptedRevision; self.sampleRevision = acceptedRevision
        self.updateSampleDescription(data)
        self.settingsStatus.stringValue = "Sample settings saved · ⌘Z to undo"
        self.finishSettingsWaiters(true)
      }
    }
  }
  func validSettingsRead(_ data: [String: Any], sample: Int) -> Bool {
    data["index"] as? Int == sample && data["name"] is String && data["rate"] is NSNumber && data["volume"] is NSNumber && data["pan"] is NSNumber
  }
  func adoptSettingsRead(_ data: [String: Any], revision: String) {
    name.stringValue = data["name"] as? String ?? ""; rate.stringValue = String(data["rate"] as? Int ?? 44100)
    volume.stringValue = String(data["volume"] as? Int ?? 64); pan.stringValue = String(data["pan"] as? Int ?? 128)
    settingsDraft.reset(); settingsDraft.begin(index: index)(); settingsRevision = revision
  }
  func discardSampleDrafts() {
    guard sampleOperation == nil, !settingsPending, !loopsBusy, !drawingBusy, !processing, !clipboardBusy, !crossfadeBusy, !hasSampleComposition else { return }
    // The explicit discard also releases a draft whose original sample was
    // deleted. Suppress focus-end submission before retiring local ownership.
    settingsPending = true; settingsWork?.cancel(); settingsWork = nil
    window?.makeFirstResponder(self)
    resetDocumentContext(); onSelect?(index)
    settingsStatus.stringValue = "Discarded local sample drafts."
  }
  func reloadSettings() {
    if drawingDraft?.numeric == true || hasNumericDrawingInput { drawingStatus.stringValue = "Save or discard drawing input before reloading settings."; return }
    guard !settingsPending, !loopsBusy, !hasSampleComposition, let onSettingsRequest, !settingsIdentity.isEmpty else { return }
    settingsWork?.cancel(); settingsWork = nil
    let sample = index, identity = settingsIdentity, generation = sampleGeneration, edited = settingsEditGeneration, snapshot = settingsDraft.snapshot
    settingsPending = true
    onSettingsRequest("sample.get", ["sample": sample], identity) { [weak self] response in
      guard let self, self.index == sample, self.sampleGeneration == generation else { return }
      self.settingsPending = false
      guard edited == self.settingsEditGeneration, snapshot == self.settingsDraft.snapshot, !self.hasSampleComposition,
        let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
        let data = result["data"] as? [String: Any], self.validSettingsRead(data, sample: sample) else {
        self.settingsStatus.stringValue = "Reload failed or newer text arrived. Fields kept."; self.finishSettingsWaiters(false); return
      }
      // This explicit read authorizes discarding only the settings draft.
      self.adoptSettingsRead(data, revision: revision); self.settingsNeedsReload = false
      self.settingsStatus.stringValue = "Loaded current sample settings."
      self.finishSettingsWaiters(!self.hasDraft)
    }
  }
}
