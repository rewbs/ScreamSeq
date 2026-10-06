import AppKit
extension InterfaceTests {
  static var performanceData:[String:Any] {
    ["pattern":0,"rows":64,"columns":[["channel":0,"count":2]],
     "bindings":[["id":1,"plugin":"gain","parameter":7,"name":"Filter motion","resolved":true,"canSlide":true]],
     "commands":[["channel":0,"track":"n2","position":16384,"duration":98304,"column":1,"kind":"parameter-slide","binding":1,"value":0.75]]]
  }
  static func performanceModel()->PatternModel {
    PatternModel(["channels":3,"rows":64,"effectColumns":[2,1,8],
      "performanceCommands":performanceData["commands"]!,"nativePlugins":[["instanceID":"gain","name":"Fixture gain"]],
      "patterns":[["index":0,"rows":64]]])
  }
  static func patternPerformanceFixture()->PatternPerformanceEditor {
    let editor=PatternPerformanceEditor(frame:.zero);editor.onContext={(performanceModel(),0,0,6)}
    editor.onRequest={method,_,reply in
      reply(["result":["revision":"song:1","data":method=="pattern.effects.get" ? performanceData as Any : [["id":7,"name":"Gain","canSlide":true]] as Any]])
    };editor.capture();editor.onRequest=nil;return editor
  }
  static func patternPerformanceChecks() throws {
    try patternInlineSlotChecks()
    try patternInlineExpansionChecks()
    try patternNudgeBeatDurationChecks()
    let modern=PatternPerformanceEditor(frame:.zero)
    modern.onContext={(performanceModel(),0,0,6)}
    var modernCalls=[String](),inlineTarget=[Any]()
    modern.onInlineEdit={inlineTarget=[$0,$1,$2,$3,$4]}
    var modernData=performanceData
    modernData["commands"]=[["kind":"native","native":"vibrato","parameters":["depth":0.375,"rate":2.0],"channel":0,"position":0,"column":1,"duration":0]]
    modern.onRequest={method,_,reply in modernCalls.append(method);reply(["result":["revision":"modern:1","data":modernData]])}
    modern.capture();modern.apply(dryRun:false);modern.apply(dryRun:true)
    try require(modern.requiresInlineEditor && modern.kind.indexOfSelectedItem == -1 && !modern.applyButton.isEnabled && !modern.value.isEnabled && modernCalls==["pattern.effects.get"],"Legacy floating editor never coerces a named native command into PS or fetches a false parameter target")
    modern.editInline()
    try require(inlineTarget.count==5 && inlineTarget[0] as? Int==0 && inlineTarget[3] as? Int==5 && inlineTarget[4] as? String=="modern:1","Floating native effect bridge retains exact cell and revision for aligned inline editing")
    modern.kind.selectItem(at:7);modern.changedKind();modern.apply(dryRun:true)
    try require(modernCalls.last=="pattern.effects.set","Only an explicit replacement or clear enables writes for a modern native effect")
    let editor=PatternPerformanceEditor(frame:.zero);var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onContext={(performanceModel(),0,0,6)}
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    editor.capture();editor.capture()
    try require(calls.count==1 && editor.pending && !editor.applyButton.isEnabled,"Pending command read cannot duplicate or apply")
    replies.removeFirst()(["result":["revision":"song:1","data":performanceData]])
    try require(calls.count==2 && calls.last!.0=="plugin.parameters.get","Binding resolves the selected plugin parameter list")
    replies.removeFirst()(["result":["revision":"song:1","data":[["id":7,"name":"Gain","canSlide":true]]]])
    try require(editor.effectColumn.selectedTag()==1 && editor.value.stringValue=="75" && editor.offset.stringValue=="0.25" && editor.duration.stringValue=="1.5","Command editor recovers fractional timing and full value")
    editor.value.stringValue="NaN";editor.apply(dryRun:false)
    try require(calls.count==2,"Nonfinite values cannot reach API")
    editor.value.stringValue="90";editor.apply(dryRun:true);editor.apply(dryRun:false)
    let request=calls.last!.1,command=(request["commands"] as! [[String:Any]])[0]
    try require(calls.count==3 && request["expectedRevision"] as? String=="song:1" && request["dryRun"] as? Bool==true &&
      command["track"]==nil && command["column"] as? Int==1 && command["position"] as? Int==16384 && command["duration"] as? Int==98304,
      "Preview sends exact native timing, binding and captured revision without duplicate submission")
    replies.removeFirst()(["error":["message":"Song changed"]])
    try require(editor.revision=="song:1" && editor.value.stringValue=="90","Stale rejection retains the user's draft and old revision")
    editor.kind.selectItem(at:7);editor.apply(dryRun:false)
    try require((calls.last!.1["commands"] as? [[String:Any]])?.isEmpty==true && calls.last!.1["bindings"]==nil,"Clear removes only selected cell without modifying bindings")
    replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    try require(editor.commands.isEmpty && editor.revision=="song:2","Saved clear updates captured command data")
    editor.kind.selectItem(at:3);editor.changedKind();editor.value.stringValue="-7.125";editor.pitchRange.stringValue="12";editor.duration.stringValue="0.125";editor.apply(dryRun:true)
    let bend=(calls.last!.1["commands"] as! [[String:Any]])[0]
    try require(bend["kind"] as? String=="pitch-slide" && bend["binding"] as? Int==0 && bend["pitchRange"] as? Int==12 && bend["duration"] as? Int==8192 && calls.last!.1["bindings"]==nil,"Pitch editor retains signed semitones, fine duration and explicit plugin wheel range without inventing a parameter binding")
    replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:720,height:400);grid.model=performanceModel();grid.column=4
    key(grid,124,"");try require(grid.column==5 && grid.cursorChannel==0,"Right arrow enters first extra effect subcolumn")
    key(grid,124,"");try require(grid.column==6 && grid.currentCommandHelp.contains("Slide parameter"),"Extra command has contextual timing help")
    key(grid,124,"");key(grid,124,"");key(grid,124,"");try require(grid.column==0 && grid.cursorChannel==1,"Arrow visits each parameter then leaves the final extra subcolumn")
    grid.cursorChannel=2;grid.column=18;grid.revealCursor()
    try require(grid.channelX(2)+grid.fieldOffset(18)+grid.fieldWidth(18)<=Float(grid.bounds.width),"Last effect remains reachable inside a very wide track")
    var opened=0,cleared=[Int]();grid.onEffectPicker={opened += 1};grid.onClearNativeEffect={cleared=[$0,$1,$2]}
    key(grid,36,"\r");key(grid,51,"")
    try require(opened==1 && cleared==[0,2,7],"Return and Delete operate on the precise extra effect cell")
    grid.cursorChannel=0;grid.column=6;grid.firstChannel=0;grid.revealCursor()
    if let argument=CommandLine.arguments.firstIndex(of:"--snapshots"),argument+1<CommandLine.arguments.count {
      let directory=URL(fileURLWithPath:CommandLine.arguments[argument+1],isDirectory:true)
      try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
      let snapshot=PatternView();snapshot.frame=grid.frame;snapshot.model=performanceModel();snapshot.column=6
      guard let image=snapshot.offscreenImage(),let bytes=NSBitmapImageRep(cgImage:image).representation(using:.png,properties:[:]) else{throw NSError(domain:"Offscreen effect grid",code:1)}
      try bytes.write(to:directory.appendingPathComponent("PatternEffectsGrid.png"))
    }
    let fresh=PatternPerformanceEditor(frame:.zero)
    fresh.onContext={(performanceModel(),1,1,3)}
    fresh.onRequest={method,_,reply in reply(["result":["revision":"song:1","data":method=="pattern.effects.get" ? performanceData as Any : [["id":7,"name":"Gain","canSlide":true]] as Any]])}
    fresh.capture()
    try require(fresh.columns.selectedTag()==1,"First native effect automatically provisions a column on an unconfigured channel")
    fresh.duration.stringValue="0";fresh.kind.selectItem(at:1);fresh.changedKind()
    try require(Double(fresh.duration.stringValue)==1,"Changing an immediate set into a slide supplies a usable duration")
    let precise=0.12345678912345678
    let raw=NativePatternCommand(["kind":"parameter-slide","binding":1,"value":precise])
    try require(raw.text.hasPrefix("PL01") && raw.value==precise,"Two-character effect display never quantizes the stored target")
    let cut=PatternPerformanceEditor(frame:.zero);cut.requestedKind="note-cut"
    cut.onContext={(performanceModel(),2,0,5)}
    var cutRequests=[(String,[String:Any])]()
    cut.onRequest={method,params,reply in cutRequests.append((method,params));reply(["result":["revision":"cut:1","data":performanceData]])}
    cut.capture();try require(cut.kind.indexOfSelectedItem==4 && !cut.plugin.isEnabled && !cut.value.isEnabled && cutRequests.count==1,"NC needs no plugin parameter read or binding")
    let cutHost=NSWindow(contentRect:NSRect(x:0,y:0,width:700,height:780),styleMask:[.titled],backing:.buffered,defer:false);cutHost.contentView=cut
    cutHost.makeFirstResponder(cut.offset);(cut.offset.currentEditor() as? NSTextView)?.string="0.5"
    cut.offsetUnits.selectItem(at:1);cut.changedOffsetUnit()
    try require(Double(cut.offset.stringValue)==0.125,"Half a row at four rows per beat becomes one eighth beat")
    cut.offset.stringValue="0.03125";cut.apply(dryRun:true)
    let cutCommand=(cutRequests.last!.1["commands"] as! [[String:Any]]).last!
    try require(cutCommand["kind"] as? String=="note-cut" && cutCommand["position"] as? Int==2*65536+8192 && cutCommand["duration"] as? Int==0 && cutRequests.last!.1["bindings"]==nil,"NC preserves precise beat offset and needs no binding")
    cut.offset.stringValue="0.25";let beforeCut=cutRequests.count;cut.apply(dryRun:false)
    try require(cutRequests.count==beforeCut,"NC cannot escape the row in beat mode")
    let nudge=PatternPerformanceEditor(frame:.zero);nudge.requestedKind="nudge-reverse"
    nudge.onContext={(performanceModel(),3,0,3)}
    var nudgeCalls=[(String,[String:Any])]()
    nudge.onRequest={method,params,reply in nudgeCalls.append((method,params));reply(["result":["revision":"nudge:1","data":performanceData]])}
    nudge.capture()
    try require(nudge.kind.indexOfSelectedItem==6 && nudgeCalls.count==1 && !nudge.plugin.isEnabled && nudge.duration.isEnabled && nudge.valueLabel.stringValue=="Nudge strength (%)" && nudge.durationLabel.stringValue=="Duration (beats)" && nudge.duration.stringValue=="1" && nudge.offsetUnits.indexOfSelectedItem==1,"Nudge editor defaults both duration and offset display to beats, without plugin reads")
    nudge.offsetUnits.selectItem(at:0);nudge.changedOffsetUnit();nudge.offset.stringValue="0.125"
    nudge.kind.selectItem(at:0);nudge.changedKind();nudge.kind.selectItem(at:6);nudge.changedKind()
    try require(nudge.offsetUnits.indexOfSelectedItem==1 && Double(nudge.offset.stringValue)==0.03125,
      "Changing a command into NR converts the existing 0.125-row offset into 0.03125 beats without losing it")
    nudge.value.stringValue="83.125";nudge.duration.stringValue="0.75";nudge.apply(dryRun:true)
    let push=(nudgeCalls.last!.1["commands"] as! [[String:Any]]).last!
    try require(push["kind"] as? String=="nudge-reverse" && push["value"] as? Double==0.83125 && push["durationBeats"] as? Double==0.75 && push["duration"]==nil && push["position"] as? Int==3*65536+8192 && push["binding"] as? Int==0 && nudgeCalls.last!.1["bindings"]==nil,"Nudge saves exact beat duration and the unchanged 8192-unit onset with no row-duration field or plugin binding")
    let displayed=NativePatternCommand(push)
    try require(displayed.code=="NR" && displayed.valueText=="83.1% · 0.75b" && displayed.description.contains("beats") && !displayed.description.contains("strength / rows") && displayed.description.contains("reverse"),"Scratch commands have readable grid text and contextual help")
    let inlineGrid=PatternView();inlineGrid.frame=NSRect(x:0,y:0,width:800,height:400)
    inlineGrid.model=PatternModel(["revisionToken":"inline:1","channels":1,"rows":64,"effectColumns":[8],"performanceCommands":[push]])
    inlineGrid.cursorRow=3;inlineGrid.column=3;inlineGrid.timingUnit = .beats
    // The saved fixture has a fractional onset. In-cell entry must retain it.
    try require(inlineGrid.beginNudgeEdit() && inlineGrid.nudgeEditor?.strength.stringValue=="83.125" && inlineGrid.nudgeEditor?.duration.stringValue=="0.75","Inline editor recovers strength and duration without rounding or opening a window")
    let inlineHost=NSWindow(contentRect:inlineGrid.bounds,styleMask:[.titled],backing:.buffered,defer:false);inlineHost.contentView=inlineGrid
    let inline=inlineGrid.nudgeEditor!
    try require(inlineGrid.accessibilityChildren()?.contains{($0 as? NSTextField)===inline.strength}==true,"Inline numeric fields remain accessible inside the custom Metal grid")
    var inlineCalls=[[String:Any]](),inlineReplies=[([String:Any])->Void]()
    inlineGrid.onNudgeRequest={p,r in inlineCalls.append(p);inlineReplies.append(r)}
    inline.strength.stringValue="50";inline.duration.stringValue="0.03125"
    _=inline.control(inline.strength,textView:NSTextView(),doCommandBy:#selector(NSResponder.insertTab(_:)))
    try require(inline.duration.currentEditor() != nil,"Tab selects the in-pattern duration field")
    inline.duration.stringValue="0.03125";inline.commit(advance:true);inline.commit(advance:true)
    let inlineCommand=inlineCalls[0]["command"] as! [String:Any]
    try require(inlineCalls.count==1 && inlineCommand["value"] as? Double==0.5 && inlineCommand["durationBeats"] as? Double==0.03125 && inlineCommand["duration"]==nil && inlineCommand["offset"] as? Int==8192 && inlineCalls[0]["expectedRevision"] as? String=="inline:1","One in-cell write preserves onset, exact fractional duration and captured revision")
    inlineGrid.model.revisionToken="inline:2"
    inlineReplies.removeFirst()(["error":["message":"Song changed"]])
    try require(inlineGrid.nudgeEditor===inline && inline.strength.stringValue=="50" && inline.revision=="inline:1","A stale inline edit retains its draft without rebasing onto a different song")
    inline.strength.stringValue="NaN";inline.commit(advance:true);inline.strength.stringValue="50";inline.duration.stringValue="1000";inline.commit(advance:true)
    try require(inlineCalls.count==1,"Invalid or out-of-pattern scratch values cannot reach the API")
    _=inline.control(inline.duration,textView:NSTextView(),doCommandBy:#selector(NSResponder.cancelOperation(_:)))
    try require(inlineGrid.nudgeEditor==nil && inlineGrid.cursorRow==3,"Escape cancels the draft without moving or writing")
    inlineGrid.column=18;_=inlineGrid.beginNudgeEdit(kind:"nudge-forward")
    inlineGrid.nudgeEditor!.commit(advance:true)
    try require(inlineCalls.last?["column"] as? Int==7,"Inline scratch values work in all eight equal FX columns")
    inlineReplies.removeFirst()(["result":["revision":"inline:3"]])
    try require(inlineGrid.nudgeEditor==nil && inlineGrid.cursorRow==4,"Return saves and advances exactly one edit step")
    let navigation=EditorNavigation()
    let moved=try navigation.prepared(["expectedRevision":"r","expectedContext":navigation.token,"channel":2,"column":12],
      revision:"r",contextToken:navigation.token,patterns:[["index":0,"rows":64]],channels:3,effectColumns:[2,1,8])
    try require(moved.column==12,"Agent context navigation reaches every effect subcolumn")
  }
  static func patternNudgeBeatDurationChecks() throws {
    let exact=0.12345678912345678
    let raw:[String:Any]=["kind":"nudge-forward","channel":0,"column":0,"position":65536+8192,"value":0.83125,"durationBeats":exact]
    let stored=NativePatternCommand(raw),defaultCommand=NativePatternCommand(["kind":"nudge-reverse"])
    try require(stored.duration==0 && stored.durationBeats==exact && stored.dictionary["duration"]==nil && stored.editCommand["duration"]==nil &&
      defaultCommand.durationBeats==1 && defaultCommand.editCommand["durationBeats"] as? Double==1,
      "NF/NR carry beat duration directly; omitted duration defaults to one beat without a row-duration payload")
    let model=PatternModel(["rows":6,"channels":1,"displayRowsPerBeat":3,"revisionToken":"beat:1","performanceCommands":[raw]])
    let schema=model.commands.schema(kind:"nudge-forward",native:nil)!
    try require(schema.inlineFields.map(\.storage)==["value","durationBeats"] && schema.inlineFields[1].unit=="beats" &&
      (schema.inlineFields[1].defaultValue as? Double)==1,"Fallback nudge slots match the beat-duration catalogue contract")
    let target=EditorNavigation(pattern:0,row:1,channel:0,column:4)
    let rowsEditor=PatternNudgeEditor(model:model,target:target,schema:schema,timing:.rows)
    try require(Double(rowsEditor.duration.stringValue)==exact*3,"Rows display uses the actual three rows per beat")
    var writes=[[String:Any]](),replies=[([String:Any])->Void]()
    rowsEditor.onRequest={writes.append($0);replies.append($1)}
    rowsEditor.strength.stringValue="50";rowsEditor.commit(advance:false);rowsEditor.commit(advance:false)
    let untouched=writes[0]["command"] as! [String:Any]
    try require(writes.count==1 && untouched["durationBeats"] as? Double==exact && untouched["duration"]==nil && untouched["offset"] as? Int==8192,
      "Changing only strength in Rows mode preserves every bit of the untouched beat duration in one guarded write")
    replies.removeFirst()(["error":["message":"Revision changed"]])
    try require(rowsEditor.strength.stringValue=="50" && rowsEditor.revision=="beat:1","Rejected beat-duration drafts keep their text and captured revision")
    rowsEditor.duration.stringValue="0.375";rowsEditor.commit(advance:false)
    try require((writes.last!["command"] as! [String:Any])["durationBeats"] as? Double==0.125,
      "Editing fractional rows divides by rows per beat without row-unit rounding")
    replies.removeFirst()(["result":["revision":"beat:2"]])
    for invalid in ["0","-1","NaN","1000"] {rowsEditor.duration.stringValue=invalid;rowsEditor.commit(advance:false)}
    try require(writes.count==2,"Zero, negative, nonfinite and beyond-pattern nudge durations are rejected before writing")
    let beatsEditor=PatternNudgeEditor(model:model,target:target,schema:schema,timing:.beats)
    beatsEditor.onRequest={writes.append($0);$1(["result":["revision":"beat:3"]])}
    let newExact=0.2718281828459045
    beatsEditor.duration.stringValue=String(format:"%.17g",newExact);beatsEditor.commit(advance:false)
    try require((writes.last!["command"] as! [String:Any])["durationBeats"] as? Double==newExact,
      "A fractional beat edit stays a double instead of rounding to 1/65536 row")

    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:800,height:400);grid.model=model;grid.cursorRow=5;grid.column=4;grid.timingUnit = .beats
    try require(grid.beginNudgeEdit(kind:"nudge-reverse") && Double(grid.nudgeEditor!.duration.stringValue)==1.0/3,
      "A new nudge near the pattern end clamps its one-beat default to the remaining beats")
    grid.nudgeEditor?.onFinish?(false)
    var endRaw=raw;endRaw["position"]=5*65536+32768;endRaw["durationBeats"]=0.1
    grid.model=PatternModel(["rows":6,"channels":1,"displayRowsPerBeat":3,"revisionToken":"beat:4","performanceCommands":[endRaw]])
    var cleared:[String:Any]=[:],message=""
    grid.onNudgeRequest={params,reply in cleared=params["command"] as! [String:Any];reply(["result":["revision":"beat:5"]])}
    grid.onMessage={message=$0};grid.parameterIndex=1;key(grid,47,".")
    try require(cleared["durationBeats"] as? Double==1.0/6 && cleared["value"] as? Double==0.83125 && cleared["offset"] as? Int==32768 && message.contains("Undo"),
      "Duration clear resets only that field, clamps to the remaining fraction of a beat and retains normal Undo feedback")
    // A history refresh supplies the restored document snapshot, not a new edit.
    grid.model=PatternModel(["rows":6,"channels":1,"displayRowsPerBeat":3,"revisionToken":"beat:6","performanceCommands":[endRaw]])
    _=grid.beginNudgeEdit()
    try require(Double(grid.nudgeEditor!.duration.stringValue)==0.1 && grid.nudgeEditor!.revision=="beat:6",
      "An Undo snapshot restores its exact beat duration when the inline editor reopens")
    grid.nudgeEditor?.onFinish?(false)

    let inspector=PatternPerformanceEditor(frame:.zero);inspector.onContext={(model,1,0,4)}
    var inspectorWrites=[[String:Any]]()
    inspector.onRequest={method,params,reply in
      if method=="pattern.effects.set"{inspectorWrites.append(params)}
      reply(["result":["revision":"beat:1","data":["pattern":0,"columns":[["channel":0,"count":1]],"commands":[raw]]]])
    }
    inspector.capture()
    try require(inspector.offsetUnits.indexOfSelectedItem==1 && abs((Double(inspector.offset.stringValue) ?? -1)-1.0/24)<1e-15,
      "Loading NF displays the saved one-eighth-row offset in beats using the actual three-row signature")
    inspector.durationUnits.selectItem(at:1);inspector.changedDurationUnit()
    try require(inspector.durationLabel.stringValue=="Duration (rows)" && Double(inspector.duration.stringValue)==exact*3,
      "The nudge inspector offers Rows explicitly with the captured pattern signature")
    inspector.value.stringValue="50";inspector.apply(dryRun:true)
    let inspectorCommand=(inspectorWrites[0]["commands"] as! [[String:Any]])[0]
    try require(inspectorCommand["durationBeats"] as? Double==exact && inspectorCommand["duration"]==nil && inspectorCommand["position"] as? Int==65536+8192,
      "An untouched inspector duration and row-unit onset retain their exact storage across beat/row display conversion")
    inspector.durationUnits.selectItem(at:0);inspector.changedDurationUnit()
    try require(inspector.durationLabel.stringValue=="Duration (beats)" && Double(inspector.duration.stringValue)==exact,
      "Returning to Beats preserves the captured double and truthful duration label")
  }
  static func patternInlineExpansionChecks() throws {
    let schema:[String:Any]=["kind":"native","native":"fixture","displayCode":"VX","parameters":[
      ["key":"depth","storage":"parameters.depth","type":"number","default":0.0],
      ["key":"reset","storage":"parameters.reset","type":"boolean","default":true,"inline":false],
      ["key":"duration","storage":"duration","type":"integer","unit":"row-units","minimum":1,"maximum":1000000,"default":65536]]]
    let exact=0.12345678912345678
    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:900,height:320)
    grid.model=PatternModel(["rows":16,"channels":1,"revisionToken":"expand:1","commandCatalog":["native":[schema]],"performanceCommands":[["kind":"native","native":"fixture","channel":0,"column":0,"position":8192,"duration":65536,"parameters":["depth":exact,"reset":true]]]])
    grid.column=4;grid.parameterIndex=1;grid.timingUnit = .beats
    try require(grid.beginNudgeEdit(),"Start visible native fields")
    let compact=grid.nudgeEditor!;compact.duration.stringValue="0.5"
    var calls=[[String:Any]](),reply:(([String:Any])->Void)?
    grid.onNudgeRequest={p,r in calls.append(p);reply=r}
    try require(grid.beginNudgeEdit(allParameters:true),"Edit all expands an existing draft")
    let expanded=grid.nudgeEditor!
    try require(expanded !== compact && expanded.descriptors.count==3 && expanded.duration.stringValue=="0.5" && grid.selectedParameterIndex==2 && calls.isEmpty && expanded.revision=="expand:1","Expansion preserves unsaved text, selected semantic field and captured revision without saving")
    expanded.fields[1].stringValue="off";expanded.commit(advance:false)
    let command=calls[0]["command"] as! [String:Any],parameters=command["parameters"] as! [String:Any]
    try require(calls.count==1 && command["duration"] as? Int==131072 && command["offset"] as? Int==8192 && parameters["depth"] as? Double==exact && parameters["reset"] as? Bool==false,"Expanded draft makes one guarded edit and preserves untouched full precision")
    reply?(["result":["revision":"expand:2"]])
    grid.column=4;_=grid.beginNudgeEdit();let pending=grid.nudgeEditor!;pending.commit(advance:false)
    try require(!grid.beginNudgeEdit(allParameters:true) && grid.nudgeEditor===pending && calls.count==2,"Pending inline writes cannot replace their captured editor during expansion")
    reply?(["error":["message":"stale"]]);pending.onFinish?(false)
    let rate=PatternEffectField(["key":"rate","unit":"cycles-per-beat-or-hz","width":8])
    try require(rate.text(2.0,model:grid.model,timing:.beats,rateMode:"beat")=="2/b" && rate.text(2.0,model:grid.model,timing:.rows,rateMode:"beat")=="0.5/r" && rate.text(2.0,model:grid.model,timing:.rows,rateMode:"hz")=="2Hz" && rate.help(model:grid.model,timing:.beats,rateMode:"hz").contains("Hz"),"Compact rate slots identify the actual beat or Hz mode even when its selector is advanced")
    let rateSchema=PatternEffectSchema(["kind":"native","native":"rate-fixture","parameters":[["key":"rate","storage":"parameters.rate","unit":"cycles-per-beat-or-hz","minimum":0,"maximum":1000,"default":2.0]]])
    let rateModel=PatternModel(["rows":8,"channels":1,"displayRowsPerBeat":3,"revisionToken":"rate:1","performanceCommands":[["kind":"native","native":"rate-fixture","channel":0,"column":0,"position":0,"parameters":["rate":2.0,"rateMode":"beat"]]]])
    let rateEditor=PatternNudgeEditor(model:rateModel,target:EditorNavigation(pattern:0,row:0,channel:0,column:4),schema:rateSchema,timing:.rows)
    var rateEdit:[String:Any]=[:];rateEditor.onRequest={p,_ in rateEdit=p};rateEditor.fields[0].stringValue="0.5";rateEditor.commit(advance:false)
    try require(((rateEdit["command"] as? [String:Any])?["parameters"] as? [String:Any])?["rate"] as? Double==1.5,"Rows-mode inline rate edits convert cycles per row to stored cycles per beat using the actual signature")
    grid.model=PatternModel(["channels":1,"rows":8])
    try require(grid.channelHeaderLabel(0)=="CH 01","Unnamed channel header fits completely before FX labels")
    grid.model=PatternModel(["channels":1,"rows":8,"tracks":[["name":"AAAAAAAA"]]])
    guard let firstImage=grid.offscreenImage() else {throw NSError(domain:"Pattern header first image",code:1)}
    grid.model.tracks=[["name":"MMMMMMMM"]]
    guard let secondImage=grid.offscreenImage() else {throw NSError(domain:"Pattern header second image",code:1)}
    let first=NSBitmapImageRep(cgImage:firstImage),second=NSBitmapImageRep(cgImage:secondImage)
    let scale=Double(first.pixelsWide)/Double(grid.bounds.width)
    var identical=true
    for y in Int(Double(grid.headerHeight-28)*scale)..<Int(Double(grid.headerHeight)*scale) {
      for x in Int(Double(grid.channelX(0)+grid.model.effectOffset(0,0))*scale)..<Int(Double(grid.channelX(0)+grid.model.channelWidth(0))*scale) {
        let a=first.colorAt(x:x,y:y),b=second.colorAt(x:x,y:y)
        identical = identical && a==b
      }
    }
    try require(identical,"Long channel names cannot paint over FX headings or the add-column control")
  }
  static func patternInlineSlotChecks() throws {
    let precise=0.12345678912345678
    let native:[String:Any] = ["kind":"native","native":"test-vibrato","displayCode":"VX","name":"Precise vibrato","parameters":[
      ["key":"depth","storage":"parameters.depth","name":"Depth","type":"number","unit":"semitones","minimum":0,"maximum":12,"default":0.5,"width":7],
      ["key":"shape","storage":"parameters.shape","name":"Shape","type":"choice","choices":["sine","triangle"],"default":"sine","width":8],
      ["key":"reset","storage":"parameters.reset","name":"Reset phase","type":"boolean","default":true,"width":4]]]
    let model=PatternModel(["rows":16,"channels":2,"displayRowsPerBeat":3,"effectColumns":[2,1],"revisionToken":"slots:1",
      "commandCatalog":["native":[native]],"effectBindings":[["id":7]],"performanceCommands":[
      ["channel":0,"column":0,"position":65536+8192,"kind":"nudge-forward","value":precise,"durationBeats":1.0],
      ["channel":0,"column":0,"position":3*65536,"kind":"parameter-slide","binding":7,"value":precise,"duration":65536],
      ["channel":0,"column":1,"position":65536,"kind":"native","native":"test-vibrato","parameters":["depth":0.375,"shape":"sine","reset":true]],
      ["channel":0,"column":1,"position":4*65536,"kind":"pitch-slide","value":-2.125,"duration":65536,"pitchRange":12]]])
    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:1000,height:430);grid.model=model;grid.timingUnit = .beats
    grid.cursorRow=1;grid.cursorChannel=0;grid.column=4
    let width=grid.model.channelWidth(0),offset=grid.model.channelOffset(1)
    try require(grid.model.channelWidth(1)==178 && width>178 && grid.model.effectLayout(0,0).slotWidths.count==3,
      "Empty and tracker FX columns use compact widths; only used multi-parameter schemas reserve aligned slots")
    key(grid,124,"");try require(grid.column==4 && grid.parameterIndex==1 && grid.currentCommandHelp.contains("Duration") && grid.currentCommandHelp.contains("beats"),"Right selects duration without treating it as another FX column")
    grid.cursorRow=3;try require(grid.model.channelWidth(0)==width && grid.model.channelOffset(1)==offset,"Moving to another command never resizes an FX column")
    grid.cursorRow=1
    let durationX=grid.fieldOffset(4,slot:1)
    try require(grid.fieldAt(durationX,0)==4 && grid.parameterAt(durationX,0,1,0)==1,"Paint and mouse hit geometry identify the same native duration slot")
    try require(grid.beginNudgeEdit() && grid.nudgeEditor?.duration.stringValue=="1","Native timing defaults to beats using the actual three-rows-per-beat signature")
    let editor=grid.nudgeEditor!;var calls=[[String:Any]](),replies=[([String:Any])->Void]()
    grid.onNudgeRequest={p,r in calls.append(p);replies.append(r)}
    editor.duration.stringValue="0.5";editor.commit(advance:false)
    let edit=calls[0]["command"] as! [String:Any]
    try require(edit["durationBeats"] as? Double==0.5 && edit["duration"]==nil && edit["value"] as? Double==precise && edit["offset"] as? Int==8192,
      "Editing a beat duration preserves the double and untouched full-precision strength and onset")
    replies.removeFirst()(["result":["revision":"slots:2"]])
    grid.timingUnit = .rows;grid.column=4;grid.parameterIndex=1;_=grid.beginNudgeEdit()
    try require(grid.nudgeEditor?.duration.stringValue=="3","Rows remains available as an explicit timing display preference")
    grid.nudgeEditor?.onFinish?(false)
    grid.column=6;grid.parameterIndex=0;_=grid.beginNudgeEdit()
    let generic=grid.nudgeEditor!;generic.fields[0].stringValue="0.8125";generic.fields[1].stringValue="triangle";generic.fields[2].stringValue="off";generic.commit(advance:true)
    let command=calls.last!["command"] as! [String:Any],parameters=command["parameters"] as! [String:Any]
    try require(command["native"] as? String=="test-vibrato" && parameters["depth"] as? Double==0.8125 && parameters["shape"] as? String=="triangle" && parameters["reset"] as? Bool==false && calls.count==2,
      "Shared descriptors drive numeric, choice and boolean fields in one native command transaction")
    replies.removeFirst()(["error":["message":"revision changed"]])
    try require(grid.nudgeEditor===generic && generic.fields[1].stringValue=="triangle" && generic.revision=="slots:1","Rejected generic drafts stay pinned and intact")
    generic.onFinish?(false)
    grid.cursorRow=3;grid.column=4;grid.parameterIndex=0;key(grid,47,".")
    let bindingClear=calls.last!["command"] as! [String:Any]
    try require(bindingClear["binding"] as? Int==7 && bindingClear["value"] as? Double==precise,"Clearing a binding selects an existing declared binding and preserves the other scalar")
    replies.removeFirst()(["result":["revision":"slots:2"]])
    grid.cursorRow=1;grid.column=4;grid.parameterIndex=1;key(grid,47,".")
    let durationClear=calls.last!["command"] as! [String:Any]
    try require(durationClear["durationBeats"] as? Double==1 && durationClear["duration"]==nil && durationClear["value"] as? Double==precise,"Dot resets only a positive-only duration to its catalogue default")
    replies.removeFirst()(["result":["revision":"slots:3"]])
    grid.selectRegion(from:(0,0),to:(15,1));grid.playPattern=grid.model.pattern;grid.playRow=4
    guard let image=grid.offscreenImage() else {throw NSError(domain:"Pattern channel rail readback",code:1)}
    let bitmap=NSBitmapImageRep(cgImage:image),x=Int(grid.channelX(1)*2)
    let colors=[0,1,3,4].compactMap{row in bitmap.colorAt(x:x,y:Int((grid.headerHeight+Float(row)*grid.rowHeight+2)*2))}
    try require(colors.count==4 && colors.allSatisfy{abs($0.redComponent-colors[0].redComponent)<0.005 && $0.redComponent>0.25},
      "Channel rails remain visible and identical through measure, ordinary, beat, selection and playhead fills")
  }
}
