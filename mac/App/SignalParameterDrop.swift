import AppKit

struct GraphParameterDragContext {
  let source:String,document:String,view:[String],graph:String?,processor:String
  let controls:GraphRackControls
}
extension SignalGraphEditor {
  func offerAudioFollower(_ source:String,_ target:String,output:UInt32,parameter:UInt32,position:NSPoint) {
    let from=realPort(source,output,output:true,modulation:false),to=realPort(target,parameter,output:false,modulation:true)
    let document=projectionDocument,context=viewContext,graph=graphID,capturedRevision=revision
    let current:()->Bool={[weak self] in guard let self else{return false};return self.projectionDocument==document && self.viewContext==context && self.graphID==graph && self.revision==capturedRevision}
    let tap:[String:Any]?
    if graph==nil {
      guard songNodePlugin[to.node] != nil else{status.stringValue="Drop audio on a plugin parameter input";return}
      tap=songFollowerTap(from.node,output:from.number);guard tap != nil else{return}
    } else {
      guard nodes.contains(where:{$0["id"] as? String==to.node && $0["kind"] as? String=="plugin"}) else{return}
      tap=nil
    }
    let label=canvas.nodes.first{$0.id==source}?.title ?? "Audio"
    let location=freePosition(near:position)
    chooseTarget(title:"Turn audio into parameter motion",entries:[.init(id:"follower",title:"Insert envelope follower",detail:"Follow \(label) · starts at zero depth · one Undo step",keywords:"audio envelope follower modulation")]) {[weak self] _ in
      guard let self,current()else{return}
      if let graph {
        self.withRecipeParameterMode(node:to.node,parameter:to.number){[weak self] quantized in
          guard let self,current()else{return}
          let connection:[String:Any]=["node":to.node,"port":to.number,"output":false,"modulation":true,"base":self.modulationBase(node:to.node,parameter:to.number),"quantized":quantized]
          self.mutate("graph.node.add",["graph":graph,"kind":"follower","name":label+" follower","x":location.x,"y":location.y,"audioInput":["node":from.node,"port":from.number],"connect":connection]) {[weak self] result in
            guard let self,let node=(result["data"] as? [String:Any])?["node"] as? String else{return}
            self.revealAddedNode()
            if let index=self.canvas.edges.firstIndex(where:{$0.source==node && $0.modulation && $0.input==to.number}){self.selectConnection(index);self.revealCableAmount();self.focusConnectionValue(self.maximum)}
          }
        }
      } else if let plugin=self.songNodePlugin[to.node],let tap {
        self.withSongParameter(plugin,to.number){[weak self] value in
          guard let self,current()else{return}
          self.withSongQuantization(value){[weak self] quantized in
            guard let self,current()else{return}
            var spec:[String:Any]=["kind":"follower","name":label+" follower","x":location.x,"y":location.y];spec.merge(tap){_,new in new}
            self.mutate("graph.song.source.add",["source":spec,"connect":["plugin":plugin,"parameter":to.number,"quantized":quantized]]) {[weak self] result in
              guard let self,let node=(result["data"] as? [String:Any])?["node"] as? String else{return}
              if self.processingGroupID != nil{self.navigateProcessingGroup(nil)}
              self.graphFilterState.revealed.insert("source:"+node);self.rebuild();self.revealAddedNode();self.focusSongModulation(source:node,plugin:plugin,parameter:to.number)
            }
          }
        }
      }
    }
  }
  func configureParameterDrop() {
    canvas.onPrepareParameterDrag={[weak self] source in self?.beginParameterDrop(source:source) ?? false}
    canvas.onParameterDragHover={[weak self] point in self?.previewParameterDrop(atWindowPoint:point)}
    canvas.onParameterDrop={[weak self] point in
      guard let self else{return}
      let target=self.parameterDropContext?.controls.parameterDropTarget(atWindowPoint:point)
      self.completeParameterDrop(target)
    }
    canvas.onEndParameterDrag={[weak self] in self?.endParameterDrop()}
  }
  // Keep the current inspector during a source drag, so its knobs remain real
  // drop targets. A normal click still selects that source when released.
  func beginParameterDrop(source:String)->Bool {
    guard !loading,parameterDropContext==nil else{return false}
    let controls:GraphRackControls
    if graphID==nil {
      guard songSource(source) != nil,!rackControls.isHidden else{return false};controls=rackControls
    } else {
      guard nodes.contains(where:{$0["id"] as? String==source && !["input","output","plugin"].contains($0["kind"] as? String ?? "")}),!pluginControls.isHidden else{return false};controls=pluginControls.parametersView
    }
    guard let processor=controls.identity else{return false}
    parameterDropContext = .init(source:source,document:projectionDocument,view:viewContext,graph:graphID,processor:processor,controls:controls)
    return true
  }
  func parameterDropReason(_ target:GraphParameterDropTarget,recipe:Bool)->String? {
    let p=target.metadata
    guard p["writable"] as? Bool==true else{return "This parameter is read-only"}
    let low=p["min"] as? Double ?? 0,high=p["max"] as? Double ?? 1
    guard low.isFinite,high.isFinite,high>low,(high-low).isFinite else{return "This parameter has no usable value range"}
    if p["canSlide"] as? Bool==false {
      let step=p["step"] as? Double ?? 0
      if !step.isFinite || step<=0 || step>high-low{return "This parameter has no supported quantization step"}
    }
    return nil
  }
  func previewParameterDrop(atWindowPoint point:NSPoint)->String? {
    guard let drag=parameterDropContext else{return nil}
    guard let target=drag.controls.parameterDropTarget(atWindowPoint:point),target.processor==drag.processor else{drag.controls.highlightParameterDrop(nil);return "Drag onto a visible parameter in this graph’s inspector · Esc cancels"}
    let hint=parameterDropReason(target,recipe:drag.graph != nil) ?? ((target.metadata["canSlide"] as? Bool==false ? "Release to choose discrete modulation for ":"Release to connect at zero depth to ")+(target.metadata["name"] as? String ?? "parameter"))
    drag.controls.highlightParameterDrop(target,hint:hint);return hint
  }
  func endParameterDrop(){parameterDropContext?.controls.highlightParameterDrop(nil);parameterDropContext=nil}
  func completeParameterDrop(_ target:GraphParameterDropTarget?) {
    guard let drag=parameterDropContext else{return};endParameterDrop()
    guard projectionDocument==drag.document,viewContext==drag.view,graphID==drag.graph,drag.controls.identity==drag.processor else{status.stringValue="The inspected processor changed; modulation drag cancelled";return}
    guard let target,target.processor==drag.processor else{status.stringValue="Drop on a visible parameter in this graph’s inspector · source position unchanged";return}
    guard let current=drag.controls.values.first(where:{($0["id"] as? NSNumber)?.uint32Value==target.parameter})else{status.stringValue="Parameter no longer available; modulation drag cancelled";return}
    let currentTarget=GraphParameterDropTarget(processor:target.processor,parameter:target.parameter,metadata:current)
    if let reason=parameterDropReason(currentTarget,recipe:drag.graph != nil){status.stringValue=reason;return}
    if let graph=drag.graph {
      let prefix=graph+"/";guard drag.processor.hasPrefix(prefix)else{return};let processor=String(drag.processor.dropFirst(prefix.count))
      guard nodes.contains(where:{$0["id"] as? String==drag.source}),nodes.contains(where:{$0["id"] as? String==processor && $0["kind"] as? String=="plugin"})else{status.stringValue="Source or processor no longer exists";return}
      if let i=(definition?["modulation"] as? [[String:Any]] ?? []).firstIndex(where:{$0["source"] as? String==drag.source && $0["target"] as? String==processor && ($0["parameter"] as? NSNumber)?.uint32Value==target.parameter}) {
        let original=(definition?["audio"] as? [[String:Any]] ?? []).count+i
        if let visible=definitionEdgeIndices.firstIndex(of:original){selectConnection(visible);focusConnectionValue(maximum)}
        return
      }
      var catalog=portCatalogs[processor] ?? [:];catalog["parameters"]=drag.controls.values;portCatalogs[processor]=catalog
      exposedParameters[processor]=target.parameter
      connectPorts(drag.source,processor,out:0,input:target.parameter,modulation:true)
    } else {
      guard songSource(drag.source) != nil,rackPlugins.contains(where:{$0["id"] as? String==drag.processor})else{status.stringValue="Source or processor no longer exists";return}
      portCatalogs["plugin:"+drag.processor]=["parameters":drag.controls.values]
      exposedParameters["plugin:"+drag.processor]=target.parameter
      _=connectSongControl(drag.source,"plugin:"+drag.processor,out:0,input:target.parameter,modulation:true)
    }
  }
}
