import AppKit

extension InterfaceTests {
  static func graphPortActionChecks() throws {
    let search=GraphAddMenu()
    let entries:[GraphAddMenu.Entry]=[.init(id:"n2",title:"Track 1 · Main out",detail:"Audio output",keywords:"n2"),.init(id:"n3",title:"Track 2 · Main out",detail:"Audio output",keywords:"n3")]
    search.show(in:NSView(),at:.zero,title:"Connect port",entries:entries,verb:"connects"){_ in}
    search.search.stringValue="track 2 main";search.filter()
    try require(search.filtered.first?.id=="n3" && search.table.selectedRow==0,"Visible channel titles outrank hidden stable IDs and select the intended target on a changed query")
    search.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false);search.replace(entries)
    try require(search.filtered[search.table.selectedRow].id=="n2","A passive catalogue refresh preserves the musician's explicit target selection")
    search.search.stringValue="n2";search.filter()
    try require(search.filtered.count==1 && search.filtered.first?.id=="n2","Stable IDs remain available as deliberate exact search terms")

    func key(_ node:String,_ number:UInt32=0,_ output:Bool=true,_ modulation:Bool=false)->GraphBoundaryPort {.init(node:node,number:number,output:output,modulation:modulation)}
    func choose(_ editor:SignalGraphEditor,_ id:String) throws {
      guard let index=editor.targetMenu.filtered.firstIndex(where:{$0.id==id})else{throw InterfaceFailure(message:"Missing socket choice \(id)")}
      editor.targetMenu.table.selectRowIndexes(IndexSet(integer:index),byExtendingSelection:false);editor.targetMenu.choose()
    }
    let song:[String:Any]=["plugins":[["id":"comp","name":"Compressor","slot":0,"audioBuses":[["index":0,"direction":"input","name":"Main in","channels":2],["index":1,"direction":"input","name":"Detector","channels":2]]]],"layout":[["node":"one","x":30.0,"y":30.0],["node":"two","x":30.0,"y":350.0],["node":"plugin:comp","x":800.0,"y":30.0]],"mixer":["buses":[["id":"one","name":"Channel","kind":"track","output":"main"],["id":"two","name":"Channel","kind":"track","output":"main"],["id":"main","name":"Main","kind":"master","output":"","inserts":["comp"]]]]]
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    var calls=[(String,[String:Any])](),revision="ports:1"
    editor.onRequest={method,params,reply in if method=="graph.get"{reply(["result":["revision":revision,"data":song]])}else{calls.append((method,params))}}
    editor.load();editor.rebuild()
    editor.filterID="one";editor.nodeSearch.stringValue="Compressor";editor.rebuild()
    try require(!editor.canvas.nodes.contains{$0.id=="two"} && editor.portChoices.contains{$0.key==key("two")},"Socket search retains channel-focus-hidden targets with stable identities")
    editor.connectFromSocket(key("plugin:comp",1,false))
    let twoChoice=editor.portChoices.first{$0.key==key("two")}!
    try require(editor.targetMenu.entries.contains{$0.id==twoChoice.id && $0.title.contains("two") && $0.keywords.contains("two") && $0.toolTip?.contains("Stable socket: two / 0")==true},"Duplicate channel names stay disambiguated in socket search")
    try choose(editor,twoChoice.id)
    try require(calls.count==1 && calls[0].0=="mixer.sidechains.set" && calls[0].1["expectedRevision"] as? String==revision,"Keyboard sidechain patch sends one revision-guarded edit")
    let sidechains=calls[0].1["sources"] as? [[String:Any]] ?? []
    try require(sidechains.first?["source"] as? String=="two" && (calls[0].1["input"] as? NSNumber)?.uint32Value==1 && editor.canvas.nodes.contains{$0.id=="two"},"Keyboard sidechain patch reveals and preserves the exact hidden source and physical detector input")

