import AppKit

extension InterfaceTests {
  static func songAutomationChecks() throws {
    try songAutomationReadBoundaryChecks()
    try automationPointOwnershipChecks()
    try audioSettingsChecks()
    try instrumentAutomaticChecks()
    try activityDraftChecks()
    let editor=SongAutomationEditor(frame:NSRect(x:0,y:0,width:1000,height:650))
    var model=PatternModel(["revisionToken":"song:1:0:0","nativePlugins":[["instanceID":"stable-a","name":"Fixture"]]])
    var requests=[(String,[String:Any])](),writeReply:(([String:Any])->Void)?,failRead=false
    var stored:[[String:Any]]=[["slot":0,"id":71,"frame":48001,"value":0.3],["slot":0,"id":71,"frame":96000,"value":0.8]]
    editor.onContext={model}
    editor.onRequest={method,params,reply in
      requests.append((method,params))
      if method=="automation.replaceLane"{writeReply=reply;return}
      if failRead{reply(["error":["message":"Read failed"]]);return}
      let data:Any=method=="automation.get" ? ["points":stored,"total":stored.count,"sampleRate":48000] : [["id":71,"name":"Cutoff","unitLabel":"Hz","min":0,"max":1,"value":0.5,"writable":true],["id":72,"name":"Gain","unitLabel":"dB","min":-60,"max":6,"value":0,"writable":true]]
      reply(["result":["revision":model.revisionToken,"data":data]])
    }
    editor.inspect(plugin:"stable-a",parameter:71)
    try require(editor.points.count==2 && editor.canvas.points[0].position==48001,"Absolute automation retains single-frame time precision")
    editor.search.stringValue="dB";editor.filter()
    try require(editor.parameterID==71 && editor.filtered.count==2 && editor.parameter.itemTitles.contains(where:{$0.contains("outside search")}),"Searching another parameter retains the captured lane")
    editor.canvas.setViewport(start:48000,span:8);editor.viewStart.stringValue="unfinished view"
    failRead=true;editor.reload()
    try require(editor.points.count==2 && editor.pluginID=="stable-a" && editor.canvas.horizontalSpan==8 && editor.viewStart.stringValue=="unfinished view","A failed reload preserves the lane, frame-scale viewport and raw view intent")
    failRead=false;editor.reload()
    try require(editor.canvas.horizontalSpan==8 && editor.viewStart.stringValue=="unfinished view","Successful same-lane refresh retains viewport and raw view intent")
    editor.canvas.selected=0;editor.showPoint();editor.time.stringValue="3";editor.value.stringValue="0.4"
    editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.time))
    var completed:Bool?;editor.finishAutomaticEdit{completed=$0}
    let first=requests.last!
    try require(first.0=="automation.replaceLane" && first.1["expectedRevision"] as? String=="song:1:0:0" && completed==nil,"Save waits for the captured lane write")
    let submitted=first.1["points"] as! [[String:Any]]
    try require(submitted.map{$0["frame"] as! Int}==[96000,144000] && (submitted[0]["value"] as! Double)==0.8,"Moving a point reorders it without rounding another point's value")
    editor.value.stringValue="0.6";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.value))
    model.revisionToken="song:2:0:0";writeReply?(["result":["revision":model.revisionToken,"data":[:]]])
    try require(completed==nil && editor.fieldDraft,"A write acknowledgement cannot erase newer raw fields or complete Save early")
    RunLoop.current.run(until:Date().addingTimeInterval(0.4))
    try require(requests.last?.1["expectedRevision"] as? String=="song:2:0:0","Newer fields continue from the accepted revision")
    model.revisionToken="song:3:0:0";writeReply?(["result":["revision":model.revisionToken,"data":[:]]])
    try require(completed==true && !editor.hasDraft && !editor.fieldDraft && editor.canvas.selected==1,"Save completes after the newest edit and preserves the moved point selection")
    try require((editor.canvas.accessibilityValue() as? String)?.contains("value 0.6")==true,"Numeric automation edits update the accessible timeline value without a reload")
    editor.value.stringValue="unfinished";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.value));completed=nil;let count=requests.count
    editor.finishAutomaticEdit{completed=$0}
    try require(completed==false && requests.count==count && editor.value.stringValue=="unfinished","Invalid fields block Save without dispatch or loss")
    editor.value.stringValue="0.7";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.value));editor.finishAutomaticEdit{completed=$0}
    writeReply?(["error":["message":"Revision conflict"]]);let refusedCount=requests.count
    editor.finishAutomaticEdit{completed=$0};RunLoop.current.run(until:Date().addingTimeInterval(0.4))
    try require(completed==false && requests.count==refusedCount && editor.hasDraft,"Save never blindly retries a refused or uncertain automation write")
    stored=[]
    editor.reload();try require(editor.points.isEmpty && !editor.hasDraft,"Explicit Reload discards the retained lane draft")
    print("PASS song automation: frame precision, captured targets, search, retained viewport, ordered Save and refused drafts")
  }
  static func songAutomationReadBoundaryChecks() throws {
    let editor=SongAutomationEditor(frame:.zero)
    let model=PatternModel(["revisionToken":"song:1","nativePlugins":[["instanceID":"a","name":"Fixture"]]]),scalar=0.123456789123
    var requests=[(String,[String:Any],([String:Any])->Void)](),finished=[Bool]()
    editor.onContext={model};editor.onRequest={requests.append(($0,$1,$2))}
    func answer(_ data:Any){requests.removeFirst().2(["result":["revision":"song:1","data":data]])}
    let catalog:[[String:Any]]=[["id":7,"min":scalar,"max":1,"writable":true]]
    let latePoints:[[String:Any]]=[["slot":0,"id":7,"frame":28_000_000_001,"value":scalar]]
    let earlyPoints:[[String:Any]]=[["slot":0,"id":7,"frame":48001,"value":scalar]]
    editor.inspect(plugin:"a",parameter:7)
    editor.finishAutomaticEdit{finished.append($0)}
    answer(["points":latePoints,"total":1,"sampleRate":48000])
    editor.finishAutomaticEdit{finished.append($0)}
    try require(finished.isEmpty,"Save and close wait through both lane read stages")
    answer(catalog)
    try require(finished==[true,true] && !editor.pending && editor.points.count==1,"Successful lane load releases every waiting boundary")
    editor.value.stringValue="0.25";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.value));editor.finishAutomaticEdit{finished.append($0)}
    try require((requests[0].1["points"] as! [[String:Any]])[0]["frame"] as? Int==28_000_000_001,"Value-only edits preserve exact late-song frame: request=\(requests[0].1), fields=\(editor.time.stringValue), points=\(editor.points)")
    answer([:])
    editor.reload();answer(["points":earlyPoints,"total":1,"sampleRate":48000]);answer(catalog)
    editor.canvas.selected=0;editor.showPoint()
    editor.time.stringValue="2";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.time));editor.finishAutomaticEdit{finished.append($0)}
    try require(requests.count==1,"Time-only editing dispatches one lane write")
    let moved=(requests[0].1["points"] as! [[String:Any]])[0]
    try require(moved["frame"] as? Int==96000 && moved["value"] as? Double==scalar,"Time-only edits preserve the untouched native parameter value exactly")
    answer([:]);let count=finished.count;editor.setPoint()
    try require(!editor.hasDraft && requests.isEmpty,"Accepting unchanged point fields cannot round the native value")
    editor.reload();editor.finishAutomaticEdit{finished.append($0)};requests.removeFirst().2(["error":["message":"Read failed"]])
    try require(finished.count==count+1 && finished.last==false,"Failed reads release Save without discarding the prior lane")
    editor.reload();answer(["points":[],"total":0,"sampleRate":48000]);editor.finishAutomaticEdit{finished.append($0)};requests.removeFirst().2(["error":["message":"Catalog failed"]])
    try require(finished.count==count+2 && finished.last==false,"Failed parameter catalogs also release Save")
  }
  static func automationPointOwnershipChecks() throws {
    let pattern=PatternAutomationEditor(frame:.zero),graph=GraphEnvelopeEditor(frame:.zero)
    pattern.parameterID=7
    let original=[EnvelopePoint(position:0,value:0.25,curve:"linear"),EnvelopePoint(position:256,value:0.75,curve:"linear")]
    for (canvas,field,show,change,commit,ramp) in [
      (pattern.canvas,pattern.pointValue,{pattern.showPoint()},{pattern.controlTextDidChange($0)},{pattern.setPoint()},{pattern.ramp(false);pattern.addPointAtCursor()}),
      (graph.canvas,graph.value,{graph.showPoint()},{graph.controlTextDidChange($0)},{graph.setPoint()},{graph.ramp()})
    ] as [(AutomationCanvas,NSTextField,()->Void,(Notification)->Void,()->Void,()->Void)] {
      canvas.frame=NSRect(x:0,y:0,width:600,height:240);canvas.points=original;canvas.selected=0;show()
      let root=NSView(frame:NSRect(x:0,y:0,width:600,height:300)),before=NSTextField(string:"Before"),after=NSTextField(string:"After")
      before.frame=NSRect(x:0,y:250,width:200,height:24);after.frame=NSRect(x:210,y:250,width:200,height:24)
      for view in [canvas,before,after] as [NSView]{root.addSubview(view)}
      let window=NSWindow(contentRect:root.frame,styleMask:[],backing:.buffered,defer:false);window.contentView=root
      before.nextKeyView=canvas;canvas.nextKeyView=after;after.nextKeyView=before
      field.stringValue="unfinished";change(Notification(name:NSControl.textDidChangeNotification,object:field));commit()
      let event=NSEvent.mouseEvent(with:.leftMouseDown,location:canvas.convert(canvas.location(original[1]),to:nil),modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,eventNumber:0,clickCount:1,pressure:1)!
      canvas.mouseDown(with:event);canvas.mouseUp(with:event)
      func press(_ code:UInt16,_ flags:NSEvent.ModifierFlags=[]){canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:flags,timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"",charactersIgnoringModifiers:"",isARepeat:false,keyCode:code)!)}
      window.makeFirstResponder(canvas);press(48)
      try require(after.currentEditor() != nil && window.firstResponder === after.currentEditor(),"Tab leaves a curve canvas with invalid point fields")
      window.makeFirstResponder(canvas);press(48,.shift)
      try require(before.currentEditor() != nil && window.firstResponder === before.currentEditor(),"Shift-Tab also leaves a curve canvas with invalid point fields")
      press(124);canvas.removeSelected();ramp()
      try require(canvas.selected==0 && canvas.points==original && field.stringValue=="unfinished","Invalid curve fields retain their original point through mouse, keyboard, delete and replacement actions")
      field.stringValue="40";change(Notification(name:NSControl.textDidChangeNotification,object:field));commit()
      try require(canvas.points[0].value==0.4 && canvas.points[1]==original[1],"Corrected curve fields apply only to the original point")
      press(48);try require(canvas.selected==1,"Point navigation resumes once fields are accepted")
    }
  }
  static func audioSettingsChecks() throws {
    let editor=AudioSettingsEditor(frame:.zero)
    var devices:[[String:Any]]=[["id":"first","name":"Duplicate name","default":true],["id":"second","name":"Duplicate name","default":false]]
    var writes=[[String:Any]]()
    editor.onRequest={method,params,reply in
      if method=="audio.settings.set"{writes.append(params);reply(["error":["message":"Output changed"]]);return}
      let data:[String:Any]=method=="audio.devices.get" ? ["devices":devices]:["audioRevision":"audio:1","endpoint":"second","periodFrames":256,"inspection":true]
      reply(["result":["revision":"audio:1","data":data]])
    }
    editor.load();try require(editor.device.numberOfItems==3 && editor.device.selectedItem?.representedObject as? String=="second","Duplicate audio names retain distinct endpoint IDs")
    editor.period.selectItem(at:4);editor.selection();devices.removeLast();editor.refresh()
    try require(editor.device.selectedItem?.representedObject as? String=="second" && editor.device.titleOfSelectedItem?.contains("Unavailable")==true && editor.period.indexOfSelectedItem==4,"Rescan retains a missing selected output and unsaved period")
    editor.apply();try require(writes.last?["endpoint"] as? String=="second" && writes.last?["periodFrames"] as? Int==512 && writes.last?["expectedAudioRevision"] as? String=="audio:1","Switch submits the complete captured settings revision")
    try require(editor.status.stringValue=="Output changed" && editor.device.selectedItem?.representedObject as? String=="second","Refused output changes retain the user's selection")
  }
  static func activityDraftChecks() throws {
    let editor=ParameterActivityEditor(frame:.zero)
    var revision="activity:1",stored:[[String:Any]]=[["frame":48000,"value":0.3],["frame":96000,"value":0.8]],writes=[[String:Any]](),answer:(([String:Any])->Void)?,finished:Bool?
    editor.onRequest={method,params,reply in
      if method=="automation.recorded.edit"{writes.append(params);answer=reply;return}
      reply(["result":["revision":revision,"data":["points":stored,"total":stored.count,"sampleRate":48000]]])
    }
    editor.inspectRecorded(plugin:"a",parameter:7);editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    editor.pointTime.stringValue="3";editor.pointValue.stringValue="0.4";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.pointTime));editor.finishAutomaticEdit{finished=$0}
    try require(writes.last?["frame"] as? Int==48000 && writes.last?["newFrame"] as? Int==144000 && writes.last?["expectedRevision"] as? String=="activity:1" && finished==nil,"Recorded fields capture the original point and read revision")
    var opened:[[String:Any]]=[];editor.onOpen={opened.append($0)}
    editor.trace.onSelect?(ParameterTraceSample(["value":0.3,"source":["kind":"recorded","frame":48000]])!)
    editor.openSelected()
    editor.trace.onSelect?(ParameterTraceSample(["value":0.8,"source":["kind":"recorded","frame":96000]])!)
    try require(opened.isEmpty,"Source navigation waits for the current recorded field write")
    editor.pointValue.stringValue="0.6";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.pointValue));revision="activity:2";answer?(["result":["revision":revision,"data":[:]]])
    RunLoop.current.run(until:Date().addingTimeInterval(0.35))
    try require(writes.count==2 && writes.last?["frame"] as? Int==144000 && writes.last?["value"] as? Double==0.6 && writes.last?["expectedRevision"] as? String==revision,"Newer raw fields follow the moved point at the acknowledged revision")
    revision="activity:3";stored=[["frame":96000,"value":0.8],["frame":144000,"value":0.6]];answer?(["result":["revision":revision,"data":[:]]])
    try require(finished==true && !editor.needsCompletion && editor.table.selectedRow==1,"Recorded Save completes after readback and retains the relocated point")
    try require(opened.count==1 && opened[0]["frame"] as? Int==48000,"Deferred source navigation retains its captured source while newer edits finish")
    editor.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false);editor.pointValue.stringValue="0.7";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.pointValue));editor.finishAutomaticEdit{finished=$0};answer?(["error":["message":"Refused"]]);let count=writes.count
    editor.finishAutomaticEdit{finished=$0};RunLoop.current.run(until:Date().addingTimeInterval(0.35))
    try require(finished==false && writes.count==count && editor.pointValue.stringValue=="0.7" && editor.needsCompletion,"A refused recorded write retains raw fields and never silently replays")
    editor.nextRecordedPage();editor.refreshCopies();editor.openSelected()
    try require(writes.count==count && editor.target?["plugin"] as? String=="a" && opened.count==1,"Paging, refresh and source navigation cannot discard or replay a refused recorded draft")
  }
  static func instrumentAutomaticChecks() throws {
    let editor=InstrumentEditor(frame:.zero),model=PatternModel(["revisionToken":"instrument:1:0:0","format":"IT","instruments":[["index":1,"id":"n20","name":"Lead"]]])
    let info:[String:Any]=["name":"Lead","volume":64,"pan":128,"fadeout":256,"envelopes":[["points":[[0,64],[10,0]],"enabled":true,"releaseNode":255,"carry":false],[:],[:]]]
    editor.update(info,model:model)
    var calls=[([String:Any],String,Int,String)](),answer:(([String:Any])->Void)?,finished:Bool?
    editor.onCommit={values,revision,index,identity,reply in calls.append((values,revision,index,identity));answer=reply}
    editor.name.stringValue="First";editor.finishAutomaticEdit{finished=$0}
    try require(editor.pending && calls.count==1 && calls[0].1==model.revisionToken && calls[0].3=="n20" && finished==nil,"Instrument Save waits on a captured identity and revision")
    editor.name.stringValue="Newer";answer?(["result":["revision":"instrument:2:0:0","data":[:]]])
    try require(calls.count==2 && calls.last?.0["name"] as? String=="Newer" && calls.last?.1=="instrument:2:0:0" && finished==nil,"An acknowledgement retains newer instrument text and finishes it before Save")
    answer?(["result":["revision":"instrument:3:0:0","data":[:]]]);try require(finished==true && !editor.hasDraft,"Instrument completion follows the newest accepted fields")
    editor.keymap[60]=3;editor.update(info,model:model)
    try require(editor.keymap[60]==3 && editor.hasDraft,"A passive instrument refresh retains a keymap draft")
    editor.keymap[60]=0
    editor.nodeTick.stringValue="unfinished";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.nodeTick));finished=nil;editor.finishAutomaticEdit{finished=$0}
    try require(finished==false && editor.nodeTick.stringValue=="unfinished" && calls.count==2,"Invalid instrument node text blocks departure and remains intact")
    editor.nodeTick.stringValue="0";editor.nodeValue.stringValue="32";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.nodeValue));editor.finishAutomaticEdit{finished=$0}
    answer?(["error":["message":"Song changed"]]);let refused=calls.count;editor.finishAutomaticEdit{finished=$0}
    try require(finished==false && calls.count==refused && editor.hasDraft,"A refused instrument write is not retried by Save")
  }
}
