import AppKit

extension SignalGraphEditor {
  var visualPresentation:[String:Any] {(graphID==nil ? data["presentation"]:definition?["presentation"]) as? [String:Any] ?? ["regions":[[String:Any]](),"cables":[[String:Any]]()]}
  var visualRegions:[[String:Any]] {visualPresentation["regions"] as? [[String:Any]] ?? []}
  var selectedVisualRegion:[String:Any]? {guard let selectedID,selectedID.hasPrefix("visual:")else{return nil};return visualRegions.first{$0["id"] as? String==String(selectedID.dropFirst(7))}}
  func mutatePresentation(_ p:[String:Any],positions:[(String,Double,Double)]=[],after:(([String:Any])->Void)?=nil){mutate("graph.presentation.set",["graph":graphID as Any? ?? NSNull(),"presentation":p,"positions":positionObjects(positions)],after:after)}
  func editVisualRegion(_ patch:[String:Any]){guard let id=selectedVisualRegion?["id"] as? String else{return};var p=visualPresentation,regions=visualRegions;guard let i=regions.firstIndex(where:{$0["id"] as? String==id})else{return};regions[i].merge(patch){_,new in new};p["regions"]=regions;mutatePresentation(p)}
  func removeVisualRegions(_ ids:Set<String>){var p=visualPresentation;p["regions"]=visualRegions.filter{!ids.contains("visual:"+($0["id"] as? String ?? ""))};mutatePresentation(p)}
  var collapsibleSelection:[SignalCanvasNode] {canvas.nodes.filter{canvas.selection.contains($0.id) && $0.canCollapse}}
  func toggleVisualCollapse(){
    if let r=selectedVisualRegion,r["kind"] as? String=="frame" {editVisualRegion(["collapsed":!(r["collapsed"] as? Bool ?? false)]);return}
    let selected=collapsibleSelection;guard !selected.isEmpty else{status.stringValue="Select a processor, source or visual frame to collapse or expand";return}
    var p=visualPresentation,keys=Set(p["collapsedNodes"] as? [String] ?? [])
    let expand=selected.allSatisfy{keys.contains($0.id)}
    for node in selected {if expand{keys.remove(node.id)}else{keys.insert(node.id)}}
    p["collapsedNodes"]=keys.sorted();mutatePresentation(p)
  }
  func addVisualRegion(comment:Bool,at position:NSPoint?=nil){
    let members=canvas.nodes.filter{canvas.selection.contains($0.id) && !$0.id.hasPrefix("visual:") && $0.kind != "boundary" && $0.kind != "provenance"}
    let rect=members.first.map{first in members.dropFirst().reduce(first.rect){$0.union($1.rect)}.insetBy(dx:-24,dy:-40)}
    let point=position ?? rect?.origin ?? NSPoint(x:canvas.visibleRect.midX,y:canvas.visibleRect.midY),id=UUID().uuidString.lowercased()
    let nodes=comment ? []:members.map(\.id)
    var regions=visualRegions
    if !nodes.isEmpty{for i in regions.indices{regions[i]["nodes"]=(regions[i]["nodes"] as? [String] ?? []).filter{!nodes.contains($0)}}}
    regions.append(["id":id,"kind":comment ? "comment":"frame","scope":processingGroupID ?? "","title":comment ? "Comment":"Frame","text":"","x":max(8,point.x),"y":max(8,point.y),"width":comment ? 320:max(320,rect?.width ?? 320),"height":comment ? 160:max(180,rect?.height ?? 180),"color":0x658b82,"collapsed":false,"nodes":nodes])
    var p=visualPresentation;p["regions"]=regions;mutatePresentation(p){[weak self] _ in guard let self else{return};self.selectedID="visual:"+id;self.canvas.selected=self.selectedID;self.inspect();self.configureConnectionInspector();self.revealAddedNode();self.window?.makeFirstResponder(self.visualControls.title)}
  }
  func visualEdgeIdentity(_ index:Int)->[String:Any]? {
    guard canvas.edges.indices.contains(index),canvas.edges[index].readOnlyReason==nil else{return nil};let edge=canvas.edges[index]
    let a=realPort(edge.source,edge.output,output:true,modulation:edge.modulation),b=realPort(edge.target,edge.input,output:false,modulation:edge.modulation)
    return ["source":a.node,"target":b.node,"output":a.number,"input":b.number,"modulation":edge.modulation,"connection":edge.connection]
  }
  func visualCableMatches(_ p:[String:Any],_ key:[String:Any])->Bool {p["source"] as? String==key["source"] as? String && p["target"] as? String==key["target"] as? String && (p["output"] as? NSNumber)?.uint32Value==(key["output"] as? NSNumber)?.uint32Value && (p["input"] as? NSNumber)?.uint32Value==(key["input"] as? NSNumber)?.uint32Value && (p["modulation"] as? Bool ?? false)==(key["modulation"] as? Bool ?? false) && (p["connection"] as? String ?? "")==((key["connection"] as? String) ?? "")}
  func setReroute(_ index:Int,points:[NSPoint]){if canvas.edges.indices.contains(index),let reason=canvas.edges[index].readOnlyReason{status.stringValue=reason;return};guard var key=visualEdgeIdentity(index)else{return};let selected=canvas.selectedReroute;var p=visualPresentation,paths=p["cables"] as? [[String:Any]] ?? [];paths.removeAll{visualCableMatches($0,key)};if !points.isEmpty{key["points"]=points.map{[Double($0.x),Double($0.y)]};paths.append(key)};p["cables"]=paths;mutatePresentation(p){[weak self] _ in guard let self,let edge=self.canvas.edges.indices.first(where:{self.visualEdgeIdentity($0).map{self.visualCableMatches($0,key)} ?? false})else{return};self.selectConnection(edge);self.canvas.selectReroute(selected.flatMap{points.indices.contains($0) ? $0:nil});self.window?.makeFirstResponder(self.canvas)}}
  func addReroute(){
    guard let index=canvas.selectedEdge,canvas.edges.indices.contains(index),canvas.edges[index].readOnlyReason==nil else{
      let references=Dictionary(uniqueKeysWithValues:canvas.edges.indices.compactMap{i in visualEdgeIdentity(i).map{(String(i),$0)}})
      let entries=canvas.edges.indices.compactMap{i -> GraphAddMenu.Entry? in
        guard references[String(i)] != nil else{return nil};let edge=canvas.edges[i]
        let source=canvas.nodes.first{$0.id==edge.source}?.title ?? edge.source,target=canvas.nodes.first{$0.id==edge.target}?.title ?? edge.target
        return .init(id:String(i),title:source+" → "+target,detail:edge.label,keywords:edge.modulation ? "modulation control":"audio")
      }
      let document=projectionDocument,graph=graphID
      chooseTarget(title:"Choose a cable for a reroute point",entries:entries){[weak self] key in
        guard let self,self.projectionDocument==document,self.graphID==graph,let identity=references[key],let edge=self.canvas.edges.indices.first(where:{self.visualEdgeIdentity($0).map{self.visualCableMatches($0,identity)} ?? false})else{return}
        self.selectConnection(edge);self.addReroute()
      };return
    }
    let edge=canvas.edges[index];guard let a=canvas.nodes.first(where:{$0.id==edge.source}),let b=canvas.nodes.first(where:{$0.id==edge.target})else{return};let from=a.portPoint(.init(number:edge.output,modulation:edge.modulation),output:true),to=b.portPoint(.init(number:edge.input,modulation:edge.modulation),output:false);setReroute(index,points:edge.waypoints+[NSPoint(x:(from.x+to.x)/2,y:(from.y+to.y)/2)])
  }
  func applyVisualCableGeometry(_ edges:inout[SignalCanvasEdge]){let paths=visualPresentation["cables"] as? [[String:Any]] ?? [];for i in edges.indices{let e=edges[i],key:[String:Any]=["source":e.source,"target":e.target,"output":e.output,"input":e.input,"modulation":e.modulation,"connection":e.connection];if let path=paths.first(where:{visualCableMatches($0,key)}){edges[i].waypoints=(path["points"] as? [[Double]] ?? []).compactMap{$0.count==2 ? NSPoint(x:$0[0],y:$0[1]):nil}}}}
  func appendPresentationCommands(to menu:NSMenu){
    menu.addItem(GraphCommand.visualFrame.item("Add visual frame around selection"){[weak self] in self?.addVisualRegion(comment:false)})
    menu.addItem(GraphCommand.visualComment.item("Add comment"){[weak self] in self?.addVisualRegion(comment:true)})
    menu.addItem(GraphCommand.visualCollapse.item("Collapse / expand selection",key:"h",reason:selectedVisualRegion?["kind"] as? String=="frame" || !collapsibleSelection.isEmpty ? nil:"Select a processor, source or visual frame"){[weak self] in self?.toggleVisualCollapse()})
    menu.addItem(GraphCommand.visualReroute.item("Add cable reroute point",reason:canvas.edges.contains{$0.readOnlyReason==nil} ? nil:"Connect an editable cable first"){[weak self] in self?.addReroute()})
    menu.addItem(GraphCommand.visualRemove.item("Remove annotation, keep contents",reason:selectedVisualRegion==nil ? "Select a frame or comment":nil){[weak self] in guard let self,let id=self.selectedID else{return};self.removeVisualRegions([id])})
  }
  /// Visual frames share the existing port-alias map, so collapsing never changes
  /// a processor, connection, parameter identity or musical routing command.
  func projectVisualPresentation(_ original:[SignalCanvasNode],edges originalEdges:[SignalCanvasEdge])->([SignalCanvasNode],[SignalCanvasEdge]) {
    let regions=visualRegions.filter{($0["scope"] as? String ?? "")==(processingGroupID ?? "")}
    var display=original,edges=originalEdges,owners=[String:String](),cards=[String:SignalCanvasNode]()
    for region in regions {
      guard let id=region["id"] as? String else{continue};let key="visual:"+id,comment=region["kind"] as? String=="comment",collapsed=region["collapsed"] as? Bool==true && !comment
      let members=Set(region["nodes"] as? [String] ?? []),visible=original.filter{members.contains($0.id)}
      var card=SignalCanvasNode(id:key,title:(collapsed ? "▸ ":"")+(region["title"] as? String ?? ""),detail:region["text"] as? String ?? "",kind:comment ? "comment":collapsed ? "frame-collapsed":"frame",x:region["x"] as? Double ?? 0,y:region["y"] as? Double ?? 0,inputs:[],outputs:[])
      card.visualColor=(region["color"] as? NSNumber)?.uint32Value ?? 0x658b82;card.visualMembers=Set(visible.map(\.id))
      if !collapsed {card.visualSize=NSSize(width:region["width"] as? Double ?? 320,height:region["height"] as? Double ?? 180)}
      if collapsed {for node in visible{owners[node.id]=key}}
      cards[key]=card
    }
    func endpoint(_ node:String,_ number:UInt32,output:Bool,modulation:Bool)->(String,UInt32){
      guard let key=owners[node],var card=cards[key]else{return(node,number)}
      let real=realPort(node,number,output:output,modulation:modulation)
      if let alias=boundaryPorts.first(where:{$0.key.node==key && $0.key.output==output && $0.key.modulation==modulation && $0.value.node==real.node && $0.value.number==real.number})?.key{return(key,alias.number)}
      let original=original.first{$0.id==node},port=(output ? original?.outputs:original?.inputs)?.first{$0.number==number && $0.modulation==modulation}
      var alias=port ?? SignalCanvasPort(number:number,modulation:modulation);alias.number=UInt32(output ? card.outputs.count:card.inputs.count);alias.label=(original?.title ?? node)+" · "+alias.label
      boundaryPorts[.init(node:key,number:alias.number,output:output,modulation:modulation)]=real
      if output{card.outputs.append(alias)}else{card.inputs.append(alias)};cards[key]=card;return(key,alias.number)
    }
    var kept=[Int]();edges=[]
    for (i,e) in originalEdges.enumerated(){if let a=owners[e.source],a==owners[e.target]{continue};let a=endpoint(e.source,e.output,output:true,modulation:e.modulation),b=endpoint(e.target,e.input,output:false,modulation:e.modulation);var copy=e;copy.source=a.0;copy.output=a.1;copy.target=b.0;copy.input=b.1;edges.append(copy);kept.append(i)}
    if graphID==nil{songConnections=kept.map{songConnections[$0]}}else{definitionEdgeIndices=kept.map{definitionEdgeIndices[$0]}}
    display.removeAll{owners[$0.id] != nil};display=cards.values.sorted{$0.id<$1.id}+display
    let compact=Set(visualPresentation["collapsedNodes"] as? [String] ?? [])
    for i in display.indices where display[i].canCollapse && compact.contains(display[i].id) {
      display[i].collapsed=true;let id=display[i].id
      display[i].inputs.removeAll{port in !edges.contains{$0.target==id && $0.input==port.number && $0.modulation==port.modulation}}
      display[i].outputs.removeAll{port in !edges.contains{$0.source==id && $0.output==port.number && $0.modulation==port.modulation}}
    }
    return(display,edges)
  }
  func moveVisualSelection(_ positions:[(String,Double,Double)])->Bool {
    let visuals=positions.filter{$0.0.hasPrefix("visual:")};guard !visuals.isEmpty else{return false}
    var p=visualPresentation,regions=visualRegions,updates=Dictionary(positions.filter{!$0.0.hasPrefix("visual:")}.map{($0.0,($0.1,$0.2))},uniquingKeysWith:{_,new in new})
    for move in visuals {guard let i=regions.firstIndex(where:{"visual:"+($0["id"] as? String ?? "")==move.0})else{continue};let dx=move.1-(regions[i]["x"] as? Double ?? 0),dy=move.2-(regions[i]["y"] as? Double ?? 0);regions[i]["x"]=move.1;regions[i]["y"]=move.2
      for key in regions[i]["nodes"] as? [String] ?? [] where updates[key]==nil {if let group=processingGroups.first(where:{$0["id"] as? String==key}){updates[key]=(max(0,(group["x"] as? Double ?? 0)+dx),max(0,(group["y"] as? Double ?? 0)+dy))}else if let n=ungroupedNodes.first(where:{$0.id==key}){updates[key]=(max(0,n.x+dx),max(0,n.y+dy))}}
    }
    p["regions"]=regions;mutatePresentation(p,positions:updates.map{($0.key,$0.value.0,$0.value.1)});return true
  }
}
