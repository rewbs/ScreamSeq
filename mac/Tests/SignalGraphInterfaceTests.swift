import AppKit
extension InterfaceTests {
  static func graphShortcutChecks() throws {
    let defaults=UserDefaults.standard
    let oldKeys=defaults.object(forKey:"workspaceShortcuts"),oldSequences=defaults.object(forKey:"workspaceSequences")
    defer {
      if let oldKeys{defaults.set(oldKeys,forKey:"workspaceShortcuts")}else{defaults.removeObject(forKey:"workspaceShortcuts")}
      if let oldSequences{defaults.set(oldSequences,forKey:"workspaceSequences")}else{defaults.removeObject(forKey:"workspaceSequences")}
    }
    defaults.removeObject(forKey:"workspaceShortcuts");defaults.removeObject(forKey:"workspaceSequences")
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:400,height:300))
    canvas.update([SignalCanvasNode(id:"fx",title:"Effect",detail:"",kind:"plugin",x:40,y:60)],edges:[]);canvas.selected="fx"
    var bypasses=0,parents=0
    canvas.onBypass={bypasses+=1};canvas.onParent={parents+=1}
    func key(_ text:String,_ code:UInt16,_ flags:NSEvent.ModifierFlags=[])->NSEvent {
      NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:flags,timestamp:0,windowNumber:0,context:nil,characters:text,charactersIgnoringModifiers:text,isARepeat:false,keyCode:code)!
    }
    let menu=NSMenu(title:"Graph");menu.addItem(GraphCommand.bypass.item("Toggle plugin bypass",key:"m"){bypasses+=1})
    let palette=WorkspaceCommandPalette();palette.additionalMenus={ [menu] };palette.collect()
    canvas.keyDown(with:key("m",46));try require(bypasses==1,"Unmodified M is a canvas-local default")
    try require(palette.setShortcut(GraphCommand.bypass.id,keys:["ctrl+opt+b"])==nil,"Graph bypass can be rebound")
    canvas.keyDown(with:key("m",46));try require(bypasses==1,"Rebinding bypass retires its old canvas key")
    palette.shortcutAllowed={_ in false}
    try require(!palette.handleAdditionalShortcut(key("b",11,[.control,.option])) && bypasses==1,"Graph bindings do not steal keys from another panel or text editor")
    palette.shortcutAllowed={_ in true}
    try require(palette.handleAdditionalShortcut(key("b",11,[.control,.option])) && bypasses==2,"Remapped graph key invokes exactly its registered command")
    try require(palette.resetShortcut(GraphCommand.bypass.id)==nil,"Reset permits shipped unmodified canvas shortcuts")
    canvas.keyDown(with:key("m",46));try require(bypasses==3,"Reset restores the canvas default")
    try require(palette.setShortcut(GraphCommand.bypass.id,keys:[])==nil,"Graph binding can be explicitly removed")
    canvas.keyDown(with:key("m",46));try require(bypasses==3,"An empty override also disables the old canvas key")
    defaults.set([GraphCommand.parent.id:["key":"p","modifiers":NSEvent.ModifierFlags.control.rawValue]],forKey:"workspaceShortcuts")
    canvas.keyDown(with:key("\u{f700}",126,.option))
    try require(parents==0 && canvas.nodes[0].y==60,"A retired Option-Up neither navigates nor accidentally moves a node")
    defaults.set([GraphCommand.bypass.id:["ctrl+g","b"]],forKey:"workspaceSequences")
    try require(!GraphCommand.bypass.usesCanvasDefault(),"Sequence overrides retire the canvas default")
    let sequence=WorkspaceSequences();sequence.load();sequence.isAvailable={_ in false}
    try require(!sequence.handle(key("g",5,.control)),"A graph-only prefix does not capture typing outside the graph")
    defaults.set(["Audio & modulation graph / More… / Fit graph/invoke":["key":"f","modifiers":NSEvent.ModifierFlags.control.rawValue],"Another panel / Fit graph/invoke":["key":"x","modifiers":0]],forKey:"workspaceShortcuts")
    defaults.set(["Graph / Add modulation source / LFO/invoke":["ctrl+g","l"],GraphCommand.sourceLFO.id:["ctrl+g","s"]],forKey:"workspaceSequences")
    GraphCommand.migrateLegacyBindings()
    let migrated=defaults.dictionary(forKey:"workspaceShortcuts") ?? [:]
    try require(migrated[GraphCommand.fit.id] != nil && migrated["Audio & modulation graph / More… / Fit graph/invoke"]==nil && migrated["Another panel / Fit graph/invoke"] != nil,"Only exact historical graph paths migrate to stable IDs")
    try require((defaults.dictionary(forKey:"workspaceSequences")?[GraphCommand.sourceLFO.id] as? [String])==["ctrl+g","s"],"A current explicit binding wins over its historical alias")
  }
  static func graphFilterBoundaryChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    var buses:[[String:Any]]=(1...16).map{["id":"n\($0)","name":"Track \($0)","kind":"track","output":"n20","inserts":$0==1 ? ["comp"]:[]]}
    buses += [["id":"n20","name":"Rhythm","kind":"group","output":"n21"],["id":"n21","name":"Main","kind":"master","output":""],
      ["id":"n30","name":"Detector sum","kind":"group","output":"n21"],["id":"n31","name":"Detector subgroup","kind":"group","output":"n30"],
      ["id":"n32","name":"Detector source","kind":"track","output":"n31"],["id":"n40","name":"Other master input","kind":"track","output":"n21"]]
    let song:[String:Any]=["mixer":["buses":buses,"sidechains":[["source":"n30","plugin":"comp","input":1]]],"plugins":[["id":"comp","name":"Compressor","audioBuses":[["direction":"input","index":0,"name":"Main"],["direction":"input","index":1,"name":"Detector"]]]]]
    var requests=0;editor.onRequest={_,_,_ in requests+=1};editor.filterID="n1";editor.update(song)
    let visible=Set(editor.canvas.nodes.map(\.id))
    try require(visible==["n1","plugin:comp","n20","n21","n30","n31","n32"],"Focused paths retain nested detector sources without expanding sibling inputs at downstream summing buses")
    let stub=editor.canvas.boundaries.first{$0.node=="n20"}!
    try require(stub.names.count==15 && !stub.output && stub.number==0 && stub.label=="15 other inputs  ›","Downstream fan-in is represented by a labeled count at the real input socket")
    try require(editor.canvas.boundaries.contains{$0.node=="n21" && $0.names==["Other master input"]},"Unrelated master inputs are visible as a revealable boundary, rather than silently disappearing")
    try require(editor.canvas.edges.count==editor.songConnections.count && !editor.canvas.nodes.contains{$0.id==stub.id},"View-only summaries never enter the editable node/connection catalogs")
    let rect=editor.canvas.boundaryRect(stub.id)!
    try require(!editor.canvas.nodes.contains{$0.rect.intersects(rect)},"Hidden-route labels do not cover visible node cards")
    editor.scroll.magnification=0.8
    // Magnification changes the badge's semantic size; use its refreshed centre.
    let refreshed=editor.canvas.boundaryRect(stub.id)!
    let freshClick=NSEvent.mouseEvent(with:.leftMouseDown,location:editor.canvas.convert(NSPoint(x:refreshed.midX,y:refreshed.midY),to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!
    editor.canvas.mouseDown(with:freshClick)
    try require(editor.canvas.nodes.contains{$0.id=="n16"} && !editor.canvas.nodes.contains{$0.id=="n40"},"One click reveals only that boundary's hidden input branches")
    try require(editor.filterID=="n1" && editor.scroll.magnification==0.8 && requests==0,"Revealing routes preserves channel focus, zoom, playback and document history")
    editor.clearNodeFilters();editor.filterID="n1";editor.update(song)
    editor.canvas.selected=editor.canvas.nodes.last!.id
    func key(_ text:String,_ code:UInt16)->NSEvent {NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:0,context:nil,characters:text,charactersIgnoringModifiers:text,isARepeat:false,keyCode:code)!}
    editor.canvas.keyDown(with:key("\t",48))
    var staleActions=0;editor.canvas.onBypass={staleActions+=1};editor.canvas.onDelete={staleActions+=1};editor.canvas.onListen={_ in staleActions+=1}
    editor.canvas.keyDown(with:key("m",46));editor.canvas.keyDown(with:key("l",37));editor.canvas.keyDown(with:key("",51))
    try require(staleActions==0 && editor.selectedID==nil,"Keyboard focus on a boundary cannot bypass, monitor or delete the previous inspector target")
    let keyboardBoundary=editor.canvas.selectedBoundary!
    let revealed=editor.canvas.boundaries.first{$0.id==keyboardBoundary}!.reveal
    editor.canvas.keyDown(with:key("\r",36))
    try require(editor.graphFilterState.revealed.isSuperset(of:revealed) && requests==0,"Tab and Return provide the same reveal without creating song edits")
    editor.clearNodeFilters();editor.nodeSearch.stringValue="Compressor";editor.changeNodeFilter()
    try require(editor.canvas.nodes.map(\.id)==["plugin:comp"] && editor.canvas.boundaries.count==3,"Search filtering exposes omitted main input, detector input and output boundaries")
    let detector=editor.canvas.boundaries.first{!$0.output && $0.number==1}!
    try require(editor.actionMenu().items.contains{($0 as? ContextAction)?.identifier?.rawValue==GraphCommand.revealHidden.id && $0.isEnabled},"Hidden-route reveal remains discoverable through the context menu and command palette")
    editor.revealBoundary(detector.id)
    try require(editor.canvas.nodes.contains{$0.id=="n30"} && editor.nodeSearch.stringValue=="Compressor" && editor.canvas.edges.count==1,"Search boundary reveal keeps the search and exact sidechain cable identity")
    try require(editor.songConnections.first?["kind"] as? String=="plugin-input","A revealed sidechain retains its authoritative edit action")
    let placement=SignalGraphEditor(frame:.zero)
    let crowded:[String:Any]=["mixer":["buses":[
      ["id":"a","kind":"track","name":"Focused","output":"main","sends":[["target":"return","gainDB":-6]]],
      ["id":"b","kind":"track","name":"Hidden input","output":"main"],
      ["id":"main","kind":"master","name":"Main","output":""],
      ["id":"return","kind":"return","name":"Saved return","output":"main"]]],
      "layout":[["node":"return","x":30.0,"y":152.0]]]
    placement.filterID="a";placement.update(crowded)
    let beforeReveal=Dictionary(uniqueKeysWithValues:placement.canvas.nodes.map{($0.id,$0.rect)})
    placement.revealBoundary(placement.canvas.boundaries.first{$0.node=="main"}!.id)
    let added=placement.canvas.nodes.first{$0.id=="b"}!
    try require(!beforeReveal.values.contains{$0.insetBy(dx:-16,dy:-16).intersects(added.rect)},"A newly revealed provisional card avoids both saved and previously cached cards")
    try require(placement.canvas.nodes.filter{$0.id != "b"}.allSatisfy{beforeReveal[$0.id]==$0.rect},"Revealing a branch never rearranges the already visible path or a saved position")
    placement.update(crowded)
    try require(placement.canvas.nodes.first{$0.id=="b"}?.rect==added.rect,"The collision-free automatic position survives later graph refreshes")
    var instrumentSong=crowded
    instrumentSong["instruments"]=[["id":"sample-one","index":1,"name":"Sample voice","plugin":false]]
    instrumentSong["instrumentAssignments"]=[["target":"sample-one","graph":"recipe"]]
    placement.update(instrumentSong)
    try require(placement.canvas.nodes.first{$0.id=="instrument:sample-one"}?.inputs.isEmpty==true && placement.canvas.nodes.first{$0.id=="instrument:sample-one"}?.outputs.isEmpty==false,"A sample instrument source exposes output sockets without a misleading Main in")
    try require(placement.canvas.nodes.first{$0.id=="instrument-graph:sample-one:a"}?.inputs.isEmpty==false,"The independent instrument processing copy retains its real audio input")
    var explicit=crowded;explicit["layout"]=[["node":"return","x":30.0,"y":152.0],["node":"b","x":30.0,"y":152.0]]
    placement.update(explicit)
    try require(placement.canvas.nodes.first{$0.id=="b"}?.y==152,"An explicit musician position remains authoritative even when it overlaps another card")
  }
  static func signalGraphChecks() throws {
    try graphFollowerGestureChecks()
    try graphControlRefreshChecks()
    try graphGestureRefreshChecks()
    try graphMutationFeedbackChecks()
    try graphDrawCullingChecks()
    try graphSignalRedrawChecks()
    try graphSignalIndexAndNameChecks()
    try graphActivityLabelChecks()
    try graphRouteObservationChecks()
    try graphShortcutChecks()
    try graphFilterBoundaryChecks()
    try processingGroupChecks()
    try songModulationChecks()
    try graphPresentationChecks()
    try graphParameterDropChecks()
    try graphProvenanceChecks()
    let busControls=GraphBusControls(frame:.zero)
    var busCalls=[(String,[String:Any])](),busReplies=[([String:Any])->Void]()
    busControls.onRequest={method,p,reply in busCalls.append((method,p));busReplies.append(reply)}
    busControls.context(["id":"bus-a","gainDB":0.0],revision:"s:1")
    busControls.change("gainDB",value:-6.0,final:false)
    busControls.change("gainDB",value:-9.0,final:false)
    busControls.context(["id":"bus-b","gainDB":3.0],revision:"s:1")
    try require(busCalls.count==1 && busCalls[0].1["preview"] as? Bool==true && busControls.identity=="bus-a","Graph level drags audition the captured bus and defer a target change")
    busReplies[0](["result":["revision":"s:1","data":[:]]])
    try require(busCalls.count==2 && busCalls[1].1["preview"] as? Bool==false && busCalls[1].1["gainDB"] as? Double == -9 && busCalls[1].1["bus"] as? String=="bus-a","Changing selection finalizes the complete old-bus gesture once")
    busReplies[1](["result":["revision":"s:2","data":[:]]])
    try require(busControls.identity=="bus-b" && busControls.gain.slider.doubleValue==3,"The new graph bus appears only after the retained gesture is committed")
    busControls.change("gainDB",value:10.0,final:false)
    try require(busCalls.last?.1["expectedRevision"] as? String=="s:2","A target queued during a gesture inherits its accepted revision")
    busReplies[2](["result":["revision":"s:2","data":[:]]])
    busControls.finishGesture()
    busReplies[3](["error":["message":"Revision changed"]])
    try require(busCalls.last?.0=="mixer.get","A rejected final level edit reloads the authoritative controls")
    busReplies[4](["result":["revision":"s:3","data":["buses":[["id":"bus-b","gainDB":1.0,"mute":false]]]]])
    try require(busCalls.last?.1["gainDB"] as? Double==1 && busCalls.last?.1["expectedRevision"] as? String=="s:3","A rejected preview is reset to current document values, not a stale snapshot")
    busReplies[5](["result":["revision":"s:3","data":[:]]])
    try require(busControls.message.stringValue=="Revision changed" && busControls.gain.slider.doubleValue==1,"Preview conflict retains an explanation after restoring the real value")
    busControls.context(["id":"bus-b","gainDB":1.0],revision:"s:3")
    try require(busControls.message.stringValue=="Revision changed","A parent refresh does not erase the level-edit conflict explanation")
    let scopeView=GraphSignalScope(frame:.zero)
    var scopeRequests=[(String,[String:Any])](),scopeReplies=[([String:Any])->Void]()
    scopeView.currentRevision={"scope:1"};scopeView.onRequest={m,p,r in scopeRequests.append((m,p));scopeReplies.append(r)}
    scopeView.show(port:"one/out/0");scopeView.show(port:nil)
    scopeReplies[0](["result":["revision":"scope:1","data":[:]]])
    try require(scopeRequests.count==2 && scopeRequests.last?.1["port"] is NSNull && scopeView.isHidden,"Releasing Q during capture setup queues a stop without reopening the scope")
    scopeReplies[1](["result":["revision":"scope:1","data":[:]]])
    scopeView.show(port:"two/out/0",spectrum:true);scopeReplies[2](["result":["revision":"scope:1","data":[:]]]);scopeView.poll(visible:true)
    try require(scopeRequests.last?.0=="graph.scope.get" && scopeRequests.last?.1["spectrum"] as? Bool==true,"Spectrum requests use the same bounded host tap")
    scopeView.show(port:nil);scopeReplies[3](["result":["revision":"scope:1","data":["port":"two/out/0","active":true,"frames":64]]])
    try require(scopeView.isHidden && scopeView.plot.value.isEmpty,"A late scope read does not redisplay a closed capture")
    let listenView=GraphListenControls(frame:.zero)
    var listenRequests=[(String,[String:Any])](),listenReplies=[([String:Any])->Void]()
    listenView.currentRevision={"listen:1"};listenView.nameForPort={_ in "Bass output"}
    listenView.onRequest={m,p,r in listenRequests.append((m,p));listenReplies.append(r)}
    listenView.select("bass/out/0");listenView.select(nil)
    try require(listenRequests.count==1,"Stop during monitor selection waits behind the first request")
    listenReplies[0](["result":["revision":"listen:1","data":["port":"bass/out/0","gainDB":0]]])
    try require(listenRequests.count==2 && listenRequests[1].1["port"] is NSNull,"Stop is the final monitor write even when the previous selection completes late")
    listenReplies[1](["result":["revision":"listen:1","data":["port":NSNull()]]])
    try require(listenView.isHidden && listenView.port==nil,"Stopping clears the persistent monitor indicator")
    listenView.select("bass/out/0",gain:-6)
    listenReplies[2](["error":["code":-32001,"message":"Stale revision"]])
    try require(listenRequests.last?.0=="graph.listen.get","A stale monitor write reads the fresh session revision")
    listenView.select(nil)
    listenReplies[3](["result":["revision":"listen:2","data":["port":"bass/out/0"]]])
    try require(listenRequests.last?.1["port"] is NSNull && listenRequests.last?.1["expectedRevision"] as? String=="listen:2","Stop supersedes a stale pending monitor choice")
    listenReplies[4](["result":["revision":"listen:2","data":["port":NSNull()]]])
    listenView.update(["port":"bass/out/0","gainDB":-9.0])
    try require(!listenView.isHidden && listenView.label.stringValue=="Listening: Bass output" && listenView.gain.stringValue=="-9.0","An external API monitor choice is visible with its measured target and independent gain")
    listenView.update(["port":"bass/out/0","gainDB":-9.0,"available":false])
    try require(!listenView.isHidden && !listenView.gain.isEnabled && listenView.label.stringValue.contains("Stop listening to return to normal mix"),"A retired monitor tap keeps its explicit escape to the normal mix and never presents silence as a current signal")
    listenView.update(["port":"bass/out/0","gainDB":-9.0,"available":true])
    try require(listenView.gain.isEnabled && listenView.label.stringValue=="Listening: Bass output","An adopted route can make the same monitored port available again")
    let freshnessScope=GraphSignalScope(frame:.zero)
    var capture:[String:Any]=["port":"bass/out/0","active":true,"available":true,"fresh":true,"frames":64,"sampleRate":48000.0,"waveform":[["minimum":[-0.1,-0.1],"maximum":[0.1,0.1]]],"spectrum":[0.1,0.2],"fftFrames":64]
    freshnessScope.onRequest={method,_,reply in reply(["result":["revision":"fresh:1","data":method=="graph.scope.get" ? capture:[:]]])}
    freshnessScope.show(port:"bass/out/0");freshnessScope.poll(visible:true)
    try require((freshnessScope.plot.value["waveform"] as? [[String:Any]])?.count==1,"A fresh adopted-plan capture reaches the plotted scope")
    capture["fresh"]=false;freshnessScope.poll(visible:true)
    try require((freshnessScope.plot.value["waveform"] as? [Any])?.isEmpty==true && (freshnessScope.plot.value["spectrum"] as? [Any])?.isEmpty==true && freshnessScope.detail.stringValue.contains("current-route samples"),"A routing change clears old plotted PCM even if a stale transport reply still includes arrays")
    capture["available"]=false;capture["fresh"]=true;freshnessScope.poll(visible:true)
    try require(freshnessScope.requestedPort=="bass/out/0" && freshnessScope.detail.stringValue.contains("Tap unavailable in current route") && freshnessScope.plot.value["frames"] as? Int==0,"Retired scope targets retain explanatory identity rather than showing a current silent waveform or retargeting another port")
    capture["available"]=true;freshnessScope.poll(visible:true)
    try require(freshnessScope.plot.value["frames"] as? Int==64 && !freshnessScope.detail.stringValue.contains("unavailable"),"New adopted-plan PCM replaces the unavailable scope state")
    let stableListen=GraphListenControls(frame:.zero)
    var stateChanges=[String?](),portName="Bass output"
    stableListen.onState={stateChanges.append($0)};stableListen.nameForPort={_ in portName}
    stableListen.update([:]);stableListen.update([:])
    try require(stateChanges.count==1 && stateChanges[0]==nil,"Unchanged normal-mix telemetry must not invalidate the global listening control repeatedly")
    stableListen.update(["port":"bass/out/0"]);stableListen.update(["port":"bass/out/0","gainDB":-4.0,"pending":true])
    try require(stateChanges.count==2 && stableListen.gain.stringValue=="-4.0" && stableListen.label.stringValue.hasSuffix("switching"),"Gain and pending status refresh without republishing an unchanged monitor name")
    portName="Renamed bass";stableListen.update(["port":"bass/out/0"]);stableListen.update([:])
    try require(stateChanges.count==4 && stateChanges[2]=="Renamed bass" && stateChanges[3]==nil,"Renaming the listening port and returning to normal mix each publish one state change")
    stableListen.update(["port":"bass/out/0","available":false]);stableListen.update(["port":"bass/out/0","available":false])
    try require(stateChanges.count==5 && stateChanges.last!?.contains("unavailable in current route")==true,"Retired monitor availability reaches the global Stop listening control once without repeated layout updates")
    try require(AppLaunchArguments.documentPath(["app","--ui-test-vst3","fixture.vst3","--ui-test-device","BlackHole 2ch","song.screamseq"],exists:{_ in true})=="song.screamseq","Test option paths must not be opened as songs")
    try require(AppLaunchArguments.isOptionValue(URL(fileURLWithPath:"fixture.vst3").path,arguments:["app","--ui-test-vst3","fixture.vst3"]),"AppKit open-file option events need the same filtering")
    try require(!AppLaunchArguments.isOptionValue("song.screamseq",arguments:["app","--ui-test-vst3","fixture.vst3","song.screamseq"]),"Actual document arguments must remain openable")
    let sliderHost=NSWindow(contentRect:NSRect(x:0,y:0,width:300,height:80),styleMask:[.titled],backing:.buffered,defer:false)
    var sliderValues=[Double](),sliderGestures=[Bool]()
    let directSlider=ParameterSlider(value:0.5,min:0,max:1){sliderValues.append($0)}
    directSlider.frame=NSRect(x:20,y:20,width:260,height:24);sliderHost.contentView?.addSubview(directSlider)
    directSlider.gesture={sliderGestures.append($0)}
    func sliderEvent(_ type:NSEvent.EventType,_ x:CGFloat)->NSEvent {NSEvent.mouseEvent(with:type,location:directSlider.convert(NSPoint(x:x,y:12),to:nil),modifierFlags:[],timestamp:0,windowNumber:sliderHost.windowNumber,context:nil,eventNumber:1,clickCount:1,pressure:1)!}
    directSlider.mouseDown(with:sliderEvent(.leftMouseDown,130));directSlider.mouseDragged(with:sliderEvent(.leftMouseDragged,210));directSlider.mouseUp(with:sliderEvent(.leftMouseUp,230))
    try require(directSlider.doubleValue>0.8 && sliderValues.count>=2 && sliderGestures==[true,false],"Direct slider tracking delivers intermediate values and exactly one completed gesture")
    let controls=GraphPluginControls(frame:.zero)
    var parameterCalls=[String](), editedValues=[[String:Any]]()
    controls.onRequest={method,params,reply in
      parameterCalls.append(method)
      if let values=params["parameters"] as? [[String:Any]] {editedValues=values}
      reply(["result":["revision":"song:1","data":["parameters":[
        ["id":7,"name":"Drive","value":editedValues.first?["value"] ?? 6.0,"writable":true],
        ["id":42,"name":"Tone","value":0.0,"writable":true]],"buses":[]]]])
    }
    controls.context(graph:"recipe",node:"effect")
    try require(parameterCalls==["graph.plugin.get"] && controls.parametersView.values.count==2,"Selecting a graph effect loads controls without an enable button")
    controls.parametersView.set(7,value:18)
    try require(parameterCalls.last=="graph.plugin.set" && (editedValues.first?["id"] as? NSNumber)?.uint32Value==7 && editedValues.first?["value"] as? Double==18,"Graph parameter end editing commits to its stable parameter")
    let count=parameterCalls.count
    controls.parametersView.set(7,value:18)
    try require(parameterCalls.count==count,"Return and focus loss do not duplicate the graph parameter edit")
    controls.context(graph:"recipe",node:"effect",revision:"song:2")
    try require(parameterCalls.last=="graph.plugin.get" && parameterCalls.count==count+1,"Undo or external edits refresh the same selected graph plugin's values")
    let rack=GraphRackControls(frame:.zero)
    var rackRequests=[(String,[String:Any])](),rackReplies=[([String:Any])->Void]()
    rack.onRequest={m,p,r in rackRequests.append((m,p));rackReplies.append(r)}
    rack.context(["id":"old"]);rack.context(["id":"new"])
    rackReplies[0](["result":["revision":"old:1","data":[["id":1,"name":"Retired"]]]])
    try require(rack.values.isEmpty,"Retired processor replies cannot populate the current inspector")
    rackReplies[1](["result":["revision":"new:1","data":[["id":2,"name":"Cutoff","min":0.0,"max":1.0,"value":0.5,"writable":true]]]])
    try require(rack.values.first?["name"] as? String=="Cutoff","Selected rack processor has its own inline parameter controls")
    rack.set(2,value:0.7);rack.set(3,value:0.3);rack.set(4,value:0.4)
    try require(rackRequests.last?.1["plugin"] as? String=="new" && rackRequests.last?.1["expectedRevision"] as? String=="new:1","Graph controls write a stable plugin identity and captured revision")
    rackReplies[2](["result":["revision":"new:2","data":[:]]])
    try require(rackRequests.last?.1["expectedRevision"] as? String=="new:2","Queued gestures use the accepted edit revision")
    rackReplies[3](["result":["revision":"new:3","data":[:]]])
    try require((rackRequests.last?.1["values"] as? [[String:Any]])?.first?["id"] as? UInt32==4,"Rapid edits to distinct parameters are retained, not overwritten")
    rack.context(nil);rackReplies[4](["result":["revision":"new:4","data":[:]]])
    try require(rack.identity==nil && rack.values.isEmpty,"Late writes cannot revive a retired inspector")
    rack.context(["id":"new"])
    rackReplies.last?(["result":["revision":"new:4","data":[["id":2,"name":"Cutoff","min":0.0,"max":1.0,"value":0.7,"writable":true]]]])
    rack.context(["id":"new"],revision:"new:5")
    try require(rackRequests.last?.0=="plugin.parameters.get","Undo on the inspected processor reloads the existing identity")
    rackReplies.last?(["result":["revision":"new:5","data":[["id":2,"name":"Cutoff","min":0.0,"max":1.0,"value":0.2,"writable":true]]]])
    try require(rack.values.first?["value"] as? Double==0.2,"Undo's refreshed value replaces the old parameter reading")
    rack.refresh();let retiredRead=rackReplies.last!
    rack.set(2,value:0.8);let parameterWrite=rackReplies.last!
    retiredRead(["result":["revision":"new:5","data":[]]])
    try require(!rack.values.isEmpty,"An in-flight read cannot erase a subsequent parameter edit")
    parameterWrite(["error":["message":"Song changed; retry this edit"]])
    rackReplies.last?(["result":["revision":"new:6","data":[["id":2,"value":0.4]]]])
    try require(rack.message.stringValue=="Song changed; retry this edit" && rack.values.first?["value"] as? Double==0.4,"Conflict recovery refreshes values without hiding why the edit was refused")
    let editor=SignalGraphEditor(frame:.zero)
    let graphMenu=editor.actionMenu()
    func commandItems(_ menu:NSMenu)->[NSMenuItem] {menu.items.flatMap { item in item.submenu.map(commandItems) ?? (item.action == nil ? []:[item]) }}
    let registered=commandItems(graphMenu)
    try require(Set(registered.compactMap{$0.identifier?.rawValue})==Set(GraphCommand.allCases.map(\.id)) && registered.count==GraphCommand.allCases.count,"Every graph command has exactly one stable identity and a menu/palette path")
    try require(registered.filter{!$0.isEnabled}.allSatisfy{!($0.toolTip ?? "").isEmpty},"Unavailable graph commands explain why")
    let entry=WorkspaceCommandPalette.Entry(item:GraphCommand.bypass.item("Renamed contextual bypass"){},path:"Moved / Different parent")
    try require(entry.id=="graph.bypass","Graph bindings survive renamed labels and relocated menus")
    try require(graphMenu.items.contains{$0.title=="Clone subgraph" && $0.isEnabled} && graphMenu.items.first(where:{$0.title=="Add modulation source"})?.submenu?.items.contains{$0.title=="LFO" && $0.isEnabled && $0.toolTip != nil} == true,"Graph commands offer target selection before a recipe is selected")
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
    editor.selectedID="n102";editor.inspect();editor.layoutSubtreeIfNeeded()
    try require(editor.libraryName.isHiddenOrHasHiddenAncestor && editor.inspectorScroll.documentVisibleRect.intersects(editor.pluginControls.parametersView.search.convert(editor.pluginControls.parametersView.search.bounds,to:editor.inspector)),"Selecting a processor brings its direct parameter search into the visible inspector instead of leaving library metadata above it")
    var request:[String:Any]=[:],reply:(([String:Any])->Void)?
    editor.onRequest={method,params,respond in request=params;reply=respond}
    editor.mutate("graph.assign",["target":"n1","graph":"n100"])
    try require(editor.loading && request["expectedRevision"] != nil,"Graph edits carry their displayed revision")
    reply?(["error":["message":"Song changed"]])
    try require(!editor.loading && editor.status.stringValue=="Song changed","Conflicts remain visible without replacing the graph")
    editor.mutate("graph.assign",["target":"n1","graph":"n100"])
    var deferredRuns=0;editor.prepareCommand{deferredRuns+=1}
    reply?(["error":["message":"Preparation failed"]])
    try require(editor.deferredGraphCommand==nil && deferredRuns==0,"A failed mutation cancels a waiting command rather than unexpectedly running it after a later refresh")
    let retrying=SignalGraphEditor(frame:.zero)
    var requestRevision="retry-song:1",writes=[[String:Any]](),reads=0,completed=0
    var retryGraph:[String:Any]=["plugins":[["id":"stable","name":"Gain","isInstrument":false]],"mixer":["buses":[["id":"n1","kind":"track","name":"Track","output":"n2","inserts":["stable"]],["id":"n2","kind":"master","name":"Master"]]]]
    retrying.onRequest={method,params,reply in
      if method=="graph.get"{reads+=1;reply(["result":["revision":requestRevision,"data":retryGraph]]);return}
      if method=="graph.song.group.create"{
        writes.append(params)
        if writes.count<3{reply(["error":["code":-32002,"message":"The document is busy; retry shortly"]])}
        else{requestRevision="retry-song:2";retryGraph["groups"]=[["id":"n400","parent":"","name":"Group","nodes":["plugin:stable"]]];reply(["result":["revision":requestRevision,"data":["group":"n400"]]])}
      }else{reply(["result":["data":[]]])}
    }
    retrying.load();retrying.selectedID="plugin:stable";retrying.canvas.selected="plugin:stable"
    retrying.mutate("graph.song.group.create",["nodes":["plugin:stable"]]){_ in completed+=1}
    let retryDeadline=Date().addingTimeInterval(2)
    while retrying.loading && Date()<retryDeadline{RunLoop.current.run(until:Date().addingTimeInterval(0.01))}
    try require(writes.count==3 && completed==1 && reads==2 && writes.allSatisfy{$0["expectedRevision"] as? String=="retry-song:1" && $0["nodes"] as? [String]==["plugin:stable"]},"Definite busy rejections retry the captured write without rebasing, duplicating completion or losing its refresh")
    try require(retrying.selectedID=="n400" && retrying.canvas.selected=="n400" && retrying.name.stringValue=="Group" && !retrying.inspectorScroll.isHidden,"Grouping replaces the hidden child selection with its returned boundary before refresh and retains the rename completion")
    var failures=0
    retrying.onRequest={_,_,reply in failures+=1;reply(["error":["code":-32001,"message":"Song changed"]])}
    retrying.mutate("graph.song.group.create",[:]);RunLoop.current.run(until:Date().addingTimeInterval(0.2))
    try require(failures==1 && !retrying.loading,"A stale revision is never retried as though it were a busy rejection")
    var pendingRead:(([String:Any])->Void)?,queuedWrite:[String:Any]?
    retrying.onRequest={method,params,reply in
      if method=="graph.get"{pendingRead=reply}else{queuedWrite=params;reply(["error":["code":-32001,"message":"Captured revision is stale"]])}
    }
    retrying.load();retrying.mutate("graph.assign",["target":"original-target"])
    pendingRead?(["result":["revision":"retry-song:3","data":[:]]])
    RunLoop.current.run(until:Date().addingTimeInterval(0.1))
    try require(queuedWrite?["expectedRevision"] as? String=="retry-song:2" && queuedWrite?["target"] as? String=="original-target","An edit begun during a graph refresh is retained with its original revision and target")
    queuedWrite=nil;retrying.load();retrying.mutate("graph.assign",["target":"original-target"])
    pendingRead?(["result":["revision":"different-song:1","data":[:]]])
    RunLoop.current.run(until:Date().addingTimeInterval(0.1))
    try require(queuedWrite==nil && retrying.status.stringValue.contains("song changed"),"A queued edit is cancelled rather than retargeted after a document switch")
    let catalogs=SignalGraphEditor(frame:.zero)
    catalogs.graphID="n100";catalogs.update(data)
    var catalogAttempts=0
    catalogs.onRequest={method,_,reply in
      guard method=="graph.plugin.get" else{reply(["result":["data":[:]]]);return}
      catalogAttempts+=1
      if catalogAttempts==1{reply(["error":["code":-32002,"message":"Busy"]])}
      else{reply(["error":["code":-32003,"message":"Plugin unavailable"]])}
    }
    catalogs.loadPortCatalogs();RunLoop.current.run(until:Date().addingTimeInterval(0.15))
    try require(catalogAttempts==2 && catalogs.portCatalogs["n102"]==nil && !catalogs.catalogLoading && catalogs.status.stringValue.contains("Reload graph"),"Port reads retry busy rejection, expose failure and never cache an empty success or loop indefinitely")
    catalogs.catalogFailures=[:]
    catalogs.onRequest={_,_,reply in catalogAttempts+=1;reply(["result":["data":["audioBuses":[["index":1,"direction":"input","name":"Detector"]]]]])}
    catalogs.loadPortCatalogs()
    try require(catalogAttempts==3 && (catalogs.portCatalogs["n102"]?["audioBuses"] as? [[String:Any]])?.first?["name"] as? String=="Detector","An explicit retry recovers a failed catalogue read")
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count {
      let directory=URL(fileURLWithPath:CommandLine.arguments[index+1],isDirectory:true);try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
      if let bitmap=editor.bitmapImageRepForCachingDisplay(in:editor.bounds){editor.cacheDisplay(in:editor.bounds,to:bitmap);try bitmap.representation(using:.png,properties:[:])?.write(to:directory.appendingPathComponent("SignalGraphEditor.png"))}
    }
    let cold=SignalGraphEditor(frame:.zero)
    var coldReply:(([String:Any])->Void)?,coldRequests=0
    cold.onRequest={_,_,reply in coldRequests+=1;coldReply=reply}
    cold.onReveal={[weak cold] in cold?.load()}
    var coldTarget:String?
    cold.withSongPlugin(title:"Bypass plugin"){coldTarget=$0}
    try require(cold.loading && coldRequests==1 && cold.targetMenu.entries.isEmpty,"A cold palette command joins the read started by revealing its hidden graph")
    coldReply?(["result":["revision":"cold:1","data":["plugins":[["id":"cold-plugin","name":"Delay","format":"VST3"]]]]])
    try require(cold.targetMenu.entries.map(\.id)==["cold-plugin"] && cold.deferredGraphCommand==nil && coldTarget==nil,"The loaded graph opens a target picker once without performing the edit")
    cold.targetMenu.choose()
    try require(coldTarget=="cold-plugin","The cold-start picker resolves its prepared target")
    let targeting=SignalGraphEditor(frame:.zero)
    targeting.update(["plugins":[["id":"stable-plugin","name":"Delay","format":"VST3"]]])
    var chosenTarget:String?
    var revealedTarget=false;targeting.onReveal={revealedTarget=true}
    targeting.withSongPlugin(title:"Bypass plugin"){chosenTarget=$0}
    try require(revealedTarget && targeting.targetMenu.entries.map(\.id)==["stable-plugin"] && chosenTarget==nil,"A command without selection reveals its panel and offers real stable plugin targets without editing")
    targeting.targetMenu.choose()
    try require(chosenTarget=="stable-plugin","Target search resolves the chosen stable plugin")
    chosenTarget=nil
    targeting.withSongPlugin(title:"Bypass plugin"){chosenTarget=$0}
    targeting.onRequest={_,_,reply in reply(["result":["revision":"other-document:1","data":[:]]])}
    targeting.load();targeting.targetMenu.choose()
    try require(chosenTarget==nil && targeting.status.stringValue.contains("song changed"),"An old target search cannot apply a command after a document revision changes")
    var song=data;song["plugins"]=[["id":"compressor","name":"Compressor","format":"AU","isInstrument":false]]
    song["mixer"]=["buses":[["id":"n1","name":"Drums","kind":"track","output":"n2","inserts":["compressor"]],["id":"n2","name":"Master","kind":"master","output":""],["id":"n3","name":"Key","kind":"track","output":"n2"]],"sidechains":[["source":"n3","plugin":"compressor","input":1]]]
    song["assignments"]=[["target":"n1","graph":"n100","amount":0.5,"wet":0.8]]
    song["layout"]=[["node":"plugin:compressor","x":900.0,"y":300.0]]
    editor.graphID=nil;editor.filterID="n1";editor.update(song)
    func measuredPort(_ node:String,_ output:Bool,_ peak:Double,_ clipped:Bool=false)->[String:Any]{["key":node+(output ? "/out/0":"/in/0"),"node":node,"port":0,"direction":output ? "output":"input","name":node,"measured":true,"peak":[peak,peak],"rms":[peak,peak],"clipped":clipped,"channels":2]}
    editor.showSignals(["active":false,"ports":[measuredPort("plugin:compressor",true,2,true)]])
    editor.showSignals(["routing":["state":"preparing"]])
    try require(!editor.routingStatus.isHidden && editor.routingStatus.stringValue.contains("previous route"),"Prepared routing is visibly distinct from the audible plan")
    editor.showSignals(["routing":["state":"failed"]])
    try require(editor.routingStatus.stringValue.contains("Undo"),"Failed routing retains an explicit restoration path")
    editor.showSignals(["routing":["state":"stable"]])
    try require(editor.routingStatus.isHidden,"Stable playback does not add permanent status chrome")
    editor.findOverload();try require(editor.status.stringValue.hasPrefix("Stopped"),"Stopped meters cannot diagnose a current overload")
    editor.showSignals(["active":true,"ports":[measuredPort("plugin:compressor",false,0.5),measuredPort("plugin:compressor",true,0)]])
    editor.selectedID="n1";editor.traceSilence()
    try require(editor.selectedID=="plugin:compressor" && editor.status.stringValue.contains("input active; output silent"),"Tracing a channel visits its effect and reports only the measured silence boundary")
    editor.showSignals(["active":true,"ports":[measuredPort("plugin:compressor",true,2,true)]])
    editor.findOverload();try require(editor.selectedID=="plugin:compressor" && editor.status.stringValue.contains("over 0 dBFS") && !editor.status.stringValue.contains("CLIP"),"Overload navigation selects the measured processor without claiming float headroom is already clipped")
    try require(editor.scroll.documentVisibleRect.contains(editor.canvas.nodes.first{$0.id=="plugin:compressor"}!.rect),"Overload framing keeps the complete processor visible beside the inspector")
    let farNode=SignalCanvasNode(id:"far",title:"Far boundary",detail:"",kind:"plugin",x:5400,y:3700)
    editor.canvas.update([farNode],edges:[]);editor.canvas.selected="far";editor.frameSelection()
    try require(editor.scroll.documentVisibleRect.contains(farNode.rect),"Framing centres a far-boundary node after zoom without clipping its ports")
    var invalidPort=measuredPort("plugin:compressor",true,0);invalidPort["peak"]=[Double.nan,0]
    try require(GraphPortReading(invalidPort)?.measured==false,"Invalid telemetry never creates a misleading meter")
    editor.graphID=nil;editor.filterID="n1";editor.update(song)
    try require(editor.canvas.nodes.contains{$0.id=="n3"} && editor.canvas.nodes.contains{$0.id=="plugin:compressor" && $0.x==900},"Filtered song graph preserves external dependencies and saved processor positions")
    let copy=editor.canvas.nodes.first{$0.id.hasPrefix("graph:")}!
    editor.showActivity([["target":"n1","graph":"n100","role":"ordinary","order":2,"tail":false]],playing:true)
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.detail=="Drums · Ordinary · playing #2" && editor.canvas.nodes.first{$0.id==copy.id}?.activity=="playing","Song graph exposes the owner and actual processor stack order")
    editor.showActivity([],playing:false)
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.activity==nil,"Stopping playback retires graph activity without altering layout")
    var activityInstrumentSong=song
    activityInstrumentSong["instruments"]=[["id":"n80","index":1,"name":"Keys","plugin":false]]
    activityInstrumentSong["instrumentAssignments"]=[["target":"n80","graph":"n100","amount":1.0,"wet":1.0]]
    let instrumentContext=SignalGraphEditor(frame:.zero);instrumentContext.update(activityInstrumentSong)
    let instrumentCopies=instrumentContext.canvas.nodes.filter{$0.id.hasPrefix("instrument-graph:")}
    try require(instrumentCopies.count==2 && Set(instrumentCopies.map(\.detail))==["Drums · Instrument I1 · independent copy","Key · Instrument I1 · independent copy"],"Stopped instrument copies retain the owning channel and instrument identity")
    instrumentContext.showActivity([["target":"n1","graph":"n100","instrument":"n80","role":"instrument","order":1,"tail":false]],playing:true)
    try require(instrumentContext.canvas.nodes.first{$0.id=="instrument-graph:n80:n1"}?.detail=="Drums · Instrument I1 · playing #1","Playback activity never replaces instrument copy context with an anonymous role")
    let structural=editor.songConnections.firstIndex{($0["kind"] as? String)==nil}!
    editor.selectConnection(structural)
    try require(editor.connectionForm.isHidden && !editor.openConnectionOwnerButton.isHidden && editor.updateConnectionButton.isHidden,"Fixed chain wires explain their ownership instead of displaying controls that cannot change them")
    editor.openConnectionOwner();try require(editor.graphID=="n100","A chain wire links directly to its editable subgraph")
    editor.graphID=nil;editor.update(song)
    try require(copy.role=="Ordinary","Subgraph instance role remains available at semantic overview zoom")
    editor.openSongNode(copy.id);try require(editor.graphID=="n100","Opening a channel copy navigates to its shared library definition")
    var nested=song
    nested["mixer"]=["buses":[["id":"n1","name":"Drums","kind":"track","output":"n2","inserts":["compressor"]],["id":"n2","name":"Master","kind":"master","output":""],["id":"n3","name":"Sidechain group","kind":"group","output":"n2"],["id":"n4","name":"Nested group","kind":"group","output":"n3"],["id":"n5","name":"Nested source","kind":"track","output":"n4"],["id":"n6","name":"Unrelated","kind":"track","output":"n2"]],"sidechains":[["source":"n3","plugin":"compressor","input":1]]]
    editor.graphID=nil;editor.filterID="n1";editor.update(nested)
    try require(editor.canvas.nodes.contains{$0.id=="n5"} && !editor.canvas.nodes.contains{$0.id=="n6"},"Channel filters recursively retain grouped sidechain inputs without unrelated master siblings")
    editor.onRequest=nil
    editor.filterID=nil;editor.nodeSearch.stringValue="";editor.update(song)
    let originalPositions=Dictionary(uniqueKeysWithValues:editor.canvas.nodes.map{($0.id,NSPoint(x:$0.x,y:$0.y))})
    editor.scroll.magnification=0.8
    editor.nodeSearch.stringValue="Compressor";editor.changeNodeFilter()
    try require(editor.scroll.magnification==0.8,"Filtering does not unexpectedly zoom the canvas")
    editor.clearNodeFilters()
    try require(editor.canvas.nodes.allSatisfy{originalPositions[$0.id]==NSPoint(x:$0.x,y:$0.y)},"Filtering restores existing node positions")
    let dense=SignalGraphEditor(frame:.zero)
    var denseBuses=(0..<16).map{["id":"n\($0+1)","name":$0<2 ? "Duplicate":"Track \($0+1)","kind":"track","output":"n20"]}
    denseBuses.append(["id":"n20","name":"Master","kind":"master","output":""])
    var denseData:[String:Any]=["mixer":["buses":denseBuses]]
    var denseRevision="first-song:1"
    dense.onRequest={_,_,reply in reply(["result":["revision":denseRevision,"data":denseData]])}
    dense.load()
    let originalBreadcrumb=dense.breadcrumbs.arrangedSubviews.first!
    denseRevision="first-song:2";denseData["activity"]=[["order":1]];dense.load()
    try require(dense.breadcrumbs.arrangedSubviews.first === originalBreadcrumb && dense.revision==denseRevision,
      "Unrelated pattern revisions preserve native graph views while advancing revision guards")
    denseBuses[0]["name"]="Renamed channel";denseData["mixer"]=["buses":denseBuses];dense.load()
    try require(dense.canvas.nodes.first{$0.id=="n1"}?.title=="Renamed channel" && dense.breadcrumbs.arrangedSubviews.first !== originalBreadcrumb,
      "A changed graph projection still rebuilds immediately")
    denseBuses[0]["name"]="Duplicate";denseData["mixer"]=["buses":denseBuses];dense.load()
    let fullPositions=Dictionary(uniqueKeysWithValues:dense.canvas.nodes.map{($0.id,NSPoint(x:$0.x,y:$0.y))})
    try require(dense.filter.itemTitles.contains("n1 · Duplicate") && dense.filter.itemTitles.contains("n2 · Duplicate"),"Duplicate bus names remain unambiguous in the channel filter")
    dense.filterID="n16";dense.update(denseData)
    try require(dense.canvas.nodes.count==2 && dense.canvas.nodes.allSatisfy{$0.y<300},"A filtered channel gets a compact provisional layout, independent of the full-song cache")
    dense.filterID=nil;dense.update(denseData)
    try require(dense.canvas.nodes.allSatisfy{fullPositions[$0.id]==NSPoint(x:$0.x,y:$0.y)},"Returning to all channels restores the original provisional positions")
    var positioned=denseData;positioned["layout"]=[["node":"n16","x":701.0,"y":1600.0]]
    dense.filterID="n16";dense.update(positioned)
    try require(dense.canvas.nodes.first{$0.id=="n16"}?.y==1600,"Explicit musician layouts remain authoritative in filtered views")
    dense.update(denseData)
    try require(dense.canvas.nodes.allSatisfy{$0.y<300},"Undoing an explicit layout restores the provisional view instead of retaining the undone positions")
    dense.selectedID="n16";dense.graphOrigin="Old channel";dense.graphTarget="n16";dense.nodeSearch.stringValue="Track"
    denseRevision="second-song:1";dense.load()
    try require(dense.filterID==nil && dense.selectedID==nil && dense.graphID==nil && dense.canvas.nodes.count==17,"A new song cannot inherit filters or selection from coincidentally equal node IDs")
    try require(dense.graphOrigin==nil && dense.graphTarget==nil && dense.nodeSearch.stringValue.isEmpty,"A new song starts at the song graph without stale breadcrumbs or search")
    let placement=editor.freePosition(near:NSPoint(x:30,y:30))
    try require(!editor.canvas.nodes.contains{$0.rect.intersects(NSRect(origin:placement,size:NSSize(width:180,height:100)).insetBy(dx:-12,dy:-12))},"Contextual Add finds a nearby non-overlapping position")
    editor.selectedID="n1";editor.canvas.selected="n1";editor.navigate(graph:"n100",origin:"Drums")
    try require(editor.scopeLabel.stringValue.contains("all uses"),"Shared group scope is visible before editing")
    editor.navigateParent()
    try require(editor.selectedID=="n1" && abs(editor.scroll.magnification-0.8)<0.001,"Parent breadcrumb restores selection and zoom")
    let insertCable=editor.songConnections.firstIndex{$0["kind"] as? String=="insert"}!
    editor.selectConnection(insertCable)
    try require(editor.addDestination.target=="n1" && editor.addDestination.before=="compressor","Add captures the selected cable's exact rack destination")
    editor.graphID="n100";editor.selectedID=nil;editor.update(data)
    let parameterNode=editor.canvas.nodes.first{$0.id=="n102"}!
    try require(parameterNode.inputs.contains{$0.modulation && $0.number==7},"Stable parameter is exposed as an individual modulation socket")
    editor.selectConnection(2)
    try require(editor.canvas.selectedEdge==2 && editor.parameter.stringValue=="7" && editor.connectionKind.titleOfSelectedItem=="Modulation","Clicking a modulation wire loads its destination settings")
    editor.layoutSubtreeIfNeeded()
    try require(editor.nodeSection.isHidden && !editor.connectionForm.isHidden && editor.updateConnectionButton.isHidden && editor.connectButton.isHidden,"A selected wire exposes live properties without an Apply or Connect action")
    editor.canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"\r",charactersIgnoringModifiers:"\r",isARepeat:false,keyCode:36)!)
    try require(editor.minimum.currentEditor() != nil,"Return on a modulation wire focuses its editable range")
    editor.update(data)
    try require(editor.minimum.currentEditor() != nil,"Refreshing the same selected cable never briefly hides its active field editor")
    window.makeFirstResponder(editor.canvas)
    var updated=[String:Any]()
    editor.onRequest={method,p,reply in updated=p;reply(["error":["message":"test captured"]])}
    editor.minimum.stringValue="0.2";editor.maximum.stringValue="0.8";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.minimum));editor.controlTextDidEndEditing(Notification(name:NSControl.textDidEndEditingNotification,object:editor.minimum))
    let updatedDefinition=updated["definition"] as? [String:Any] ?? [:]
    try require((updatedDefinition["audio"] as? [[String:Any]])?.count==2 && (updatedDefinition["modulation"] as? [[String:Any]])?.first?["minimum"] as? Double==0.2,"Wire editing preserves audio topology")
    editor.canvas.selectNodes(["n102"]);var detachMethod=""
    editor.onRequest={method,p,reply in detachMethod=method;updated=p;reply(["error":["message":"captured"]])}
    editor.canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:.control,timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"x",charactersIgnoringModifiers:"x",isARepeat:false,keyCode:7)!)
    try require(detachMethod=="graph.nodes.detach" && updated["remove"] as? Bool==true && updated["nodes"] as? [String]==["n102"],"Control-X invokes atomic delete-and-heal while Command-X is untouched")
    editor.cutConnections([0,2])
    let cutDefinition=updated["definition"] as? [String:Any] ?? [:]
    try require((cutDefinition["audio"] as? [[String:Any]])?.count==1 && (cutDefinition["modulation"] as? [[String:Any]])?.isEmpty==true,"One cut stroke removes only crossed audio and modulation edges in one transaction")
    editor.selectConnection(2)
    editor.canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:0,context:nil,characters:"\u{7f}",charactersIgnoringModifiers:"\u{7f}",isARepeat:false,keyCode:51)!)
    let disconnected=updated["definition"] as? [String:Any] ?? [:]
    try require((disconnected["audio"] as? [[String:Any]])?.count==2 && (disconnected["modulation"] as? [[String:Any]])?.isEmpty==true,"Delete removes precisely the selected modulation wire")
    var opened="";editor.onRequest={method,p,reply in opened=method;updated=p;reply(["error":["message":"captured"]])}
    editor.openNode("n102")
    try require(opened=="graph.plugin.editor.open" && updated["node"] as? String=="n102","Opening a library plugin opens its custom editor directly")
    editor.graphID=nil;editor.filterID=nil;editor.update(data)
    let output=editor.songConnections.firstIndex{$0["kind"] as? String=="output"}!
    editor.selectConnection(output)
    try require(!editor.source.isEnabled && editor.destination.isEnabled,"Main output fixes its owning bus and allows retargeting the destination")
    updated=[:];editor.updateSongConnection(output,source:"n2",target:"n1")
    try require(updated.isEmpty,"Editing an output cannot silently reroute a different bus")
    editor.disconnect()
    try require(opened=="graph.connections.remove" && (updated["connections"] as? [[String:Any]])?.first?["kind"] as? String=="output","Deleting a song main-output wire uses the atomic semantic cable API")
    var pluginSong=data;pluginSong["plugins"]=[["id":"synth","name":"Synth","isInstrument":true]]
    editor.update(pluginSong)
    let synthOutput=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-output"}!
    editor.selectConnection(synthOutput);editor.disconnect()
    try require(opened=="graph.connections.remove" && (updated["connections"] as? [[String:Any]])?.first?["plugin"] as? String=="synth","Deleting a default instrument wire uses the shared API that suppresses the master fallback")
    var pluginMixer=pluginSong["mixer"] as! [String:Any];pluginMixer["instruments"]=[["plugin":"synth","output":0,"target":""]];pluginSong["mixer"]=pluginMixer
    editor.update(pluginSong)
    try require(editor.canvas.nodes.contains{$0.id=="plugin:synth"} && !editor.songConnections.contains{$0["kind"] as? String=="plugin-output"},"Disconnected instruments stay visible without a phantom master wire")
    editor.onRequest=nil
    var instrumentSong=data;instrumentSong["instruments"]=[["index":1,"id":"n80","name":"Piano","plugin":false]];instrumentSong["instrumentAssignments"]=[["target":"n80","graph":"n100","amount":0.4,"wet":0.7]]
    editor.graphID=nil;editor.selectedID="instrument:n80";editor.update(instrumentSong)
    try require(editor.canvas.nodes.contains{$0.id=="instrument-graph:n80:n1"} && editor.assignAmount.doubleValue==0.4,"Instrument graph copies remain connected to their individual channel inputs")
    editor.onRequest={_,p,reply in updated=p;reply(["error":["message":"test captured"]])};editor.assignSampleInstrument(editor.selectedInstrument!)
    try require(updated["instrument"] as? Int==1 && updated["graph"] as? String=="n100","Instrument graph assignment uses the public API with its target")
    editor.selectedID="instrument-graph:n80:n1";editor.update(instrumentSong)
    try require(editor.selectedInstrument?["id"] as? String=="n80" && editor.assignAmount.doubleValue==0.4 && editor.assignmentHeading.stringValue=="INSTRUMENT GRAPH · ALL CHANNELS" && editor.detail.stringValue.contains("Piano") && editor.name.isHidden,"Inspecting an instrument copy exposes its actual instrument assignment, never the owning channel's ordinary graph or name editor")
    updated=[:];editor.assignment.sendAction(editor.assignment.action,to:editor.assignment.target)
    try require(updated["instrument"] as? Int==1 && updated["target"]==nil,"Changing the instrument-copy assignment cannot silently change the channel's ordinary stage")
    editor.onRequest=nil
    let wireCanvas=SignalCanvas(frame:NSRect(x:0,y:0,width:800,height:400))
    let a=SignalCanvasNode(id:"a",title:"A",detail:"",kind:"audio",x:40,y:40,outputs:[SignalCanvasPort(),SignalCanvasPort(number:2,label:"Aux")])
    let b=SignalCanvasNode(id:"b",title:"B",detail:"",kind:"audio",x:440,y:40,inputs:[SignalCanvasPort(),SignalCanvasPort(number:3,label:"Side")])
    wireCanvas.update([a,b],edges:[SignalCanvasEdge(source:"a",target:"b",label:"",output:2,input:3)])
    try require(wireCanvas.edge(at:NSPoint(x:330,y:121))==0 && wireCanvas.edge(at:NSPoint(x:330,y:20))==nil,"Wire hit testing follows the selected ports")
    try require(wireCanvas.crossedEdges(from:NSPoint(x:330,y:90),to:NSPoint(x:330,y:150))==[0] && wireCanvas.crossedEdges(from:NSPoint(x:330,y:10),to:NSPoint(x:330,y:20)).isEmpty,"Cut strokes intersect real cable geometry, not node rectangles")
    wireCanvas.cutTool=true
    wireCanvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:0,context:nil,characters:"\u{1b}",charactersIgnoringModifiers:"\u{1b}",isARepeat:false,keyCode:53)!)
    try require(!wireCanvas.cutTool,"Escape cancels the cut tool without a graph mutation")
    wireCanvas.edges[0].label="Auxiliary"
    try require(wireCanvas.edge(at:NSPoint(x:300,y:107))==0,"The visible wire label is clickable as well as the curve")
    var editedWire:Int?;wireCanvas.onEditEdge={editedWire=$0}
    let wireHost=NSWindow(contentRect:wireCanvas.bounds,styleMask:[.titled],backing:.buffered,defer:false);wireHost.contentView=wireCanvas
    let middle=wireCanvas.convert(NSPoint(x:330,y:121),to:nil)
    wireCanvas.mouseDown(with:NSEvent.mouseEvent(with:.leftMouseDown,location:middle,modifierFlags:[],timestamp:0,windowNumber:wireHost.windowNumber,context:nil,eventNumber:1,clickCount:2,pressure:1)!)
    try require(editedWire==0,"Double-clicking the actual wire opens its connection settings")
    let envelope=GraphEnvelopeEditor(frame:NSRect(x:0,y:0,width:850,height:260))
    var envelopeReply:(([String:Any])->Void)?,envelopeParams=[String:Any]()
    envelope.onRequest={_,p,r in envelopeParams=p;envelopeReply=r}
    envelope.context(graph:"n100",node:["id":"n105","kind":"automation","name":"Motion"],patterns:[["index":0,"rows":64]],revision:"v1")
    envelopeReply?(["result":["revision":"v1","data":["rows":64,"rowsPerBeat":4,"points":[]]]])
    envelope.ramp();envelope.context(graph:"n200",node:["id":"n205","kind":"automation"],patterns:[["index":1]],revision:"v2")
    try require(envelope.node=="n105" && envelope.hasDraft,"Graph automation draft retains its original target when graph selection changes")
    envelope.curve.selectItem(at:8);envelope.changeCurve();envelope.formula.stringValue="mix(start,end,t^2)";envelope.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:envelope.formula))
    envelope.apply()
    try require(envelopeParams["node"] as? String=="n105" && envelopeParams["expectedRevision"] as? String=="v1" && (envelopeParams["points"] as? [[String:Any]])?.first?["formula"] as? String=="mix(start,end,t^2)","Graph formula edits carry captured target and revision")
    envelope.ramp();envelopeReply?(["result":["revision":"v3","data":[:]]])
    try require(envelope.hasDraft && envelope.revision=="v3","Edits made during an in-flight Apply remain pending")
    envelope.onRequest=nil
    try graphEnvelopeCommitChecks()
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
    try cableRoutingChecks()
    print("PASS shared graph UI: distinct audio/modulation edges, channel context, compact canvas and revision-checked errors")
  }
}

