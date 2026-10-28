import AppKit
extension InterfaceTests {
  static func gateInspectorChecks() throws {
    var shape=GateShapeModel();shape.parameters["period"]=1.0;shape.parameters["width"]=0.25;shape.parameters["rise"]=8.0;shape.parameters["fall"]=8.0
    try require(shape.effective("rise")==0.25 && shape.effective("fall")==0.75 && shape.value(0.125)==0.5 && shape.value(0.25)==1 && shape.value(0.625)==0.5,"Gate preview clips each transition while retaining the rhythmic edges")
    try require(GateShapeModel.parse("3/8")==0.375 && GateShapeModel.parse("1/0")==nil && GateShapeModel.parse("NaN")==nil,"Gate beat fields accept fractions and reject nonfinite input")
    let view=GateInspector(frame:NSRect(x:0,y:0,width:440,height:850))
    let model=PatternModel(["pattern":0,"rows":64,"channels":2,"rowsPerBeat":4,"revision":"gate:1","effectColumns":[2,1]])
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onContext={(model,4,0,5)}
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.capture();view.capture()
    try require(calls.count==1 && view.pending,"Gate read has a single captured context")
    replies.removeFirst()(["result":["revision":model.revisionToken,"data":["commands":[["kind":"native","native":"gate","parameters":GateShapeModel.defaults,"position":0,"channel":1,"column":0]],"bindings":[]]]])
    try require(view.controls.count==9 && view.controls.values.allSatisfy{$0.slider.isContinuous},"Every numeric gate control has a continuous slider and exact entry")
    view.controls["rise"]!.value.stringValue="3/8";view.controls["rise"]!.entered()
    try require(view.hasDraft && view.controls["fall"]!.scalar==0.375,"Linked transitions update both values")
    view.apply(stop:false)
    let request=calls.last!.1,commands=request["commands"] as! [[String:Any]],written=commands.last!
    try require(request["expectedRevision"] as? String==model.revisionToken && commands.count==2 && written["position"] as? Int==4*65536 && written["column"] as? Int==1 && written["channel"] as? Int==0,"Apply targets the captured cell and preserves unrelated commands")
    view.controls["period"]!.value.stringValue="7/8";view.controls["period"]!.entered()
    replies.removeFirst()(["result":["revision":"gate:2","data":[:]]])
    try require(view.hasDraft && view.controls["period"]!.scalar==0.875,"Apply completion preserves a newer slider or field draft")
    view.apply(stop:false);replies.removeFirst()(["error":["message":"Song changed"]])
    try require(view.hasDraft && view.controls["period"]!.scalar==0.875 && view.status.stringValue=="Song changed","Stale rejection retains the gate draft")
    let before=calls.count;view.controls["period"]!.value.stringValue="NaN";view.controls["period"]!.entered();view.apply(stop:false)
    try require(calls.count==before,"Invalid displayed text cannot silently apply the previous value")
    view.controls["width"]!.slider.doubleValue=0.25;view.controls["width"]!.slid();view.apply(stop:false)
    try require(calls.count==before && view.controls["period"]!.value.stringValue=="NaN" && !view.controls["period"]!.isValid,"Moving another slider preserves invalid exact text and prevents applying a hidden value")
    view.controls["period"]!.value.stringValue="1/2";view.controls["period"]!.entered();view.apply(stop:true,all:true)
    let stop=(calls.last!.1["commands"] as! [[String:Any]]).last!
    try require(stop["native"] as? String=="gate-stop" && (stop["parameters"] as? [String:Any])?["target"] as? String=="all","Stop all is an explicit native pattern command")
    replies.removeFirst()(["result":["revision":"gate:3","data":[:]]])
    view.layoutSubtreeIfNeeded()
    try require(view.waveform.bounds.width>250 && view.controls["period"]!.slider.bounds.width>20,"Gate waveform and sliders fit a dock column")
    if let at=CommandLine.arguments.firstIndex(of:"--snapshots"),at+1<CommandLine.arguments.count {
      let folder=URL(fileURLWithPath:CommandLine.arguments[at+1]);try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true)
      if let bitmap=view.bitmapImageRepForCachingDisplay(in:view.bounds){view.cacheDisplay(in:view.bounds,to:bitmap);try bitmap.representation(using:.png,properties:[:])?.write(to:folder.appendingPathComponent("GateInspector.png"))}
    }
  }
}
