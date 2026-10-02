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
    try graphGroupBypassChecks()
    try graphParameterBaselineChecks()
    try graphParameterRangeChecks()
    try detachedEffectChecks()
    try graphArrangeAndAddChecks()
    try graphSelectedBypassChecks()
    try graphSourceAndIndependentChecks()
    try graphClipboardAndPresetChecks()
    try graphBranchedDetachChecks()
    try graphSongSourceGroupChecks()
    try graphDetachedChainChecks()
    try graphDirectPluginConnectionChecks()
    try graphNoteRoutingChecks()
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
    for key in ["instrument:n20","source:n999","stale-plugin"] {select(key);editor.toggleSelectedBypass();try require(writes.count==count && editor.targetMenu.entries.isEmpty && editor.bypassUnavailableReason != nil,"Unsupported selected context cannot redirect bypass to a song-rack chooser")}
    editor.graphID="n100";editor.update(song);select("n102");editor.toggleSelectedBypass()
    try require(writes.count==count+1 && writes.last?.0=="graph.plugin.bypass" && writes.last?.1["graph"] as? String=="n100" && writes.last?.1["node"] as? String=="n102" && writes.last?.1["bypass"] as? Bool==true && editor.targetMenu.entries.isEmpty,"Selected recipe bypass targets the exact definition processor, not an unrelated song-rack processor")
    let action=editor.actionMenu().items.first{($0 as? ContextAction)?.commandID==GraphCommand.bypass.id}
    try require(action?.isEnabled==true && action?.title.contains("all uses")==true,"Recipe bypass is discoverable and explains its shared-definition scope")
    select("n101");editor.toggleSelectedBypass()
    try require(writes.count==count+1 && editor.bypassUnavailableReason != nil,"Graph input boundaries cannot redirect bypass to a processor")
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


extension InterfaceTests {
  static func graphSourceAndIndependentChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let recipe:[String:Any]=["id":"n100","name":"Shared","number":1,"nodes":[["id":"n101","kind":"input"],["id":"n102","kind":"lfo","muted":false],["id":"n103","kind":"output"]],"audio":[["source":"n101","target":"n103"]]]
    let song:[String:Any]=["library":[recipe],"songSources":[["id":"n200","kind":"random","muted":true]],"assignments":[["target":"n10","graph":"n100"]],"commands":[["target":"n10","graph":"n100","kind":"start"],["target":"n11","graph":"n100","kind":"row"]],"instrumentAssignments":[["target":"n20","graph":"n100"]],"instruments":[["id":"n20","index":2,"name":"Keys","plugin":false]],"mixer":["buses":[["id":"n10","name":"Lead","kind":"track","output":"n12"],["id":"n11","name":"Bass","kind":"track","output":"n12"],["id":"n12","name":"Master","kind":"master"]]]]
    editor.update(song);var writes=[(String,[String:Any])]()
    editor.onRequest={method,params,reply in writes.append((method,params));reply(["error":["message":"captured"]])}
    editor.selectedID="source:n200";editor.canvas.selectNodes(["source:n200"]);editor.toggleSelectedBypass()
    try require(editor.bypassActionTitle=="Unmute source" && writes.last?.0=="graph.source.mute" && writes.last?.1["node"] as? String=="n200" && writes.last?.1["graph"] is NSNull && writes.last?.1["muted"] as? Bool==false,"Root source mute addresses the source identity and does not mutate a rack bypass")
    let sourceMenu=NSMenu();editor.appendSelectedObjectActions(to:sourceMenu)
    try require(sourceMenu.items.first?.title=="Unmute source" && sourceMenu.items.first?.keyEquivalent=="m","A source right-click exposes mute directly with its shortcut, without nested panel menus")
    try require((editor.canvas.accessibilityValue() as? String)?.contains("muted")==true,"Canvas accessibility describes source mute state")
    editor.graphID="n100";editor.update(song);editor.selectedID="n102";editor.canvas.selectNodes(["n102"]);editor.toggleSelectedBypass()
    try require(writes.last?.0=="graph.source.mute" && writes.last?.1["graph"] as? String=="n100" && writes.last?.1["node"] as? String=="n102" && writes.last?.1["muted"] as? Bool==true && editor.bypassActionTitle.contains("all uses"),"Reusable source mute states and edits the shared-definition scope")
    editor.chooseIndependentUse(graph:"n100")
    try require(Set(editor.targetMenu.entries.map(\.id))==["channel:n10","channel:n11","instrument:n20"],"Make-independent chooser includes command-only channels, deduplicates channel uses and includes instrument scope")
    try require(editor.targetMenu.entries.first{$0.id=="channel:n10"}?.detail.contains("ordinary and pattern")==true,"Channel copy explains that paired Start/Stop and ordinary uses move together")
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="channel:n10"}!)
    try require(writes.last?.0=="graph.makeIndependent" && writes.last?.1["graph"] as? String=="n100" && writes.last?.1["target"] as? String=="n10" && writes.last?.1["scope"] as? String=="channel","Independent channel uses one exact shared-model transaction")
    let muted=editor.canvasNode(["id":"n102","kind":"lfo","muted":true],definition:recipe)
    try require(muted.detail.contains("Muted") && !muted.bypassed,"Muted sources never display an unrelated processor bypass badge")
  }
}