extension InterfaceTests {
  static func graphFollowerGestureChecks() throws {
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:900,height:500),styleMask:[.titled],backing:.buffered,defer:false)
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:900,height:500));window.contentView=canvas
    let audio=SignalCanvasPort(number:2,label:"Aux 2"),parameter=SignalCanvasPort(number:4294967295,label:"Tone",modulation:true,signal:.parameter)
    let a=SignalCanvasNode(id:"a",title:"Audio",detail:"",kind:"plugin",x:30,y:70,inputs:[],outputs:[audio])
    let b=SignalCanvasNode(id:"b",title:"Target",detail:"",kind:"plugin",x:510,y:70,inputs:[parameter],outputs:[])
    canvas.update([a,b],edges:[])
    var drops=[(String,String,UInt32,UInt32)](),normal=0,rewires=0
    canvas.onAudioParameterDrop={s,t,o,p,_ in drops.append((s,t,o,p))};canvas.onConnectPorts={_,_,_,_,_ in normal+=1};canvas.onRewire={_,_,_,_,_,_ in rewires+=1}
    func mouse(_ type:NSEvent.EventType,_ point:NSPoint)->NSEvent {NSEvent.mouseEvent(with:type,location:canvas.convert(point,to:nil),modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,eventNumber:1,clickCount:1,pressure:1)!}
    let start=a.portPoint(audio,output:true),end=b.portPoint(parameter,output:false)
    for (from,to) in [(start,end),(end,start)] {canvas.mouseDown(with:mouse(.leftMouseDown,from));canvas.mouseDragged(with:mouse(.leftMouseDragged,to));canvas.mouseUp(with:mouse(.leftMouseUp,to))}
    try require(drops.count==2 && drops.allSatisfy{$0.0=="a" && $0.1=="b" && $0.2==2 && $0.3==UInt32.max} && normal==0 && rewires==0,"Fresh cables offer a follower from either direction with the exact physical output and stable parameter ID")
    try require(!canvas.permitsFollowerDrop(from:audio,to:parameter,output:true,rewiring:true) && !canvas.permitsFollowerDrop(from:.init(modulation:true,signal:.control),to:audio,output:true,rewiring:false),"Existing rewires and control-to-audio drags cannot introduce an implicit converter")
    let catalogue:[[String:Any]]=[["id":UInt32.max,"name":"Tone","min":0.0,"max":1.0,"value":0.6,"writable":true,"canSlide":true]]
    let song:[String:Any]=["library":[],"plugins":[["id":"stable","name":"Tone","slot":0]],"mixer":["enabled":true,"buses":[["id":"n2","name":"Channel 1","kind":"track","inserts":["stable"]]]]]
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:900,height:600));editor.update(song);editor.portCatalogs["plugin:stable"]=["parameters":catalogue]
    var requests=[(String,[String:Any])]();editor.onRequest={method,params,_ in requests.append((method,params))}
    editor.offerAudioFollower("n2","plugin:stable",output:0,parameter:UInt32.max,position:NSPoint(x:200,y:220))
    try require(requests.isEmpty && editor.targetMenu.entries.first?.id=="follower","Audio-to-parameter drop offers a converter without changing the song")
    editor.targetMenu.choose()
    guard let request=requests.first,let source=request.1["source"] as? [String:Any],let connect=request.1["connect"] as? [String:Any]else{throw InterfaceFailure(message:"Follower gesture did not submit one atomic request")}
    try require(requests.count==1 && request.0=="graph.song.source.add" && source["kind"] as? String=="follower" && source["audioBus"] as? String=="n2" && connect["plugin"] as? String=="stable" && connect["parameter"] as? UInt32==UInt32.max,"Song converter creates the exact audio tap and modulation target in one request")
    let recipe=SignalGraphEditor(frame:NSRect(x:0,y:0,width:900,height:600));recipe.graphID="n10"
    recipe.update(["library":[["id":"n10","name":"Recipe","nodes":[["id":"n11","kind":"input"],["id":"n12","kind":"plugin","plugin":["name":"Tone"]]],"audio":[],"modulation":[]]]])
    recipe.portCatalogs["n12"]=["parameters":catalogue];var recipeCalls=[(String,[String:Any])]();recipe.onRequest={method,params,_ in recipeCalls.append((method,params))}
    recipe.offerAudioFollower("n11","n12",output:2,parameter:UInt32.max,position:NSPoint(x:300,y:240));recipe.targetMenu.choose()
    let recipeRequest=recipeCalls.first,route=recipeRequest?.1["audioInput"] as? [String:Any],target=recipeRequest?.1["connect"] as? [String:Any]
    try require(recipeCalls.count==1 && recipeRequest?.0=="graph.node.add" && route?["node"] as? String=="n11" && route?["port"] as? UInt32==2 && target?["node"] as? String=="n12" && target?["base"] as? Double==0.6,"Recipe conversion preserves baseline, source bus and target parameter atomically")
    let stale=SignalGraphEditor(frame:NSRect(x:0,y:0,width:900,height:600));stale.update(song);var throwawayWrites=0
    stale.onRequest={method,_,reply in if method=="graph.get"{reply(["result":["revision":"changed","data":song]])}else{throwawayWrites+=1}}
    stale.offerAudioFollower("n2","plugin:stable",output:0,parameter:UInt32.max,position:.zero);stale.load();stale.targetMenu.choose()
    try require(throwawayWrites==0,"A stale follower picker cannot mutate a changed song")
    print("PASS audio-to-parameter gesture: typed sockets, both drag directions, explicit converter, zero-depth atomic requests and stale cancellation")
  }
}

