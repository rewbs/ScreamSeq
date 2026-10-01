import AppKit

struct GraphHiddenBranch {
  var endpoint:GraphBoundaryPort
  let hidden:String,title:String,reveal:Set<String>
}
final class GraphFilterState {
  var context=[String](),revealed=Set<String>(),focusBranches=[GraphHiddenBranch]()
  func prepare(_ next:[String]) {if context != next{context=next;revealed=[];focusBranches=[]}}
}


extension SignalGraphEditor {
  var filterContext:[String] {[projectionDocument,graphID ?? "",filterID ?? "",nodeSearch.stringValue,String(nodeCategory.indexOfSelectedItem),processingGroupID ?? ""]}
  func hiddenBranches(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge],visible:Set<String>)->[GraphHiddenBranch] {
    let titles=Dictionary(nodes.map{($0.id,$0.title)},uniquingKeysWith:{a,_ in a})
    return edges.compactMap { edge in
      let sourceVisible=visible.contains(edge.source),targetVisible=visible.contains(edge.target)
      guard sourceVisible != targetVisible else{return nil}
      let hidden=sourceVisible ? edge.target:edge.source
      var reveal:Set<String>=[hidden]
      if graphID==nil,let bus=songNodeBus[hidden]{reveal.insert(bus)}
      return GraphHiddenBranch(endpoint:GraphBoundaryPort(node:sourceVisible ? edge.source:edge.target,number:sourceVisible ? edge.output:edge.input,output:sourceVisible,modulation:edge.modulation),hidden:hidden,title:titles[hidden] ?? hidden,reveal:reveal)
    }
  }
  func focusSongNodes(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge])->([SignalCanvasNode],[SignalCanvasEdge]) {
    songPortNodes=nodes;songPortEdges=edges
    graphFilterState.prepare(filterContext);graphFilterState.focusBranches=[]
    guard let filterID else{return(nodes,edges)}
    let roots=Set(nodes.filter{node in graphFilterState.revealed.contains(node.id) || (node.id==selectedID && node.id.hasPrefix("source:")) || songNodeBus[node.id].map{$0==filterID || graphFilterState.revealed.contains($0)}==true}.map(\.id))
    var downstream=roots,required=roots
    // Follow the focused channel to its outputs. Its sidechain sources need
    // their complete input trees, but their unrelated output branches do not.
    // In particular, downstream summing buses must not expand every sibling.
    var changed=true
    while changed {
      let oldDownstream=downstream,oldRequired=required
      for edge in edges where downstream.contains(edge.source){downstream.insert(edge.target)}
      let visible=downstream.union(required)
      for (index,edge) in edges.enumerated() {
        let kind=songConnections[index]["kind"] as? String
        if required.contains(edge.target) || (visible.contains(edge.target) && (kind=="plugin-input" || kind=="graph-input" || kind=="follower-input" || edge.modulation)) {required.insert(edge.source)}
      }
      changed=oldDownstream != downstream || oldRequired != required
    }
    let visible=downstream.union(required)
    graphFilterState.focusBranches=hiddenBranches(nodes,edges:edges,visible:visible)
    let indices=edges.indices.filter{visible.contains(edges[$0].source) && visible.contains(edges[$0].target)}
    songConnections=indices.map{songConnections[$0]}
    return(nodes.filter{visible.contains($0.id)},indices.map{edges[$0]})
  }
  func revealHiddenRoutes() {
    if let selected=canvas.selectedBoundary{revealBoundary(selected);return}
    let candidates=canvas.boundaries.filter{selectedID==nil || $0.node==selectedID}
    let choices=candidates.isEmpty ? canvas.boundaries:candidates
    if choices.count==1,let only=choices.first{revealBoundary(only.id);return}
    let captured=filterContext
    chooseTarget(title:"Reveal hidden routes",entries:choices.map { stub in
      .init(id:stub.id,title:(canvas.nodes.first{$0.id==stub.node}?.title ?? "Node")+" · "+stub.label,detail:stub.help,keywords:stub.names.joined(separator:" "))
    }) {[weak self] id in guard let self,self.filterContext==captured else{return};self.revealBoundary(id)}
  }
  func revealBoundary(_ id:String) {
    guard !hasDraft,let boundary=canvas.boundaries.first(where:{$0.id==id})else{status.stringValue="Finish the current edit before revealing hidden routes";return}
    let previousEdge=canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? canvas.edges[$0]:nil}
    graphFilterState.revealed.formUnion(boundary.reveal)
    rebuild()
    canvas.selectedEdge=previousEdge.flatMap{old in canvas.edges.firstIndex{$0.source==old.source && $0.target==old.target && $0.output==old.output && $0.input==old.input && $0.modulation==old.modulation && $0.connection==old.connection}}
    inspect();configureConnectionInspector();window?.makeFirstResponder(canvas)
    status.stringValue="Revealed hidden routes · Clear filters restores the complete graph"
  }
  func updateFilterBoundaries(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge],visible:Set<String>) {
    var branches=hiddenBranches(nodes,edges:edges,visible:visible)
    if graphID==nil {
      for var branch in graphFilterState.focusBranches {
        if !visible.contains(branch.endpoint.node) {
          guard let alias=boundaryPorts.first(where:{$0.key.output==branch.endpoint.output && $0.key.modulation==branch.endpoint.modulation && $0.value.node==branch.endpoint.node && $0.value.number==branch.endpoint.number && visible.contains($0.key.node)})?.key else{continue}
          branch.endpoint=alias
        }
        branches.append(branch)
      }
    }
    let groups=Dictionary(grouping:branches,by:{$0.endpoint})
    canvas.boundaries=groups.map { endpoint,items in
      let distinct=Dictionary(items.map{($0.hidden,$0.title)},uniquingKeysWith:{a,_ in a})
      return SignalCanvasBoundary(node:endpoint.node,number:endpoint.number,output:endpoint.output,modulation:endpoint.modulation,names:distinct.values.sorted(),reveal:items.reduce(into:Set<String>()){$0.formUnion($1.reveal)})
    }.sorted{$0.id<$1.id}
    canvas.onRevealBoundary={[weak self] id in self?.revealBoundary(id)}
    canvas.onFocusBoundary={[weak self] in guard let self else{return};self.selectedID=nil;self.inspect();self.configureConnectionInspector()}
  }
  @objc func changeNodeFilter() {
    let edge=canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? canvas.edges[$0]:nil}
    rebuild()
    canvas.selectedEdge=edge.flatMap{old in canvas.edges.firstIndex{$0.source==old.source && $0.target==old.target && $0.output==old.output && $0.input==old.input && $0.modulation==old.modulation && $0.connection==old.connection}}
    if !hasDraft {inspect()};configureConnectionInspector();requestEmptyViewportRecovery()
  }
  func clearNodeFilters() {
    graphFilterState.revealed=[]
    nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
    if !hasDraft {filterID=nil;filter.selectItem(at:0)}
    changeNodeFilter()
  }
  func filterNodes(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge])->([SignalCanvasNode],[SignalCanvasEdge]) {
    graphFilterState.prepare(filterContext)
    let words=nodeSearch.stringValue.lowercased().split(whereSeparator:{$0.isWhitespace})
    let category=nodeCategory.indexOfSelectedItem
    var visible=Set(nodes.filter{node in
      let members=node.kind=="group" ? expandedProcessingSelection([node.id]):[]
      let contents=ungroupedNodes.filter{members.contains($0.id)}.map{"\($0.title) \($0.detail)"}.joined(separator:" ")
      let text="\(node.title) \(node.detail) \(node.id) \(contents)".lowercased()
      let plugin=node.kind=="group" || (graphID == nil ? songNodePlugin[node.id] != nil || songNodeGraph[node.id] != nil : self.nodes.first{$0["id"] as? String==node.id}?["kind"] as? String=="plugin")
      let typeMatches=category==0 || (category==1 && node.kind != "modulation" && !plugin) || (category==2 && plugin) || (category==3 && node.kind=="modulation")
      return graphFilterState.revealed.contains(node.id) || (typeMatches && words.allSatisfy{text.contains($0)})
    }.map(\.id))
    // Keep an in-progress inspector edit visible without resetting its values.
    if hasDraft {
      if let selectedID {visible.insert(selectedID)}
      if let i=canvas.selectedEdge,canvas.edges.indices.contains(i){visible.insert(canvas.edges[i].source);visible.insert(canvas.edges[i].target)}
    }
    if !hasDraft,let selectedID,!visible.contains(selectedID) {self.selectedID=nil}
    if visible.isEmpty && !nodes.isEmpty {canvas.emptyMessage="No nodes match these filters. Clear filters to show the graph."}
    updateFilterBoundaries(nodes,edges:edges,visible:visible)
    let indices=edges.indices.filter{visible.contains(edges[$0].source) && visible.contains(edges[$0].target)}
    if graphID != nil {definitionEdgeIndices=indices.map{definitionEdgeIndices[$0]}}
    else {songConnections=indices.map{songConnections[$0]};definitionEdgeIndices=[]}
    filterCount.stringValue="\(visible.count) / \(nodes.count) nodes"+(canvas.boundaries.isEmpty ? "":" · \(canvas.boundaries.count) hidden branches")
    return(nodes.filter{visible.contains($0.id)},indices.map{edges[$0]})
  }
}
