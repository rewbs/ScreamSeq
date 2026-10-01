import AppKit

extension SignalGraphEditor {
  /// Activity links navigate by document identities, never by a projected edge index.
  /// A graph read already in flight owns the eventual callback; late catalogue
  /// reads may improve labels but must not replace the requested selection.
  func inspectSongModulation(source sourceID:String?,plugin:String,parameter:UInt32,editConnection:Bool) {
    guard !hasDraft else{status.stringValue="Finish or cancel the current graph edit before inspecting modulation";return}
    let document=projectionDocument,context=viewContext
    let reveal:()->Void={[weak self] in
      guard let self,!self.hasDraft,(document.isEmpty || self.projectionDocument==document),self.viewContext==context else{return}
      guard self.rackPlugins.contains(where:{$0["id"] as? String==plugin})else{self.status.stringValue="This modulation target is no longer in the song";return}
      if let sourceID,!self.songSources.contains(where:{$0["id"] as? String==sourceID}){self.status.stringValue="This modulation source is no longer in the song";return}
      self.navigate(graph:nil);self.clearNodeFilters()
      self.exposedParameters["plugin:"+plugin]=parameter;self.rebuild()
      let indices=self.songConnections.indices.filter { index in
        let edge=self.songConnections[index]
        return edge["kind"] as? String=="modulation" && edge["plugin"] as? String==plugin && (edge["parameter"] as? NSNumber)?.uint32Value==parameter && (sourceID==nil || edge["source"] as? String==sourceID)
      }
      if editConnection,sourceID != nil,let index=indices.first {
        self.selectConnection(index);self.revealCableAmount();self.focusConnectionValue(self.maximum)
      } else {
        let target=indices.first.map{self.canvas.edges[$0].target} ?? self.canvas.nodes.first{$0.id=="plugin:"+plugin}?.id ?? self.boundaryPorts.first{$0.value.node=="plugin:"+plugin && !$0.key.output && $0.key.modulation && $0.value.number==parameter}?.key.node
        let sourceKeys=indices.map{self.canvas.edges[$0].source}
        let primary=sourceID.map{"source:"+$0} ?? target
        let visible=Set(self.canvas.nodes.map(\.id))
        var selection=Set(sourceKeys+[target].compactMap{$0});if let primary{selection.insert(primary)};selection.formIntersection(visible)
        self.selectedID=primary;self.canvas.selectedEdge=nil;self.canvas.selectNodes(selection,primary:primary);self.inspect();self.configureConnectionInspector();self.frameSelection()
        self.status.stringValue=sourceID==nil ? "Selected parameter target and its modulation sources":"Selected modulation source · edit its settings here"
      }
      self.loadSongParameterCatalogs()
    }
    deferredGraphCommand=reveal
    if !loading {if onRequest != nil{load()}else{deferredGraphCommand=nil;reveal()}}
  }
  var songSources:[[String:Any]]{data["songSources"] as? [[String:Any]] ?? []}
  var songModulation:[[String:Any]]{data["songModulation"] as? [[String:Any]] ?? []}
  func songSource(_ key:String?)->[String:Any]? {
    guard graphID==nil,let key,key.hasPrefix("source:")else{return nil}
    let id=String(key.dropFirst(7));return songSources.first{$0["id"] as? String==id}
  }
  func songParameter(_ plugin:String,_ parameter:UInt32)->[String:Any]? {
    (portCatalogs["plugin:"+plugin]?["parameters"] as? [[String:Any]] ?? []).first{($0["id"] as? NSNumber)?.uint32Value==parameter}
  }
  func loadSongParameterCatalogs() {
    guard graphID==nil,!loading,!catalogLoading,onRequest != nil else{return}
    let wanted=Set(songModulation.compactMap{$0["plugin"] as? String}+exposedParameters.keys.filter{$0.hasPrefix("plugin:")}.map{String($0.dropFirst(7))}+[selectedID.flatMap{songNodePlugin[$0]}].compactMap{$0})
    guard let plugin=rackPlugins.compactMap({$0["id"] as? String}).first(where:{wanted.contains($0)&&portCatalogs["plugin:"+$0]==nil&&catalogFailures["plugin:"+$0] != revision})else{return}
    let key="plugin:"+plugin,document=projectionDocument,generation=catalogGeneration,capturedRevision=revision
    catalogLoading=true
    requestGraph("plugin.parameters.get",["plugin":plugin],document:document){[weak self] response in
      guard let self,self.projectionDocument==document,self.catalogGeneration==generation else{return};self.catalogLoading=false
      if let values=(response["result"] as? [String:Any])?["data"] as? [[String:Any]] {self.portCatalogs[key]=["parameters":values];self.catalogFailures[key]=nil;if self.graphID==nil && !self.hasDraft && !self.loading{self.rebuild()}}
      else{self.catalogFailures[key]=capturedRevision}
      self.loadSongParameterCatalogs()
    }
  }
  func withSongParameter(_ plugin:String,_ parameter:UInt32,_ action:@escaping([String:Any])->Void) {
    if let value=songParameter(plugin,parameter){action(value);return}
    let document=projectionDocument,revision=self.revision
    requestGraph("plugin.parameters.get",["plugin":plugin],document:document){[weak self] response in
      guard let self,self.projectionDocument==document,self.revision==revision,self.graphID==nil else{return}
      guard let values=(response["result"] as? [String:Any])?["data"] as? [[String:Any]],let value=values.first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter})else{self.status.stringValue="Parameter unavailable; refresh the plugin controls";return}
      self.portCatalogs["plugin:"+plugin]=["parameters":values];action(value)
    }
  }
  func withSongQuantization(_ parameter:[String:Any],alreadyChosen:Bool=false,_ action:@escaping(Bool)->Void) {
    guard parameter["writable"] as? Bool != false else{status.stringValue="This parameter is read-only";return}
    guard parameter["canSlide"] as? Bool==false else{action(false);return}
    let step=parameter["step"] as? Double ?? 0,range=(parameter["max"] as? Double ?? 0)-(parameter["min"] as? Double ?? 0)
    guard step.isFinite,range.isFinite,step>0,range>0,step<=range else{status.stringValue="This parameter has no supported quantization step";return}
    if alreadyChosen{action(true);return}
    let steps=ceil(range/step),count=steps<100000 ? String(Int(steps)+1):"Many"
    let document=projectionDocument,context=viewContext
    chooseTarget(title:"\(parameter["name"] as? String ?? "Parameter") uses discrete values",entries:[.init(id:"quantized",title:"Connect using discrete values",detail:"\(count) values · summed modulation rounds to the nearest step",keywords:"quantized stepped discrete")]) {[weak self] _ in
      guard let self,self.projectionDocument==document,self.viewContext==context else{return};action(true)
    }
  }
  func focusSongModulation(source:String,plugin:String,parameter:UInt32) {
    guard let index=songConnections.firstIndex(where:{$0["kind"] as? String=="modulation" && $0["source"] as? String==source && $0["plugin"] as? String==plugin && ($0["parameter"] as? NSNumber)?.uint32Value==parameter})else{return}
    selectConnection(index);revealCableAmount();focusConnectionValue(maximum)
  }
  func addSongSource(kind:String,name:String,position:NSPoint,connecting connection:GraphAddConnection?=nil) {
    var spec:[String:Any]=["kind":kind,"name":name,"x":position.x,"y":position.y]
    if let c=connection,!c.port.modulation {let real=realPort(c.node,c.port.number,output:c.output,modulation:false);guard c.output,kind=="follower",let tap=songFollowerTap(real.node,output:real.number)else{return};spec.merge(tap){_,new in new}}
    let create:([String:Any]?)->Void={[weak self] connect in
      guard let self else{return};var params:[String:Any]=["source":spec];if let connect{params["connect"]=connect}
      self.mutate("graph.song.source.add",params){[weak self] result in
        guard let self,let node=(result["data"] as? [String:Any])?["node"] as? String else{return}
        if self.processingGroupID != nil {self.navigateProcessingGroup(nil)}
        self.selectedID="source:"+node;self.canvas.selected=self.selectedID;self.graphFilterState.revealed.insert("source:"+node);self.rebuild();self.inspect();self.revealAddedNode()
        if let connect,let plugin=connect["plugin"] as? String,let parameter=(connect["parameter"] as? NSNumber)?.uint32Value{self.focusSongModulation(source:node,plugin:plugin,parameter:parameter)}
      }
    }
    if let c=connection,c.port.modulation {
      let real=realPort(c.node,c.port.number,output:false,modulation:true)
      guard !c.output,let plugin=songNodePlugin[real.node]else{return}
      withSongParameter(plugin,real.number){[weak self] value in guard let self else{return};self.withSongQuantization(value){quantized in create(["plugin":plugin,"parameter":real.number,"quantized":quantized])}}
    }else{create(nil)}
  }
  func songFollowerTap(_ key:String,output:UInt32)->[String:Any]? {
    if let plugin=songNodePlugin[key]{return ["audioBus":"","audioPlugin":plugin,"output":output,"preFader":false]}
    guard output==0,let bus=songNodeBus[key] else{status.stringValue="Choose a channel output or a plugin audio output for the follower";return nil}
    return ["audioBus":bus,"audioPlugin":"","output":0,"preFader":false]
  }
  @discardableResult func connectSongControl(_ a:String,_ b:String,out:UInt32,input:UInt32,modulation:Bool)->Bool {
    guard graphID==nil else{return false}
    if modulation {
      guard let source=songSource(a)?["id"] as? String,let plugin=songNodePlugin[b]else{status.stringValue="Connect a song modulation source to an exposed plugin parameter";return true}
      if songModulation.contains(where:{$0["source"] as? String==source && $0["plugin"] as? String==plugin && ($0["parameter"] as? NSNumber)?.uint32Value==input}){status.stringValue="This modulation source is already connected to that parameter";return true}
      withSongParameter(plugin,input){[weak self] value in guard let self else{return};self.withSongQuantization(value){[weak self] quantized in self?.mutate("graph.song.modulation.set",["source":source,"plugin":plugin,"parameter":input,"minimum":0,"maximum":0,"quantized":quantized]){[weak self] _ in self?.focusSongModulation(source:source,plugin:plugin,parameter:input)}}}
      return true
    }
    if let follower=songSource(b),follower["kind"] as? String=="follower",let node=follower["id"] {
      if let tap=songFollowerTap(a,output:out){mutate("graph.song.source.update",["node":node,"source":tap])};return true
    }
    return false
  }
  func configureSongSourceInspector() {
    guard let source=songSource(selectedID)else{return}
    var entries:[(String,String)]=[("All notes","")]
    for bus in buses where bus["kind"] as? String=="track" {
      let id=bus["id"] as? String ?? ""
      entries.append(("Channel · "+busLabel(bus),"bus:"+id))
    }
    let instruments=data["instruments"] as? [[String:Any]] ?? []
    for instrument in instruments {
      let name=instrument["name"] as? String ?? "",id=instrument["id"] as? String ?? ""
      entries.append(("Instrument · "+name,"instrument:"+id))
    }
    let channel=source["noteTarget"] as? String ?? "",instrument=source["noteInstrument"] as? String ?? ""
    picker(sourceScope,entries,select:!channel.isEmpty ? "bus:"+channel:!instrument.isEmpty ? "instrument:"+instrument:"")
    followerPreFader.state=source["preFader"] as? Bool==true ? .on:.off;followerPreFader.isEnabled = !(source["audioBus"] as? String ?? "").isEmpty
  }
  @objc func changeSongSourceScope() {
    guard let node=songSource(selectedID)?["id"]else{return};let key=chosen(sourceScope) ?? ""
    mutate("graph.song.source.update",["node":node,"source":["noteTarget":key.hasPrefix("bus:") ? String(key.dropFirst(4)):"","noteInstrument":key.hasPrefix("instrument:") ? String(key.dropFirst(11)):""]])
  }
  @objc func changeFollowerTap() {guard let node=songSource(selectedID)?["id"]else{return};mutate("graph.song.source.update",["node":node,"source":["preFader":followerPreFader.state == .on]])}
  func updateSongControl(_ index:Int,source a:String,target b:String) {
    guard songConnections.indices.contains(index)else{return};let old=songConnections[index]
    if old["kind"] as? String=="follower-input" {
      guard let follower=songSource(b),follower["id"] as? String==old["node"] as? String else{status.stringValue="This input belongs to its follower. Drag the source handle to choose the audio it follows.";return}
      let from=realPort(a,UInt32(outputPort.stringValue) ?? 0,output:true,modulation:false)
      guard var tap=songFollowerTap(from.node,output:from.number)else{return};if !(tap["audioBus"] as? String ?? "").isEmpty{tap["preFader"]=old["preFader"] ?? false};mutate("graph.song.source.update",["node":old["node"] ?? "","source":tap]);return
    }
    guard old["kind"] as? String=="modulation",let number=UInt32(parameter.stringValue),let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),lo.isFinite,hi.isFinite else{return}
    let from=realPort(a,0,output:true,modulation:true),to=realPort(b,number,output:false,modulation:true)
    guard let source=songSource(from.node)?["id"] as? String,let plugin=songNodePlugin[to.node]else{status.stringValue="Choose a modulation source and plugin parameter";return}
    let enabled=connectionEnabled.state == .on,chosenQuantized=connectionQuantized.state == .on
    withSongParameter(plugin,to.number){[weak self] value in
      guard let self else{return}
      let targetChanged=plugin != old["plugin"] as? String || to.number != (old["parameter"] as? NSNumber)?.uint32Value
      if !targetChanged,value["canSlide"] as? Bool==false,!chosenQuantized {self.status.stringValue="This parameter requires discrete values; keep quantization enabled";self.connectionQuantized.state = .on;return}
      self.withSongQuantization(value,alreadyChosen:!targetChanged && chosenQuantized){[weak self] stepped in
        self?.mutate("graph.song.modulation.set",["source":source,"plugin":plugin,"parameter":to.number,"minimum":lo,"maximum":hi,"enabled":enabled,"quantized":stepped || (!targetChanged && chosenQuantized),"replace":old.filter{["source","plugin","parameter"].contains($0.key)}])
      }
    }
  }
}
