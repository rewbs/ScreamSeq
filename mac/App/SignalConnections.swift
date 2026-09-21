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
      for p in catalog where p["direction"] as? String==(output ? "output" : "input") && p["active"] as? Bool==true{numbers.insert((p["index"] as? NSNumber)?.uint32Value ?? 0)}
      return numbers.sorted().map{number in let name=catalog.first{$0["direction"] as? String==(output ? "output" : "input") && ($0["index"] as? NSNumber)?.uint32Value==number}?["name"] as? String
        return SignalCanvasPort(number:number,label:name ?? "\(output ? "Out" : "In") \(number)")}
    }
    result.inputs=["plugin","output","follower"].contains(kind) ? ports(output:false) : []
    result.outputs=["plugin","input"].contains(kind) ? ports(output:true) : kind=="output" ? [] : [SignalCanvasPort(label:"Signal",modulation:true)]
    if kind=="plugin"{
      var ids=Set(modulation.filter{$0["target"] as? String==id}.compactMap{($0["parameter"] as? NSNumber)?.uint32Value})
      if let exposed=exposedParameters[id]{ids.insert(exposed)}
      let parameters=portCatalogs[id]?["parameters"] as? [[String:Any]] ?? []
      result.inputs += ids.sorted().map{number in SignalCanvasPort(number:number,label:parameters.first{($0["id"] as? NSNumber)?.uint32Value==number}?["name"] as? String ?? "Param \(number)",modulation:true)}
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
      let audio=definition?["audio"] as? [[String:Any]] ?? [],mod=definition?["modulation"] as? [[String:Any]] ?? []
      if index<audio.count {connectionKind.selectItem(withTitle:"Audio");connectionGain.doubleValue=audio[index]["gain"] as? Double ?? 1}
      else if mod.indices.contains(index-audio.count){let m=mod[index-audio.count];connectionKind.selectItem(withTitle:"Modulation");parameter.stringValue="\(m["parameter"] ?? 0)";minimum.doubleValue=m["minimum"] as? Double ?? 0;maximum.doubleValue=m["maximum"] as? Double ?? 1;base.doubleValue=m["base"] as? Double ?? 0;connectionEnabled.state=(m["enabled"] as? Bool ?? true) ? .on : .off}
    }else{selectSongConnection(index)}
    connectionModeChanged();connection.scrollToVisible(connection.bounds)
    status.stringValue="Selected wire · edit its settings, then Update wire"
  }
  func updateConnection(){
    guard let raw=chosen(connection),let index=Int(raw),canvas.edges.indices.contains(index),let a=chosen(source),let b=chosen(destination)else{status.stringValue="Select a wire first";return}
    guard graphID != nil else{updateSongConnection(index,source:a,target:b);return}
    let audio=definition?["audio"] as? [[String:Any]] ?? [],mods=definition?["modulation"] as? [[String:Any]] ?? []
    if index<audio.count {
      guard connectionKind.titleOfSelectedItem=="Audio",let out=Int(outputPort.stringValue),let input=Int(inputPort.stringValue),let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Use valid audio ports and a gain multiplier";return}
      updateDefinition{d in var edges=audio;edges[index]=["source":a,"target":b,"output":out,"input":input,"gain":gain];d["audio"]=edges}
    }else{
      guard mods.indices.contains(index-audio.count),connectionKind.titleOfSelectedItem=="Modulation",let param=UInt32(parameter.stringValue),let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),let baseline=Double(base.stringValue)else{status.stringValue="Use valid modulation settings";return}
      updateDefinition{d in var edges=mods;edges[index-audio.count]=["source":a,"target":b,"parameter":param,"minimum":lo,"maximum":hi,"base":baseline,"enabled":connectionEnabled.state == .on]
        // Base belongs to the destination parameter; every contributing source shares it.
        for i in edges.indices where edges[i]["target"] as? String==b && (edges[i]["parameter"] as? NSNumber)?.uint32Value==param{edges[i]["base"]=baseline};d["modulation"]=edges}
    }
  }
  func arrange(){
    guard !loading,!canvas.nodes.isEmpty else{return}
    var levels=Dictionary(canvas.nodes.map{($0.id,0)},uniquingKeysWith:{_,last in last})
    for _ in 0..<canvas.nodes.count{var changed=false;for edge in canvas.edges{if let from=levels[edge.source],let to=levels[edge.target],to<from+1{levels[edge.target]=min(canvas.nodes.count,from+1);changed=true}};if !changed{break}}
    var bottoms=[Int:Double](),positions=[String:(Double,Double)]()
    for node in canvas.nodes{let level=levels[node.id] ?? 0,y=bottoms[level] ?? 32;positions[node.id]=(32+Double(level)*235,y);bottoms[level]=y+node.rect.height+44}
    if graphID != nil {updateDefinition{d in var list=d["nodes"] as? [[String:Any]] ?? [];for i in list.indices{if let id=list[i]["id"] as? String,let p=positions[id]{list[i]["x"]=p.0;list[i]["y"]=p.1}};d["nodes"]=list}}
    else{mutate("graph.layout.set",["positions":positions.map{["node":$0.key,"x":$0.value.0,"y":$0.value.1] as [String:Any]}])}
  }
  func selectSongConnection(_ index:Int){
    guard songConnections.indices.contains(index)else{return};let action=songConnections[index]
    let kind=action["kind"] as? String ?? ""
    connectionKind.selectItem(withTitle:["output":"Main output","send":"Send","graph-input":"Graph sidechain","graph-output":"Graph auxiliary","plugin-input":"Plugin sidechain","plugin-output":"Plugin auxiliary"][kind] ?? "Main output")
    if kind=="send",let bus=buses.first(where:{$0["id"] as? String==action["source"] as? String}),let i=action["index"] as? Int,let sends=bus["sends"] as? [[String:Any]],sends.indices.contains(i){connectionGain.doubleValue=sends[i]["gainDB"] as? Double ?? -12}
    if ["graph-input","plugin-input"].contains(kind),let i=action["index"] as? Int{let entries=(kind=="graph-input" ? data["inputs"] : mixer["sidechains"]) as? [[String:Any]] ?? [];if entries.indices.contains(i){connectionGain.doubleValue=entries[i]["gainDB"] as? Double ?? 0;inputPort.integerValue=entries[i]["input"] as? Int ?? 1}}
  }
  func updateSongConnection(_ index:Int,source a:String,target b:String){
    guard songConnections.indices.contains(index)else{return};let action=songConnections[index],from=songNodeBus[a] ?? a,to=songNodeBus[b] ?? b
    switch action["kind"] as? String {
    case "output":mutate("mixer.bus.set",["bus":from,"output":to])
    case "send":
      guard from==action["source"] as? String,let bus=buses.first(where:{$0["id"] as? String==from}),let i=action["index"] as? Int,let gain=Double(connectionGain.stringValue)else{status.stringValue="Remove and reconnect to change a send's source";return}
      var sends=bus["sends"] as? [[String:Any]] ?? [];guard sends.indices.contains(i)else{return};sends[i]["target"]=to;sends[i]["gainDB"]=gain;mutate("mixer.sends.set",["bus":from,"sends":sends])
    case "graph-input":
      guard let i=action["index"] as? Int,let port=Int(inputPort.stringValue),let gain=Double(connectionGain.stringValue)else{return};var list=data["inputs"] as? [[String:Any]] ?? [];guard list.indices.contains(i)else{return};list[i].merge(["source":from,"target":to,"input":port,"gainDB":gain]){_,new in new};mutate("graph.routes.set",["inputs":list])
    case "graph-output":
      guard let i=action["index"] as? Int,let port=Int(outputPort.stringValue)else{return};var list=data["outputs"] as? [[String:Any]] ?? [];guard list.indices.contains(i)else{return};list[i]=["source":from,"target":to,"output":port];mutate("graph.routes.set",["outputs":list])
    case "plugin-output":
      guard let plugin=songNodePlugin[a],plugin==action["plugin"] as? String,let port=Int(outputPort.stringValue),port==action["output"] as? Int else{status.stringValue="Remove and reconnect to change an auxiliary output port";return};mutate("mixer.plugin.route",["plugin":plugin,"target":to,"output":port])
    case "plugin-input":
      guard let i=action["index"] as? Int,let plugin=songNodePlugin[b],let port=Int(inputPort.stringValue),let gain=Double(connectionGain.stringValue)else{return};let all=mixer["sidechains"] as? [[String:Any]] ?? [];guard all.indices.contains(i),all[i]["plugin"] as? String==plugin,all[i]["input"] as? Int==port else{status.stringValue="Remove and reconnect to change the sidechain destination";return}
      var sources=[[String:Any]]();for (j,route) in all.enumerated() where route["plugin"] as? String==plugin && route["input"] as? Int==port{var item=route;item.removeValue(forKey:"plugin");item.removeValue(forKey:"input");if j==i{item["source"]=from;item["gainDB"]=gain};sources.append(item)};mutate("mixer.sidechains.set",["plugin":plugin,"input":port,"sources":sources])
    default:status.stringValue="This wire represents the insert order. Open the subgraph to edit its connections."
    }
  }
}