extension InterfaceTests {
  static func graphClipboardAndPresetChecks() throws {
    let editor=SignalGraphEditor(frame:.zero),board=NSPasteboard(name:.init("screamseq-graph-test-"+UUID().uuidString));editor.graphPasteboard=board
    defer{board.releaseGlobally()}
    let definition:[String:Any]=["id":"n100","name":"Test","nodes":[["id":"n101","kind":"input"],["id":"n102","kind":"plugin","name":"Effect"],["id":"n103","kind":"output"],["id":"n104","kind":"automation","name":"Curve","envelopes":[["pattern":"n20","points":[["position":0,"value":0.5]]]]]],"audio":[]]
    let song:[String:Any]=["library":[definition],"patterns":[["id":"n20","index":0,"name":"Verse"],["id":"n21","index":1,"name":"Chorus"]]]
    editor.graphID="n100";editor.update(song);editor.selectedID="n102";editor.canvas.selectNodes(["n102","n104"])
    var requests=[(String,[String:Any])]()
    editor.onRequest={method,params,reply in requests.append((method,params));if method=="graph.selection.copy"{reply(["result":["revision":"","data":["version":1,"fragment":definition]]])}else if method=="plugin.preset.inspect"{reply(["result":["revision":"","data":["presetRevision":"preset-1","name":"Warm"]]])}else{reply(["error":["message":"captured"]])}}
    editor.canvas.copy(nil)
    try require(requests.last?.0=="graph.selection.copy" && Set(requests.last?.1["nodes"] as? [String] ?? [])==["n102","n104"] && board.data(forType:SignalGraphEditor.graphClipboardType) != nil,"Copy uses exact selected nodes and captures their baseline state through the read API")
    editor.pasteGraphSelection()
    let paste=requests.last
    try require(paste?.0=="graph.selection.paste" && (paste?.1["patternMap"] as? [[String:String]])==[["source":"n20","target":"n20"]],"Same-song clipboard explicitly maps envelope patterns rather than trusting numeric IDs")
    editor.duplicateGraphSelection();try require(requests.last?.0=="graph.selection.duplicate" && requests.last?.1["graph"] as? String=="n100","Duplicate is one shared native transaction")
    editor.canvas.cut(nil);try require(requests.last?.0=="graph.selection.cut","Cmd-X cuts nodes through one document transaction")
    board.clearContents();board.setData(try JSONSerialization.data(withJSONObject:["version":1,"document":"other-song","fragment":definition]),forType:SignalGraphEditor.graphClipboardType)
    let count=requests.count;editor.pasteGraphSelection()
    try require(requests.count==count && Set(editor.targetMenu.entries.map(\.id))==["n20","n21"],"Cross-song envelope paste offers explicit pattern mapping before making any mutation")
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="n21"}!)
    try require((requests.last?.1["patternMap"] as? [[String:String]])==[["source":"n20","target":"n21"]],"Cross-song paste uses the chosen destination pattern")
    editor.targetMenu.close()
    let target=GraphPresetTarget(graph:"n100",plugin:"n102",name:"Effect")
    editor.loadGraphPreset(target,path:"/tmp/example.screamseq-preset",revision:"",document:editor.projectionDocument)
    try require(requests.last?.0=="graph.plugin.preset.load" && requests.last?.1["graph"] as? String=="n100" && requests.last?.1["node"] as? String=="n102" && requests.last?.1["expectedPresetRevision"] as? String=="preset-1","Recipe preset load pins its exact target and inspected file revision")
    editor.saveGraphPreset(target,path:"/tmp/example.screamseq-preset",name:"Sound",revision:"",document:editor.projectionDocument)
    try require(requests.last?.0=="graph.plugin.preset.save" && requests.last?.1["node"] as? String=="n102","Recipe preset save uses the configured baseline API, not a rack slot")
  }
}


