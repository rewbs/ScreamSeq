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
    guard let frames=Int(crossfadeFrames.stringValue),(2...1048576).contains(frames) else {
      crossfadeStatus.stringValue="Crossfade length must be 2…1,048,576 frames.";return
    }
    let params:[String:Any]=["sample":index,"loop":crossfadeLoop.indexOfSelectedItem==1 ? "sustain" : "normal",
      "mode":crossfadeMode.indexOfSelectedItem==1 ? "overlap" : "preserve","frames":frames,
      "curve":crossfadeCurve.indexOfSelectedItem==1 ? "equal-power" : "linear"]
    let preview = !dryRun && crossfadePreviewSignature?.isEqual(params as NSDictionary) == true ? crossfadePreviewRevision : nil
    guard let operation = beginOperation("sample.crossfade", params: params, dryRun: dryRun, previewRevision: preview) else { return }
    submitOperation(operation)
  }
}
