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
        guard self.drawingDraft?.numeric != true, !self.hasNumericDrawingInput else { self.drawingStatus.stringValue = "Save or discard drawing input before changing loops."; return }
        first.stringValue = String(range.lowerBound); last.stringValue = String(range.upperBound); enabled.state = .on
        self.loopEditGeneration += 1; self.syncLoopMarkers();self.saveLoopsSoon(immediate:true)
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
    return stack(.horizontal, [loopPreviewButton, loopApplyButton,
      ActionButton("Reload loops") { [weak self] in self?.reloadLoops() }, NSView()], spacing: 10)
  }
  func syncLoopMarkers() {
    waveform.loopStart=looping.state == .on ? loopStart.integerValue:0;waveform.loopEnd=looping.state == .on ? loopEnd.integerValue:0
    waveform.sustainStart=sustaining.state == .on ? sustainStart.integerValue:0;waveform.sustainEnd=sustaining.state == .on ? sustainEnd.integerValue:0
    waveform.needsDisplay=true
  }
  @objc func loopFieldChanged() {loopEditGeneration += 1;syncLoopMarkers();saveLoopsSoon(immediate:true)}
  func controlTextDidChange(_ notification:Notification) {
    if let field = notification.object as? NSTextField, field === drawingFrame || field === drawingValue { numericDrawingInputChanged(); return }
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
  func loadLoopFields(_ info: [AnyHashable: Any]) {
    loopStart.stringValue = String(info["loopStart"] as? Int ?? 0); loopEnd.stringValue = String(info["loopEnd"] as? Int ?? 0)
    sustainStart.stringValue = String(info["sustainStart"] as? Int ?? 0); sustainEnd.stringValue = String(info["sustainEnd"] as? Int ?? 0)
    looping.state = info["loop"] as? Bool == true ? .on : .off
    pingpong.state = info["pingpong"] as? Bool == true ? .on : .off
    sustaining.state = info["sustainLoop"] as? Bool == true ? .on : .off
    sustainPingpong.state = info["sustainPingpong"] as? Bool == true ? .on : .off
    loopReverse.state = info["reverseLoop"] as? Bool == true ? .on : .off
    sustainReverse.state = info["sustainReverse"] as? Bool == true ? .on : .off
  }
  func setLoops(dryRun: Bool) {
    guard !loopsBusy else { return }
    loopSaveWork?.cancel(); loopSaveWork = nil
    guard !protectsSampleOperation else { operationStatus.stringValue = "Review the retained sample operation before changing loops."; finishLoopWaiters(false); return }
    guard !protectsDrawingInput else { drawingStatus.stringValue = "Save or discard drawing input before changing loops."; finishLoopWaiters(false); return }
    guard !loopNeedsReload, !drawingBusy, !processing, !crossfadeBusy, !clipboardBusy, !snapBusy, !waveformInFlight, !sampleSelectionInFlight,
      waveformQuery == nil, sampleSelectionQuery == nil, waveform.stroke.isEmpty, !hasSampleComposition,
      onSettingsRequest != nil || onRequest != nil, let revision = loopDraftRevision else { finishLoopWaiters(false); return }
    if settingsPending || settingsDraft.hasDraft || settingsNeedsReload {
      if dryRun { loopsStatus.stringValue = "Finish sample settings before previewing loops."; finishLoopWaiters(false); return }
      finishAutomaticEdit { [weak self] accepted in if !accepted { self?.finishLoopWaiters(false) } }; return
    }
    guard !settingsIdentity.isEmpty, catalogue.contains(where: { $0.index == index && $0.identity == settingsIdentity }),
      (onSampleRevision?() ?? sampleRevision) == revision, waveformDocumentID?.isEmpty == false else {
      loopNeedsReload = true; loopsStatus.stringValue = "The captured sample changed. Reload loops before continuing."; finishLoopWaiters(false); return
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
    let signature = rawLoopDraft
    var params = values; params["sample"] = index; params["dryRun"] = dryRun
    params["expectedRevision"] = !dryRun && loopPreviewSignature?.isEqual(signature) == true ? loopPreviewRevision ?? revision : revision
    let write = SampleLoopWrite(self, params: params, signature: signature, dryRun: dryRun)
    loopWrite = write; loopsBusy = true; sendLoops(write)
  }
}
