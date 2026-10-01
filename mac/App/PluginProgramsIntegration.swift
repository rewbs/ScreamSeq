import AppKit
extension AppController {
  func showPluginPrograms(_ slot:Int) {
    guard !busy,model.nativePlugins.indices.contains(slot),let identity=model.nativePlugins[slot]["instanceID"] as? String else{return}
    let editor=PluginProgramsEditor(plugin:identity)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    pluginProgramsWindow?.close()
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:680,height:580),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Factory programs";win.minSize=NSSize(width:620,height:520);win.isReleasedWhenClosed=false;win.contentView=editor;pluginProgramsWindow=win
    win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
