import AppKit
extension AppController {
  func showNewPluginInstrument(slot:Int?=nil) {
    guard !busy,model.editable else{return}
    let identity=slot.flatMap{model.nativePlugins.indices.contains($0) ? model.nativePlugins[$0]["instanceID"] as? String : nil}
    let editor=InstrumentPluginEditor(instrument:0,model:model,selectedPlugin:identity)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    editor.onSaved = {[weak self,weak editor] index in
      guard let self else{return}
      self.instrumentEditor.index=index;self.patternView.instrument=index
      // An explicit creation acts like selecting an instrument in its picker:
      // keep it visible until the editing cursor moves, without changing pin state.
      self.workspaceContextTokens["instruments"]="asset:\(self.model.pattern):\(self.patternView.cursorRow):\(self.patternView.cursorChannel)"
      self.workspace?.panels["instruments"]?.target.stringValue="Instrument \(index)"
      self.refreshAll();self.workspace?.show("instruments",focus:true);self.refreshWorkspaceAsset("instruments")
      editor?.status.stringValue="Instrument \(index) is ready. Enter notes using instrument \(index), or play Z–M / Q–U in the instrument inspector."
    }
    let win=NSWindow(contentRect:editor.frame,styleMask:[.titled,.closable],backing:.buffered,defer:false)
    win.title="New plugin instrument";win.isReleasedWhenClosed=false;win.contentView=editor
    instrumentPluginWindow?.close();instrumentPluginWindow=win;win.center();win.makeKeyAndOrderFront(nil)
  }

  func showInstrumentPluginAssignment() {
    guard !busy,model.instruments.contains(where:{$0["index"] as? Int==instrumentEditor.index}) else{
      statusLabel.stringValue="Create a tracker instrument with New in the instrument inspector first.";return
    }
    let editor=InstrumentPluginEditor(instrument:instrumentEditor.index,model:model)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    editor.onSaved = {[weak self] _ in self?.refreshAll()}
    let win=NSWindow(contentRect:editor.frame,styleMask:[.titled,.closable],backing:.buffered,defer:false)
    win.title="Instrument sound source";win.isReleasedWhenClosed=false;win.contentView=editor
    instrumentPluginWindow?.close();instrumentPluginWindow=win;win.center();win.makeKeyAndOrderFront(nil)
  }

  func showPluginInstruments(_ slot:Int) {
    guard !busy,model.nativePlugins.indices.contains(slot),model.nativePlugins[slot]["isInstrument"] as? Bool==true,
      let identity=model.nativePlugins[slot]["instanceID"] as? String else{return}
    let editor=PluginInstrumentsEditor(plugin:identity)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    pluginInstrumentsWindow?.close()
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:680,height:520),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Plugin instruments";win.minSize=NSSize(width:620,height:460);win.isReleasedWhenClosed=false;win.contentView=editor;pluginInstrumentsWindow=win
    win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