    let detectorChoice=editor.portChoices.first{$0.key==key("plugin:comp",1,false)}!
    try require(detectorChoice.detail.contains("Main") && detectorChoice.detail.contains("insert") && detectorChoice.detail.contains("Sidechain audio input 1") && !detectorChoice.detail.contains("plugin:comp"),"A named socket shows its owner, role and physical signal port instead of its internal identity")
    editor.connectFromSocket(key("two"));editor.targetMenu.search.stringValue="plugin:comp";editor.targetMenu.filter()
    try require(!editor.targetMenu.filtered.isEmpty && editor.targetMenu.filtered.allSatisfy{$0.keywords.contains("plugin:comp")},"Stable socket identities remain searchable even when removed from visible row details")
    editor.targetMenu.close()
    let unavailable=GraphPortChoice(key:key("instrument-graph:i1:two"),node:.init(id:"instrument-graph:i1:two",title:"Instrument character",detail:"I1 → Hats",kind:"audio",x:0,y:0),port:.init(label:"Main out",channels:2),owner:"Hats")
    let unavailableEntry=unavailable.entry(unavailable:"Assign an instrument graph; its output follows the note’s channel")
    editor.targetMenu.search.stringValue="";editor.targetMenu.replace([unavailableEntry])
    let unavailableCell=editor.targetMenu.tableView(editor.targetMenu.table,viewFor:nil,row:0) as? NSStackView
    let unavailableText=(unavailableCell?.arrangedSubviews.last as? NSTextField)?.stringValue ?? ""
    try require(unavailableText.contains("I1 → Hats") && unavailableText.contains("Assign an instrument graph") && unavailableCell?.toolTip?.contains("instrument-graph:i1:two")==true,"Unavailable socket rows retain their copy’s human context alongside the reason, with full stable identity on hover")

    let add=SignalGraphEditor(frame:.zero);add.update(song)
    let insertion=add.connectedAddDestination(GraphAddConnection(node:"master-input:main",port:.init(),output:true))
    try require(insertion?.target=="main" && insertion?.before=="comp","Adding from a summing-input socket inserts before its first effect instead of silently appending at the end")

    let move=SignalGraphEditor(frame:.zero);move.update(song);var moves=[(String,[String:Any])]();move.onRequest={m,p,_ in moves.append((m,p))}
    move.connectSocketPair(key("one"),key("plugin:comp",0,false))
    try require(moves.isEmpty && Set(move.targetMenu.entries.map(\.id))==["move","sum"],"Root Main input patch explicitly offers chain ownership versus additive audio before any write")
    try choose(move,"move")
    try require(moves.count==1 && moves[0].0=="mixer.inserts.move" && moves[0].1["target"] as? String=="one" && moves[0].1["plugins"] as? [String]==["comp"],"Move chain choice uses the exact existing ownership operation")
    let sum=SignalGraphEditor(frame:.zero);sum.update(song);var sums=[(String,[String:Any])]();sum.onRequest={m,p,_ in sums.append((m,p))}
    sum.connectSocketPair(key("one"),key("plugin:comp",0,false));try choose(sum,"sum")
    try require(sums.count==1 && sums[0].0=="mixer.sidechains.set" && !sum.canvas.addingMainInput,"Add/sum uses explicit Main input routing and does not leak Option-drag mode to later gestures")

    let recipe:[String:Any]=["library":[["id":"g","name":"Recipe","nodes":[["id":"in","name":"Source","kind":"input","x":30.0,"y":40.0],["id":"fx","name":"Effect","kind":"plugin","x":300.0,"y":40.0,"plugin":["name":"Effect"]],["id":"lfo","name":"LFO","kind":"lfo","x":30.0,"y":260.0]],"audio":[],"modulation":[]]]]
    let controls=SignalGraphEditor(frame:.zero);controls.graphID="g";controls.update(recipe);controls.exposedParameters["fx"]=UInt32.max
    controls.portCatalogs["fx"]=["parameters":[["id":UInt32.max,"name":"Mode","min":0.0,"max":2.0,"value":1.0,"step":1.0,"canSlide":false,"writable":true]]];controls.rebuild()
    var edits=[(String,[String:Any])]();controls.onRequest={m,p,_ in edits.append((m,p))}
    controls.connectSocketPair(key("lfo",0,true,true),key("fx",UInt32.max,false,true))
    try require(edits.isEmpty && controls.targetMenu.entries.first?.title.contains("discrete")==true,"Keyboard control patch retains the explicit stepped-target choice")
    controls.targetMenu.choose()
    let definition=edits.first?.1["definition"] as? [String:Any],mod=(definition?["modulation"] as? [[String:Any]])?.first
    try require(edits.count==1 && edits.first?.0=="graph.update" && (mod?["parameter"] as? NSNumber)?.uint32Value==UInt32.max && mod?["quantized"] as? Bool==true && mod?["maximum"] as? Double==0,"Recipe keyboard modulation retains full-width parameter ID, explicit discrete mode, and zero depth")
    let follower=SignalGraphEditor(frame:.zero);follower.graphID="g";follower.update(recipe);follower.exposedParameters["fx"]=UInt32.max;follower.portCatalogs=controls.portCatalogs;follower.rebuild()
    var followerEdits=0;follower.onRequest={_,_,_ in followerEdits+=1};follower.connectSocketPair(key("in"),key("fx",UInt32.max,false,true))
    try require(followerEdits==0 && follower.targetMenu.entries.first?.id=="follower","Keyboard audio-to-parameter patch offers the same explicit follower as a socket drag")
    follower.targetMenu.close()

