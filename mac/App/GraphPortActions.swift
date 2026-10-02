import AppKit

/// A socket identity survives filtering, duplicate labels, and catalogue refresh.
/// No index from a chooser is ever used as a document endpoint.
struct GraphPortChoice {
  let key:GraphBoundaryPort,node:SignalCanvasNode,port:SignalCanvasPort
  var owner:String? = nil
  var id:String {"\(key.node.utf8.count):\(key.node):\(key.output):\(key.modulation):\(key.number)"}
  var title:String {node.title+" · "+port.label}
  var detail:String {
    let type:String
    switch port.signalType {case .audio:type="Audio";case .sidechain:type="Sidechain audio";case .control:type="Control";case .parameter:type="Parameter";case .events:type="Note events"}
    let context=[owner,node.detail.trimmingCharacters(in:.whitespacesAndNewlines)].compactMap{$0}.filter{!$0.isEmpty && $0 != node.title}
    let socket=(key.modulation || port.signalType == .events) ? "\(type) \(key.output ? "output":"input")":"\(type) \(key.output ? "output":"input") \(key.number)"
    return (context+[socket]+(port.channels.map{["\($0) ch"]} ?? [])).joined(separator:" · ")
  }
  var keywords:String {"\(node.title) \(node.detail) \(detail) \(key.node) \(key.number)"}
  var toolTip:String {"\(title)\n\(detail)\nStable socket: \(key.node) / \(key.number)"}
  func entry(unavailable:String?=nil)->GraphAddMenu.Entry {
    .init(id:id,title:title,detail:detail,keywords:keywords,unavailable:unavailable,toolTip:toolTip)
  }
}
struct GraphCableLocation:Equatable {
  let source:String,target:String,output:UInt32,input:UInt32,modulation:Bool
  var connection=""
}
struct GraphPortReturn {
  let document:String,graph:String?,group:String?,view:GraphViewState,revealed:Set<String>,cable:GraphCableLocation?
}

