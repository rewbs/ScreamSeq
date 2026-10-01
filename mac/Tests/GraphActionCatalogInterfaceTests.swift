import AppKit

extension InterfaceTests {
  static func graphActionCatalogChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    func actions(_ menu:NSMenu)->[ContextAction] {menu.items.flatMap{item in if let child=item.submenu{return actions(child)};return (item as? ContextAction).map{[$0]} ?? []}}
    let catalog=actions(editor.actionMenu())
    try require(Set(catalog.compactMap(\.commandID)).isSuperset(of:GraphCommand.allCases.map(\.id)),"Every registered graph command remains in the menu and complete command palette, even before selecting an object")
    try require(catalog.allSatisfy{$0.isEnabled || !($0.toolTip ?? "").isEmpty},"Unavailable graph actions always explain the specific requirement")
    let finder=SignalGraphEditor(frame:.zero)
    let finderData:[String:Any]=["mixer":["buses":[["id":"find-track","name":"Duplicate","kind":"track","output":"find-master"],["id":"find-master","name":"Duplicate","kind":"master","output":""]]]]
    var finderRevision="find-song:1"
    finder.onRequest={method,_,reply in reply(["result":["revision":finderRevision,"data":method=="graph.get" ? finderData:[:]]])}
    finder.load()
    finder.chooseVisibleNode()
    let duplicateChoices=finder.targetMenu.entries.filter{$0.title.hasSuffix("Duplicate")}
    try require(Set(duplicateChoices.map(\.id))==["find-track","find-master"],"Find node retains both stable choices when displayed bus names are disambiguated")
    let target=finder.targetMenu.entries.first{$0.id=="find-track"}!
    finder.targetMenu.onChoose?(target)
    try require(finder.selectedID=="find-track" && finder.canvas.selected=="find-track" && finder.revision=="find-song:1","The relocated node chooser selects the exact node without a song edit")
    finder.targetMenu.close();finder.chooseVisibleNode();finderRevision="find-song:2";finder.load()
    finder.targetMenu.onChoose?(finder.targetMenu.entries.first{$0.id=="find-master"}!)
    try require(finder.selectedID=="find-track" && finder.status.stringValue.contains("song changed"),"A stale node chooser never redirects an action to a changed graph")
    finder.targetMenu.close()
    finder.canvas.update([SignalCanvasNode(id:"source:one",title:"LFO",detail:"lfo",kind:"modulation",x:0,y:0),SignalCanvasNode(id:"source:two",title:"LFO",detail:"lfo",kind:"modulation",x:0,y:100)],edges:[])
    finder.chooseVisibleNode()
    try require(Set(finder.targetMenu.entries.map(\.detail))==["lfo · source:one","lfo · source:two"],"Otherwise identical source choices show a stable identity instead of indistinguishable rows")
    finder.targetMenu.close()
    editor.update(["library":[],"plugins":[["id":"stable-effect","name":"Gain","isInstrument":false]],
                   "mixer":["buses":[["id":"n1","name":"Master","kind":"master","output":"","inserts":["stable-effect"]]]]])
    var requests=[String](),reply:(([String:Any])->Void)?,opened:(String,UInt32)?
    editor.onRequest={method,_,done in requests.append(method);reply=done}
    editor.rackControls.onActivity={opened=($0,$1)}
    editor.chooseGraphParameter(.parameterActivity,graph:nil,processor:"stable-effect")
    try require(requests==["plugin.parameters.get"],"Parameter catalogue action reads the selected stable rack identity")
    reply?(["result":["data":[["id":UInt32.max,"name":"Same name","value":0.5,"writable":true],
                              ["id":17,"name":"Same name","value":0.2,"writable":false]]]])
    try require(editor.targetMenu.entries.count==2,"Duplicate parameter names retain separate selectable stable IDs")
    editor.targetMenu.search.stringValue=String(UInt32.max);editor.targetMenu.filter();editor.targetMenu.choose()
    try require(opened?.0=="stable-effect" && opened?.1==UInt32.max,"Parameter activity command resolves full-width IDs without altering a parameter")
    opened=nil;editor.chooseGraphParameter(.parameterAutomate,graph:nil,processor:"stable-effect")
    reply?(["result":["data":[["id":17,"name":"Meter","value":0.2,"writable":false]]]])
    editor.targetMenu.choose()
    try require(editor.targetMenu.hint.stringValue.contains("Read-only") && opened==nil,"Readonly automation targets explain why without issuing a write")
    editor.targetMenu.close()
    editor.chooseGraphParameter(.parameterActivity,graph:nil,processor:"stable-effect")
    editor.selectedID="different-selection"
    reply?(["result":["data":[["id":1,"name":"Old reply","value":0.5,"writable":true]]]])
    try require(editor.targetMenu.onChoose==nil,"A late parameter catalog cannot reopen a picker after graph context changes")
    let rack=GraphRackControls(frame:.zero);var loads=[([String:Any])->Void]()
    rack.onRequest={_,_,done in loads.append(done)}
    rack.context(["id":"first"]);rack.focusParameter(2)
    loads.removeFirst()(["result":["revision":"r1","data":[["id":2,"name":"Cutoff","value":0.5,"min":0,"max":1,"writable":true]]]])
    try require(rack.search.stringValue=="Cutoff","Exact-value bridge survives a pending catalog and finds its stable parameter")
    rack.context(["id":"second"]);rack.focusParameter(9);rack.context(["id":"third"])
    loads.removeFirst()(["result":["revision":"r2","data":[["id":9,"name":"Retired","value":0.5,"min":0,"max":1,"writable":true]]]])
    loads.removeFirst()(["result":["revision":"r3","data":[["id":9,"name":"New meaning","value":0.5,"min":0,"max":1,"writable":true]]]])
    try require(rack.search.stringValue != "Retired" && rack.search.stringValue != "New meaning","Retargeting retires a pending exact-value focus request")
    let searching=GraphRackControls(frame:.zero);var catalogReply:(([String:Any])->Void)?
    searching.onRequest={_,_,reply in catalogReply=reply}
    searching.context(["id":"eq"]);catalogReply?(["result":["revision":"song:1","data":[["id":1,"name":"Band 1 frequency","value":0.5,"min":0,"max":1,"writable":true]]]])
    searching.search.stringValue="Band 1 frequency";searching.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:searching.search))
    searching.context(["id":"eq"])
    try require(searching.search.stringValue=="Band 1 frequency","Passive refresh of the same processor preserves the musician's parameter search")
    searching.context(["id":"compressor"]);catalogReply?(["result":["revision":"song:1","data":[["id":2,"name":"Threshold","value":0.5,"min":0,"max":1,"writable":true]]]])
    try require(searching.search.stringValue.isEmpty && searching.filtered.count==1,"A diagnostic jump to another processor clears a stale search instead of showing an empty parameter list")
    let retry=GraphRackControls(frame:.zero);var replies=[([String:Any])->Void](),retryMethods=[String]()
    retry.onRequest={method,_,reply in retryMethods.append(method);replies.append(reply)}
    retry.context(["id":"gainer"])
    replies.removeFirst()(["result":["revision":"song:1","data":[["id":1,"name":"Gain","value":0.0,"min":-60.0,"max":12.0,"writable":true]]]])
    retry.set(1,value:-6)
    replies.removeFirst()(["error":["message":"This graph edit cannot yet be prepared"]])
    try require(retryMethods.last=="plugin.parameters.get","Rejected parameter edits reload the actual retained baseline")
    replies.removeFirst()(["result":["revision":"song:1","data":[["id":1,"name":"Gain","value":0.0,"min":-60.0,"max":12.0,"writable":true]]]])
    try require(retry.message.stringValue.contains("cannot yet"),"Automatic rollback reads preserve the actionable failed-edit message")
    retry.set(1,value:-6);replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    try require(retry.message.stringValue.contains("updated") && !retry.message.stringValue.contains("cannot yet"),"A successful retry clears the inspector's previous parameter error")
    try graphParameterBaselineChecks()
    try graphParameterRangeChecks()
    try detachedEffectChecks()
    try graphArrangeAndAddChecks()
    try graphSelectedBypassChecks()
    try graphStageChecks()
    try graphLastTouchedChecks()
    print("PASS graph parameter catalogue: stable targets, duplicate names, readonly reasons, retired reads and pending focus")
  }
  static func graphLastTouchedChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    let song:[String:Any]=["plugins":[["id":"touched","name":"Duplicate","slot":7]],
      "mixer":["buses":[["id":"main","name":"Main","kind":"master","output":"","inserts":["touched"]]]],
      "library":[["id":"g","name":"Original recipe","nodes":[["id":"in","name":"Source","kind":"input"],["id":"out","name":"Output","kind":"output"]],"audio":[],"modulation":[]]]]
    var methods=[String](),pending=[([String:Any])->Void]()
    var catalog:[[String:Any]]=[["id":UInt32.max,"name":"Duplicate","value":0.5,"min":0.0,"max":1.0,"writable":true],
      ["id":12,"name":"Duplicate","value":0.2,"min":0.0,"max":1.0,"writable":true]]
    editor.onRequest={method,params,reply in
      methods.append(method)
      if method=="automation.target.get"{pending.append(reply);return}
      let payload:Any=method=="graph.get" ? song:method=="plugin.parameters.get" ? catalog:[:]
      reply(["result":["revision":"touch:1","data":payload]])
    }
    func answer(_ target:Any,revision:String="touch:1") {pending.removeFirst()(["result":["revision":revision,"data":["target":target]]])}
    let target:[String:Any]=["available":true,"plugin":"touched","parameter":UInt32.max,"slot":7]
    editor.load();editor.navigate(graph:"g",origin:"Original channel",target:"copy:one")
    editor.selectedID="in";editor.canvas.selected="in";editor.scroll.magnification=0.85
    editor.filterID="main";editor.filter.selectItem(at:1)
    editor.showLastTouchedParameter();answer(target)
    try require(editor.graphID==nil && editor.selectedID=="plugin:touched" && editor.exposedParameters["plugin:touched"]==UInt32.max,
      "Last touched resolves the stable plugin and full-width parameter, exposes the socket and navigates out of a recipe")
    try require(editor.portCatalogs["plugin:touched"]?["parameters"] as? [[String:Any]] != nil && editor.lastTouchedReturn?.graph=="g",
      "The bridge retains the target catalogue and original graph context")
    try require(editor.filterID==nil && editor.filter.indexOfSelectedItem==0 && !editor.inspectorScroll.isHidden && !editor.rackControls.isHidden,
      "Following a parameter visibly clears the channel filter and reveals its inspector, even from an empty recipe selection")
    editor.returnFromLastTouchedParameter()
    try require(editor.graphID=="g" && editor.graphOrigin=="Original channel" && editor.graphTarget=="copy:one" && editor.selectedID=="in" && editor.filterID=="main" && editor.filter.indexOfSelectedItem==1 && abs(editor.scroll.magnification-0.85)<0.001,
      "Back restores the recipe instance context, selection and zoom")
    try require(methods.allSatisfy{$0.hasSuffix(".get")},"Following a touched knob and returning are read-only, outside document Undo")
    let original=editor.exposedParameters
    editor.showLastTouchedParameter();editor.selectedID="out";answer(target)
    try require(editor.graphID=="g" && editor.selectedID=="out" && editor.exposedParameters==original,"A late touched-target read cannot override newer navigation")
    for invalid:Any in [NSNumber(value:true),NSNumber(value:-1),NSNumber(value:4294967296.0),NSNumber(value:1.5)] {
      editor.showLastTouchedParameter();var bad=target;bad["parameter"]=invalid;answer(bad)
      try require(editor.graphID=="g" && editor.lastTouchedReturn==nil,"Invalid parameter IDs cannot wrap or address an unrelated socket")
    }
    editor.showLastTouchedParameter();answer(NSNull())
    try require(editor.status.stringValue.contains("Move a song-rack plugin knob"),"No learned gesture explains the available custom-editor fallback")
    editor.showLastTouchedParameter();answer(target,revision:"touch:2")
    try require(editor.status.stringValue.contains("song changed") && editor.graphID=="g","Mixed revision reads cannot expose a stale target")
    editor.showLastTouchedParameter();editor.showLastTouchedParameter()
    answer(target)
    try require(editor.graphID=="g" && pending.count==1,"A repeated command retires an older read even at the same document revision and view")
    answer(NSNull())
    catalog=[];editor.showLastTouchedParameter();answer(target)
    try require(editor.status.stringValue.contains("no longer exposes") && editor.graphID=="g","A missing parameter fails without changing graph depth")
    catalog=[["id":UInt32.max,"name":"Duplicate","value":0.5,"min":0.0,"max":1.0,"writable":true]]
    editor.showLastTouchedParameter();answer(target)
    try require(editor.lastTouchedReturn != nil,"The successful retry prepares a return before switching documents")
    editor.onRequest={method,_,reply in reply(["result":["revision":"other-song:1","data":method=="graph.get" ? song:[:]]])}
    editor.load()
    try require(editor.lastTouchedReturn==nil,"Changing documents clears the prior touched-parameter navigation return")
  }
  static func detachedEffectChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let effect:[String:Any]=["id":"loose","name":"Loose gain","isInstrument":false,"format":"Built-in","classID":"resonance.gainer.v1"]
    editor.update(["plugins":[effect],"mixer":["detached":["loose"],"buses":[["id":"track","name":"Track","kind":"track","output":"master"],["id":"master","name":"Master","kind":"master","output":""]]]])
    try require(editor.canvas.nodes.contains{$0.id=="plugin:loose"} && !editor.canvas.edges.contains{$0.source=="plugin:loose" || $0.target=="plugin:loose"},"A detached rack effect is visible without fake Master audio cables")
    try require(editor.songNodeBus["plugin:loose"]==nil && editor.effectiveInserts(editor.buses.last!).isEmpty,"Detached effects must not inherit a fallback Master owner")
    let main=editor.songConnections.firstIndex{$0["kind"] as? String=="output"}!
    try require(editor.insertionMove(["plugin:loose"],edge:main)?["target"] as? String=="track","Dropping an unconnected processor on a channel wire creates an exact insertion request")
    try require(editor.insertMove("track","plugin:loose")?["plugins"] as? [String]==["loose"],"Dragging a channel socket to a detached input establishes ownership of exactly that processor")
    editor.addCatalog=[effect];editor.selectedID=nil;editor.canvas.selected=nil
    let entry=editor.addEntries(connecting:nil).first{$0.payload["kind"] as? String=="plugin"}!
    try require(entry.unavailable==nil && entry.detail.contains("Unconnected"),"Blank-canvas Add offers effects with their unconnected destination clearly named")
    var requested:[String:Any]=[:],method=""
    editor.onRequest={m,p,_ in method=m;requested=p}
    editor.addEntry(entry,graph:nil,target:nil,node:nil,position:NSPoint(x:900,y:150))
    try require(method=="plugin.add" && requested["detached"] as? Bool==true && requested["target"]==nil && (requested["position"] as? [String:Any])?["x"] as? Double==900,"One atomic Add creates the detached effect at the chosen position without a Master insert")
  }
  static func graphArrangeAndAddChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1000,height:650))
    let data:[String:Any]=["plugins":[["id":"effect","name":"EQ","isInstrument":false]],"layout":[["node":"track","x":60.0,"y":100.0],["node":"plugin:effect","x":900.0,"y":100.0],["node":"master","x":1300.0,"y":100.0]],"mixer":["buses":[["id":"track","name":"Track","kind":"track","output":"master","inserts":["effect"]],["id":"master","name":"Master","kind":"master","output":""]]]]
    editor.update(data);editor.canvas.selectNodes(["track","plugin:effect"])
    var writes=[(String,[String:Any])]()
    editor.onRequest={method,params,_ in writes.append((method,params))}
    editor.arrange(onlySelection:true)
    let positions=writes.last?.1["positions"] as? [[String:Any]] ?? []
    try require(writes.count==1 && writes[0].0=="graph.layout.set" && Set(positions.compactMap{$0["node"] as? String})==["track","plugin:effect"],"Arrange selection commits exactly the chosen nodes in one layout transaction")
    try require((editor.data as NSDictionary).isEqual(to:data) && !positions.contains{$0["node"] as? String=="master"},"Selected arrangement preserves unselected saved positions and never edits routing")
    let add=editor.addEntries()
    try require(add.contains{$0.payload["kind"] as? String=="existing-automation" && $0.unavailable==nil} && add.contains{$0.payload["kind"] as? String=="visual-reroute" && $0.unavailable==nil},"Unified Add discovers existing automation and presentation-only cable reroutes")
    let chooser=SignalGraphEditor(frame:.zero);chooser.update(data);var requested=[(String,[String:Any])]()
    chooser.onRequest={method,params,_ in requested.append((method,params))}
    let reroute=chooser.addEntries().first{$0.payload["kind"] as? String=="visual-reroute"}!
    chooser.addEntry(reroute,graph:nil,target:nil,node:nil,position:.zero)
    try require(requested.isEmpty && !chooser.targetMenu.entries.isEmpty,"Adding a reroute without a selected cable offers exact targets instead of a disabled dead end")
    chooser.targetMenu.choose()
    let presentation=requested.last?.1["presentation"] as? [String:Any]
    try require(requested.count==1 && requested[0].0=="graph.presentation.set" && (presentation?["cables"] as? [[String:Any]])?.count==1,"Choosing a cable adds only presentation geometry in one Undo transaction")
  }
}


