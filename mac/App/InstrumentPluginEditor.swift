import AppKit

/// Captures one tracker instrument and revision. Moving it preserves every other
/// assignment, including other MIDI parts sharing either plugin.
final class InstrumentPluginEditor: NSView {
  let plugin=NSPopUpButton(), channel=NSPopUpButton(), status=Theme.label("",size:12,color:Theme.muted)
  let instrument:Int
  var revision:String
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onSaved:(()->Void)?
  var pending=false
  var applyButton:ActionButton!
  init(instrument:Int,model:PatternModel) {
    self.instrument=instrument;revision=model.revisionToken;super.init(frame:NSRect(x:0,y:0,width:570,height:230))
    plugin.addItem(withTitle:"No plugin · use sample keymap");plugin.lastItem?.representedObject=""
    for item in model.nativePlugins where item["isInstrument"] as? Bool==true {
      plugin.addItem(withTitle:item["name"] as? String ?? "Instrument plugin");plugin.lastItem?.representedObject=item["instanceID"]
      let route=(item["instrumentAssignments"] as? [[String:Any]] ?? []).first{$0["instrument"] as? Int==instrument}
      if route != nil || item["instrument"] as? Int==instrument {
        plugin.selectItem(at:plugin.numberOfItems-1)
        status.tag=route?["channel"] as? Int ?? 1
      }
    }
    for n in 1...16 {channel.addItem(withTitle:"MIDI channel \(n)");channel.lastItem?.tag=n}
    channel.selectItem(withTag:max(1,status.tag))
    plugin.setAccessibilityLabel("Instrument sound source");channel.setAccessibilityLabel("Instrument MIDI channel")
    applyButton=ActionButton("Apply assignment"){[weak self] in self?.apply()}
    status.stringValue="Other instruments sharing a plugin keep their assignments. Undo effect change restores this routing."
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping
    let body=stack(.vertical,[Theme.label("Instrument \(instrument) · Sound source",size:18,weight:.semibold),
      plugin,channel,status,stack(.horizontal,[NSView(),applyButton!])],spacing:12)
    body.stretchAcrossAxis();body.fill(self,inset:20)
  }
  required init?(coder:NSCoder){fatalError()}
  func apply(){
    guard !pending,let id=plugin.selectedItem?.representedObject as? String,let onRequest else{return}
    pending=true;applyButton.isEnabled=false;plugin.isEnabled=false;channel.isEnabled=false
    onRequest("instrument.plugin.set",["instrument":instrument,"plugin":id,"channel":channel.selectedTag(),"expectedRevision":revision]){[weak self] reply in
      guard let self else{return};self.pending=false;self.applyButton.isEnabled=true;self.plugin.isEnabled=true;self.channel.isEnabled=true
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String else{
        self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not apply assignment. Reopen to refresh.";return
      }
      self.revision=revision;self.status.stringValue="Assignment saved. Play this instrument from the instrument inspector or pattern.";self.onSaved?()
    }
  }
}
