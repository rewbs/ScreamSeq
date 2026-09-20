import AppKit
extension AppController {
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
