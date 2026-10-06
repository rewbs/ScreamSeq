import AppKit

/// Persisted shortcut/API identities must not depend on a menu's label, parent,
/// current selection, or the common ContextAction.invoke selector.
enum GraphCommand:String,CaseIterable {
  case add,parent,fit,traceSilence,findOverload,clearOverloads,scope,spectrum,observeCopy
  case openPlugin,bypass,listen,stopListening,frameSelection,showPattern,newGroup,cloneGroup
  case sourceAutomation,sourceLFO,sourceFollower,sourceRandom,sourceNote,sourceMIDI,sourceAmount
  case patch,advancedPatch,portAdd,portSources,portTargets,moveInsertChain,cableSource,cableTarget,backToCable,cut,detach,deleteHeal,arrange,zoomIn,zoomOut,reload
  case groupSelection,ungroup,exportGroup,groupDryPaths,revealHidden,makeIndependent,restoreNoteAssignment,noteSampleMapping,noteAssignPlugin
  case parameterAutomate,parameterActivity,parameterExpose,parameterValue,parameterSources,parameterLastTouched,returnFromLastTouched,editProvenance,hideProvenance,returnFromSource,nextProvenancePage
  case visualFrame,visualComment,visualReroute,visualCollapse,visualRemove
  case copySelection,cutSelection,pasteSelection,duplicateSelection,presetSave,presetLoad
  case findNode,openNode,removeNode,arrangeSelection,reconnectMain,cutMasterOutput
  var id:String {"graph."+rawValue}
  // Canvas-only defaults must yield to the same persisted overrides used by
  // the command palette. Otherwise removing/remapping M still bypasses audio.
  func usesCanvasDefault(defaults:UserDefaults = .standard)->Bool {
    let singles=defaults.dictionary(forKey:"workspaceShortcuts") ?? [:]
    let sequences=defaults.dictionary(forKey:"workspaceSequences") ?? [:]
    return singles[id]==nil && sequences[id]==nil
  }
  static func migrateLegacyBindings(defaults:UserDefaults = .standard) {
    let names:[GraphCommand:[String]]=[
      .add:["Add node…"],.parent:["Parent graph"],.fit:["Fit graph"],
      .traceSilence:["Trace silence"],.findOverload:["Find next overload"],.clearOverloads:["Clear overload indicators"],
      .scope:["Scope selected signal"],.spectrum:["Spectrum of selected signal"],
      .openPlugin:["Open plugin interface"],.bypass:["Toggle plugin bypass"],.listen:["Listen here"],.stopListening:["Stop listening"],
      .frameSelection:["Frame selection"],.showPattern:["Show in pattern"],.newGroup:["New subgraph…"],.cloneGroup:["Clone subgraph"],
      .patch:["Patch by keyboard…"],.cut:["Cut cables…"],.detach:["Detach and reconnect"],.deleteHeal:["Delete and reconnect"],
      .arrange:["Arrange nodes"],.zoomIn:["Zoom in"],.zoomOut:["Zoom out"],.reload:["Reload graph"]]
    // These are exact historical menu-path IDs, not a suffix/name heuristic:
    // other panels can have identically named actions with different meaning.
    let sources:[GraphCommand:String]=[.sourceAutomation:"Pattern envelope",.sourceLFO:"LFO",.sourceFollower:"Audio follower",.sourceRandom:"Random",.sourceNote:"Note envelope",.sourceMIDI:"MIDI controller",.sourceAmount:"Amount"]
    for key in ["workspaceShortcuts","workspaceSequences"] {
      guard var bindings=defaults.dictionary(forKey:key) else{continue}
      var changed=false
      for command in allCases {
        let paths=(names[command] ?? []) + (sources[command].map{["Add modulation source / \($0)"]} ?? [])
        let aliases=["Graph", "Audio & modulation graph / More…"].flatMap{prefix in paths.map{"\(prefix) / \($0)/invoke"}}
        for alias in aliases where bindings[alias] != nil {
          if bindings[command.id]==nil {bindings[command.id]=bindings[alias]}
          bindings.removeValue(forKey:alias);changed=true
        }
      }
      if changed {defaults.set(bindings,forKey:key)}
    }
  }
  func item(_ title:String,key:String="",modifiers:NSEvent.ModifierFlags=[],reason:String?=nil,run:@escaping()->Void)->ContextAction {
    let item=ContextAction(title,id:id,key:key,modifiers:modifiers,enabled:reason==nil,run:run)
    item.toolTip=reason;return item
  }
}