extension InterfaceTests {
  static func graphActivityLabelChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    var song:[String:Any]=["library":[["id":"A","number":1,"name":"Delay","nodes":[]]],
      "assignments":[["target":"track","graph":"A","amount":1.0,"wet":1.0]],
      "mixer":["buses":[["id":"track","name":"Layer","kind":"track","output":"master"],
        ["id":"other","name":"Layer","kind":"track","output":"master"],
        ["id":"master","name":"Master","kind":"master","output":""]]]]
    let activity:[[String:Any]]=[["target":"track","graph":"A","role":"ordinary","order":1,"tail":false]]
    editor.update(song);editor.showActivity(activity,playing:true)
    let id="graph:track:Ordinary:A"
    try require(editor.canvas.nodes.first{$0.id==id}?.detail=="track · Layer · Ordinary · playing #1" && editor.canvas.stages.first?.title=="track · Layer › Ordinary · 1 copy","Live copy and stage labels disambiguate duplicate bus names consistently")
    let generation=editor.stagePresentation.generation,positions=editor.canvas.nodes.map(\.rect)
    for _ in 0..<12 {editor.showActivity(activity,playing:true)}
    try require(editor.stagePresentation.generation==generation && editor.canvas.nodes.map(\.rect)==positions,"Unchanged meter polls reuse static labels and stage geometry without moving cards")
    song["mixer"]=["buses":[["id":"track","name":"Bass","kind":"track","output":"master"],
      ["id":"other","name":"Layer","kind":"track","output":"master"],
      ["id":"master","name":"Master","kind":"master","output":""]]]
    var revision=2
    editor.onRequest={method,_,reply in guard method=="graph.get" else{return};reply(["result":["revision":"label-song:\(revision)","data":song]])}
    editor.load();editor.showActivity(activity,playing:true)
    try require(editor.revision=="label-song:2" && editor.canvas.nodes.first{$0.id==id}?.detail=="Bass · Ordinary · playing #1" && editor.canvas.stages.first?.title=="Bass › Ordinary · 1 copy","An external rename refreshes cached copy/stage names at the new document revision")
    song["mixer"]=["buses":[["id":"track","name":"Bass","kind":"track","output":"master"],
      ["id":"other","name":"Bass","kind":"track","output":"master"],
      ["id":"master","name":"Master","kind":"master","output":""]]]
    revision=3;editor.load();editor.showActivity(activity,playing:true)
    try require(editor.canvas.nodes.first{$0.id==id}?.detail=="track · Bass · Ordinary · playing #1","Adding a duplicate name refreshes disambiguation without changing the copy identity")
    song["mixer"]=["buses":[["id":"track","name":"Bass","kind":"track","output":"master"],
      ["id":"master","name":"Master","kind":"master","output":""]]]
    revision=4;editor.load();editor.showActivity(activity,playing:true)
    try require(editor.canvas.nodes.first{$0.id==id}?.detail=="Bass · Ordinary · playing #1" && !editor.canvas.nodes.contains{$0.id=="other"},"Removing a duplicate bus retires its name and restores an unambiguous live label")
  }
  static func graphDrawCullingChecks() throws {
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:900,height:700))
    canvas.collectDrawStatistics=true
    func pixels(_ dirty:NSRect,in crop:NSRect)->Data {
      let bitmap=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:Int(crop.width),pixelsHigh:Int(crop.height),bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
      NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
      NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:bitmap)
      let context=NSGraphicsContext.current!.cgContext
      context.translateBy(x:-crop.minX,y:-crop.minY);context.clip(to:crop)
      canvas.draw(dirty)
      return Data(bytes:bitmap.bitmapData!,count:bitmap.bytesPerRow*bitmap.pixelsHigh)
    }
    let a=SignalCanvasNode(id:"a",title:"A",detail:"",kind:"plugin",x:100,y:100)
    let b=SignalCanvasNode(id:"b",title:"B",detail:"",kind:"plugin",x:300,y:400)
    let far=(0..<40).map{SignalCanvasNode(id:"far\($0)",title:"Outside viewport \($0)",detail:"",kind:"plugin",x:1000,y:Double(40+$0*100))}
    canvas.update([a,b]+far,edges:[SignalCanvasEdge(source:"a",target:"b",label:"Long exposed cable label")])
    let crop=NSRect(x:232,y:293,width:26,height:15)
    let reference=pixels(canvas.bounds,in:crop)
    canvas.edges[0].label=""
    try require(reference != pixels(canvas.bounds,in:crop),"Cable-label overhang fixture must contain actual rendered text outside its path bounds")
    canvas.edges[0].label="Long exposed cable label";canvas.resetDrawStatistics()
    try require(reference==pixels(crop,in:crop),"Partial graph redraws preserve cable labels beyond the wire's stroke bounds")
    try require(canvas.drawStatistics["maximumDrawnNodes"] as? Int==0,"A small cable-only dirty region must skip all42 out-of-region node cards")
    canvas.edges[0].waypoints=[NSPoint(x:600,y:300)];canvas.selectedEdge=0;canvas.selectReroute(0)
    for crop in [NSRect(x:590,y:290,width:22,height:22),NSRect(x:278,y:136,width:30,height:32)] {
      try require(pixels(canvas.bounds,in:crop)==pixels(crop,in:crop),"Culling retains selected reroute points and endpoint handles outside node cards")
    }
    var frame=SignalCanvasNode(id:"frame",title:"Frame",detail:"",kind:"frame",x:50,y:50,inputs:[],outputs:[])
    frame.visualSize=NSSize(width:200,height:200);canvas.update([frame],edges:[])
    let border=NSRect(x:250,y:150,width:2,height:14)
    try require(pixels(canvas.bounds,in:border)==pixels(border,in:border),"Partial redraws include visual frame strokes outside the fill rectangle")
    print("PASS graph draw culling: cable-label overhang, frame stroke, reroutes/handles and skipped offscreen cards")
  }
}

