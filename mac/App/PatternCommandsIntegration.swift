import AppKit
extension AppController {
  @objc func showPatternCommands() {
    guard !busy, model.editable else { return }
    commandPickerWindow?.close()
    let picker=PatternCommandPicker(frame:NSRect(x:0,y:0,width:620,height:460))
    let context=(model,patternView.cursorRow,patternView.cursorChannel,patternView.column)
    picker.onContext={context}
    picker.onRequest = {[weak self] params,reply in self?.handleAutomation(params["cells"] == nil ? "pattern.effect.set" : "pattern.apply",params:params,reply:reply)}
    picker.onDismiss = {[weak self] in
      guard let self else{return};self.commandPickerWindow?.close();self.window.makeKeyAndOrderFront(nil);self.window.makeFirstResponder(self.patternView)
    }
    picker.onNativeSelection = {[weak self,weak picker] entry,model,row,channel,column in
      guard let kind=entry.nativeKind else{return}
      guard let self else{return}
      guard self.session.automationRevision==model.revisionToken else{picker?.status.stringValue="The song changed. Close and reopen this list to choose a new target.";return}
      guard self.patternView.model.pattern==model.pattern else {picker?.status.stringValue="The displayed pattern changed. Close and reopen this list to edit that pattern.";return}
      self.commandPickerWindow?.close()
      if entry.nativeName=="scratch" {self.patternView.cursorRow=row;self.patternView.cursorChannel=channel;self.patternView.column=max(3,column);self.showScratchGestures();return}
      if model.commands.schema(kind:kind,native:entry.nativeName) != nil {
        self.window.makeKeyAndOrderFront(nil)
        self.patternView.cursorRow=row;self.patternView.cursorChannel=channel;self.patternView.column=max(3,column)
        _=self.patternView.beginNudgeEdit(kind:kind,native:entry.nativeName)
      } else {self.openPatternPerformance(context:(model,row,channel,column),kind:kind)}
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
  func updateInputContext() {
    octavePicker.selectItem(at:patternView.octave)
    let assets=model.instruments.isEmpty ? model.samples:model.instruments
    let name=assets.first{($0["index"] as? Int)==patternView.instrument}?["name"] as? String ?? ""
    inputLabel.stringValue=String(format:"INS %02d",patternView.instrument)+" \(name) · OCT \(patternView.octave)"
    inputLabel.toolTip="New notes use this instrument and octave. Return on an instrument cell selects it. Option–Up/Down changes instrument; Option–Left/Right or keypad ÷/× changes octave."
    inputLabel.lineBreakMode = .byTruncatingTail
    inputLabel.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    updateCommandHelp()
  }
  func updateCommandHelp() {
    cursorLabel.stringValue = String(format: "EDIT P%02d · R%03d · CH%02d", model.pattern, patternView.cursorRow, patternView.cursorChannel + 1)
    commandHelpLabel.stringValue = patternView.currentCommandHelp
    commandHelpLabel.toolTip = commandHelpLabel.stringValue
  }
}
