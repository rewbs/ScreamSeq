import AppKit

extension SignalGraphEditor {
  @objc func changeNodeFilter() {
    let edge=canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? canvas.edges[$0]:nil}
    rebuild()
    canvas.selectedEdge=edge.flatMap{old in canvas.edges.firstIndex{$0.source==old.source && $0.target==old.target && $0.output==old.output && $0.input==old.input && $0.modulation==old.modulation}}
    if !hasDraft {inspect()};configureConnectionInspector()
  }
  func clearNodeFilters() {
    nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
    if !hasDraft {filterID=nil;filter.selectItem(at:0)}
    changeNodeFilter()
  }
  func filterNodes(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge])->([SignalCanvasNode],[SignalCanvasEdge]) {
    let words=nodeSearch.stringValue.lowercased().split(whereSeparator:{$0.isWhitespace})
    let category=nodeCategory.indexOfSelectedItem
    var visible=Set(nodes.filter{node in
      let members=node.kind=="group" ? expandedProcessingSelection([node.id]):[]
      let contents=ungroupedNodes.filter{members.contains($0.id)}.map{"\($0.title) \($0.detail)"}.joined(separator:" ")
      let text="\(node.title) \(node.detail) \(node.id) \(contents)".lowercased()
      let plugin=node.kind=="group" || (graphID == nil ? songNodePlugin[node.id] != nil || songNodeGraph[node.id] != nil : self.nodes.first{$0["id"] as? String==node.id}?["kind"] as? String=="plugin")
      let typeMatches=category==0 || (category==1 && node.kind != "modulation" && !plugin) || (category==2 && plugin) || (category==3 && node.kind=="modulation")
      return typeMatches && words.allSatisfy{text.contains($0)}
    }.map(\.id))
    // Keep an in-progress inspector edit visible without resetting its values.
    if hasDraft {
      if let selectedID {visible.insert(selectedID)}
      if let i=canvas.selectedEdge,canvas.edges.indices.contains(i){visible.insert(canvas.edges[i].source);visible.insert(canvas.edges[i].target)}
    }
    if !hasDraft,let selectedID,!visible.contains(selectedID) {self.selectedID=nil}
    if visible.isEmpty && !nodes.isEmpty {canvas.emptyMessage="No nodes match these filters. Clear filters to show the graph."}
    let indices=edges.indices.filter{visible.contains(edges[$0].source) && visible.contains(edges[$0].target)}
    if graphID != nil {definitionEdgeIndices=indices.map{definitionEdgeIndices[$0]}}
    else {songConnections=indices.map{songConnections[$0]};definitionEdgeIndices=[]}
    filterCount.stringValue="\(visible.count) / \(nodes.count) nodes"
    return(nodes.filter{visible.contains($0.id)},indices.map{edges[$0]})
  }
}