extension InterfaceTests {
  static func graphControlRefreshChecks() throws {
    let bus=GraphBusControls(frame:.zero)
    var calls=[[String:Any]]()
    bus.onRequest={_,params,reply in calls.append(params);reply(["result":["revision":"bus:2","data":[:]]])}
    bus.context(["id":"bus-a","gainDB":-6.0],revision:"bus:1")
    bus.gain.value.stringValue="-6";bus.gain.entered()
    bus.context(["id":"bus-a","gainDB":-6.0],revision:"bus:3")
    try require(bus.gain.value.stringValue=="-6","Unrelated graph revisions preserve accepted gain text instead of resetting an unchanged native control")
    bus.change("mute",value:true,final:true)
    try require(calls.last?["expectedRevision"] as? String=="bus:3","An unchanged mixer display still consumes the latest revision for subsequent edits")
    bus.context(["id":"bus-a","gainDB":3.5,"mute":true,"solo":true],revision:"bus:4")
    try require(bus.gain.slider.doubleValue==3.5 && bus.gain.value.stringValue=="3.50" && bus.mute.state == .on && bus.solo.state == .on,"External level/mute/solo changes refresh the existing bus controls immediately")
    bus.context(nil,revision:"bus:5")
    try require(!bus.gain.slider.isEnabled && !bus.gain.value.isEnabled && !bus.mute.isEnabled && !bus.solo.isEnabled,"Retiring the selected bus disables every retained control")

    let envelope=GraphEnvelopeEditor(frame:.zero),node:[String:Any]=["id":"source","kind":"automation","name":"Motion"]
    var reads=0,revision="curve:1",pointValue=0.2,writes=[[String:Any]]()
    envelope.onRequest={method,params,reply in
      if method=="graph.automation.get"{reads+=1;reply(["result":["revision":revision,"data":["rows":64,"points":[["position":0,"value":pointValue,"curve":"linear"]]]]])}
      else if method=="graph.automation.set"{writes.append(params);reply(["result":["revision":"curve:5","data":[:]]])}
    }
    let patterns:[[String:Any]]=[["index":0,"name":"Verse"],["index":1,"name":"Chorus"]]
    envelope.context(graph:nil,node:node,patterns:patterns,revision:revision)
    let menuItems=envelope.pattern.itemArray
    revision="curve:2";pointValue=0.7
    envelope.context(graph:nil,node:node,patterns:patterns,revision:revision)
    try require(zip(menuItems,envelope.pattern.itemArray).allSatisfy{$0 === $1},"An unchanged pattern catalogue retains its native menu items across graph revisions")
    try require(reads==2 && envelope.revision=="curve:2" && envelope.canvas.points.first?.value==0.7,"Menu reuse does not suppress newly read external envelope changes or revision updates")
    envelope.pattern.selectItem(at:1);envelope.changePattern()
    revision="curve:3"
    envelope.context(graph:nil,node:node,patterns:[["index":0,"name":"Verse"],["index":1,"name":"Renamed"]],revision:revision)
    try require(envelope.pattern.itemTitle(at:1).contains("Renamed") && envelope.patternIndex==1 && envelope.pattern.indexOfSelectedItem==1,"Pattern renaming rebuilds the actual menu and preserves the selected pattern")
    revision="curve:4"
    envelope.context(graph:nil,node:node,patterns:[["index":3,"name":"Replacement"]],revision:revision)
    try require(envelope.patternIndex==3 && envelope.pattern.selectedItem?.representedObject as? Int==3,"Removing the selected pattern retargets both the menu and envelope read to the available replacement")
    envelope.canvas.selected=0;envelope.canvas.replaceSelected(position:0,value:0.4,curve:"linear");envelope.apply()
    try require(writes.last?["expectedRevision"] as? String=="curve:4" && writes.last?["pattern"] as? Int==3,"The next curve edit uses the refreshed revision and selected replacement pattern")
    envelope.resetDocument();envelope.onRequest=nil
    print("PASS graph control refresh: stable menus/accepted text, current revisions and real external changes")
  }
  static func graphEnvelopeCommitChecks() throws {
    let editor=GraphEnvelopeEditor(frame:NSRect(x:0,y:0,width:900,height:340))
    var writes=[[String:Any]](),replies=[([String:Any])->Void]()
    func waitForWrites(_ count:Int) {
      let deadline=Date().addingTimeInterval(1)
      while writes.count<count && Date()<deadline {RunLoop.current.run(until:Date().addingTimeInterval(0.01))}
    }
    editor.onRequest={method,params,reply in
      if method=="graph.automation.get" {reply(["result":["revision":"song:1","data":["rows":64,"points":[["position":0,"value":0.2,"curve":"linear"],["position":16383,"value":0.8,"curve":"linear"]]]]])}
      else if method=="graph.automation.set" {writes.append(params);replies.append(reply)}
    }
    editor.context(graph:"n1",node:["id":"n2","kind":"automation"],patterns:[["index":0]],revision:"song:1")
    editor.canvas.selected=0;editor.showPoint();editor.value.stringValue="37.5"
    editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.value))
    RunLoop.current.run(until:Date().addingTimeInterval(0.16))
    try require(writes.isEmpty,"Incomplete numeric text never saves an unchanged or invalid graph envelope")
    editor.controlTextDidEndEditing(Notification(name:NSControl.textDidEndEditingNotification,object:editor.value))
    waitForWrites(1)
    try require(writes.count==1 && (writes[0]["points"] as? [[String:Any]])?.first?["value"] as? Double==0.375,"Leaving a numeric field saves the curve without Set point or Apply")
    editor.canvas.replaceSelected(position:0,value:0.5,curve:"linear")
    replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    waitForWrites(2)
    try require(writes.count==2 && writes[1]["expectedRevision"] as? String=="song:2" && (writes[1]["points"] as? [[String:Any]])?.first?["value"] as? Double==0.5,"A newer edit waits for the in-flight save and uses its accepted revision")
    replies.removeFirst()(["error":["code":-32002,"message":"busy"]])
    waitForWrites(3)
    try require(writes.count==3 && NSDictionary(dictionary:writes[1]).isEqual(to:writes[2]),"A definite busy rejection retries the exact captured target, revision and curve")
    replies.removeFirst()(["error":["code":-32001,"message":"changed elsewhere"]])
    RunLoop.current.run(until:Date().addingTimeInterval(0.16))
    try require(writes.count==3 && editor.hasDraft && editor.canvas.points[0].value==0.5,"A revision conflict retains editable values and never silently rebases or spins")
    editor.load();writes=[];replies=[]
    let window=NSWindow(contentRect:editor.frame,styleMask:[.titled],backing:.buffered,defer:false);window.contentView=editor;editor.layoutSubtreeIfNeeded()
    func mouse(_ type:NSEvent.EventType,_ point:NSPoint)->NSEvent {NSEvent.mouseEvent(with:type,location:editor.canvas.convert(point,to:nil),modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,eventNumber:1,clickCount:1,pressure:1)!}
    let start=editor.canvas.location(editor.canvas.points[0]),end=NSPoint(x:start.x+30,y:start.y-10)
    editor.canvas.mouseDown(with:mouse(.leftMouseDown,start));editor.canvas.mouseDragged(with:mouse(.leftMouseDragged,end))
    RunLoop.current.run(until:Date().addingTimeInterval(0.16))
    try require(writes.isEmpty,"A graph-curve pointer gesture does not create intermediate Undo records")
    editor.canvas.mouseUp(with:mouse(.leftMouseUp,end))
    try require(writes.count==1,"Releasing a graph-curve gesture commits once without Apply")
    editor.resetDocument();replies.removeFirst()(["result":["revision":"retired:2","data":[:]]])
    try require(editor.graph==nil && editor.revision.isEmpty && !editor.hasDraft,"A late curve reply cannot restore a retired document")
    editor.onRequest=nil
  }
  static func cableRoutingChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let graphMenu=editor.actionMenu()
    try require(graphMenu.items.contains{$0.title=="Clone subgraph" && $0.isEnabled} && graphMenu.items.first(where:{$0.title=="Add modulation source"})?.submenu?.items.contains{$0.title=="LFO" && $0.isEnabled && $0.toolTip != nil} == true,"Graph commands offer target selection before a recipe is selected")
    let song:[String:Any]=["mixer":["buses":[["id":"n1","name":"Track 8","kind":"track","output":"n2"],["id":"n2","name":"Master","kind":"master"]]],"plugins":[["id":"dist","name":"Distortion","isInstrument":false],["id":"comp","name":"Compressor","isInstrument":false]]]
    editor.update(song)
    let master=editor.canvas.nodes.first{$0.id=="n2"}!
    try require(master.outputs.isEmpty && editor.canvas.nodes.filter{$0.id != "n2"}.allSatisfy{$0.x<master.x},"Master defaults to the rightmost final sink, after its inserts")
    try require(editor.canvas.edges.contains{$0.source=="n1" && $0.target=="master-input:n2"},"Tracks enter the master summing node before its insert chain")
    var method="",params=[String:Any](),calls=0
    editor.onRequest={m,p,r in method=m;params=p;calls+=1;r(["error":["message":"captured"]])}
    editor.connectPorts("n1","plugin:dist",out:0,input:0,modulation:false)
    try require(method=="mixer.inserts.move" && params["plugins"] as? [String]==["dist","comp"] && params["target"] as? String=="n1" && params["expectedRevision"] != nil,"Track output to an effect input atomically moves the entire suffix onto that track")
    let insert=editor.songConnections.firstIndex{$0["plugin"] as? String=="dist"}!
    editor.rewire(insert,source:"n1",target:"plugin:dist",out:0,input:0,modulation:false)
    try require(method=="mixer.inserts.move" && params["plugins"] as? [String]==["dist","comp"],"Dragging the existing input endpoint has identical chain-move semantics")
    editor.detachNodes(["plugin:dist"],positions:[("plugin:dist",810,390)],remove:false)
    try require(method=="mixer.inserts.detach" && params["plugins"] as? [String]==["dist"] && (params["positions"] as? [[String:Any]])?.first?["x"] as? Double==810 && params["expectedRevision"] != nil,"Option-dragging one song insert sends one captured-revision detach and layout transaction")
    let detachCalls=calls;editor.detachNodes(["plugin:dist","plugin:comp"],positions:[],remove:false)
    try require(calls==detachCalls && editor.status.stringValue.contains("one rack effect"),"Unsupported loose chains remain connected with an explicit reason")
    let before=calls
    editor.nodeSearch.stringValue="compressor";editor.changeNodeFilter()
    try require(editor.canvas.nodes.map(\.id)==["plugin:comp"] && editor.canvas.edges.isEmpty && calls==before,"Text filtering hides unmatched nodes and cables without mutating the song")
    editor.clearNodeFilters();editor.nodeCategory.selectItem(at:2);editor.changeNodeFilter()
    try require(editor.canvas.nodes.count==2 && editor.canvas.edges.count==1 && editor.songConnections.first?["plugin"] as? String=="comp","Type filters preserve the underlying song-connection identity")
    editor.clearNodeFilters()
    let definition:[String:Any]=["id":"n100","nodes":[["id":"in","kind":"input","name":"Input"],["id":"fx","kind":"plugin","name":"Keep FX"],["id":"out","kind":"output","name":"Output"],["id":"lfo","kind":"lfo","name":"Keep LFO"]],"audio":[["source":"in","target":"fx","gain":0.25],["source":"fx","target":"out","gain":0.5]],"modulation":[["source":"lfo","target":"fx","parameter":7,"minimum":0.1,"maximum":0.9,"base":0.3,"enabled":false]]]
    editor.graphID="n100";editor.update(["library":[definition]])
    editor.nodeSearch.stringValue="keep";editor.changeNodeFilter();editor.selectConnection(0)
    try require(editor.definitionEdgeIndices==[2] && editor.minimum.doubleValue==0.1 && editor.connectionEnabled.state == .off,"Filtered modulation wire reads its real source index")
    editor.minimum.doubleValue=0.2;editor.updateConnection()
    var changed=params["definition"] as! [String:Any]
    try require((changed["audio"] as! [[String:Any]]).count==2 && (changed["modulation"] as! [[String:Any]])[0]["minimum"] as? Double==0.2,"Editing a filtered wire preserves hidden audio routes")
    editor.selectConnection(0);editor.disconnect();changed=params["definition"] as! [String:Any]
    try require((changed["audio"] as! [[String:Any]]).count==2 && (changed["modulation"] as! [[String:Any]]).isEmpty,"Deleting a filtered wire cannot delete an unrelated hidden edge")
    editor.hasDraft=false;editor.clearNodeFilters()
    editor.rewire(0,source:"in",target:"out",out:0,input:0,modulation:false);changed=params["definition"] as! [String:Any]
    try require((changed["audio"] as! [[String:Any]])[0]["gain"] as? Double==0.25 && (changed["audio"] as! [[String:Any]])[1]["gain"] as? Double==0.5,"Cable rerouting preserves gain and all other edges")

    var portSong=song
    portSong["plugins"]=[["id":"comp","name":"Compressor","isInstrument":false,"format":"Built-in","classID":"resonance.compressor.v1","slot":0,"audioBuses":[["direction":"input","index":0,"name":"Stereo input","active":true,"supported":true],["direction":"input","index":1,"name":"Detector sidechain","active":false,"supported":true],["direction":"output","index":0,"name":"Stereo output","active":true,"supported":true]]]]
    editor.graphID=nil;editor.update(portSong)
    let compressor=editor.canvas.nodes.first{$0.id=="plugin:comp"}!
    try require(compressor.inputs.map(\.label)==["Stereo input","Detector sidechain"] && !compressor.inputs[1].active,"Inactive supported sidechains are discoverable, named sockets")
    editor.picker(editor.destination,editor.canvas.nodes.map{($0.title,$0.id)},select:"plugin:comp");editor.refreshPortChoices()
    let detectorPoint=compressor.portPoint(compressor.inputs[1],output:false)
    editor.scroll.magnification=0.3
    try require(editor.canvas.socket(at:detectorPoint)?.port.number==1,"At overview zoom the nearest Detector port wins over overlapping Main input hit area")
    try require(editor.canvas.socket(at:compressor.portPoint(compressor.inputs[0],output:false))?.port.number==0,"Main input still selects its own exact socket at overview zoom")
    try require(compressor.inputs[1].signalType == .sidechain,"Vendor-labelled detector has a distinct sidechain signal type")
    let unknown=SignalGraphEditor.audioPort(2,output:false,catalog:[])
    try require(unknown.signalType == .audio && unknown.label=="Aux input 2","An unnamed auxiliary input is never guessed to be a detector")
    try require(editor.inputChoice.itemTitles.contains{$0.contains("Detector sidechain") && $0.contains("auto-enable")},"Connection form uses named ports with explicit automatic activation")
    editor.connectPorts("n1","plugin:comp",out:0,input:1,modulation:false)
    try require(method=="mixer.sidechains.set" && params["input"] as? Int==1,"Detector socket routes the source separately from the compressor main input")
    try require(editor.chosen(editor.source)=="n1" && editor.chosen(editor.destination)=="plugin:comp" && editor.inputChoice.selectedItem?.representedObject as? UInt32==1,"Cable gestures keep the named connection form on their actual endpoints and port")
    let beforeInvalidSource=calls
    editor.connectPorts("plugin:comp","plugin:comp",out:1,input:1,modulation:false)
    try require(calls==beforeInvalidSource && editor.status.stringValue.contains("bus first"),"Unsupported plugin-output to detector gestures explain the bus step instead of routing the wrong signal")
    editor.selectedID="plugin:comp";editor.useConnectedDetector();try require(method=="plugin.parameters.set" && (params["values"] as? [[String:Any]])?.first?["value"] as? Int==2,"Existing compressors expose a graph action to follow their connected detector")
    editor.canvas.addingMainInput=true;editor.connectPorts("n1","plugin:comp",out:0,input:0,modulation:false);editor.canvas.addingMainInput=false
    try require(method=="mixer.sidechains.set" && params["input"] as? Int==0,"Option-drag can sum an additional channel into an insert main input")
    var patchedMixer=portSong["mixer"] as! [String:Any]
    var patchedBuses=patchedMixer["buses"] as! [[String:Any]];patchedBuses.append(["id":"n3","name":"Key","kind":"track","output":"n2"]);patchedMixer["buses"]=patchedBuses
    patchedMixer["sidechains"]=[["plugin":"comp","source":"n3","input":0,"gainDB":0]];portSong["mixer"]=patchedMixer;editor.update(portSong)
    let mixWire=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-input"}!
    editor.rewire(mixWire,source:"n1",target:"plugin:comp",out:0,input:0,modulation:false)
    try require(method=="mixer.sidechains.set" && params["input"] as? Int==0,"Rerouting an extra main-input cable changes that source without moving the insert chain")
    let beforeBadRewire=calls
    editor.rewire(mixWire,source:"plugin:comp",target:"plugin:comp",out:1,input:0,modulation:false)
    try require(calls==beforeBadRewire && editor.status.stringValue.contains("bus first"),"Wire handles also reject unsupported detector sources without substituting a different signal")
    editor.graphID="n100";editor.update(["library":[definition]])
    try require(editor.connectionGain.doubleValue==1,"Switching from song dB gain to library multiplier starts at unity")
    editor.insertNodes(["fx"],edge:0,positions:[("fx",500,200)])
    try require(method=="graph.nodes.insert" && params["nodes"] as? [String]==["fx"] && params["positions"] != nil,"Wire-drop uses an atomic topology and layout operation with revision guard")

    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:900,height:450))
    let a=SignalCanvasNode(id:"a",title:"A",detail:"",kind:"audio",x:30,y:40),b=SignalCanvasNode(id:"b",title:"B",detail:"",kind:"audio",x:400,y:40),c=SignalCanvasNode(id:"c",title:"C",detail:"",kind:"audio",x:650,y:220)
    let nearby=SignalCanvasNode(id:"nearby",title:"Nearby",detail:"",kind:"audio",x:30,y:48)
    canvas.update([a,b,nearby],edges:[SignalCanvasEdge(source:"a",target:"b",label:"Main"),SignalCanvasEdge(source:"nearby",target:"b",label:"Other")])
    try require(canvas.edge(at:NSPoint(x:305,y:101))==0,"A direct wire hit selects the closest curve instead of the later converging route")
    try require(canvas.edge(at:NSPoint(x:305,y:105))==1,"The nearby curve remains independently selectable")
    let short=SignalCanvasNode(id:"short",title:"Short",detail:"",kind:"audio",x:250,y:40)
    canvas.update([a,short,c],edges:[SignalCanvasEdge(source:"a",target:"short",label:"Output"),SignalCanvasEdge(source:"c",target:"short",label:"Output")])
    try require(canvas.edge(at:NSPoint(x:230,y:101))==0,"A short straight main cable wins over fan-in curves passing near its destination")
    canvas.update([a,b,c],edges:[SignalCanvasEdge(source:"a",target:"b",label:"")])
    let window=NSWindow(contentRect:canvas.bounds,styleMask:[.titled],backing:.buffered,defer:false);window.contentView=canvas
    var bypasses=0;canvas.onBypass={bypasses+=1}
    canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"m",charactersIgnoringModifiers:"m",isARepeat:false,keyCode:46)!)
    try require(bypasses==1,"M on the graph canvas invokes bypass without changing global playback keys")

    func event(_ type:NSEvent.EventType,_ point:NSPoint,_ flags:NSEvent.ModifierFlags=[])->NSEvent {NSEvent.mouseEvent(with:type,location:canvas.convert(point,to:nil),modifierFlags:flags,timestamp:0,windowNumber:window.windowNumber,context:nil,eventNumber:1,clickCount:1,pressure:1)!}
    var rewired:(Int,String,String)?,connected:(String,String)?
    canvas.onRewire={i,a,b,_,_,_ in rewired=(i,a,b)};canvas.onConnectPorts={a,b,_,_,_ in connected=(a,b)}
    canvas.selectedEdge=0
    canvas.mouseDown(with:event(.leftMouseDown,canvas.wireHandle(0,source:false)!))
    try require(canvas.isWiring,"Selected wire has a draggable endpoint handle")
    canvas.mouseDragged(with:event(.leftMouseDragged,c.portPoint(SignalCanvasPort(),output:false)));canvas.mouseUp(with:event(.leftMouseUp,c.portPoint(SignalCanvasPort(),output:false)))
    try require(rewired?.0==0 && rewired?.1=="a" && rewired?.2=="c","Dragging destination handle replaces exactly the selected cable")
    rewired=nil;canvas.selectedEdge=nil
    canvas.mouseDown(with:event(.leftMouseDown,b.portPoint(SignalCanvasPort(),output:false)));canvas.mouseUp(with:event(.leftMouseUp,c.portPoint(SignalCanvasPort(),output:true)))
    try require(connected?.0=="c" && connected?.1=="b" && rewired==nil,"Dragging a connected input adds another source; handles reroute")
    canvas.selectedEdge=nil;rewired=nil
    canvas.mouseDown(with:event(.leftMouseDown,b.portPoint(SignalCanvasPort(),output:false),.option));canvas.mouseUp(with:event(.leftMouseUp,c.portPoint(SignalCanvasPort(),output:true)))
    try require(connected?.0=="c" && connected?.1=="b" && rewired==nil,"Option-drag preserves existing cables and adds a source")
    canvas.edges[0].amount=0;canvas.edges[0].amountRange = -1...1;canvas.edges[0].amountUnit="depth";canvas.selectedEdge=0
    let covered=SignalCanvasNode(id:"cover",title:"Cover",detail:"",kind:"audio",x:260,y:60)
    let withAmount=canvas.edges
    canvas.update([a,b,c,covered],edges:withAmount)
    try require(!canvas.nodes.contains{$0.rect.intersects(canvas.amountBadge(0)! )},"Cable amount moves to clear space instead of hiding behind a card")
    canvas.update([a,b,c],edges:withAmount)
    let badge=canvas.amountBadge(0)!,badgeStart=NSPoint(x:badge.midX,y:badge.midY)
    var amounts=[Double]();canvas.onAmount={_,value in amounts.append(value)}
    canvas.mouseDown(with:event(.leftMouseDown,badgeStart));canvas.mouseDragged(with:event(.leftMouseDragged,NSPoint(x:badgeStart.x+50,y:badgeStart.y)))
    try require(amounts.isEmpty && canvas.isEditing && canvas.edges[0].amount==0,"Cable amount drag previews without mutating the saved route or repeated transactions")
    canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:badgeStart.x+50,y:badgeStart.y)))
    try require(amounts==[0.25],"Cable depth gesture commits once at the final value")
    canvas.mouseDown(with:event(.leftMouseDown,badgeStart));canvas.mouseDragged(with:event(.leftMouseDragged,NSPoint(x:badgeStart.x+80,y:badgeStart.y)))
    canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"\u{1b}",charactersIgnoringModifiers:"\u{1b}",isARepeat:false,keyCode:53)!)
    canvas.mouseUp(with:event(.leftMouseUp,badgeStart));try require(amounts==[0.25],"Escape restores cable amount without sending an edit")
    let zoomScroll=NSScrollView(frame:window.contentView!.bounds);zoomScroll.documentView=canvas;zoomScroll.allowsMagnification=true;zoomScroll.minMagnification=0.3;window.contentView=zoomScroll;zoomScroll.magnification=0.3
    let hs=canvas.wireHandle(0,source:true)!,ht=canvas.wireHandle(0,source:false)!
    try require(hypot(hs.x-ht.x,hs.y-ht.y)*0.3>16 && hypot(ht.x-b.portPoint(SignalCanvasPort(),output:false).x,ht.y-b.portPoint(SignalCanvasPort(),output:false).y)*0.3>16,"Zoomed-out cable handles remain distinct from one another and the sockets")
    canvas.selectedEdge=0;rewired=nil
    canvas.mouseDown(with:event(.leftMouseDown,b.portPoint(SignalCanvasPort(),output:false)));canvas.mouseUp(with:event(.leftMouseUp,c.portPoint(SignalCanvasPort(),output:true)))
    try require(connected?.0=="c" && connected?.1=="b" && rewired==nil,"A selected wire cannot steal an additive input-socket drag at minimum zoom")
    zoomScroll.magnification=1
    connected=nil;rewired=nil;canvas.selectedEdge=nil
    canvas.mouseDown(with:event(.leftMouseDown,a.portPoint(SignalCanvasPort(),output:true)));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:300,y:300)))
    try require(connected==nil && rewired==nil && !canvas.isWiring,"Dropping on empty space cancels without a destructive disconnect")
    var addOffer:(String,UInt32,Bool)?
    canvas.onAddConnected={node,port,output,_ in addOffer=(node,port.number,output)}
    canvas.mouseDown(with:event(.leftMouseDown,a.portPoint(SignalCanvasPort(),output:true)));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:300,y:300)))
    try require(addOffer?.0=="a" && addOffer?.1==0 && addOffer?.2==true && !canvas.isEditing,"An empty socket drag offers compatible Add only after the gesture is complete")
    addOffer=nil;canvas.selectedEdge=0
    canvas.mouseDown(with:event(.leftMouseDown,canvas.wireHandle(0,source:false)!));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:300,y:300)))
    try require(addOffer==nil && rewired==nil,"Dropping an existing endpoint on empty space cancels instead of invoking Add or deleting its route")
    canvas.selectedEdge=nil
    canvas.mouseDown(with:event(.leftMouseDown,a.portPoint(SignalCanvasPort(),output:true)))
    var deleted=false;canvas.onDelete={deleted=true}
    canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"\u{7f}",charactersIgnoringModifiers:"\u{7f}",isARepeat:false,keyCode:51)!)
    try require(!deleted && canvas.isWiring,"Delete cannot mutate topology halfway through a cable drag")
    canvas.keyDown(with:NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:window.windowNumber,context:nil,characters:"\u{1b}",charactersIgnoringModifiers:"\u{1b}",isARepeat:false,keyCode:53)!)
    canvas.mouseUp(with:event(.leftMouseUp,c.portPoint(SignalCanvasPort(),output:false)))
    try require(connected==nil && rewired==nil,"Escape cancels a cable before the release event")
    var moved=[(String,Double,Double)]();canvas.onMoveNodes={moved=$0}
    canvas.mouseDown(with:event(.leftMouseDown,NSPoint(x:a.x+80,y:a.y+20)));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:a.x+80,y:a.y+20)))
    canvas.mouseDown(with:event(.leftMouseDown,NSPoint(x:c.x+80,y:c.y+20),.shift));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:c.x+80,y:c.y+20),.shift))
    try require(canvas.selection==["a","c"],"Shift-click creates a multiple-node selection")
    canvas.mouseDown(with:event(.leftMouseDown,NSPoint(x:a.x+80,y:a.y+20)));canvas.mouseDragged(with:event(.leftMouseDragged,NSPoint(x:a.x+100,y:a.y+50)));canvas.mouseUp(with:event(.leftMouseUp,NSPoint(x:a.x+100,y:a.y+50)))
    try require(moved.count==2 && moved.contains{$0.0=="a" && $0.1==a.x+20 && $0.2==a.y+30} && moved.contains{$0.0=="c" && $0.1==c.x+20 && $0.2==c.y+30},"Group movement keeps offsets and dispatches one edit")
    canvas.selectNodes(["c"]);canvas.insertionHint={ids,e in ids==["c"] && e==0 ? "Insert":nil}
    var inserted=false;canvas.onInsertNodes={ids,e,_ in inserted=ids==["c"] && e==0}
    let start=canvas.nodes.first{$0.id=="c"}!;let drop=NSPoint(x:320,y:111)
    canvas.mouseDown(with:event(.leftMouseDown,NSPoint(x:start.x+80,y:start.y+20)))
    // Use the actual new curve midpoint (A moved, B stayed fixed).
    let wireMid=NSPoint(x:320,y:(canvas.nodes[0].portPoint(SignalCanvasPort(),output:true).y+b.portPoint(SignalCanvasPort(),output:false).y)/2)
    canvas.mouseDragged(with:event(.leftMouseDragged,wireMid));canvas.mouseUp(with:event(.leftMouseUp,wireMid))
    try require(inserted,"Dragging a selected effect onto a highlighted cable dispatches insertion instead of a separate move")
    _=drop

  }
}

