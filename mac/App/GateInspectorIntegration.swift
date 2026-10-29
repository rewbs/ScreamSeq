import AppKit
extension AppController {
  @objc func showGateInspector(){
    guard !busy,model.editable else{return}
    if workspaceReturnPoints["gate"] == nil {workspaceReturnPoints["gate"]=patternView.navigation}
    workspace?.show("gate");if !gateInspector.hasDraft && !gateInspector.pending {followWorkspacePanel("gate",force:true)}
  }
  func installGateInspector(){
    gateInspector.onContext={[weak self] in guard let self else{return(PatternModel([:]),0,0,3)};return(self.model,self.patternView.cursorRow,self.patternView.cursorChannel,self.patternView.column)}
    gateInspector.onRequest={[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    gateInspector.onDefinition={[weak self] pattern,row,channel,column in
      guard let self,!self.busy,self.model.pattern==pattern else{return}
      self.patternView.navigate(EditorNavigation(pattern:pattern,row:row,channel:channel,column:3+2*column,following:false),clearSelection:true)
      self.window.makeKeyAndOrderFront(nil);self.window.makeFirstResponder(self.patternView)
    }
  }
}