extension InterfaceTests {
  static func graphBranchedDetachChecks() throws {
    let editor=SignalGraphEditor(frame:.zero)
    let definition:[String:Any]=["id":"n100","name":"Branches","nodes":[["id":"n101","kind":"input","name":"Input"],["id":"n102","kind":"plugin","name":"Effect"],["id":"n103","kind":"output","name":"Output"]],"audio":[["source":"n101","target":"n102","output":0,"input":0,"gain":0.5],["source":"n101","target":"n102","output":1,"input":0,"gain":0.2],["source":"n102","target":"n103","output":0,"input":0,"gain":0.3],["source":"n102","target":"n103","output":0,"input":1,"gain":0.8]]]
    editor.graphID="n100";editor.update(["library":[definition]]);editor.selectedID="n102";editor.canvas.selectNodes(["n102"])
    var writes=[(String,[String:Any])]()
    editor.onRequest={method,params,reply in writes.append((method,params));reply(["error":["message":"captured"]])}
    editor.detachNodes(["n102"],positions:[],remove:false)
    try require(writes.isEmpty && Set(editor.targetMenu.entries.map(\.id))==["0","1"],"A branched detach chooses a source before touching any route")
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="1"}!)
    try require(writes.isEmpty && Set(editor.targetMenu.entries.map(\.id))==["2","3"],"A branched detach chooses its exact destination instead of inventing a cross-product")
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="3"}!)
    let heal=writes.last?.1["heal"] as? [String:Any]
    try require(writes.count==1 && writes.last?.0=="graph.nodes.detach" && heal?["incoming"] as? Int==1 && heal?["outgoing"] as? Int==3 && writes.last?.1["remove"] as? Bool==false,"Chosen main path is one revision-guarded transaction preserving other branches")
    var grouped=definition;grouped["nodes"]=(definition["nodes"] as? [[String:Any]] ?? [])+[["id":"n104","kind":"lfo","name":"Motion"]];grouped["modulation"]=[["source":"n104","target":"n102","parameter":1]];grouped["groups"]=[["id":"n105","name":"Effect + motion","nodes":["n102","n104"],"x":100.0,"y":100.0]]
    editor.update(["library":[grouped]]);writes=[];editor.detachNodes(["n105"],positions:[("n105",400,300)],remove:false)
    editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="0"}!);editor.targetMenu.onChoose?(editor.targetMenu.entries.first{$0.id=="2"}!)
    try require(Set(writes.last?.1["nodes"] as? [String] ?? [])==["n102","n104"] && (writes.last?.1["positions"] as? [[String:Any]])?.first?["node"] as? String=="n105","Option-drag of a whole group retains its modulator and moves its boundary in the same detach transaction")
  }
}

