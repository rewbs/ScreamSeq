import AppKit

extension AppController {
  func showPluginLocation(_ target:[String:String]) {
    let key=recoveryID+"/"+target.keys.sorted().map{target[$0]!}.joined(separator:"/")
    if let panel=pluginLocationWindows[key] {panel.makeKeyAndOrderFront(nil);return}
    let panel=PluginLocationWindow(target:target);pluginLocationWindows[key]=panel
    panel.editor.onContext = {[weak self] in
      guard let self else{return ("","")};return (self.recoveryID,self.session.automationRevision)
    }
    panel.editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    panel.center();panel.makeKeyAndOrderFront(nil);panel.editor.load()
  }
}
