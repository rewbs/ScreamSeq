import AppKit

extension SignalGraphEditor {
  @discardableResult func offerBranchedDetach(_ ids:[String],positions:[(String,Double,Double)],remove:Bool)->Bool {
    guard let graphID,let definition else{return false}
    let selected=expandedProcessingSelection(Set(ids)),audio=definition["audio"] as? [[String:Any]] ?? []
    let incoming=audio.indices.filter{!selected.contains(audio[$0]["source"] as? String ?? "") && selected.contains(audio[$0]["target"] as? String ?? "")}
    let outgoing=audio.indices.filter{selected.contains(audio[$0]["source"] as? String ?? "") && !selected.contains(audio[$0]["target"] as? String ?? "")}
    // A unique pair also makes a branched/grouped selection explicit. This
    // keeps modulators and internal branches intact without extra confirmation.
    guard !selected.isEmpty else{return false}
    let context=viewContext,document=projectionDocument,capturedRevision=revision
    // Option-drag only previews position. Canceling a path chooser must leave
    // even that preview at the stored document position.
    rebuild()
    func valid()->Bool {self.viewContext==context && self.projectionDocument==document && self.revision==capturedRevision}
    func nodeName(_ key:String)->String{(definition["nodes"] as? [[String:Any]] ?? []).first{$0["id"] as? String==key}?["name"] as? String ?? key}
    func description(_ i:Int)->String {let e=audio[i];return "\(nodeName(e["source"] as? String ?? "")) out \(e["output"] ?? 0) → \(nodeName(e["target"] as? String ?? "")) in \(e["input"] ?? 0)"}
    func commit(_ entering:Int?,_ leaving:Int?) {
      guard valid()else{self.status.stringValue="The graph changed; choose the main path again";return}
      var params:[String:Any]=["graph":graphID,"nodes":selected.sorted(),"remove":remove,"heal":["incoming":entering as Any? ?? NSNull(),"outgoing":leaving as Any? ?? NSNull()]]
      if !remove && !positions.isEmpty{params["positions"]=self.positionObjects(positions)}
      self.mutate("graph.nodes.detach",params)
    }
    func chooseOutgoing(_ entering:Int?) {
      guard valid()else{return}
      if outgoing.count<=1{commit(entering,outgoing.first);return}
      self.chooseTarget(title:"Choose the destination to reconnect",entries:outgoing.map{i in
        let gain=(entering.map{audio[$0]["gain"] as? Double ?? 1} ?? 1)*(audio[i]["gain"] as? Double ?? 1)
        return .init(id:String(i),title:description(i),detail:"Reconnect chosen source · gain ×\(String(format:"%.4g",gain)) · \(remove ? "remaining incident cables removed":"other branches retained")",keywords:"main path reconnect heal")
      }){value in guard let i=Int(value)else{return};commit(entering,i)}
    }
    if incoming.count<=1{chooseOutgoing(incoming.first)}else{
      chooseTarget(title:"Choose the source to reconnect",entries:incoming.map{i in .init(id:String(i),title:description(i),detail:remove ? "Reconnect this source before deleting the selected processors":"Reconnect this source; retain every unchosen branch",keywords:"main path reconnect heal")}){value in guard let i=Int(value)else{return};chooseOutgoing(i)}
    }
    return true
  }
}