extension SignalGraphEditor {
  func withVisibleNode(title:String,action:@escaping(String)->Void) {
    if data.isEmpty || loading {prepareCommand{[weak self] in self?.withVisibleNode(title:title,action:action)};return}
    if let id=canvas.selected ?? selectedID,canvas.nodes.contains(where:{$0.id==id}){action(id);return}
    chooseVisibleNode(title:title,action:action)
  }
  func chooseVisibleNode(title:String="Find node",action:((String)->Void)?=nil) {
    if data.isEmpty || loading {prepareCommand{[weak self] in self?.chooseVisibleNode(title:title,action:action)};return}
    chooseTarget(title:title,entries:canvas.nodes.map{node in
      let ambiguous=canvas.nodes.contains{$0.id != node.id && $0.title==node.title && $0.detail==node.detail}
      return .init(id:node.id,title:node.title,detail:node.detail+(ambiguous ? " · "+node.id:""),keywords:node.kind+" "+(node.role ?? "")+" "+node.id)
    }){[weak self] id in
      guard let self,self.canvas.nodes.contains(where:{$0.id==id})else{return}
      self.canvas.selectedEdge=nil;self.manualConnection=false;self.selectedID=id;self.canvas.selected=id
      self.inspect();self.configureConnectionInspector();self.frameSelection();self.window?.makeFirstResponder(self.canvas)
      action?(id)
    }
  }
  func prepareCommand(_ action:@escaping()->Void) {
    // One deferred user intention, shared with any already-running graph read.
    // Reveal can start that read synchronously through workspace selection.
    deferredGraphCommand=action;onReveal?()
    guard deferredGraphCommand != nil,!loading else{return}
    if !data.isEmpty {deferredGraphCommand=nil;action()} else {load()}
  }
  func chooseTarget(title:String,entries:[GraphAddMenu.Entry],choose:@escaping(String)->Void) {
    guard !entries.isEmpty else {status.stringValue="No compatible targets for \(title.lowercased())";return}
    onReveal?()
    let capturedRevision=revision
    targetMenu.show(in:canvas,at:NSPoint(x:scroll.contentView.bounds.midX,y:scroll.contentView.bounds.midY),title:title,entries:entries,verb:"chooses") {[weak self] entry in
      guard let self,self.revision==capturedRevision else {self?.status.stringValue="The song changed. Choose the target again.";return}
      choose(entry.id)
    }
  }
  func withSongPlugin(title:String,action:@escaping(String)->Void) {
    if data.isEmpty || loading {prepareCommand {[weak self] in self?.withSongPlugin(title:title,action:action)};return}
    if graphID==nil,let key=canvas.selected ?? selectedID,let id=songNodePlugin[key] {action(id);return}
    chooseTarget(title:title,entries:rackPlugins.compactMap { plugin in
      guard let id=plugin["id"] as? String else {return nil}
      let owner=buses.first{($0["inserts"] as? [String] ?? []).contains(id)}?["name"] as? String ?? "Song rack"
      return .init(id:id,title:plugin["name"] as? String ?? "Plugin",detail:owner,keywords:plugin["format"] as? String ?? "")
    },choose:action)
  }
  func withProcessingGroup(title:String,createThenAct:Bool=true,action:@escaping(String)->Void) {
    if data.isEmpty || loading {prepareCommand {[weak self] in self?.withProcessingGroup(title:title,createThenAct:createThenAct,action:action)};return}
    if let graphID {action(graphID);return}
    var entries=definitions.compactMap { definition -> GraphAddMenu.Entry? in
      guard let id=definition["id"] as? String else{return nil}
      return .init(id:id,title:definition["name"] as? String ?? "Group",detail:"Group \(definition["number"] ?? 0) · shared definition",keywords:"group subgraph")
    }
    entries.append(.init(id:"create",title:"New processing group…",detail:"Create a reusable group for this action",keywords:"new create"))
    chooseTarget(title:title,entries:entries) {[weak self] id in
      guard let self else{return}
      if id=="create" {
        self.mutate("graph.create",["name":"New subgraph"]) {[weak self] _ in
          if createThenAct,let id=self?.graphID {action(id)}
        }
      } else {self.navigate(graph:id);if self.graphID==id {action(id)}}
    }
  }
  func openSelectedPlugin() {
    if let graph=graphID {
      let processors=nodes.filter{$0["kind"] as? String=="plugin"}
      if let selectedID,processors.contains(where:{$0["id"] as? String==selectedID}){openNode(selectedID);return}
      chooseTarget(title:"Open group processor",entries:processors.compactMap{node in
        guard let id=node["id"] as? String else{return nil}
        return .init(id:id,title:node["name"] as? String ?? "Plugin",detail:"Shared definition",keywords:"interface effect")
      }){[weak self] id in guard self?.graphID==graph else{return};self?.openNode(id)}
      return
    }
    withSongPlugin(title:"Open plugin interface") {[weak self] id in self?.onPlugin?(id)}
  }
}
