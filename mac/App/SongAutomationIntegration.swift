import AppKit

extension AppController {
  @objc func showSongAutomation(){
    let selected=model.nativePlugins.indices.contains(pluginEditor.selected) ? model.nativePlugins[pluginEditor.selected]["instanceID"] as? String:nil
    openSongAutomation(plugin:selected ?? "",parameter:nil,retain:true)
  }
  func openSongAutomation(plugin:String,parameter:Int?,retain:Bool=false){
    if songAutomationWindow==nil {
      let controller=SongAutomationWindow();songAutomationWindow=controller
      controller.editor.onContext = {[weak self] in self?.model ?? PatternModel([:])}
      controller.editor.onBusy = {[weak self] in self?.busy != false || self?.sessionReading != false}
      controller.editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
      controller.editor.onOpen = {[weak self] plugin,parameter,pattern in
        if pattern{self?.automateParameter(plugin:plugin,parameter:parameter)}else{self?.openParameterSource(["kind":"baseline","plugin":plugin,"parameter":parameter])}
      }
    }
    guard let controller=songAutomationWindow else{return}
    let retainExisting=retain && (!controller.editor.revision.isEmpty || controller.editor.needsCompletion)
    controller.showWindow(nil);controller.window?.makeKeyAndOrderFront(nil)
    if !retainExisting{controller.editor.inspect(plugin:plugin,parameter:parameter)}
  }
}
