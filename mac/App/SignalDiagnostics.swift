import AppKit

struct GraphPortReading {
  let key:String,node:String,name:String,output:Bool,port:UInt32
  let peak:Double,rms:Double,measured:Bool,clipped:Bool,invalid:Bool
  let through:Double,lastSignal:Double,channels:Int,latency:Double?,compensation:Double?
  init?(_ value:[String:Any]) {
    guard let key=value["key"] as? String,let node=value["node"] as? String,let number=(value["port"] as? NSNumber)?.uint32Value else{return nil}
    self.key=key;self.node=node;self.name=value["name"] as? String ?? key;self.port=number;output=value["direction"] as? String=="output"
    let peaks=value["peak"] as? [Double] ?? [],rmsValues=value["rms"] as? [Double] ?? []
    measured=value["measured"] as? Bool==true && peaks.count==2 && rmsValues.count==2 && (peaks+rmsValues).allSatisfy{$0.isFinite && $0>=0}
    peak=measured ? peaks.max() ?? 0:0;rms=measured ? rmsValues.max() ?? 0:0
    clipped=value["clipped"] as? Bool==true;invalid=value["nonFinite"] as? Bool==true
    through=value["through"] as? Double ?? 0;lastSignal=value["lastSignal"] as? Double ?? 0
    channels=value["channels"] as? Int ?? 0;latency=value["processorLatency"] as? Double;compensation=value["compensation"] as? Double
  }
  static func db(_ level:Double)->String{level>0 ? String(format:"%.1f dBFS",20*log10(level)):"−∞ dBFS"}
  func summary(active:Bool)->String {
    guard active else{return "Stopped · \(name)"}
    guard measured else{return "\(name) · Measurement unavailable"}
    return "\(name) · peak \(Self.db(peak)) · RMS \(Self.db(rms)) · \(channels)ch"+(clipped ? " · CLIP":"")+(invalid ? " · Invalid output":"")
  }
}
struct GraphSignalReadings {
  var ports=[GraphPortReading](),active=false,rate=48000.0
  var aliases=[GraphBoundaryPort:GraphRealPort]()
  mutating func update(_ value:[String:Any]) {
    active=value["active"] as? Bool==true
    let sampleRate=value["sampleRate"] as? Double ?? 48000;rate=sampleRate.isFinite && sampleRate>0 ? sampleRate:48000
    ports=(value["ports"] as? [[String:Any]] ?? []).compactMap(GraphPortReading.init)
  }
  func port(_ node:String,output:Bool,number:UInt32=0)->GraphPortReading? {
    let real=aliases[GraphBoundaryPort(node:node,number:number,output:output,modulation:false)] ?? GraphRealPort(node:node,number:number)
    return ports.first{$0.node==real.node && $0.output==output && $0.port==real.number}
  }
  func nodePorts(_ node:String,output:Bool?=nil)->[GraphPortReading] {
    let boundary=aliases.filter{$0.key.node==node && !$0.key.modulation && (output==nil || $0.key.output==output)}
    guard !boundary.isEmpty else{return ports.filter{$0.node==node && (output==nil || $0.output==output)}}
    let keys=Set(boundary.compactMap{port(node,output:$0.key.output,number:$0.key.number)?.key})
    return ports.filter{keys.contains($0.key)}
  }
  // Boundary socket zero is a display index, not necessarily Main. Prefer the
  // unique real Main port; ambiguous parallel groups require a port choice.
  func primaryPort(_ node:String,output:Bool)->GraphPortReading? {
    let values=nodePorts(node,output:output),main=values.filter{$0.port==0}
    return main.count==1 ? main[0] : values.count==1 ? values[0]:nil
  }
}
extension SignalGraphEditor {
  func observedCablePort(_ index:Int)->GraphPortReading? {
    guard graphID==nil,canvas.edges.indices.contains(index),songConnections.indices.contains(index) else{return nil}
    let action=songConnections[index],edge=canvas.edges[index]
    switch action["kind"] as? String {
    case "insert":return signalReadings.port(edge.target,output:false,number:edge.input)
    case "output":return (action["source"] as? String).flatMap{signalReadings.port($0,output:true)}
    case "send":
      guard let source=action["source"] as? String,let i=action["index"] as? Int,let sends=buses.first(where:{$0["id"] as? String==source})?["sends"] as? [[String:Any]],sends.indices.contains(i),sends[i]["preFader"] as? Bool != true else{return nil}
      return signalReadings.port(source,output:true)
    case "plugin-input","graph-input":
      let routes=(action["kind"] as? String=="plugin-input" ? mixer["sidechains"]:data["inputs"]) as? [[String:Any]] ?? []
      guard let i=action["index"] as? Int,routes.indices.contains(i),routes[i]["preFader"] as? Bool != true,let source=routes[i]["source"] as? String else{return nil}
      return signalReadings.port(source,output:true)
    case "plugin-output","master-output":return signalReadings.port(edge.source,output:true,number:edge.output)
    default:return nil
    }
  }
  func openScope(spectrum:Bool) {
    if let port=canvas.scopeTarget(at:nil) {signalScope.show(port:port,spectrum:spectrum);return}
    chooseTarget(title:spectrum ? "Spectrum of signal":"Scope signal",entries:signalReadings.ports.map {
      .init(id:$0.key,title:$0.name,detail:$0.output ? "Audio output":"Audio input",keywords:$0.node)
    }) {[weak self] port in self?.signalScope.show(port:port,spectrum:spectrum)}
  }
  func showSignals(_ value:[String:Any]) {
    var named=value
    let songGroups=data["groups"] as? [[String:Any]] ?? []
    named["ports"]=(value["ports"] as? [[String:Any]] ?? []).map { port -> [String:Any] in
      guard let node=port["node"] as? String,node.hasPrefix("plugin:"),
        let plugin=rackPlugins.first(where:{"plugin:\($0["id"] as? String ?? "")"==node}) else{return port}
      let owner=buses.first{effectiveInserts($0).contains(plugin["id"] as? String ?? "")}
      var labels=[String](),group=songGroups.first{($0["nodes"] as? [String] ?? []).contains(node)},seen=Set<String>()
      while let current=group,let id=current["id"] as? String,seen.insert(id).inserted {
        labels.insert(current["name"] as? String ?? "Group",at:0)
        group=songGroups.first{$0["id"] as? String==current["parent"] as? String}
      }
      if let owner{labels.insert(owner["name"] as? String ?? "Bus",at:0)}
      labels += [plugin["name"] as? String ?? "Processor",port["name"] as? String ?? "Port"]
      var result=port;result["name"]=labels.joined(separator:" › ");return result
    }
    signalReadings.update(named)
    signalReadings.aliases=boundaryPorts
    canvas.signalReadings=graphID==nil ? signalReadings:GraphSignalReadings()
    listenControls.update(value["listen"] as? [String:Any] ?? [:])
    canvas.listeningPort=listenControls.port
    let routing=value["routing"] as? [String:Any] ?? [:]
    let state=routing["state"] as? String ?? "stopped"
    let message=state=="failed" ? "Routing update failed · previous route is still playing. Undo the edit to restore its graph." :
      routing["latencyPending"] as? Bool==true ? "Updating plugin latency…" :
      state=="preparing" ? "Preparing routing · playback continues on the previous route" : ""
    if routingStatus.stringValue != message {routingStatus.stringValue=message;routingStatus.isHidden=message.isEmpty}
  }
  func listenSelected() {
    if let id=canvas.selected,let port=signalReadings.primaryPort(id,output:true),graphID==nil {
      listenControls.select(port.key==listenControls.port ? nil:port.key);return
    }
    let selected=canvas.selected.map{signalReadings.nodePorts($0,output:true)} ?? []
    let choices=graphID==nil && !selected.isEmpty ? selected:signalReadings.ports.filter(\.output)
    chooseTarget(title:"Listen to output",entries:choices.map {
      .init(id:$0.key,title:$0.name,detail:"Temporary monitor · output \($0.port)",keywords:$0.node)
    }) {[weak self] port in self?.listenControls.select(port)}
  }
  func revealObservedNode(_ id:String) {
    rememberGraphView();graphID=nil;graphOrigin=nil;graphTarget=nil;filterID=nil
    processingGroupID=processingGroups.first{($0["nodes"] as? [String] ?? []).contains(id)}?["id"] as? String
    nodeSearch.stringValue="";nodeCategory.selectItem(at:0);selectedID=id;canvas.selectedEdge=nil
    update(data);frameSelection()
  }
  func findOverload() {
    guard !hasDraft else{status.stringValue="Finish the current edit before navigating to an overload";return}
    guard signalReadings.active else{status.stringValue="Stopped · play the song to locate measured overloads";return}
    let hits=signalReadings.ports.filter{$0.measured && ($0.clipped || $0.invalid)}
    guard !hits.isEmpty else{status.stringValue="No overload latched on observed channel and rack ports";return}
    let next=lastOverload.flatMap{key in hits.firstIndex{$0.key==key}}.map{($0+1)%hits.count} ?? 0
    let port=hits[next];lastOverload=port.key
    revealObservedNode(port.node)
    status.stringValue=port.summary(active:true)+" · adjust here, then Clear overload indicators"
  }
  func clearOverloads() {
    let hits=signalReadings.ports.filter{$0.clipped || $0.invalid}.map(\.key)
    guard let onRequest else{return}
    func clear(_ index:Int) {
      guard index<hits.count else{return}
      onRequest("graph.signal.clear",["port":hits[index],"expectedRevision":revision]){[weak self] response in
        guard let self else{return};if let error=response["error"] as? [String:Any]{self.status.stringValue=error["message"] as? String ?? "Could not clear indicator";return};clear(index+1)
      }
    }
    clear(0)
  }
  func traceSilence() {
    guard !hasDraft else{status.stringValue="Finish the current edit before tracing signal";return}
    guard signalReadings.active else{status.stringValue="Stopped · play the song to trace signal";return}
    guard graphID==nil else{status.stringValue="Internal recipe ports are not measured yet. Return to Song to inspect its channel and rack path.";return}
    guard let selectedID else{status.stringValue="Select the channel or processor whose path you want to inspect";return}
    let owner=songNodeBus[selectedID]
    if let owner,let bus=buses.first(where:{$0["id"] as? String==owner}) {
      if bus["mute"] as? Bool==true {
        self.selectedID=owner;canvas.selected=owner;canvas.selectedEdge=nil;inspect();configureConnectionInspector();frameSelection()
        status.stringValue="\(bus["name"] as? String ?? "Channel") is muted · uncheck Mute in its controls here";return
      }
      let inserts=effectiveInserts(bus)
      let auxiliary=(mixer["instruments"] as? [[String:Any]] ?? []).contains{inserts.contains($0["plugin"] as? String ?? "") && !($0["target"] as? String ?? "").isEmpty}
      let graphOutput=(data["outputs"] as? [[String:Any]] ?? []).contains{$0["source"] as? String==owner}
      if bus["kind"] as? String != "master",!auxiliary,!graphOutput,(bus["output"] as? String ?? "").isEmpty,(bus["sends"] as? [[String:Any]] ?? []).allSatisfy({$0["enabled"] as? Bool==false}) {
        status.stringValue="\(bus["name"] as? String ?? "Channel") has no main output or enabled send · drag its output to a destination";return
      }
    }
    var path=Set([selectedID]),changed=true
    path.formUnion(expandedProcessingSelection([selectedID]))
    if let owner,songNodePlugin[selectedID]==nil{for node in ungroupedNodes where songNodeBus[node.id]==owner{path.insert(node.id)}}
    while changed{let old=path;for edge in ungroupedEdges where !edge.modulation && edge.enabled && path.contains(edge.target){path.insert(edge.source)};changed=old != path}
    let candidates=ungroupedNodes.filter{path.contains($0.id)}
    for node in candidates {
      guard songNodePlugin[node.id] != nil,let input=signalReadings.port(node.id,output:false),let output=signalReadings.port(node.id,output:true),input.measured,output.measured else{continue}
      if input.peak>1e-5 && output.peak<=1e-7 {
        revealObservedNode(node.id)
        status.stringValue="\(node.title): input active; output silent · Open interface to inspect the plugin";return
      }
    }
    let readings=signalReadings.ports.filter{path.contains($0.node) && $0.measured}
    let audible=readings.filter{$0.peak>1e-5}
    status.stringValue=readings.isEmpty ? "Measurement unavailable for this path":audible.isEmpty ? "No signal measured on the observed part of this path · check notes and source playback":"Signal reaches \(audible.last?.name ?? "this path") · measured host ports do not diagnose vendor-internal routing"
    canvas.selectNodes(path.intersection(Set(canvas.nodes.map(\.id))),primary:selectedID)
  }
}