    let stale=SignalGraphEditor(frame:.zero);var staleWrites=0,staleRevision="p:1"
    stale.onRequest={m,_,reply in if m=="graph.get"{reply(["result":["revision":staleRevision,"data":song]])}else{staleWrites+=1}}
    stale.load();stale.connectFromSocket(key("one"));let oldPick=stale.targetMenu.onChoose,oldEntry=stale.targetMenu.entries.first!
    staleRevision="p:2";stale.load();oldPick?(oldEntry)
    try require(staleWrites==0 && stale.status.stringValue.contains("song changed"),"A socket chooser cannot write against an externally changed graph revision")
    stale.connectFromSocket(key("one"));let changedContext=stale.targetMenu.onChoose,contextEntry=stale.targetMenu.entries.first!;stale.processingGroupID="different-depth";changedContext?(contextEntry)
    try require(staleWrites==0,"A socket chooser cannot retarget across processing-group navigation")
    stale.targetMenu.close()

    let aliases=SignalGraphEditor(frame:.zero);aliases.graphID="g"
    var grouped=recipe,groupDefinition=(recipe["library"] as! [[String:Any]])[0]
    groupDefinition["groups"]=[["id":"group","name":"Boundary","nodes":["fx"],"x":400.0,"y":20.0]];grouped["library"]=[groupDefinition];aliases.update(grouped)
    let boundary=aliases.portChoices.first{$0.key.node=="group" && !$0.key.output && !$0.key.modulation}!
    var aliasWrites=0;aliases.onRequest={_,_,_ in aliasWrites+=1};aliases.connectFromSocket(key("in"))
    let capturedAlias=aliases.targetMenu.onChoose,aliasEntry=aliases.targetMenu.entries.first{$0.id==boundary.id}!
    aliases.boundaryPorts[boundary.key]=GraphRealPort(node:"other-vendor",number:9);capturedAlias?(aliasEntry)
    try require(aliasWrites==0 && aliases.status.stringValue.contains("socket layout changed"),"A same-revision catalogue refresh cannot redirect a captured group-boundary socket")
    aliases.targetMenu.close()