extension InterfaceTests {
  static func graphGroupBypassChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1000,height:600))
    let group:[String:Any]=["id":"n20","name":"Parallel","nodes":["n3","n4"]]
    let recipe:[String:Any]=["id":"n1","number":1,"name":"Recipe","nodes":[["id":"n2","kind":"input"],["id":"n3","kind":"plugin"],["id":"n4","kind":"plugin"],["id":"n5","kind":"output"]],"audio":[],"groups":[group]]
    editor.graphID="n1";editor.update(["library":[recipe]]);editor.selectedID="n20";editor.canvas.selectNodes(["n20"])
    let input:[String:Any]=["source":"n2","target":"n3","output":0,"input":0],outA:[String:Any]=["node":"n3","port":0],outB:[String:Any]=["node":"n4","port":0]
    var reads=0,writes=[[String:Any]](),pending:(([String:Any])->Void)?
    editor.onRequest={method,p,reply in
      if method=="graph.group.boundary"{reads+=1;pending=reply}
      else if method=="graph.group.bypass"{writes.append(p);reply(["error":["message":"captured"]])}
    }
    editor.toggleSelectedBypass();pending?(["result":["data":["needsMapping":false]]])
    try require(reads==1 && writes.count==1 && writes[0]["graph"] as? String=="n1" && writes[0]["group"] as? String=="n20" && writes[0]["bypass"] as? Bool==true,"A selected processing group uses true boundary bypass in one exact-target mutation")
    writes=[];editor.setProcessingGroupBypass(group,bypass:true);pending?(["result":["data":["needsMapping":true,"inputs":[input],"outputs":[outA,outB]]]])
    try require(writes.isEmpty && editor.targetMenu.entries.count==1,"Ambiguous group bypass waits for explicit dry routes without partial edits")
    editor.targetMenu.onChoose?(editor.targetMenu.entries[0]);try require(writes.isEmpty,"Choosing the first branch must not commit a partial group map")
    editor.targetMenu.onChoose?(editor.targetMenu.entries[0]);try require(writes.count==1 && (writes[0]["dryRoutes"] as? [[String:Any]])?.count==2,"All branch choices commit in one group transaction")
    writes=[];editor.setProcessingGroupBypass(group,bypass:true);editor.selectedID="n3";pending?(["result":["data":["needsMapping":false]]])
    try require(writes.isEmpty,"A retargeted pending group read cannot bypass a different selection")
    editor.selectedID="n20";editor.setProcessingGroupBypass(group,bypass:false)
    try require(writes.count==1 && writes[0]["bypass"] as? Bool==false && reads==3,"Enabling a group preserves its dry mapping without an extra chooser/read")
  }
}


extension InterfaceTests {
  static func graphSongSourceGroupChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1000,height:600))
    let source:[String:Any]=["id":"n80","kind":"lfo","name":"Motion","x":30.0,"y":30.0]
    var song:[String:Any]=["plugins":[["id":"effect","name":"Effect","slot":0]],"songSources":[source],"groups":[["id":"n90","name":"Group","nodes":["source:n80","plugin:effect"]]],"mixer":["buses":[["id":"track","kind":"track","name":"Track","output":"main","inserts":["effect"]],["id":"main","kind":"master","name":"Master","output":""]]]]
    editor.update(song)
    try require(editor.expandedProcessingSelection(["n90"])==["source:n80","plugin:effect"],"A song group expands both effect and modulation source identities")
    var calls=[(String,[String:Any])]()
    editor.onRequest={method,params,reply in calls.append((method,params));reply(["error":["message":"captured"]])}
    editor.removeSongNodes(["n90"])
    try require(calls.count==1 && calls[0].0=="plugin.remove" && calls[0].1["plugins"] as? [String]==["effect"] && calls[0].1["sources"] as? [String]==["n80"],"Deleting a mixed processing group is one transaction, never partial plugin/source edits")
    calls=[];editor.removeSongNodes(["n90","main"]);try require(calls.isEmpty,"A mixed bus selection cannot silently remove only the group contents")
    song["groups"]=[];editor.update(song);editor.canvas.selectNodes(["source:n80","plugin:effect"]);editor.groupSelection()
    try require(calls.count==1 && calls[0].0=="graph.song.group.create" && Set(calls[0].1["nodes"] as? [String] ?? [])==["source:n80","plugin:effect"],"Song grouping accepts modulation sources and processors directly")
    let layout=SignalGraphEditor(frame:.zero)
    var bare:[String:Any]=["mixer":["buses":[["id":"track","kind":"track","name":"Track","output":"main"],["id":"main","kind":"master","name":"Master","output":""]]]]
    layout.update(bare);let initial=layout.canvas.nodes.first{$0.id=="main"}!.x
    bare["plugins"]=[["id":"new","name":"New effect","slot":0]]
    bare["mixer"]=["buses":[["id":"track","kind":"track","name":"Track","output":"main","inserts":["new"]],["id":"main","kind":"master","name":"Master","output":""]]]
    bare["layout"]=[["node":"plugin:new","x":initial+200,"y":30.0]];layout.update(bare)
    let processor=layout.canvas.nodes.first{$0.id=="plugin:new"}!,master=layout.canvas.nodes.first{$0.id=="main"}!
    try require(master.x>processor.rect.maxX && processor.x==initial+200,"An unpositioned Master stays downstream of an automatically inserted effect without moving the new processor")
    bare["layout"]=[["node":"plugin:new","x":initial+200,"y":30.0],["node":"main","x":40.0,"y":400.0]];layout.update(bare)
    try require(layout.canvas.nodes.first{$0.id=="main"}!.x==40,"An explicitly positioned Master remains untouched")
  }
}

