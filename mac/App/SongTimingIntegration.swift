import AppKit
extension AppController {
  @objc func showSongTiming() {
    guard !busy,model.editable else{return}
    let editor=SongTimingEditor(frame:.zero)
    editor.onRequest = { [weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply) }
    songTimingWindow?.close()
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:660,height:560),styleMask:[.titled,.closable],backing:.buffered,defer:false)
    win.title="Tempo and groove";win.isReleasedWhenClosed=false;win.contentView=editor;songTimingWindow=win
    win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
