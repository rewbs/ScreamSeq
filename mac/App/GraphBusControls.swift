import AppKit

// The graph's compact mixer surface uses the same audition/commit transaction
// as a mixer strip. A target change waits for the old gesture's final write.
final class GraphBusControls:NSView {
  let gain=MixerControl("Level · dB",key:"gainDB",min:-96,max:24)
  let mute=NSButton(checkboxWithTitle:"Mute",target:nil,action:nil)
  let solo=NSButton(checkboxWithTitle:"Solo",target:nil,action:nil)
  let message=Theme.label("",size:10,color:Theme.muted)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onChanged:(()->Void)?
  private(set) var identity:String?
  private var revision="",saved=[String:Any](),draft=[String:Any]()
  private var pending=false,finish=false,previewed=false
  private var shownLevel:Double?
  var implicit=false
  private var nextContext:([String:Any]?,String)?
  var editing:Bool {pending || !draft.isEmpty || gain.slider.trackingGesture || gain.editingValue}
  override init(frame:NSRect) {
    super.init(frame:frame)
    gain.onChange={[weak self] key,value,final in self?.change(key,value:value,final:final)}
    gain.onFinish={[weak self] in self?.finishGesture()}
    mute.target=self;mute.action=#selector(toggleMute);solo.target=self;solo.action=#selector(toggleSolo)
    let content=stack(.vertical,[gain,stack(.horizontal,[mute,solo]),message],spacing:3)
    content.stretchAcrossAxis();content.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(_ bus:[String:Any]?,revision:String) {
    if editing {
      nextContext=(bus,revision)
      if bus?["id"] as? String != identity {finishGesture()}
      return
    }
    let targetChanged=identity != bus?["id"] as? String
    identity=bus?["id"] as? String;saved=bus ?? [:];self.revision=revision
    let level=(saved["gainDB"] as? NSNumber)?.doubleValue ?? 0
    // Pattern edits advance the revision without changing mixer controls. Do
    // not invalidate native slider/button layout or replace accepted text on
    // those refreshes; still consume the new revision for the next edit.
    if shownLevel != level || gain.slider.doubleValue != level {gain.set(level);shownLevel=level}
    let muted:NSControl.StateValue=saved["mute"] as? Bool==true ? .on:.off
    let soloed:NSControl.StateValue=saved["solo"] as? Bool==true ? .on:.off
    if mute.state != muted{mute.state=muted};if solo.state != soloed{solo.state=soloed}
    let enabled=identity != nil
    for control in [gain.slider,gain.value,mute,solo] as [NSControl] where control.isEnabled != enabled{control.isEnabled=enabled}
    if targetChanged{message.stringValue=""}
  }
  @objc private func toggleMute(){change("mute",value:mute.state == .on,final:true)}
  @objc private func toggleSolo(){change("solo",value:solo.state == .on,final:true)}
  func change(_ key:String,value:Any,final:Bool) {
    guard identity != nil else{return}
    message.stringValue=""
    draft[key]=value;finish=finish || final || implicit;send()
  }
  func finishGesture(){guard !draft.isEmpty || pending else{applyNextContext();return};finish=true;send()}
  private func send() {
    guard !pending,!draft.isEmpty,let id=identity,let onRequest else{return}
    let values=draft,final=finish,before=revision
    var params=values;params["bus"]=id;params["expectedRevision"]=revision;params["preview"] = !final
    pending=true
    if final{draft=[:];finish=false}else{previewed=true}
    onRequest("mixer.bus.set",params){[weak self] response in
      guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any] else{
        self.draft=[:];self.finish=false
        let error=(response["error"] as? [String:Any])?["message"] as? String ?? "Level edit failed"
        self.recoverPreview(error);return
      }
      self.revision=result["revision"] as? String ?? self.revision
      if let next=self.nextContext,next.1==before {self.nextContext=(next.0,self.revision)}
      if final {
        self.previewed=false;self.implicit=false;for (key,value) in values{self.saved[key]=value}
        if !self.draft.isEmpty{self.send()}else{self.applyNextContext();self.onChanged?()}
      } else if self.finish || !NSDictionary(dictionary:values).isEqual(to:self.draft){self.send()}
    }
  }
  private func applyNextContext() {
    guard !pending,draft.isEmpty else{return}
    if let next=nextContext {
      nextContext=nil
      // A refresh received during our write still describes the prior
      // revision. Do not flash it over the just-accepted value.
      if next.0?["id"] as? String==identity {context(saved,revision:revision)}
      else{context(next.0,revision:next.1)}
    }
  }
  private func recoverPreview(_ error:String) {
    guard let id=identity,let onRequest else{message.stringValue=error;return}
    // A rejected final write must not leave an unsaved audition value audible.
    // Read the current document, then restore its controls at that revision.
    pending=true
    onRequest("mixer.get",["includeImplicit":true]){[weak self] response in
      guard let self else{return}
      guard let result=response["result"] as? [String:Any],let rev=result["revision"] as? String,
        let mixer=result["data"] as? [String:Any],let bus=(mixer["buses"] as? [[String:Any]])?.first(where:{$0["id"] as? String==id}) else{
        self.pending=false;self.message.stringValue=error;self.onChanged?();return
      }
      self.saved=bus;self.revision=rev
      func done(_ restored:Bool){self.pending=false;self.previewed=false;self.context(bus,revision:rev);self.applyNextContext();self.message.stringValue=error+(restored ? "":" · audition reset failed; stop playback to reset");self.onChanged?()}
      guard self.previewed else{done(true);return}
      var restore=bus.filter{["preGainDB","prePan","gainDB","pan","width","mute","solo"].contains($0.key)}
      restore["bus"]=id;restore["expectedRevision"]=rev
      onRequest("mixer.bus.set",restore){reply in done(reply["result"] != nil)}
    }
  }
}
