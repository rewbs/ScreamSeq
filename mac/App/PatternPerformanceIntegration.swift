import AppKit
extension AppController {
  @objc func showPatternPerformance() {
    guard !busy,model.editable else{return}
    openPatternPerformance()
  }
  func openPatternPerformance(context:(PatternModel,Int,Int,Int)?=nil,kind:String?=nil,target:(plugin:String,parameter:Int)?=nil) {
    guard !busy,model.editable else{return}
    let editor=PatternPerformanceEditor(frame:.zero)
    editor.requestedKind=kind;editor.requestedTarget=target
    var initial=context
    editor.onContext = {[weak self] in
      if let captured=initial {initial=nil;return captured}
      guard let self else{return (PatternModel([:]),0,0,5)}
      return (self.model,self.patternView.cursorRow,self.patternView.cursorChannel,self.patternView.column)
    }
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    patternPerformanceWindow?.close()
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:700,height:780),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Pattern effects";win.minSize=NSSize(width:620,height:600);win.isReleasedWhenClosed=false;win.contentView=editor
    patternPerformanceWindow=win;win.center();win.makeKeyAndOrderFront(nil);editor.capture()
  }
  func setEffectColumns(channel:Int,count:Int) {
    guard !busy,model.editable,(1...8).contains(count) else{return}
    handleAutomation("pattern.effects.set",params:["pattern":model.pattern,"columns":[["channel":channel,"count":count]],"expectedRevision":session.automationRevision]){[weak self] reply in
      guard let self else{return}
      if let error=reply["error"] as? [String:Any] {self.statusLabel.stringValue=error["message"] as? String ?? "Could not change columns";return}
      self.patternView.cursorChannel=channel
      if count>0 {self.patternView.column=2+count*2;self.patternView.revealCursor()}
      self.statusLabel.stringValue="\(count) FX columns · ? finds effects · Return edits a cell"
    }
  }
  func clearNativeEffect(row:Int,channel:Int,column:Int) {
    guard !busy,model.editable else{return}
    handleAutomation("pattern.effect.set",params:["pattern":model.pattern,"row":row,"channel":channel,"column":column,"command":NSNull(),"expectedRevision":session.automationRevision]){[weak self] reply in
      if let failure=reply["error"] as? [String:Any] {self?.statusLabel.stringValue=failure["message"] as? String ?? "Command could not be cleared"}
    }
  }
  func setTrackerEffect(row:Int,channel:Int,column:Int,effect:Int,parameter:Int) {
    guard !busy,model.editable else{return}
    let command:Any=effect==0 && parameter==0 ? NSNull() : ["kind":"tracker","effect":effect,"parameter":parameter]
    patternView.deferringEffectKeys=true
    handleAutomation("pattern.effect.set",params:["pattern":model.pattern,"row":row,"channel":channel,"column":column,"command":command,"expectedRevision":session.automationRevision]){[weak self] reply in
      if let failure=reply["error"] as? [String:Any] {self?.statusLabel.stringValue=failure["message"] as? String ?? "Effect could not be changed"}
      else {self?.statusLabel.stringValue="FX \(column+1) updated"}
      self?.patternView.finishEffectKeys(success:reply["error"]==nil)
    }
  }
}
