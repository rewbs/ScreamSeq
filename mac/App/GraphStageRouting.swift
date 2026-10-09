import AppKit

extension SignalGraphEditor {
  var stageConnections:[[String:Any]] {data["stageConnections"] as? [[String:Any]] ?? []}
  func stageTarget(_ node:String)->String? {node.hasPrefix("stage:") ? String(node.dropFirst(6)):nil}
  func stagePorts(_ target:String,input:Bool)->[UInt32] {
    var graphs=Set((data["assignments"] as? [[String:Any]] ?? []).filter{$0["target"] as? String==target}.compactMap{$0["graph"] as? String})
    for c in data["commands"] as? [[String:Any]] ?? [] where c["target"] as? String==target && ["row","start"].contains(c["kind"] as? String ?? ""){if let id=c["graph"] as? String{graphs.insert(id)}}
    var ports=Set<UInt32>()
    for d in definitions where graphs.contains(d["id"] as? String ?? "") {
      let endpoints=Set((d["nodes"] as? [[String:Any]] ?? []).filter{$0["kind"] as? String==(input ? "input":"output")}.compactMap{$0["id"] as? String})
      for e in d["audio"] as? [[String:Any]] ?? [] where endpoints.contains(e[input ? "source":"target"] as? String ?? "") {
        if let p=(e[input ? "output":"input"] as? NSNumber)?.uint32Value,p>0,p<64{ports.insert(p)}
      }
    }
    return ports.sorted()
  }
  func stageSockets(_ target:String,input:Bool)->[SignalCanvasPort] {
    let declared=Set(stagePorts(target,input:input));var ports=declared
    for r in stageConnections {if (r[input ? "target":"source"] as? [String:Any])?["stage"] as? String==target,let p=(r[input ? "input":"output"] as? NSNumber)?.uint32Value{ports.insert(p)}}
    if !input {for s in songSources where s["audioStage"] as? String==target {if let p=(s["output"] as? NSNumber)?.uint32Value{ports.insert(p)}}}
    for r in data[input ? "inputs":"outputs"] as? [[String:Any]] ?? [] where r[input ? "target":"source"] as? String==target{if let p=(r[input ? "input":"output"] as? NSNumber)?.uint32Value{ports.insert(p)}}
    return ports.sorted().map{SignalCanvasPort(number:$0,label:(input ? "Shared graph input ":"Combined graph output ")+String($0),unavailable:declared.contains($0) ? nil:"This stage no longer exposes the saved port; assign a matching recipe or cut the cable")}
  }
  func stageEndpoint(_ node:String)->[String:Any]? {
    if let target=stageTarget(node){return ["stage":target]}
    return songNodePlugin[node].map{["plugin":$0]}
  }
  func stageNode(_ endpoint:[String:Any])->String? {
    if let stage=endpoint["stage"] as? String{return "stage:"+stage}
    return (endpoint["plugin"] as? String).map{"plugin:"+$0}
  }
  func connectStage(_ a:String,_ b:String,output:UInt32,input:UInt32,gain:Double=0,enabled:Bool=true,replacing:[String:Any]?=nil) {
    guard let from=stageEndpoint(a),let to=stageEndpoint(b),stageTarget(a) != nil || stageTarget(b) != nil else{status.stringValue="Choose a graph stage and a rack or stage audio socket";return}
    guard a != b else{status.stringValue="A graph stage cannot connect to itself";return}
    if let stage=stageTarget(a),!stagePorts(stage,input:false).contains(output){status.stringValue="That combined graph output is unavailable";return}
    if let stage=stageTarget(b),!stagePorts(stage,input:true).contains(input){status.stringValue="That shared graph input is unavailable";return}
    var value:[String:Any]=["source":from,"output":output,"target":to,"input":input,"gainDB":gain,"enabled":enabled]
    if let replacing{value["replace"]=replacing}
    mutate("graph.audio.connection.set",value)
  }
  func stageRuntimeEndpoint(_ endpoint:[String:Any])->String {
    if let stage=endpoint["stage"] as? String{return "plugin:signal-bus-"+stage.dropFirst()}
    return "plugin:"+(endpoint["plugin"] as? String ?? "")
  }
}