extension InterfaceTests {
  static func processingGroupChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:650))
    let definition:[String:Any]=["id":"n100","number":1,"name":"Motion","nodes":[
      ["id":"n1","kind":"input","name":"Input","x":30.0,"y":60.0],
      ["id":"n2","kind":"plugin","name":"Filter","x":300.0,"y":60.0],
      ["id":"n3","kind":"plugin","name":"Drive","x":550.0,"y":60.0],
      ["id":"n4","kind":"output","name":"Output","x":800.0,"y":60.0],
      ["id":"n5","kind":"lfo","name":"LFO","x":300.0,"y":250.0]],
      "audio":[["source":"n1","target":"n2","gain":0.7],["source":"n2","target":"n3"],["source":"n3","target":"n4"]],
      "modulation":[["source":"n5","target":"n2","parameter":7,"minimum":0.0,"maximum":0.2,"base":0.4]],
      "groups":[["id":"n200","parent":"","name":"Tone","x":300.0,"y":60.0,"nodes":["n2","n3"]]]]
    editor.graphID="n100";editor.update(["library":[definition]])
    try require(Set(editor.canvas.nodes.map(\.id))==["n1","n4","n5","n200"] && editor.definitionEdgeIndices==[0,2,3],"A group hides only internal nodes/cables and keeps authoritative boundary edge indices")
    guard let group=editor.canvas.nodes.first(where:{$0.id=="n200"}),let audio=group.inputs.first(where:{!$0.modulation}),let parameter=group.inputs.first(where:{$0.modulation})else{throw InterfaceFailure(message:"Missing processing group ports")}
    try require(editor.realPort("n200",audio.number,output:false,modulation:false).node=="n2" && editor.realPort("n200",parameter.number,output:false,modulation:true).number==7,"Audio and named parameter boundary sockets resolve to original processor identities")
    var method="",params=[String:Any]()
    editor.onRequest={m,p,reply in method=m;params=p;reply(["error":["message":"Test captured transaction"]])}
    editor.detachNodes(["n200"],positions:[],remove:true)
    try require(method=="graph.nodes.detach" && params["nodes"] as? [String]==["n2","n3"] && params["remove"] as? Bool==true,"Delete and heal expands all processors behind a reusable group boundary")
    editor.changeCableAmount(0,value:0.45)
    let gainDefinition=params["definition"] as? [String:Any]
    let gainEdge=(gainDefinition?["audio"] as? [[String:Any]])?.first
    try require(method=="graph.update" && gainEdge?["target"] as? String=="n2" && gainEdge?["gain"] as? Double==0.45,"Editing a visible group cable changes the real cable, never a synthetic endpoint")
    editor.update(["library":[definition]])
    editor.changeCableAmount(2,value:0.6)
    let modulation=((params["definition"] as? [String:Any])?["modulation"] as? [[String:Any]])?.first
    try require(modulation?["target"] as? String=="n2" && (modulation?["parameter"] as? NSNumber)?.intValue==7 && modulation?["maximum"] as? Double==0.6,"A boundary modulation depth retains stable parameter identity")
    editor.update(["library":[definition]])
    editor.moveNodes([("n200",400,100)])
    let moved=params["definition"] as? [String:Any],movedNodes=moved?["nodes"] as? [[String:Any]] ?? []
    try require(movedNodes.first{$0["id"] as? String=="n2"}?["x"] as? Double==400 && movedNodes.first{$0["id"] as? String=="n3"}?["x"] as? Double==650,"Moving a group preserves the relative positions of its contents in one transaction")
    editor.update(["library":[definition]]);editor.arrange()
    let arranged=params["definition"] as? [String:Any],arrangedGroup=(arranged?["groups"] as? [[String:Any]])?.first
    let arrangedNodes=arranged?["nodes"] as? [[String:Any]] ?? []
    try require(arrangedGroup?["x"] as? Double != 300 && arrangedNodes.first{$0["id"] as? String=="n3"}?["x"] as? Double == (arrangedGroup?["x"] as? Double ?? 0)+250,"Arrange moves group cards and their hidden contents together")
    editor.update(["library":[definition]]);editor.selectedID="n200";editor.exportProcessingGroup()
    try require(method=="graph.group.export" && params["group"] as? String=="n200" && params["name"] as? String=="Tone","Library export targets the selected boundary explicitly and retains its name")
    editor.update(["library":[definition]]);editor.openNode("n200")
    try require(editor.processingGroupID=="n200" && editor.canvas.nodes.contains{$0.id=="n2"} && editor.canvas.nodes.contains{$0.id=="n1" && $0.kind=="boundary"},"Entering a group reveals its processors and labeled external dependencies")
    editor.canvas.selectNodes(["n2","n3"]);editor.groupSelection()
    try require(method=="graph.group.create" && params["parent"] as? String=="n200" && params["nodes"] as? [String]==["n2","n3"],"Grouping at depth captures the current parent explicitly")
    editor.mutate("graph.node.add",["graph":"n100","kind":"lfo"])
    try require(params["parent"] as? String=="n200","Add stays inside the currently opened processing group")
    editor.navigateParent();try require(editor.processingGroupID==nil && editor.graphID=="n100","Option-Up leaves one processing depth before leaving its reusable definition")
    editor.selectedID="n200";editor.canvas.selected="n200";editor.ungroupSelection()
    try require(method=="graph.group.remove" && params["group"] as? String=="n200" && params["deleteContents"]==nil,"Ungroup is an explicit non-destructive boundary operation")
    var removed=definition;removed["nodes"]=(definition["nodes"] as! [[String:Any]]).filter{!["n2","n3"].contains($0["id"] as? String ?? "")};editor.pruneProcessingGroups(&removed)
    try require((removed["groups"] as? [[String:Any]])?.isEmpty==true,"Bulk node deletion cannot leave dangling group membership")
    let songGroup:[String:Any]=["id":"n300","name":"Rack pair","parent":"","x":300.0,"y":100.0,"nodes":["plugin:rack-a","plugin:rack-b"]]
    let song:[String:Any]=["groups":[songGroup],"library":[],"plugins":[["id":"rack-a","name":"First","isInstrument":false],["id":"rack-b","name":"Second","isInstrument":false]],"mixer":["buses":[["id":"n1","kind":"track","name":"Track 1","output":"n2","inserts":["rack-a","rack-b"]],["id":"n2","kind":"master","name":"Master","inserts":[]]]]]
    editor.graphID=nil;editor.processingGroupID=nil;editor.update(song)
    try require(editor.canvas.nodes.contains{$0.id=="n300"} && !editor.canvas.nodes.contains{$0.id=="plugin:rack-a"} && editor.canvas.edges.count==2,"Song groups collapse rack processors while retaining main boundary cables")
    try require(editor.songConnections.count==editor.canvas.edges.count && editor.songConnections.first?["plugin"] as? String=="rack-a","Song projection retains the exact underlying connection action")
    editor.selectedID="n300";editor.canvas.selected="n300";editor.moveNodes([("n300",400,200)])
    try require(method=="graph.layout.set" && (params["groups"] as? [[String:Any]])?.first?["group"] as? String=="n300","Dragging a song boundary commits all descendants as one layout transaction")
    var sidechainSong=song,mixer=song["mixer"] as! [String:Any]
    var buses=mixer["buses"] as! [[String:Any]]
    buses.insert(["id":"n3","kind":"track","name":"Kick","output":"n2","inserts":[]],at:1)
    mixer["buses"]=buses;mixer["sidechains"]=[["source":"n3","plugin":"rack-b","input":1,"gainDB":-4.0]];sidechainSong["mixer"]=mixer
    editor.update(sidechainSong)
    let sc=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-input"}!
    let projected=editor.canvas.edges[sc]
    try require(projected.target=="n300" && editor.realPort(projected.target,projected.input,output:false,modulation:false).node=="plugin:rack-b","Collapsed sidechain keeps the real downstream processor")
    editor.changeCableAmount(sc,value:-8)
    try require(method=="mixer.sidechains.set" && params["plugin"] as? String=="rack-b" && params["input"] as? Int==1 && (params["sources"] as? [[String:Any]])?.first?["gainDB"] as? Double == -8,"A collapsed sidechain gain edit resolves both boundary identity and native port")
    editor.update(sidechainSong);editor.newConnection()
    editor.picker(editor.source,editor.canvas.nodes.map{($0.title,$0.id)},select:"n3")
    editor.picker(editor.destination,editor.canvas.nodes.map{($0.title,$0.id)},select:"n300")
    editor.outputPort.stringValue="0";editor.inputPort.stringValue=String(projected.input)
    editor.inferConnectionKind()
    try require(editor.connectionKind.titleOfSelectedItem=="Plugin sidechain","Keyboard patching recognises a group boundary's actual sidechain port")
    editor.hasDraft=false;editor.update(sidechainSong)
    func reading(_ node:String,_ output:Bool,_ number:Int,_ peak:Double,_ clipped:Bool=false)->[String:Any] {
      ["key":node+(output ? "/out/":"/in/")+String(number),"node":node,"port":number,"direction":output ? "output":"input","name":node+" · "+String(number),"measured":true,"peak":[peak,peak],"rms":[peak,peak],"clipped":clipped,"channels":2]
    }
    editor.showSignals(["active":true,"ports":[reading("plugin:rack-a",false,0,0.4),reading("plugin:rack-b",false,1,0.2),reading("plugin:rack-b",true,0,0.7)]])
    let boundary=editor.canvas.nodes.first{$0.id=="n300"}!
    let detector=boundary.inputs.first{editor.realPort("n300",$0.number,output:false,modulation:false).number==1}!
    try require(editor.canvas.signalReadings.port("n300",output:false,number:detector.number)?.key=="plugin:rack-b/in/1","Group socket telemetry resolves its real downstream detector rather than alias number zero")
    try require(editor.canvas.signalReadings.primaryPort("n300",output:true)?.key=="plugin:rack-b/out/0","A serial group's Listen and meter use its actual final output")
    let firstInsert=editor.songConnections.firstIndex{$0["kind"] as? String=="insert"}!
    try require(editor.observedCablePort(firstInsert)==nil,"A collapsed group's input cable never substitutes its summed plugin input for a missing route tap")
    var mainContribution=reading("plugin:rack-a",true,0,0.3)
    mainContribution["key"]="opaque-main-contribution";mainContribution["route"]=["kind":"insert","source":"n1","plugin":"rack-a","tap":"main-path","gainDB":0]
    editor.showSignals(["active":true,"ports":[reading("plugin:rack-a",false,0,0.4),reading("plugin:rack-b",false,1,0.2),reading("plugin:rack-b",true,0,0.7),mainContribution]])
    try require(editor.observedCablePort(firstInsert)?.key=="opaque-main-contribution","Collapsed group cable inspection resolves the exact serial contribution rather than a display socket alias")
    editor.canvas.selected="n300"
    try require(editor.canvas.scopeTarget(at:nil)=="plugin:rack-b/out/0","Node scope uses the resolved group output")
    editor.nodeSearch.stringValue="First";editor.changeNodeFilter()
    try require(editor.canvas.nodes.contains{$0.id=="n300"},"Searching a hidden processor keeps its enclosing group discoverable")
    editor.clearNodeFilters()
    var nestedSong=sidechainSong,inner=songGroup
    inner["parent"]="n301";nestedSong["groups"]=[inner,["id":"n301","name":"Outer","parent":"","nodes":[]]]
    editor.update(nestedSong)
    editor.showSignals(["active":true,"ports":[reading("plugin:rack-b",true,0,2,true)]])
    editor.findOverload()
    try require(editor.processingGroupID=="n300" && editor.canvas.selected=="plugin:rack-b" && editor.canvas.nodes.contains{$0.id=="plugin:rack-b"},"Overload navigation enters the real nested owner rather than selecting an invisible child")
    editor.navigate(graph:nil);editor.selectedID="n1";editor.canvas.selected="n1"
    editor.showSignals(["active":true,"ports":[reading("plugin:rack-b",false,0,0.6),reading("plugin:rack-b",true,0,0)]])
    editor.traceSilence()
    try require(editor.processingGroupID=="n300" && editor.selectedID=="plugin:rack-b" && editor.status.stringValue.contains("input active; output silent"),"Silence tracing traverses hidden group contents and opens the measured boundary")
    var readings=GraphSignalReadings()
    readings.aliases=[GraphBoundaryPort(node:"parallel",number:0,output:true,modulation:false):GraphRealPort(node:"a",number:2),GraphBoundaryPort(node:"parallel",number:1,output:true,modulation:false):GraphRealPort(node:"b",number:0)]
    readings.update(["ports":[reading("a",true,2,0.2),reading("b",true,0,0.4)]])
    try require(readings.primaryPort("parallel",output:true)?.key=="b/out/0","The first synthetic socket cannot make an auxiliary output masquerade as Main")
    readings.aliases[GraphBoundaryPort(node:"parallel",number:2,output:true,modulation:false)]=GraphRealPort(node:"c",number:0)
    readings.update(["ports":[reading("a",true,2,0.2),reading("b",true,0,0.4),reading("c",true,0,0.3)]])
    try require(readings.primaryPort("parallel",output:true)==nil && readings.nodePorts("parallel",output:true).count==3,"Parallel main outputs require an explicit monitor choice")
    editor.processingGroupID=nil
    editor.update(song);editor.openNode("n300")
    try require(editor.processingGroupID=="n300" && editor.canvas.nodes.contains{$0.id=="plugin:rack-a"} && editor.canvas.nodes.contains{$0.id=="n1" && $0.kind=="boundary"},"Song group navigation exposes real rack processors and external endpoints")
    editor.canvas.selectNodes(["plugin:rack-a"]);editor.groupSelection()
    try require(method=="graph.song.group.create" && params["parent"] as? String=="n300" && params["nodes"] as? [String]==["plugin:rack-a"],"Nested song grouping targets rack identities and the current parent")
    editor.canvas.selectNodes(["plugin:rack-a"]);editor.canvas.selectedEdge=nil;editor.canvas.onDelete?()
    try require(method=="plugin.remove" && params["plugins"] as? [String]==["rack-a"],"Deleting a song processor emits its stable rack identity")
    var addSong=song,addMixer=song["mixer"] as! [String:Any],addBuses=addMixer["buses"] as! [[String:Any]]
    addBuses[0]["inserts"]=["rack-a","rack-b","rack-c"];addMixer["buses"]=addBuses;addSong["mixer"]=addMixer
    addSong["plugins"]=(song["plugins"] as! [[String:Any]])+[["id":"rack-c","name":"Outside group","isInstrument":false]]
    editor.update(addSong);editor.selectedID=nil;editor.canvas.selected=nil;editor.canvas.selectedEdge=nil
    try require(editor.addDestination.target=="n1" && editor.addDestination.before=="rack-c","Add within a song group derives its bus and inserts before the following external processor")
    let insertion=editor.addDestination
    editor.addEntry(GraphAddMenu.Entry(id:"new-effect",title:"New effect",detail:"",keywords:"",payload:["kind":"plugin","descriptor":["id":"new-effect","isInstrument":false]]),graph:nil,target:insertion.target,node:nil,position:NSPoint(x:500,y:100),before:insertion.before)
    try require(method=="plugin.add" && params["target"] as? String=="n1" && params["before"] as? String=="rack-c" && params["parent"] as? String=="n300","Contextual Add carries the current song processing group in the same plugin transaction")
    editor.update(song);editor.navigateParent();editor.canvas.selectedEdge=nil;editor.canvas.selectNodes(["n300"]);editor.canvas.onDelete?()
    try require(method=="plugin.remove" && params["plugins"] as? [String]==["rack-a","rack-b"],"Deleting a collapsed song group removes all stable processor members in one request")
    method="";editor.detachNodes(["n300"],positions:[],remove:true)
    try require(method=="plugin.remove" && params["plugins"] as? [String]==["rack-a","rack-b"],"Delete and heal expands a complete song group into one stable rack removal")
    method="";editor.detachNodes(["n1","n300"],positions:[],remove:true)
    try require(method.isEmpty,"Delete and heal rejects a mixed channel and group selection as a whole")
    editor.update(song);editor.navigate(graph:nil)
    let explicit=editor.songConnections.indices.filter{editor.songConnections[$0]["kind"] as? String=="output"}
    if let cable=explicit.first {
      method="";editor.cutConnections([cable]);let references=params["connections"] as? [[String:Any]]
      try require(method=="graph.connections.remove" && references?.count==1 && references?.first?["source"] as? String==editor.songConnections[cable]["source"] as? String,"Song cuts use semantic endpoints behind collapsed group boundary sockets")
      if let fixed=editor.songConnections.firstIndex(where:{$0["kind"] as? String=="insert"}) {
        method="";editor.cutConnections([cable,fixed]);try require(method.isEmpty && editor.status.stringValue.contains("No cables cut"),"Mixed explicit and implicit cable strokes reject the entire gesture")
      }
    } else {throw InterfaceFailure(message:"Missing explicit song output fixture")}
    method="";editor.canvas.selectNodes(["n1","n300"]);editor.canvas.onDelete?()
    try require(method.isEmpty && editor.status.stringValue.contains("Select rack processors"),"A mixed channel/processor selection cannot silently delete only its effect subset")
    editor.navigateParent();editor.selectedID="n300";editor.canvas.selected="n300";editor.exportProcessingGroup()
    try require(method=="graph.song.group.export" && params["group"] as? String=="n300","Song group export uses the shared rack-to-recipe API")

  }
}


