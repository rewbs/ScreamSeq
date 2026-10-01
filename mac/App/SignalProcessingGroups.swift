import AppKit

struct GraphBoundaryPort:Hashable {
  let node:String,number:UInt32,output:Bool,modulation:Bool
}
struct GraphRealPort {let node:String,number:UInt32}

extension SignalGraphEditor {
  var processingGroups:[[String:Any]] {(definition ?? data)["groups"] as? [[String:Any]] ?? []}
  var selectedProcessingGroup:[String:Any]? {processingGroups.first{$0["id"] as? String==selectedID}}
  func realPort(_ node:String,_ number:UInt32,output:Bool,modulation:Bool)->GraphRealPort {
    boundaryPorts[GraphBoundaryPort(node:node,number:number,output:output,modulation:modulation)] ?? GraphRealPort(node:node,number:number)
  }
  func expandedProcessingSelection(_ ids:Set<String>)->Set<String> {
    let candidates=graphID==nil ? rackPlugins.compactMap{($0["id"] as? String).map{"plugin:"+$0}}:nodes.compactMap{$0["id"] as? String}
    var groups=ids,result=Set(candidates.filter{ids.contains($0)})
    for _ in processingGroups.indices {for group in processingGroups where groups.contains(group["parent"] as? String ?? "") {if let id=group["id"] as? String{groups.insert(id)}}}
    for group in processingGroups where groups.contains(group["id"] as? String ?? ""){result.formUnion(group["nodes"] as? [String] ?? [])}
    return result
  }
  func pruneProcessingGroups(_ definition:inout [String:Any]) {
    guard var groups=definition["groups"] as? [[String:Any]] else{return}
    let existing=Set((definition["nodes"] as? [[String:Any]] ?? []).compactMap{$0["id"] as? String})
    for i in groups.indices{groups[i]["nodes"]=(groups[i]["nodes"] as? [String] ?? []).filter{existing.contains($0)}}
    while true {
      let parents=Set(groups.compactMap{$0["parent"] as? String}),count=groups.count
      groups.removeAll{($0["nodes"] as? [String] ?? []).isEmpty && !parents.contains($0["id"] as? String ?? "")}
      if count==groups.count{break}
    }
    definition["groups"]=groups
  }
  func groupSelection() {
    let ids=canvas.nodes.filter{canvas.selection.contains($0.id) && $0.kind != "boundary"}.map(\.id)
    guard !ids.isEmpty else{status.stringValue="Select processors or nested groups to package";return}
    var params:[String:Any]=["nodes":ids,"parent":processingGroupID as Any? ?? NSNull(),"name":"Group"]
    if let graphID{params["graph"]=graphID}else{
      let groups=ids.filter{id in processingGroups.contains{$0["id"] as? String==id}},processors=ids.filter{!groups.contains($0)}
      guard processors.allSatisfy({id in songNodePlugin[id] != nil && rackPlugins.first{$0["id"] as? String==songNodePlugin[id]}?["isInstrument"] as? Bool != true})else{status.stringValue="Select rack effects or processing groups; channels, instruments and mixer buses remain outside the boundary";return}
      params["nodes"]=processors;params["groups"]=groups
      params["positions"]=canvas.nodes.filter{processors.contains($0.id)}.map{["node":$0.id,"x":$0.x,"y":$0.y] as [String:Any]}
    }
    mutate(graphID==nil ? "graph.song.group.create":"graph.group.create",params) {[weak self] result in
      guard let self,let id=(result["data"] as? [String:Any])?["group"] as? String else{return}
      self.selectedID=id;self.canvas.selected=id;self.inspect();self.focusConnectionValue(self.name)
    }
  }
  func ungroupSelection() {
    guard let group=selectedProcessingGroup?["id"] as? String else{status.stringValue="Select a processing group to unpack its contents";return}
    var params:[String:Any]=["group":group];if let graphID{params["graph"]=graphID}
    mutate(graphID==nil ? "graph.song.group.remove":"graph.group.remove",params)
  }
  func exportProcessingGroup() {
    guard let group=selectedProcessingGroup,let id=group["id"] as? String else{status.stringValue="Select a processing group to save an independent library copy";return}
    var params:[String:Any]=["group":id,"name":group["name"] ?? "Group"];if let graphID{params["graph"]=graphID}
    mutate(graphID==nil ? "graph.song.group.export":"graph.group.export",params){[weak self] result in
      guard let self,let id=(result["data"] as? [String:Any])?["graph"] as? String else{return}
      self.navigate(graph:id);self.status.stringValue="Saved an independent library copy · original processors and assignments retained";self.focusConnectionValue(self.libraryName)
    }
  }
  func navigateProcessingGroup(_ id:String?) {
    guard !hasDraft else{status.stringValue="Finish or cancel the current edit before leaving this group";return}
    guard id==nil || processingGroups.contains(where:{$0["id"] as? String==id})else{return}
    rememberGraphView();processingGroupID=id;selectedID=nil;canvas.selected=nil;canvas.selectedEdge=nil
    nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
    let saved=graphViewStates[graphViewKey]
    if let saved{selectedID=saved.selection;nodeSearch.stringValue=saved.search;nodeCategory.selectItem(at:saved.category)}
    update(data);layoutSubtreeIfNeeded()
    if let saved{scroll.magnification=saved.scale;canvas.scroll(saved.origin)}else{fit()}
    window?.makeFirstResponder(canvas)
  }
  // Project boundaries without rewriting a single DSP endpoint. Port aliases
  // resolve back to the original stable node/parameter before every mutation.
  func projectProcessingGroups(_ original:[SignalCanvasNode],edges:[SignalCanvasEdge])->([SignalCanvasNode],[SignalCanvasEdge]) {
    boundaryPorts=[:]
    definitionEdgeIndices=Array(edges.indices)
    guard !processingGroups.isEmpty else{return(original,edges)}
    let groups=Dictionary(processingGroups.compactMap{g in (g["id"] as? String).map{($0,g)}},uniquingKeysWith:{a,_ in a})
    let originals=Dictionary(original.map{($0.id,$0)},uniquingKeysWith:{a,_ in a})
    var owners=[String:String]()
    for (id,g) in groups{for node in g["nodes"] as? [String] ?? []{owners[node]=id}}
    func parent(_ id:String)->String? {let value=groups[id]?["parent"] as? String;return value=="" ? nil:value}
    func representative(_ node:String)->String? {
      var owner=owners[node]
      if owner==processingGroupID{return node}
      var seen=Set<String>()
      while let id=owner,seen.insert(id).inserted {
        let up=parent(id)
        if up==processingGroupID{return id}
        owner=up
      }
      return nil
    }
    var display=original.filter{owners[$0.id]==processingGroupID}
    var members=[String:[SignalCanvasNode]]()
    for node in original{if let group=representative(node.id),groups[group] != nil {members[group,default:[]].append(node)}}
    for group in processingGroups {
      guard let id=group["id"] as? String,parent(id)==processingGroupID else{continue}
      let contents=members[id] ?? []
      if graphID==nil,filterID != nil,contents.isEmpty{continue}
      var card=SignalCanvasNode(id:id,title:group["name"] as? String ?? "Group",detail:"Processing group · \(contents.count) nodes",kind:"group",x:group["x"] as? Double ?? 0,y:group["y"] as? Double ?? 0,inputs:[],outputs:[])
      for node in contents {for output in [false,true] {for port in output ? node.outputs:node.inputs {
        // Connected internal-only sockets stay inside. Free and external
        // sockets remain available on the group boundary for new patching.
        let cables=edges.filter{(output ? $0.source:$0.target)==node.id && (output ? $0.output:$0.input)==port.number && $0.modulation==port.modulation}
        if !cables.isEmpty && cables.allSatisfy({representative(output ? $0.target:$0.source)==id}) {continue}
        var alias=port;alias.number=UInt32(output ? card.outputs.count:card.inputs.count)
        alias.label=node.title+" · "+port.label
        boundaryPorts[GraphBoundaryPort(node:id,number:alias.number,output:output,modulation:alias.modulation)]=GraphRealPort(node:node.id,number:port.number)
        if output{card.outputs.append(alias)}else{card.inputs.append(alias)}
      }}}
      display.append(card)
    }
    let x0=display.map(\.x).min() ?? 300,x1=display.map{$0.rect.maxX}.max() ?? 600
    var outside=[String:SignalCanvasNode](),projected=[SignalCanvasEdge](),indices=[Int]()
    func endpoint(_ node:String,_ number:UInt32,output:Bool,modulation:Bool)->(String,UInt32)? {
      if let id=representative(node) {
        if id==node{return(node,number)}
        guard let port=boundaryPorts.first(where:{$0.key.node==id && $0.key.output==output && $0.key.modulation==modulation && $0.value.node==node && $0.value.number==number})?.key else{return nil}
        return(id,port.number)
      }
      guard processingGroupID != nil,var stub=originals[node] else{return nil}
      if outside[node]==nil {
        stub.kind="boundary";stub.detail="Outside this group";stub.x=output ? max(8,x0-235):x1+55;stub.y=40+Double(outside.count)*120
        outside[node]=stub
      }
      return(node,number)
    }
    for (index,edge) in edges.enumerated() {
      let a=representative(edge.source),b=representative(edge.target)
      guard a != nil || b != nil else{continue}
      if a==b,a != nil{continue}
      guard let from=endpoint(edge.source,edge.output,output:true,modulation:edge.modulation),let to=endpoint(edge.target,edge.input,output:false,modulation:edge.modulation)else{continue}
      var edge=edge;edge.source=from.0;edge.output=from.1;edge.target=to.0;edge.input=to.1
      projected.append(edge);indices.append(index)
    }
    display += outside.values.sorted{$0.y<$1.y};definitionEdgeIndices=indices
    return(display,projected)
  }
}
