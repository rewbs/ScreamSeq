import AppKit
extension InterfaceTests {
  static func graphProvenanceChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    let data:[String:Any]=["plugins":[["id":"stable","name":"Gain","isInstrument":false]],"mixer":["buses":[["id":"track","name":"Track 1","kind":"track","output":"master","inserts":["stable"]],["id":"master","name":"Master","kind":"master","output":""]]]]
    editor.update(data)
    let envelope:[String:Any]=["key":"envelope:n23","kind":"envelope","id":"n23","plugin":"stable","parameter":UInt32.max,"pattern":4,"patternID":"n11","enabled":false,"count":2,"position":16*65536,"endPosition":32*65536]
    let commands:[String:Any]=["key":"pattern:n11:n5:1:2","kind":"pattern-commands","plugin":"stable","parameter":UInt32.max,"pattern":4,"patternID":"n11","track":"n5","channel":2,"column":1,"binding":2,"enabled":true,"count":2,"position":17*65536,"endPosition":18*65536,"commands":[["kind":"pattern-set","position":17*65536,"value":0.2,"duration":0],["kind":"pattern-slide","position":18*65536+4096,"value":0.8,"duration":65536]]]
    let recorded:[String:Any]=["key":"recorded:stable:4294967295","kind":"recorded","plugin":"stable","parameter":UInt32.max,"enabled":true,"count":3,"firstFrame":48000,"lastFrame":96000]
    var methods=[String](),opened:[[String:Any]]=[]
    editor.onRequest={method,params,reply in
      methods.append(method)
      if method=="graph.provenance.get"{reply(["result":["data":["sources":[envelope,commands,recorded],"total":3]]])}
    }
    editor.onSourceReference={opened.append($0)}
    editor.showParameterProvenance(plugin:"stable",parameter:UInt32.max)
    let references=editor.canvas.nodes.filter{$0.kind=="provenance"}
    try require(references.count==3 && methods.filter{$0=="graph.provenance.get"}.count==1,"One exposed stable parameter lazily projects its existing sources with one bounded read")
    try require(references.allSatisfy{$0.outputs.first?.unavailable != nil} && editor.canvas.edges.filter{$0.readOnlyReason != nil}.count==3,"Existing automation has visibly read-only base-reference ports and wires")
    try require(editor.canvas.edges.contains{$0.readOnlyReason != nil && $0.input==UInt32.max && !$0.enabled},"Full-width parameter identity and disabled envelope state survive the graph projection")
    let existing=editor.addEntries().first{$0.id=="provenance:envelope:n23"}!
    let readsBefore=methods.count
    editor.addEntry(existing,graph:nil,target:nil,node:nil,position:.zero)
    try require(editor.selectedID=="provenance:envelope:n23" && methods.count==readsBefore,"Unified Add reveals an existing source by stable reference without duplicating its lane or making a song edit")
    let edge=editor.canvas.edges.firstIndex{$0.source=="provenance:envelope:n23"}!
    editor.selectConnection(edge)
    try require(!editor.provenanceControls.isHidden && editor.connectionSection.isHidden,"Reference selection explains baseline provenance instead of offering a misleading Connect or depth form")
    let writesBefore=methods.count
    editor.cutConnections([edge]);editor.rewire(edge,source:"track",target:"master",out:0,input:0,modulation:false);editor.setReroute(edge,points:[NSPoint(x:100,y:100)])
    editor.canvas.selected="provenance:envelope:n23";editor.selectedID=editor.canvas.selected;editor.canvas.selectedEdge=nil;editor.canvas.onDelete?()
    try require(methods.count==writesBefore,"Cut, repatch, reroute and Delete on source references cannot create musical edits or partial batch mutations")
    editor.moveNodes([("provenance:envelope:n23",700,800)])
    try require(methods.count==writesBefore && editor.canvas.nodes.first{$0.id=="provenance:envelope:n23"}?.x==700,"Repositioning source reference cards is transient and never adds song Undo history")
    editor.openNode("provenance:envelope:n23")
    try require(opened.last?["id"] as? String=="n23" && editor.provenance.hasReturn,"Double-click preserves the exact existing envelope identity and a route back")
    editor.openNode("provenance:pattern:n11:n5:1:2")
    try require(editor.targetMenu.entries.count==2,"A grouped command reference exposes its exact underlying row commands")
    editor.targetMenu.search.stringValue="18.06";editor.targetMenu.filter();editor.targetMenu.choose()
    try require(opened.last?["kind"] as? String=="pattern-slide" && opened.last?["position"] as? Int==18*65536+4096 && opened.last?["column"] as? Int==1 && opened.last?["channel"] as? Int==2,"Command bridge keeps precise timing, channel and FX subcolumn instead of inventing a second lane")
    var single=commands;single["commands"]=[(commands["commands"] as! [[String:Any]])[0]];let priorOpens=opened.count
    editor.openProvenanceSource(single)
    try require(opened.count==priorOpens+1 && opened.last?["kind"] as? String=="pattern-set" && opened.last?["position"] as? Int==17*65536,"A sole pattern command opens its exact source directly without a second chooser")
    editor.openNode("provenance:recorded:stable:4294967295")
    try require(opened.last?["kind"] as? String=="recorded","Recorded provenance opens its existing recorded data source")
    var returns=0;editor.onReveal={returns+=1};editor.returnToProvenance();try require(returns==1,"Back reveals the retained graph context")
    editor.hideParameterProvenance();try require(!editor.canvas.nodes.contains{$0.kind=="provenance"},"Source projections can be hidden without removing any underlying song edits")
    editor.returnToProvenance();try require(returns==2,"A retained panel Back button still opens the graph after its transient source view was hidden")
    let bookmark=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700));bookmark.update(data)
    bookmark.filterID="track";bookmark.nodeSearch.stringValue="Gain";bookmark.selectedID="plugin:stable";bookmark.canvas.selected=bookmark.selectedID;bookmark.rebuild();bookmark.rememberPanelReturn()
    bookmark.filterID=nil;bookmark.nodeSearch.stringValue="Master";bookmark.selectedID="master";bookmark.canvas.selected="master";bookmark.rebuild();bookmark.restorePanelReturn()
    try require(bookmark.filterID=="track" && bookmark.nodeSearch.stringValue=="Gain" && bookmark.selectedID=="plugin:stable" && bookmark.canvas.selection==["plugin:stable"],"Back from parameter activity restores the captured channel filter, search and stable processor selection")
    bookmark.onRequest={method,_,reply in if method=="graph.get"{reply(["result":["revision":"new-document:1","data":data]])}}
    bookmark.load()
    try require(bookmark.panelReturn==nil,"Opening another document discards graph return targets even if local IDs happen to match")
    let reveal=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    var saved=data;saved["layout"]=[["node":"plugin:stable","x":400.0,"y":1400.0],["node":"provenance:envelope:n23","x":150.0,"y":2300.0]]
    reveal.update(saved);reveal.nodeCategory.selectItem(at:2);reveal.changeNodeFilter()
    var revealReply:(([String:Any])->Void)?
    reveal.onRequest={method,_,reply in if method=="graph.provenance.get"{revealReply=reply}}
    let oldOrigin=reveal.scroll.contentView.bounds.origin
    reveal.showParameterProvenance(plugin:"stable",parameter:UInt32.max)
    try require(reveal.provenance.revealPending && reveal.scroll.contentView.bounds.origin==oldOrigin,"Source reveal waits for its asynchronous data instead of prematurely framing an empty target")
    revealReply?(["result":["data":["sources":[envelope],"total":1]]])
    guard let shown=reveal.canvas.nodes.first(where:{$0.id=="provenance:envelope:n23"})else{throw InterfaceFailure(message:"Explicit provenance reveal remained filtered out")}
    try require(reveal.selectedID==shown.id && reveal.scroll.documentVisibleRect.contains(NSPoint(x:shown.rect.midX,y:shown.rect.midY)),"The explicit source action selects and frames its single result even outside the previous viewport/filter")
    try require(shown.x==150 && shown.y==2300 && (reveal.data as NSDictionary).isEqual(to:saved),"Revealing existing sources preserves saved card coordinates and creates no song mutation")
    try require(reveal.provenanceControls.summary.lineBreakMode == .byWordWrapping && reveal.provenanceControls.summary.maximumNumberOfLines==0 && reveal.provenanceControls.summary.preferredMaxLayoutWidth>0,"Provenance explanations wrap without an ellipsis or fixed line cutoff")
    reveal.canvas.scroll(.zero);let parked=reveal.scroll.contentView.bounds.origin
    reveal.provenance.requestKey="";reveal.loadParameterProvenance();revealReply?(["result":["data":["sources":[envelope],"total":1]]])
    try require(reveal.scroll.contentView.bounds.origin==parked,"Passive provenance refresh does not reframe or steal the musician's chosen viewport")
    var deferred:(([String:Any])->Void)?
    editor.onRequest={method,_,reply in if method=="graph.provenance.get"{deferred=reply}}
    editor.showParameterProvenance(plugin:"stable",parameter:42);editor.hideParameterProvenance();deferred?(["result":["data":["sources":[envelope],"total":1]]])
    try require(editor.provenance.target==nil && editor.provenance.sources.isEmpty,"A retired provenance request cannot reopen a hidden or retargeted projection")

    editor.provenance.target=("stable",UInt32.max);editor.provenance.total=65;var pageOffset:Int?
    editor.onRequest={method,params,reply in if method=="graph.provenance.get"{pageOffset=params["offset"] as? Int;reply(["result":["data":["sources":[recorded],"total":65]]])}}
    editor.nextProvenancePage();try require(pageOffset==64 && editor.provenance.ordered.count==1,"Bounded source views page to later sources instead of creating an unbounded graph")
    editor.nextProvenancePage();try require(pageOffset==0,"Source paging cycles back to the first page without changing underlying automation")

    let ports=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    ports.update(data);ports.portCatalogs["plugin:stable"]=["parameters":[["id":0,"name":"Band 1 frequency","writable":true,"canSlide":true],["id":1,"name":"Band 1 gain","writable":true,"canSlide":true]]]
    var portReads=0,portOpened:[String:Any]?
    ports.onRequest={method,params,reply in
      guard method=="graph.provenance.get"else{return};portReads+=1
      let parameter=(params["parameter"] as? NSNumber)?.uint32Value ?? 0
      reply(["result":["data":["sources":[["key":"recorded:stable:\(parameter)","kind":"recorded","plugin":"stable","parameter":parameter,"count":2]],"total":1]]])
    }
    ports.onSourceReference={portOpened=$0}
    ports.showParameterProvenance(plugin:"stable",parameter:0)
    ports.showParameterProvenance(plugin:"stable",parameter:1)
    // A later expose/modulation-inspection action must not move this existing
    // base-reference wire to a different parameter or the audio input row.
    ports.exposedParameters["plugin:stable"]=0;ports.rebuild()
    let owner=ports.canvas.nodes.first{$0.id=="plugin:stable"}!
    guard let exact=owner.inputs.first(where:{$0.modulation && $0.number==1}),let old=owner.inputs.first(where:{$0.modulation && $0.number==0}),let index=ports.canvas.edges.firstIndex(where:{$0.readOnlyReason != nil})else{throw InterfaceFailure(message:"Provenance lost its independent parameter socket")}
    let location=owner.portPoint(exact,output:false),oldLocation=owner.portPoint(old,output:false),scale=max(0.3,ports.scroll.magnification)
    let handle=ports.canvas.wireHandle(index,source:false)!
    try require(exact.label=="Band 1 gain" && old.label=="Band 1 frequency" && location != oldLocation && handle==NSPoint(x:location.x-12/scale,y:location.y+14/scale),"A provenance wire lands on its exact named parameter socket even when another parameter remains exposed")
    ports.openNode("provenance:recorded:stable:1")
    try require(portOpened?["parameter"] as? UInt32==1 && portReads==2,"Switching same-plugin provenance retains the exact source target without new lanes or extra reads")
    var grouped=data;grouped["groups"]=[["id":"n900","name":"Group","nodes":["plugin:stable"],"x":500.0,"y":100.0]]
    ports.update(grouped)
    let projected=ports.canvas.edges.first{$0.readOnlyReason != nil}!
    let real=ports.realPort(projected.target,projected.input,output:false,modulation:true)
    let boundary=ports.canvas.nodes.first{$0.id==projected.target}!.inputs.first{$0.modulation && $0.number==projected.input}
    try require(projected.target=="n900" && real.node=="plugin:stable" && real.number==1 && boundary?.label.contains("Band 1 gain")==true,"A collapsed processing boundary preserves provenance's exact stable parameter and friendly name")

    let activity=ParameterActivityEditor(frame:.zero);var activityRequests=[(String,[String:Any])](),activityReply:(([String:Any])->Void)?
    activity.onRequest={method,params,reply in activityRequests.append((method,params));activityReply=reply}
    activity.onRecordedNames={plugin,parameter in plugin=="unloaded-stable" && parameter==Int(UInt32.max) ? ("EQ 5","Band 1 gain"):(nil,nil)}
    activity.inspectRecorded(plugin:"unloaded-stable",parameter:Int(UInt32.max))
    try require(activityRequests.last?.0=="automation.recorded.get" && activityRequests.last?.1["plugin"] as? String=="unloaded-stable" && activity.detailMode.selectedSegment==2,"Recorded source bridge reads song data directly while stopped or unloaded, without a live processor target")
    activityReply?(["result":["revision":"document:3","data":["points":[["frame":48000,"value":0.5]],"total":1]]])
    try require(activity.processor.titleOfSelectedItem=="EQ 5 · recorded song data" && activity.parameter.titleOfSelectedItem=="Band 1 gain" && activity.status.stringValue.contains("during playback") && activity.status.stringValue.contains("support Undo") && !activity.status.stringValue.contains("Stop playback"),"Recorded provenance uses opportunistic friendly names and accurately explains live edits with unified Undo")
    activity.selectProcessor();activity.poll();try require(!activity.processor.isEnabled && activity.target?["plugin"] as? String=="unloaded-stable","Recorded-only metadata cannot silently retarget to an old live processor catalogue")
    try require(activity.recorded.count==1 && activityRequests.count==1 && activity.trace.samples.isEmpty,"Recorded metadata mode does not start a trace, take over capture, or claim a live rendered value")
    activity.inspect(plugin:"loaded",parameter:2)
    try require(activityRequests.last?.0=="parameter.activity.targets","Choosing a live target exits recorded metadata mode normally")
    let focused=SignalGraphEditor(frame:.zero)
    focused.filterID="kick"
    focused.update(["plugins":[],"mixer":["buses":[["id":"kick","name":"Kick","kind":"track","output":"master"],["id":"sub","name":"Sub","kind":"track","output":"master"],["id":"return","name":"Return","kind":"return","output":"master"],["id":"master","name":"Master","kind":"master","output":""]]]])
    try require(!focused.canvas.nodes.contains{$0.id=="return"},"An unrelated return starts outside the focused Kick path")
    focused.selectedID="return";focused.canvas.selected="return";focused.revealAddedNode()
    try require(focused.canvas.nodes.contains{$0.id=="return"} && focused.selectedID=="return" && focused.filterID=="kick","A newly added disconnected bus is selected and revealed without clearing the musician's channel focus")
    focused.rebuild();try require(focused.canvas.nodes.contains{$0.id=="return"},"The explicit Add reveal survives subsequent passive refreshes")
    focused.nodeSearch.stringValue="Kick";focused.changeNodeFilter();try require(!focused.canvas.nodes.contains{$0.id=="return"},"Changing filters retires the temporary Add reveal")
    print("PASS graph provenance: bounded lazy references, exact source bridges, immutable base wires, stale reads, Back and unloaded recorded data")
  }
}