extension InterfaceTests {
  static func songModulationChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:650))
    func source(_ id:String,_ kind:String,_ extra:[String:Any]=[:])->[String:Any] {var s:[String:Any]=["id":id,"kind":kind,"name":kind,"x":80.0,"y":400.0,"rate":2.0,"phase":0.0,"attack":0.01,"release":0.1,"controller":74,"amount":1.0];s.merge(extra){_,new in new};return s}
    let sources=[source("n20","lfo"),source("n21","follower",["audioBus":"n3","output":0,"preFader":false]),source("n22","amount"),source("n23","note-envelope",["noteTarget":"n1"])]
    let song:[String:Any]=["songSources":sources,"songModulation":[["source":"n20","plugin":"rack-a","parameter":777,"minimum":0.0,"maximum":0.3,"enabled":true]],"library":[],"plugins":[["id":"rack-a","name":"Gain","isInstrument":false]],"mixer":["buses":[["id":"n1","kind":"track","name":"Track 1","output":"n2","inserts":["rack-a"]],["id":"n3","kind":"track","name":"Track 2","output":"n2","inserts":[]],["id":"n2","kind":"master","name":"Master","inserts":[]]]]]
    editor.portCatalogs["plugin:rack-a"]=["parameters":[["id":777,"name":"Mix","min":0.0,"max":1.0,"value":0.4,"writable":true,"canSlide":true,"step":0.0],["id":17,"name":"Mode","min":0.0,"max":3.0,"writable":true,"canSlide":false,"step":1.0],["id":9,"name":"Meter","writable":false,"canSlide":false]]]
    editor.exposedParameters["plugin:rack-a"]=9;editor.update(song)
    guard let lfo=editor.canvas.nodes.first(where:{$0.id=="source:n20"}),let plugin=editor.canvas.nodes.first(where:{$0.id=="plugin:rack-a"})else{throw InterfaceFailure(message:"Root modulation source or rack target missing")}
    try require(lfo.kind=="modulation" && lfo.inputs.isEmpty && lfo.outputs.first?.modulation==true && lfo.x==80 && lfo.y==400,"Song modulation sources are independent saved cards with control outputs")
    try require(plugin.inputs.contains{$0.number==777 && $0.modulation && $0.label=="Mix"} && !plugin.inputs.contains{$0.number==777 && !$0.modulation},"Stable parameter ports cannot become bogus audio bus numbers")
    try require(plugin.inputs.first{$0.number==9 && $0.modulation}?.unavailable != nil,"Read-only root parameter ports explain why they cannot connect")
    var method="",params=[String:Any]();editor.onRequest={m,p,reply in method=m;params=p;reply(["error":["message":"captured"]])}
    editor.connectPorts("source:n22","plugin:rack-a",out:0,input:777,modulation:true)
    try require(method=="graph.song.modulation.set" && params["source"] as? String=="n22" && params["plugin"] as? String=="rack-a" && params["maximum"] as? Int==0,"Dragging a root source to a parameter adds zero-depth modulation using stable identities")
    method="";editor.connectPorts("source:n20","plugin:rack-a",out:0,input:777,modulation:true)
    try require(method.isEmpty,"Repeated root modulation connections do not create duplicate edges")
    method="";editor.addEntry(.init(id:"source:lfo",title:"LFO",detail:"",keywords:"",payload:["kind":"lfo"]),graph:nil,target:nil,node:nil,position:NSPoint(x:800,y:100),connecting:GraphAddConnection(node:"plugin:rack-a",port:SignalCanvasPort(number:17,label:"Mode",modulation:true),output:false))
    try require(method.isEmpty && editor.targetMenu.entries.first?.id=="quantized","Stepped targets require an explicit discrete-values choice before any write")
    editor.targetMenu.choose()
    try require(method=="graph.song.source.add" && (params["source"] as? [String:Any])?["kind"] as? String=="lfo" && (params["connect"] as? [String:Any])?["quantized"] as? Bool==true,"Dragging a stepped parameter into space creates and connects an explicitly quantized source atomically")
    editor.addEntry(.init(id:"source:follower",title:"Follower",detail:"",keywords:"",payload:["kind":"follower"]),graph:nil,target:nil,node:nil,position:.zero,connecting:GraphAddConnection(node:"plugin:rack-a",port:SignalCanvasPort(number:1,label:"Aux"),output:true))
    try require(method=="graph.song.source.add" && (params["source"] as? [String:Any])?["audioPlugin"] as? String=="rack-a" && (params["source"] as? [String:Any])?["output"] as? UInt32==1,"Audio-port Add follower preserves the actual native output tap")
    let mod=editor.songConnections.firstIndex{$0["kind"] as? String=="modulation"}!
    editor.changeCableAmount(mod,value:-0.4)
    try require(method=="graph.song.modulation.set" && params["maximum"] as? Double == -0.4 && params["base"]==nil,"Root cable depth changes preserve the host's manual and pattern baseline")
    method="";editor.rewire(mod,source:"source:n22",target:"plugin:rack-a",out:0,input:17,modulation:true)
    try require(method.isEmpty,"Rerouting onto a stepped target also awaits an explicit choice")
    editor.targetMenu.choose()
    try require(params["source"] as? String=="n22" && params["parameter"] as? UInt32==17 && (params["replace"] as? [String:Any])?["source"] as? String=="n20" && params["quantized"] as? Bool==true,"Root modulation handle rerouting replaces exactly one old identity in a single write")
    let follower=editor.songConnections.firstIndex{$0["kind"] as? String=="follower-input"}!,output=editor.songConnections.firstIndex{$0["kind"] as? String=="output"}!
    editor.rewire(follower,source:"plugin:rack-a",target:"source:n21",out:1,input:0,modulation:false)
    try require(method=="graph.song.source.update" && (params["source"] as? [String:Any])?["audioPlugin"] as? String=="rack-a","A follower cable can select a different current audio tap directly")
    editor.cutConnections([mod,follower,output]);let cut=params["connections"] as? [[String:Any]] ?? []
    try require(method=="graph.connections.remove" && Set(cut.compactMap{$0["kind"] as? String})==["modulation","follower-input","output"],"One stroke combines audio, follower and modulation wires in one guarded transaction")
    editor.canvas.selectNodes(["source:n20","source:n22"]);editor.canvas.selectedEdge=nil;editor.canvas.onDelete?()
    try require(method=="graph.song.source.remove" && params["nodes"] as? [String]==["n20","n22"],"Root source deletion uses stable document identities")
    editor.selectedID="source:n23";editor.inspect();try require(!editor.sourceScopeRow.isHidden,"Root note source exposes its note scope where it is edited")
    editor.selectedID="source:n20";editor.inspect();try require(!editor.sourceRateRow.isHidden && editor.sourceCCRow.isHidden,"Source inspector shows controls relevant to the chosen source")
    editor.filterID="n1";editor.selectedID=nil;editor.rebuild()
    try require(editor.canvas.nodes.contains{$0.id=="source:n20"},"Channel focus retains the modulation sources affecting its plugins")
    var preSong=song,preSources=sources;preSources[1]["audioBus"]="n1";preSources[1]["preFader"]=true;preSong["songSources"]=preSources
    editor.filterID=nil;editor.update(preSong)
    let pre=editor.songConnections.firstIndex{$0["kind"] as? String=="follower-input"}!
    try require(editor.canvas.edges[pre].source=="plugin:rack-a" && editor.canvas.edges[pre].label.contains("pre-fader"),"Pre-fader follower taps are after inserts, matching MixerRuntime's actual tap")
    let jump=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:650));jump.update(song)
    var graphReply:(([String:Any])->Void)?,catalogReply:(([String:Any])->Void)?
    jump.onRequest={m,_,reply in if m=="graph.get"{graphReply=reply}else if m=="plugin.parameters.get"{catalogReply=reply}}
    jump.filterID="n3";jump.nodeSearch.stringValue="Nothing";jump.rebuild()
    jump.inspectSongModulation(source:"n20",plugin:"rack-a",parameter:777,editConnection:true)
    try require(jump.loading && jump.canvas.selectedEdge==nil,"Activity navigation waits for a current graph projection")
    graphReply?(["result":["revision":"song:2","data":song]])
    guard let selected=jump.canvas.selectedEdge else{throw InterfaceFailure(message:"Activity link did not select its root modulation wire")}
    try require(jump.filterID==nil && jump.nodeSearch.stringValue.isEmpty && jump.songConnections[selected]["source"] as? String=="n20" && (jump.songConnections[selected]["parameter"] as? NSNumber)?.uint32Value==777,"Activity link clears filters and resolves an exact stable source/parameter wire")
    catalogReply?(["result":["revision":"song:2","data":editor.portCatalogs["plugin:rack-a"]?["parameters"] ?? []]])
    try require(jump.canvas.selectedEdge==selected && jump.songConnections[selected]["source"] as? String=="n20","A pending parameter catalogue refresh cannot replace the activity link's selected wire")
    jump.inspectSongModulation(source:nil,plugin:"rack-a",parameter:777,editConnection:false)
    graphReply?(["result":["revision":"song:2","data":song]])
    try require(jump.canvas.selection==["plugin:rack-a","source:n20"],"Aggregate activity focuses the target together with all its contributors")
    jump.hasDraft=true;let previous=jump.canvas.selection;jump.inspectSongModulation(source:"n22",plugin:"rack-a",parameter:777,editConnection:false)
    try require(jump.canvas.selection==previous && jump.hasDraft && jump.status.stringValue.contains("Finish or cancel"),"Activity navigation preserves an unfinished graph draft")

  }
}

