import AppKit

extension SignalGraphEditor {
  func detachNodes(_ ids:[String],positions:[(String,Double,Double)],remove:Bool) {
    guard !ids.isEmpty else{status.stringValue="Select an effect chain to reconnect its main path";return}
    guard let graphID else{rebuild();status.stringValue="Enter a processing group to detach its main path";return}
    var params:[String:Any]=["graph":graphID,"nodes":ids,"remove":remove]
    if !positions.isEmpty {params["positions"]=positionObjects(positions)}
    mutate("graph.nodes.detach",params)
  }
  func cutConnections(_ indices:[Int]) {
    guard graphID != nil else{status.stringValue="Cut cables inside a processing group; song connections can be selected and deleted individually";return}
    let removed=Set(indices.compactMap{definitionEdgeIndices.indices.contains($0) ? definitionEdgeIndices[$0]:nil})
    guard !removed.isEmpty else{return}
    updateDefinition{d in
      let audio=d["audio"] as? [[String:Any]] ?? [],mods=d["modulation"] as? [[String:Any]] ?? []
      d["audio"]=audio.enumerated().filter{!removed.contains($0.offset)}.map(\.element)
      d["modulation"]=mods.enumerated().filter{!removed.contains(audio.count+$0.offset)}.map(\.element)
    }
  }
  func validateBusInputSource(_ node:String,port:Int)->Bool {
    let owner=songNodeBus[node].flatMap{id in buses.first{$0["id"] as? String==id}}
    let isBus=node==songNodeBus[node],atBusEnd=songNodePlugin[node].map{effectiveInserts(owner ?? [:]).last==$0} ?? false
    guard port==0,owner != nil,isBus || atBusEnd else {
      status.stringValue="This input needs a channel or return output. Route the plugin output into a bus first, then connect that bus here; direct plugin patching is available inside subgraphs.";return false
    };return true
  }
  var selectedBuiltinDetector:[String:Any]? {
    guard graphID==nil,let selectedID,let plugin=songNodePlugin[selectedID],let p=rackPlugins.first(where:{$0["id"] as? String==plugin}),p["format"] as? String=="Built-in",["resonance.compressor.v1","resonance.bus-compressor.v1","resonance.gate.v1"].contains(p["classID"] as? String ?? "")else{return nil};return p
  }
  func useConnectedDetector() {
    guard let plugin=selectedBuiltinDetector,let slot=plugin["slot"] else{return}
    mutate("plugin.parameters.set",["slot":slot,"values":[["id":9,"value":2]]])
  }
  func moveNodes(_ positions:[(String,Double,Double)]) {
    guard !loading else{rebuild();return}
    if graphID==nil{
      let groupIDs=Set(processingGroups.compactMap{$0["id"] as? String})
      let valid=positions.filter{p in canvas.nodes.first{$0.id==p.0}?.kind != "boundary"}
      mutate("graph.layout.set",["positions":positionObjects(valid.filter{!groupIDs.contains($0.0)}),"groups":valid.filter{groupIDs.contains($0.0)}.map{["group":$0.0,"x":$0.1,"y":$0.2] as [String:Any]}]);return
    }
    updateDefinition{d in
      var list=d["nodes"] as? [[String:Any]] ?? [],groups=d["groups"] as? [[String:Any]] ?? []
      for p in positions {
        guard canvas.nodes.first(where:{$0.id==p.0})?.kind != "boundary" else{continue}
        if let group=groups.first(where:{$0["id"] as? String==p.0}) {
          let dx=p.1-(group["x"] as? Double ?? 0),dy=p.2-(group["y"] as? Double ?? 0),members=expandedProcessingSelection([p.0])
          var descendants=Set([p.0])
          for _ in groups.indices {for child in groups where descendants.contains(child["parent"] as? String ?? ""){if let id=child["id"] as? String{descendants.insert(id)}}}
          for i in list.indices where members.contains(list[i]["id"] as? String ?? ""){list[i]["x"]=(list[i]["x"] as? Double ?? 0)+dx;list[i]["y"]=(list[i]["y"] as? Double ?? 0)+dy}
          for i in groups.indices where descendants.contains(groups[i]["id"] as? String ?? ""){groups[i]["x"]=(groups[i]["x"] as? Double ?? 0)+dx;groups[i]["y"]=(groups[i]["y"] as? Double ?? 0)+dy}
        } else if let i=list.firstIndex(where:{$0["id"] as? String==p.0}) {list[i]["x"]=p.1;list[i]["y"]=p.2}
      }
      d["nodes"]=list;d["groups"]=groups
    }
  }

