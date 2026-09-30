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
  static func signalGraphChecks() throws {
    try graphShortcutChecks()
    try processingGroupChecks()
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
    editor.findOverload();try require(editor.selectedID=="plugin:compressor" && editor.status.stringValue.contains("CLIP"),"Overload navigation selects the actual measured processor")
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
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.detail=="Ordinary · playing #2" && editor.canvas.nodes.first{$0.id==copy.id}?.activity=="playing","Song graph exposes actual processor stack order")
    editor.showActivity([],playing:false)
    try require(editor.canvas.nodes.first{$0.id==copy.id}?.activity==nil,"Stopping playback retires graph activity without altering layout")
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
    try require(opened=="mixer.bus.set" && updated["output"] is NSNull,"Deleting a song main-output wire disconnects that bus")
    var pluginSong=data;pluginSong["plugins"]=[["id":"synth","name":"Synth","isInstrument":true]]
    editor.update(pluginSong)
    let synthOutput=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-output"}!
    editor.selectConnection(synthOutput);editor.disconnect()
    try require((updated["targets"] as? [String])?.isEmpty==true,"Deleting a default instrument wire explicitly suppresses the master fallback")
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
    try require(editor.observedCablePort(firstInsert)?.key=="plugin:rack-a/in/0","A collapsed group's input cable scope retains its authoritative host tap")
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
    editor.navigateParent();editor.selectedID="n300";editor.canvas.selected="n300";editor.exportProcessingGroup()
    try require(method=="graph.song.group.export" && params["group"] as? String=="n300","Song group export uses the shared rack-to-recipe API")

  }
}
