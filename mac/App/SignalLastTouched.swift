import AppKit

struct GraphLastTouchedReturn {
  let document:String,graph:String?,group:String?,origin:String?,target:String?
  let view:GraphViewState,revealed:Set<String>,cable:GraphCableLocation?
}

extension SignalGraphEditor {
  // Custom editors need not support pointer hit-testing: their accepted native
  // parameter gesture supplies a stable rack identity through this read API.
  func showLastTouchedParameter() {
    if data.isEmpty || loading {prepareCommand{[weak self] in self?.showLastTouchedParameter()};return}
    guard !hasDraft else{status.stringValue="Finish the current graph edit before following a parameter";return}
    let document=projectionDocument,context=viewContext
    lastTouchedReadGeneration+=1;let generation=lastTouchedReadGeneration
    // A just-completed vendor gesture can precede the next passive graph read.
    load{[weak self] in
      guard let self,self.projectionDocument==document,self.viewContext==context,self.lastTouchedReadGeneration==generation else{return}
      self.readLastTouchedGraphParameter()
    }
  }
  private func readLastTouchedGraphParameter() {
    let document=projectionDocument,context=viewContext,capturedRevision=revision,generation=lastTouchedReadGeneration
    let current:()->Bool={[weak self] in
      guard let self else{return false}
      return self.projectionDocument==document && self.viewContext==context && self.revision==capturedRevision && self.lastTouchedReadGeneration==generation && !self.hasDraft
    }
    status.stringValue="Finding the last touched plugin parameter…"
    func data(_ reply:[String:Any])->Any? {
      guard let result=reply["result"] as? [String:Any] else {
        status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Parameter unavailable";return nil
      }
      guard result["revision"] as? String==capturedRevision else{status.stringValue="The song changed. Follow the parameter again.";return nil}
      return result["data"]
    }
    requestGraph("automation.target.get",[:],document:document){[weak self] reply in
      guard let self,current(),let payload=data(reply) as? [String:Any] else{return}
      guard let target=payload["target"] as? [String:Any] else {
        self.status.stringValue="Move a song-rack plugin knob, then choose Show last touched plugin parameter. Reusable template editors use their host parameter list.";return
      }
      guard target["available"] as? Bool==true,let plugin=target["plugin"] as? String,
        let raw=target["parameter"] as? NSNumber,CFGetTypeID(raw) != CFBooleanGetTypeID(),
        let parameter=UInt32(exactly:raw.doubleValue),self.rackPlugins.contains(where:{$0["id"] as? String==plugin}) else {
        self.status.stringValue=target["reason"] as? String ?? "The touched plugin or parameter is unavailable";return
      }
      self.requestGraph("plugin.parameters.get",["plugin":plugin],document:document){[weak self] reply in
        guard let self,current(),let values=data(reply) as? [[String:Any]] else{return}
        guard let value=values.first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter}) else{
          self.status.stringValue="The plugin no longer exposes the touched parameter";return
        }
        self.lastTouchedReturn=GraphLastTouchedReturn(document:document,graph:self.graphID,group:self.processingGroupID,origin:self.graphOrigin,target:self.graphTarget,
          view:GraphViewState(origin:self.scroll.contentView.bounds.origin,scale:self.scroll.magnification,selection:self.selectedID,filter:self.filterID,search:self.nodeSearch.stringValue,category:self.nodeCategory.indexOfSelectedItem),
          revealed:self.graphFilterState.revealed,cable:self.canvas.selectedEdge.flatMap{self.canvas.edges.indices.contains($0) ? self.cableLocation(self.canvas.edges[$0]):nil})
        if self.graphID != nil{self.navigate(graph:nil)}
        self.portCatalogs["plugin:"+plugin]=["parameters":values]
        self.performGraphParameter(.parameterExpose,graph:nil,processor:plugin,parameter:parameter)
        self.rackControls.focusParameter(parameter)
        self.frameSelection();self.window?.makeFirstResponder(self.canvas)
        let name=value["name"] as? String ?? "Parameter"
        self.status.stringValue="\(name) · parameter socket exposed · drag a source to connect · Back from last touched parameter returns to your previous view"
      }
    }
  }
  func returnFromLastTouchedParameter() {
    guard let back=lastTouchedReturn,back.document==projectionDocument,!hasDraft else{status.stringValue="The previous graph view is no longer available";return}
    if let graph=back.graph,!definitions.contains(where:{$0["id"] as? String==graph}){status.stringValue="The previous subgraph was removed";return}
    navigate(graph:back.graph,origin:back.origin,target:back.target)
    if let group=back.group {
      guard processingGroups.contains(where:{$0["id"] as? String==group})else{lastTouchedReturn=nil;status.stringValue="The previous processing group was removed";return}
      navigateProcessingGroup(group)
    }
    filterID=back.view.filter;nodeSearch.stringValue=back.view.search;nodeCategory.selectItem(at:back.view.category)
    picker(filter,[("All channels","")]+buses.map{(busLabel($0),$0["id"] as? String ?? "")},select:filterID)
    graphFilterState.prepare(filterContext);graphFilterState.revealed=back.revealed;selectedID=back.view.selection;canvas.selectedEdge=nil;rebuild()
    if let cable=back.cable,let index=canvas.edges.firstIndex(where:{cableLocation($0)==cable}){selectConnection(index)}else{inspect();configureConnectionInspector()}
    scroll.magnification=back.view.scale;canvas.scroll(back.view.origin);lastTouchedReturn=nil;window?.makeFirstResponder(canvas)
  }
}
