import AppKit

extension AppController {
  func installParameterActivity() {
    parameterActivity.onRequest = {[weak self] method,p,reply in self?.handleAutomation(method,params:p,reply:reply)}
    parameterActivity.onContext = {[weak self] in self?.model ?? PatternModel([:])}
    parameterActivity.onRecordedNames = {[weak self] plugin,parameter in
      guard let self else{return(nil,nil)}
      let name=self.model.nativePlugins.first{$0["instanceID"] as? String==plugin}?["name"] as? String
      let catalog=self.signalGraphEditor.portCatalogs["plugin:"+plugin]?["parameters"] as? [[String:Any]] ?? []
      let graphName=catalog.first{($0["id"] as? NSNumber)?.intValue==parameter}?["name"] as? String
      let inspected=self.model.nativePlugins.indices.contains(self.pluginEditor.selected) && self.model.nativePlugins[self.pluginEditor.selected]["instanceID"] as? String==plugin
      let inspectorName=inspected ? self.pluginEditor.parameterValues.first{($0["id"] as? NSNumber)?.intValue==parameter}?["name"] as? String:nil
      return(name,graphName ?? inspectorName)
    }
    parameterActivity.onCapture = {[weak self] in guard let self,!self.busy else{return};self.handleAutomation("transport.play",params:["pattern":self.model.pattern,"loop":false,"expectedRevision":self.session.automationRevision]){[weak self] reply in if let error=reply["error"] as? [String:Any]{self?.statusLabel.stringValue=error["message"] as? String ?? "Could not capture pattern"}}}
    parameterActivity.onOpen = {[weak self] source in self?.openParameterSource(source)}
    pluginEditor.onActivity = {[weak self] in guard let self,self.model.nativePlugins.indices.contains(self.pluginEditor.selected),let plugin=self.model.nativePlugins[self.pluginEditor.selected]["instanceID"] as? String else{return};self.showParameterActivity(plugin:plugin)}
    workspaceAutomation.onActivity = {[weak self] plugin,parameter in self?.showParameterActivity(plugin:plugin,parameter:parameter)}
    signalGraphEditor.pluginControls.onActivity = {[weak self] graph,node,parameter in guard let self else{return};let copy=self.signalGraphEditor.copyObservation.selected;self.prepareGraphReturn(to:"parameterActivity");self.workspace?.show("parameterActivity",focus:true);self.parameterActivity.inspect(graph:graph,node:node,parameter:Int(parameter),copy:copy)}
  }
  func prepareGraphReturn(to panel:String) {
    signalGraphEditor.rememberPanelReturn()
    workspace?.panels[panel]?.onBack={[weak self] in self?.signalGraphEditor.returnToProvenance()}
  }
  func showParameterActivity(plugin:String,parameter:Int?=nil){workspace?.show("parameterActivity",focus:true);parameterActivity.inspect(plugin:plugin,parameter:parameter)}
  func openParameterSource(_ source:[String:Any]) {
    guard !busy else{return}
    window.makeKeyAndOrderFront(nil)
    let kind=source["kind"] as? String ?? "baseline",plugin=source["plugin"] as? String ?? ""
    if kind=="recorded",!plugin.isEmpty,let parameter=source["parameter"] as? Int{workspace?.show("parameterActivity",focus:true);parameterActivity.inspectRecorded(plugin:plugin,parameter:parameter);return}
    if kind=="envelope" {
      guard let pattern=source["pattern"] as? Int,let parameter=source["parameter"] as? Int,let slot=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==plugin}),model.patterns.contains(where:{$0["index"] as? Int==pattern}) else{return}
      if workspaceAutomation.hasDraft || workspaceAutomation.loading {statusLabel.stringValue="Wait for the current envelope edit to finish before opening another source.";return}
      if model.pattern != pattern {model.pattern=pattern;refreshPattern()}
      pluginEditor.selected=slot;workspaceAutomationModel=model;workspaceAutomation.selectedPluginID=plugin;workspaceAutomation.pluginIndex=slot;workspaceAutomation.parameterID=parameter;workspaceAutomation.search.stringValue=""
      workspace?.show("automation",focus:true);workspaceAutomation.load();return
    }
    if kind=="pattern-set" || kind=="pattern-slide" {
      guard let pattern=source["pattern"] as? Int,let channel=source["channel"] as? Int,channel<model.channels,model.patterns.contains(where:{$0["index"] as? Int==pattern}) else{return}
      if model.pattern != pattern{model.pattern=pattern;refreshPattern()}
      var navigation=patternView.navigation;navigation.pattern=pattern;navigation.row=(source["position"] as? Int ?? 0)/65536;navigation.channel=channel;navigation.column=3+2*(source["column"] as? Int ?? 0);navigation.following=false
      patternView.navigate(navigation,clearSelection:true);window.makeFirstResponder(patternView);patternView.revealCursor();statusLabel.stringValue="Parameter command selected · Return edits its binding, value and duration";return
    }
    if kind=="graph-command" {if let pattern=source["pattern"] as? Int,model.patterns.contains(where:{$0["index"] as? Int==pattern}),model.pattern != pattern{model.pattern=pattern;refreshPattern()};openGraphCommand(target:source["target"] as? String,column:source["column"] as? Int ?? 0,row:(source["position"] as? Int ?? 0)/65536);return}
    if source["scope"] as? String=="song",!plugin.isEmpty,let parameter=source["parameter"] as? NSNumber,parameter.uint64Value<=UInt32.max,
       let node=source["node"] as? String ?? (kind=="graph" ? "":nil) {
      guard !signalGraphEditor.hasDraft else{statusLabel.stringValue="Finish the current graph edit before opening another source.";return}
      workspace?.show("graph",focus:true)
      signalGraphEditor.inspectSongModulation(source:node.isEmpty ? nil:node,plugin:plugin,parameter:parameter.uint32Value,editConnection:source["editConnection"] as? Bool == true)
      return
    }
    if let graph=source["graph"] as? String,graph != "n0" {
      guard !signalGraphEditor.hasDraft else{statusLabel.stringValue="Finish the current graph edit before opening another source.";return}
      signalGraphEditor.pendingModulationFocus=source["editConnection"] as? Bool == true ? source["connection"] as? Int:nil
      signalGraphEditor.canvas.selectedEdge=nil;signalGraphEditor.filterID=nil;signalGraphEditor.nodeSearch.stringValue="";signalGraphEditor.nodeCategory.selectItem(at:0);signalGraphEditor.graphID=graph;signalGraphEditor.selectedID=source["node"] as? String
      workspace?.show("graph",focus:true);signalGraphEditor.load();return
    }
    if !plugin.isEmpty {inspectWorkspacePlugin(plugin);if let id=source["parameter"] as? Int,let value=pluginEditor.parameterValues.first(where:{$0["id"] as? Int==id}){pluginEditor.search.stringValue=value["name"] as? String ?? "";pluginEditor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:pluginEditor.search))}}
  }
}
