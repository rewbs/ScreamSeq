import AppKit

extension SampleEditor {
  func makeLoopRow(sustain: Bool) -> NSView {
    let first = sustain ? sustainStart : loopStart, last = sustain ? sustainEnd : loopEnd
    let enabled = sustain ? sustaining : looping, ping = sustain ? sustainPingpong : pingpong
    let reverse = sustain ? sustainReverse : loopReverse
    enabled.fixed(width: 78); ping.fixed(width: 92)
    reverse.fixed(width: 78)
    for field in [first,last] {field.delegate=self;field.target=self;field.action=#selector(loopFieldChanged)}
    for control in [enabled,ping, reverse] { control.target = self; control.action = #selector(loopModeChanged) }
    return stack(.horizontal, [enabled, ping, reverse, labeled("START FRAME", first), labeled("END FRAME", last), NSView(),
      ActionButton("Use selection") { [weak self] in
        guard let self, let range = self.waveform.selection else { return }
        first.stringValue = String(range.lowerBound); last.stringValue = String(range.upperBound); enabled.state = .on
        self.syncLoopMarkers();self.saveLoopsSoon(immediate:true)
      }], spacing: 10)
  }
  @objc func loopModeChanged(_ sender: NSButton) {
    loopEditGeneration += 1
    if sender === looping || sender === sustaining {
      let first=sender === looping ? loopStart:sustainStart,last=sender === looping ? loopEnd:sustainEnd
      if sender.state == .on && last.integerValue<=first.integerValue {first.stringValue="0";last.stringValue=String(waveform.frames)}
    }
    if sender.state == .on {
    if sender === pingpong { loopReverse.state = .off }
    else if sender === loopReverse { pingpong.state = .off }
    else if sender === sustainPingpong { sustainReverse.state = .off }
    else if sender === sustainReverse { sustainPingpong.state = .off }
    }
    syncLoopMarkers();saveLoopsSoon(immediate:true)
  }
  func makeLoopActions() -> NSView {
    loopsStatus.maximumNumberOfLines = 3; loopsStatus.lineBreakMode = .byWordWrapping
    return stack(.horizontal, [Theme.label("Drag waveform handles · changes save immediately",size:11,color:Theme.muted), NSView(),
      ActionButton("Reload loops") { [weak self] in self?.reloadLoops() }], spacing: 10)
  }
  func syncLoopMarkers() {
    waveform.loopStart=looping.state == .on ? loopStart.integerValue:0;waveform.loopEnd=looping.state == .on ? loopEnd.integerValue:0
    waveform.sustainStart=sustaining.state == .on ? sustainStart.integerValue:0;waveform.sustainEnd=sustaining.state == .on ? sustainEnd.integerValue:0
    waveform.needsDisplay=true
  }
  @objc func loopFieldChanged() {syncLoopMarkers();saveLoopsSoon(immediate:true)}
  func controlTextDidChange(_ notification:Notification) {
    if let field = notification.object as? NSTextField, [name, rate, volume, pan].contains(where: { $0 === field }) { scheduleSettings(); return }
    loopEditGeneration += 1; syncLoopMarkers();saveLoopsSoon()
  }
  func saveLoopsSoon(immediate:Bool=false) {
    // An in-flight write owns any queued retry. New gestures/Return must not
    // cancel that work while leaving loopsBusy waiting for its completion.
    guard !loopNeedsReload, !loopsBusy else { return }
    guard loopDraftBaseline?.isEqual(rawLoopDraft) != true else{return}
    if immediate {loopSaveWork?.cancel();loopSaveWork=nil;setLoops(dryRun:false);return}
    guard loopSaveWork == nil else { return }
    let sample=index,generation=sampleGeneration
    let work=DispatchWorkItem{[weak self] in guard let self,self.index==sample,self.sampleGeneration==generation else{return};self.loopSaveWork=nil;self.setLoops(dryRun:false)}
    loopSaveWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.035,execute:work)
  }

  var rawLoopDraft: NSDictionary {
    ["normal": [loopStart.stringValue, loopEnd.stringValue, String(looping.state.rawValue), String(pingpong.state.rawValue), String(loopReverse.state.rawValue)],
     "sustain": [sustainStart.stringValue, sustainEnd.stringValue, String(sustaining.state.rawValue), String(sustainPingpong.state.rawValue), String(sustainReverse.state.rawValue)]] as NSDictionary
  }
  func updateLoopSettings(_ info: [AnyHashable: Any], revision: String?) {
    savedLoopInfo = info
    if loopDraftBaseline == nil || (!loopsBusy && !loopNeedsReload && loopPreviewSignature == nil && loopDraftBaseline?.isEqual(rawLoopDraft) == true) {
      loadLoopFields(info); loopDraftRevision = revision; loopDraftBaseline = rawLoopDraft
    }
  }
  private func loadLoopFields(_ info: [AnyHashable: Any]) {
    loopStart.stringValue = String(info["loopStart"] as? Int ?? 0); loopEnd.stringValue = String(info["loopEnd"] as? Int ?? 0)
    sustainStart.stringValue = String(info["sustainStart"] as? Int ?? 0); sustainEnd.stringValue = String(info["sustainEnd"] as? Int ?? 0)
    looping.state = info["loop"] as? Bool == true ? .on : .off
    pingpong.state = info["pingpong"] as? Bool == true ? .on : .off
    sustaining.state = info["sustainLoop"] as? Bool == true ? .on : .off
    sustainPingpong.state = info["sustainPingpong"] as? Bool == true ? .on : .off
    loopReverse.state = info["reverseLoop"] as? Bool == true ? .on : .off
    sustainReverse.state = info["sustainReverse"] as? Bool == true ? .on : .off
  }
  func reloadLoops() {
    guard !loopsBusy, !settingsPending, !hasSampleComposition, let onRequest else { return }
    loopSaveWork?.cancel();loopSaveWork=nil
    let sample = index, generation = sampleGeneration, signature = rawLoopDraft, edited = loopEditGeneration
    let settings = settingsDraft.snapshot, settingsGeneration = settingsEditGeneration
    loopsBusy = true
    let receive: ([String: Any]) -> Void = { [weak self] response in
      guard let self, self.index == sample, self.sampleGeneration == generation else { return }
      self.loopsBusy = false
      guard !self.hasSampleComposition, edited == self.loopEditGeneration, self.rawLoopDraft.isEqual(signature),
        let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
        let info = result["data"] as? [String: Any], info["index"] as? Int == sample,
        ["frames", "loopStart", "loopEnd", "sustainStart", "sustainEnd", "loop", "pingpong", "sustainLoop", "sustainPingpong", "reverseLoop", "sustainReverse"].allSatisfy({ info[$0] is NSNumber }) else {
        self.loopNeedsReload = true; self.loopsStatus.stringValue = "Loop readback failed or newer fields arrived. Draft kept."; self.finishLoopWaiters(false); return
      }
      self.loadLoopFields(info); self.loopDraftBaseline = self.rawLoopDraft; self.loopDraftRevision = revision
      self.loopPreviewSignature = nil; self.loopPreviewRevision = nil; self.loopNeedsReload = false
      self.savedLoopInfo = info; self.sampleRevision = revision
      if !self.settingsDraft.hasDraft, !self.settingsNeedsReload, self.settingsEditGeneration == settingsGeneration,
        self.settingsDraft.snapshot == settings, !self.settingsFields.contains(where: { $0.currentEditor() != nil }), self.validSettingsRead(info, sample: sample) {
        self.adoptSettingsRead(info, revision: revision)
      }
      self.syncLoopMarkers(); self.loopsStatus.stringValue = "Loaded current loop settings."; self.finishLoopWaiters(true)
    }
    if let onSettingsRequest { onSettingsRequest("sample.get", ["sample": sample], settingsIdentity, receive) }
    else { onRequest("sample.get", ["sample": sample], receive) }
  }
  func setLoops(dryRun: Bool, attempt: Int = 0) {
    guard !loopsBusy else { return }
    guard !loopNeedsReload, !drawingBusy, waveform.stroke.isEmpty, !hasSampleComposition, onRequest != nil, let revision = loopDraftRevision else { finishLoopWaiters(false); return }
    if settingsPending || settingsDraft.hasDraft || settingsNeedsReload {
      finishAutomaticEdit { [weak self] accepted in if !accepted { self?.finishLoopWaiters(false) } }; return
    }
    var values: [String: Any] = [:]
    for (key, first, last, enabled, ping, reverse) in [("normal", loopStart, loopEnd, looping, pingpong, loopReverse),
      ("sustain", sustainStart, sustainEnd, sustaining, sustainPingpong, sustainReverse)] {
      guard let start = Int(first.stringValue), let end = Int(last.stringValue), start >= 0,
        start <= end, end <= waveform.frames, enabled.state != .on || start < end else {
        loopsStatus.stringValue = "Enter loop boundaries inside the sample; enabled loops must be nonempty."; finishLoopWaiters(false); return
      }
      guard enabled.state != .on || ping.state != .on || reverse.state != .on else {
        loopsStatus.stringValue = "Choose either reverse or ping-pong for each loop."; finishLoopWaiters(false); return
      }
      values[key] = ["start": start, "end": end, "enabled": enabled.state == .on,
        "pingpong": enabled.state == .on && ping.state == .on, "reverse": enabled.state == .on && reverse.state == .on]
    }
    let signature = rawLoopDraft, sample = index, generation = sampleGeneration
    var params = values; params["sample"] = sample; params["dryRun"] = dryRun
    params["expectedRevision"] = !dryRun && loopPreviewSignature?.isEqual(signature) == true ? loopPreviewRevision ?? revision : revision
    sendLoops(params, signature: signature, settings: settingsDraft.snapshot, edited: loopEditGeneration, sample: sample, generation: generation, dryRun: dryRun, attempt: attempt)
  }
  private func sendLoops(_ params: [String: Any], signature: NSDictionary, settings: [String: String], edited: Int, sample: Int, generation: Int, dryRun: Bool, attempt: Int) {
    guard let onRequest else { finishLoopWaiters(false); return }
    loopsBusy = true
    loopsStatus.stringValue = dryRun ? "Checking loop settings…" : "Applying loop settings…"
    onRequest("sample.loops.set", params) { [weak self] response in
      guard let self, sample == self.index, generation == self.sampleGeneration else { return }
      self.loopsBusy = false
      guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any],
        let updatedRevision = result["revision"] as? String else {
        self.loopPreviewSignature = nil; self.loopPreviewRevision = nil
        let error=response["error"] as? [String:Any]
        if NativeRequestOutcome.canRetryWrite(response) && !dryRun && attempt < 5 {
          self.loopsStatus.stringValue="Waiting to save loop changes…"
          self.loopSaveWork?.cancel()
          self.loopsBusy = true
          let work=DispatchWorkItem{[weak self] in
            guard let self,self.index==sample,self.sampleGeneration==generation else{return}
            self.loopSaveWork=nil;self.sendLoops(params, signature: signature, settings: settings, edited: edited, sample: sample, generation: generation, dryRun: false, attempt: attempt+1)
          }
          self.loopSaveWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.035,execute:work)
        } else {
          self.loopSaveWork?.cancel();self.loopSaveWork=nil
          self.loopNeedsReload = true; self.finishLoopWaiters(false)
          self.loopsStatus.stringValue = (error?["message"] as? String ?? "Loop edit failed.") + " Reload loops to use the latest settings."
        }
        return
      }
      if dryRun {
        guard self.rawLoopDraft.isEqual(signature) else {
          self.loopPreviewSignature = nil; self.loopPreviewRevision = nil
          self.loopsStatus.stringValue = "Loop settings changed; preview discarded."; return
        }
        self.loopPreviewSignature = signature; self.loopPreviewRevision = updatedRevision
        self.loopsStatus.stringValue = data["loopsChanged"] as? Bool == true
          ? "Preview: both loop settings will be saved together in one Undo step. Audio is unchanged."
          : "Preview: loop settings are already saved."
      } else {
        guard self.loopEditGeneration == edited, self.settingsDraft.snapshot == settings, !self.hasSampleComposition else {
          self.loopNeedsReload = true; self.settingsNeedsReload = self.settingsNeedsReload || self.settingsDraft.hasDraft
          self.loopsStatus.stringValue = "Newer sample fields kept at their captured revision. Reload loops to review the result."
          self.finishLoopWaiters(false); return
        }
        self.loopPreviewSignature = nil; self.loopPreviewRevision = nil
        self.loopDraftBaseline = signature; self.loopDraftRevision = updatedRevision
        if self.settingsDraft.snapshot == settings && !self.settingsNeedsReload {
          self.settingsRevision = updatedRevision
        } else {
          self.settingsNeedsReload = true; self.settingsStatus.stringValue = "Newer settings kept at their captured revision. Reload settings before continuing."
          self.finishLoopWaiters(false)
        }
        self.loopsStatus.stringValue = data["loopsChanged"] as? Bool == true ? "Loop settings saved in one Undo step." : "Loop settings are already saved."
        self.syncLoopMarkers()
        if !self.rawLoopDraft.isEqual(signature) {self.saveLoopsSoon()}
        else { self.finishLoopWaiters(true) }
      }
    }
  }
}
