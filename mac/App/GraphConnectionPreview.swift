import AppKit

struct GraphCableOrigin:Equatable {let context:[String],port:GraphBoundaryPort}

/// Bounded, view-side diagnostics for a recipe. The shared model remains the
/// authority at commit. No dryRun, plugin instantiation or host work on hover.
final class GraphConnectionPreview {
  private struct Link {let source:String,target:String,output:UInt32,input:UInt32,modulation:Bool,enabled:Bool}
  private struct Query:Hashable {let source:String,target:String,output:UInt32,input:UInt32,modulation:Bool,converter:Bool,replacing:Int?}
  private enum Result {case valid,invalid(String);var reason:String?{if case let .invalid(reason)=self{return reason};return nil}}
  private var kinds=[String:String](),links=[Link](),audioCount=0,context=[String](),cache=[Query:Result]()
  private(set) var evaluations=0
  func prepare(_ definition:[String:Any]?) {
    kinds=Dictionary((definition?["nodes"] as? [[String:Any]] ?? []).compactMap{node in (node["id"] as? String).map{($0,node["kind"] as? String ?? "")}},uniquingKeysWith:{a,_ in a})
    links=(definition?["audio"] as? [[String:Any]] ?? []).map{.init(source:$0["source"] as? String ?? "",target:$0["target"] as? String ?? "",output:($0["output"] as? NSNumber)?.uint32Value ?? 0,input:($0["input"] as? NSNumber)?.uint32Value ?? 0,modulation:false,enabled:true)}
    audioCount=links.count
    links += (definition?["modulation"] as? [[String:Any]] ?? []).map{.init(source:$0["source"] as? String ?? "",target:$0["target"] as? String ?? "",output:0,input:($0["parameter"] as? NSNumber)?.uint32Value ?? 0,modulation:true,enabled:$0["enabled"] as? Bool ?? true)}
    cache=[:];context=[]
  }
  func reason(from:GraphBoundaryPort,to:GraphBoundaryPort,replacing:Int?,context:[String])->String? {
    if self.context != context{self.context=context;cache=[:]}
    let query=Query(source:from.node,target:to.node,output:from.number,input:to.number,modulation:from.modulation,converter:!from.modulation && to.modulation,replacing:replacing)
    if let previous=cache[query]{return previous.reason}
    evaluations+=1
    let result=validate(query)
    if cache.count>=128{cache.removeAll(keepingCapacity:true)}
    cache[query]=result.map{.invalid($0)} ?? .valid;return result
  }
  private func validate(_ q:Query)->String? {
    guard let source=kinds[q.source],let target=kinds[q.target]else{return "That recipe endpoint no longer exists"}
    if q.source==q.target{return "A processor cannot connect to itself"}
    if let i=q.replacing,!links.indices.contains(i){return "That connection changed; select its current endpoints again"}
    if q.converter {
      guard q.replacing==nil else{return "Rewiring preserves the cable type; create a new cable to insert a follower"}
      guard ["input","plugin"].contains(source),target=="plugin" else{return "An envelope follower needs audio output and a processor parameter"}
      if kinds.count>=64 || audioCount>=256 || links.count-audioCount>=256{return "This recipe has reached its node or connection limit"}
    } else if q.modulation {
      guard ["lfo","automation","follower","random","note-envelope","midi","amount"].contains(source),target=="plugin" else{return "Connect a modulation source to a processor parameter"}
    }else{
      guard ["input","plugin"].contains(source),["plugin","output","follower"].contains(target)else{return "Choose an audio output and an audio input"}
      if q.output>=64 || q.input>=64{return "Audio buses must be between 0 and 63"}
      if target=="follower",q.input != 0{return "Envelope followers have one stereo input"}
    }
    if !q.converter,links.enumerated().contains(where:{i,e in i != q.replacing && e.source==q.source && e.target==q.target && e.modulation==q.modulation && e.output==q.output && e.input==q.input}){return "These sockets are already connected"}
    // A disabled existing modulation cable stays disabled when rewired. The
    // shared compiler includes audio and enabled control dependencies only.
    let enabled=q.replacing.map{links[$0].enabled} ?? true
    if q.modulation && !enabled{return nil}
    var outgoing=[String:[String]]()
    for (i,edge) in links.enumerated() where i != q.replacing && edge.enabled {outgoing[edge.source,default:[]].append(edge.target)}
    var pending=[q.target],visited=Set<String>()
    while let node=pending.popLast() {
      if node==q.source{return "This would create a feedback cycle. A delay effect does not make the cycle safe."}
      if visited.insert(node).inserted{pending += outgoing[node] ?? []}
    }
    return nil
  }
}

extension SignalGraphEditor {
  func cableOrigin(_ key:GraphBoundaryPort)->GraphCableOrigin {.init(context:portActionContext,port:canonicalPort(key))}
  func previewCable(_ first:GraphBoundaryPort,_ second:GraphBoundaryPort,replacing:Int?)->String? {
    if portChoice(first)?.port.signalType == .events || portChoice(second)?.port.signalType == .events {
      guard let a=portChoice(first),let b=portChoice(second)else{return "That note socket no longer exists"}
      return portPairUnavailable(a,b)
    }
    guard graphID != nil else{return nil} // Song ownership/mixer transitions remain host-authoritative.
    let a=canonicalPort(first.output ? first:second),b=canonicalPort(first.output ? second:first)
    let original=replacing.flatMap{definitionEdgeIndices.indices.contains($0) ? definitionEdgeIndices[$0]:nil}
    if replacing != nil && original==nil{return "That connection changed; select its current endpoints again"}
    return connectionPreview.reason(from:a,to:b,replacing:original,context:portActionContext)
  }
}
