import AppKit

// A route is a contribution to a destination, distinct from either endpoint's
// aggregate port. Its opaque observation key comes from the adopted audio plan.
struct GraphSignalRouteID:Hashable {
  let kind:String,source:String,target:String,plugin:String,input:UInt32,output:UInt32
  init?(_ value:[String:Any]) {
    guard let kind=value["kind"] as? String else{return nil}
    self.kind=kind;source=value["source"] as? String ?? "";target=value["target"] as? String ?? "";plugin=value["plugin"] as? String ?? ""
    func number(_ key:String)->UInt32? {
      guard let raw=value[key] else{return 0}
      guard let n=raw as? NSNumber,n.doubleValue.isFinite,n.doubleValue>=0,n.doubleValue<=Double(UInt32.max),n.doubleValue.rounded()==n.doubleValue else{return nil}
      return n.uint32Value
    }
    guard let input=number("input"),let output=number("output") else{return nil};self.input=input;self.output=output
    switch kind {
    case "output","send","graph-input","graph-output","graph-audio","graph-modulation","plugin-connection":guard !source.isEmpty && !target.isEmpty else{return nil}
    case "plugin-input","insert":guard !source.isEmpty && !plugin.isEmpty else{return nil}
    case "plugin-output":guard !plugin.isEmpty && !target.isEmpty else{return nil}
    case "master-output":guard !source.isEmpty else{return nil}
    default:return nil
    }
  }
}
struct GraphSignalRoute {
  let identity:GraphSignalRouteID,tap:String,preFader:Bool,gainDB:Double?
  init?(_ value:[String:Any]) {
    guard let identity=GraphSignalRouteID(value),let tap=value["tap"] as? String,["post-gain","main-path","pre-master-fader","contribution"].contains(tap) else{return nil}
    self.identity=identity;self.tap=tap;preFader=value["preFader"] as? Bool ?? false
    gainDB=(value["gainDB"] as? Double).flatMap{$0.isFinite ? $0:nil}
  }
  var detail:String {
    let busTap=["output","send","plugin-input","graph-input"].contains(identity.kind)
    let point=tap=="main-path" ? "serial contribution before input summing":tap=="pre-master-fader" ? "before Main fader":"after route gain"+(busTap ? " · "+(preFader ? "pre-fader":"post-fader"):"")
    return "Adopted route · "+point+(tap=="post-gain" ? " · "+(gainDB.map{String(format:"%+.1f dB",$0)} ?? "silent gain"):"")
  }
}

