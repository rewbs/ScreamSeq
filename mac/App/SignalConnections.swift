import AppKit

extension SignalGraphEditor {
  func canvasNode(_ node:[String:Any],definition:[String:Any])->SignalCanvasNode {
    let id=node["id"] as? String ?? "",kind=node["kind"] as? String ?? ""
    var result=SignalCanvasNode(id:id,title:node["name"] as? String ?? kind,detail:kind,kind:["input","output","plugin"].contains(kind) ? "audio" : "modulation",x:node["x"] as? Double ?? 40,y:node["y"] as? Double ?? 40)
    let audio=definition["audio"] as? [[String:Any]] ?? [],modulation=definition["modulation"] as? [[String:Any]] ?? []
    let catalog=portCatalogs[id]?["buses"] as? [[String:Any]] ?? []
    func ports(output:Bool)->[SignalCanvasPort]{
      var numbers=Set<UInt32>([0])
      for edge in audio where edge[output ? "source" : "target"] as? String==id{numbers.insert((edge[output ? "output" : "input"] as? NSNumber)?.uint32Value ?? 0)}
      let recipe=node["plugin"] as? [String:Any] ?? [:]
      for p in recipe[output ? "outputs" : "inputs"] as? [NSNumber] ?? []{numbers.insert(p.uint32Value)}
      for p in catalog where p["direction"] as? String==(output ? "output" : "input"){numbers.insert((p["index"] as? NSNumber)?.uint32Value ?? 0)}
      return numbers.sorted().map{Self.audioPort($0,output:output,catalog:catalog)}
    }
    result.inputs=["plugin","output","follower"].contains(kind) ? ports(output:false) : []
    result.outputs=["plugin","input"].contains(kind) ? ports(output:true) : kind=="output" ? [] : [SignalCanvasPort(label:"Signal",modulation:true)]
    if kind=="plugin"{
      var ids=Set(modulation.filter{$0["target"] as? String==id}.compactMap{($0["parameter"] as? NSNumber)?.uint32Value})
      if let exposed=exposedParameters[id]{ids.insert(exposed)}
      let parameters=portCatalogs[id]?["parameters"] as? [[String:Any]] ?? []
      result.inputs += ids.sorted().map{number in SignalCanvasPort(number:number,label:parameters.first{($0["id"] as? NSNumber)?.uint32Value==number}?["name"] as? String ?? "Param \(number)",modulation:true,signal:.parameter)}
    }
    return result
  }
  @objc func connectionChosen(){selectConnection(connection.indexOfSelectedItem)}
  func selectConnection(_ index:Int){
    guard canvas.edges.indices.contains(index)else{return}
    selectedID=nil;canvas.selected=nil;canvas.selectedEdge=index;connection.selectItem(at:index)
    let edge=canvas.edges[index]
    picker(source,canvas.nodes.map{($0.title,$0.id)},select:edge.source)
    picker(destination,canvas.nodes.map{($0.title,$0.id)},select:edge.target)
    outputPort.stringValue=String(edge.output);inputPort.stringValue=String(edge.input)
    if graphID != nil {
      let index=definitionEdgeIndices[index]
      let audio=definition?["audio"] as? [[String:Any]] ?? [],mod=definition?["modulation"] as? [[String:Any]] ?? []
      if index<audio.count {connectionKind.selectItem(withTitle:"Audio");connectionGain.doubleValue=audio[index]["gain"] as? Double ?? 1}
      else if mod.indices.contains(index-audio.count){let m=mod[index-audio.count];connectionKind.selectItem(withTitle:"Modulation");parameter.stringValue=String(edge.input);minimum.doubleValue=m["minimum"] as? Double ?? 0;maximum.doubleValue=m["maximum"] as? Double ?? 1;base.doubleValue=m["base"] as? Double ?? 0;connectionEnabled.state=(m["enabled"] as? Bool ?? true) ? .on : .off}
    }else{selectSongConnection(index)}
    connectionModeChanged();inspect();configureConnectionInspector()
    connectionHeading.scrollToVisible(connectionHeading.bounds)
    status.stringValue="Selected connection · changes save immediately · Return focuses settings · Delete removes"
  }
  func changeCableAmount(_ index:Int,value:Double){
    guard value.isFinite,!loading else{return};selectConnection(index)
    // Pointer coordinates can leave values such as -11.99999999999996. Keep
    // more precision than the gesture can express, without exposing that noise.
    if canvas.edges[index].modulation{maximum.stringValue=String(format:"%.12g",value)}else{connectionGain.stringValue=String(format:"%.12g",value)}
    updateConnection()
  }
  func updateConnection(){
    guard let visibleIndex=canvas.selectedEdge,canvas.edges.indices.contains(visibleIndex),let a=chosen(source),let b=chosen(destination)else{status.stringValue="Select a wire first";return}
    guard graphID != nil else{updateSongConnection(visibleIndex,source:a,target:b);return}
    let index=definitionEdgeIndices[visibleIndex]
    let audio=definition?["audio"] as? [[String:Any]] ?? [],mods=definition?["modulation"] as? [[String:Any]] ?? []
    if index<audio.count {
      guard connectionKind.titleOfSelectedItem=="Audio",let out=UInt32(outputPort.stringValue),let input=UInt32(inputPort.stringValue),let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Use valid audio ports and a gain multiplier";return}
      let from=realPort(a,out,output:true,modulation:false),to=realPort(b,input,output:false,modulation:false)
      let a=from.node,b=to.node,realOutput=from.number,realInput=to.number
      updateDefinition{d in var edges=audio;edges[index]=["source":a,"target":b,"output":realOutput,"input":realInput,"gain":gain];d["audio"]=edges}
    }else{
      guard mods.indices.contains(index-audio.count),connectionKind.titleOfSelectedItem=="Modulation",let param=UInt32(parameter.stringValue),let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),let baseline=Double(base.stringValue),lo.isFinite,hi.isFinite,baseline.isFinite else{status.stringValue="Use valid modulation settings";return}
      let from=realPort(a,UInt32(outputPort.stringValue) ?? 0,output:true,modulation:true),to=realPort(b,param,output:false,modulation:true)
      let a=from.node,b=to.node,realParameter=to.number
      updateDefinition{d in var edges=mods;edges[index-audio.count]=["source":a,"target":b,"parameter":realParameter,"minimum":lo,"maximum":hi,"base":baseline,"enabled":connectionEnabled.state == .on]
        // Base belongs to the destination parameter; every contributing source shares it.
        for i in edges.indices where edges[i]["target"] as? String==b && (edges[i]["parameter"] as? NSNumber)?.uint32Value==realParameter{edges[i]["base"]=baseline};d["modulation"]=edges}
    }
  }
  func arrange(){
    guard !loading,!canvas.nodes.isEmpty else{return}
    var levels=Dictionary(canvas.nodes.map{($0.id,0)},uniquingKeysWith:{_,last in last})
    for _ in 0..<canvas.nodes.count{var changed=false;for edge in canvas.edges{if let from=levels[edge.source],let to=levels[edge.target],to<from+1{levels[edge.target]=min(canvas.nodes.count,from+1);changed=true}};if !changed{break}}
    if graphID==nil,let master=buses.first(where:{$0["kind"] as? String=="master"})?["id"] as? String,levels[master] != nil {levels[master]=(levels.filter{$0.key != master}.values.max() ?? 0)+1}
    var bottoms=[Int:Double](),positions=[String:(Double,Double)]()
    for node in canvas.nodes{let level=levels[node.id] ?? 0,y=bottoms[level] ?? 32;positions[node.id]=(32+Double(level)*235,y);bottoms[level]=y+node.rect.height+44}
    moveNodes(positions.map{($0.key,$0.value.0,$0.value.1)})
  }
  func selectSongConnection(_ index:Int){
    guard songConnections.indices.contains(index)else{return};let action=songConnections[index]
    let kind=action["kind"] as? String ?? ""
    connectionKind.selectItem(withTitle:["output":"Main output","send":"Send","graph-input":"Graph sidechain","graph-output":"Graph auxiliary","plugin-input":"Plugin sidechain","plugin-output":"Plugin auxiliary"][kind] ?? "Main output")
    if kind=="send",let bus=buses.first(where:{$0["id"] as? String==action["source"] as? String}),let i=action["index"] as? Int,let sends=bus["sends"] as? [[String:Any]],sends.indices.contains(i){connectionGain.doubleValue=sends[i]["gainDB"] as? Double ?? -12}
    if ["graph-input","plugin-input"].contains(kind),let i=action["index"] as? Int{let entries=(kind=="graph-input" ? data["inputs"] : mixer["sidechains"]) as? [[String:Any]] ?? [];if entries.indices.contains(i){connectionGain.doubleValue=entries[i]["gainDB"] as? Double ?? 0}}
    if kind=="plugin-input",let i=action["index"] as? Int,let entries=mixer["sidechains"] as? [[String:Any]],entries.indices.contains(i),entries[i]["input"] as? Int==0{connectionKind.selectItem(withTitle:"Mix into main")}
  }
  func updateSongConnection(_ index:Int,source a:String,target b:String){
    guard songConnections.indices.contains(index),let rawOutput=UInt32(outputPort.stringValue),let rawInput=UInt32(inputPort.stringValue)else{return}
    let sourcePort=realPort(a,rawOutput,output:true,modulation:false),targetPort=realPort(b,rawInput,output:false,modulation:false)
    let a=sourcePort.node,b=targetPort.node,output=Int(sourcePort.number),input=Int(targetPort.number)
    let action=songConnections[index],from=songNodeBus[a] ?? a,to=songNodeBus[b] ?? b
    if ["graph-input","plugin-input"].contains(action["kind"] as? String ?? ""),!validateBusInputSource(a,port:output){return}
    switch action["kind"] as? String {
    case "output":
      guard from==action["source"] as? String else{status.stringValue="A main output belongs to its source bus. Use New… to route a different bus.";return}
      mutate("mixer.bus.set",["bus":from,"output":to])
    case "send":
      guard from==action["source"] as? String,let bus=buses.first(where:{$0["id"] as? String==from}),let i=action["index"] as? Int,let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Remove and reconnect to change a send's source";return}
      var sends=bus["sends"] as? [[String:Any]] ?? [];guard sends.indices.contains(i)else{return};sends[i]["target"]=to;sends[i]["gainDB"]=gain;if gain > -96 {sends[i]["enabled"]=true};mutate("mixer.sends.set",["bus":from,"sends":sends])
    case "graph-input":
      guard let i=action["index"] as? Int,case let port=input,let gain=Double(connectionGain.stringValue),gain.isFinite else{return};var list=data["inputs"] as? [[String:Any]] ?? [];guard list.indices.contains(i)else{return};list[i].merge(["source":from,"target":to,"input":port,"gainDB":gain]){_,new in new};mutate("graph.routes.set",["inputs":list])
    case "graph-output":
      guard let i=action["index"] as? Int,case let port=output else{return};var list=data["outputs"] as? [[String:Any]] ?? [];guard list.indices.contains(i)else{return};list[i]=["source":from,"target":to,"output":port];mutate("graph.routes.set",["outputs":list])
    case "plugin-output":
      guard let plugin=songNodePlugin[a],plugin==action["plugin"] as? String,case let port=output,port==action["output"] as? Int else{status.stringValue="Remove and reconnect to change an auxiliary output port";return};var targets=pluginOutputTargets(plugin,port:port);if let old=action["target"] as? String,let i=targets.firstIndex(of:old){targets[i]=to};mutate("mixer.plugin.route",["plugin":plugin,"targets":targets,"output":port])
    case "plugin-input":
      guard let i=action["index"] as? Int,let plugin=songNodePlugin[b],case let port=input,let gain=Double(connectionGain.stringValue),gain.isFinite else{return};let all=mixer["sidechains"] as? [[String:Any]] ?? [];guard all.indices.contains(i),all[i]["plugin"] as? String==plugin,all[i]["input"] as? Int==port else{status.stringValue="Remove and reconnect to change the sidechain destination";return}
      var sources=[[String:Any]]();for (j,route) in all.enumerated() where route["plugin"] as? String==plugin && route["input"] as? Int==port{var item=route;item.removeValue(forKey:"plugin");item.removeValue(forKey:"input");if j==i{item["source"]=from;item["gainDB"]=gain};sources.append(item)};mutate("mixer.sidechains.set",["plugin":plugin,"input":port,"sources":sources])
    default:status.stringValue="This wire represents the insert order. Open the subgraph to edit its connections."
    }
  }
}