extension InterfaceTests {
  static func graphDetachedChainChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1000,height:600))
    let buses:[[String:Any]]=[["id":"n1","kind":"track","name":"Track","output":"n2"],["id":"n2","kind":"master","name":"Master","inserts":["C"]]]
    let plugins:[[String:Any]]=["A","B","C"].enumerated().map{["id":$0.element,"name":$0.element,"slot":$0.offset]}
    var mixer:[String:Any]=["buses":buses,"detachedChains":[["id":"n50","plugins":["A","B"]]]]
    var song:[String:Any]=["plugins":plugins,"mixer":mixer]
    editor.update(song)
    try require(editor.effectiveInserts(buses[1])==["C"] && editor.songNodeBus["plugin:A"]=="n50" && editor.songNodeBus["plugin:B"]=="n50","Loose chain retains its silent ownership instead of reappearing in Master")
    try require(!editor.canvas.nodes.contains{$0.id=="n50"},"Internal silent root is not a fake user bus card")
    let cable=editor.canvas.edges.firstIndex{$0.source=="plugin:A" && $0.target=="plugin:B"}!
    editor.selectConnection(cable)
    try require(!editor.removeConnectionButton.isHidden && editor.removeConnectionButton.isEnabled,"Selected implicit insert supports direct Delete/Remove without exposing an unrelated gain form")
    let ref=editor.songCableReference(cable)
    try require(ref?["kind"] as? String=="insert" && ref?["source"] as? String=="n50" && ref?["plugin"] as? String=="B","Loose internal cable exposes exact cut identity")
    var writes=[(String,[String:Any])]()
    editor.onRequest={method,p,reply in writes.append((method,p));reply(["error":["message":"captured"]])}
    editor.cutConnections([cable]);try require(writes.count==1 && writes[0].0=="graph.connections.remove","An internal loose-chain cut is one atomic graph transaction")
    writes=[];editor.detachNodes(["plugin:A","plugin:B"],positions:[],remove:false)
    try require(writes.last?.0=="mixer.inserts.detach" && writes.last?.1["plugins"] as? [String]==["A","B"],"Multi-processor detach follows existing order")
    let move=editor.insertMove("n1","plugin:A")
    try require(move?["plugins"] as? [String]==["A","B"] && move?["target"] as? String=="n1","Dragging the first loose input moves its whole suffix into the destination path")
    mixer["disconnectedMainInputs"]=["B"];mixer["masterOutputDisconnected"]=true;song["mixer"]=mixer;editor.update(song)
    try require(!editor.canvas.edges.contains{$0.source=="plugin:A" && $0.target=="plugin:B"} && !editor.songConnections.contains{$0["kind"] as? String=="master-output"},"Cut hides exactly its implicit and terminal wires without hiding processors")
    writes=[];editor.selectedID="plugin:B";editor.reconnectSongMain()
    try require(writes.last?.0=="mixer.inserts.move" && writes.last?.1["before"] as? String=="B" && writes.last?.1["target"] as? String=="n50","Reconnect restores the selected exact input without reordering")
    writes=[];editor.connectPorts("plugin:C","n2",out:0,input:0,modulation:false)
    try require(writes.last?.0=="mixer.bus.set" && writes.last?.1["mainOutputConnected"] as? Bool==true,"Repatching the last Master output clears only the terminal cut")
  }
}

