import AppKit
extension InterfaceTests {
  static func gateInspectorChecks() throws {
    let departure = DocumentDepartureInput()
    departure.begin(); departure.refreshed()
    try require(departure.blocked && !departure.adopted,"An early refresh cannot release input before adoption")
    departure.finish(adopted:false)
    try require(!departure.blocked,"A failed replacement immediately restores native input")
    departure.begin(); departure.finish(adopted:true)
    try require(departure.blocked && departure.adopted,"Successful adoption keeps input blocked through workspace refresh")
    departure.refreshed()
    try require(!departure.blocked && !departure.adopted,"Completed workspace refresh restores input")
    let formula = FormulaWorkbench(source:"start",title:"Departure fixture",points:[],selected:0,rows:64,rowsPerBeat:4,request:nil,use:{_ in true})
    try require(!formula.hasDraft,"An unchanged formula does not retain document work")
    formula.code.string="start +"
    try require(formula.hasDraft,"Incomplete expanded formula text retains its own draft")
    formula.close()
    var shape=GateShapeModel();shape.parameters["period"]=1.0;shape.parameters["width"]=0.25;shape.parameters["rise"]=8.0;shape.parameters["fall"]=8.0
    try require(shape.effective("rise")==0.25 && shape.effective("fall")==0.75 && shape.value(0.125)==0.5 && shape.value(0.25)==1 && shape.value(0.625)==0.5,"Gate preview clips each transition while retaining the rhythmic edges")
    try require(GateShapeModel.parse("3/8")==0.375 && GateShapeModel.parse("1/0")==nil && GateShapeModel.parse("NaN")==nil,"Gate beat fields accept fractions and reject nonfinite input")
    let exact=GateValueControl("Period")
    var exactChanges=0;exact.onChange={_ in exactChanges+=1}
    exact.configure(0.123456789123,min:1.0/64,max:16,log:true)
    exact.controlTextDidEndEditing(Notification(name:NSControl.textDidEndEditingNotification,object:exact.value))
    exact.entered()
    try require(exact.scalar==0.123456789123 && exactChanges==0,"Focusing, leaving or pressing Return in an untouched rounded display preserves the exact stored value")
    let displayed=exact.value.stringValue
    exact.value.stringValue="";exact.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:exact.value))
    try require(!exact.isValid,"Clearing an exact field marks its draft invalid")
    exact.value.stringValue=displayed;exact.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:exact.value))
    try require(exact.isValid && exact.scalar==0.123456789123 && exactChanges==1,"Pasting the original text restores validity without rounding the preserved value")
    let view=GateInspector(frame:NSRect(x:0,y:0,width:440,height:850))
    let model=PatternModel(["pattern":0,"rows":64,"channels":2,"rowsPerBeat":4,"revision":"gate:1","effectColumns":[2,1]])
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onContext={(model,4,0,5)}
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.capture();view.capture()
    try require(view.needsCompletion,"An in-flight gate read prevents document departure")
    try require(calls.count==1 && view.pending,"Gate read has a single captured context")
    replies.removeFirst()(["result":["revision":model.revisionToken,"data":["commands":[["kind":"native","native":"gate","parameters":GateShapeModel.defaults,"position":0,"channel":1,"column":0]],"bindings":[]]]])
    try require(!view.needsCompletion,"A clean gate permits departure without changing focus or applying")
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
    let before=calls.count;view.controls["period"]!.value.stringValue="NaN"
    view.controls["period"]!.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:view.controls["period"]!.value))
    try require(view.hasDraft && !view.controls["period"]!.isValid,"Uncommitted field-editor text is validated before another control can refresh it")
    view.apply(stop:false)
    try require(calls.count==before,"Invalid displayed text cannot silently apply the previous value")
    view.controls["width"]!.slider.doubleValue=0.25;view.controls["width"]!.slid();view.apply(stop:false)
    try require(calls.count==before && view.controls["period"]!.value.stringValue=="NaN" && !view.controls["period"]!.isValid,"Moving another slider preserves invalid exact text and prevents applying a hidden value")
    view.controls["period"]!.value.stringValue="1/2";view.controls["period"]!.entered();view.apply(stop:true,all:true)
    let stop=(calls.last!.1["commands"] as! [[String:Any]]).last!
    try require(stop["native"] as? String=="gate-stop" && (stop["parameters"] as? [String:Any])?["target"] as? String=="all","Stop all is an explicit native pattern command")
    replies.removeFirst()(["result":["revision":"gate:3","data":[:]]])
    for wanted in [Int(UInt32.max),7] {
      let bound=GateInspector(frame:.zero)
      let boundModel=PatternModel(["pattern":0,"rows":64,"channels":1,"rowsPerBeat":4,"revision":"binding:1","nativePlugins":[["instanceID":"stable","name":"Fixture"]]])
      bound.onContext={(boundModel,0,0,3)}
      bound.onRequest={method,params,reply in
        if method=="pattern.effects.get" {reply(["result":["revision":boundModel.revisionToken,"data":["commands":[["channel":0,"column":0,"position":0,"native":"gate","parameters":["target":"parameter","binding":1]]],"bindings":[["id":1,"plugin":"stable","parameter":wanted]]]]])}
        else {reply(["result":["revision":boundModel.revisionToken,"data":[["id":2,"name":"First","writable":true],["id":wanted==7 ? 9:wanted,"name":"Saved","writable":true]]]])}
      }
      bound.capture()
      try require(bound.parameter.indexOfSelectedItem==(wanted==7 ? -1:1),"Full-width saved parameter IDs stay selected; missing parameters remain unresolved")
    }
    view.layoutSubtreeIfNeeded()
    view.controls["period"]!.value.stringValue="-"
    view.controls["period"]!.entered()
    try require(view.needsCompletion && view.hasDraft,"Invalid raw gate text remains owned during Save, history and replacement")
    try require(view.waveform.bounds.width>250 && view.controls["period"]!.slider.bounds.width>20,"Gate waveform and sliders fit a dock column")
    if let at=CommandLine.arguments.firstIndex(of:"--snapshots"),at+1<CommandLine.arguments.count {
      let folder=URL(fileURLWithPath:CommandLine.arguments[at+1]);try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true)
      if let bitmap=view.bitmapImageRepForCachingDisplay(in:view.bounds){view.cacheDisplay(in:view.bounds,to:bitmap);try bitmap.representation(using:.png,properties:[:])?.write(to:folder.appendingPathComponent("GateInspector.png"))}
    }
  }
}
