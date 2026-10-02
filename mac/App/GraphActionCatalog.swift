import AppKit

extension SignalGraphEditor {
  var bypassSelection:String? {canvas.selected ?? selectedID}
  var selectedMuteSource:[String:Any]? {
    guard let key=bypassSelection else{return nil}
    let node=graphID==nil ? songSource(key):nodes.first{$0["id"] as? String==key}
    guard let node,["lfo","follower","random","note-envelope","midi","amount","automation"].contains(node["kind"] as? String ?? "")else{return nil};return node
  }
  var bypassUnavailableReason:String? {
    if canvas.selection.count>1{return "Select one processor or mixer bus; batch bypass is not supported"}
    if canvas.selectedEdge != nil{return "Select a processor or bus; use cable properties to mute a connection"}
    if selectedMuteSource != nil || selectedProcessingGroup != nil{return nil}
    if graphID != nil {
      guard let key=bypassSelection,nodes.contains(where:{$0["id"] as? String==key && $0["kind"] as? String=="plugin"})else{return "Select an effect or modulation source inside this definition; whole-group bypass is not supported yet"}
      return nil
    }
    guard let key=bypassSelection else{return nil}
    if let plugin=songNodePlugin[key],rackPlugins.contains(where:{$0["id"] as? String==plugin}){return nil}
    if buses.contains(where:{$0["id"] as? String==key}){return nil}
    if selectedProcessingGroup != nil || songNodeGraph[key] != nil{return "Bypass for a processing group or subgraph copy is not supported yet"}
    if instrumentForSongNode(key) != nil{return "Sample-instrument mute is not supported from the graph yet"}
    if key.hasPrefix("source:"){return "The modulation source no longer exists; reload the graph and select it again"}
    return "Select a current rack processor or mixer bus to bypass or mute"
  }
  var bypassActionTitle:String {
    guard let key=bypassSelection else{return "Bypass / mute processor…"}
    if let group=selectedProcessingGroup{return (group["bypass"] as? Bool==true ? "Enable group":"Bypass group")+(graphID==nil ? "":" in all uses")}
    if let source=selectedMuteSource{return (source["muted"] as? Bool==true ? "Unmute source":"Mute source")+(graphID==nil ? "":" in all uses")}
    if graphID != nil,let node=nodes.first(where:{$0["id"] as? String==key && $0["kind"] as? String=="plugin"}) {
      return (node["plugin"] as? [String:Any])?["bypass"] as? Bool==true ? "Enable effect in all uses":"Bypass effect in all uses"
    }
    if let id=songNodePlugin[key],let plugin=rackPlugins.first(where:{$0["id"] as? String==id}) {
      let inactive=plugin["bypass"] as? Bool==true
      return plugin["isInstrument"] as? Bool==true ? (inactive ? "Unmute instrument":"Mute instrument"):(inactive ? "Enable effect":"Bypass effect")
    }
    if let bus=buses.first(where:{$0["id"] as? String==key}){return bus["mute"] as? Bool==true ? "Unmute bus":"Mute bus"}
    return "Bypass / mute selection"
  }
  func toggleSelectedBypass() {
    if data.isEmpty || loading {
      let context=viewContext
      prepareCommand{[weak self] in guard let self,self.viewContext==context else{return};self.toggleSelectedBypass()};return
    }
    if let reason=bypassUnavailableReason{status.stringValue=reason;return}
    if let group=selectedProcessingGroup{setProcessingGroupBypass(group,bypass:!(group["bypass"] as? Bool ?? false));return}
    if let source=selectedMuteSource,let node=source["id"] {mutate("graph.source.mute",["graph":graphID as Any? ?? NSNull(),"node":node,"muted":!(source["muted"] as? Bool ?? false)]);return}
    if let key=bypassSelection {
      if let graph=graphID,let node=nodes.first(where:{$0["id"] as? String==key && $0["kind"] as? String=="plugin"}) {
        mutate("graph.plugin.bypass",["graph":graph,"node":key,"bypass":!((node["plugin"] as? [String:Any])?["bypass"] as? Bool ?? false)]);return
      }
      if let id=songNodePlugin[key],let plugin=rackPlugins.first(where:{$0["id"] as? String==id}) {
        mutate("plugin.bypass",["plugin":id,"bypass":!(plugin["bypass"] as? Bool ?? false)])
      }else if let bus=buses.first(where:{$0["id"] as? String==key}){
        mutate("mixer.bus.set",["bus":key,"mute":!(bus["mute"] as? Bool ?? false)])
      }
      return
    }
    // With no selected object, the catalogue action may explicitly ask for a
    // rack target. A selected recipe/source/group never falls through here.
    withSongPlugin(title:"Choose a processor to bypass or mute") {[weak self] id in
      guard let self,let plugin=self.rackPlugins.first(where:{$0["id"] as? String==id})else{return}
      self.mutate("plugin.bypass",["plugin":id,"bypass":!(plugin["bypass"] as? Bool ?? false)])
    }
  }
  // A parameter action is always discoverable in ⌘K, including when no card is
  // selected. Resolve the processor and stable parameter before invoking the
  // same bridge used by its inline menu; a row index is never an edit target.
  func appendParameterCommands(to menu:NSMenu) {
    let parameters=NSMenu(title:"Parameters");parameters.autoenablesItems=false
    parameters.addItem(GraphCommand.parameterLastTouched.item("Show last touched plugin parameter"){[weak self] in self?.showLastTouchedParameter()})
    parameters.addItem(GraphCommand.returnFromLastTouched.item("Back from last touched parameter",reason:lastTouchedReturn==nil ? "Show a last-touched parameter first":nil){[weak self] in self?.returnFromLastTouchedParameter()})
    for (command,title) in [(GraphCommand.parameterValue,"Edit parameter value…"),
                            (.parameterExpose,"Expose parameter port…"),
                            (.parameterAutomate,graphID==nil ? "Automate parameter…":"Connect modulation to parameter…"),
                            (.parameterActivity,"Inspect effective parameter and sources…"),
                            (.parameterSources,"Show existing automation sources in graph…")] {
      parameters.addItem(command.item(title,reason:command == .parameterSources && graphID != nil ? "Existing pattern/recorded sources belong to song rack processors; return to Song":nil){[weak self] in self?.chooseGraphParameter(command)})
    }
    parameters.addItem(GraphCommand.editProvenance.item("Edit selected automation source…",reason:selectedProvenance==nil ? "Select an existing automation source card or Sets base wire":nil){[weak self] in if let source=self?.selectedProvenance{self?.openProvenanceSource(source)}})
    parameters.addItem(GraphCommand.nextProvenancePage.item("Next page of existing sources",reason:provenance.total>64 ? nil:"All sources for the inspected parameter fit on one page"){[weak self] in self?.nextProvenancePage()})
    parameters.addItem(GraphCommand.hideProvenance.item("Hide existing automation sources",reason:provenance.target==nil ? "No parameter source view is open":nil){[weak self] in self?.hideParameterProvenance()})
    parameters.addItem(GraphCommand.returnFromSource.item("Back to graph",reason:provenance.hasReturn || panelReturn != nil ? nil:"Open automation or parameter activity from the graph first"){[weak self] in self?.returnToProvenance()})
    ContextActions.appendMenu(parameters,to:menu)
  }
  func chooseGraphParameter(_ command:GraphCommand) {
    if data.isEmpty || loading {prepareCommand{[weak self] in self?.chooseGraphParameter(command)};return}
    guard !hasDraft else{status.stringValue="Finish the current graph edit before choosing a parameter.";return}
    let graph=graphID
    if let graph {
      let processors=nodes.filter{$0["kind"] as? String=="plugin"}
      if let selectedID,processors.contains(where:{$0["id"] as? String==selectedID}) {
        chooseGraphParameter(command,graph:graph,processor:selectedID);return
      }
      chooseTarget(title:"Choose a processor",entries:processors.compactMap{p in
        guard let id=p["id"] as? String else{return nil}
        return .init(id:id,title:p["name"] as? String ?? "Plugin",detail:"Shared definition",keywords:"parameter effect")
      }) {[weak self] id in self?.chooseGraphParameter(command,graph:graph,processor:id)}
    } else {
      withSongPlugin(title:"Choose a processor"){[weak self] id in self?.chooseGraphParameter(command,graph:nil,processor:id)}
    }
  }
  func chooseGraphParameter(_ command:GraphCommand,graph:String?,processor:String) {
    let document=projectionDocument,context=viewContext,capturedRevision=revision
    let method=graph==nil ? "plugin.parameters.get":"graph.plugin.get"
    let params:[String:Any]=graph.map{["graph":$0,"node":processor]} ?? ["plugin":processor]
    requestGraph(method,params,document:document){[weak self] reply in
      guard let self,self.projectionDocument==document,self.viewContext==context,self.revision==capturedRevision else{return}
      guard let result=reply["result"] as? [String:Any] else{self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Parameters unavailable";return}
      let payload=result["data"],catalog=graph==nil ? payload as? [[String:Any]]:(payload as? [String:Any])?["parameters"] as? [[String:Any]]
      let entries=(catalog ?? []).compactMap{p -> GraphAddMenu.Entry? in
        guard let id=(p["id"] as? NSNumber)?.uint32Value else{return nil}
        let writable=p["writable"] as? Bool==true
        let reason = !writable && (command == .parameterAutomate || command == .parameterValue) ? "Read-only parameter · use Inspect effective value to view it":nil
        return .init(id:String(id),title:p["name"] as? String ?? "Parameter",detail:"\(p["value"] ?? "") \(p["unitLabel"] as? String ?? "") · ID \(id)",keywords:String(id),payload:p,unavailable:reason)
      }
      self.chooseTarget(title:"Choose a parameter",entries:entries){[weak self] id in
        guard let self,self.projectionDocument==document,self.graphID==graph,let parameter=UInt32(id) else{return}
        self.performGraphParameter(command,graph:graph,processor:processor,parameter:parameter)
      }
    }
  }
  func performGraphParameter(_ command:GraphCommand,graph:String?,processor:String,parameter:UInt32) {
    guard graphID==graph,!hasDraft else{return}
    if let graph {
      if command == .parameterSources {status.stringValue="Pattern and recorded automation target song rack processors. Choose one in the song graph.";return}
      if command == .parameterActivity{pluginControls.onActivity?(graph,processor,parameter);return}
      revealParameterOwner(processor)
      if command == .parameterValue {pluginControls.parametersView.focusParameter(parameter)}
      else {pluginControls.onParameter?(parameter)}
    } else {
      if command == .parameterSources {revealParameterOwner("plugin:"+processor);showParameterProvenance(plugin:processor,parameter:parameter);return}
      if command == .parameterActivity{rackControls.onActivity?(processor,parameter);return}
      if command == .parameterAutomate{rackControls.onAutomate?(processor,parameter);return}
      let key="plugin:"+processor
      revealParameterOwner(key)
      if command == .parameterValue {rackControls.focusParameter(parameter)}
      else {rackControls.onExpose?(processor,parameter);revealAddedNode()}
    }
  }
  private func revealParameterOwner(_ key:String){
    let owner=processingGroups.first{($0["nodes"] as? [String] ?? []).contains(key)}?["id"] as? String
    if processingGroupID != owner{navigateProcessingGroup(owner)}
    filterID=nil;filter.selectItem(at:0);nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
    graphFilterState.prepare(filterContext);graphFilterState.revealed.insert(key)
    selectedID=key;canvas.selected=key;canvas.selectedEdge=nil;rebuild();inspect();configureConnectionInspector();revealAddedNode()
  }
}