extension InterfaceTests {
  static func graphDirectPluginConnectionChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:640))
    let ports:[[String:Any]]=[["index":0,"direction":"input","name":"Main","channels":2],["index":1,"direction":"input","name":"Detector","channels":2],["index":0,"direction":"output","name":"Main","channels":2],["index":2,"direction":"output","name":"Channels 5–6","channels":2]]
    let plugins:[[String:Any]]=["A","B","C"].enumerated().map{["id":$0.element,"name":$0.element,"slot":$0.offset,"audioBuses":ports]}
    let route:[String:Any]=["source":"A","output":2,"target":"B","input":1,"gainDB":-8.0,"enabled":true]
    let song:[String:Any]=["plugins":plugins,"mixer":["buses":[["id":"n1","name":"Track","kind":"track","output":"n3","inserts":["A"]],["id":"n2","name":"Return","kind":"return","output":"n3","inserts":["B"]],["id":"n3","name":"Master","kind":"master","inserts":["C"]]],"pluginConnections":[route]]]
    editor.update(song)
    let i=editor.songConnections.firstIndex{$0["kind"] as? String=="plugin-connection"}!
    let edge=editor.canvas.edges[i]
    try require(edge.source=="plugin:A" && edge.target=="plugin:B" && edge.output==2 && edge.input==1 && edge.amount == -8,"Direct plugin route uses exact physical slice sockets and route gain")
    let ref=editor.songCableReference(i)!
    try require(ref["source"] as? String=="A" && ref["target"] as? String=="B" && ref["output"] as? Int==2 && ref["input"] as? Int==1,"Cut identity contains both stable plugin endpoints and ports")
    editor.selectConnection(i)
    try require(editor.connectionKind.titleOfSelectedItem=="Direct plugin audio" && !editor.connectionGainRow.isHidden && !editor.connectionEnabled.isHidden && editor.source.isEnabled && editor.destination.isEnabled,"Direct route supports immediate gain, enable and either endpoint editing")
    var writes=[(String,[String:Any])]()
    editor.onRequest={method,p,reply in writes.append((method,p));reply(["error":["message":"captured"]])}
    editor.rewire(i,source:"plugin:C",target:"plugin:B",out:0,input:0,modulation:false)
    try require(writes.count==1 && writes[0].0=="mixer.plugin.connection.set" && writes[0].1["source"] as? String=="C" && writes[0].1["input"] as? UInt32==0,"Repatching direct cable into main is one endpoint transaction, never an insert-owner move")
    let replaced=writes[0].1["replace"] as? [String:Any]
    try require(replaced?["source"] as? String=="A" && replaced?["output"] as? Int==2 && writes[0].1["gainDB"] as? Double == -8,"Rewire retains exact previous identity and gain")
    writes=[];editor.canvas.addingMainInput=true;editor.connectPorts("plugin:A","plugin:B",out:0,input:0,modulation:false);editor.canvas.addingMainInput=false
    try require(writes.last?.0=="mixer.plugin.connection.set" && writes.last?.1["gainDB"] as? Double==0,"Option main-input gesture adds unity contribution while retaining chain ownership")
    writes=[];editor.connectPorts("plugin:A","plugin:B",out:2,input:1,modulation:false)
    try require(writes.last?.0=="mixer.plugin.connection.set" && writes.last?.1["output"] as? UInt32==2,"An auxiliary processor socket goes directly to detector, not its owning bus output")
    let choices=editor.portChoices
    let a=choices.first{$0.key.node=="plugin:A" && $0.key.output && $0.key.number==2}!,b=choices.first{$0.key.node=="plugin:B" && !$0.key.output && $0.key.number==1}!
    try require(editor.portPairUnavailable(a,b)==nil,"Keyboard patcher offers the same direct processor connection")
    writes=[];editor.cutConnections([i]);let batch=writes.last?.1["connections"] as? [[String:Any]]
    try require(writes.count==1 && writes[0].0=="graph.connections.remove" && batch?.first?["kind"] as? String=="plugin-connection","Direct cable Delete shares atomic mixed-route cut")
    try require(editor.observedCablePort(i)==nil,"No exact adopted route means unavailable telemetry, not a host-port proxy")
    let dryA=editor.groupBoundaryLabel(["kind":"plugin-connection","source":"plugin:A","target":"plugin:B","output":2,"input":1],output:false)
    let dryB=editor.groupBoundaryLabel(["kind":"plugin-connection","source":"plugin:A","target":"plugin:B","output":0,"input":0],output:false)
    try require(dryA != dryB && dryA.contains("output 2") && dryA.contains("input 1"),"A dry-path chooser distinguishes exact logical ports between the same processor pair")
  }
}
