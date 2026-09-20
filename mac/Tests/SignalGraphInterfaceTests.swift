import AppKit
extension InterfaceTests {
  static func signalGraphChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let data:[String:Any]=["library":[["id":"n100","number":1,"name":"Motion","nodes":[
      ["id":"n101","kind":"input","name":"Input","x":30.0,"y":60.0],
      ["id":"n102","kind":"plugin","name":"Filter","x":300.0,"y":60.0],
      ["id":"n103","kind":"output","name":"Output","x":600.0,"y":60.0],
      ["id":"n104","kind":"lfo","name":"Slow sweep","x":30.0,"y":200.0]],
      "audio":[["source":"n101","target":"n102","input":0,"output":0],["source":"n102","target":"n103","input":0,"output":0]],
      "modulation":[["source":"n104","target":"n102","parameter":7]]]],
      "mixer":["buses":[["id":"n1","name":"Drums","kind":"track","output":"n2"],["id":"n2","name":"Master","kind":"master","output":""]]],"assignments":[],"commands":[],"lanes":[]]
    editor.graphID="n100";editor.update(data)
    try require(editor.canvas.nodes.count==4 && editor.canvas.edges.count==3 && editor.canvas.edges.last?.modulation==true,"Canvas distinguishes audio and modulation topology")
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:1280,height:520),styleMask:[.titled],backing:.buffered,defer:false)
    editor.widthAnchor.constraint(equalToConstant:1280).isActive=true;editor.heightAnchor.constraint(equalToConstant:520).isActive=true;window.contentView=editor;editor.layoutSubtreeIfNeeded();editor.fit()
    try require(editor.scroll.bounds.width>600 && editor.scroll.bounds.height>300,"Docked graph leaves a substantial editable canvas")
    editor.showBus("n1",filter:true)
    try require(editor.canvas.nodes.count==2 && editor.canvas.selected=="n1","Channel context retains its downstream master")
    editor.library.selectItem(at:1);editor.changeLibrary()
    var request:[String:Any]=[:],reply:(([String:Any])->Void)?
    editor.onRequest={method,params,respond in request=params;reply=respond}
    editor.mutate("graph.assign",["target":"n1","graph":"n100"])
    try require(editor.loading && request["expectedRevision"] != nil,"Graph edits carry their displayed revision")
    reply?(["error":["message":"Song changed"]])
    try require(!editor.loading && editor.status.stringValue=="Song changed","Conflicts remain visible without replacing the graph")
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count {
      let directory=URL(fileURLWithPath:CommandLine.arguments[index+1],isDirectory:true);try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
      if let bitmap=editor.bitmapImageRepForCachingDisplay(in:editor.bounds){editor.cacheDisplay(in:editor.bounds,to:bitmap);try bitmap.representation(using:.png,properties:[:])?.write(to:directory.appendingPathComponent("SignalGraphEditor.png"))}
    }
    var song=data;song["plugins"]=[["id":"compressor","name":"Compressor","format":"AU","isInstrument":false]]
    song["mixer"]=["buses":[["id":"n1","name":"Drums","kind":"track","output":"n2","inserts":["compressor"]],["id":"n2","name":"Master","kind":"master","output":""],["id":"n3","name":"Key","kind":"track","output":"n2"]],"sidechains":[["source":"n3","plugin":"compressor","input":1]]]
    song["assignments"]=[["target":"n1","graph":"n100","amount":0.5,"wet":0.8]]
    song["layout"]=[["node":"plugin:compressor","x":900.0,"y":300.0]]
    editor.graphID=nil;editor.filterID="n1";editor.update(song)
    try require(editor.canvas.nodes.contains{$0.id=="n3"} && editor.canvas.nodes.contains{$0.id=="plugin:compressor" && $0.x==900},"Filtered song graph preserves external dependencies and saved processor positions")
    let copy=editor.canvas.nodes.first{$0.id.hasPrefix("graph:")}!
    editor.showActivity([["target":"n1","graph":"n100","role":"ordinary","order":2,"tail":false]],playing:true)
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.detail=="Ordinary · playing #2" && editor.canvas.nodes.first{$0.id==copy.id}?.activity=="playing","Song graph exposes actual processor stack order")
    editor.showActivity([],playing:false)
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.activity==nil,"Stopping playback retires graph activity without altering layout")
    editor.openSongNode(copy.id);try require(editor.graphID=="n100","Opening a channel copy navigates to its shared library definition")
    var nested=song
    nested["mixer"]=["buses":[["id":"n1","name":"Drums","kind":"track","output":"n2","inserts":["compressor"]],["id":"n2","name":"Master","kind":"master","output":""],["id":"n3","name":"Sidechain group","kind":"group","output":"n2"],["id":"n4","name":"Nested group","kind":"group","output":"n3"],["id":"n5","name":"Nested source","kind":"track","output":"n4"],["id":"n6","name":"Unrelated","kind":"track","output":"n2"]],"sidechains":[["source":"n3","plugin":"compressor","input":1]]]
    editor.graphID=nil;editor.filterID="n1";editor.update(nested)
    try require(editor.canvas.nodes.contains{$0.id=="n5"} && !editor.canvas.nodes.contains{$0.id=="n6"},"Channel filters recursively retain grouped sidechain inputs without unrelated master siblings")
    editor.onRequest=nil
    let model=PatternModel(["rows":64,"channels":4,"patterns":[["index":0,"id":"n9"]],"graphLanes":[["target":"n1","name":"Drums","count":3]],"graphCommands":[["target":"n1","graph":"n100","kind":"row","position":32768,"column":1,"number":1,"amount":0.5]]])
    let grid=PatternView();grid.model=model;let host=PatternGraphHost(grid);host.frame=NSRect(x:0,y:0,width:1000,height:500);host.refresh();host.layoutSubtreeIfNeeded()
    try require(host.lanes.lanes.count==3 && host.lanes.frame.width==372 && grid.frame.width==628,"Graph lanes reserve only their required pattern width")
    try require(host.lanes.commands[GraphLaneStrip.key(0,"n1",1)]?.display.hasPrefix("~R001")==true,"Sub-row graph commands expose their timing marker")
    var edited:(String,Int,Int)?;host.lanes.onEdit={edited=($0,$1,$2)};host.lanes.selected=1;grid.cursorRow=7;host.lanes.edit()
    try require(edited?.0=="n1" && edited?.1==1 && edited?.2==7,"Graph lane activation edits the selected target, column and current pattern row")
    let commandEditor=GraphCommandsEditor(frame:.zero);commandEditor.onContext={(model,0,0)};commandEditor.preferredTarget="n1";var params=[String:Any](),respond:(([String:Any])->Void)?
    var commandData=song;commandData["lanes"]=[["target":"n1","count":3]];commandData["commands"]=[["pattern":"n9","target":"n1","graph":"n100","kind":"start","position":65536,"column":2,"amount":0.5,"wet":1.0]]
    commandEditor.onRequest={_,p,r in params=p;respond=r};commandEditor.capture();respond?(["result":["revision":"graph:1","data":commandData]])
    commandEditor.enableLane();try require((params["lanes"] as? [[String:Any]])?.first?["count"] as? Int==3,"Enabling a lane never removes higher occupied columns")
    respond?(["error":["message":"Song changed"]]);commandEditor.offset.stringValue="50";commandEditor.apply()
    let events=params["commands"] as? [[String:Any]] ?? []
    try require(params["expectedRevision"] as? String=="graph:1" && events.contains{$0["position"] as? Int==32768} && events.contains{$0["column"] as? Int==2},"Graph command editing preserves other lanes and exact sub-row timing with a revision guard")
    respond?(["error":["message":"Song changed"]])
    print("PASS shared graph UI: distinct audio/modulation edges, channel context, compact canvas and revision-checked errors")
  }
}