extension InterfaceTests {
  static func graphPresentationChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:650))
    let frame:[String:Any]=["id":"tone","kind":"frame","title":"Tone","x":260.0,"y":30.0,"width":500.0,"height":200.0,"nodes":["n2","n3"]]
    let comment:[String:Any]=["id":"memo","kind":"comment","title":"Remember","text":"Keep the transients","x":400.0,"y":400.0,"width":300.0,"height":150.0]
    var definition:[String:Any]=["id":"n100","number":1,"name":"Motion","nodes":[
      ["id":"n1","kind":"input","name":"Input","x":30.0,"y":80.0],
      ["id":"n2","kind":"plugin","name":"Filter","x":300.0,"y":80.0],
      ["id":"n3","kind":"plugin","name":"Drive","x":550.0,"y":80.0],
      ["id":"n4","kind":"output","name":"Output","x":820.0,"y":80.0]],
      "audio":[["source":"n1","target":"n2"],["source":"n2","target":"n3"],["source":"n3","target":"n4"]],
      "presentation":["regions":[frame,comment],"cables":[["source":"n1","target":"n2","output":0,"input":0,"modulation":false,"points":[[180.0,270.0]]]]]]
    editor.graphID="n100";editor.update(["library":[definition]])
    try require(editor.canvas.nodes.count==6 && editor.canvas.edges.count==3 && editor.canvas.edges[0].waypoints==[NSPoint(x:180,y:270)],"Frames and comments add only presentation; reroute geometry follows stable cable endpoints")
    let visual=editor.canvas.nodes.first{$0.id=="visual:tone"}!
    try require(visual.hitRect.height==30 && !visual.hitRect.contains(NSPoint(x:400,y:140)),"Expanded frame interiors leave member processors directly clickable")
    var method="",params=[String:Any]();editor.onRequest={m,p,reply in method=m;params=p;reply(["error":["message":"captured"]])}
    editor.moveNodes([("visual:tone",300,70)])
    let moved=params["positions"] as? [[String:Any]] ?? []
    try require(method=="graph.presentation.set" && moved.count==2 && moved.first{$0["node"] as? String=="n2"}?["x"] as? Double==340,"Frame movement commits member positions and frame geometry in one transaction")
    editor.selectedID="visual:memo";editor.inspect();editor.visualControls.text.stringValue="Sharper attack";editor.visualControls.commit(editor.visualControls.text)
    try require((params["presentation"] as? [String:Any])?["regions"] is [[String:Any]] && !editor.visualControls.dirty,"Comment edits commit inline without an Apply button or lingering draft")
    var collapsed=frame;collapsed["collapsed"]=true
    var presentation=definition["presentation"] as! [String:Any];presentation["regions"]=[collapsed,comment];definition["presentation"]=presentation
    editor.selectedID=nil;editor.update(["library":[definition]])
    try require(Set(editor.canvas.nodes.map(\.id))==["n1","n4","visual:tone","visual:memo"] && editor.definitionEdgeIndices==[0,2],"Collapsed visual frames hide only their internal wire and retain authoritative edge indices")
    let card=editor.canvas.nodes.first{$0.id=="visual:tone"}!,input=card.inputs.first!,output=card.outputs.first!
    try require(editor.realPort(card.id,input.number,output:false,modulation:false).node=="n2" && editor.realPort(card.id,output.number,output:true,modulation:false).node=="n3","Collapsed annotation sockets alias real processors without routing changes")
    let identity=editor.visualEdgeIdentity(0)!
    try require(identity["source"] as? String=="n1" && identity["target"] as? String=="n2","Reroute storage never persists projected annotation identities")
    editor.canvas.selectedEdge=0;editor.addReroute()
    let paths=(params["presentation"] as? [String:Any])?["cables"] as? [[String:Any]] ?? []
    try require(paths.count==1 && (paths[0]["points"] as? [[Double]])?.count==2 && paths[0]["target"] as? String=="n2","Adding a reroute extends the cable path without adding a processor or changing audio")
    editor.selectedID="visual:tone";editor.canvas.selectNodes(["visual:tone"]);editor.canvas.selectedEdge=nil;editor.canvas.onDelete?()
    try require(method=="graph.presentation.set" && ((params["presentation"] as? [String:Any])?["regions"] as? [[String:Any]])?.count==1,"Deleting a visual frame removes only its annotation")
    // Ordinary cards use the same persisted presentation transaction, retaining
    // connected main, sidechain and parameter sockets with their stable IDs.
    presentation["regions"]=[];presentation["collapsedNodes"]=["n2"];definition["presentation"]=presentation
    var audio=definition["audio"] as! [[String:Any]];audio.append(["source":"n1","target":"n2","input":1]);definition["audio"]=audio
    definition["modulation"]=[["source":"n1","target":"n2","parameter":777]]
    editor.portCatalogs["n2"]=["buses":[["direction":"input","index":1,"name":"Detector","channels":2],["direction":"input","index":2,"name":"Unused","channels":2]],"parameters":[["id":777,"name":"Cutoff","writable":true,"canSlide":true]]]
    editor.update(["library":[definition]]);editor.selectedID="n2";editor.canvas.selectNodes(["n2"])
    let compact=editor.canvas.nodes.first{$0.id=="n2"}!
    try require(compact.collapsed && compact.title=="Filter" && compact.inputs.contains{$0.number==1 && !$0.modulation} && compact.inputs.contains{$0.number==777 && $0.modulation} && !compact.inputs.contains{$0.number==2 && !$0.modulation} && editor.canvas.edges.count==5,"Compact ordinary nodes retain their names and all connected sidechain/parameter ports without changing edges")
    let socket=compact.inputs.first{$0.number==1 && !$0.modulation}!,point=compact.portPoint(socket,output:false)
    try require(editor.canvas.socket(at:point)?.port.number==1 && compact.rect.contains(NSPoint(x:compact.x+1,y:point.y)) && compact.meterY==compact.y+33,"Collapsed card port geometry and hit testing use the same compact coordinates")
    editor.toggleVisualCollapse()
    try require(method=="graph.presentation.set" && ((params["presentation"] as? [String:Any])?["collapsedNodes"] as? [String])==[],"H expands a selected ordinary card through one presentation-only Undo transaction")
    var toggleCount=0;let ordinaryDisclosure=SignalCanvas(frame:NSRect(x:0,y:0,width:600,height:350));ordinaryDisclosure.update([compact],edges:[]);ordinaryDisclosure.onCollapse={toggleCount+=1}
    let ordinaryClick=NSEvent.mouseEvent(with:.leftMouseDown,location:ordinaryDisclosure.convert(NSPoint(x:compact.disclosureRect.midX,y:compact.disclosureRect.midY),to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!
    ordinaryDisclosure.mouseDown(with:ordinaryClick)
    try require(toggleCount==1 && ordinaryDisclosure.selected=="n2" && !ordinaryDisclosure.isEditing,"An ordinary-node disclosure collapses directly without beginning a move or opening the plugin")
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:600,height:350))
    var edge=SignalCanvasEdge(source:"a",target:"b",label:"");edge.waypoints=[NSPoint(x:250,y:290)]
    canvas.update([SignalCanvasNode(id:"a",title:"A",detail:"",kind:"plugin",x:30,y:30),SignalCanvasNode(id:"b",title:"B",detail:"",kind:"plugin",x:400,y:30)],edges:[edge]);canvas.selectedEdge=0;canvas.selectReroute(0)
    var cuts=0,reroutes=[[NSPoint]]();canvas.onDelete={cuts+=1};canvas.onReroute={_,points in reroutes.append(points)}
    let key=NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:[],timestamp:0,windowNumber:0,context:nil,characters:"",charactersIgnoringModifiers:"",isARepeat:false,keyCode:51)!
    canvas.keyDown(with:key)
    try require(cuts==0 && reroutes.count==1 && reroutes[0].isEmpty && canvas.edges.count==1,"Deleting a selected reroute point preserves its audio cable")
    let disclosure=SignalCanvas(frame:NSRect(x:0,y:0,width:600,height:350));var opened:String?
    disclosure.update([visual],edges:[]);disclosure.onOpen={opened=$0}
    let click=NSEvent.mouseEvent(with:.leftMouseDown,location:disclosure.convert(NSPoint(x:visual.x+15,y:visual.y+15),to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!
    disclosure.mouseDown(with:click)
    try require(opened==visual.id && !disclosure.isEditing,"The frame header disclosure toggles in one click without starting a drag")
  }
}

extension InterfaceTests {
  static func graphParameterDropChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:650))
    let parameters:[[String:Any]]=[
      ["id":777,"name":"Mix","min":0.0,"max":1.0,"value":0.4,"writable":true,"canSlide":true,"step":0.0],
      ["id":17,"name":"Mode","min":0.0,"max":3.0,"value":1.0,"writable":true,"canSlide":false,"step":1.0],
      ["id":9,"name":"Meter","min":0.0,"max":1.0,"value":0.0,"writable":false,"canSlide":true]]
    let song:[String:Any]=["songSources":[["id":"n20","kind":"lfo","name":"LFO","x":80.0,"y":400.0]],"songModulation":[],"library":[],"plugins":[["id":"rack-a","name":"Gain","isInstrument":false]],"mixer":["buses":[["id":"n1","kind":"track","name":"Track 1","output":"n2","inserts":["rack-a"]],["id":"n2","kind":"master","name":"Master","inserts":[]]]]]
    var writes=[(String,[String:Any])]()
    editor.onRequest={method,p,reply in
      if method=="plugin.parameters.get"{reply(["result":["revision":"drop:1","data":parameters]])}
      else{writes.append((method,p));reply(["error":["message":"captured"]])}
    }
    editor.update(song);editor.selectedID="plugin:rack-a";editor.canvas.selected=editor.selectedID;editor.inspect()
    let continuous=editor.rackControls.parameterDropTarget(row:0)!,stepped=editor.rackControls.parameterDropTarget(row:1)!,readonly=editor.rackControls.parameterDropTarget(row:2)!
    let surface=GraphRackControls(frame:NSRect(x:0,y:0,width:320,height:420))
    surface.onRequest={_,_,reply in reply(["result":["revision":"surface:1","data":parameters]])};surface.context(["id":"visible-processor"])
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:320,height:420),styleMask:[.titled],backing:.buffered,defer:false);window.contentView=surface;surface.layoutSubtreeIfNeeded()
    let rowRect=surface.table.rect(ofRow:1),rowPoint=surface.table.convert(NSPoint(x:rowRect.midX,y:rowRect.midY),to:nil)
    try require(surface.parameterDropTarget(atWindowPoint:rowPoint)?.parameter==17,"The actual visible parameter row resolves its stable identity from a window-space pointer")
    surface.search.stringValue="Mix";surface.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification))
    try require(surface.parameterDropTarget(row:0)?.parameter==777,"Filtering controls changes row order without changing drop identities")
    surface.isHidden=true;try require(surface.parameterDropTarget(atWindowPoint:rowPoint)==nil,"Hidden parameter surfaces cannot receive a drop")
    try require(editor.beginParameterDrop(source:"source:n20"),"A source drag captures the currently visible rack parameter surface")
    editor.completeParameterDrop(continuous)
    try require(writes.count==1 && writes[0].0=="graph.song.modulation.set" && writes[0].1["source"] as? String=="n20" && writes[0].1["plugin"] as? String=="rack-a" && writes[0].1["parameter"] as? UInt32==777 && writes[0].1["maximum"] as? Int==0,"A host control drop creates one zero-depth connection using stable source, processor and parameter identities")
    writes=[];try require(editor.beginParameterDrop(source:"source:n20"),"Source drag can repeat after a failed transaction")
    editor.completeParameterDrop(readonly);try require(writes.isEmpty && editor.status.stringValue.contains("read-only"),"Dropping onto a read-only parameter is explained without writing")
    _=editor.beginParameterDrop(source:"source:n20");editor.completeParameterDrop(stepped)
    try require(writes.isEmpty && editor.targetMenu.entries.first?.id=="quantized","Stepped control drops await an explicit discrete-values choice")
    editor.targetMenu.choose();try require(writes.count==1 && writes[0].1["quantized"] as? Bool==true,"The explicit discrete choice remains one graph transaction")
    writes=[];_=editor.beginParameterDrop(source:"source:n20");editor.rackControls.context(["id":"other"]);editor.completeParameterDrop(continuous)
    try require(writes.isEmpty && editor.status.stringValue.contains("processor changed"),"A parameter surface retargeted during a drag cannot receive the old connection")
    let recipe=SignalGraphEditor(frame:.zero);let definition:[String:Any]=["id":"n100","number":1,"nodes":[["id":"n1","kind":"input"],["id":"n2","kind":"plugin","name":"Gain"],["id":"n3","kind":"output"],["id":"n4","kind":"lfo"]],"audio":[["source":"n1","target":"n2"],["source":"n2","target":"n3"]],"modulation":[]]
    recipe.onRequest={method,p,reply in if method=="graph.plugin.get"{reply(["result":["revision":"recipe:1","data":["parameters":parameters,"buses":[]]]])}else{writes.append((method,p));reply(["error":["message":"captured"]])}}
    recipe.graphID="n100";recipe.update(["library":[definition]]);recipe.selectedID="n2";recipe.canvas.selected="n2";recipe.inspect()
    let recipeTarget=recipe.pluginControls.parametersView.parameterDropTarget(row:0)!
    try require(recipe.beginParameterDrop(source:"n4"),"Reusable modulation source captures the recipe's stable parameter surface")
    recipe.completeParameterDrop(recipeTarget)
    let connections=(writes.last?.1["definition"] as? [String:Any])?["modulation"] as? [[String:Any]] ?? []
    try require(writes.last?.0=="graph.update" && connections.count==1 && connections[0]["source"] as? String=="n4" && connections[0]["target"] as? String=="n2" && connections[0]["maximum"] as? Double==0 && connections[0]["base"] as? Double==0.4,"Recipe drops preserve the manual baseline and use one zero-depth definition edit")
    writes=[];_=recipe.beginParameterDrop(source:"n4");recipe.completeParameterDrop(recipe.pluginControls.parametersView.parameterDropTarget(row:1))
    try require(writes.isEmpty && recipe.targetMenu.entries.first?.id=="quantized","Reusable recipe drops await explicit discrete mode rather than silently approximating a stepped parameter")
    recipe.targetMenu.choose()
    let discrete=(writes.last?.1["definition"] as? [String:Any])?["modulation"] as? [[String:Any]] ?? []
    try require(writes.count==1 && discrete.first?["quantized"] as? Bool==true && discrete.first?["maximum"] as? Double==0,"Recipe discrete source drops publish one explicit zero-depth edge")
    writes=[]
    recipe.addEntry(.init(id:"lfo",title:"LFO",detail:"",keywords:"lfo",payload:["kind":"lfo"]),graph:"n100",target:nil,node:nil,position:.zero,connecting:.init(node:"n2",port:.init(number:17,label:"Mode",modulation:true),output:false))
    try require(writes.isEmpty && recipe.targetMenu.entries.first?.id=="quantized","Creating a recipe source from a stepped parameter port also asks for discrete mode")
    recipe.targetMenu.choose()
    try require(writes.count==1 && writes[0].0=="graph.node.add" && (writes[0].1["connect"] as? [String:Any])?["quantized"] as? Bool==true,"Create-and-connect carries the explicit discrete choice in one atomic request")
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:500,height:300))
    canvas.update([SignalCanvasNode(id:"source",title:"Source",detail:"",kind:"modulation",x:60,y:60,inputs:[],outputs:[.init(modulation:true)]),SignalCanvasNode(id:"target",title:"Target",detail:"",kind:"plugin",x:300,y:60)],edges:[]);canvas.selected="target"
    var selected=[String](),moves=0,drops=0,ends=0
    canvas.onPrepareParameterDrag={$0=="source"};canvas.onSelect={selected.append($0)};canvas.onMoveNodes={_ in moves+=1};canvas.onParameterDrop={_ in drops+=1};canvas.onEndParameterDrag={ends+=1}
    func mouse(_ type:NSEvent.EventType,_ p:NSPoint)->NSEvent {NSEvent.mouseEvent(with:type,location:canvas.convert(p,to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!}
    let original=canvas.nodes[0].rect,outside=NSPoint(x:canvas.visibleRect.maxX+120,y:120)
    canvas.mouseDown(with:mouse(.leftMouseDown,NSPoint(x:100,y:80)));canvas.mouseDragged(with:mouse(.leftMouseDragged,outside))
    try require(selected.isEmpty && canvas.isEditing,"Dragging a modulator keeps the current parameter inspector instead of switching it to the source")
    canvas.mouseUp(with:mouse(.leftMouseUp,outside))
    try require(drops==1 && moves==0 && ends==1 && canvas.nodes[0].rect==original && canvas.selected=="target","A control drop restores the source position and creates no separate layout Undo entry")
    canvas.mouseDown(with:mouse(.leftMouseDown,NSPoint(x:100,y:80)));canvas.mouseDragged(with:mouse(.leftMouseDragged,NSPoint(x:200,y:120)));canvas.cancelGesture()
    try require(drops==1 && moves==0 && ends==2 && canvas.nodes[0].rect==original && canvas.selected=="target","Escape restores the source and inspector with no connection or position write")
    canvas.mouseDown(with:mouse(.leftMouseDown,NSPoint(x:100,y:80)));canvas.mouseUp(with:mouse(.leftMouseUp,NSPoint(x:100,y:80)))
    try require(selected.last=="source","An ordinary click still inspects the modulation source")
  }
}


extension InterfaceTests {
  static func graphMutationFeedbackChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let song:[String:Any]=["plugins":[["id":"compressor","name":"Compressor","audioBuses":[["direction":"input","index":0,"name":"Main","active":true],["direction":"input","index":1,"name":"Detector","active":false],["direction":"output","index":0,"name":"Output","active":true]]]],"mixer":["buses":[["id":"n1","name":"Kick","kind":"track","output":"n2","inserts":["compressor"]],["id":"n2","name":"Master","kind":"master"],["id":"n3","name":"Sub","kind":"track","output":"n2"],["id":"n4","name":"Other","kind":"track","output":"n2"]]]]
    var holdRead=false,pendingRead:(([String:Any])->Void)?,writes=[[String:Any]]()
    let refusal="This routing change needs a stopped transport; playback and routing were preserved"
    editor.onRequest={method,params,reply in
      if method=="graph.get" {
        if holdRead{pendingRead=reply}else{reply(["result":["revision":"routing:1","data":song]])}
      }else if method=="mixer.sidechains.set" {
        writes.append(params);reply(["error":["code":-32002,"message":refusal]])
      }else{reply(["result":["revision":"routing:1","data":["parameters":[]]]])}
    }
    editor.load();editor.showActivity([],playing:true)
    holdRead=true;editor.load()
    editor.connectPorts("n3","plugin:compressor",out:0,input:1,modulation:false)
    try require(editor.loading && writes.isEmpty && editor.status.stringValue=="Applying graph change…","A socket release during a graph read queues its intent and replaces the obsolete hover hint")
    holdRead=false;pendingRead?(["result":["revision":"routing:1","data":song]])
    let deadline=Date().addingTimeInterval(1.2)
    repeat {RunLoop.current.run(until:Date().addingTimeInterval(0.01))}while (writes.count<6 || editor.loading) && Date()<deadline
    try require(writes.count==6 && !editor.loading && writes.allSatisfy{$0["expectedRevision"] as? String=="routing:1" && $0["plugin"] as? String=="compressor" && $0["input"] as? Int==1 && ($0["sources"] as? [[String:Any]])?.first?["source"] as? String=="n3"},"Queued detector connection retries only its captured source, stable plugin, port and revision")
    try require(editor.status.stringValue==refusal && !editor.songConnections.contains{$0["kind"] as? String=="plugin-input"},"A rejected detector connection exposes the backend reason and preserves existing cables")
    editor.rebuild();try require(editor.status.stringValue==refusal,"Catalogue redraw cannot erase a rejected routing operation")
    editor.update(song);editor.load();try require(editor.status.stringValue==refusal,"Passive projection update and refresh retain the routing refusal")
    editor.canvas.onCableHint?("Drag to add a connection")
    editor.rebuild();try require(editor.status.stringValue != refusal,"The next deliberate cable gesture clears the previous operation's error")

    var connected=song,mixer=song["mixer"] as! [String:Any]
    mixer["sidechains"]=[["source":"n3","plugin":"compressor","input":1,"gainDB":-3.0]];connected["mixer"]=mixer;editor.update(connected)
    let index=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-input"}!
    writes=[];pendingRead=nil
    editor.onRequest={method,params,reply in
      if method=="graph.get"{pendingRead=reply}
      else if method=="mixer.sidechains.set"{writes.append(params);reply(["error":["code":-32001,"message":"Captured revision is stale"]])}
      else{reply(["result":["data":["parameters":[]]]])}
    }
    editor.load();editor.rewire(index,source:"n4",target:"plugin:compressor",out:0,input:1,modulation:false)
    pendingRead?(["result":["revision":"routing:2","data":connected]])
    RunLoop.current.run(until:Date().addingTimeInterval(0.12))
    try require(writes.count==1 && writes[0]["expectedRevision"] as? String=="routing:1" && (writes[0]["sources"] as? [[String:Any]])?.first?["source"] as? String=="n4" && (writes[0]["sources"] as? [[String:Any]])?.first?["gainDB"] as? Double == -3,"A handle release during refresh retains its revision, chosen source and existing sidechain gain")
    try require(editor.status.stringValue=="Captured revision is stale","A queued rewire conflict stays visible instead of silently rebasing the gesture")

