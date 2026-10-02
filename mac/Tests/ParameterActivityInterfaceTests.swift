import AppKit
extension InterfaceTests {
  static func parameterActivityChecks() throws {
    let editor=ParameterActivityEditor(frame:NSRect(x:0,y:0,width:900,height:700))
    var requests=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onRequest={method,p,reply in requests.append((method,p));replies.append(reply)}
    func answer(_ data:[String:Any],revision:String="r1"){replies.removeFirst()(["result":["revision":revision,"data":data]])}
    let targets:[[String:Any]]=[["key":"rack/a","name":"First","plugin":"a","graph":"n0"],["key":"rack/b","name":"Second","plugin":"b","graph":"n0"]]
    editor.inspect(plugin:"a",parameter:71);answer(["engine":1,"targets":targets]);answer(["parameters":[["id":71,"name":"Cutoff","min":20,"max":20000],["id":72,"name":"Gain","min":0,"max":1]]])
    try require(requests.last?.0=="parameter.activity.watch" && requests.last?.1["parameter"] as? Int==71,"Inspect opens the exact stable parameter")
    answer(["token":"1:1"]);answer(["sources":[["kind":"envelope","id":"n9","pattern":2,"plugin":"a","parameter":71,"title":"Pattern 2 envelope","enabled":true]],"rule":"Rule"])
    editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    var link:[String:Any]?;editor.onOpen={link=$0};editor.openSelected()
    try require(link?["pattern"] as? Int==2 && link?["parameter"] as? Int==71,"Source link retains pattern and parameter identity")
    editor.request("parameter.activity.get"){_ in fatalError("Stale target reply accepted")}
    editor.processor.selectItem(at:1);editor.selectProcessor();answer(["points":[],"token":"1:1"])
    try require(editor.targetKey=="rack/b" && editor.samples.isEmpty && editor.parameterID==nil,"Changing target retires stale capture and parameter catalog")
    editor.reloadTargets();answer(["engine":1,"targets":targets]);answer(["parameters":[["id":71,"name":"Different meaning","min":-60,"max":6]]]);answer(["token":"1:2"]);answer(["sources":[]])
    try require(editor.parameterID==71 && editor.parameters.first?["name"] as? String=="Different meaning","Same numeric parameter IDs cannot retain another processor's catalog")
    var fixture=[[String:Any]]();for i in 0..<1000 {var d:[String:Any]=["sequence":i+1,"frame":i*48,"seconds":Double(i)/1000,"value":Double(i%100)/100];d.merge(["minimum":0,"maximum":1,"pattern":0,"order":0,"position":Double(i)*16,"audible":true,"source":["kind":"envelope","id":"n9"]]){_,new in new};fixture.append(d)}
    let trace=ParameterTraceView(frame:NSRect(x:0,y:0,width:900,height:250));trace.samples=fixture.compactMap(ParameterTraceSample.init);trace.span=64;trace.zoom(2)
    trace.needsDisplay=false;trace.editRow=20;trace.playRow=24
    let cursors=(trace.layer?.sublayers ?? []).compactMap{$0 as? CAShapeLayer}
    try require(!trace.needsDisplay && cursors.count==2 && cursors.allSatisfy{!$0.isHidden},
      "Trace playheads update independently without repainting captured history")
    try require(abs(cursors[1].frame.minX-trace.location(24,0).x)<0.001,
      "Trace playheads use the zoomed row position")
    trace.playRow=100
    try require(cursors[1].isHidden,"Out-of-view trace playheads cannot escape the plot")
    try require(ParameterTraceSample(["value":0.5,"source":["kind":"pattern-set"]])?.continuous==false && ParameterTraceSample(["value":0.5,"source":["kind":"pattern-slide"]])?.continuous==true,"Instant parameter changes must draw steps, not invented ramps")
    try require(trace.end-trace.viewStart==32,"Trace zoom changes its visible span")
    if let rep=trace.bitmapImageRepForCachingDisplay(in:trace.bounds){trace.cacheDisplay(in:trace.bounds,to:rep)}
    trace.fit();try require(trace.viewStart==0 && trace.end==64,"Trace fit restores pattern context")
    editor.poll();answer(["target":["key":"rack/b"],"parameter":71,"token":"1:2","cursor":1000,"points":fixture,"active":false])
    try require(editor.samples.count==1000 && editor.reading.stringValue.contains("Pattern envelope"),"Capture replies populate the visible trace and source reading")
    RunLoop.current.run(until:Date().addingTimeInterval(0.12));editor.poll();answer(["target":["key":"rack/a"],"parameter":72,"token":"1:3","points":[]])
    try require(!editor.pending && editor.token=="1:2" && editor.status.stringValue.contains("Another client"),"The visible panel must not steal another API client's capture selection")
    if let first=editor.samples.first{editor.trace.onSelect?(first);let selected=editor.reading.stringValue;editor.updateTrace();try require(editor.reading.stringValue==selected,"Selected trace values remain available for inspection")}
    let host=NSWindow(contentRect:NSRect(x:0,y:0,width:1000,height:740),styleMask:[.titled],backing:.buffered,defer:false)
    host.isReleasedWhenClosed=false;host.contentView=editor;editor.frame=NSRect(x:0,y:0,width:1000,height:740);editor.layoutSubtreeIfNeeded()
    try require(editor.trace.bounds.height>=180 && editor.table.bounds.width>400,"Parameter trace and source table remain usable in the full panel")
    host.setContentSize(NSSize(width:370,height:860));editor.frame=NSRect(x:0,y:0,width:370,height:860);editor.layoutSubtreeIfNeeded()
    try require(editor.viewMode.bounds.width>=editor.viewMode.intrinsicContentSize.width-1 && editor.detailMode.bounds.width>=editor.detailMode.intrinsicContentSize.width-1 && editor.reading.bounds.width>=340,"A docked parameter panel keeps its mode labels and effective value readable without competing transport buttons")
    host.setContentSize(NSSize(width:1000,height:740));editor.frame=NSRect(x:0,y:0,width:1000,height:740);editor.layoutSubtreeIfNeeded()
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count,let bitmap=editor.bitmapImageRepForCachingDisplay(in:editor.bounds){editor.cacheDisplay(in:editor.bounds,to:bitmap);if let png=bitmap.representation(using:.png,properties:[:]){let folder=URL(fileURLWithPath:CommandLine.arguments[index+1]);try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true);try png.write(to:folder.appendingPathComponent("ParameterActivityEditor.png"))}}
    editor.detailMode.selectedSegment=2;editor.changeDetail();answer(["points":[["frame":48000,"value":-40]],"total":1])
    editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    try require(editor.status.stringValue.contains("during playback") && !editor.status.stringValue.contains("Stop playback"),"Recorded editor explains its supported live-edit behavior")
    editor.editRecordedCell(row:0,column:1)
    try require(editor.table.editedRow==0 && editor.table.editedColumn==1,"Recorded values must open a real inline field editor")
    (host.firstResponder as? NSTextView)?.string="-30"
    host.makeFirstResponder(nil)
    try require(requests.last?.0=="automation.recorded.edit" && requests.last?.1["frame"] as? Int==48000 && requests.last?.1["value"] as? Double == -30,"Committing an inline recorded value preserves its time and edits the exact parameter")
    answer([:]);answer(["points":[["frame":48000,"value":-30]],"total":1])
    try require(editor.table.selectedRow==0 && editor.pointTime.doubleValue==1 && editor.pointValue.doubleValue == -30,"Inline recorded edits refresh the selected point form even when its table row stays selected")
    editor.loadRecorded();answer(["points":[["frame":48000,"value":-40]],"total":1])
    try require(editor.pointValue.doubleValue == -40,"Recorded Undo refresh restores the selected form alongside the table")
    editor.loadRecorded();answer(["points":[["frame":48000,"value":-30],["frame":96000,"value":-20]],"total":2])
    editor.tableView(editor.table,setObjectValue:"3",for:editor.table.tableColumns[0],row:0)
    answer([:]);answer(["points":[["frame":96000,"value":-20],["frame":144000,"value":-30]],"total":2])
    try require(editor.table.selectedRow==1 && editor.pointTime.doubleValue==3 && editor.pointValue.doubleValue == -30,"Editing recorded time follows that exact point through table reordering")
    host.makeFirstResponder(editor.pointValue);(host.firstResponder as? NSTextView)?.string="-12.5"
    editor.loadRecorded();answer(["points":[["frame":96000,"value":-20],["frame":144000,"value":-31]],"total":2])
    try require((host.firstResponder as? NSTextView)?.string=="-12.5","Passive recorded refresh preserves an in-progress point-form text draft")
    host.makeFirstResponder(nil)
    editor.loadRecorded();editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    answer(["points":[["frame":96000,"value":-21],["frame":144000,"value":-31]],"total":2])
    try require(editor.table.selectedRow==0 && editor.pointTime.doubleValue==2 && editor.pointValue.doubleValue == -21,"A pending recorded read respects a newer selected point instead of restoring the old row")
    // Song-level contributors use stable source IDs, not recipe edge indices.
    editor.detailMode.selectedSegment=0
    editor.loadSources();answer(["sources":[
      ["kind":"graph-source","scope":"song","id":"n0","title":"Unmodulated base","enabled":true,"graph":NSNull()],
      ["kind":"graph-source","scope":"song","id":"n501","node":"n501","title":"Slow LFO","enabled":true,"graph":NSNull()],
      ["kind":"graph-source","scope":"song","id":"n502","node":"n502","title":"Disabled LFO","enabled":false,"graph":NSNull()]
    ]])
    editor.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false)
    editor.openSelectedMapping()
    try require(link?["node"] as? String=="n501" && link?["plugin"] as? String=="b" && link?["parameter"] as? Int==71 && link?["editConnection"] as? Bool==true,"Song mapping link retains stable source, plugin and parameter")
    let contribution=ParameterTraceSample(["value":0.3,"source":["kind":"graph-source","id":"n501"]])!
    let contributionLink=editor.link(contribution)
    try require(contributionLink["node"] as? String=="n501" && contributionLink["scope"] as? String=="song","Trace contribution resolves its source rather than the receiving processor")
    let aggregate=editor.link(ParameterTraceSample(["value":1,"source":["kind":"graph","id":"n0"]])!)
    try require(aggregate["scope"] as? String=="song" && aggregate["node"]==nil && aggregate["plugin"] as? String=="b","Final song value opens the target and all contributors")
    try require(editor.unclampedModulationValue==nil,"An incomplete contribution snapshot cannot invent a clamp total")
    RunLoop.current.run(until:Date().addingTimeInterval(0.12));editor.poll()
    answer(["target":["key":"rack/b"],"parameter":71,"token":"1:2","cursor":1005,"points":[
      ["sequence":1001,"value":0.8,"source":["kind":"graph-source","id":"n0"]],
      ["sequence":1002,"value":0.3,"source":["kind":"graph-source","id":"n501"]],
      ["sequence":1003,"value":0.9,"source":["kind":"graph-source","id":"n502"]],
      ["sequence":1004,"value":0.9,"source":["kind":"graph-source","id":"n999"]],
      ["sequence":1005,"value":1.0,"source":["kind":"graph","id":"n0"]]
    ]])
    try require(abs((editor.unclampedModulationValue ?? 0)-1.1)<0.00001,"Clamp accounting includes the base and only currently enabled contributions")
    try require(editor.tableView(editor.table,objectValueFor:editor.table.tableColumns[1],row:2) as? String=="Inactive","Disabled sources never display an old active contribution")
    editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false);link=nil;editor.openSelectedMapping()
    try require(link==nil && editor.status.stringValue.contains("contribution"),"The base has no modulation cable to edit")
    let copies=ParameterActivityEditor(frame:.zero)
    var copyRequests=[(String,[String:Any])](),copyReplies=[([String:Any])->Void]()
    copies.onRequest={method,p,reply in copyRequests.append((method,p));copyReplies.append(reply)}
    func copyAnswer(_ data:[String:Any]){copyReplies.removeFirst()(["result":["revision":"copy-r1","data":data]])}
    let duplicateTargets:[[String:Any]]=[
      ["key":"rack/first","name":"Compressor","plugin":"first","graph":"n0"],
      ["key":"rack/second","name":"Compressor","plugin":"second","graph":"n0"],
      ["key":"graph/5","name":"Channel · Compressor","graph":"n25","node":"n28","target":"n6","role":2,"instrument":"n0","channel":65535],
      ["key":"graph/6","name":"Channel · Compressor","graph":"n25","node":"n28","target":"n7","role":2,"instrument":"n0","channel":65535]
    ]
    copies.inspect(graph:"n25",node:"n28",parameter:1,copy:"n25/n7/ordinary//inspector")
    copyAnswer(["engine":1,"targets":duplicateTargets])
    try require(copies.processor.numberOfItems==4 && copies.processor.indexOfSelectedItem==3 && copies.targetKey=="graph/6","Repeated plugin and channel names preserve every stable target, and graph Inspect follows the exact observed copy")
    copyAnswer(["parameters":[["id":1,"name":"Threshold","min":-96,"max":0]]]);copyAnswer(["token":"copy:1"]);copyAnswer(["sources":[]])
    copies.processor.selectItem(at:1);copies.selectProcessor()
    try require(copyRequests.last?.1["target"] as? String=="rack/second","Selecting a duplicate-name menu item addresses its own stable processor identity")
    copyAnswer(["parameters":[["id":1,"name":"Threshold","min":-96,"max":0]]]);copyAnswer(["token":"copy:2"]);copyAnswer(["sources":[]])
    copies.request("parameter.activity.get"){_ in fatalError("Old processor capture survived a new explicit inspection")}
    copies.detailMode.selectedSegment=2
    copies.inspect(graph:"n25",node:"n28",parameter:1,copy:"n25/n99/ordinary//inspector")
    copyAnswer(["target":["key":"rack/second"],"parameter":1,"points":[]])
    try require(copies.targetKey.isEmpty && copies.parameters.isEmpty && copies.sources.isEmpty && copies.detailMode.selectedSegment==0,"A new graph inspection retires stale catalog/source replies and leaves rack-only recorded editing")
    copies.reloadTargets()
    copyAnswer(["engine":1,"targets":duplicateTargets])
    try require(copies.processor.indexOfSelectedItem == -1 && copies.reading.stringValue.contains("unavailable") && copyRequests.last?.0=="parameter.activity.targets","An unavailable explicitly observed copy cannot silently fall back to a different channel")
    try require(ParameterActivityEditor.copyKey(["graph":"n25","target":"n0","role":3,"instrument":"n9","channel":4])=="n25//instrument/n9/4","Instrument observation preserves instrument identity and raw channel")
    print("PASS parameter activity UI: stable target selection, source links, retired replies and trace zoom")
  }
}