struct GraphNoteGateReading {
  let held:Bool,on:UInt64,off:UInt64,retrigger:UInt64
  init?(_ data:[String:Any]) {
    guard data["scope"] as? String=="aggregate-envelope-gate",let held=data["held"] as? Bool,
          let on=data["on"] as? NSNumber,let off=data["off"] as? NSNumber,let retrigger=data["retrigger"] as? NSNumber else{return nil}
    self.held=held;self.on=on.uint64Value;self.off=off.uint64Value;self.retrigger=retrigger.uint64Value
  }
  var summary:String {"Gate \(held ? "held":"released") · \(on) opens / \(off) closes / \(retrigger) retriggers"}
}
struct GraphPortReading {
  let noteGate:GraphNoteGateReading?
  let key:String,node:String,name:String,output:Bool,port:UInt32
  let kind:String,value:Double?
  let route:GraphSignalRoute?
  let peak:Double,rms:Double,measured:Bool,clipped:Bool,invalid:Bool,available:Bool,fresh:Bool
  let through:Double,lastSignal:Double,channels:Int,latency:Double?,compensation:Double?
  init?(_ value:[String:Any]) {
    guard let key=value["key"] as? String,let node=value["node"] as? String,let number=(value["port"] as? NSNumber)?.uint32Value else{return nil}
    if let descriptor=value["route"] {
      guard let descriptor=descriptor as? [String:Any],let parsed=GraphSignalRoute(descriptor) else{return nil};route=parsed
    }else{route=nil}
    kind=value["kind"] as? String ?? "audio";self.value=(value["value"] as? Double).flatMap{$0.isFinite ? $0:nil}
    noteGate=(value["noteGate"] as? [String:Any]).flatMap(GraphNoteGateReading.init)
    self.key=key;self.node=node;self.name=value["name"] as? String ?? key;self.port=number;output=value["direction"] as? String=="output"
    available=value["available"] as? Bool ?? true;fresh=value["fresh"] as? Bool ?? true
    let peaks=value["peak"] as? [Double] ?? [],rmsValues=value["rms"] as? [Double] ?? []
    measured=available && fresh && value["measured"] as? Bool==true && peaks.count==2 && rmsValues.count==2 && (peaks+rmsValues).allSatisfy{$0.isFinite && $0>=0}
    peak=measured ? peaks.max() ?? 0:0;rms=measured ? rmsValues.max() ?? 0:0
    clipped=value["clipped"] as? Bool==true;invalid=value["nonFinite"] as? Bool==true
    through=value["through"] as? Double ?? 0;lastSignal=value["lastSignal"] as? Double ?? 0
    channels=value["channels"] as? Int ?? 0;latency=value["processorLatency"] as? Double;compensation=(value["compensation"] as? Double).flatMap{$0.isFinite && $0>=0 ? $0:nil}
  }
  static func db(_ level:Double)->String{level>0 ? String(format:"%.1f dBFS",20*log10(level)):"−∞ dBFS"}
  func summary(active:Bool)->String {
    guard active else{return "Stopped · \(name)"}
    guard available else{return "\(name) · Port unavailable in current route"}
    guard fresh else{return "\(name) · Waiting for current-route measurement"}
    guard measured else{return "\(name) · Measurement unavailable"}
    if kind=="control" {return name+" · "+(value.map{String(format:"%.5g",$0)} ?? "Value unavailable")+(noteGate.map{" · "+$0.summary+" (envelope gate)"} ?? "")+" · exact selected copy at frame \(String(format:"%.0f",through))"}
    let routeDetail=route.map{" · "+$0.detail+(compensation.map{String(format:" · %.0f frames route delay",$0)} ?? "")} ?? ""
    return "\(name) · peak \(Self.db(peak)) · RMS \(Self.db(rms)) · \(channels)ch"+routeDetail+(clipped ? " · over 0 dBFS (latched)":"")+(invalid ? " · Invalid output":"")
  }
}
struct GraphSignalReadings {
  private(set) var ports=[GraphPortReading]()
  var active=false,rate=48000.0
  var aliases=[GraphBoundaryPort:GraphRealPort]()
  private var portIndices=[GraphBoundaryPort:Int](),nodeIndices=[String:[Int]](),routeIndices=[GraphSignalRouteID:Int]()
  mutating func update(_ value:[String:Any]) {
    active=value["active"] as? Bool==true
    let sampleRate=value["sampleRate"] as? Double ?? 48000;rate=sampleRate.isFinite && sampleRate>0 ? sampleRate:48000
    ports=(value["ports"] as? [[String:Any]] ?? []).compactMap(GraphPortReading.init)
    portIndices.removeAll(keepingCapacity:true);nodeIndices.removeAll(keepingCapacity:true);routeIndices.removeAll(keepingCapacity:true)
    for (index,port) in ports.enumerated(){
      if let route=port.route {
        if port.available{routeIndices[route.identity]=routeIndices[route.identity]==nil ? index:-1}
        continue
      }
      let key=GraphBoundaryPort(node:port.node,number:port.port,output:port.output,modulation:port.kind=="control")
      if portIndices[key]==nil{portIndices[key]=index}
      nodeIndices[port.node,default:[]].append(index)
    }
  }
  func route(_ identity:GraphSignalRouteID)->GraphPortReading? {
    guard let index=routeIndices[identity],index>=0 else{return nil};return ports[index]
  }
  func port(_ node:String,output:Bool,number:UInt32=0,modulation:Bool=false)->GraphPortReading? {
    let real=aliases[GraphBoundaryPort(node:node,number:number,output:output,modulation:modulation)] ?? GraphRealPort(node:node,number:number)
    return portIndices[GraphBoundaryPort(node:real.node,number:real.number,output:output,modulation:modulation)].map{ports[$0]}
  }
  func nodePorts(_ node:String,output:Bool?=nil)->[GraphPortReading] {
    let boundary=aliases.filter{$0.key.node==node && !$0.key.modulation && (output==nil || $0.key.output==output)}
    guard !boundary.isEmpty else{return (nodeIndices[node] ?? []).compactMap{index in let port=ports[index];return output==nil || port.output==output ? port:nil}}
    let keys=Set(boundary.compactMap{port(node,output:$0.key.output,number:$0.key.number)?.key})
    return ports.filter{keys.contains($0.key)}
  }
  // Boundary socket zero is a display index, not necessarily Main. Prefer the
  // unique real Main port; ambiguous parallel groups require a port choice.
  func primaryPort(_ node:String,output:Bool)->GraphPortReading? {
    let values=nodePorts(node,output:output).filter{$0.kind=="audio"},main=values.filter{$0.port==0}
    return main.count==1 ? main[0] : values.count==1 ? values[0]:nil
  }
}
extension SignalGraphEditor {
  // Topology labels change with graph data, not with every audio meter sample.
  // Retain the first owner/group semantics used by the displayed song graph.
  func rebuildSignalNames() {
    let plugins=rackPlugins,allBuses=buses,groups=data["groups"] as? [[String:Any]] ?? []
    var owners=[String:String]()
    for bus in allBuses {for plugin in effectiveInserts(bus) where owners[plugin]==nil {owners[plugin]=bus["name"] as? String ?? "Bus"}}
    signalNamePrefixes=[:];signalPortNames=[:]
    for plugin in plugins {
      guard let id=plugin["id"] as? String else{continue};let node="plugin:"+id
      var labels=[String](),group=groups.first{($0["nodes"] as? [String] ?? []).contains(node)},seen=Set<String>()
      while let current=group,let id=current["id"] as? String,seen.insert(id).inserted {
        labels.insert(current["name"] as? String ?? "Group",at:0)
        group=groups.first{$0["id"] as? String==current["parent"] as? String}
      }
      if let owner=owners[id]{labels.insert(owner,at:0)}
      labels.append(plugin["name"] as? String ?? "Processor");signalNamePrefixes[node]=labels
    }
  }
  func observedCablePort(_ index:Int)->GraphPortReading? {
    guard canvas.edges.indices.contains(index),canvas.edges[index].enabled else{return nil}
    if graphID != nil {
      guard definitionEdgeIndices.indices.contains(index) else{return nil};let original=definitionEdgeIndices[index]
      let audio=definition?["audio"] as? [[String:Any]] ?? [],modulation=definition?["modulation"] as? [[String:Any]] ?? []
      let isAudio=original<audio.count
      guard isAudio || modulation.indices.contains(original-audio.count) else{return nil}
      var descriptor=isAudio ? audio[original]:modulation[original-audio.count]
      descriptor["kind"]=isAudio ? "graph-audio":"graph-modulation"
      if !isAudio {descriptor["input"]=descriptor["parameter"];descriptor["output"]=0}
      return GraphSignalRouteID(descriptor).flatMap{signalReadings.route($0)}
    }
    guard !canvas.edges[index].modulation,songConnections.indices.contains(index) else{return nil}
    let action=songConnections[index],kind=action["kind"] as? String ?? ""
    var descriptor=action
    switch kind {
    case "send":
      guard let source=action["source"] as? String,let i=action["index"] as? Int,let sends=buses.first(where:{$0["id"] as? String==source})?["sends"] as? [[String:Any]],sends.indices.contains(i),sends[i]["enabled"] as? Bool != false else{return nil}
      descriptor["target"]=sends[i]["target"]
    case "plugin-connection":
      guard let i=action["index"] as? Int,let all=mixer["pluginConnections"] as? [[String:Any]],all.indices.contains(i),all[i]["enabled"] as? Bool != false else{return nil}
      descriptor=all[i];descriptor["kind"]=kind;descriptor["source"]="plugin:"+(all[i]["source"] as? String ?? "");descriptor["target"]="plugin:"+(all[i]["target"] as? String ?? "")
    case "plugin-input","graph-input","graph-output":
      let routes=(kind=="plugin-input" ? mixer["sidechains"]:kind=="graph-input" ? data["inputs"]:data["outputs"]) as? [[String:Any]] ?? []
      guard let i=action["index"] as? Int,routes.indices.contains(i) else{return nil}
      descriptor=routes[i];descriptor["kind"]=kind
    case "insert","output","plugin-output","master-output":break
    default:return nil
    }
    guard let identity=GraphSignalRouteID(descriptor) else{return nil}
    return signalReadings.route(identity)
  }
  func signalRouteName(_ route:GraphSignalRouteID)->String {
    func bus(_ id:String)->String {buses.first{$0["id"] as? String==id}.map(busLabel) ?? id}
    func plugin(_ id:String)->String {(signalNamePrefixes["plugin:"+id] ?? [id]).joined(separator:" › ")}
    switch route.kind {
    case "send":return bus(route.source)+" → "+bus(route.target)+" · send"
    case "output":return bus(route.source)+" → "+bus(route.target)+" · main route"
    case "plugin-connection":return plugin(String(route.source.dropFirst(7)))+" · output \(route.output) → "+plugin(String(route.target.dropFirst(7)))+" · input \(route.input) contribution"
    case "plugin-input":return bus(route.source)+" → "+plugin(route.plugin)+" · "+(route.input==0 ? "main input contribution":"input \(route.input) contribution")
    case "graph-input":return bus(route.source)+" → "+bus(route.target)+" · recipe input \(route.input) contribution"
    case "plugin-output":return plugin(route.plugin)+" · output \(route.output) → "+bus(route.target)
    case "graph-output":return bus(route.source)+" · recipe output \(route.output) → "+bus(route.target)
    case "insert":return bus(route.source)+" → "+plugin(route.plugin)+" · serial main contribution"
    case "master-output":return bus(route.source)+" · processor output before final fader"
    default:return "Route contribution"
    }
  }
  func openScope(spectrum:Bool) {
    if let port=canvas.scopeTarget(at:nil) {signalScope.show(port:port,spectrum:spectrum);return}
    if canvas.selectedEdge != nil {status.stringValue="Measurement unavailable for this cable in the adopted route · select another measured cable or port";return}
    chooseTarget(title:spectrum ? "Spectrum of signal":"Scope signal",entries:signalReadings.ports.filter{$0.available && $0.kind=="audio"}.map {
      .init(id:$0.key,title:$0.name,detail:$0.route?.detail ?? ($0.output ? "Audio output":"Audio input"),keywords:$0.node)
    }) {[weak self] port in self?.signalScope.show(port:port,spectrum:spectrum)}
  }
  func showSignals(_ value:[String:Any]) {
    if (value["active"] as? Bool ?? false) != (lastSignalData["active"] as? Bool ?? false) ||
       (value["playing"] as? Bool ?? false) != (lastSignalData["playing"] as? Bool ?? false) {
      rackControls.refreshSnapshot();pluginControls.parametersView.refreshSnapshot()
    }
    var named=value
    var retainedNames=[String:(node:String,raw:String,shown:String)]()
    named["ports"]=(value["ports"] as? [[String:Any]] ?? []).map { port -> [String:Any] in
      guard port["copy"]==nil,let node=port["node"] as? String else{return port}
      let route=(port["route"] as? [String:Any]).flatMap(GraphSignalRouteID.init)
      guard route != nil || signalNamePrefixes[node] != nil else{return port}
      let raw=port["name"] as? String ?? "Port",key=port["key"] as? String ?? node+"/"+raw
      let previous=signalPortNames[key]
      let shown=previous?.node==node && previous?.raw==raw ? previous!.shown:route.map(signalRouteName) ?? ((signalNamePrefixes[node] ?? [])+[raw]).joined(separator:" › ")
      retainedNames[key]=(node,raw,shown)
      var result=port;result["name"]=shown;return result
    }
    signalPortNames=retainedNames
    lastSignalData=named;applyCopyObservation()
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
    if let edge=canvas.selectedEdge {
      guard let port=observedCablePort(edge),port.output,port.kind=="audio" else{status.stringValue="Measurement unavailable for this cable in the adopted route";return}
      listenControls.select(port.key==listenControls.port ? nil:port.key);return
    }
    if let id=canvas.selected,let port=signalReadings.primaryPort(id,output:true) {
      listenControls.select(port.key==listenControls.port ? nil:port.key);return
    }
    let selected=canvas.selected.map{signalReadings.nodePorts($0,output:true).filter{$0.kind=="audio"}} ?? []
    let choices = !selected.isEmpty ? selected:signalReadings.ports.filter{$0.output && $0.available && $0.kind=="audio"}
    chooseTarget(title:"Listen to output",entries:choices.map {
      .init(id:$0.key,title:$0.name,detail:$0.route?.detail ?? "Temporary monitor · output \($0.port)",keywords:$0.node)
    }) {[weak self] port in self?.listenControls.select(port)}
  }
  func revealObservedNode(_ id:String) {
    if graphID != nil {selectedID=id;canvas.selected=id;canvas.selectedEdge=nil;inspect();configureConnectionInspector();frameSelection();return}
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
      guard songNodePlugin[node.id] != nil || (graphID != nil && nodes.contains{$0["id"] as? String==node.id && $0["kind"] as? String=="plugin"}),let input=signalReadings.port(node.id,output:false),let output=signalReadings.port(node.id,output:true),input.measured,output.measured else{continue}
      if input.peak>1e-5 && output.peak<=1e-7 {
        revealObservedNode(node.id)
        status.stringValue="\(node.title): input active; output silent · Open interface to inspect the plugin";return
      }
    }
    let readings=signalReadings.ports.filter{path.contains($0.node) && $0.measured && $0.kind=="audio"}
    let audible=readings.filter{$0.peak>1e-5}
    status.stringValue=readings.isEmpty ? "Measurement unavailable for this path":audible.isEmpty ? "No signal measured on the observed part of this path · check notes and source playback":"Signal reaches \(audible.last?.name ?? "this path") · measured host ports do not diagnose vendor-internal routing"
    canvas.selectNodes(path.intersection(Set(canvas.nodes.map(\.id))),primary:selectedID)
  }
}
