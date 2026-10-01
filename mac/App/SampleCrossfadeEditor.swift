import AppKit

extension SampleEditor {
  func makeCrossfadeControls() -> NSView {
    crossfadeLoop.addItems(withTitles:["Normal","Sustain"]);crossfadeLoop.fixed(width:100)
    crossfadeLoop.setAccessibilityLabel("Crossfade loop target")
    crossfadeMode.addItems(withTitles:["Preserve duration","Overlap"]);crossfadeMode.fixed(width:165)
    crossfadeMode.setAccessibilityLabel("Loop crossfade mode")
    crossfadeCurve.addItems(withTitles:["Linear","Equal power"]);crossfadeCurve.fixed(width:130)
    crossfadeCurve.setAccessibilityLabel("Loop crossfade curve")
    for label in [crossfadeInfo,crossfadeStatus] { label.lineBreakMode = .byWordWrapping;label.maximumNumberOfLines=3 }
    return stack(.horizontal,[labeled("CROSSFADE LOOP",crossfadeLoop),labeled("MODE",crossfadeMode),
      labeled("CURVE",crossfadeCurve),labeled("FADE (frames)",crossfadeFrames),NSView()],spacing:14)
  }
  func makeCrossfadeActions() -> NSView {
    stack(.horizontal,[crossfadePreviewButton,crossfadeApplyButton,NSView()],spacing:10)
  }
  func updateCrossfadeInfo(_ info:[AnyHashable:Any]) {
    func loop(_ enabled:String,_ start:String,_ end:String,_ pingpong:String,_ reverse:String) -> String {
      guard info[enabled] as? Bool == true else { return "off" }
      return "\(info[start] ?? 0) → \(info[end] ?? 0)" + (info[reverse] as? Bool == true ? " (reverse)" : info[pingpong] as? Bool == true ? " (ping-pong)" : "")
    }
    crossfadeInfo.stringValue="Saved loops · normal: \(loop("loop","loopStart","loopEnd","pingpong","reverseLoop")) · sustain: \(loop("sustainLoop","sustainStart","sustainEnd","sustainPingpong","sustainReverse"))\nApply pending loop settings before crossfading. Overlap shortens the loop period."
  }
  func crossfade(dryRun:Bool) {
    guard !crossfadeBusy,let onRequest,let revision=sampleRevision else { return }
    guard let frames=Int(crossfadeFrames.stringValue),(2...1048576).contains(frames) else {
      crossfadeStatus.stringValue="Crossfade length must be 2…1,048,576 frames.";return
    }
    var params:[String:Any]=["sample":index,"loop":crossfadeLoop.indexOfSelectedItem==1 ? "sustain" : "normal",
      "mode":crossfadeMode.indexOfSelectedItem==1 ? "overlap" : "preserve","frames":frames,
      "curve":crossfadeCurve.indexOfSelectedItem==1 ? "equal-power" : "linear"]
    let signature=params as NSDictionary,sample=index,generation=sampleGeneration
    params["expectedRevision"] = !dryRun && crossfadePreviewSignature?.isEqual(signature)==true
      ? crossfadePreviewRevision ?? revision : revision
    params["dryRun"]=dryRun
    crossfadeBusy=true;crossfadePreviewButton.isEnabled=false;crossfadeApplyButton.isEnabled=false
    crossfadeStatus.stringValue=dryRun ? "Calculating crossfade preview…" : "Applying crossfade…"
    onRequest("sample.crossfade",params) { [weak self] response in
      guard let self else { return };self.crossfadeBusy=false
      self.crossfadePreviewButton.isEnabled=true;self.crossfadeApplyButton.isEnabled=true
      guard sample==self.index,generation==self.sampleGeneration else { return }
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],
        let before=data["loopBefore"] as? [String:Any],let after=data["loopAfter"] as? [String:Any] else {
        self.crossfadePreviewSignature=nil;self.crossfadePreviewRevision=nil
        self.crossfadeStatus.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Crossfade failed.";return
      }
      if dryRun && (Int(self.crossfadeFrames.stringValue) != frames ||
        (self.crossfadeLoop.indexOfSelectedItem==1 ? "sustain" : "normal") != signature["loop"] as? String ||
        (self.crossfadeMode.indexOfSelectedItem==1 ? "overlap" : "preserve") != signature["mode"] as? String ||
        (self.crossfadeCurve.indexOfSelectedItem==1 ? "equal-power" : "linear") != signature["curve"] as? String) {
        self.crossfadePreviewSignature=nil;self.crossfadePreviewRevision=nil
        self.crossfadeStatus.stringValue="Crossfade settings changed; preview discarded.";return
      }
      let prefix=dryRun ? "Preview" : "Applied",clipped=data["clippedSamples"] as? Int ?? 0
      self.crossfadeStatus.stringValue="\(prefix): loop \(before["frames"] ?? 0) → \(after["frames"] ?? 0) frames; \(data["changedFrames"] ?? 0) audio frames change."
        + (clipped>0 ? " \(clipped) clipped values." : "")
      self.crossfadePreviewSignature=dryRun ? signature : nil
      self.crossfadePreviewRevision=dryRun ? result["revision"] as? String : nil
    }
  }
}
