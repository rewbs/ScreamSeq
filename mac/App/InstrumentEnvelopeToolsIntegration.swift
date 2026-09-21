import AppKit
extension AppController {
  func showInstrumentEnvelopeBank(_ kind:Int){
    if let instrumentEnvelopeBank,instrumentEnvelopeBank.window?.isVisible==true{instrumentEnvelopeBank.window?.makeKeyAndOrderFront(nil);return}
    guard !busy,model.editable,(0...2).contains(kind),let item=model.instruments.first(where:{$0["index"] as? Int==instrumentEditor.index}),let identity=item["id"] as? String else{return}
    let target:[String:Any]=["kind":["volume","pan","pitch"][kind],"instrument":identity]
    instrumentEnvelopeBank?.close();instrumentEnvelopeBank=EnvelopeBankWindow(title:"Instrument \(instrumentEditor.index)",target:target,shape:nil,revision:"",request:{[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)},applied:{[weak self] in self?.refreshAssets()})
  }

  func showInstrumentEnvelopeTools(_ kind:Int) {
    guard !busy,model.editable,(0...2).contains(kind),
      let instrument=model.instruments.first(where:{$0["index"] as? Int==instrumentEditor.index}),let identity=instrument["id"] as? String else{return}
    let editor=InstrumentEnvelopeToolsEditor(instrument:identity,kind:["volume","pan","pitch"][kind],clipboard:instrumentEnvelopeClipboard)
    editor.onRequest={[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    instrumentEnvelopeToolsWindow?.close()
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:720,height:660),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    window.title="Instrument envelope tools";window.minSize=NSSize(width:680,height:640);window.isReleasedWhenClosed=false
    window.contentView=editor;instrumentEnvelopeToolsWindow=window;window.center();window.makeKeyAndOrderFront(nil);editor.load()
  }
}
