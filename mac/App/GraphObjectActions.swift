import AppKit

extension SignalGraphEditor {
  func appendSelectedObjectActions(to menu:NSMenu) {
    guard let node=canvas.selected,canvas.selectedEdge==nil else{return}
    let isPlugin=songNodePlugin[node] != nil || (graphID != nil && nodes.contains{$0["id"] as? String==node && $0["kind"] as? String=="plugin"})
    if isPlugin {
      menu.addItem(GraphCommand.openPlugin.item("Open plugin interface",key:"\r"){[weak self] in self?.openNode(node)})
    }
    // Sources and groups deserve the same direct comparison action as plugins.
    // Keep unavailable reasons on the complete catalog, not on unrelated objects.
    if bypassUnavailableReason==nil {
      menu.addItem(GraphCommand.bypass.item(bypassActionTitle,key:"m"){[weak self] in self?.toggleSelectedBypass()})
    }
    if isPlugin || bypassUnavailableReason==nil {menu.addItem(.separator())}
  }
  func makeIndependentUse() {
    guard !hasDraft else{status.stringValue="Finish the current edit before making a use independent";return}
    if data.isEmpty || loading{prepareCommand{[weak self] in self?.makeIndependentUse()};return}
    let selected=canvas.selected ?? selectedID
    let graph=graphID ?? selected.flatMap{songNodeGraph[$0]}
    guard let graph else {
      withProcessingGroup(title:"Choose a shared definition",createThenAct:false){[weak self] id in self?.chooseIndependentUse(graph:id)};return
    }
    if graphID==nil,let selected {
      if let instrument=instrumentForSongNode(selected),let target=instrument["id"] as? String {makeIndependentUse(graph:graph,target:target,scope:"instrument");return}
      if let target=songNodeBus[selected] {makeIndependentUse(graph:graph,target:target,scope:"channel");return}
    }
    chooseIndependentUse(graph:graph)
  }
  func chooseIndependentUse(graph:String) {
    let assignments=data["assignments"] as? [[String:Any]] ?? [],commands=data["commands"] as? [[String:Any]] ?? []
    let targets=Set((assignments+commands).filter{$0["graph"] as? String==graph}.compactMap{$0["target"] as? String})
    var lookup=[String:(String,String)](),entries=[GraphAddMenu.Entry]()
    for target in targets.sorted(){let id="channel:"+target;lookup[id]=(target,"channel");entries.append(.init(id:id,title:busLabels[target] ?? target,detail:"Make this channel’s ordinary and pattern uses independent together",keywords:target+" channel row persistent ordinary"))}
    for use in data["instrumentAssignments"] as? [[String:Any]] ?? [] where use["graph"] as? String==graph {
      guard let target=use["target"] as? String else{continue};let id="instrument:"+target,instrument=sampleInstruments.first{$0["id"] as? String==target};lookup[id]=(target,"instrument")
      entries.append(.init(id:id,title:"I\(instrument?["index"] ?? "") · \(instrument?["name"] as? String ?? "Instrument")",detail:"Make this instrument graph independent; other instruments keep the shared definition",keywords:target+" instrument"))
    }
    chooseTarget(title:"Choose a use to make independent",entries:entries){[weak self] id in guard let use=lookup[id]else{return};self?.makeIndependentUse(graph:graph,target:use.0,scope:use.1)}
  }
  func makeIndependentUse(graph:String,target:String,scope:String) {
    mutate("graph.makeIndependent",["graph":graph,"target":target,"scope":scope]){[weak self] result in
      guard let self,let fresh=(result["data"] as? [String:Any])?["graph"] as? String,!fresh.isEmpty else{return}
      self.navigate(graph:fresh,origin:scope=="channel" ? self.busLabels[target]:"Instrument",target:target)
      self.status.stringValue=scope=="channel" ? "This channel’s uses now share their own independent copy · other channels unchanged":"This instrument now uses an independent graph · other instruments unchanged"
    }
  }
}
