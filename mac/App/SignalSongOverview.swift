import AppKit

extension SignalGraphEditor {
  var rackPlugins:[[String:Any]]{data["plugins"] as? [[String:Any]] ?? []}
  func effectiveInserts(_ bus:[String:Any])->[String]{
    var inserts=bus["inserts"] as? [String] ?? []
    if bus["kind"] as? String=="master"{let assigned=Set(buses.flatMap{$0["inserts"] as? [String] ?? []});inserts += rackPlugins.filter{$0["isInstrument"] as? Bool != true && !assigned.contains($0["id"] as? String ?? "")}.compactMap{$0["id"] as? String}}
    return inserts
  }
  func buildSongOverview()->([SignalCanvasNode],[SignalCanvasEdge]){
    songNodeBus=[:];songNodeGraph=[:];songNodePlugin=[:];songConnections=[]
    var display=[SignalCanvasNode](),edges=[SignalCanvasEdge](),firstStage=[String:String](),lastStage=[String:String]()
    let assignments=data["assignments"] as? [[String:Any]] ?? [],commands=data["commands"] as? [[String:Any]] ?? []
    var visible=Set(buses.compactMap{$0["id"] as? String})
    if let filterID {
      // Follow output paths, then recursively include their audio dependencies.
      // Do not pull in unrelated sibling tracks solely through the master.
      visible=[filterID];var changed=true
      while changed{let old=visible
        for b in buses{guard let id=b["id"] as? String else{continue};if visible.contains(id){if let out=b["output"] as? String,!out.isEmpty{visible.insert(out)};for send in b["sends"] as? [[String:Any]] ?? []{if let target=send["target"] as? String{visible.insert(target)}}}}
        for route in data["inputs"] as? [[String:Any]] ?? []{if let target=route["target"] as? String,visible.contains(target),let source=route["source"] as? String{visible.insert(source)}}
        for route in data["outputs"] as? [[String:Any]] ?? []{if let source=route["source"] as? String,visible.contains(source),let target=route["target"] as? String{visible.insert(target)}}
        for side in mixer["sidechains"] as? [[String:Any]] ?? []{let plugin=side["plugin"] as? String ?? "";if buses.contains(where:{visible.contains($0["id"] as? String ?? "") && effectiveInserts($0).contains(plugin)}),let source=side["source"] as? String{visible.insert(source)}}
        for target in buses where visible.contains(target["id"] as? String ?? "") && target["kind"] as? String != "track" {
          guard let targetID=target["id"] as? String,target["kind"] as? String != "master" || targetID==filterID else{continue}
          for source in buses where source["output"] as? String==targetID || (source["sends"] as? [[String:Any]] ?? []).contains(where:{$0["target"] as? String==targetID}){if let id=source["id"] as? String{visible.insert(id)}}
          for route in data["outputs"] as? [[String:Any]] ?? [] where route["target"] as? String==targetID{if let source=route["source"] as? String{visible.insert(source)}}
          for route in mixer["instruments"] as? [[String:Any]] ?? [] where route["target"] as? String==targetID{let plugin=route["plugin"] as? String ?? "";for source in buses where effectiveInserts(source).contains(plugin){if let id=source["id"] as? String{visible.insert(id)}}}
        }
        changed=old != visible
      }
    }
    let saved=Dictionary((data["layout"] as? [[String:Any]] ?? []).compactMap{p->(String,[String:Any])? in guard let key=p["node"] as? String else{return nil};return(key,p)},uniquingKeysWith:{_,last in last})
    func add(_ id:String,_ title:String,_ detail:String,_ x:Double,_ y:Double,kind:String="audio") {display.append(SignalCanvasNode(id:id,title:title,detail:detail,kind:kind,x:saved[id]?["x"] as? Double ?? x,y:saved[id]?["y"] as? Double ?? y))}
    func edge(_ a:String,_ b:String,_ label:String,_ action:[String:Any]=[:],modulation:Bool=false){edges.append(SignalCanvasEdge(source:a,target:b,label:label,modulation:modulation));songConnections.append(action)}
    var row=0
    for b in buses{guard let id=b["id"] as? String,visible.contains(id)else{continue};let kind=b["kind"] as? String ?? "track",y=Double(30+row*125);row+=1
      add(id,b["name"] as? String ?? "Bus",kind=="track" ? "Samples / channel input" : "Summed \(kind) input",30,y);songNodeBus[id]=id
      var previous=id,column=1;firstStage[id]=id
      var uses=[(String,String)]()
      for role in ["row","start"]{var seen=Set<String>();for command in commands where command["target"] as? String==id && command["kind"] as? String==role{if let g=command["graph"] as? String,seen.insert(g).inserted{uses.append((g,role=="row" ? "Row" : "Persistent"))}}}
      if let g=assignments.first(where:{$0["target"] as? String==id})?["graph"] as? String{uses.append((g,"Ordinary"))}
      for (graph,role) in uses{let key="graph:\(id):\(role):\(graph)",d=definitions.first{$0["id"] as? String==graph};add(key,"\(d?["number"] as? Int ?? 0) · \(d?["name"] as? String ?? "Subgraph")","\(role) · independent copy",Double(30+column*235),y);column+=1;songNodeBus[key]=id;songNodeGraph[key]=graph;edge(previous,key,role=="Ordinary" ? "" : "When active");previous=key}
      for plugin in effectiveInserts(b){let key="plugin:\(plugin)",p=rackPlugins.first{$0["id"] as? String==plugin};add(key,p?["name"] as? String ?? plugin,p?["bypass"] as? Bool==true ? "Bypassed" : "\(p?["format"] as? String ?? "") insert",Double(30+column*235),y);column+=1;songNodeBus[key]=id;songNodePlugin[key]=plugin;edge(previous,key,"");previous=key}
      lastStage[id]=previous
    }
    for b in buses{guard let id=b["id"] as? String,let end=lastStage[id]else{continue}
      if let out=b["output"] as? String,!out.isEmpty,visible.contains(out){edge(end,out,"Output",["kind":"output","source":id,"target":out])}
      for (i,send) in (b["sends"] as? [[String:Any]] ?? []).enumerated(){if let to=send["target"] as? String,visible.contains(to){edge(end,to,"Send",["kind":"send","source":id,"index":i])}}
    }
    for p in rackPlugins where p["isInstrument"] as? Bool==true {guard let id=p["id"] as? String else{continue};let key="plugin:\(id)";let routes=(mixer["instruments"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? String==id};let master=buses.first{$0["kind"] as? String=="master"}?["id"] as? String ?? ""
      let targets=routes.isEmpty ? [["target":master,"output":0] as [String:Any]] : routes
      guard targets.contains(where:{visible.contains($0["target"] as? String ?? "")})else{continue};add(key,p["name"] as? String ?? "Instrument","Instrument \((p["instruments"] as? [Int] ?? []).map(String.init).joined(separator:", "))",30,Double(30+row*125));row+=1;songNodePlugin[key]=id
      for r in targets{if let target=r["target"] as? String,visible.contains(target){edge(key,target,"Out \(r["output"] as? Int ?? 0)",["kind":"plugin-output","plugin":id,"output":r["output"] ?? 0])}}
    }
    for (i,r) in (data["inputs"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let b=r["target"] as? String,let from=lastStage[a],visible.contains(b){edge(from,b,"Graph in \(r["input"] ?? 1)",["kind":"graph-input","index":i])}}
    for (i,r) in (data["outputs"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let b=r["target"] as? String,visible.contains(a),visible.contains(b){let stage=display.last{songNodeBus[$0.id]==a && songNodeGraph[$0.id] != nil}?.id ?? a;edge(stage,b,"Graph out \(r["output"] ?? 1)",["kind":"graph-output","index":i])}}
    for (i,r) in (mixer["sidechains"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let plugin=r["plugin"] as? String,let from=lastStage[a],songNodePlugin["plugin:\(plugin)"] != nil{edge(from,"plugin:\(plugin)","Sidechain \(r["input"] ?? 1)",["kind":"plugin-input","index":i])}}
    for r in mixer["instruments"] as? [[String:Any]] ?? []{let plugin=r["plugin"] as? String ?? "";if rackPlugins.first(where:{$0["id"] as? String==plugin})?["isInstrument"] as? Bool != true,let target=r["target"] as? String,visible.contains(target),songNodePlugin["plugin:\(plugin)"] != nil{edge("plugin:\(plugin)",target,"Aux \(r["output"] ?? 0)",["kind":"plugin-output","plugin":plugin,"output":r["output"] ?? 0])}}
    return(display,edges)
  }
  func openSongNode(_ id:String){if let graph=songNodeGraph[id]{graphID=graph;selectedID=nil;update(data);fit()}else if let plugin=songNodePlugin[id]{onPlugin?(plugin)}else if let bus=songNodeBus[id]{onBus?(bus)}}
  func connectSong(_ a:String,_ b:String){
    let from=songNodeBus[a] ?? a,to=songNodeBus[b] ?? b
    switch connectionKind.titleOfSelectedItem ?? "Main output" {
    case "Main output":mutate("mixer.bus.set",["bus":from,"output":to])
    case "Send":guard let bus=buses.first(where:{$0["id"] as? String==from})else{return};var sends=bus["sends"] as? [[String:Any]] ?? [];sends.append(["target":to,"gainDB":0]);mutate("mixer.sends.set",["bus":from,"sends":sends])
    case "Graph sidechain":guard let port=Int(inputPort.stringValue)else{return};var routes=data["inputs"] as? [[String:Any]] ?? [];routes.append(["source":from,"target":to,"input":port]);mutate("graph.routes.set",["inputs":routes])
    case "Graph auxiliary":guard let port=Int(outputPort.stringValue)else{return};var routes=data["outputs"] as? [[String:Any]] ?? [];routes.removeAll{$0["source"] as? String==from && $0["output"] as? Int==port};routes.append(["source":from,"target":to,"output":port]);mutate("graph.routes.set",["outputs":routes])
    case "Plugin sidechain":guard let plugin=songNodePlugin[b],let port=Int(inputPort.stringValue)else{status.stringValue="Select a plugin destination and auxiliary input";return};var routes=(mixer["sidechains"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? String==plugin && $0["input"] as? Int==port}.map{r->[String:Any] in var v=r;v.removeValue(forKey:"plugin");v.removeValue(forKey:"input");return v};routes.append(["source":from]);mutate("mixer.sidechains.set",["plugin":plugin,"input":port,"sources":routes])
    case "Plugin auxiliary":guard let plugin=songNodePlugin[a],let port=Int(outputPort.stringValue)else{status.stringValue="Select a plugin source and output";return};mutate("mixer.plugin.route",["plugin":plugin,"target":to,"output":port])
    default:break
    }
  }
  func disconnectSong(_ index:Int){guard songConnections.indices.contains(index)else{return};let item=songConnections[index]
    switch item["kind"] as? String {
    case "send":guard let source=item["source"] as? String,let i=item["index"] as? Int,let bus=buses.first(where:{$0["id"] as? String==source})else{return};var sends=bus["sends"] as? [[String:Any]] ?? [];sends.remove(at:i);mutate("mixer.sends.set",["bus":source,"sends":sends])
    case "graph-input","graph-output":let key=item["kind"] as? String=="graph-input" ? "inputs" : "outputs";var routes=data[key] as? [[String:Any]] ?? [];guard let i=item["index"] as? Int,routes.indices.contains(i)else{return};routes.remove(at:i);mutate("graph.routes.set",[key:routes])
    case "plugin-input":var routes=mixer["sidechains"] as? [[String:Any]] ?? [];guard let i=item["index"] as? Int,routes.indices.contains(i)else{return};let removed=routes.remove(at:i);let plugin=removed["plugin"] as? String ?? "",port=removed["input"] as? Int ?? 1;let sources=routes.filter{$0["plugin"] as? String==plugin && $0["input"] as? Int==port}.map{r->[String:Any] in var v=r;v.removeValue(forKey:"plugin");v.removeValue(forKey:"input");return v};mutate("mixer.sidechains.set",["plugin":plugin,"input":port,"sources":sources])
    case "plugin-output":mutate("mixer.plugin.route",["plugin":item["plugin"] ?? "","output":item["output"] ?? 0,"target":NSNull()])
    default:status.stringValue="Reconnect the channel output to another bus; internal chains are edited in their subgraph."
    }
  }
}