    editor.update(connected);editor.canvas.selectedEdge=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-input"}!
    let handle=editor.canvas.wireHandle(editor.canvas.selectedEdge!,source:true)!
    let down=NSEvent.mouseEvent(with:.leftMouseDown,location:editor.canvas.convert(handle,to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!
    editor.canvas.mouseDown(with:down);try require(editor.canvas.isWiring,"Editor starts an endpoint gesture before a pending refresh completes")
    editor.update(song)
    try require(!editor.canvas.isEditing && editor.status.stringValue.contains("Graph connections changed"),"An invalidated gesture's explanation survives the editor's full projection rebuild")
  }
}


extension InterfaceTests {
  static func graphGestureRefreshChecks() throws {
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:900,height:450))
    let nodes=[SignalCanvasNode(id:"a",title:"A",detail:"",kind:"audio",x:30,y:40),SignalCanvasNode(id:"b",title:"B",detail:"",kind:"audio",x:360,y:40),SignalCanvasNode(id:"c",title:"C",detail:"",kind:"audio",x:650,y:220)]
    let first=SignalCanvasEdge(source:"a",target:"b",label:"First",amount:1,waypoints:[NSPoint(x:270,y:120)]),second=SignalCanvasEdge(source:"b",target:"c",label:"Second",amount:0.5)
    var invalidations=0,commits=0
    canvas.onInvalidatedGesture={_ in invalidations+=1};canvas.onRewire={_,_,_,_,_,_ in commits+=1};canvas.onReroute={_,_ in commits+=1};canvas.onAmount={_,_ in commits+=1};canvas.onCutEdges={_ in commits+=1}
    func mouse(_ type:NSEvent.EventType,_ point:NSPoint)->NSEvent {NSEvent.mouseEvent(with:type,location:canvas.convert(point,to:nil),modifierFlags:[],timestamp:0,windowNumber:0,context:nil,eventNumber:0,clickCount:1,pressure:1)!}
    canvas.update(nodes,edges:[first,second]);canvas.selectedEdge=0
    let handle=canvas.wireHandle(0,source:true)!
    canvas.mouseDown(with:mouse(.leftMouseDown,handle));try require(canvas.isWiring,"Wire endpoint handle starts an indexed gesture")
    var measured=first;measured.label="Live value";measured.amount=0.8;measured.enabled=false
    var liveNodes=nodes;liveNodes[0].activity="playing";liveNodes[0].detail="Live meter"
    canvas.update(liveNodes,edges:[measured,second])
    try require(canvas.isWiring && invalidations==0,"Unchanged connection identities retain an active gesture through live value, enable-state and label refreshes")
    canvas.update(nodes,edges:[second,first]);canvas.mouseUp(with:mouse(.leftMouseUp,nodes[2].portPoint(.init(),output:false)))
    try require(!canvas.isEditing && invalidations==1 && commits==0,"Reordered edge indices cancel a pending rewire before it can target another connection")
    canvas.update(nodes,edges:[first,second]);canvas.selectedEdge=0
    let point=first.waypoints[0];canvas.mouseDown(with:mouse(.leftMouseDown,point));canvas.mouseDragged(with:mouse(.leftMouseDragged,NSPoint(x:280,y:160)))
    try require(canvas.isEditing,"Reroute handle starts a geometry edit")
    canvas.update(nodes,edges:[second]);canvas.mouseUp(with:mouse(.leftMouseUp,NSPoint(x:280,y:160)))
    try require(!canvas.isEditing && invalidations==2 && commits==0 && canvas.edges[0].waypoints==second.waypoints,"Deleting an edited cable cancels its reroute without writing old points onto the remaining edge")
    canvas.update(nodes,edges:[first,second]);canvas.cutTool=true
    canvas.mouseDown(with:mouse(.leftMouseDown,NSPoint(x:270,y:60)));canvas.mouseDragged(with:mouse(.leftMouseDragged,NSPoint(x:270,y:200)))
    try require(canvas.isEditing,"Cut gesture retains crossed edge identities until release")
    canvas.update(nodes,edges:[second,first]);canvas.mouseUp(with:mouse(.leftMouseUp,NSPoint(x:270,y:200)))
    try require(invalidations==3 && commits==0 && !canvas.cutTool,"Topology changes cancel pending cut indices instead of removing replacement cables")
  }
}


extension InterfaceTests {
  static func graphSignalRedrawChecks() throws {
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:800,height:1100))
    let scroll=NSScrollView(frame:NSRect(x:0,y:0,width:800,height:240));scroll.documentView=canvas
    canvas.collectDrawStatistics=true
    let nodes=[SignalCanvasNode(id:"a",title:"A sufficiently long processor title",detail:"Details stay intact",kind:"plugin",x:40,y:50),
      SignalCanvasNode(id:"b",title:"Second processor",detail:"Sidechain and output",kind:"plugin",x:370,y:120),
      SignalCanvasNode(id:"far",title:"Offscreen processor",detail:"Retained pixels update",kind:"plugin",x:40,y:700)]
    canvas.update(nodes,edges:[SignalCanvasEdge(source:"a",target:"b",label:"Main output")])
    let crop=NSRect(x:0,y:0,width:800,height:850)
    func port(_ node:String,_ output:Bool,_ peak:Double,_ clipped:Bool=false,_ number:Int=0)->[String:Any] {
      ["key":node+(output ? "/out/":"/in/")+String(number),"node":node,"port":number,"direction":output ? "output":"input","name":"Main","measured":true,"peak":[peak,peak],"rms":[peak*0.5,peak*0.5],"clipped":clipped,"channels":2]
    }
    func reading(_ ports:[[String:Any]],active:Bool=true)->GraphSignalReadings {var value=GraphSignalReadings();value.update(["active":active,"ports":ports]);return value}
    func bitmap()->NSBitmapImageRep {NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:Int(crop.width),pixelsHigh:Int(crop.height),bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!}
    func paint(_ rep:NSBitmapImageRep,_ rect:NSRect) {
      NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()}
      NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep)
      let context=NSGraphicsContext.current!.cgContext;context.clip(to:rect)
      canvas.draw(rect)
    }
    func data(_ rep:NSBitmapImageRep)->Data{Data(bytes:rep.bitmapData!,count:rep.bytesPerRow*rep.pixelsHigh)}
    let initial=reading([port("a",false,0.2),port("a",true,0.3),port("b",false,0.1),port("b",true,0.4),port("far",true,0.2)])
    func compare(_ before:GraphSignalReadings,_ after:GraphSignalReadings,_ label:String)throws {
      canvas.signalReadings=before;let partial=bitmap();paint(partial,crop)
      canvas.signalReadings=after;let dirty=canvas.signalDirtyRects(from:before)
      for rect in dirty {paint(partial,rect)}
      let full=bitmap();paint(full,crop)
      try require(data(partial)==data(full),"Incremental graph telemetry redraw exactly matches the full bitmap: "+label)
    }
    let louder=reading([port("a",false,0.7),port("a",true,1.3,true),port("b",false,0.1),port("b",true,0.4),port("far",true,0.8)])
    canvas.signalReadings=louder
    let meterRects=canvas.signalDirtyRects(from:initial)
    try require(meterRects.count==3 && meterRects.allSatisfy{$0.height==7},"Only changed meter pixels are invalidated, including the overload indicator")
    try require(meterRects.contains{$0.minY>canvas.visibleRect.maxY},"Offscreen changed meters retain explicit invalidation for later scroll exposure")
    canvas.resetDrawStatistics();let targeted=bitmap();for rect in meterRects {paint(targeted,rect)}
    let partialLabels=canvas.drawStatistics["meanDrawnLabels"] as! Double
    canvas.resetDrawStatistics();paint(bitmap(),crop)
    try require(partialLabels < canvas.drawStatistics["meanDrawnLabels"] as! Double,"A meter-only redraw skips unrelated title/detail/port text preparation")
    try compare(initial,louder,"levels and overload latch")
    let stopped=reading([port("a",false,0.7),port("a",true,1.3,true),port("b",false,0.1),port("b",true,0.4),port("far",true,0.8)],active:false)
    try compare(louder,stopped,"transport stop clears moving levels but retains overload")
    try compare(louder,reading([]),"retired measurements remove meters and listen icons, restoring full-width titles")
    try compare(reading([]),initial,"newly measured ports shrink titles and reveal listen icons")
    var alias=initial;alias.aliases=[GraphBoundaryPort(node:"b",number:0,output:true,modulation:false):GraphRealPort(node:"a",number:0)]
    try compare(initial,alias,"group alias changes target reading")
    canvas.signalReadings=initial
    try require(canvas.signalDirtyRects(from:initial).isEmpty,"Identical readings do not request any canvas redraw")
    var metadata=port("a",true,0.3);metadata["rms"]=[0.12,0.12];metadata["through"]=400.0;metadata["lastSignal"]=399.0
    let metadataOnly=reading([port("a",false,0.2),metadata,port("b",false,0.1),port("b",true,0.4),port("far",true,0.2)])
    canvas.signalReadings=metadataOnly
    try require(canvas.signalDirtyRects(from:initial).isEmpty,"Scope timestamps and undisplayed RMS changes do not invalidate card pixels")
    canvas.listeningPort=nil;let monitored=bitmap();paint(monitored,crop)
    canvas.listeningPort="a/out/0";let listenRects=canvas.listeningDirtyRects(from:nil);for rect in listenRects{paint(monitored,rect)}
    let listenFull=bitmap();paint(listenFull,crop)
    try require(listenRects.count==1 && data(monitored)==data(listenFull),"Listening changes repaint exactly the affected headphone badge")
    canvas.listeningPort=nil
    let activeCopy=bitmap();paint(activeCopy,crop)
    canvas.nodes[0].detail="Channel 1 · Ordinary · playing #2";canvas.nodes[0].activity="playing"
    paint(activeCopy,canvas.nodes[0].rect.insetBy(dx:-2,dy:-2))
    let activeFull=bitmap();paint(activeFull,crop)
    try require(data(activeCopy)==data(activeFull),"A recipe activity transition repaints its own card without damaging neighboring labels, wires or meters")
    var compactNodes=canvas.nodes;compactNodes[0].collapsed=true
    let expandedCard=canvas.nodes[0].rect;canvas.update(compactNodes,edges:canvas.edges)
    try require(canvas.nodes[0].rect.height<expandedCard.height && canvas.nodes[0].outputs.count==1,"Compact presentation reduces card height while keeping a connected meter/port")
    try compare(initial,louder,"compact card meters and overload latch")
    try compare(louder,reading([]),"compact title/listen availability")
    scroll.allowsMagnification=true;scroll.minMagnification=0.3;scroll.magnification=0.5
    try compare(initial,reading([]),"overview two-line title and listen availability")
    canvas.signalReadings=louder
    try require(canvas.signalDirtyRects(from:initial).isEmpty,"Overview mode avoids hidden meter invalidation")
    print("PASS graph incremental telemetry: pixel-exact meters/listen/aliases/stop/retirement, offscreen invalidation and text work skipped")
  }
  static func graphSignalIndexAndNameChecks() throws {
    func port(_ node:String,_ output:Bool,_ number:Int=0,_ name:String="Main",_ peak:Double=0.2)->[String:Any] {
      ["key":node+(output ? "/out/":"/in/")+String(number),"node":node,"port":number,"direction":output ? "output":"input","name":name,"measured":true,"peak":[peak,peak],"rms":[peak,peak],"channels":2]
    }
    var stale=port("a",true);stale["fresh"]=false
    let staleReading=GraphPortReading(stale)!
    try require(!staleReading.measured && staleReading.peak==0 && staleReading.summary(active:true).contains("current-route measurement"),"Stale host data cannot look like a currently measured silent signal")
    stale["available"]=false;stale["fresh"]=true
    let retiredReading=GraphPortReading(stale)!
    try require(!retiredReading.measured && retiredReading.summary(active:true).contains("unavailable in current route"),"A retired observation reports route availability instead of reusing its previous measurement")
    var indexed=GraphSignalReadings()
    indexed.update(["ports":[port("a",true,0,"First",0.2),port("a",false),port("a",true,0,"Duplicate",0.4),port("b",true,1)]])
    try require(indexed.port("a",output:true)?.name=="First" && indexed.nodePorts("a").map(\.name)==["First","Main","Duplicate"],"Indexed readings preserve first-duplicate lookup and original node port ordering")
    indexed.aliases=[GraphBoundaryPort(node:"group",number:0,output:true,modulation:false):GraphRealPort(node:"b",number:1)]
    try require(indexed.port("group",output:true)?.key=="b/out/1" && indexed.primaryPort("group",output:true)?.key=="b/out/1","Alias lookups preserve exact auxiliary port identity")
    indexed.update(["ports":[port("a",false)]])
    try require(indexed.port("a",output:true)==nil && indexed.nodePorts("b").isEmpty && indexed.primaryPort("group",output:true)==nil,"Retired ports leave neither stale direct nor aliased index entries")
    let editor=SignalGraphEditor(frame:.zero)
    var song:[String:Any]=["plugins":[["id":"fx","name":"Compressor"]],"mixer":["buses":[["id":"track","name":"Drums","kind":"track","output":"main","inserts":["fx"]],["id":"main","kind":"master","name":"Main"]]],"groups":[["id":"inner","name":"Inner","nodes":["plugin:fx"],"parent":"outer"],["id":"outer","name":"Outer","nodes":[]]]]
    editor.update(song)
    editor.showSignals(["active":true,"ports":[port("plugin:fx",true)]])
    try require(editor.signalReadings.ports.first?.name=="Drums › Outer › Inner › Compressor › Main","Cached signal labels preserve channel, nested group, processor and real port identity")
    editor.showSignals(["active":true,"ports":[port("plugin:fx",true,0,"Detector",0.8)]])
    try require(editor.signalReadings.ports.first?.name.hasSuffix("Compressor › Detector")==true,"A hosted port rename invalidates the retained full label")
    song["plugins"]=[["id":"fx","name":"Renamed processor"]]
    song["mixer"]=["buses":[["id":"track","name":"Percussion","kind":"track","output":"main","inserts":["fx"]],["id":"main","kind":"master","name":"Main"]]]
    song["groups"]=[["id":"inner","name":"Changed group","nodes":["plugin:fx"]]]
    editor.update(song);editor.showSignals(["active":true,"ports":[port("plugin:fx",true)]])
    try require(editor.signalReadings.ports.first?.name=="Percussion › Changed group › Renamed processor › Main","A structural refresh immediately updates bus, group and processor names")
    editor.showSignals(["active":false,"ports":[]])
    try require(editor.signalPortNames.isEmpty,"Retired host ports cannot grow the transient full-name cache indefinitely")
    print("PASS graph telemetry indices/cache: first match, ordering, alias identity, retirement and external structural/port renames")
  }
}


extension InterfaceTests {
  static func graphRouteObservationChecks() throws {
    func physical(_ node:String,_ key:String)->[String:Any] {
      ["key":key,"node":node,"port":0,"direction":"output","name":"Main","available":true,"fresh":true,"measured":true,"peak":[0.8,0.8],"rms":[0.4,0.4],"channels":2]
    }
    func route(_ key:String,_ descriptor:[String:Any],_ peak:Double=0.2)->[String:Any] {
      var result=physical(descriptor["source"] as? String ?? "plugin:fx",key),value=descriptor
      value["tap"]=value["tap"] ?? "post-gain";value["gainDB"]=value["gainDB"] ?? -12.0
      result["route"]=value;result["peak"]=[peak,peak];result["compensation"]=43.0
      return result
    }
    let main:[String:Any]=["kind":"output","source":"a","target":"main"]
    let send:[String:Any]=["kind":"send","source":"a","target":"main","preFader":true]
    let side:[String:Any]=["kind":"plugin-input","source":"a","plugin":"fx","input":1]
    let graphIn:[String:Any]=["kind":"graph-input","source":"a","target":"b","input":2]
    let graphOut:[String:Any]=["kind":"graph-output","source":"b","target":"main","output":2]
    let pluginOut:[String:Any]=["kind":"plugin-output","plugin":"fx","target":"main","output":1]
    let insert:[String:Any]=["kind":"insert","source":"b","plugin":"fx","tap":"main-path"]
    let final:[String:Any]=["kind":"master-output","source":"main","tap":"pre-master-fader"]
    let descriptors=[main,send,side,graphIn,graphOut,pluginOut,insert,final]
    let routePorts=descriptors.enumerated().map{route("opaque-route-\($0.offset)",$0.element)}
    var readings=GraphSignalReadings()
    readings.update(["ports":routePorts+[physical("a","physical-a")]])
    try require(readings.port("a",output:true)?.key=="physical-a" && readings.nodePorts("a").count==1,"Route observations cannot collide with physical port indices, node meters or socket choices")
    for (index,d) in descriptors.enumerated(){try require(readings.route(GraphSignalRouteID(d)!)?.key=="opaque-route-\(index)","Every route kind resolves its exact stable endpoint tuple")}
    readings.update(["ports":[routePorts[0],route("different-token-same-route",main)]])
    try require(readings.route(GraphSignalRouteID(main)!)==nil,"Ambiguous adopted-route observations never silently choose the first token")
    readings.update(["ports":[physical("a","physical-a")]])
    try require(readings.route(GraphSignalRouteID(main)!)==nil,"Retired route lookup cannot fall back to the source output")
    var malformed=routePorts[0];malformed["route"]=["kind":"output","source":"a","target":"main","input":-1,"tap":"post-gain"]
    try require(GraphPortReading(malformed)==nil,"Malformed route metadata cannot be indexed as a physical port")
    let sendReading=GraphPortReading(routePorts[1])!
    try require(sendReading.summary(active:true).contains("pre-fader") && sendReading.summary(active:true).contains("-12.0 dB") && sendReading.summary(active:true).contains("43 frames route delay"),"Cable details expose adopted gain, pre/post-fader tap and actual compensation")
    try require(!GraphPortReading(routePorts[5])!.summary(active:true).contains("post-fader"),"Auxiliary plugin output does not falsely claim a bus-fader tap")
    let editor=SignalGraphEditor(frame:.zero)
    let buses:[[String:Any]]=[["id":"a","name":"Kick","kind":"track","output":"main","sends":[["target":"main","gainDB":-6.0,"preFader":false]]],
      ["id":"b","name":"Bass","kind":"track","output":"main","inserts":["fx"]],
      ["id":"main","name":"Main","kind":"master","inserts":["master-fx"]]]
    let song:[String:Any]=["plugins":[["id":"fx","name":"Compressor"],["id":"master-fx","name":"Limiter"]],
      "mixer":["buses":buses,"sidechains":[["source":"a","plugin":"fx","input":1]],"instruments":[["plugin":"fx","target":"main","output":1]]],
      "inputs":[["source":"a","target":"b","input":2]],"outputs":[["source":"b","target":"main","output":2]]]
    editor.update(song);editor.showSignals(["active":true,"routing":["state":"preparing"],"ports":routePorts+[physical("a","physical-a")]])
    for (index,d) in descriptors.enumerated() {
      let found=editor.songConnections.indices.first{i in editor.observedCablePort(i)?.key=="opaque-route-\(index)"}
      try require(found != nil,"Song cable \(d["kind"]!) resolves its exact adopted contribution")
    }
    let sendIndex=editor.songConnections.firstIndex{$0["kind"] as? String=="send"}!
    let adopted=editor.observedCablePort(sendIndex)!
    try require(adopted.name=="Kick → Main · send" && adopted.route?.preFader==true && adopted.route?.gainDB == -12,"Pending UI route edits never overwrite the adopted audio plan's gain or fader tap")
    try require(editor.routingStatus.stringValue.contains("previous route"),"Pending topology remains clearly identified while inspecting adopted contributions")
    editor.canvas.selectedEdge=sendIndex
    try require(editor.canvas.scopeTarget(at:nil)=="opaque-route-1","Wire scope selects the route key, never reconstructs a source-output key")
    var requested="";editor.listenControls.onRequest={method,params,reply in if method=="graph.listen.set"{requested=params["port"] as? String ?? ""};reply(["result":["data":[:]]])}
    editor.listenSelected()
    try require(requested=="opaque-route-1","Wire Listen uses exactly the same post-gain contribution as Scope")
    editor.showSignals(["active":true,"ports":[physical("a","physical-a")]])
    requested="";editor.openScope(spectrum:false);editor.listenSelected()
    try require(requested.isEmpty && editor.status.stringValue.contains("unavailable for this cable"),"Missing cable measurement gives an explicit unavailable state without silently choosing another port")
    editor.graphID="library-definition";editor.showSignals(["active":true,"ports":routePorts])
    try require(editor.observedCablePort(sendIndex)==nil,"Root-route telemetry cannot masquerade as a shared recipe copy's internal wire")
    print("PASS exact cable observations: route identity, physical isolation, adopted gain/delay, ambiguity, retirement, pending topology and scope/listen keys")
  }
}
