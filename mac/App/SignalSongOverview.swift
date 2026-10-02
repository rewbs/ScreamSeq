import AppKit

extension SignalGraphEditor {
  // Names and duplicate disambiguation depend on document topology, not on the
  // audio meter clock. Bridge the bus collection once per data replacement;
  // repeated casts/filtering here used several milliseconds on every live poll.
  func rebuildBusLabels() {
    let all=buses
    busNameCounts=Dictionary(all.map{($0["name"] as? String ?? "Bus",1)},uniquingKeysWith:+)
    busLabels=Dictionary(all.compactMap{bus -> (String,String)? in
      guard let id=bus["id"] as? String else{return nil}
      return(id,busLabel(bus))
    },uniquingKeysWith:{first,_ in first})
  }
  func busLabel(_ bus:[String:Any])->String {
    let name=bus["name"] as? String ?? "Bus"
    guard (busNameCounts[name] ?? 0)>1 else{return name}
    return (bus["id"] as? String ?? "")+" · "+name
  }
  var rackPlugins:[[String:Any]]{data["plugins"] as? [[String:Any]] ?? []}
  func effectiveInserts(_ bus:[String:Any])->[String]{
    var inserts=bus["inserts"] as? [String] ?? []
    if bus["kind"] as? String=="master"{let assigned=Set(buses.flatMap{$0["inserts"] as? [String] ?? []}).union(detachedEffects);inserts += rackPlugins.filter{$0["isInstrument"] as? Bool != true && !assigned.contains($0["id"] as? String ?? "")}.compactMap{$0["id"] as? String}}
    return inserts
  }
  var detachedChains:[[String:Any]]{mixer["detachedChains"] as? [[String:Any]] ?? []}
  var detachedEffects:Set<String>{Set(mixer["detached"] as? [String] ?? []).union(detachedChains.flatMap{$0["plugins"] as? [String] ?? []})}
  var disconnectedMainInputs:Set<String>{Set(mixer["disconnectedMainInputs"] as? [String] ?? [])}
  func insertChain(_ owner:String)->[String]? {
    if let bus=buses.first(where:{$0["id"] as? String==owner}){return effectiveInserts(bus)}
    return detachedChains.first{$0["id"] as? String==owner}?["plugins"] as? [String]
  }
  func orderedSongProcessors(_ selected:Set<String>)->[String]? {
    guard !selected.isEmpty else{return nil}
    if selected.count==1,let id=selected.first,(mixer["detached"] as? [String] ?? []).contains(id){return [id]}
    let chains=buses.map{effectiveInserts($0)}+detachedChains.map{$0["plugins"] as? [String] ?? []}
    guard let all=chains.first(where:{Set($0).isSuperset(of:selected)}),let start=all.firstIndex(where:{selected.contains($0)})else{return nil}
    let ordered=all.filter{selected.contains($0)}
    return Array(all[start..<(start+ordered.count)])==ordered ? ordered:nil
  }
  func buildSongOverview()->([SignalCanvasNode],[SignalCanvasEdge]){
    songNodeBus=[:];songNodeGraph=[:];songNodePlugin=[:];songConnections=[]
    // Filtered channel paths retain their own provisional layout. Cached default
    // positions from a 16-channel overview must not leave acres of empty space
    // in a three-channel view. Saved, explicitly moved nodes remain authoritative.
    let layoutKey=filterID ?? "all"
    var provisionalPositions=provisionalLayouts[layoutKey] ?? [:]
    let retainedPositions=Set(provisionalPositions.keys)
    var display=[SignalCanvasNode](),edges=[SignalCanvasEdge](),firstStage=[String:String](),lastStage=[String:String](),defaultRows=[String:Double]()
    let assignments=data["assignments"] as? [[String:Any]] ?? [],commands=data["commands"] as? [[String:Any]] ?? []
    let visible=Set(buses.compactMap{$0["id"] as? String})
    var saved=Dictionary((data["layout"] as? [[String:Any]] ?? []).compactMap{p->(String,[String:Any])? in guard let key=p["node"] as? String else{return nil};return(key,p)},uniquingKeysWith:{_,last in last})
    func add(_ id:String,_ title:String,_ detail:String,_ x:Double,_ y:Double,kind:String="audio") {defaultRows[id]=y;display.append(SignalCanvasNode(id:id,title:title,detail:detail,kind:kind,x:saved[id]?["x"] as? Double ?? x,y:saved[id]?["y"] as? Double ?? y))}
    func edge(_ a:String,_ b:String,_ label:String,_ action:[String:Any]=[:],modulation:Bool=false,output:UInt32=0,input:UInt32=0,enabled:Bool=true){edges.append(SignalCanvasEdge(source:a,target:b,label:label,modulation:modulation,output:output,input:input,enabled:enabled));songConnections.append(action)}
    // Master is a final output sink. When it has processors, expose the summing
    // input separately so cables still describe the real processing order.
    for b in buses {
      guard let id=b["id"] as? String else{continue}
      let stages = !effectiveInserts(b).isEmpty || assignments.contains{$0["target"] as? String==id} || commands.contains{$0["target"] as? String==id}
      firstStage[id]=b["kind"] as? String=="master" && stages ? "master-input:\(id)" : id
    }
    var row=0
    for b in buses{guard let id=b["id"] as? String,visible.contains(id)else{continue};let kind=b["kind"] as? String ?? "track",y=Double(30+row*125);row+=1
      let entry=firstStage[id] ?? id
      add(entry,busLabel(b)+(entry==id ? "" : " input"),kind=="track" ? "Samples / channel input" : kind=="master" && entry==id ? (mixer["masterOutputDisconnected"] as? Bool==true ? "Final output disconnected":"Final output") : "Summed \(kind) input",30,y);songNodeBus[entry]=id
      var previous=entry,column=1
      var uses=[(String,String)]()
      for role in ["row","start"]{var seen=Set<String>();for command in commands where command["target"] as? String==id && command["kind"] as? String==role{if let g=command["graph"] as? String,seen.insert(g).inserted{uses.append((g,role=="row" ? "Row" : "Persistent"))}}}
      if let g=assignments.first(where:{$0["target"] as? String==id})?["graph"] as? String{uses.append((g,"Ordinary"))}
      for (graph,role) in uses{let key="graph:\(id):\(role):\(graph)",d=definitions.first{$0["id"] as? String==graph};add(key,"\(d?["number"] as? Int ?? 0) · \(d?["name"] as? String ?? "Subgraph")","\(role) · independent copy",Double(30+column*235),y);display[display.count-1].role=role;column+=1;songNodeBus[key]=id;songNodeGraph[key]=graph;edge(previous,key,role=="Ordinary" ? "" : "When active");previous=key}
      for plugin in effectiveInserts(b){let key="plugin:\(plugin)",p=rackPlugins.first{$0["id"] as? String==plugin};add(key,p?["name"] as? String ?? plugin,p?["bypass"] as? Bool==true ? "Bypassed" : "\(p?["format"] as? String ?? "") insert",Double(30+column*235),y);column+=1;songNodeBus[key]=id;songNodePlugin[key]=plugin;if !disconnectedMainInputs.contains(plugin){edge(previous,key,"",["kind":"insert","plugin":plugin,"source":id])}else{display[display.count-1].detail += " · main disconnected"};previous=key}
      if kind=="master",previous != id {add(id,b["name"] as? String ?? "Master","Final output",Double(30+column*235),y);songNodeBus[id]=id;if mixer["masterOutputDisconnected"] as? Bool != true{edge(previous,id,"Main output",["kind":"master-output","source":id])}else{display[display.count-1].detail="Final output disconnected"};previous=id}
      lastStage[id]=previous
    }
    for b in buses{guard let id=b["id"] as? String,let end=lastStage[id]else{continue}
      if let out=b["output"] as? String,!out.isEmpty,visible.contains(out){edge(end,firstStage[out] ?? out,"Output",["kind":"output","source":id,"target":out])}
      for (i,send) in (b["sends"] as? [[String:Any]] ?? []).enumerated(){if let to=send["target"] as? String,visible.contains(to){edge(end,firstStage[to] ?? to,send["enabled"] as? Bool==false ? "Send · −∞":"Send",["kind":"send","source":id,"index":i],enabled:send["enabled"] as? Bool ?? true)}}
    }
    for chain in detachedChains {
      guard let id=chain["id"] as? String else{continue};var previous:String?
      for (column,plugin) in (chain["plugins"] as? [String] ?? []).enumerated(){let key="plugin:"+plugin,p=rackPlugins.first{$0["id"] as? String==plugin}
        add(key,p?["name"] as? String ?? plugin,"Detached chain"+(disconnectedMainInputs.contains(plugin) ? " · main disconnected":""),Double(30+column*235),Double(30+row*125));songNodePlugin[key]=plugin;songNodeBus[key]=id
        if let previous,!disconnectedMainInputs.contains(plugin){edge(previous,key,"",["kind":"insert","plugin":plugin,"source":id])};previous=key
      };if let previous{lastStage[id]=previous};row+=1
    }
    for p in rackPlugins where (mixer["detached"] as? [String] ?? []).contains(p["id"] as? String ?? "") {
      guard let id=p["id"] as? String else{continue};let key="plugin:"+id
      add(key,p["name"] as? String ?? "Effect","Unconnected · drag into a path",30,Double(30+row*125));row+=1;songNodePlugin[key]=id
    }
    for p in rackPlugins where p["isInstrument"] as? Bool==true {guard let id=p["id"] as? String else{continue};let key="plugin:\(id)";let routes=(mixer["instruments"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? String==id};let master=buses.first{$0["kind"] as? String=="master"}?["id"] as? String ?? ""
      var targets=routes
      if !routes.contains(where:{$0["output"] as? Int==0}){targets.append(["target":master,"output":0])}
      guard filterID==nil || targets.contains(where:{visible.contains($0["target"] as? String ?? "")})else{continue};add(key,p["name"] as? String ?? "Instrument","Instrument \((p["instruments"] as? [Int] ?? []).map(String.init).joined(separator:", "))",30,Double(30+row*125));row+=1;songNodePlugin[key]=id
      for r in targets{if let target=r["target"] as? String,visible.contains(target){edge(key,firstStage[target] ?? target,"Out \(r["output"] as? Int ?? 0)",["kind":"plugin-output","plugin":id,"output":r["output"] ?? 0,"target":target],output:(r["output"] as? NSNumber)?.uint32Value ?? 0)}}
    }
    for (i,r) in (data["inputs"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let b=r["target"] as? String,let from=lastStage[a],visible.contains(b){edge(from,firstStage[b] ?? b,"Graph in \(r["input"] ?? 1)",["kind":"graph-input","index":i],input:(r["input"] as? NSNumber)?.uint32Value ?? 1)}}
    for (i,r) in (data["outputs"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let b=r["target"] as? String,visible.contains(a),visible.contains(b){let stage=display.last{songNodeBus[$0.id]==a && songNodeGraph[$0.id] != nil}?.id ?? a;edge(stage,firstStage[b] ?? b,"Graph out \(r["output"] ?? 1)",["kind":"graph-output","index":i],output:(r["output"] as? NSNumber)?.uint32Value ?? 1)}}
    for (i,r) in (mixer["sidechains"] as? [[String:Any]] ?? []).enumerated(){if let a=r["source"] as? String,let plugin=r["plugin"] as? String,let from=lastStage[a],songNodePlugin["plugin:\(plugin)"] != nil{edge(from,"plugin:\(plugin)",(r["input"] as? Int==0 ? "Main input mix":"Sidechain \(r["input"] ?? 1)"),["kind":"plugin-input","index":i],input:(r["input"] as? NSNumber)?.uint32Value ?? 1)}}
    for (i,r) in (mixer["pluginConnections"] as? [[String:Any]] ?? []).enumerated(){
      guard let a=r["source"] as? String,let b=r["target"] as? String,songNodePlugin["plugin:"+a] != nil,songNodePlugin["plugin:"+b] != nil else{continue}
      edge("plugin:"+a,"plugin:"+b,"Audio contribution",["kind":"plugin-connection","index":i],output:(r["output"] as? NSNumber)?.uint32Value ?? 0,input:(r["input"] as? NSNumber)?.uint32Value ?? 0,enabled:r["enabled"] as? Bool ?? true)
    }
    for r in mixer["instruments"] as? [[String:Any]] ?? []{let plugin=r["plugin"] as? String ?? "";if rackPlugins.first(where:{$0["id"] as? String==plugin})?["isInstrument"] as? Bool != true,let target=r["target"] as? String,visible.contains(target),songNodePlugin["plugin:\(plugin)"] != nil{edge("plugin:\(plugin)",firstStage[target] ?? target,"Aux \(r["output"] ?? 0)",["kind":"plugin-output","plugin":plugin,"output":r["output"] ?? 0,"target":target],output:(r["output"] as? NSNumber)?.uint32Value ?? 0)}}
    for source in songSources {
      guard let id=source["id"] as? String else{continue};let key="source:"+id,kind=source["kind"] as? String ?? "lfo"
      if saved[key]==nil{saved[key]=["x":source["x"] ?? 40,"y":source["y"] ?? 40]}
      add(key,source["name"] as? String ?? kind,kind,source["x"] as? Double ?? 40,source["y"] as? Double ?? 40,kind:"modulation")
      if source["muted"] as? Bool==true{display[display.count-1].detail="Muted · "+kind}
      if kind=="follower" {
        let bus=source["audioBus"] as? String ?? "",plugin=source["audioPlugin"] as? String ?? "",port=source["output"] as? Int ?? 0
        let from = !bus.isEmpty ? lastStage[bus]:!plugin.isEmpty ? "plugin:"+plugin:nil
        if let from,display.contains(where:{$0.id==from}) {
          var action:[String:Any]=["kind":"follower-input","node":id,"output":port,"preFader":source["preFader"] ?? false]
          if !bus.isEmpty{action["source"]=bus}else{action["plugin"]=plugin}
          edge(from,key,source["preFader"] as? Bool==true ? "Follower · pre-fader":"Follower · post-fader",action,output:UInt32(port))
        }
      }
    }
    for m in songModulation {
      guard let source=m["source"] as? String,let plugin=m["plugin"] as? String else{continue};let parameter=(m["parameter"] as? NSNumber)?.uint32Value ?? 0
      let label=songParameter(plugin,parameter)?["name"] as? String ?? "Param \(parameter)"
      edge("source:"+source,"plugin:"+plugin,label,["kind":"modulation","source":source,"plugin":plugin,"parameter":parameter],modulation:true,input:parameter,enabled:m["enabled"] as? Bool ?? true)
      edges[edges.count-1].amount=m["maximum"] as? Double ?? 0;edges[edges.count-1].amountRange = -1...1;edges[edges.count-1].amountUnit=m["quantized"] as? Bool==true ? "stepped depth":"depth"
    }
    for i in edges.indices {
      let action=songConnections[i],kind=action["kind"] as? String ?? ""
      var amount:Double?
      if kind=="send",let bus=buses.first(where:{$0["id"] as? String==action["source"] as? String}),let index=action["index"] as? Int,let sends=bus["sends"] as? [[String:Any]],sends.indices.contains(index){amount=sends[index]["gainDB"] as? Double ?? -96}
      if ["graph-input","plugin-input","plugin-connection"].contains(kind),let index=action["index"] as? Int {
        let routes=(kind=="graph-input" ? data["inputs"]:kind=="plugin-connection" ? mixer["pluginConnections"]:mixer["sidechains"]) as? [[String:Any]] ?? []
        if routes.indices.contains(index){amount=routes[index]["gainDB"] as? Double ?? 0}
      }
      if let amount{edges[i].amount=amount;edges[i].amountRange = -96...12;edges[i].amountUnit="dB"}
    }
    let (instrumentNodes,instrumentEdges)=instrumentOverview(visible:visible);display+=instrumentNodes;edges+=instrumentEdges;songConnections+=instrumentEdges.map{_ in ["kind":"instrument"]}
    for i in display.indices {
      let id=display[i].id
      let plugin=songNodePlugin[id].flatMap{key in rackPlugins.first{$0["id"] as? String==key}}
      display[i].bypassed=plugin?["bypass"] as? Bool ?? false
      if display[i].bypassed {display[i].detail=plugin?["isInstrument"] as? Bool==true ? "Bypassed · silent":"Bypassed · dry through"}
      let ports=plugin?["audioBuses"] as? [[String:Any]] ?? []
      func sockets(output:Bool)->[SignalCanvasPort] {
        let known=ports.filter{$0["direction"] as? String==(output ? "output":"input")}
        var exposed=[UInt32]()
        if let graph=songNodeGraph[id],let definition=definitions.first(where:{$0["id"] as? String==graph}),let endpoint=(definition["nodes"] as? [[String:Any]] ?? []).first(where:{$0["kind"] as? String==(output ? "output":"input")})?["id"] as? String {
          exposed=(definition["audio"] as? [[String:Any]] ?? []).filter{$0[output ? "target":"source"] as? String==endpoint}.compactMap{($0[output ? "input":"output"] as? NSNumber)?.uint32Value}
        }
        let numbers=Set(exposed+edges.filter{!$0.modulation && (output ? $0.source:$0.target)==id}.map{output ? $0.output:$0.input}+known.compactMap{($0["index"] as? NSNumber)?.uint32Value}+[UInt32(0)])
        return numbers.sorted().map{number in Self.audioPort(number,output:output,catalog:known)}
      }
      display[i].inputs=plugin?["isInstrument"] as? Bool==true || id.hasPrefix("instrument:") ? []:sockets(output:false)
      display[i].outputs=sockets(output:true)
      if let source=songSource(id) {display[i].inputs=source["kind"] as? String=="follower" ? [SignalCanvasPort(label:"Audio to follow")]:[];display[i].outputs=[SignalCanvasPort(label:"Signal",modulation:true)]}
      else if let pluginID=songNodePlugin[id] {
        var parameters=Set(songModulation.filter{$0["plugin"] as? String==pluginID}.compactMap{($0["parameter"] as? NSNumber)?.uint32Value})
        if let exposed=exposedParameters[id]{parameters.insert(exposed)}
        // Existing base-automation references are independent of the most
        // recently exposed modulation target. Keep their exact socket alive.
        if let target=provenance.target,target.plugin==pluginID{parameters.insert(target.parameter)}
        display[i].inputs += parameters.sorted().map{number in
          let value=songParameter(pluginID,number),title=value?["name"] as? String ?? "Param \(number)"
          return SignalCanvasPort(number:number,label:title+(value?["canSlide"] as? Bool==false ? " · stepped":""),modulation:true,signal:.parameter,unavailable:value?["writable"] as? Bool==false ? "Read-only parameter":nil)
        }
      }
    }
    appendNoteRouting(nodes:&display,edges:&edges)
    (display,edges)=focusSongNodes(display,edges:edges)
    // Multi-output instruments can be taller than an entire channel strip.
    // Allocate default rows from their real socket heights, retaining saved positions.
    var heights=[Double:Double](),rowY=[Double:Double](),nextY=30.0
    for node in display {let row=defaultRows[node.id] ?? node.y;heights[row]=max(heights[row] ?? 0,node.rect.height)}
    for row in heights.keys.sorted(){rowY[row]=nextY;nextY+=(heights[row] ?? 78)+44}
    for i in display.indices where saved[display[i].id]==nil {display[i].y=rowY[defaultRows[display[i].id] ?? display[i].y] ?? display[i].y}
    var levels=Dictionary(uniqueKeysWithValues:display.map{($0.id,0)})
    for _ in 0..<display.count {var changed=false;for e in edges {if let a=levels[e.source],let b=levels[e.target],b<a+1 {levels[e.target]=min(display.count,a+1);changed=true}};if !changed{break}}
    let master=buses.first{$0["kind"] as? String=="master"}?["id"] as? String
    if let master {levels[master]=(levels.filter{$0.key != master}.values.max() ?? 0)+1}
    for i in display.indices {
      let id=display[i].id
      if saved[id]==nil {
        if let point=provisionalPositions[id] {display[i].x=point.x;display[i].y=point.y}
        else {display[i].x=30+Double(levels[id] ?? 0)*235;provisionalPositions[id]=NSPoint(x:display[i].x,y:display[i].y)}
      }
      if id==master {display[i].outputs=[]}
    }
    // A newly inserted processor may have a saved Add position beyond the
    // cached default Master. Only the unsaved output sink follows new upstream
    // geometry; explicitly positioned cards are never moved by this projection.
    if let master, saved[master]==nil, let index=display.firstIndex(where:{$0.id==master}) {
      var upstream=Set([master])
      for _ in display.indices {let previous=upstream;for edge in edges where !edge.modulation && edge.output != Self.notePort && upstream.contains(edge.target){upstream.insert(edge.source)};if previous==upstream{break}}
      let right=display.filter{upstream.contains($0.id) && $0.id != master}.map{$0.rect.maxX}.max() ?? 0
      if display[index].x<right+55 {display[index].x=right+55;provisionalPositions[master]=NSPoint(x:display[index].x,y:display[index].y)}
    }
    // Revealing another branch must not drop its new default card onto a
    // musician-positioned return, or move the path they were already reading.
    // Reserve saved and previously displayed positions first; only fresh
    // provisional cards may move. These positions never enter song metadata.
    var reserved=display.filter{saved[$0.id] != nil || retainedPositions.contains($0.id)}.map(\.rect)
    for index in display.indices where saved[display[index].id]==nil && !retainedPositions.contains(display[index].id) {
      for _ in 0...display.count {
        let collisions=reserved.filter{$0.insetBy(dx:-16,dy:-16).intersects(display[index].rect)}
        guard let bottom=collisions.map(\.maxY).max() else{break}
        display[index].y=max(display[index].y,bottom+24)
      }
      reserved.append(display[index].rect)
      provisionalPositions[display[index].id]=NSPoint(x:display[index].x,y:display[index].y)
    }
    provisionalLayouts[layoutKey]=provisionalPositions
    return(display,edges)
  }
  func openSongNode(_ id:String){
    if id.hasPrefix("instrument:"),let instrument=sampleInstruments.first(where:{"instrument:\($0["id"] as? String ?? "")"==id}),let stable=instrument["id"] as? String {
      if let graph=(data["instrumentAssignments"] as? [[String:Any]] ?? []).first(where:{$0["target"] as? String==stable})?["graph"] as? String {
        navigate(graph:graph,origin:"I\(instrument["index"] ?? 0) · \(instrument["name"] ?? "Instrument")",target:filterID)
      }else{status.stringValue="Choose this instrument’s processing graph in the inspector.";selectedID=id;inspect()}
      return
    }
    if let graph=songNodeGraph[id] {
      let bus=songNodeBus[id],name=buses.first{$0["id"] as? String==bus}?["name"] as? String
      let role=instrumentForSongNode(id).map{"I\($0["index"] ?? 0) · \($0["name"] as? String ?? "Instrument")"} ?? id.split(separator:":").dropFirst(2).first.map(String.init)
      navigate(graph:graph,origin:[name,role].compactMap{$0}.joined(separator:" › "),target:bus)
    }else if let plugin=songNodePlugin[id]{onPlugin?(plugin)}else if let bus=songNodeBus[id]{onBus?(bus)}
  }
  func connectSong(_ a:String,_ b:String){
    let mode=connectionKind.titleOfSelectedItem ?? ""
    if mode=="Modulation" || mode=="Follower input" {let modulation=mode=="Modulation",out=UInt32(outputPort.stringValue) ?? 0,input=UInt32(parameter.stringValue) ?? 0;let from=realPort(a,out,output:true,modulation:modulation),to=realPort(b,input,output:false,modulation:modulation);_ = connectSongControl(from.node,to.node,out:from.number,input:to.number,modulation:modulation);return}

    guard let rawOutput=UInt32(outputPort.stringValue),let rawInput=UInt32(inputPort.stringValue)else{status.stringValue="Choose valid audio ports";return}
    let sourcePort=realPort(a,rawOutput,output:true,modulation:false),targetPort=realPort(b,rawInput,output:false,modulation:false)
    let a=sourcePort.node,b=targetPort.node,output=Int(sourcePort.number),input=Int(targetPort.number)
    if a.hasPrefix("instrument:") || a.hasPrefix("instrument-graph:") || b.hasPrefix("instrument:") || b.hasPrefix("instrument-graph:"){status.stringValue="Choose an instrument graph with Assign; its output follows the note’s channel.";return}
    let from=songNodeBus[a] ?? a,to=songNodeBus[b] ?? b
    guard let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Enter a finite gain in dB";return}
    let kind=connectionKind.titleOfSelectedItem ?? "Main output"
    if kind=="Direct plugin audio" || (songNodePlugin[a] != nil && songNodePlugin[b] != nil && ["Plugin sidechain","Mix into main"].contains(kind)) {connectSongPlugins(a,b,output:UInt32(output),input:UInt32(kind=="Mix into main" ? 0:input),gain:gain,enabled:connectionEnabled.state == .on);return}
    if ["Plugin sidechain","Mix into main","Graph sidechain"].contains(kind) {
      guard validateBusInputSource(a,port:output)else{return}
    }
    switch kind {
    case "Main output":if let move=insertMove(a,b){mutate("mixer.inserts.move",move)}else if songNodePlugin[a] != nil {addPluginOutput(a,target:to,port:0)}else if let bus=buses.first(where:{$0["id"] as? String==from}),let current=bus["output"] as? String,!current.isEmpty {
      if current==to {status.stringValue="This output is already connected";return};var sends=bus["sends"] as? [[String:Any]] ?? [];if sends.contains(where:{$0["target"] as? String==to}){status.stringValue="This send is already connected";return};sends.append(["target":to,"gainDB":0]);mutate("mixer.sends.set",["bus":from,"sends":sends])
    }else{mutate("mixer.bus.set",["bus":from,"output":to])}
    case "Send":guard let bus=buses.first(where:{$0["id"] as? String==from})else{return};var sends=bus["sends"] as? [[String:Any]] ?? [];sends.append(["target":to,"gainDB":gain]);mutate("mixer.sends.set",["bus":from,"sends":sends])
    case "Graph sidechain":let port=input;var routes=data["inputs"] as? [[String:Any]] ?? [];routes.append(["source":from,"target":to,"input":port,"gainDB":gain]);mutate("graph.routes.set",["inputs":routes])
    case "Graph auxiliary":let port=output;var routes=data["outputs"] as? [[String:Any]] ?? [];routes.append(["source":from,"target":to,"output":port]);mutate("graph.routes.set",["outputs":routes])
    case "Plugin sidechain","Mix into main":guard let plugin=songNodePlugin[b],case let port=connectionKind.titleOfSelectedItem=="Mix into main" ? 0:input else{status.stringValue="Select a plugin destination and auxiliary input";return};var routes=(mixer["sidechains"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? String==plugin && $0["input"] as? Int==port}.map{r->[String:Any] in var v=r;v.removeValue(forKey:"plugin");v.removeValue(forKey:"input");return v};guard !routes.contains(where:{$0["source"] as? String==from})else{status.stringValue="This source is already connected to that input";return};routes.append(["source":from,"gainDB":gain]);mutate("mixer.sidechains.set",["plugin":plugin,"input":port,"sources":routes])
    case "Plugin auxiliary":let port=output;addPluginOutput(a,target:to,port:port)
    default:break
    }
  }
  func disconnectSong(_ index:Int){if !disconnectNoteRoute(index){cutConnections([index])}}
}
