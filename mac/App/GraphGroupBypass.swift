import AppKit

extension SignalGraphEditor {
  // Boundary choices are captured at one document revision. The whole map is
  // committed once, after all choices; cancelling never edits a partial map.
  func setProcessingGroupBypass(_ group:[String:Any],bypass:Bool,choosePaths:Bool=false) {
    guard let id=group["id"] as? String,let request=onRequest else{return}
    let graph=graphID,context=viewContext,expected=revision
    let base:[String:Any]=["graph":graph as Any? ?? NSNull(),"group":id]
    func current()->Bool {self.revision==expected && self.viewContext==context}
    func commit(_ routes:[[String:Any]]?) {
      guard current() else{self.status.stringValue="The graph changed while choosing dry paths; choose the group again";return}
      var parameters=base;parameters["bypass"]=bypass
      if let routes{parameters["dryRoutes"]=routes}
      self.mutate("graph.group.bypass",parameters)
    }
    if !bypass && !choosePaths {commit(nil);return}
    request("graph.group.boundary",base){[weak self] response in
      guard let self,current() else{return}
      if let error=response["error"] as? [String:Any]{self.status.stringValue=error["message"] as? String ?? "Unable to read group boundary";return}
      guard let data=(response["result"] as? [String:Any] ?? response)["data"] as? [String:Any] else{return}
      if !choosePaths && data["needsMapping"] as? Bool != true{commit(nil);return}
      let inputs=data["inputs"] as? [[String:Any]] ?? [],outputs=data["outputs"] as? [[String:Any]] ?? []
      guard !inputs.isEmpty && !outputs.isEmpty else{self.status.stringValue="This boundary has no selectable dry path";return}
      var mappings=[[String:Any]]()
      func choose(_ index:Int) {
        guard current() else{self.status.stringValue="The graph changed while choosing dry paths; choose the group again";return}
        guard index<outputs.count else{commit(mappings);return}
        let output=outputs[index]
        let entries=inputs.enumerated().map{offset,input in GraphAddMenu.Entry(id:String(offset),title:self.groupBoundaryLabel(input,output:false),detail:"Dry input for "+self.groupBoundaryLabel(output,output:true),keywords:String(describing:input))}
        self.chooseTarget(title:"Dry path \(index+1)/\(outputs.count) → "+self.groupBoundaryLabel(output,output:true),entries:entries){choice in
          guard current(),let selected=Int(choice),inputs.indices.contains(selected) else{return}
          mappings.append(["input":inputs[selected],"output":output]);choose(index+1)
        }
      }
      choose(0)
    }
  }
  func chooseGroupDryPaths() {
    if let group=selectedProcessingGroup{setProcessingGroupBypass(group,bypass:group["bypass"] as? Bool ?? false,choosePaths:true);return}
    chooseTarget(title:"Choose a processing group",entries:processingGroups.compactMap{group in
      guard let id=group["id"] as? String else{return nil};return .init(id:id,title:group["name"] as? String ?? "Group",detail:"Configure its audio dry paths without changing bypass",keywords:id)
    }){[weak self] id in guard let self,let group=self.processingGroups.first(where:{$0["id"] as? String==id})else{return};self.setProcessingGroupBypass(group,bypass:group["bypass"] as? Bool ?? false,choosePaths:true)}
  }
  func groupBoundaryLabel(_ endpoint:[String:Any],output:Bool)->String {
    func label(_ key:String)->String {
      if key.hasPrefix("plugin:"){return rackPlugins.first{$0["id"] as? String==String(key.dropFirst(7))}?["name"] as? String ?? "Processor"}
      return nodes.first{$0["id"] as? String==key}?["name"] as? String ?? busLabels[key] ?? key
    }
    if let node=endpoint["node"] as? String{return label(node)+" · output \(endpoint["port"] as? Int ?? 0)"}
    if graphID != nil {return label(endpoint["source"] as? String ?? "")+" → "+label(endpoint["target"] as? String ?? "")+" · input \(endpoint["input"] as? Int ?? 0)"}
    let plugin=endpoint["plugin"] as? String ?? "",kind=endpoint["kind"] as? String ?? ""
    if kind=="insert"{return "Main path → "+label("plugin:"+plugin)}
    let source=endpoint["source"] as? String ?? "",target=endpoint["target"] as? String ?? ""
    if kind=="plugin-connection"{return label(source)+" / output \(endpoint["output"] as? Int ?? 0) → "+label(target)+" / input \(endpoint["input"] as? Int ?? 0)"}
    if kind=="plugin-input"{return label(source)+" → "+label("plugin:"+plugin)+" / input \(endpoint["input"] as? Int ?? 0)"}
    if kind=="plugin-output"{return label("plugin:"+plugin)+" / output \(endpoint["output"] as? Int ?? 0) → "+label(target)}
    return (source.isEmpty ? label("plugin:"+plugin):label(source))+" → "+(target.isEmpty ? label("plugin:"+plugin):label(target))+" · "+kind
  }
}
