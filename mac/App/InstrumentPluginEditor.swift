import AppKit

/// Captures one tracker instrument and revision. Moving it preserves every other
/// assignment, including other MIDI parts sharing either plugin.
final class InstrumentPluginEditor: NSView {
  let plugin=NSPopUpButton(), channel=NSPopUpButton(), status=Theme.label("",size:12,color:Theme.muted)
  private(set) var instrument:Int
  let isCreating:Bool
  let name=NSTextField(string:"Plugin instrument")
  var revision:String
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onSaved:((Int)->Void)?
  var pending=false
  var applyButton:ActionButton!
  init(instrument:Int,model:PatternModel,selectedPlugin:String?=nil) {
    self.instrument=instrument;isCreating=instrument==0;revision=model.revisionToken;super.init(frame:NSRect(x:0,y:0,width:570,height:310))
    if !isCreating {plugin.addItem(withTitle:"No plugin · use sample keymap");plugin.lastItem?.representedObject=""}
    for item in model.nativePlugins where item["isInstrument"] as? Bool==true {
      plugin.addItem(withTitle:item["name"] as? String ?? "Instrument plugin");plugin.lastItem?.representedObject=item["instanceID"]
      let route=(item["instrumentAssignments"] as? [[String:Any]] ?? []).first{$0["instrument"] as? Int==instrument}
      if route != nil || item["instrument"] as? Int==instrument || item["instanceID"] as? String==selectedPlugin {
        plugin.selectItem(at:plugin.numberOfItems-1)
        status.tag=route?["channel"] as? Int ?? 1
      }
    }
    for n in 1...16 {channel.addItem(withTitle:"MIDI channel \(n)");channel.lastItem?.tag=n}
    channel.selectItem(withTag:max(1,status.tag))
    plugin.setAccessibilityLabel("Instrument sound source");channel.setAccessibilityLabel("Instrument MIDI channel")
    name.setAccessibilityLabel("New plugin instrument name")
    if isCreating {name.stringValue=plugin.titleOfSelectedItem ?? "Plugin instrument"}
    applyButton=ActionButton(isCreating ? "Create & assign instrument" : "Apply assignment", prominent:true){[weak self] in self?.apply()}
    status.stringValue=isCreating ? "Creates an empty tracker instrument and connects it to this synth. No sample needed. Enter its number beside a pattern note. Undo effect change restores assignment; document Undo removes the new instrument." : "Other instruments sharing a plugin keep their assignments. Undo effect change restores this routing."
    if isCreating && plugin.numberOfItems==0 {status.stringValue="First add a VST3 or AU instrument in Plugins → Add plugin…, then reopen New plugin instrument.";applyButton.isEnabled=false}
    status.maximumNumberOfLines=5;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=530;status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let body=stack(.vertical,[Theme.label(isCreating ? "New plugin trigger instrument" : "Instrument \(instrument) · Sound source",size:18,weight:.semibold),
      name,plugin,channel,status,stack(.horizontal,[NSView(),applyButton!])],spacing:12)
    name.isHidden = !isCreating
    body.stretchAcrossAxis();body.fill(self,inset:20)
  }
  required init?(coder:NSCoder){fatalError()}
  func apply(){
    guard !pending,let id=plugin.selectedItem?.representedObject as? String,let onRequest else{return}
    guard window?.makeFirstResponder(nil) != false else{return}
    pending=true;applyButton.isEnabled=false;plugin.isEnabled=false;channel.isEnabled=false
    if instrument==0 {
      name.isEnabled=false
      onRequest("instrument.create",["empty":true,"name":name.stringValue,"expectedRevision":revision]){[weak self] reply in
        guard let self else{return};self.pending=false
        guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let index=data["instrument"] as? Int,let revision=result["revision"] as? String else {
          self.applyButton.isEnabled=true;self.plugin.isEnabled=true;self.channel.isEnabled=true;self.name.isEnabled=true
          self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not create instrument. Reopen to refresh.";return
        }
        self.instrument=index;self.revision=revision;self.apply()
      };return
    }
    onRequest("instrument.plugin.set",["instrument":instrument,"plugin":id,"channel":channel.selectedTag(),"expectedRevision":revision]){[weak self] reply in
      guard let self else{return};self.pending=false;self.applyButton.isEnabled=true;self.plugin.isEnabled=true;self.channel.isEnabled=true
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String else{
        self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not apply assignment. Reopen to refresh.";return
      }
      self.applyButton.title="Apply assignment"
      self.revision=revision;self.status.stringValue="Assignment saved. Play this instrument from the instrument inspector or pattern.";self.onSaved?(self.instrument)
    }
  }
}