extension SignalGraphEditor {
  var portActionContext:[String] {[projectionDocument,revision]+filterContext}
  func canonicalPort(_ key:GraphBoundaryPort)->GraphBoundaryPort {
    let real=realPort(key.node,key.number,output:key.output,modulation:key.modulation)
    return .init(node:real.node,number:real.number,output:key.output,modulation:key.modulation)
  }
  var portChoices:[GraphPortChoice] {
    // Retain sockets hidden by channel/name filters. Processing group internals
    // remain at their own depth; their exposed boundary sockets are included.
    var inventory=portActionNodes
    let ids=Set(inventory.map(\.id)),members=Set(processingGroups.flatMap{$0["nodes"] as? [String] ?? []})
    if graphID==nil,processingGroupID==nil {
      let framed=Set(visualRegions.filter{$0["collapsed"] as? Bool==true}.flatMap{$0["nodes"] as? [String] ?? []})
      let compact=Set(visualPresentation["collapsedNodes"] as? [String] ?? [])
      inventory += songPortNodes.filter{!ids.contains($0.id) && !members.contains($0.id) && !framed.contains($0.id)}.map{original in
        var node=original
        let id=node.id
        if compact.contains(id) {
          node.inputs.removeAll{port in !songPortEdges.contains{$0.target==id && $0.input==port.number && $0.modulation==port.modulation}}
          node.outputs.removeAll{port in !songPortEdges.contains{$0.source==id && $0.output==port.number && $0.modulation==port.modulation}}
        }
        return node
      }
    }
    return inventory.flatMap{node in [false,true].flatMap{output in (output ? node.outputs:node.inputs).map{port in
      GraphPortChoice(key:.init(node:node.id,number:port.number,output:output,modulation:port.modulation),node:node,port:port,owner:songNodeBus[node.id].flatMap{busLabels[$0]})
    }}}
  }
  func portChoice(_ key:GraphBoundaryPort)->GraphPortChoice? {portChoices.first{$0.key==key}}
  func portUnavailable(_ choice:GraphPortChoice)->String? {
    if let reason=choice.port.unavailable{return reason}
    if choice.node.kind=="provenance" {return "Reference source: edit its existing pattern or automation lane"}
    if choice.port.signalType == .events,graphID != nil{return "Plugin-instrument note routing lives in the Song graph"}
    return nil
  }
  func portPairUnavailable(_ first:GraphPortChoice,_ second:GraphPortChoice)->String? {
    if let reason=portUnavailable(first) ?? portUnavailable(second){return reason}
    guard first.key.output != second.key.output else{return "Choose an input and an output"}
    let a=first.key.output ? first:second,b=first.key.output ? second:first
    if a.port.signalType == .events || b.port.signalType == .events {
      guard a.port.signalType == .events,b.port.signalType == .events else{return "Notes connect only to Notes sockets; audio and control remain separate"}
      return noteSource(canonicalPort(a.key).node) != nil && noteTarget(canonicalPort(b.key).node) != nil ? nil:"Choose a channel or plugin-instrument note source and a plugin instrument destination"
    }
    if canonicalPort(a.key).node==canonicalPort(b.key).node{return "A processor cannot connect to itself"}
    if a.key.modulation==b.key.modulation{
      if graphID==nil,!a.key.modulation {
        let from=canonicalPort(a.key),to=canonicalPort(b.key)
        if [from.node,to.node].contains(where:{$0.hasPrefix("instrument:") || $0.hasPrefix("instrument-graph:")}) {return "Assign an instrument graph; its output follows the note’s channel"}
        if songNodeGraph[to.node] != nil,to.number==0 {return "Assign this reusable channel copy; open it to edit its internal inputs"}
        if stageTarget(from.node) != nil || stageTarget(to.node) != nil,stageEndpoint(from.node) != nil,stageEndpoint(to.node) != nil{return nil}
        if to.number>0,!(songNodePlugin[from.node] != nil && songNodePlugin[to.node] != nil),!canSumSongInput(from){return "Sidechain inputs require a channel/return output or its final insert; use a subgraph for direct processor patching"}
      }
      return nil
    }
    if !a.key.modulation,b.key.modulation,b.port.signalType == .parameter{return nil} // Explicit follower offer.
    return "Audio sockets accept audio; parameter sockets accept control sources or an envelope follower"
  }
  func canSumSongInput(_ from:GraphBoundaryPort)->Bool {
    let owner=songNodeBus[from.node].flatMap{id in buses.first{$0["id"] as? String==id}}
    let atEnd=songNodePlugin[from.node].map{id in owner.map{effectiveInserts($0).last==id} ?? false} ?? false
    return from.number==0 && owner != nil && (from.node==songNodeBus[from.node] || atEnd)
  }
  func socketStillMatches(_ key:GraphBoundaryPort,_ expected:GraphBoundaryPort)->Bool {
    guard portChoice(key) != nil,canonicalPort(key)==expected else{status.stringValue="The socket layout changed. Choose the port again.";return false};return true
  }
  func chooseSocket(title:String,output:Bool?=nil,editing:Bool=true,action:@escaping(GraphBoundaryPort)->Void) {
    if data.isEmpty || loading{prepareCommand{[weak self] in self?.chooseSocket(title:title,output:output,editing:editing,action:action)};return}
    let selected=canvas.selected ?? selectedID
    let all=portChoices.filter{output==nil || $0.key.output==output},local=all.filter{$0.key.node==selected}
    let choices=local.isEmpty ? all:local,context=portActionContext
    let lookup=Dictionary(choices.map{($0.id,($0.key,canonicalPort($0.key)))},uniquingKeysWith:{a,_ in a})
    chooseTarget(title:local.isEmpty ? title:title+" · selected "+(local.first?.node.title ?? "node"),entries:choices.map{$0.entry(unavailable:editing ? portUnavailable($0):nil)}){[weak self] id in
      guard let self,self.portActionContext==context,let (key,real)=lookup[id],self.socketStillMatches(key,real) else{return}
      action(key)
    }
  }
  func patchByKeyboard(){chooseSocket(title:"Choose a socket to connect"){[weak self] in self?.connectFromSocket($0)}}
  func connectFromSocket(_ start:GraphBoundaryPort) {
    guard !hasDraft else{status.stringValue="Finish the current edit before patching a socket";return}
    guard let first=portChoice(start)else{status.stringValue="That socket is no longer available";return}
    if let reason=portUnavailable(first){status.stringValue=reason;return}
    let choices=portChoices.filter{$0.key.output != start.output},context=portActionContext,realStart=canonicalPort(start)
    let lookup=Dictionary(choices.map{($0.id,($0,canonicalPort($0.key)))},uniquingKeysWith:{a,_ in a})
    chooseTarget(title:"Connect \(first.title) to…",entries:choices.map{$0.entry(unavailable:portPairUnavailable(first,$0))}){[weak self] id in
      guard let self,self.portActionContext==context,let (choice,real)=lookup[id],self.socketStillMatches(start,realStart),self.socketStillMatches(choice.key,real) else{return}
      self.connectSocketPair(start,choice.key)
    }
  }
  func connectSocketPair(_ first:GraphBoundaryPort,_ second:GraphBoundaryPort) {
    guard !hasDraft else{status.stringValue="Finish the current edit before patching a socket";return}
    guard let a=portChoice(first),let b=portChoice(second)else{status.stringValue="The sockets changed; choose them again";return}
    if let reason=portPairUnavailable(a,b){status.stringValue=reason;return}
    let output=first.output ? first:second,input=first.output ? second:first
    let from=canonicalPort(output),to=canonicalPort(input),context=portActionContext
    let commit:(Bool)->Void={[weak self] sum in
      guard let self,self.portActionContext==context,self.socketStillMatches(output,from),self.socketStillMatches(input,to) else{return}
      self.revealPorts([output,input]);self.framePortEndpoints([from,to])
      if !from.modulation,to.modulation {
        self.offerAudioFollower(from.node,to.node,output:from.number,parameter:to.number,position:NSPoint(x:self.scroll.documentVisibleRect.midX,y:self.scroll.documentVisibleRect.midY));return
      }
      let previous=self.canvas.addingMainInput;self.canvas.addingMainInput=sum
      self.connectPorts(from.node,to.node,out:from.number,input:to.number,modulation:from.modulation)
      self.canvas.addingMainInput=previous
    }
    let instrumentTarget=songNodePlugin[to.node].flatMap{id in rackPlugins.first{$0["id"] as? String==id}}?["isInstrument"] as? Bool==true
    if graphID==nil,!from.modulation,!to.modulation,to.number==0,songNodePlugin[to.node] != nil,!instrumentTarget {
      let move=from.number==0 ? insertMove(from.node,to.node):nil
      let sum=canSumSongInput(from) || songNodePlugin[from.node] != nil || stageTarget(from.node) != nil
      chooseTarget(title:"How should this Main input connect?",entries:[
        .init(id:"move",title:"Move chain here",detail:"Move this effect and its following inserts to the source channel · one Undo",keywords:"move insert ownership",unavailable:move==nil ? "This output cannot own the selected effect chain":nil),
        .init(id:"sum",title:"Add / sum input",detail:"Keep the effect chain in place and mix the exact source into Main in · Option-drag equivalent",keywords:"mix add sum input",unavailable:sum ? nil:"Summing requires a channel/return output or a plugin audio output")
      ]){id in commit(id=="sum")}
    }else{commit(false)}
  }
  func connectedAddDestination(_ connection:GraphAddConnection)->(target:String?,before:String?,edge:Int?)? {
    guard graphID==nil,connection.output,!connection.port.modulation,connection.port.number==0,
      let owner=songNodeBus[connection.node],let bus=buses.first(where:{$0["id"] as? String==owner})else{return nil}
    let inserts=effectiveInserts(bus)
    if let plugin=songNodePlugin[connection.node],let index=inserts.firstIndex(of:plugin) {return(owner,index+1<inserts.count ? inserts[index+1]:nil,nil)}
    // A channel/input card precedes its inserts; the final Master sink follows them.
    return(owner,connection.node==owner && bus["kind"] as? String=="master" ? nil:inserts.first,nil)
  }
  func addAtSocket(_ key:GraphBoundaryPort) {
    guard let choice=portChoice(key)else{return}
    if let reason=addSocketUnavailable(choice){status.stringValue=reason;return}
    if choice.port.signalType == .events{connectFromSocket(key);return}
    let real=canonicalPort(key);revealPorts([key]);selectedID=key.node;canvas.selected=key.node;canvas.selectedEdge=nil
    var port=choice.port;port.number=real.number
    showAdd(connecting:GraphAddConnection(node:real.node,port:port,output:key.output))
  }
  func addSocketUnavailable(_ choice:GraphPortChoice)->String? {
    if let reason=portUnavailable(choice){return reason}
    if choice.port.signalType == .events{return nil}
    if choice.key.modulation && choice.key.output{return "Expose a processor parameter and choose Connect to…"}
    if graphID==nil,!choice.key.modulation,!choice.key.output{return "Add from the upstream audio output, or open a reusable subgraph to insert before this socket"}
    if graphID==nil,!choice.key.modulation,choice.key.output,choice.key.number>0{return nil} // A follower accepts an auxiliary tap.
    return nil
  }
  func appendPortCommands(to menu:NSMenu,socket:GraphBoundaryPort?=nil) {
    let reason=socket.flatMap{portChoice($0)}.flatMap{portUnavailable($0)}
    menu.addItem(GraphCommand.patch.item(socket==nil ? "Patch by keyboard…":"Connect to…",reason:reason){[weak self] in guard let self else{return};if let socket{self.connectFromSocket(socket)}else{self.patchByKeyboard()}})
    let addReason=socket.flatMap{portChoice($0)}.flatMap{addSocketUnavailable($0)}
    let add=GraphCommand.portAdd.item(graphID==nil ? "Add compatible node / quiet send…":"Add compatible node…",reason:addReason){[weak self] in guard let self else{return};if let socket{self.addAtSocket(socket)}else{self.chooseSocket(title:"Choose the socket for a new node"){[weak self] in self?.addAtSocket($0)}}}
    if addReason==nil,graphID==nil{add.toolTip="For a quiet send, choose a channel output and then Return bus. The new send starts disabled at −96 dB; raise its cable gain to hear it."}
    menu.addItem(add)
    for (output,title,command) in [(false,"Show sources…",GraphCommand.portSources),(true,"Show targets…",.portTargets)] {
      let wrong=socket.map{$0.output != output} ?? false
      menu.addItem(command.item(title,reason:wrong ? (output ? "Choose an output to show its destinations":"Choose an input to show its sources"):nil){[weak self] in
        guard let self else{return};if let socket{self.showPortNeighbors(socket)}else{self.chooseSocket(title:title,output:output,editing:false){[weak self] in self?.showPortNeighbors($0)}}
      })
    }
    menu.addItem(GraphCommand.cableSource.item("Show cable source",reason:canvas.selectedEdge==nil ? "Select a connection first":nil){[weak self] in self?.showCableEndpoint(output:true)})
    menu.addItem(GraphCommand.cableTarget.item("Show cable target",reason:canvas.selectedEdge==nil ? "Select a connection first":nil){[weak self] in self?.showCableEndpoint(output:false)})
    menu.addItem(GraphCommand.backToCable.item("Back to connection",reason:portReturn==nil ? "Follow a source or target first":nil){[weak self] in self?.returnToConnection()})
    if socket==nil{menu.addItem(GraphCommand.advancedPatch.item("Advanced numeric patching…"){[weak self] in self?.newConnection()})}
  }
  func cableLocation(_ edge:SignalCanvasEdge)->GraphCableLocation {
    let a=realPort(edge.source,edge.output,output:true,modulation:edge.modulation),b=realPort(edge.target,edge.input,output:false,modulation:edge.modulation)
    return .init(source:a.node,target:b.node,output:a.number,input:b.number,modulation:edge.modulation,connection:edge.connection)
  }
  func rememberPortReturn() {
    portReturn=GraphPortReturn(document:projectionDocument,graph:graphID,group:processingGroupID,view:GraphViewState(origin:scroll.contentView.bounds.origin,scale:scroll.magnification,selection:selectedID,filter:filterID,search:nodeSearch.stringValue,category:nodeCategory.indexOfSelectedItem),revealed:graphFilterState.revealed,cable:canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? cableLocation(canvas.edges[$0]):nil})
  }
  func revealPorts(_ keys:[GraphBoundaryPort]) {
    graphFilterState.prepare(filterContext)
    for key in keys {graphFilterState.revealed.insert(key.node);graphFilterState.revealed.insert(canonicalPort(key).node)}
    rebuild()
  }
  func framePortEndpoints(_ endpoints:[GraphBoundaryPort]) {
    let visible=scroll.documentVisibleRect
    let targets=endpoints.compactMap{key -> (NSRect,NSPoint)? in
      let alias=boundaryPorts.first{$0.key.output==key.output && $0.key.modulation==key.modulation && $0.value.node==key.node && $0.value.number==key.number}?.key
      let shown=canvas.nodes.contains{$0.id==key.node} ? key:(alias ?? key)
      guard let node=canvas.nodes.first(where:{$0.id==shown.node}),let port=(shown.output ? node.outputs:node.inputs).first(where:{$0.number==shown.number && $0.modulation==shown.modulation})else{return nil}
      return(node.rect,node.portPoint(port,output:shown.output))
    }
    guard let first=targets.first,targets.contains(where:{!visible.contains($0.1)})else{return}
    frameCanvas(targets.dropFirst().reduce(first.0){$0.union($1.0)}.insetBy(dx:-24,dy:-24),maximumScale:scroll.magnification)
  }
  func showPort(_ key:GraphBoundaryPort) {
    guard !hasDraft else{status.stringValue="Finish the current edit before following this connection";return}
    let real=canonicalPort(key);rememberPortReturn();revealPorts([key])
    let alias=boundaryPorts.first{$0.key.output==real.output && $0.key.modulation==real.modulation && $0.value.node==real.node && $0.value.number==real.number}?.key
    let id=canvas.nodes.contains{$0.id==key.node} ? key.node:(alias?.node ?? real.node)
    guard canvas.nodes.contains(where:{$0.id==id})else{status.stringValue="This endpoint is inside another group; open that group to inspect it";return}
    canvas.selectedEdge=nil;manualConnection=false;selectedID=id;canvas.selected=id;inspect();configureConnectionInspector();frameSelection();window?.makeFirstResponder(canvas)
    status.stringValue="Showing \(canvas.nodes.first{$0.id==id}?.title ?? id) · Back to connection returns to the previous view"
  }
  func showCableEndpoint(output:Bool) {
    guard let i=canvas.selectedEdge,canvas.edges.indices.contains(i)else{return};let edge=canvas.edges[i]
    showPort(.init(node:output ? edge.source:edge.target,number:output ? edge.output:edge.input,output:output,modulation:edge.modulation))
  }
  func showPortNeighbors(_ key:GraphBoundaryPort) {
    let real=canonicalPort(key),context=portActionContext
    var seen=Set<GraphBoundaryPort>()
    let edges=portActionEdges+(graphID==nil && processingGroupID==nil ? songPortEdges:[])
    let neighbors=edges.compactMap{edge -> GraphBoundaryPort? in
      let current=canonicalPort(.init(node:key.output ? edge.source:edge.target,number:key.output ? edge.output:edge.input,output:key.output,modulation:edge.modulation))
      guard current==real else{return nil}
      let other=GraphBoundaryPort(node:key.output ? edge.target:edge.source,number:key.output ? edge.input:edge.output,output:!key.output,modulation:edge.modulation)
      return seen.insert(canonicalPort(other)).inserted ? other:nil
    }
    if neighbors.count==1,let first=neighbors.first{showPort(first);return}
    let lookup=Dictionary(neighbors.enumerated().map{(String($0.offset),canonicalPort($0.element))},uniquingKeysWith:{a,_ in a})
    chooseTarget(title:key.output ? "Show connection target":"Show connection source",entries:neighbors.enumerated().map{index,key in
      let choice=portChoice(key),node=(portActionNodes+songPortNodes).first{$0.id==key.node}
      return .init(id:String(index),title:choice?.title ?? (node?.title ?? key.node),detail:choice?.detail ?? "Socket \(key.number) · \(key.node)",keywords:choice?.keywords ?? key.node,toolTip:choice?.toolTip)
    }){[weak self] id in guard let self,self.portActionContext==context,let key=lookup[id]else{return};self.showPort(key)}
  }
  func returnToConnection() {
    guard let back=portReturn,back.document==projectionDocument,back.graph==graphID,back.group==processingGroupID,!hasDraft else{status.stringValue="The original connection view is no longer available";return}
    filterID=back.view.filter;nodeSearch.stringValue=back.view.search;nodeCategory.selectItem(at:back.view.category)
    picker(filter,[("All channels","")]+buses.map{(busLabel($0),$0["id"] as? String ?? "")},select:filterID)
    graphFilterState.prepare(filterContext);graphFilterState.revealed=back.revealed;selectedID=back.view.selection;canvas.selectedEdge=nil;rebuild()
    if let cable=back.cable,let index=canvas.edges.firstIndex(where:{cableLocation($0)==cable}){selectConnection(index)}else{inspect();configureConnectionInspector()}
    scroll.magnification=back.view.scale;canvas.scroll(back.view.origin);portReturn=nil;window?.makeFirstResponder(canvas)
  }
}