extension SignalGraphEditor {
  var selectedConnectionIsEditable:Bool {
    guard let i=canvas.selectedEdge,canvas.edges.indices.contains(i) else{return false}
    return graphID != nil || (songConnections.indices.contains(i) && ["output","send","graph-input","graph-output","plugin-input","plugin-output"].contains(songConnections[i]["kind"] as? String ?? ""))
  }
  func configureConnectionInspector() {
    guard connectionSection != nil else{return}
    let selected=canvas.selectedEdge != nil,editable=selectedConnectionIsEditable
    connectionSection.isHidden = !selected && !manualConnection
    inspectorScroll.isHidden = !selected && selectedID == nil && graphID == nil && !manualConnection
    connectionHeading.stringValue=selected ? "SELECTED CONNECTION" : "NEW CONNECTION"
    connectButton.isEnabled = !selected;updateConnectionButton.isEnabled=editable;removeConnectionButton.isEnabled=editable
    connectButton.isHidden=selected;updateConnectionButton.isHidden = true;removeConnectionButton.isHidden = !editable
    connectionForm.isHidden=selected && !editable;openConnectionOwnerButton.isHidden = !selected || editable
    connectionKind.isEnabled = !selected
    source.isEnabled=true;destination.isEnabled=true
    if selected && !editable {
      connectionHint.stringValue="Drag an insert’s input to another channel’s output to move that insert and the rest of its chain. Open a subgraph to edit its internal wires."
      openConnectionOwnerButton.title=connectionOwnerGraphNode==nil ? "Open mixer / assignment…" : "Open subgraph…"
    } else {
      connectionHint.stringValue=selected ? "Drag the round endpoint handles to reroute, or edit these values; changes commit immediately." : graphID != nil ? "Choose nodes and named ports. Multiple cables into an input are summed. Sockets add cables; handles reroute. Hollow ports enable automatically when connected." : "Choose nodes and named ports. Main in inserts an effect chain; Sidechain feeds its detector. Sockets add cables; handles reroute. Mix into main sums an extra channel."
      if graphID==nil,let i=canvas.selectedEdge,songConnections.indices.contains(i) {
        let kind=songConnections[i]["kind"] as? String ?? ""
        source.isEnabled = !["output","send","plugin-output"].contains(kind)
        destination.isEnabled=kind != "plugin-input"
        if kind=="plugin-output" {outputPort.isEnabled=false;outputChoice.isEnabled=false}
        if kind=="plugin-input" {inputPort.isEnabled=false;inputChoice.isEnabled=false}
        if !source.isEnabled || !destination.isEnabled {connectionHint.stringValue += " Locked endpoints belong to this bus or plugin; use Remove and New… to replace them."}
      }
    }
  }
  func newConnection() {
    manualConnection=true;canvas.selectedEdge=nil;selectedID=nil;canvas.selected=nil;hasDraft=false
    connectionGain.doubleValue=graphID==nil ? 0 : 1
    inspect();connectionModeChanged();configureConnectionInspector();connectionHeading.scrollToVisible(connectionHeading.bounds)
    window?.makeFirstResponder(source)
  }
  func focusConnection() {
    guard canvas.selectedEdge != nil else{return}
    layoutSubtreeIfNeeded()
    connectionHeading.scrollToVisible(connectionHeading.bounds)
    if !selectedConnectionIsEditable {window?.makeFirstResponder(openConnectionOwnerButton);return}
    let target:NSView=connectionKind.titleOfSelectedItem=="Modulation" ? minimum : connectionGain.isEnabled && connectionKind.titleOfSelectedItem != "Main output" ? connectionGain : destination
    window?.makeFirstResponder(target);(target as? NSTextField)?.selectText(nil)
  }
  func focusConnectionValue(_ field:NSTextField) {
    // The selection reveals previously hidden inspector sections. Lay them out
    // before AppKit chooses the field editor or it silently refuses focus.
    layoutSubtreeIfNeeded();field.scrollToVisible(field.bounds)
    window?.makeKeyAndOrderFront(nil);window?.makeFirstResponder(field);field.selectText(nil)
    let document=projectionDocument,graph=graphID,group=processingGroupID,edge=canvas.selectedEdge,selection=selectedID
    DispatchQueue.main.async {[weak self,weak field] in
      guard let self,let field,self.projectionDocument==document,self.graphID==graph,self.processingGroupID==group,
        self.canvas.selectedEdge==edge,self.selectedID==selection,!field.isHiddenOrHasHiddenAncestor else{return}
      // The Add child panel may still be finishing its key-window handoff.
      // Retry only the same target, after native window activation settles.
      self.window?.makeFirstResponder(field);field.selectText(nil)
    }
  }
  var connectionOwnerGraphNode:String? {
    guard let i=canvas.selectedEdge,canvas.edges.indices.contains(i) else{return nil}
    return [canvas.edges[i].target,canvas.edges[i].source].first{songNodeGraph[$0] != nil}
  }
  func openConnectionOwner() {
    guard let i=canvas.selectedEdge,canvas.edges.indices.contains(i) else{return}
    if let node=connectionOwnerGraphNode {canvas.selectedEdge=nil;openSongNode(node)}
    else if let bus=songNodeBus[canvas.edges[i].source] ?? songNodeBus[canvas.edges[i].target] {onBus?(bus)}
  }
}
