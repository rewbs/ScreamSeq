import AppKit
extension InterfaceTests {
  static func sampleLoopChecks() throws {
    let editor=SampleEditor(frame:NSRect(x:0,y:0,width:729,height:1400))
    let info:[String:Any]=["frames":2048,"loop":true,"loopStart":64,"loopEnd":1024,"sustainLoop":false,"sustainStart":0,"sustainEnd":0]
    let inventory:[[String:Any]]=[["index":1,"name":"Loops"]]
    editor.update(info,samples:inventory,revision:"source")
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    func answer(_ revision:String) throws {try require(!replies.isEmpty,"Expected loop save before reply \(revision): \(editor.loopsStatus.stringValue)");replies.removeFirst()(["result":["revision":revision,"data":["loopsChanged":true]]])}
    func waitForSave(){let deadline=Date(timeIntervalSinceNow:2);while editor.loopSaveWork != nil && Date()<deadline {_ = RunLoop.main.run(mode:.default,before:Date(timeIntervalSinceNow:0.01))}}
    editor.waveform.moveLoopMarker(.start,to:100,finished:false)
    waitForSave()
    try require(calls.count==1 && calls[0].0=="sample.loops.set" && calls[0].1["dryRun"] as? Bool==false &&
      (calls[0].1["normal"] as? [String:Any])?["start"] as? Int==100 && calls[0].1["expectedRevision"] as? String=="source",
      "Dragging a loop handle commits during the gesture with the source revision: \(calls) \(editor.loopsStatus.stringValue)")
    editor.waveform.moveLoopMarker(.start,to:200,finished:true)
    try require(calls.count==1,"In-flight loop saves cannot duplicate")
    try answer("first");waitForSave()
    try require(calls.count==2 && calls[1].1["expectedRevision"] as? String=="first" &&
      (calls[1].1["normal"] as? [String:Any])?["start"] as? Int==200,"A later drag survives the first save and uses its new revision")
    replies.removeFirst()(["error":["code":-32002,"message":"Busy"]]);waitForSave()
    try require(calls.count==3 && editor.loopsBusy,"A transient busy response retries the loop edit")
    try answer("second")
    editor.waveform.moveLoopMarker(.start,to:3000,finished:true)
    try require(editor.loopStart.integerValue==1023,"Loop start clamps before the exclusive end")
    try answer("third")
    editor.sustaining.state = .on;editor.loopModeChanged(editor.sustaining)
    try require(editor.sustainEnd.integerValue==2048 && calls.last?.1["dryRun"] as? Bool==false,"Enabling an empty sustain loop immediately saves the sample span")
    try answer("fourth")
    editor.waveform.moveLoopMarker(.sustainEnd,to:900,finished:true);try answer("fifth")
    try require(editor.sustainEnd.integerValue==900 && editor.waveform.sustainEnd==900,"Sustain handles edit the independent sustain loop")
    editor.loopReverse.state = .on;editor.loopModeChanged(editor.loopReverse)
    try require(editor.pingpong.state == .off && (calls.last?.1["normal"] as? [String:Any])?["reverse"] as? Bool==true,"Reverse mode saves immediately")
    try answer("sixth")
    editor.pingpong.state = .on;editor.loopModeChanged(editor.pingpong)
    try require(editor.loopReverse.state == .off,"Ping-pong excludes reverse")
    try answer("seventh")
    editor.loopStart.stringValue="-1";let count=calls.count;editor.loopFieldChanged()
    try require(calls.count==count,"Invalid loop text cannot modify the song")
    editor.loopStart.stringValue="72";editor.loopFieldChanged()
    editor.update(info,samples:inventory,revision:"external")
    replies.removeFirst()(["error":["message":"Song changed"]])
    try require(editor.loopDraftRevision=="seventh" && editor.loopStart.integerValue==72 && editor.loopsStatus.stringValue.contains("Reload loops"),"External conflicts keep the local intent and require explicit reload")
    editor.reloadLoops();try require(editor.loopDraftRevision=="external" && editor.loopStart.integerValue==64,"Reload retires conflicting edits")
    editor.waveform.moveLoopMarker(.end,to:1200,finished:true)
    editor.index=2
    try require(editor.loopDraftRevision==nil,"Changing sample retires the previous loop draft")
    editor.update(info,samples:inventory,revision:"new-sample")
    editor.waveform.moveLoopMarker(.start,to:150,finished:true)
    try answer("retired");waitForSave()
    try require(editor.loopsBusy && calls.last?.1["sample"] as? Int==2 && calls.last?.1["expectedRevision"] as? String=="new-sample",
      "Retiring a previous sample's reply resumes a new sample's pending gesture without adopting the old revision")
    try answer("new-saved")
    editor.loopSaveWork?.cancel();editor.onRequest=nil
  }
}
