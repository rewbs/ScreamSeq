import AppKit

extension InterfaceTests {
  static func scratchFormulaReferenceChecks() throws {
    let oldSymbols=FormulaCatalog.symbols,oldNotes=FormulaCatalog.notes
    defer{FormulaCatalog.symbols=oldSymbols;FormulaCatalog.notes=oldNotes}
    FormulaCatalog.symbols=["row","beat","beats","duration","startBeat","endBeat"].map{["name":$0,"insert":$0,"description":"Pattern meaning for \($0)"]}
    FormulaCatalog.notes="Results are clamped to 0–1.\nThe final node's scripted segment continues to the envelope end.\nPlayback evaluates on a bounded 32-sample clock with host ramps between values."
    let originalSymbols=FormulaCatalog.symbols,originalNotes=FormulaCatalog.notes
    let ordinary=FormulaReferenceView(frame:.zero);ordinary.reload()
    let editor=ScratchGestureEditor(target:nil)
    editor.showFormulaReference()
    let standalone=FormulaWorkbench.referenceWindow?.contentView?.subviews.compactMap{$0 as? FormulaReferenceView}.first
    try require(standalone?.scratchContext==true && FormulaWorkbench.referenceWindow?.title=="Scratch formula reference","Scratch Reference opens the cycle-specific reference")
    try require(standalone?.filtered.first(where:{$0["name"]=="row"})?["description"]?.contains("0–256")==true && standalone?.filtered.first(where:{$0["name"]=="beat"})?["description"]?.contains("repeat")==true,"Scratch reference describes the synthetic row coordinate and repeating cycle clock")
    try require(standalone?.notes.stringValue.contains("every audio sample")==true && standalone?.notes.stringValue.contains("32-sample")==false && standalone?.notes.stringValue.contains("no following segment")==true,"Scratch notes explain sample resolution and the final endpoint without automation host-ramp semantics")
    standalone?.search.stringValue="scratch cycle";standalone?.filter()
    try require(standalone?.filtered.contains(where:{$0["name"]=="startBeat"})==true && standalone?.filtered.contains(where:{$0["name"]=="endBeat"})==true,"Search uses context-specific symbol descriptions")
    FormulaWorkbench.referenceWindow?.close()
    editor.motion.points=[EnvelopePoint(position:0,value:0,curve:"scripted",formula:"t"),EnvelopePoint(position:65536,value:1,curve:"linear")]
    editor.motion.selected=0;editor.activeLane=0;editor.expandFormula();editor.formulaWorkbench?.window?.orderOut(nil)
    try require(editor.formulaWorkbench?.reference.scratchContext==true && editor.formulaWorkbench?.reference.notes.stringValue.contains("SK beats ÷ repeats")==true,"Expanded scratch formulas use the same cycle-specific reference")
    editor.formulaWorkbench?.work?.cancel();editor.formulaWorkbench?.close()
    ordinary.reload()
    try require(FormulaCatalog.symbols==originalSymbols && FormulaCatalog.notes==originalNotes && ordinary.notes.stringValue.contains("32-sample") && ordinary.filtered.first?["description"]=="Pattern meaning for row","Scratch references do not alter the shared catalogue or ordinary envelope semantics")
    print("PASS scratch formula reference: cycle clock, synthetic rows, per-sample evaluation and isolated catalogue")
  }
  static func scratchGestureChecks() throws {
    try scratchFormulaReferenceChecks()
    let motion:[[String:Any]]=[["position":0,"value":0.0,"curve":"smooth"],["position":32768,"value":1.0,"curve":"smooth"],["position":65536,"value":0.0,"curve":"linear"]]
    let fader:[[String:Any]]=[["position":0,"value":1.0,"curve":"linear"],["position":65536,"value":1.0,"curve":"linear"]]
    var stored:[String:Any]=["id":3,"name":"Baby","motion":motion,"fader":fader,"uses":2]
    let command:[String:Any]=["kind":"native","native":"scratch","offset":1234,"parameters":["gesture":3,"beats":2.0,"travelMs":90.0,"repeats":4,"reverse":true]]
    let target=ScratchPatternTarget(pattern:2,row:16,channel:1,column:2,rows:64,revision:"song:1",command:command)
    let editor=ScratchGestureEditor(target:target)
    var writes=[(String,[String:Any])](),replies=[([String:Any])->Void](),readRevision="song:1"
    editor.onRequest={method,params,reply in
      if method=="scratch.gestures.get"{reply(["result":["revision":readRevision,"data":["unitsPerCycle":65536,"gestures":[stored],"presets":[["id":"baby","name":"Baby","motion":motion,"fader":fader]]]]])}
      else if method != "automation.formula.reference" {writes.append((method,params));replies.append(reply)}
    }
    editor.load()
    try require(editor.selectedID==3 && editor.motion.maximumPosition==65536 && editor.motion.horizontalEnd==65536 && editor.motion.points.last?.position==65536,"Scratch canvas preserves the inclusive cycle endpoint without rescaling its stored points")
    try require(editor.linkage.stringValue.contains("2 linked") && editor.previewBeats==0.5,"Scratch inspector explains shared uses and previews actual beats per repeated cycle")
    let menuEditor=ScratchGestureEditor(target:target);var other=stored,menuRevision="menu:1",reads=0
    menuEditor.onRequest={method,_,reply in if method=="scratch.gestures.get"{reads+=1;reply(["result":["revision":menuRevision,"data":["gestures":[other],"presets":[]]]])}}
    menuEditor.load();other["name"]="Renamed by agent";menuRevision="menu:2";menuEditor.observeRevision(menuRevision)
    try require(menuEditor.gestures.selectedItem?.title=="3 · Renamed by agent","External bank renames refresh the phrase selector as well as its curves")
    menuEditor.motion.selected=1;menuEditor.showPoint();menuEditor.position.stringValue="invalid";menuEditor.pointFieldsDirty=true
    let oldReads=reads;menuEditor.load();menuEditor.observeRevision("menu:3")
    try require(reads==oldReads && menuEditor.position.stringValue=="invalid","Automatic refresh and reopening cannot discard a dirty numeric entry")
    editor.motion.setViewport(start:1000,span:12000)
    try require(editor.fader.visibleStart==1000 && editor.fader.horizontalSpan==12000,"Motion and fader use synchronized zoom and pan")
    editor.motion.selected=0;editor.activeLane=0;editor.showPoint();editor.motion.removeSelected()
    try require(editor.motion.points.count==3 && !editor.position.isEnabled,"Mandatory endpoints cannot be deleted or moved horizontally")
    editor.motion.replaceSelected(position:20000,value:0.25,curve:"linear")
    try require(editor.motion.points[0].position==0 && editor.motion.points[0].value==0.25,"An anchored endpoint still supports direct value and curve editing")
    editor.save()
    try require(writes.count==1 && writes[0].0=="scratch.gestures.set" && writes[0].1["expectedRevision"] as? String=="song:1" && ((writes[0].1["motion"] as? [[String:Any]])?.last?["position"] as? Int)==65536,"A completed motion edit writes one guarded paired phrase with the exact final point")
    // Another drag while publication is pending must not be replaced by its reply.
    editor.motion.replaceSelected(position:0,value:0.5,curve:"smooth")
    replies.removeFirst()(["result":["revision":"song:2","data":["id":3]]])
    try require(editor.hasDraft && editor.motion.points[0].value==0.5 && editor.target?.revision=="song:2","The older save reply retains newer motion edits and advances only its own captured target")
    editor.save();let second=writes.last!.1
    replies.removeFirst()(["result":["revision":"song:3","data":["id":3]]])
    try require(!editor.hasDraft && second["expectedRevision"] as? String=="song:2" && editor.revision=="song:3","Queued edits serialize against the preceding successful revision")
    editor.useInPattern()
    let use=writes.last!.1,insert=use["command"] as? [String:Any],parameters=insert?["parameters"] as? [String:Any]
    try require(writes.last!.0=="pattern.effect.set" && use["pattern"] as? Int==2 && use["row"] as? Int==16 && use["channel"] as? Int==1 && use["column"] as? Int==2 && use["expectedRevision"] as? String=="song:3","Use writes the captured FX cell rather than a subsequently moved cursor")
    try require(insert?["offset"] as? Int==1234 && parameters?["travelMs"] as? Double==90 && parameters?["beats"] as? Double==2 && parameters?["repeats"] as? Int==4 && parameters?["reverse"] as? Bool==true,"Replacing a phrase reference retains the cell's detailed onset, duration, travel and repeat controls")
    replies.removeFirst()(["result":["revision":"song:4","data":[:]]])
    let cloneEditor=ScratchGestureEditor(target:target);var cloneCalls=[(String,[String:Any])](),cloneReply:(([String:Any])->Void)?
    cloneEditor.onRequest={method,params,reply in
      if method=="scratch.gestures.get"{reply(["result":["revision":"song:1","data":["gestures":[stored],"presets":[]]]])}
      else{cloneCalls.append((method,params));cloneReply=reply}
    }
    cloneEditor.load();cloneEditor.duplicate(use:true)
    let clonedTarget=cloneCalls.first?.1["target"] as? [String:Int]
    try require(cloneCalls.count==1 && cloneCalls[0].0=="scratch.gestures.clone" && cloneCalls[0].1["id"] as? Int==3 && clonedTarget==["pattern":2,"row":16,"channel":1,"column":2],"Make unique submits one captured clone-and-assign transaction")
    cloneReply?(["error":["code":-32001,"message":"Song changed"]])
    try require(cloneCalls.count==1 && cloneEditor.target?.gesture==3 && !cloneEditor.pending,"Failed Make unique does not perform a second write or change the captured reference")
    cloneEditor.duplicate(use:false)
    try require(cloneCalls.last?.0=="scratch.gestures.clone" && cloneCalls.last?.1["target"]==nil,"Ordinary duplication only clones the song bank")
    cloneEditor.invalidate()
    editor.fader.selected=0;editor.activeLane=1;editor.fader.replaceSelected(position:0,value:0.2,curve:"linear");editor.save()
    replies.removeFirst()(["error":["code":-32001,"message":"Song changed"]])
    try require(editor.hasDraft && editor.fader.points[0].value==0.2 && editor.status.stringValue.contains("retained"),"Rejected stale writes keep the original draft visible")
    let previous=writes.count;stored["name"]="Edited elsewhere";readRevision="song:9";editor.retry()
    try require(writes.count==previous && editor.hasDraft && editor.status.stringValue.contains("changed elsewhere"),"Retry refuses to overwrite a phrase that changed externally")
    let scripted=EnvelopePoint(position:0,value:0,curve:"linear",formula:"sin(t)")
    try require(ScratchGestureEditor.encoded([scripted]).first?["formula"]==nil,"Changing away from Scripted does not submit an invalid formula on an ordinary curve")
    editor.invalidate();editor.save();editor.useInPattern()
    try require(writes.count==previous && !editor.motion.allowsEditing,"Changing songs retires all scratch mutations while retaining the draft")
    editor.saveWork?.cancel();editor.previewWork?.cancel()

    let lastTarget=ScratchPatternTarget(pattern:0,row:31,channel:0,column:0,rows:32,rowsPerBeat:8,revision:"last:1",command:nil)
    let last=ScratchGestureEditor(target:lastTarget);var lastRequest:[String:Any]?
    last.onRequest={method,params,reply in
      if method=="scratch.gestures.get"{reply(["result":["revision":"last:1","data":["gestures":[stored],"presets":[]]]])}
      else{lastRequest=params}
    };last.load();last.useInPattern()
    try require((((lastRequest?["command"] as? [String:Any])?["parameters"] as? [String:Any])?["beats"] as? Double)==0.125,"A new phrase placed on the final row defaults to the remaining musical duration")
    let lastModel=PatternModel(["rows":32,"channels":1,"displayRowsPerBeat":8,"scratchGestures":[stored]])
    let schema=PatternEffectSchema(["kind":"native","native":"scratch","displayCode":"SK","parameters":[["key":"beats","storage":"parameters.beats","unit":"beats","default":1.0],["key":"gesture","storage":"parameters.gesture","type":"integer","default":1]]])
    let inline=PatternNudgeEditor(model:lastModel,target:EditorNavigation(pattern:0,row:31,channel:0,column:3),schema:schema,timing:.beats)
    var inlineRequest:[String:Any]?;inline.onRequest={params,_ in inlineRequest=params};inline.commit(advance:false)
    let defaults=(inlineRequest?["command"] as? [String:Any])?["parameters"] as? [String:Any]
    try require(defaults?["beats"] as? Double==0.125 && defaults?["gesture"] as? Int==3,"Typing SK at the final row clamps only its new default and selects an available song phrase")

    let model=PatternModel(["rows":32,"channels":1,"pattern":0,"scratchGestures":[stored]])
    try require(model.scratchGestures.count==1,"Pattern snapshots retain full scratch definitions for portable clipboard copies")
    let view=PatternView();view.frame=NSRect(x:0,y:0,width:600,height:250);view.model=model;view.canEdit={true};view.commandRevision={"clip:1"}
    var paste:[String:Any]?
    view.onPaste={paste=$0}
    let payload:[String:Any]=["rows":1,"channels":1,"cells":[[0,0,0,0,0,0]],"effects":[],"bindings":[],"notes":[],"scratchGestures":[stored]]
    let text="ScreamSeq Pattern 2\n"+String(data:try JSONSerialization.data(withJSONObject:payload),encoding:.utf8)!
    view.pasteText(text)
    let deadline=Date().addingTimeInterval(2)
    while paste==nil && Date()<deadline{RunLoop.current.run(until:Date().addingTimeInterval(0.01))}
    try require((paste?["scratchGestures"] as? [[String:Any]])?.first?["name"] as? String=="Edited elsewhere","Native paste forwards scratch definitions for collision-safe shared-model remapping")
    print("PASS scratch UI: inclusive endpoints, linked use disclosure, synchronized views, queued revisions, draft retention, guarded cell insertion and portable clipboard")
  }
}
