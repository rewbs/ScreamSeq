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
  func controlTextDidChange(_ notification:Notification) {syncLoopMarkers();saveLoopsSoon()}
  func saveLoopsSoon(immediate:Bool=false) {
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
    if loopDraftBaseline == nil || (!loopsBusy && loopPreviewSignature == nil && loopDraftBaseline?.isEqual(rawLoopDraft) == true) {
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
    guard !loopsBusy else { return }
    loopSaveWork?.cancel();loopSaveWork=nil
    loadLoopFields(savedLoopInfo); loopDraftBaseline = rawLoopDraft; loopDraftRevision = sampleRevision
    loopPreviewSignature = nil; loopPreviewRevision = nil
    syncLoopMarkers();loopsStatus.stringValue = "Loaded saved loop settings."
  }
  func setLoops(dryRun: Bool) {
    guard !loopsBusy, !drawingBusy, let onRequest, let revision = loopDraftRevision else { return }
    var values: [String: Any] = [:]
    for (key, first, last, enabled, ping, reverse) in [("normal", loopStart, loopEnd, looping, pingpong, loopReverse),
      ("sustain", sustainStart, sustainEnd, sustaining, sustainPingpong, sustainReverse)] {
      guard let start = Int(first.stringValue), let end = Int(last.stringValue), start >= 0,
        start <= end, end <= waveform.frames, enabled.state != .on || start < end else {
        loopsStatus.stringValue = "Enter loop boundaries inside the sample; enabled loops must be nonempty."; return
      }
      guard enabled.state != .on || ping.state != .on || reverse.state != .on else {
        loopsStatus.stringValue = "Choose either reverse or ping-pong for each loop."; return
      }
      values[key] = ["start": start, "end": end, "enabled": enabled.state == .on,
        "pingpong": enabled.state == .on && ping.state == .on, "reverse": enabled.state == .on && reverse.state == .on]
    }
    let signature = rawLoopDraft, sample = index, generation = sampleGeneration
    var params = values; params["sample"] = sample; params["dryRun"] = dryRun
    params["expectedRevision"] = !dryRun && loopPreviewSignature?.isEqual(signature) == true ? loopPreviewRevision ?? revision : revision
    loopsBusy = true
    loopsStatus.stringValue = dryRun ? "Checking loop settings…" : "Applying loop settings…"
    onRequest("sample.loops.set", params) { [weak self] response in
      guard let self else { return }
      self.loopsBusy = false
      guard sample == self.index, generation == self.sampleGeneration else { self.saveLoopsSoon();return }
      guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any],
        let updatedRevision = result["revision"] as? String else {
        self.loopPreviewSignature = nil; self.loopPreviewRevision = nil
        let error=response["error"] as? [String:Any]
        if error?["code"] as? Int == -32002 && !dryRun {
          self.loopsStatus.stringValue="Waiting to save loop changes…";self.saveLoopsSoon()
        } else {
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
        self.loopPreviewSignature = nil; self.loopPreviewRevision = nil
        self.loopDraftBaseline = signature; self.loopDraftRevision = updatedRevision
        self.loopsStatus.stringValue = data["loopsChanged"] as? Bool == true ? "Loop settings saved in one Undo step." : "Loop settings are already saved."
        self.syncLoopMarkers()
        if !self.rawLoopDraft.isEqual(signature) {self.saveLoopsSoon()}
      }
    }
  }
}
