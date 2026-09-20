import AppKit

extension InterfaceTests {
  static func sampleSnapChecks() throws {
    let editor=SampleEditor(frame:NSRect(x:0,y:0,width:729,height:1080))
    editor.update(["frames":1000,"channels":2],samples:[["index":1,"name":"Snap"]],revision:"r1")
    var requests=[(String,[String:Any])](), replies=[([String:Any])->Void]()
    editor.onRequest={ method,params,reply in requests.append((method,params));replies.append(reply) }
    func answer(_ a:Int,_ b:Int,revision:String="r1") {
      replies.removeFirst()(["result":["revision":revision,"data":["positions":[["after":a,"matched":true],["after":b,"matched":true]]]]])
    }
    editor.waveform.selection=13...97;editor.snapBoundaries(loop:false)
    try require(requests.last?.0=="sample.snap.get" && requests.last?.1["positions"] as? [Int]==[13,97] &&
      requests.last?.1["channels"] as? String=="both" && requests.last?.1["expectedRevision"]==nil,"Native snap uses the shared read-only boundary query")
    editor.snapBoundaries(loop:false);try require(requests.count==1,"Duplicate pending snap suppressed")
    answer(11,99)
    try require(editor.waveform.selection==11...99 && editor.snapStatus.stringValue.contains("Matched 2/2"),"Snap selects returned absolute boundaries")
    editor.snapMode.selectItem(at:1);editor.updateSnapOptions();editor.snapStep.stringValue="64";editor.snapOrigin.stringValue="8"
    editor.loopStart.stringValue="13";editor.loopEnd.stringValue="97";editor.snapBoundaries(loop:true)
    try require(requests.last?.1["mode"] as? String=="grid" && requests.last?.1["step"] as? Int==64 &&
      requests.last?.1["origin"] as? Int==8 && requests.last?.1["channels"]==nil,"Grid controls dispatch exact step/origin without zero-only controls")
    answer(8,72)
    try require(editor.loopStart.integerValue==8 && editor.loopEnd.integerValue==72 && editor.waveform.selection==11...99,
      "Loop snap changes pending loop controls without editing PCM or selection")
    editor.snapBoundaries(loop:true);answer(8,8)
    try require(editor.loopEnd.integerValue==72 && editor.snapStatus.stringValue.contains("collapse"),"Collapsed loops are rejected without changing controls")
    editor.snapBoundaries(loop:false);editor.waveform.selection=20...80;answer(8,72)
    try require(editor.waveform.selection==20...80,"A delayed snap cannot replace a newer selection")
    editor.snapBoundaries(loop:false);editor.snapStep.stringValue="32";answer(8,72)
    try require(editor.waveform.selection==20...80,"A changed snap configuration retires old results")
    editor.snapBoundaries(loop:false);answer(8,72,revision:"r2")
    try require(editor.waveform.selection==20...80 && editor.snapStatus.stringValue.contains("sample changed"),"Sample revision changes reject stale geometric intent")
    editor.snapStep.stringValue="0";let beforeInvalid=requests.count;editor.snapBoundaries(loop:false)
    try require(requests.count==beforeInvalid,"Invalid grid controls issue no query")
    editor.snapStep.stringValue="16";editor.snapAutomatically.state = .on
    let beforeAuto=requests.count
    editor.waveform.selection=10...80;editor.snapAfterSelection()
    editor.waveform.selection=100...180;editor.snapAfterSelection()
    editor.waveform.selection=200...280;editor.snapAfterSelection()
    try require(requests.count==beforeAuto+1,"Auto snap coalesces rapid selections into one in-flight read")
    answer(8,72)
    try require(requests.count==beforeAuto+2 && requests.last?.1["positions"] as? [Int]==[200,280],"Auto snap dispatches only the latest queued selection")
    answer(200,280)
    try require(editor.waveform.selection==200...280 && !editor.snapBusy && !editor.snapPending,"Final auto snap settles without recursive requests")
    editor.snapBoundaries(loop:false);editor.index=2;answer(200,280)
    try require(editor.waveform.selection==nil,"Changing samples retires the pending snap")
  }
}
