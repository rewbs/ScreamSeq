import AppKit
extension AppController {
  @objc func showPatternCommands() {
    guard !busy, model.editable else { return }
    commandPickerWindow?.close()
    let picker=PatternCommandPicker(frame:NSRect(x:0,y:0,width:620,height:460))
    let context=(model,patternView.cursorRow,patternView.cursorChannel,patternView.column)
    picker.onContext={context}
    picker.onRequest = {[weak self] params,reply in self?.handleAutomation("pattern.apply",params:params,reply:reply)}
    picker.onDismiss = {[weak self] in
      guard let self else{return};self.commandPickerWindow?.close();self.window.makeKeyAndOrderFront(nil);self.window.makeFirstResponder(self.patternView)
    }
    picker.onNativeCommand = {[weak self,weak picker] kind,model,row,channel,column in
      guard let self else{return}
      guard self.session.automationRevision==model.revisionToken else{picker?.status.stringValue="The song changed. Close and reopen this list to choose a new target.";return}
      self.commandPickerWindow?.close();self.openPatternPerformance(context:(model,row,channel,column),kind:kind)
    }
    let panel=EffectFinderPanel(picker:picker);commandPickerWindow=panel;picker.capture()
    window.makeKeyAndOrderFront(nil);patternView.revealCursor()
    // Complete the originating menu/key event before handing focus to the list.
    DispatchQueue.main.async {[weak self,weak panel,weak picker] in
      guard let self,let panel,let picker,self.commandPickerWindow === panel else{return}
      let anchor=self.window.convertToScreen(self.patternView.convert(self.patternView.cursorRect,to:nil))
      let screen=self.window.screen?.visibleFrame ?? self.window.frame
      let size=panel.frame.size
      let y=anchor.minY-size.height>=screen.minY ? anchor.minY-size.height : min(screen.maxY-size.height,anchor.maxY)
      panel.setFrameOrigin(NSPoint(x:max(screen.minX,min(anchor.minX,screen.maxX-size.width)),y:max(screen.minY,y)))
      panel.makeKeyAndOrderFront(nil);picker.focusSearch()
    }
  }
  func updateCommandHelp() {
    cursorLabel.stringValue = String(format: "EDIT P%02d · R%03d · CH%02d", model.pattern, patternView.cursorRow, patternView.cursorChannel + 1)
    commandHelpLabel.stringValue = patternView.currentCommandHelp
    commandHelpLabel.toolTip = commandHelpLabel.stringValue
  }
}
