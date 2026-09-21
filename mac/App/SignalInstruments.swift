import AppKit

extension SignalGraphEditor {
  var sampleInstruments:[[String:Any]]{(data["instruments"] as? [[String:Any]] ?? []).filter{$0["plugin"] as? Bool != true}}
  var selectedInstrument:[String:Any]?{sampleInstruments.first{"instrument:\($0["id"] as? String ?? "")"==selectedID}}
  func inspectSampleInstrument(){
    guard let raw=chosen(instrumentPicker),let instrument=sampleInstruments.first(where:{$0["id"] as? String==raw})else{status.stringValue="Create a sample instrument first";return}
    graphID=nil;selectedID="instrument:\(instrument["id"] as? String ?? "")";update(data)
  }
  func assignSampleInstrument(_ instrument:[String:Any]){
    guard let index=instrument["index"] as? Int,let amount=Double(assignAmount.stringValue),let wet=Double(assignWet.stringValue)else{return}
    mutate("graph.instrument.assign",["instrument":index,"graph":chosen(assignment) as Any? ?? NSNull(),"amount":amount,"wet":wet])
  }
  func instrumentOverview(visible:Set<String>)->([SignalCanvasNode],[SignalCanvasEdge]){
    var nodes=[SignalCanvasNode](),edges=[SignalCanvasEdge]()
    let assignments=data["instrumentAssignments"] as? [[String:Any]] ?? []
    let selected=selectedInstrument?["id"] as? String
    let saved=data["layout"] as? [[String:Any]] ?? []
    var startY=Double(buses.filter{visible.contains($0["id"] as? String ?? "")}.count*125+60)
    for instrument in sampleInstruments{guard let id=instrument["id"] as? String else{continue}
      let assignment=assignments.first{$0["target"] as? String==id}
      guard assignment != nil || id==selected else{continue}
      let key="instrument:\(id)",y=startY;startY+=Double(max(1,buses.filter{$0["kind"] as? String=="track" && visible.contains($0["id"] as? String ?? "")}.count))*90+100
      func node(_ key:String,_ title:String,_ detail:String,_ x:Double,_ y:Double)->SignalCanvasNode{let position=saved.first{$0["node"] as? String==key};return SignalCanvasNode(id:key,title:title,detail:detail,kind:"audio",x:position?["x"] as? Double ?? x,y:position?["y"] as? Double ?? y)}
      var source=node(key,"I\(instrument["index"] ?? 0) · \(instrument["name"] as? String ?? "Instrument")","Sample voices · per channel",30,y);source.inputs=[];nodes.append(source)
      guard let graph=assignment?["graph"] as? String else{continue}
      let definition=definitions.first{$0["id"] as? String==graph}
      for (i,bus) in buses.filter({$0["kind"] as? String=="track" && visible.contains($0["id"] as? String ?? "")}).enumerated(){guard let target=bus["id"] as? String else{continue};let copy="instrument-graph:\(id):\(target)"
        nodes.append(node(copy,"\(definition?["number"] ?? 0) · \(definition?["name"] as? String ?? "Subgraph")","I\(instrument["index"] ?? 0) → \(bus["name"] as? String ?? "channel")",265,y+Double(i)*90))
        songNodeGraph[copy]=graph
        edges.append(SignalCanvasEdge(source:key,target:copy,label:"Independent copy"));edges.append(SignalCanvasEdge(source:copy,target:target,label:"Before channel"))
      }
    }
    return(nodes,edges)
  }
}
