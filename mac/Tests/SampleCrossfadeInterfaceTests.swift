import AppKit
extension InterfaceTests {
  static func sampleCrossfadeChecks() throws {
    let editor=SampleEditor(frame:NSRect(x:0,y:0,width:729,height:1280))
    let info:[String:Any]=["frames":2048,"channels":2,"loop":true,"loopStart":0,"loopEnd":1024,
      "sustainLoop":true,"sustainStart":512,"sustainEnd":1800]
    let inventory:[[String:Any]]=[["index":1,"name":"Crossfade"]]
    editor.update(info,samples:inventory,revision:"preview-source")
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    editor.crossfadeMode.selectItem(at:1);editor.crossfadeCurve.selectItem(at:1);editor.crossfadeFrames.stringValue="64"
    func answer(_ revision:String) {
      replies.removeFirst()(["result":["revision":revision,"data":["loopBefore":["frames":1024],"loopAfter":["frames":960],"changedFrames":62,"clippedSamples":4]]])
    }
    editor.crossfade(dryRun:true)
    try require(calls.count==1 && calls[0].0=="sample.crossfade" && calls[0].1["mode"] as? String=="overlap" &&
      calls[0].1["curve"] as? String=="equal-power" && calls[0].1["frames"] as? Int==64 && calls[0].1["expectedRevision"] as? String=="preview-source",
      "Native crossfade exposes exact modes/curves/length through the shared revision-checked API")
    editor.crossfade(dryRun:false);try require(calls.count==1 && !editor.crossfadeApplyButton.isEnabled,"Pending crossfade cannot submit twice")
    answer("preview-source")
    try require(editor.crossfadeStatus.stringValue.contains("1024 → 960") && editor.crossfadeStatus.stringValue.contains("4 clipped"),"Preview makes shortening and clipping explicit")
    editor.update(info,samples:inventory,revision:"song-changed")
    editor.crossfade(dryRun:false)
    try require(calls.last?.1["expectedRevision"] as? String=="preview-source","Applying a reviewed crossfade never rebases its revision after a document change")
    replies.removeFirst()(["error":["code":-32001,"message":"Song changed"]])
    try require(editor.crossfadeStatus.stringValue=="Song changed" && editor.crossfadePreviewRevision==nil,"Stale crossfade intent is rejected visibly")
    editor.crossfadeLoop.selectItem(at:1);editor.crossfadeMode.selectItem(at:0);editor.crossfade(dryRun:true)
    try require(calls.last?.1["loop"] as? String=="sustain" && calls.last?.1["expectedRevision"] as? String=="song-changed","Sustain crossfades use the newly displayed revision after rejection")
    editor.crossfadeFrames.stringValue="128";answer("song-changed")
    try require(editor.crossfadePreviewRevision==nil && editor.crossfadeStatus.stringValue.contains("discarded"),"Changed controls retire a pending preview")
    editor.crossfadeFrames.stringValue="1";let before=calls.count;editor.crossfade(dryRun:false)
    try require(calls.count==before,"Invalid fade length sends no request")
    editor.crossfadeFrames.stringValue="64";editor.crossfade(dryRun:true);editor.index=2;answer("song-changed")
    try require(editor.crossfadePreviewRevision==nil && !editor.crossfadeBusy,"Changing sample cannot apply an old crossfade preview")
    try require(editor.crossfadeInfo.stringValue.contains("sustain: 512 → 1800"),"Imported sustain loop geometry is visible")
  }
}