extension InterfaceTests {
  static func graphSelectedBypassChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let recipe:[String:Any]=["id":"n100","name":"Recipe","nodes":[["id":"n101","kind":"input"],["id":"n102","kind":"plugin","name":"Local effect"],["id":"n103","kind":"output"]],"audio":[["source":"n101","target":"n102"],["source":"n102","target":"n103"]]]
    let song:[String:Any]=["library":[recipe],"plugins":[["id":"effect","name":"Rack effect","isInstrument":false],["id":"synth","name":"Synth","isInstrument":true,"bypass":true]],"instruments":[["id":"n20","index":1,"name":"Sample instrument","plugin":false]],"mixer":["buses":[["id":"track","kind":"track","name":"Track","output":"master","inserts":["effect"]],["id":"master","kind":"master","name":"Master","output":""]]],"groups":[["id":"n200","nodes":["plugin:effect"],"name":"Processing group"]]]
    editor.update(song);var writes=[(String,[String:Any])]()
    editor.onRequest={method,params,reply in writes.append((method,params));reply(["error":["message":"captured"]])}
    func select(_ key:String){editor.selectedID=key;editor.canvas.selectNodes([key]);editor.canvas.selectedEdge=nil}
    select("plugin:synth");editor.toggleSelectedBypass()
    try require(editor.bypassActionTitle=="Unmute instrument" && writes.last?.0=="plugin.bypass" && writes.last?.1["plugin"] as? String=="synth" && writes.last?.1["bypass"] as? Bool==false,"Selected plugin instrument mute targets its exact stable instance, never another effect")
    select("track");editor.toggleSelectedBypass()
    try require(editor.bypassActionTitle=="Mute bus" && writes.last?.0=="mixer.bus.set" && writes.last?.1["bus"] as? String=="track" && writes.last?.1["mute"] as? Bool==true,"Channel mute uses the existing shared mixer transaction")
    let count=writes.count
    for key in ["instrument:n20","n200","source:n999","stale-plugin"] {select(key);editor.toggleSelectedBypass();try require(writes.count==count && editor.targetMenu.entries.isEmpty && editor.bypassUnavailableReason != nil,"Unsupported selected context cannot redirect bypass to a song-rack chooser")}
    editor.graphID="n100";editor.update(song);select("n102");editor.toggleSelectedBypass()
    try require(writes.count==count+1 && writes.last?.0=="graph.plugin.bypass" && writes.last?.1["graph"] as? String=="n100" && writes.last?.1["node"] as? String=="n102" && writes.last?.1["bypass"] as? Bool==true && editor.targetMenu.entries.isEmpty,"Selected recipe bypass targets the exact definition processor, not an unrelated song-rack processor")
    let action=editor.actionMenu().items.first{($0 as? ContextAction)?.commandID==GraphCommand.bypass.id}
    try require(action?.isEnabled==true && action?.title.contains("all uses")==true,"Recipe bypass is discoverable and explains its shared-definition scope")
    select("n101");editor.toggleSelectedBypass()
    try require(writes.count==count+1 && editor.bypassUnavailableReason != nil,"Source and whole-group bypass remain unsupported rather than redirecting to a processor")
    let bypassed=editor.canvasNode(["id":"n102","kind":"plugin","plugin":["bypass":true]],definition:recipe)
    try require(bypassed.bypassed && bypassed.detail.contains("dry through"),"Bypassed recipe card visibly describes host pass-through")
    editor.graphID=nil;editor.update(song);editor.selectedID=nil;editor.canvas.selected=nil;editor.canvas.selectedEdge=nil
    editor.toggleSelectedBypass()
    try require(Set(editor.targetMenu.entries.map(\.id))==["effect","synth"] && writes.count==count+1,"A targetless bypass action explicitly offers known processors without editing")
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="effect"}!)
    try require(writes.last?.0=="plugin.bypass" && writes.last?.1["plugin"] as? String=="effect","Explicit target selection uses the chosen stable plugin identity")
    editor.targetMenu.close();select("plugin:synth");editor.canvas.selectNodes(["plugin:synth","track"]);let before=writes.count;editor.toggleSelectedBypass()
    try require(writes.count==before && editor.bypassUnavailableReason?.contains("one processor")==true,"Mixed multiple selection cannot silently bypass just one arbitrary processor")
  }
}
