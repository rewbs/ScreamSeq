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
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count,let bitmap=editor.bitmapImageRepForCachingDisplay(in:editor.bounds){editor.cacheDisplay(in:editor.bounds,to:bitmap);if let png=bitmap.representation(using:.png,properties:[:]){let folder=URL(fileURLWithPath:CommandLine.arguments[index+1]);try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true);try png.write(to:folder.appendingPathComponent("ParameterActivityEditor.png"))}}
    editor.detailMode.selectedSegment=2;editor.changeDetail();answer(["points":[["frame":48000,"value":-40]],"total":1])
    editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    editor.editRecordedCell(row:0,column:1)
    try require(editor.table.editedRow==0 && editor.table.editedColumn==1,"Recorded values must open a real inline field editor")
    (host.firstResponder as? NSTextView)?.string="-30"
    host.makeFirstResponder(nil)
    try require(requests.last?.0=="automation.recorded.edit" && requests.last?.1["frame"] as? Int==48000 && requests.last?.1["value"] as? Double == -30,"Committing an inline recorded value preserves its time and edits the exact parameter")
    answer([:]);answer(["points":[["frame":48000,"value":-30]],"total":1])
    print("PASS parameter activity UI: stable target selection, source links, retired replies and trace zoom")
  }
}
