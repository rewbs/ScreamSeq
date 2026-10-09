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
    try scratchNumericDraftChecks()
    try scratchKeyboardInsertionChecks()
    let captured=ScratchPatternTarget(pattern:2,row:16,channel:1,column:1,rows:64,patternID:"p7",trackID:"t4",revision:"song:1",command:nil)
    var moved=PatternModel(["revisionToken":"song:9","channels":3,"patterns":[["id":"p7","index":4,"rows":32],["id":"other","index":2,"rows":64]],"tracks":[["id":"t4","index":2],["id":"other","index":1]]])
    moved.effectColumns=[1,1,2]
    let location=captured.location(in:moved)
    try require(location?.pattern==4 && location?.channel==2 && location?.rows==32,"Scratch Return and Preview follow captured identities after pattern/channel moves and use the current pattern length")
    try require(captured.pattern==2 && captured.channel==1 && captured.revision=="song:1","Resolving scratch navigation never rebases the captured mutation revision or destination")
    var missing=moved;missing.patterns.removeAll{$0["id"] as? String=="p7"}
    try require(captured.location(in:missing)==nil,"A deleted scratch pattern cannot redirect to the new occupant of its old index")
    missing=moved;missing.tracks.removeAll{$0["id"] as? String=="t4"}
    try require(captured.location(in:missing)==nil,"A deleted scratch channel cannot redirect to another channel")
    missing=moved;missing.revisionToken="another:1"
    try require(captured.location(in:missing)==nil,"Scratch navigation rejects a different song even if identities are reused")
    missing=moved;missing.patterns[0]["rows"]=16
    try require(captured.location(in:missing)==nil,"Scratch navigation rejects a row removed by pattern shortening")
    missing=moved;missing.effectColumns[2]=1
    try require(captured.location(in:missing)==nil,"Scratch navigation rejects a removed FX column")
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
    menuEditor.pointFieldsDirty=false;other["id"]=7;other["name"]="Remaining phrase";menuRevision="menu:3";menuEditor.observeRevision(menuRevision)
    try require(menuEditor.selectedID==7 && menuEditor.name.stringValue=="Remaining phrase" && menuEditor.motion.allowsEditing && menuEditor.status.stringValue.contains("previous phrase was removed"),"Undo removing the inspected copy leaves the remaining song bank usable and explains the selection change")
    try require(menuEditor.target?.revision==target.revision && menuEditor.target?.gesture==target.gesture,"Bank history refresh never silently rebases a captured pattern destination")
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

  static func scratchNumericDraftChecks() throws {
    let motion:[[String:Any]]=[["position":0,"value":0.0,"curve":"linear"],["position":32768,"value":1.0,"curve":"linear"],["position":65536,"value":0.0,"curve":"linear"]]
    let fader:[[String:Any]]=[["position":0,"value":1.0,"curve":"linear"],["position":65536,"value":1.0,"curve":"linear"]]
    var phrase:[String:Any]=["id":3,"name":"Original","motion":motion,"fader":fader,"uses":0]
    let other:[String:Any]=["id":4,"name":"Other","motion":motion,"fader":fader,"uses":0]
    let command:[String:Any]=["kind":"native","native":"scratch","parameters":["gesture":3]]
    let target=ScratchPatternTarget(pattern:2,row:16,channel:1,column:2,rows:64,revision:"song:1",command:command)
    let editor=ScratchGestureEditor(target:target)
    var writes=[(String,[String:Any])](),revision="song:1",previews=0
    editor.onPreview={_ in previews+=1}
    editor.onRequest={method,params,reply in
      if method=="scratch.gestures.get" {
        reply(["result":["revision":revision,"data":["gestures":[phrase,other],"presets":[["id":"baby","name":"Baby"]]]]])
      } else if method=="scratch.gestures.set" {
        writes.append((method,params));for key in ["name","motion","fader"]{phrase[key]=params[key]};revision="song:2"
        reply(["result":["revision":revision,"data":["id":3]]])
      } else {writes.append((method,params));reply(["error":["message":"Unexpected mutation"]])}
    }
    editor.load()
    for field in [editor.position,editor.value] {
      editor.activeLane=0;editor.motion.selected=1;editor.showPoint()
      field.stringValue="invalid";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:field))
      editor.controlTextDidEndEditing(Notification(name:NSControl.textDidEndEditingNotification,object:field))
      try require(editor.pointFieldsDirty && !editor.hasDraft && !editor.gestures.isEnabled && !editor.presetButton.isEnabled,"An uncommitted numeric entry disables phrase-changing controls without pretending a curve was saved")
      let actions=editor.actionsMenu()
      for title in ["Duplicate phrase","Make unique at captured row","Delete unused phrase","Insert point after selected","Delete selected interior point"] {
        try require(actions.item(withTitle:title)?.isEnabled==false,"Numeric drafts disable the same secondary bank actions as curve drafts")
      }
      try require(editor.presetMenu().item(at:0)?.isEnabled==false,"A previously opened preset menu cannot offer a new phrase while numeric text is retained")
      // Reproduce a stale popup/menu action delivered after text became dirty.
      editor.gestures.selectItem(at:1);editor.selectGesture();editor.create(preset:"baby")
      editor.duplicate(use:false);editor.duplicate(use:true);editor.remove();editor.play();editor.useInPattern();editor.insertPoint()
      try require(writes.isEmpty && previews==0 && editor.selectedID==3 && editor.gestures.selectedItem?.representedObject as? Int==3 && field.stringValue=="invalid" && editor.pointFieldsDirty,"Explicit selection and stale bank actions retain invalid Cycle/% text and perform no mutation or preview")
      try require(!editor.motion.allowsEditing && !editor.fader.allowsEditing && !editor.curve.isEnabled,"Numeric drafts prevent canvas and curve mutations before selection callbacks can change their target")
      editor.motion.selected=0;editor.motion.onSelect?()
      editor.fader.selected=1;editor.fader.onSelect?()
      editor.activeLane=1;editor.showPoint()
      editor.curve.selectItem(at:8);editor.changeCurve()
      editor.motion.replaceSelected(position:100,value:0.1,curve:"smooth");editor.motion.removeSelected()
      try require(editor.activeLane==0 && editor.motion.selected==1 && editor.motion.points.count==3 && editor.motion.points[1].position==32768 && editor.motion.points[1].curve=="linear" && editor.curve.indexOfSelectedItem==1 && field.stringValue=="invalid" && writes.isEmpty,"Queued point/lane/curve actions restore the numeric draft target without replacing its text or editing another point")
      try require(editor.target?.revision=="song:1" && editor.target?.gesture==3,"Rejected navigation never silently rebases the captured pattern reference")
      let reload=actions.indexOfItem(withTitle:"Reload / discard retained changes")
      try require(reload>=0 && actions.item(at:reload)?.isEnabled==true,"Explicit draft discard remains available")
      actions.performActionForItem(at:reload)
      try require(!editor.hasPendingEdits && editor.gestures.isEnabled && editor.selectedID==3 && editor.target?.revision=="song:1","Explicit discard restores the same phrase without rebasing the captured cell")
    }
    editor.motion.selected=1;editor.showPoint();editor.position.stringValue="0.4";editor.value.stringValue="62.5"
    editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.position));editor.commitPoint()
    try require(writes.count==1 && writes[0].0=="scratch.gestures.set" && !editor.hasPendingEdits && editor.gestures.isEnabled && abs(editor.motion.points[1].value-0.625)<0.000001,"A corrected numeric entry saves immediately and restores phrase navigation")
    editor.gestures.selectItem(at:1);editor.selectGesture()
    try require(editor.selectedID==4 && editor.name.stringValue=="Other","Phrase selection works normally after numeric editing completes")
    editor.saveWork?.cancel();editor.previewWork?.cancel()
    print("PASS scratch numeric draft: explicit selection, stale bank actions, discard and corrected values")
  }

  static func scratchKeyboardInsertionChecks() throws {
    let points:[[String:Any]]=[["position":0,"value":0.0,"curve":"linear"],["position":65536,"value":1.0,"curve":"linear"]]
    var phrase:[String:Any]=["id":9,"name":"Keyboard phrase","motion":points,"fader":points,"uses":0]
    let editor=ScratchGestureEditor();var writes=0,revision=1
    editor.onRequest={method,params,reply in
      if method=="scratch.gestures.get"{reply(["result":["revision":"keyboard:\(revision)","data":["gestures":[phrase],"presets":[]]]])}
      else if method=="scratch.gestures.set"{
        writes+=1;revision+=1;for key in ["name","motion","fader"]{phrase[key]=params[key]}
        reply(["result":["revision":"keyboard:\(revision)","data":["id":9]]])
      }else{reply(["error":["message":"Unexpected operation"]])}
    }
    editor.load()
    func hasInsert(_ menu:NSMenu)->Bool {menu.items.contains{$0.title=="Insert point after selected" || $0.submenu.map(hasInsert)==true}}
    try require(hasInsert(ContextActions.controls(in:editor,title:"Scratch phrases")),"The keyboard insertion action is exposed through the same More and command-palette catalogue")
    let menu=editor.actionsMenu(),insert=menu.indexOfItem(withTitle:"Insert point after selected")
    try require(insert>=0 && menu.item(at:insert)?.isEnabled==true,"Insertion is available without a selected point")
    menu.performActionForItem(at:insert)
    try require(writes==1 && editor.motion.selected==1 && editor.motion.points[1].position==32768 && editor.motion.points[1].value==0.5 && editor.position.stringValue=="0.5" && editor.value.stringValue=="50","No-selection insertion chooses a free midpoint and selects its exact point fields in one save")
    editor.insertPoint()
    try require(writes==2 && editor.motion.selected==2 && editor.motion.points[2].position==49152,"Keyboard insertion places the next point after the selected point")
    editor.motion.selected=editor.motion.points.count-1;editor.showPoint();editor.insertPoint()
    try require(writes==3 && editor.motion.points[editor.motion.selected!].position==57344 && editor.motion.points.last?.position==65536,"Selecting the last endpoint inserts before it without moving the mandatory endpoint")
    let originalMotion=editor.motion.points
    editor.fader.selected=0;editor.fader.onSelect?();editor.insertPoint()
    try require(writes==4 && editor.activeLane==1 && editor.fader.selected==1 && editor.fader.points[1].position==32768 && editor.motion.points==originalMotion,"Keyboard insertion edits the selected fader lane and leaves motion unchanged")
    editor.activeLane=0;editor.motion.points=[EnvelopePoint(position:0,value:0,curve:"linear"),EnvelopePoint(position:1,value:0.5,curve:"linear"),EnvelopePoint(position:65536,value:1,curve:"linear")];editor.motion.selected=0;editor.showPoint();editor.insertPoint()
    try require(writes==4 && editor.motion.points.count==3 && editor.status.stringValue.contains("No free cycle position") && editor.actionsMenu().item(withTitle:"Insert point after selected")?.isEnabled==false,"A full selected interval rejects insertion without a duplicate position or mutation")
    editor.motion.points=(0..<256).map{EnvelopePoint(position:$0*65536/255,value:0.5,curve:"linear")};editor.motion.selected=0;editor.showPoint();editor.insertPoint()
    try require(writes==4 && editor.motion.points.count==256 && editor.status.stringValue.contains("256 points"),"Keyboard insertion respects the shared point budget and retains both endpoint positions")
    editor.saveWork?.cancel();editor.previewWork?.cancel()
    print("PASS scratch keyboard insertion: catalogue, no selection, interior/last points, both lanes and bounded gaps")
  }
}
