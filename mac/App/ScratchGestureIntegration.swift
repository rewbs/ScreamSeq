import AppKit

extension AppController {
  func scratchPatternTarget()->ScratchPatternTarget? {
    guard !busy,model.editable,model.rows>0 else{return nil}
    let column=max(0,patternView.effectColumn),channel=patternView.cursorChannel,row=patternView.cursorRow
    guard column<model.effectCount(channel) else{return nil}
    return ScratchPatternTarget(pattern:model.pattern,row:row,channel:channel,column:column,rows:model.rows,rowsPerBeat:model.rowsPerBeat,revision:session.automationRevision,command:model.nativeCommand(row,channel,column)?.editCommand)
  }
  @objc func showScratchGestures(){
    guard !busy,model.editable else{return}
    let target=scratchPatternTarget()
    if let win=scratchGestureWindow,let editor=win.contentView as? ScratchGestureEditor,!editor.invalidated {
      if !editor.hasPendingEdits,!editor.pending {editor.target=target;editor.targetLabel.stringValue=target?.title ?? "Choose a pattern FX cell";editor.load(select:target?.gesture)}
      else {editor.status.stringValue="Finish the retained phrase edit first; Current cursor updates its pattern destination."}
      win.makeKeyAndOrderFront(nil);return
    }
    let editor=ScratchGestureEditor(target:target)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    editor.onCurrentTarget = {[weak self] in self?.scratchPatternTarget()}
    editor.onReturn = {[weak self,weak editor] target in
      guard let self,self.session.automationRevision.split(separator:":").first==target.revision.split(separator:":").first else{editor?.status.stringValue="The original song is no longer open.";return}
      guard self.model.patterns.contains(where:{$0["index"] as? Int==target.pattern}) else{editor?.status.stringValue="The original pattern no longer exists.";return}
      if self.model.pattern != target.pattern{self.model.pattern=target.pattern;self.refreshPattern()}
      guard target.row<self.model.rows,target.channel<self.model.channels,target.column<self.model.effectCount(target.channel) else{editor?.status.stringValue="The original pattern cell no longer exists.";return}
      self.patternView.navigate(EditorNavigation(pattern:target.pattern,row:target.row,channel:target.channel,column:3+2*target.column,following:false),clearSelection:true)
      self.window.makeKeyAndOrderFront(nil);self.window.makeFirstResponder(self.patternView)
    }
    editor.onPreview = {[weak self,weak editor] target in
      guard let self,!self.busy,self.session.automationRevision.split(separator:":").first==target.revision.split(separator:":").first else{return}
      let settings:[String:Any]=["pattern":target.pattern,"startRow":target.row,"endRow":target.rows,"cursorRow":target.row,"loop":self.playbackLoop,"order":self.selectedOrder]
      self.perform("Playing scratch arrangement…",refresh:false,{try self.session.playRegion(settings)},completion:{editor?.status.stringValue="Playing from \(target.title) · Space stops playback"})
    }
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:1000,height:720),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Scratch phrases";win.minSize=NSSize(width:880,height:620);win.isReleasedWhenClosed=false;win.contentView=editor
    scratchGestureWindow=win;win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
