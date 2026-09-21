import AppKit
extension AppController {
  func showInstrumentPluginAssignment() {
    guard !busy,model.instruments.contains(where:{$0["index"] as? Int==instrumentEditor.index}) else{
      statusLabel.stringValue="Create a tracker instrument with New in the instrument inspector first.";return
    }
    let editor=InstrumentPluginEditor(instrument:instrumentEditor.index,model:model)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    editor.onSaved = {[weak self] in self?.refreshAll()}
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
