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
    key(grid,124,"");try require(grid.column==6 && grid.currentCommandHelp.contains("Slide binding 1"),"Extra command has contextual timing help")
    key(grid,124,"");try require(grid.column==0 && grid.cursorChannel==1,"Arrow leaves the final extra subcolumn")
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
    try require(nudge.kind.indexOfSelectedItem==6 && nudgeCalls.count==1 && !nudge.plugin.isEnabled && nudge.duration.isEnabled && nudge.valueLabel.stringValue=="Nudge strength (%)","Nudge editor has strength and duration without plugin reads")
    nudge.value.stringValue="83.125";nudge.duration.stringValue="0.75";nudge.offset.stringValue="0.125";nudge.apply(dryRun:true)
    let push=(nudgeCalls.last!.1["commands"] as! [[String:Any]]).last!
    try require(push["kind"] as? String=="nudge-reverse" && push["value"] as? Double==0.83125 && push["duration"] as? Int==49152 && push["binding"] as? Int==0 && nudgeCalls.last!.1["bindings"]==nil,"Nudge saves exact strength and sub-row timing with no plugin binding")
    let displayed=NativePatternCommand(push)
    try require(displayed.code=="NR" && displayed.valueText=="83.1/0.75" && displayed.description.contains("reverse"),"Scratch commands have readable grid text and contextual help")
    let inlineGrid=PatternView();inlineGrid.frame=NSRect(x:0,y:0,width:800,height:400)
    inlineGrid.model=PatternModel(["revisionToken":"inline:1","channels":1,"rows":64,"effectColumns":[8],"performanceCommands":[push]])
    inlineGrid.cursorRow=3;inlineGrid.column=3
    // The saved fixture has a fractional onset. In-cell entry must retain it.
    try require(inlineGrid.beginNudgeEdit() && inlineGrid.nudgeEditor?.strength.stringValue=="83.125" && inlineGrid.nudgeEditor?.duration.stringValue=="0.75","Inline editor recovers strength and duration without rounding or opening a window")
    let inlineHost=NSWindow(contentRect:inlineGrid.bounds,styleMask:[.titled],backing:.buffered,defer:false);inlineHost.contentView=inlineGrid
    let inline=inlineGrid.nudgeEditor!
    try require(inlineGrid.accessibilityChildren()?.contains{($0 as? NSTextField)===inline.strength}==true,"Inline numeric fields remain accessible inside the custom Metal grid")
    var inlineCalls=[[String:Any]](),inlineReplies=[([String:Any])->Void]()
    inlineGrid.onNudgeRequest={p,r in inlineCalls.append(p);inlineReplies.append(r)}
    inline.strength.stringValue="50";inline.duration.stringValue="0.125"
    _=inline.control(inline.strength,textView:NSTextView(),doCommandBy:#selector(NSResponder.insertTab(_:)))
    try require(inline.duration.currentEditor() != nil,"Tab selects the in-pattern duration field")
    inline.duration.stringValue="0.125";inline.commit(advance:true);inline.commit(advance:true)
    let inlineCommand=inlineCalls[0]["command"] as! [String:Any]
    try require(inlineCalls.count==1 && inlineCommand["value"] as? Double==0.5 && inlineCommand["duration"] as? Int==8192 && inlineCommand["offset"] as? Int==8192 && inlineCalls[0]["expectedRevision"] as? String=="inline:1","One in-cell write preserves onset, exact fractional duration and captured revision")
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
}