    let readonly=GraphPortChoice(key:key("meter",7,false,true),node:SignalCanvasNode(id:"meter",title:"Meter",detail:"",kind:"audio",x:0,y:0),port:.init(number:7,label:"Level",modulation:true,signal:.parameter,unavailable:"Read-only parameter"))
    let event=GraphPortChoice(key:key("events"),node:SignalCanvasNode(id:"events",title:"Notes",detail:"",kind:"audio",x:0,y:0),port:.init(signal:.events))
    try require(controls.portUnavailable(readonly)=="Read-only parameter" && controls.portUnavailable(event)?.contains("Song graph")==true,"Readonly parameters and unsupported event sockets give specific unavailable reasons")
    let nav=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700));nav.update(song)
    let host=NSWindow(contentRect:nav.frame,styleMask:[.borderless],backing:.buffered,defer:false);host.contentView=nav;nav.layoutSubtreeIfNeeded()
    nav.scroll.magnification=1;nav.canvas.scroll(.zero)
    let preserved=nav.canvas.nodes.map(\.rect)
    nav.framePortEndpoints([key("two"),key("plugin:comp",1,false)])
    let detector=nav.canvas.nodes.first{$0.id=="plugin:comp"}!,detectorPort=detector.inputs.first{$0.number==1}!
    try require(nav.scroll.documentVisibleRect.contains(detector.portPoint(detectorPort,output:false)) && nav.canvas.nodes.map(\.rect)==preserved,"Choosing an off-screen socket frames its endpoint without rewriting any saved card position")
    let edge=nav.canvas.edges.firstIndex{$0.source=="one"}!,route=nav.cableLocation(nav.canvas.edges[edge]);nav.selectConnection(edge)
    let positions=Dictionary(uniqueKeysWithValues:nav.canvas.nodes.map{($0.id,$0.rect)});var navigationWrites=0;nav.onRequest={_,_,_ in navigationWrites+=1}
    nav.showCableEndpoint(output:false)
    try require(nav.portReturn?.cable==route && nav.selectedID==route.target,"Show cable target selects the actual endpoint and records a stable return connection")
    nav.returnToConnection()
    try require(nav.canvas.selectedEdge.map{nav.cableLocation(nav.canvas.edges[$0])}==route && nav.portReturn==nil && navigationWrites==0 && nav.canvas.nodes.allSatisfy{positions[$0.id]==$0.rect},"Back restores the original connection without moving cards or creating document Undo")
    controls.targetMenu.close();editor.targetMenu.close();move.targetMenu.close();sum.targetMenu.close()
    try graphConnectionGainChecks(song:song,recipe:recipe)
    print("PASS typed socket keyboard routing: stable IDs, hidden endpoints, sidechain, explicit move/sum, discrete/follower choices, stale guards and return navigation")
  }
  static func graphConnectionGainChecks(song:[String:Any],recipe:[String:Any]) throws {
    var song=song,mixer=song["mixer"] as! [String:Any],buses=mixer["buses"] as! [[String:Any]]
    buses[0]["sends"]=[["target":"main","gainDB":-12.0]]
    mixer["buses"]=buses;mixer["sidechains"]=[["source":"two","plugin":"comp","input":1,"gainDB":-4.0]];song["mixer"]=mixer
    let editor=SignalGraphEditor(frame:.zero);editor.update(song)
    let send=editor.songConnections.firstIndex{$0["kind"] as? String=="send"}!
    let output=editor.songConnections.firstIndex{$0["kind"] as? String=="output"}!
    let master=editor.songConnections.firstIndex{$0["kind"] as? String=="master-output"}!
    let insert=editor.songConnections.firstIndex{$0["kind"] as? String=="insert"}!
    let sidechain=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-input"}!
    editor.selectConnection(send)
    try require(editor.connectionGain.doubleValue == -12 && editor.connectionGain.isEnabled && !editor.connectionGainRow.isHidden,"A send’s actual gain remains directly editable")
    editor.selectConnection(output)
    try require(editor.connectionGain.doubleValue==0 && !editor.connectionGain.isEnabled && editor.connectionGainRow.isHidden && editor.connectionHint.stringValue.contains("no separate gain"),"Selecting Main output after a -12 dB send hides irrelevant gain and clears the prior route value")
    editor.selectConnection(send);editor.selectConnection(master)
    try require(editor.connectionGain.doubleValue==0 && editor.connectionGainRow.isHidden && editor.connectionForm.isHidden,"The final master and fixed insert links never retain a previous send gain")
    editor.selectConnection(send);editor.selectConnection(insert)
    try require(editor.connectionGain.doubleValue==0 && editor.connectionGainRow.isHidden,"Fixed insert connections have no independent gain control")
    editor.selectConnection(sidechain)
    try require(editor.connectionGain.doubleValue == -4 && editor.connectionGain.isEnabled && !editor.connectionGainRow.isHidden,"A subsequent sidechain selection restores its actual editable route gain")
    var recipe=recipe,definition=(recipe["library"] as! [[String:Any]])[0]
    definition["audio"]=[["source":"in","target":"fx","output":0,"input":0,"gain":0.5]];recipe["library"]=[definition]
    editor.graphID="g";editor.update(recipe);editor.selectConnection(0)
    try require(editor.connectionGain.doubleValue==0.5 && editor.connectionGain.isEnabled && !editor.connectionGainRow.isHidden && editor.gainLabel.stringValue=="Gain ×","Recipe cables retain their independent gain multiplier when switching scopes")
  }
}
