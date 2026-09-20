import AppKit
extension AppController {
  @objc func showPatternPerformance() {
    guard !busy,model.editable else{return}
    if let patternPerformanceWindow,let editor=patternPerformanceWindow.contentView as? PatternPerformanceEditor {
      editor.capture();patternPerformanceWindow.makeKeyAndOrderFront(nil);return
    }
    let editor=PatternPerformanceEditor(frame:.zero)
    editor.onContext = {[weak self] in
      guard let self else{return (PatternModel([:]),0,0,5)}
      return (self.model,self.patternView.cursorRow,self.patternView.cursorChannel,self.patternView.column)
    }
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:700,height:780),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Pattern effects";win.minSize=NSSize(width:620,height:600);win.isReleasedWhenClosed=false;win.contentView=editor
    patternPerformanceWindow=win;win.center();win.makeKeyAndOrderFront(nil);editor.capture()
  }
  func clearNativeEffect(row:Int,channel:Int,column:Int) {
    guard !busy,model.editable else{return}
    let expected=session.automationRevision,pattern=model.pattern
    handleAutomation("pattern.performance.get",params:["pattern":pattern]){[weak self] reply in
      guard let self,let result=reply["result"] as? [String:Any],result["revision"] as? String==expected,
        let data=result["data"] as? [String:Any],let commands=data["commands"] as? [[String:Any]] else{return}
      let replacement=commands.filter{!($0["channel"] as? Int==channel && $0["column"] as? Int==column && ($0["position"] as? Int ?? 0)/65536==row)}
        .map{$0.filter{$0.key != "track"}}
      self.handleAutomation("pattern.performance.set",params:["pattern":pattern,"commands":replacement,"expectedRevision":expected]){[weak self] reply in
        if let failure=reply["error"] as? [String:Any] {self?.statusLabel.stringValue=failure["message"] as? String ?? "Command could not be cleared"}
      }
    }
  }
}
