import AppKit
import UniformTypeIdentifiers

struct GraphPresetTarget:Equatable {let graph:String?,plugin:String,name:String}
extension SignalGraphEditor {
  func withPresetTarget(_ saving:Bool,action:@escaping(GraphPresetTarget)->Void) {
    guard !hasDraft else{status.stringValue="Finish the current edit before choosing a preset";return}
    if loading || data.isEmpty{prepareCommand{[weak self] in self?.withPresetTarget(saving,action:action)};return}
    if let graphID {
      if let selectedID,let node=nodes.first(where:{$0["id"] as? String==selectedID && $0["kind"] as? String=="plugin"}){action(.init(graph:graphID,plugin:selectedID,name:node["name"] as? String ?? "Plugin"));return}
      chooseTarget(title:"Choose an effect for \(saving ? "saving":"loading") a preset",entries:nodes.filter{$0["kind"] as? String=="plugin"}.compactMap{node in guard let id=node["id"] as? String else{return nil};return .init(id:id,title:node["name"] as? String ?? "Plugin",detail:"Shared definition · all uses",keywords:id)}){[weak self] id in guard let self,let node=self.nodes.first(where:{$0["id"] as? String==id})else{return};action(.init(graph:graphID,plugin:id,name:node["name"] as? String ?? "Plugin"))}
    } else {withSongPlugin(title:"Choose a plugin preset target"){[weak self] id in guard let self,let plugin=self.rackPlugins.first(where:{$0["id"] as? String==id})else{return};action(.init(graph:nil,plugin:id,name:plugin["name"] as? String ?? "Plugin"))}}
  }
  func showGraphPreset(saving:Bool) {withPresetTarget(saving){[weak self] target in
    guard let self,let window=self.window,window.attachedSheet==nil else{return}
    let revision=self.revision,document=self.projectionDocument,type=UTType(filenameExtension:"screamseq-preset",conformingTo:.data) ?? .data
    if saving {
      let panel=NSSavePanel();panel.allowedContentTypes=[type];panel.canCreateDirectories=true;panel.title="Save plugin preset";panel.message="Save the configured sound for \(target.name). Routing and modulation stay in the song.";panel.nameFieldStringValue=target.name.replacingOccurrences(of:"/",with:"-")+".screamseq-preset"
      panel.beginSheetModal(for:window){[weak self] response in guard response == .OK,let url=panel.url else{return};self?.saveGraphPreset(target,path:url.path,name:url.deletingPathExtension().lastPathComponent,revision:revision,document:document)}
    } else {
      let panel=NSOpenPanel();panel.allowedContentTypes=[type];panel.allowsMultipleSelection=false;panel.canChooseDirectories=false;panel.title="Load plugin preset";panel.message="Load \(target.name) settings\(target.graph==nil ? "":" in all uses of this shared definition"). Routing and bypass stay unchanged; Undo restores the previous sound."
      panel.beginSheetModal(for:window){[weak self] response in guard response == .OK,let url=panel.url else{return};self?.loadGraphPreset(target,path:url.path,revision:revision,document:document)}
    }
  }}
  func presetParams(_ target:GraphPresetTarget)->[String:Any] {target.graph.map{["graph":$0,"node":target.plugin]} ?? ["plugin":target.plugin]}
  func saveGraphPreset(_ target:GraphPresetTarget,path:String,name:String,revision:String,document:String) {
    guard self.revision==revision,self.projectionDocument==document else{status.stringValue="The song changed; choose the preset target again";return}
    var p=presetParams(target);p.merge(["path":path,"name":name,"overwrite":true,"expectedRevision":revision]){_,new in new}
    requestGraph(target.graph==nil ? "plugin.preset.save":"graph.plugin.preset.save",p,document:document){[weak self] reply in guard let self else{return};guard (reply["result"] as? [String:Any])?["data"] is [String:Any] else{self.showExternalFailure((reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed; retry this operation");return};self.status.stringValue="Saved preset · \(name)"}
  }
  func loadGraphPreset(_ target:GraphPresetTarget,path:String,revision:String,document:String) {
    guard self.revision==revision,self.projectionDocument==document else{status.stringValue="The song changed; choose the preset target again";return}
    requestGraph("plugin.preset.inspect",["path":path],document:document){[weak self] reply in
      guard let self,let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let fileRevision=data["presetRevision"] as? String else{self?.showExternalFailure((reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed; retry this operation");return}
      guard self.revision==revision,result["revision"] as? String==revision else{self.status.stringValue="The song changed while reading the preset; choose it again";return}
      var p=self.presetParams(target);p.merge(["path":path,"expectedPresetRevision":fileRevision]){_,new in new}
      self.mutate(target.graph==nil ? "plugin.preset.load":"graph.plugin.preset.load",p){[weak self] _ in self?.status.stringValue="Loaded preset · \(data["name"] as? String ?? target.name) · Undo restores the previous sound"}
    }
  }
}
