import AppKit

extension AppController {
  func scratchPatternTarget()->ScratchPatternTarget? {
    guard !busy,model.editable,model.rows>0 else{return nil}
    let column=max(0,patternView.effectColumn),channel=patternView.cursorChannel,row=patternView.cursorRow
    guard column<model.effectCount(channel),
      let patternID=model.patterns.first(where:{$0["index"] as? Int==model.pattern})?["id"] as? String,
      let trackID=model.tracks.first(where:{$0["index"] as? Int==channel})?["id"] as? String else{return nil}
    return ScratchPatternTarget(pattern:model.pattern,row:row,channel:channel,column:column,rows:model.rows,rowsPerBeat:model.rowsPerBeat,patternID:patternID,trackID:trackID,revision:session.automationRevision,command:model.nativeCommand(row,channel,column)?.editCommand)
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
      guard let self,!self.busy else{return}
      guard let location=target.location(in:self.model) else{editor?.status.stringValue="The captured pattern cell no longer exists in this song. Current cursor chooses a new destination.";return}
      if self.model.pattern != location.pattern{self.model.pattern=location.pattern;self.refreshPattern()}
      self.patternView.navigate(EditorNavigation(pattern:location.pattern,row:target.row,channel:location.channel,column:3+2*target.column,following:false),clearSelection:true)
      self.window.makeKeyAndOrderFront(nil);self.window.makeFirstResponder(self.patternView)
    }
    editor.onPreview = {[weak self,weak editor] target in
      guard let self,!self.busy else{return}
      guard let location=target.location(in:self.model) else{editor?.status.stringValue="The captured pattern cell no longer exists in this song. Current cursor chooses a new destination.";return}
      let settings:[String:Any]=["pattern":location.pattern,"startRow":target.row,"endRow":location.rows,"cursorRow":target.row,"loop":self.playbackLoop,"order":self.selectedOrder]
      self.perform("Playing scratch arrangement…",refresh:false,{try self.session.playRegion(settings)},completion:{editor?.status.stringValue="Playing from pattern \(location.pattern) · row \(target.row) · Space stops playback"})
    }
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:1000,height:720),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Scratch phrases";win.minSize=NSSize(width:880,height:620);win.isReleasedWhenClosed=false;win.contentView=editor
    scratchGestureWindow=win;win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
