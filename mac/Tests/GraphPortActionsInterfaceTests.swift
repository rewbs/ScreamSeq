import AppKit

extension InterfaceTests {
  static func graphPortActionChecks() throws {
    try graphSearchKeyboardChecks()
    let search=GraphAddMenu()
    let entries:[GraphAddMenu.Entry]=[.init(id:"n2",title:"Track 1 · Main out",detail:"Audio output",keywords:"n2"),.init(id:"n3",title:"Track 2 · Main out",detail:"Audio output",keywords:"n3")]
    search.show(in:NSView(),at:.zero,title:"Connect port",entries:entries,verb:"connects"){_ in}
    search.search.stringValue="track 2 main";search.filter()
    try require(search.filtered.first?.id=="n3" && search.table.selectedRow==0,"Visible channel titles outrank hidden stable IDs and select the intended target on a changed query")
    search.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false);search.replace(entries)
    try require(search.filtered[search.table.selectedRow].id=="n2","A passive catalogue refresh preserves the musician's explicit target selection")
    search.search.stringValue="n2";search.filter()
    try require(search.filtered.count==1 && search.filtered.first?.id=="n2","Stable IDs remain available as deliberate exact search terms")

    search.search.stringValue="";search.replace([.init(id:"invalid",title:"Compressor audio input",detail:"",keywords:"",unavailable:"Notes require Notes sockets"),.init(id:"valid",title:"Synth Notes input",detail:"",keywords:"")])
    try require(search.filtered.map(\.id)==["valid","invalid"] && search.table.selectedRow==0,"Compatible note targets come first; incompatible choices retain explanations below them")

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
    move.moveInsertChain(at:key("plugin:comp",0,false))
    let moveTarget=move.portChoices.first{$0.key==key("one")}!
    try require(moves.isEmpty && move.targetMenu.entries.contains{$0.id==moveTarget.id},"Moving a chain is an explicit action with an exact destination chooser")
    try choose(move,moveTarget.id)
    try require(moves.count==1 && moves[0].0=="mixer.inserts.move" && moves[0].1["target"] as? String=="one" && moves[0].1["plugins"] as? [String]==["comp"],"Move chain choice uses the exact existing ownership operation")
    let sum=SignalGraphEditor(frame:.zero);sum.update(song);var sums=[(String,[String:Any])]();sum.onRequest={m,p,_ in sums.append((m,p))}
    sum.connectSocketPair(key("one"),key("plugin:comp",0,false))
    try require(sums.count==1 && sums[0].0=="mixer.sidechains.set" && sum.targetMenu.onChoose==nil,"Keyboard Main input patch immediately adds a source without a second picker or an ownership move")

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
    try graphAudioFanChecks()
    print("PASS typed socket keyboard routing: stable IDs, hidden endpoints, additive fan-in/out, explicit chain move, discrete/follower choices, stale guards and return navigation")
  }
  static func graphSearchKeyboardChecks() throws {
    let owner = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 400, height: 300), styleMask: [.titled], backing: .buffered, defer: false)
    let unrelated = NSWindow(contentRect: owner.frame, styleMask: [.titled], backing: .buffered, defer: false)
    let panel = NSPanel(contentRect: owner.frame, styleMask: [.titled], backing: .buffered, defer: false)
    for window in [owner, unrelated, panel] { window.isReleasedWhenClosed = false }
    let search = NSSearchField(frame: NSRect(x: 10, y: 10, width: 300, height: 28))
    panel.contentView?.addSubview(search)
    try require(panel.makeFirstResponder(search), "Offscreen graph chooser can focus its search field")
    func event(_ text: String, code: UInt16 = 49, flags: NSEvent.ModifierFlags = [], target: NSWindow? = nil) -> NSEvent {
      NSEvent.keyEvent(with: .keyDown, location: NSPoint(x: 12, y: 34), modifierFlags: flags,
        timestamp: 123.5, windowNumber: (target ?? owner).windowNumber, context: nil,
        characters: text, charactersIgnoringModifiers: text, isARepeat: false, keyCode: code)!
    }
    func route(_ event: NSEvent, keyWindow: NSWindow? = nil) -> NSEvent {
      GraphAddMenu.eventForFocusedSearch(event, keyWindow: keyWindow ?? panel, owner: owner, panel: panel, search: search)
    }
    let space = event(" ")
    try require(space.window === owner && KeyboardSettings.focusWindow(for: space, keyWindow: panel) === panel,
      "Queued Space addressed to the graph resolves keyboard focus in the newly active search, not the transport's old canvas")
    try require(KeyboardSettings.focusWindow(for: space, keyWindow: nil) === owner,
      "Keyboard focus falls back to the event window when the application has no key window")
    for character in "Track 2" {
      let original = event(String(character)), routed = route(original)
      try require(routed.window === panel && routed.characters == original.characters && routed.keyCode == original.keyCode && routed.modifierFlags == original.modifierFlags && routed.timestamp == original.timestamp,
        "Immediate Track 2 typing, including Space, reaches the focused graph search with the original key data")
    }
    let shifted = event("T", code: 17, flags: [.shift])
    try require(route(shifted).window === panel && route(shifted).modifierFlags == [.shift], "Shift typing remains text in the newly opened search")
    for flags: NSEvent.ModifierFlags in [[.control], [.control, .shift], [.command], [.option]] {
      let shortcut = event(" ", flags: flags)
      try require(route(shortcut) === shortcut, "Graph search does not retarget transport chords, command shortcuts or input-method modifier keys")
    }
    for (text, code): (String, UInt16) in [("\r", 36), ("\u{1b}", 53), ("\u{F700}", 126), ("", 0)] {
      let command = event(text, code: code)
      try require(route(command) === command, "Search retargeting leaves control, navigation and composition events unchanged")
    }
    let current = event(" ", target: panel), other = event(" ", target: unrelated)
    try require(route(current) === current && route(other) === other && route(space, keyWindow: owner) === space && route(space, keyWindow: unrelated) === space,
      "Only stale typing from this chooser's owner is redirected while the chooser is actually the key window")
    _ = panel.makeFirstResponder(panel.contentView)
    try require(route(space) === space, "A graph chooser whose search no longer owns focus does not redirect typing")
    print("PASS graph search keyboard focus: immediate queued text stays local; transport chords and unrelated windows remain unchanged")
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
  static func graphAudioFanChecks() throws {
    let ports:[[String:Any]]=[
      ["index":0,"direction":"input","name":"Main","channels":2],
      ["index":1,"direction":"input","name":"Detector","channels":2],
      ["index":0,"direction":"output","name":"Main","channels":2],
      ["index":2,"direction":"output","name":"Aux","channels":2]]
    let plugins=["A","B","C","loose"].enumerated().map{["id":$0.element,"name":$0.element,"slot":$0.offset,"audioBuses":ports] as [String:Any]}
    let existingSend:[String:Any]=["target":"return","gainDB":-18.0,"enabled":false,"preFader":true]
    let existingInput:[String:Any]=["source":"three","plugin":"B","input":0,"gainDB":-6.0,"preFader":true,"enabled":false]
    let direct:[String:Any]=["source":"A","output":2,"target":"B","input":1,"gainDB":-8.0,"enabled":false]
    let mixer:[String:Any]=["buses":[
      ["id":"one","name":"One","kind":"track","output":"main","inserts":["A"],"sends":[existingSend]],
      ["id":"two","name":"Two","kind":"track","output":"main","inserts":["B"]],
      ["id":"three","name":"Three","kind":"track","output":"main"],
      ["id":"return","name":"Return","kind":"return","output":"main"],
      ["id":"main","name":"Main","kind":"master","output":"","inserts":["C"]]],
      "detached":["loose"],"sidechains":[existingInput,["source":"one","plugin":"B","input":1,"gainDB":-9.0,"preFader":true]],
      "pluginConnections":[direct],"instruments":[["plugin":"A","output":2,"target":"three"],["plugin":"A","output":2,"target":"return"]]]
    var song:[String:Any]=["plugins":plugins,"mixer":mixer,"songSources":[["id":"follower","kind":"follower","name":"Follow One","audioBus":"one","output":0]]]
    let editor=SignalGraphEditor(frame:.zero);var writes=[(String,[String:Any])](),revision="fan:7"
    editor.onRequest={method,p,reply in
      if method=="graph.get"{reply(["result":["revision":revision,"data":song]])}
      else{writes.append((method,p));reply(["error":["message":"captured"]])}
    }
    editor.load();writes=[]
    editor.connectPorts("one","plugin:B",out:0,input:0,modulation:false)
    let sources=writes.last?.1["sources"] as? [[String:Any]] ?? []
    try require(writes.count==1 && writes[0].0=="mixer.sidechains.set" && writes[0].1["expectedRevision"] as? String==revision && sources.count==2 && sources[0]["source"] as? String=="three" && sources[0]["gainDB"] as? Double == -6 && sources[0]["preFader"] as? Bool==true && sources[0]["enabled"] as? Bool==false && sources[1]["source"] as? String=="one" && sources[1]["gainDB"] as? Double==0,"Main-input fan-in appends at unity, retains gain/pre-fader/disabled controls and uses one revision-guarded edit")
    try require(editor.canvas.edges.contains{$0.source=="three" && $0.target=="plugin:B" && $0.input==0 && !$0.enabled},"Existing disabled input contributions remain visibly disabled when adding another source")
    writes=[];editor.connectPorts("one","plugin:B",out:0,input:1,modulation:false)
    try require(writes.isEmpty && editor.status.stringValue.contains("already connected"),"A bus-card gesture recognizes an existing post-insert detector tap without resetting its gain or pre-fader setting")
    writes=[];editor.connectPorts("plugin:A","plugin:B",out:0,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.plugin.connection.set" && writes[0].1["source"] as? String=="A" && writes[0].1["target"] as? String=="B" && writes[0].1["replace"]==nil,"A plugin Main-to-Main patch adds its exact contribution without moving either rack chain")
    writes=[];editor.connectPorts("plugin:A","plugin:loose",out:2,input:1,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.plugin.connection.set" && writes[0].1["output"] as? UInt32==2 && writes[0].1["target"] as? String=="loose" && writes[0].1["replace"]==nil,"Fan-out from an occupied auxiliary output creates a separate exact connection, including detached targets")
    writes=[];editor.connectPorts("three","plugin:loose",out:0,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.sidechains.set" && writes[0].1["plugin"] as? String=="loose","A detached effect accepts an additive channel input without assigning it to the channel insert chain")
    writes=[];editor.connectPorts("plugin:A","master-input:main",out:2,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.plugin.route" && writes[0].1["targets"] as? [String]==["three","return","main"],"Plugin output fan-out retains every earlier destination when appending another bus")
    writes=[];editor.connectPorts("one","three",out:0,input:0,modulation:false)
    let sends=writes.last?.1["sends"] as? [[String:Any]] ?? []
    try require(writes.count==1 && writes[0].0=="mixer.sends.set" && sends.count==2 && NSDictionary(dictionary:sends[0]).isEqual(to:existingSend) && sends[1]["target"] as? String=="three" && sends[1]["gainDB"] as? Int==0,"Bus fan-out retains its main output and every send control while appending a unity branch")
    writes=[];editor.connectPorts("one","plugin:A",out:0,input:0,modulation:false)
    try require(writes.isEmpty && editor.status.stringValue.contains("already connected"),"Drawing over the channel’s existing implicit insert cable is a no-op, never post-insert feedback")
    editor.connectPorts("one","plugin:A",out:0,input:1,modulation:false)
    try require(writes.isEmpty && editor.status.stringValue.contains("after its inserts"),"An unavailable raw channel-input branch explains its actual post-insert tap semantics")
    let sourceKey=GraphBoundaryPort(node:"one",number:0,output:true,modulation:false)
    try require(editor.portChoice(sourceKey)?.detail.contains("post-insert bus tap")==true && editor.socketPatchHelp(sourceKey)?.contains("post-insert")==true,"Channel chooser and hover help disclose the real post-insert source")
    let send=editor.songConnections.firstIndex{$0["kind"] as? String=="send"}!
    writes=[];editor.rewire(send,source:"plugin:A",target:"plugin:B",out:0,input:0,modulation:false)
    try require(writes.isEmpty && editor.status.stringValue.contains("bus input"),"A selected send handle cannot move an unrelated destination’s effect chain")
    let route=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-connection"}!
    editor.rewire(route,source:"plugin:A",target:"plugin:loose",out:2,input:1,modulation:false)
    let replaced=writes.last?.1["replace"] as? [String:Any] ?? [:]
    try require(writes.count==1 && replaced["target"] as? String=="B" && writes[0].1["target"] as? String=="loose" && writes[0].1["gainDB"] as? Double == -8 && writes[0].1["enabled"] as? Bool==false,"A selected direct cable handle replaces only its captured endpoint tuple and preserves its disabled gain state")
    writes=[];editor.connectPorts("three","source:follower",out:0,input:0,modulation:false)
    try require(writes.isEmpty && editor.status.stringValue.contains("already has an analysis tap"),"A fresh socket never overwrites a Song follower’s occupied single analysis tap")
    let follower=editor.songConnections.firstIndex{$0["kind"] as? String=="follower-input"}!
    editor.rewire(follower,source:"three",target:"source:follower",out:0,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="graph.song.source.update" && (writes[0].1["source"] as? [String:Any])?["audioBus"] as? String=="three","The follower’s selected cable handle still explicitly changes its one analysis tap")
    writes=[];editor.moveInsertChain(at:.init(node:"plugin:B",number:0,output:false,modulation:false));let old=editor.targetMenu.onChoose,entry=editor.targetMenu.entries.first!
    revision="fan:8";editor.load();writes=[];old?(entry)
    try require(writes.isEmpty && editor.status.stringValue.contains("song changed"),"Explicit chain movement keeps the same stale-revision protection as additive socket patching")
    editor.targetMenu.close()
    song["plugins"]=plugins+[["id":"synth","name":"Synth","slot":4,"isInstrument":true,"audioBuses":ports]];editor.load();writes=[]
    editor.connectPorts("plugin:synth","three",out:0,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.plugin.route" && writes[0].1["targets"] as? [String]==["main","three"],"An instrument’s first added audio destination preserves its implicit Master route")
    writes=[];editor.connectPorts("plugin:synth","master-input:main",out:0,input:0,modulation:false)
    try require(writes.isEmpty,"Repeating an instrument’s implicit Master cable creates no mutation or Undo record")
    let definition:[String:Any]=["id":"g","nodes":[["id":"in","kind":"input"],["id":"a","kind":"plugin"],["id":"b","kind":"plugin"],["id":"out","kind":"output"]],"audio":[["source":"in","target":"a","output":0,"input":0,"gain":0.4],["source":"a","target":"out","output":0,"input":0,"gain":0.6]],"modulation":[]]
    song=["library":[definition]];editor.graphID="g";editor.load();writes=[]
    editor.connectPorts("in","b",out:0,input:0,modulation:false)
    var audio=((writes.last?.1["definition"] as? [String:Any])?["audio"] as? [[String:Any]]) ?? []
    try require(audio.count==3 && audio[0]["gain"] as? Double==0.4 && audio[1]["gain"] as? Double==0.6 && audio[2]["target"] as? String=="b","Recipe output fan-out appends one cable without changing the existing path or gains")
    writes=[];editor.connectPorts("b","out",out:0,input:0,modulation:false)
    audio=((writes.last?.1["definition"] as? [String:Any])?["audio"] as? [[String:Any]]) ?? []
    try require(audio.count==3 && audio[1]["source"] as? String=="a" && audio[2]["source"] as? String=="b" && audio[2]["target"] as? String=="out","Recipe input fan-in retains its first source and appends the additional one")
  }
}
