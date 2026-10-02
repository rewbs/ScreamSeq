import AppKit

struct GraphAddConnection {
  var node:String,port:SignalCanvasPort,output:Bool
}
struct GraphViewState {
  var origin:NSPoint, scale:CGFloat, selection:String?, filter:String?, search:String, category:Int
}
struct GraphPanelReturn {
  var document:String,graph:String?,group:String?,origin:String?,target:String?
  var view:GraphViewState,selection:Set<String>,edge:SignalCanvasEdge?
}

extension SignalGraphEditor {
  func rememberPanelReturn() {
    rememberGraphView()
    guard let view=graphViewStates[graphViewKey] else{return}
    panelReturn=GraphPanelReturn(document:projectionDocument,graph:graphID,group:processingGroupID,origin:graphOrigin,target:graphTarget,view:view,selection:canvas.selection,edge:canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? canvas.edges[$0]:nil})
    provenance.hasReturn=true
  }
  func restorePanelReturn() {
    guard let saved=panelReturn,saved.document==projectionDocument else{return}
    guard !hasDraft else{status.stringValue="Finish the current graph edit before returning";return}
    graphID=saved.graph;processingGroupID=saved.group;graphOrigin=saved.origin;graphTarget=saved.target
    filterID=saved.view.filter;nodeSearch.stringValue=saved.view.search;nodeCategory.selectItem(at:saved.view.category)
    selectedID=saved.view.selection;canvas.selected=selectedID;canvas.selectedEdge=nil;update(data)
    let retained=saved.selection.intersection(Set(canvas.nodes.map(\.id)))
    canvas.selectNodes(retained,primary:retained.contains(saved.view.selection ?? "") ? saved.view.selection:nil);selectedID=canvas.selected
    if let edge=saved.edge,let index=canvas.edges.firstIndex(where:{$0.source==edge.source&&$0.target==edge.target&&$0.output==edge.output&&$0.input==edge.input&&$0.modulation==edge.modulation&&$0.connection==edge.connection}){selectConnection(index)}
    else{inspect();configureConnectionInspector()}
    layoutSubtreeIfNeeded();scroll.magnification=saved.view.scale;canvas.scroll(saved.view.origin)
  }
  var graphViewKey:String { (graphID ?? "song")+(processingGroupID.map{"/"+$0} ?? "") }
  func rememberGraphView() {
    graphViewStates[graphViewKey]=GraphViewState(origin:scroll.contentView.bounds.origin,scale:scroll.magnification,selection:selectedID,filter:filterID,search:nodeSearch.stringValue,category:nodeCategory.indexOfSelectedItem)
  }
  func navigate(graph:String?, origin:String?=nil,target:String?=nil,observedCopy:GraphObservedCopy?=nil) {
    guard !hasDraft else { status.stringValue="Finish or cancel the current edit before leaving this group";return }
    if let observedCopy {copyObservation.enter(observedCopy)}
    rememberGraphView();graphID=graph;processingGroupID=nil;graphOrigin=origin;graphTarget=target;selectedID=nil;canvas.selected=nil;canvas.selectedEdge=nil
    nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
    let saved=graphViewStates[graphViewKey]
    if let saved {selectedID=saved.selection;filterID=saved.filter;nodeSearch.stringValue=saved.search;nodeCategory.selectItem(at:saved.category)}
    update(data)
    layoutSubtreeIfNeeded()
    if let saved {scroll.magnification=saved.scale;canvas.scroll(saved.origin)} else {fit()}
    window?.makeFirstResponder(canvas);loadPortCatalogs()
  }
  func navigateParent() {
    if let processingGroupID {
      let parent=processingGroups.first{$0["id"] as? String==processingGroupID}?["parent"] as? String
      navigateProcessingGroup(parent=="" ? nil:parent);return
    }
    guard graphID != nil else{return};navigate(graph:nil)
  }
  func refreshBreadcrumbs() {
    for view in breadcrumbs.arrangedSubviews {breadcrumbs.removeArrangedSubview(view);view.removeFromSuperview()}
    breadcrumbs.addArrangedSubview(ActionButton("Song"){[weak self] in self?.navigate(graph:nil)})
    if let definition {
      if let graphOrigin {breadcrumbs.addArrangedSubview(Theme.label("› \(graphOrigin)",size:11,color:Theme.muted))}
      breadcrumbs.addArrangedSubview(ActionButton("› \(definition["name"] as? String ?? "Group")"){[weak self] in self?.navigateProcessingGroup(nil)})
      let id=definition["id"] as? String
      let buses=(data["assignments"] as? [[String:Any]] ?? []).filter{$0["graph"] as? String==id}.count
      let instruments=(data["instrumentAssignments"] as? [[String:Any]] ?? []).filter{$0["graph"] as? String==id}.count
      let commands=(data["commands"] as? [[String:Any]] ?? []).filter{$0["graph"] as? String==id}.count
      scopeLabel.stringValue="Shared definition · edits update all uses · \(buses) bus / \(instruments) instrument assignments · \(commands) pattern commands"
    } else { scopeLabel.stringValue="Channels → row graphs → persistent graphs → ordinary graphs → inserts → outputs" }
      var chain=[[String:Any]](),current=processingGroupID,seen=Set<String>()
      while let id=current,seen.insert(id).inserted,let group=processingGroups.first(where:{$0["id"] as? String==id}) {chain.append(group);current=group["parent"] as? String}
      for group in chain.reversed(){let id=group["id"] as? String;breadcrumbs.addArrangedSubview(ActionButton("› \(group["name"] as? String ?? "Group")"){[weak self] in self?.navigateProcessingGroup(id)})}

  }
  func frameSelection() {
    let chosen=canvas.nodes.filter{canvas.selection.contains($0.id)}
    guard let first=chosen.first else {fit();return}
    let rect=chosen.dropFirst().reduce(first.rect){$0.union($1.rect)}.insetBy(dx:-30,dy:-30)
    frameCanvas(rect,maximumScale:1.5)
  }
  @discardableResult func recoverEmptyViewport()->Bool {
    guard window != nil,!hasDraft,!canvas.nodes.isEmpty else{return false}
    layoutSubtreeIfNeeded();scroll.layoutSubtreeIfNeeded()
    let visible=scroll.documentVisibleRect
    guard visible.width>80,visible.height>60,!canvas.nodes.contains(where:{$0.rect.intersects(visible)})else{return false}
    // Context changes can keep a scroll origin whose last visible card was
    // filtered out. Reveal one existing card without moving saved positions,
    // changing selection, or zooming out to fit a distant entire song.
    let preferred=canvas.nodes.first{$0.id==selectedID} ?? canvas.nodes.min{a,b in
      hypot(a.rect.midX-visible.midX,a.rect.midY-visible.midY)<hypot(b.rect.midX-visible.midX,b.rect.midY-visible.midY)
    }!
    frameCanvas(preferred.rect.insetBy(dx:-24,dy:-24),maximumScale:scroll.magnification)
    return true
  }
  func frameCanvas(_ rect:NSRect,maximumScale:CGFloat) {
    // Selecting a diagnostic can reveal a different inspector. Resolve that
    // layout before calculating the canvas viewport, then centre in document
    // coordinates. scrollToVisible alone can leave the far edge clipped after
    // changing magnification, especially on a node near the document boundary.
    layoutSubtreeIfNeeded()
    scroll.magnification=max(scroll.minMagnification,min(maximumScale,min(scroll.contentSize.width/max(1,rect.width),scroll.contentSize.height/max(1,rect.height))))
    scroll.layoutSubtreeIfNeeded()
    let visible=scroll.documentVisibleRect.size
    let origin=NSPoint(x:max(0,rect.midX-visible.width/2),y:max(0,rect.midY-visible.height/2))
    canvas.frame.size=NSSize(width:max(canvas.frame.width,origin.x+visible.width),height:max(canvas.frame.height,origin.y+visible.height))
    scroll.contentView.scroll(to:origin);scroll.reflectScrolledClipView(scroll.contentView)
  }
  var addDestination:(target:String?,before:String?,edge:Int?) {
    if let edge=canvas.selectedEdge,canvas.edges.indices.contains(edge),!canvas.edges[edge].modulation {
      if graphID != nil,definitionEdgeIndices.indices.contains(edge) {return(nil,nil,definitionEdgeIndices[edge])}
      if songConnections.indices.contains(edge) {
        let action=songConnections[edge],kind=action["kind"] as? String ?? ""
        if ["insert","output","master-output"].contains(kind) {return(action["source"] as? String,kind=="insert" ? action["plugin"] as? String:nil,nil)}
      }
    }
    if let bus=selectedID.flatMap({songNodeBus[$0]}) {return(bus,nil,nil)}
    // Inside a processing group, use its last insert when its descendants share
    // one bus. Do not guess a destination for a boundary spanning several buses.
    if let group=processingGroupID ?? selectedProcessingGroup?["id"] as? String {
      let members=expandedProcessingSelection([group])
      let owners=Set(members.compactMap{songNodeBus[$0]})
      if owners.count==1,let owner=owners.first,let bus=buses.first(where:{$0["id"] as? String==owner}) {
        let inserts=bus["inserts"] as? [String] ?? []
        if let last=inserts.lastIndex(where:{members.contains("plugin:"+$0)}) {
          return(owner,last+1<inserts.count ? inserts[last+1]:nil,nil)
        }
        return(owner,nil,nil)
      }
      return(nil,nil,nil)
    }
    return(filterID,nil,nil)
  }
  func freePosition(near point:NSPoint)->NSPoint {
    let origin=NSPoint(x:max(8,point.x),y:max(8,point.y))
    // Search nearby columns as well as rows. A vertical-only search sends a
    // new processor past every channel in a dense song.
    for radius in 0...canvas.nodes.count+1 {
      var candidates=[NSPoint]()
      for dx in -radius...radius {
        let dy=radius-abs(dx)
        for sign in (dy==0 ? [1]:[-1,1]) {
          let p=NSPoint(x:origin.x+CGFloat(dx)*210,y:origin.y+CGFloat(dy*sign)*125)
          if p.x>=8 && p.y>=8 {candidates.append(p)}
        }
      }
      for p in candidates.sorted(by:{hypot($0.x-origin.x,$0.y-origin.y)<hypot($1.x-origin.x,$1.y-origin.y)}) {
        let rect=NSRect(origin:p,size:NSSize(width:180,height:100)).insetBy(dx:-12,dy:-12)
        if !canvas.nodes.contains(where:{$0.rect.intersects(rect)}) {return p}
      }
    }
    return origin
  }
  func revealAddedNode() {
    guard let id=selectedID else{return}
    // Add may create an unconnected bus/source outside the focused path. Treat
    // it like an explicit hidden-branch reveal until the user changes filters.
    graphFilterState.prepare(filterContext);graphFilterState.revealed.insert(id)
    if !canvas.nodes.contains(where:{$0.id==id}){rebuild();selectedID=id;canvas.selected=id;inspect();configureConnectionInspector()}
    guard let node=canvas.nodes.first(where:{$0.id==id})else{return}
    canvas.scrollToVisible(node.rect.insetBy(dx:-16,dy:-16));window?.makeFirstResponder(canvas)
  }
  func revealCableAmount() {
    guard let index=canvas.selectedEdge,let badge=canvas.amountBadge(index) else{return}
    layoutSubtreeIfNeeded()
    // A channel's send originates after its last insert, which may be outside
    // the current viewport. Reveal the new control without fitting the whole
    // song or moving any saved cards.
    canvas.scrollToVisible(badge.insetBy(dx:-30,dy:-30))
  }
  func addEntries(connecting connection:GraphAddConnection?=nil)->[GraphAddMenu.Entry] {
    var entries=[GraphAddMenu.Entry]()
    let bus=addDestination.target
    for p in addCatalog {
      let instrument=p["isInstrument"] as? Bool == true
      if graphID != nil && instrument {continue}
      let format=p["format"] as? String ?? "",title=p["name"] as? String ?? "Plugin"
      let identity=[format,p["classID"] as? String ?? "",String(describing:p["type"] ?? 0),String(describing:p["subtype"] ?? 0),String(describing:p["manufacturer"] ?? 0)].joined(separator:":")
      entries.append(.init(id:"plugin:"+identity,title:title,detail:"\(format) · \(instrument ? "Instrument" : graphID==nil && bus==nil ? "Unconnected effect":"Effect")",keywords:instrument ? "synth sampler instrument" : "audio effect insert",payload:["kind":"plugin","descriptor":p]))
    }
    for (title,kind) in [("LFO","lfo"),("Pattern envelope","automation"),("Envelope follower","follower"),("Random","random"),("Note envelope","note-envelope"),("MIDI controller","midi"),("Amount macro","amount")] {
      entries.append(.init(id:"source:"+kind,title:title,detail:"Modulation source",keywords:"modulator control "+kind,payload:["kind":kind],unavailable:nil))
    }
    entries.append(.init(id:"visual-frame",title:"Visual frame",detail:"Labeled region · move contents together · sound unchanged",keywords:"annotation organize frame",payload:["kind":"visual-frame"]))
    entries.append(.init(id:"visual-comment",title:"Comment",detail:"Text annotation · sound unchanged",keywords:"note text annotate comment",payload:["kind":"visual-comment"]))
    entries.append(.init(id:"visual-reroute",title:"Cable reroute point",detail:"Reshape an existing cable · sound unchanged",keywords:"wire bend path reroute",payload:["kind":"visual-reroute"],unavailable:canvas.edges.contains{$0.readOnlyReason==nil} ? nil:"Connect an editable cable first"))
    entries.append(.init(id:"new-group",title:"New reusable group",detail:"Input → Output · edit its shared definition",keywords:"subgraph chain library",payload:["kind":"new-group"]))
    if graphID==nil {
      entries.append(.init(id:"existing-automation",title:"Existing automation sources…",detail:"Reveal pattern FX, envelopes or recorded points · no new lane",keywords:"existing source provenance pattern recorded envelope",payload:["kind":"existing-automation"],unavailable:rackPlugins.isEmpty ? "Add a rack processor before choosing its automation sources":nil))
      for id in provenance.ordered {if let source=provenance.sources[id]{entries.append(.init(id:id,title:provenanceTitle(source),detail:"Existing source · "+provenanceDetail(source),keywords:"existing automation source provenance",payload:["kind":"existing-reference","node":id]))}}
      for kind in ["return","group"] {entries.append(.init(id:"bus:"+kind,title:kind=="return" ? "Return bus" : "Group bus",detail:"Summing bus → Master",keywords:"send routing bus",payload:["kind":"bus","busKind":kind]))}
      for d in definitions {guard let id=d["id"] as? String else{continue};entries.append(.init(id:"group:"+id,title:d["name"] as? String ?? "Group",detail:"Reusable group \(d["number"] ?? 0) · ordinary stage",keywords:"subgraph library chain",payload:["kind":"use-group","graph":id],unavailable:bus==nil ? "Select a channel for this group":nil))}
    }
    if let c=connection {
      entries=entries.filter{item in
        let kind=item.payload["kind"] as? String ?? ""
        if c.port.modulation {return !c.output && ["lfo","automation","random","note-envelope","midi","amount"].contains(kind)}
        let effect=kind=="plugin" && (item.payload["descriptor"] as? [String:Any])?["isInstrument"] as? Bool != true
        if graphID != nil {return effect || (c.output && kind=="follower")}
        return c.output && (kind=="follower" || (c.port.number==0 && (effect || (kind=="bus" && item.payload["busKind"] as? String=="return"))))
      }
    }
    return entries
  }
  func showAdd(at point:NSPoint?=nil,connecting connection:GraphAddConnection?=nil) {
    if data.isEmpty || loading {prepareCommand {[weak self] in self?.showAdd(at:point,connecting:connection)};return}
    guard !loading,!hasDraft else {status.stringValue="Finish the current edit before adding a node";return}
    onReveal?()
    let position=freePosition(near:point ?? canvas.nodes.first(where:{$0.id==selectedID}).map{NSPoint(x:$0.rect.maxX+30,y:$0.y)} ?? NSPoint(x:canvas.visibleRect.midX,y:canvas.visibleRect.midY))
    let insertion=connection.flatMap{connectedAddDestination($0)} ?? addDestination,target=insertion.target
    var title=definition.map{"\($0["name"] ?? "Group") · shared definition, all uses"} ?? buses.first{$0["id"] as? String==target}.map{"\($0["name"] ?? "Channel") › inserts"} ?? "Song · unconnected effect"
    if let connection {title=(canvas.nodes.first{$0.id==connection.node}?.title ?? "Node")+" / "+connection.port.label+(connection.output ? " → new node":" ← new node")}
    let capturedGraph=graphID,capturedRevision=revision,capturedNode=selectedID,capturedGroup=processingGroupID
    addGeneration+=1;let generation=addGeneration
    addMenu.show(in:canvas,at:position,title:title,entries:addEntries(connecting:connection)){[weak self] item in
      guard let self,self.graphID==capturedGraph,self.processingGroupID==capturedGroup,self.revision==capturedRevision else{self?.status.stringValue="The graph changed while Add was open. Reopen Add to use the current graph.";return}
      self.addEntry(item,graph:capturedGraph,target:target,node:capturedNode,position:position,before:insertion.before,edge:connection==nil ? insertion.edge:nil,connecting:connection)
    }
    if !addCatalogLoaded {
      onRequest?("plugin.discover",["rescan":false]){[weak self] response in
        guard let self else{return}
        if let values=(response["result"] as? [String:Any])?["data"] as? [[String:Any]] {self.addCatalog=values;self.addCatalogLoaded=true}
        if self.addGeneration==generation,self.addMenu.panel?.isVisible==true {self.addMenu.replace(self.addEntries(connecting:connection))}
      }
    }
  }
  func addEntry(_ entry:GraphAddMenu.Entry,graph:String?,target:String?,node:String?,position:NSPoint,before:String?=nil,edge:Int?=nil,connecting connection:GraphAddConnection?=nil) {
    let kind=entry.payload["kind"] as? String ?? ""
    if kind=="visual-frame" || kind=="visual-comment"{addVisualRegion(comment:kind=="visual-comment",at:position);return}
    if kind=="visual-reroute"{addReroute();return}
    if kind=="existing-automation"{chooseGraphParameter(.parameterSources);return}
    if kind=="existing-reference",let id=entry.payload["node"] as? String,provenance.sources[id] != nil {
      graphFilterState.prepare(filterContext);graphFilterState.revealed.insert(id);selectedID=id;canvas.selectedEdge=nil;rebuild();canvas.selected=id;inspect();configureConnectionInspector();frameSelection();return
    }
    if kind=="new-group" {mutate("graph.create",["name":"New group"]);return}
    if kind=="bus" {
      var params:[String:Any]=["kind":entry.payload["busKind"] ?? "return","position":["x":position.x,"y":position.y]]
      if let c=connection,let owner=songNodeBus[c.node] {params["sendFrom"]=owner}
      mutate("mixer.bus.add",params){[weak self] result in
        guard let self,let id=(result["data"] as? [String:Any])?["bus"] as? String else{return}
        if connection != nil,let edge=self.canvas.edges.firstIndex(where:{$0.target==id}) {self.selectConnection(edge);self.revealCableAmount();self.focusConnectionValue(self.connectionGain)}
        else {self.selectedID=id;self.canvas.selected=id;self.inspect();self.revealAddedNode()}
      };return
    }
    if kind=="use-group",let target {mutate("graph.assign",["target":target,"graph":entry.payload["graph"] ?? ""]);return}
    if kind=="plugin",let descriptor=entry.payload["descriptor"] as? [String:Any],graph==nil {
      var params:[String:Any]=["descriptor":descriptor]
      if descriptor["isInstrument"] as? Bool != true {
        params["position"]=["x":Double(position.x),"y":Double(position.y)]
        if let target{params["target"]=target;if let before{params["before"]=before}}
        else{params["detached"]=true}
      }
      if target != nil,descriptor["isInstrument"] as? Bool != true,let parent=processingGroupID ?? selectedProcessingGroup?["id"] as? String {params["parent"]=parent}
      mutate("plugin.add",params){[weak self] result in
        guard let self,let slot=(result["data"] as? [String:Any])?["slot"] as? Int,let id=self.rackPlugins.first(where:{$0["slot"] as? Int==slot})?["id"] as? String else{return}
        self.selectedID="plugin:"+id;self.canvas.selected=self.selectedID;self.inspect();self.revealAddedNode()
      };return
    }
    guard let graph else{addSongSource(kind:kind,name:entry.title,position:position,connecting:connection);return}
    var params:[String:Any]=["graph":graph,"kind":kind,"name":entry.title,"x":max(8,position.x),"y":max(8,position.y)]
    if kind=="plugin",let descriptor=entry.payload["descriptor"] as? [String:Any] {
      params["plugin"]=descriptor.filter{["format","name","path","classID","type","subtype","manufacturer"].contains($0.key)}
      let edges=definition?["audio"] as? [[String:Any]] ?? []
      if let edge {params["insertEdge"]=edge}
      else if connection==nil,let node,edges.filter({$0["source"] as? String==node && ($0["output"] as? Int ?? 0)==0}).count==1 {params["insertAfter"]=node}
    }
    if let c=connection {
      let real=realPort(c.node,c.port.number,output:c.output,modulation:c.port.modulation)
      var endpoint:[String:Any]=["node":real.node,"port":real.number,"output":c.output,"modulation":c.port.modulation]
      if c.port.modulation {endpoint["base"]=modulationBase(node:real.node,parameter:real.number)}
      params["connect"]=endpoint
    }
    let create:([String:Any])->Void={[weak self] params in self?.mutate("graph.node.add",params){[weak self] _ in
      guard let self else{return};self.revealAddedNode()
      if let c=connection,c.port.modulation,let source=self.selectedID,let edge=self.canvas.edges.firstIndex(where:{$0.source==source && $0.target==c.node && $0.input==c.port.number}) {
        self.selectConnection(edge);self.revealCableAmount();self.focusConnectionValue(self.maximum)
      }
    }}
    if let c=connection,c.port.modulation {
      let real=realPort(c.node,c.port.number,output:c.output,modulation:true)
      withRecipeParameterMode(node:real.node,parameter:real.number){quantized in var ready=params;var endpoint=ready["connect"] as? [String:Any] ?? [:];endpoint["quantized"]=quantized;ready["connect"]=endpoint;create(ready)}
    }else{create(params)}
  }
}