  func positionObjects(_ values:[(String,Double,Double)])->[[String:Any]] {values.map{["node":$0.0,"x":$0.1,"y":$0.2]}}
  func insertionMove(_ ids:[String],edge:Int)->[String:Any]? {
    guard graphID==nil,songConnections.indices.contains(edge),!ids.isEmpty else{return nil}
    let selected=Set(ids.compactMap{songNodePlugin[$0]});guard selected.count==ids.count,
      let owner=buses.first(where:{Set(effectiveInserts($0)).isSuperset(of:selected)})else{return nil}
    let all=effectiveInserts(owner),ordered=all.filter{selected.contains($0)}
    guard let start=all.firstIndex(of:ordered[0]),Array(all[start..<(start+ordered.count)])==ordered else{return nil}
    let action=songConnections[edge],kind=action["kind"] as? String ?? ""
    guard ["insert","output","master-output"].contains(kind),let target=action["source"] as? String else{return nil}
    let before=kind=="insert" ? action["plugin"] as? String:nil
    guard before==nil || !selected.contains(before!)else{return nil}
    return ["plugins":ordered,"target":target,"before":before as Any? ?? NSNull()]
  }
  func insertionDescription(_ ids:[String],edge:Int)->String? {
    guard canvas.edges.indices.contains(edge),!canvas.edges[edge].modulation else{return nil}
    if graphID != nil {guard ids.allSatisfy({id in nodes.contains{$0["id"] as? String==id && $0["kind"] as? String=="plugin"}})else{return nil}}
    else if insertionMove(ids,edge:edge)==nil{return nil}
    return "Release to insert \(ids.count) effect(s) here · old chain is reconnected · one Undo"
  }
  func insertNodes(_ ids:[String],edge:Int,positions:[(String,Double,Double)]) {
    if let graphID {guard definitionEdgeIndices.indices.contains(edge)else{return};mutate("graph.nodes.insert",["graph":graphID,"nodes":ids,"edge":definitionEdgeIndices[edge],"positions":positionObjects(positions)])}
    else if var move=insertionMove(ids,edge:edge){move["positions"]=positionObjects(positions);mutate("mixer.inserts.move",move)}
    else{rebuild();status.stringValue="Choose consecutive effects and drop on an insert or channel-output wire"}
  }
  static func audioPort(_ number:UInt32,output:Bool,catalog:[[String:Any]])->SignalCanvasPort {
    let p=catalog.first{$0["direction"] as? String==(output ? "output":"input") && ($0["index"] as? NSNumber)?.uint32Value==number}
    let name=p?["name"] as? String ?? ""
    let title=name.isEmpty ? (number==0 ? (output ? "Main out":"Main in") : "Aux \(output ? "output":"input") \(number)") : name
    let channels=p?["channels"] as? Int
    let sidechain = !output && (name.lowercased().contains("sidechain") || name.lowercased().contains("side chain") || name.lowercased().contains("detector"))
    let label=title+(channels.map{" · \($0)ch"} ?? "")
    return SignalCanvasPort(number:number,label:label,active:p?["active"] as? Bool ?? true,signal:sidechain ? .sidechain:.audio,channels:channels,unavailable:p?["supported"] as? Bool==false ? "\(title) · \(channels ?? 0) channels: this host currently supports mono/stereo buses":nil)
  }
  func refreshPortChoices() {
    for (popup,nodePicker,field,output) in [(outputChoice,source,outputPort,true),(inputChoice,destination,inputPort,false)] {
      let id=chosen(nodePicker),node=canvas.nodes.first{$0.id==id},ports=(output ? node?.outputs:node?.inputs) ?? []
      popup.removeAllItems()
      for port in ports where !port.modulation {let item=NSMenuItem(title:"\(port.label) [\(port.number)]\(port.active ? "":" · auto-enable")",action:nil,keyEquivalent:"");item.representedObject=port.number;popup.menu?.addItem(item)}
      if let index=popup.itemArray.firstIndex(where:{($0.representedObject as? UInt32)==UInt32(field.stringValue)}) {popup.selectItem(at:index)}
      else if let port=popup.selectedItem?.representedObject as? UInt32{field.stringValue=String(port)}
      popup.isEnabled=popup.numberOfItems>0
    }
  }
  @objc func portChoiceChanged(_ sender:NSPopUpButton) {
    guard let value=sender.selectedItem?.representedObject as? UInt32 else{return}
    (sender===outputChoice ? outputPort:inputPort).stringValue=String(value);hasDraft=true
    if canvas.selectedEdge==nil {inferConnectionKind()}
    else {updateConnection()}
  }
  func inferConnectionKind() {
    guard graphID==nil,let selectedSource=chosen(source),let selectedTarget=chosen(destination)else{return}
    let from=realPort(selectedSource,UInt32(outputPort.stringValue) ?? 0,output:true,modulation:false),to=realPort(selectedTarget,UInt32(inputPort.stringValue) ?? 0,output:false,modulation:false)
    let a=from.node,b=to.node,input=to.number,output=from.number
    let kind=input>0 ? (songNodePlugin[b] != nil ? "Plugin sidechain":"Graph sidechain") : songNodePlugin[b] != nil ? "Main output" : songNodePlugin[a] != nil ? "Plugin auxiliary" : output>0 ? "Graph auxiliary":"Main output"
    connectionKind.selectItem(withTitle:kind);connectionModeChanged()
  }
  func pluginOutputTargets(_ plugin:String,port:Int)->[String] {
    let routes=(mixer["instruments"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? String==plugin && ($0["output"] as? Int ?? 0)==port}
    if routes.isEmpty,port==0,rackPlugins.contains(where:{$0["id"] as? String==plugin && $0["isInstrument"] as? Bool==true}),let master=buses.first(where:{$0["kind"] as? String=="master"})?["id"] as? String{return [master]}
    return routes.compactMap{$0["target"] as? String}.filter{!$0.isEmpty}
  }
  func addPluginOutput(_ node:String,target:String,port:Int) {
    guard let plugin=songNodePlugin[node]else{return}
    var targets=pluginOutputTargets(plugin,port:port);guard !targets.contains(target)else{status.stringValue="This output is already connected";return};targets.append(target)
    mutate("mixer.plugin.route",["plugin":plugin,"output":port,"targets":targets])
  }
  func enableDefinitionPorts(_ d:inout [String:Any]) {
    var list=d["nodes"] as? [[String:Any]] ?? []
    for i in list.indices where list[i]["kind"] as? String=="plugin" {
      guard let id=list[i]["id"] as? String,var recipe=list[i]["plugin"] as? [String:Any]else{continue}
      for (key,endpoint,port) in [("inputs","target","input"),("outputs","source","output")] {
        var values=Set(recipe[key] as? [Int] ?? [])
        for edge in d["audio"] as? [[String:Any]] ?? [] where edge[endpoint] as? String==id{if let n=edge[port] as? Int,n>0{values.insert(n)}}
        recipe[key]=values.sorted()
      };list[i]["plugin"]=recipe
    };d["nodes"]=list
  }
  func loadPortCatalogs() {
    guard let graph=graphID,!hasDraft,!loading,!catalogLoading,onRequest != nil,
      let node=nodes.first(where:{$0["kind"] as? String=="plugin" && portCatalogs[$0["id"] as? String ?? ""]==nil && catalogFailures[$0["id"] as? String ?? ""] != revision}),let id=node["id"] as? String else{return}
    catalogLoading=true;let document=projectionDocument,generation=catalogGeneration,capturedRevision=revision
    requestGraph("graph.plugin.get",["graph":graph,"node":id],document:document){[weak self] response in
      guard let self,self.projectionDocument==document,self.catalogGeneration==generation else{return}
      self.catalogLoading=false
      if let catalog=(response["result"] as? [String:Any])?["data"] as? [String:Any] {
        self.portCatalogs[id]=catalog;self.catalogFailures[id]=nil
        if self.graphID==graph && !self.hasDraft && !self.loading {self.rebuild()}
      }else{
        self.catalogFailures[id]=capturedRevision
        if self.graphID==graph {self.status.stringValue="Plugin ports unavailable · \((response["error"] as? [String:Any])?["message"] as? String ?? "Read failed") · Reload graph to retry"}
      }
      self.loadPortCatalogs()
    }
  }
}
