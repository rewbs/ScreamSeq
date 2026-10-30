import AppKit

/// Retained native fields over document.patch. Presentation never owns a second
/// song model, and a read or completion never replaces newer field text.
final class SongSettingsEditor: NSView, NSTextFieldDelegate {
  let title = NSTextField(), tempo = NSTextField(), speed = NSTextField(), channels = NSTextField()
  let status = Theme.label("", size:12, color:Theme.gold)
  var onRequest: ((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onContext: (()->(document:String,revision:String))?
  var onReturn:(()->Void)?, onClose:(()->Void)?
  private(set) var capturedDocument = "", revision = "", pending = false, uncertain = false
  private var baseline = [String:String](), generation:UInt64 = 0
  private var waiters = [(Bool)->Void]()
  private var observed:(document:String,revision:String,fields:[String:String])?
  private var buttons = [NSButton]()
  var raw:[String:String] {["title":title.stringValue,"tempo":tempo.stringValue,"speed":speed.stringValue,"channels":channels.stringValue]}
  var hasDraft:Bool {baseline.isEmpty ? raw.values.contains{!$0.isEmpty}:raw != baseline}
  var needsCompletion:Bool {pending || uncertain || hasDraft}
  var stale:Bool {guard let now=onContext?() else{return true};return now.document != capturedDocument || now.revision != revision}
  var composing:Bool {[title,tempo,speed,channels].contains{($0.currentEditor() as? NSTextView)?.hasMarkedText() == true}}
  override init(frame:NSRect) {
    super.init(frame:frame)
    for (field,label) in [(title,"Song title"),(tempo,"Tempo in BPM"),(speed,"Ticks per row"),(channels,"Channel count")] {
      field.delegate=self;field.setAccessibilityLabel(label)
    }
    for field in [tempo,speed,channels] {field.fixed(width:100)}
    let help=Theme.label("Apply stops playback. Reducing channels removes trailing columns and their music; Undo restores them. Closing keeps your draft. Use current revision retains your text; Reload values discards it.",size:12,color:Theme.muted)
    for label in [help,status] {label.maximumNumberOfLines=0;label.lineBreakMode = .byWordWrapping;label.preferredMaxLayoutWidth=570;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    let capture=ActionButton("Use current revision"){[weak self] in self?.load(reload:false)}
    let reload=ActionButton("Reload values"){[weak self] in self?.load(reload:true)}
    let apply=ActionButton("Apply"){[weak self] in self?.apply()}
    apply.keyEquivalent="\r";apply.keyEquivalentModifierMask = .command
    let accept=ActionButton("Accept observed state"){[weak self] in self?.acceptObservedState()}
    let back=ActionButton("Pattern"){[weak self] in guard let self,self.canLeave() else{return};self.onReturn?()}
    let close=ActionButton("Close"){[weak self] in guard let self,self.canLeave() else{return};self.onClose?()}
    buttons=[capture,reload,apply,accept]
    let content=stack(.vertical,[Theme.label("Song settings",size:18,weight:.semibold),labeled("TITLE",title),
      stack(.horizontal,[labeled("BPM",tempo),labeled("TICKS / ROW",speed),labeled("CHANNELS",channels)],spacing:24),help,
      stack(.horizontal,[capture,reload],spacing:10),status,stack(.horizontal,[apply,accept,NSView(),back,close],spacing:10)],spacing:16)
    content.stretchAcrossAxis();content.fill(self,inset:24);controls()
  }
  required init?(coder:NSCoder){fatalError()}
  func controlTextDidChange(_ obj:Notification) {generation &+= 1;observed=nil;controls()}
  func canLeave()->Bool {
    if composing {status.stringValue="Finish composing the current field before continuing.";return false}
    return true
  }
  private func fields(_ data:[String:Any])->[String:String]? {
    guard let name=data["title"] as? String,let bpm=data["tempo"] as? NSNumber,
      let ticks=data["speed"] as? NSNumber,let count=data["channels"] as? NSNumber else{return nil}
    return ["title":name,"tempo":bpm.stringValue,"speed":ticks.stringValue,"channels":count.stringValue]
  }
  private func show(_ fields:[String:String]) {
    for (field,key) in [(title,"title"),(tempo,"tempo"),(speed,"speed"),(channels,"channels")] {field.stringValue=fields[key] ?? ""}
  }
  func load(reload:Bool) {
    guard !pending,canLeave(),let onRequest,let context=onContext?() else{return}
    let before=raw,version=generation;pending=true;controls();status.stringValue="Reading song settings…"
    onRequest("document.get",[:]) {[weak self] reply in
      guard let self else{return};self.pending=false
      guard self.onContext?().document==context.document,let result=reply["result"] as? [String:Any],
        let data=result["data"] as? [String:Any],let revision=result["revision"] as? String,let fields=self.fields(data) else {
        self.fail(reply,fallback:"Song changed or settings could not be read. Draft retained.");return
      }
      if self.uncertain {
        self.observed=(context.document,revision,fields)
        self.status.stringValue="Current song observed; the earlier write outcome is unknown. Accept observed state to continue; your field text is retained."
        self.controls();self.complete(false);return
      }
      self.capturedDocument=context.document;self.revision=revision;self.baseline=fields
      if reload && self.generation==version && self.raw==before && !self.composing {self.show(fields)}
      self.status.stringValue=self.hasDraft ? "Current revision captured; field text retained.":"Current song values loaded."
      self.controls();self.resumeBoundaries()
    }
  }
  func acceptObservedState() {
    guard !pending,uncertain,canLeave(),let observed,let now=onContext?(),
      now.document==observed.document,now.revision==observed.revision else {
      status.stringValue="Read the current settings again before accepting observed state.";return
    }
    capturedDocument=observed.document;revision=observed.revision;baseline=observed.fields
    uncertain=false;self.observed=nil;status.stringValue="Observed state accepted; earlier outcome remains unverified. Field text retained.";controls()
  }
  func apply() {
    guard !pending else{return}
    guard canLeave(),!uncertain,let onRequest else {complete(false);return}
    guard !revision.isEmpty,!stale else {status.stringValue="Song changed; use current revision or Reload values. Draft retained.";complete(false);return}
    guard hasDraft else {complete(true);return}
    let draft=raw,version=generation,document=capturedDocument
    guard let bpm=Double(draft["tempo"]!),bpm.isFinite,(32...512).contains(bpm),
      let ticks=Int(draft["speed"]!),(1...31).contains(ticks),let count=Int(draft["channels"]!),count>0 else {
      status.stringValue="Use BPM 32–512, whole-number ticks 1–31 and a positive whole-number channel count.";complete(false);return
    }
    var params:[String:Any] = ["expectedRevision":revision]
    for (key,value) in [("title",draft["title"]! as Any),("tempo",bpm),("speed",ticks),("channels",count)] where draft[key] != baseline[key] {params[key]=value}
    pending=true;observed=nil;controls();status.stringValue="Applying song settings…"
    onRequest("document.patch",params) {[weak self] reply in
      guard let self else{return};self.pending=false
      guard self.onContext?().document==document else {self.status.stringValue="Original song is no longer open. Draft retained.";self.controls();self.complete(false);return}
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String else {
        let error=reply["error"] as? [String:Any]
        self.uncertain=error==nil || (error?["code"] as? Int == -32002 && !NativeRequestOutcome.canRetryWrite(reply))
        self.fail(reply,fallback:"Settings outcome is unavailable. Read current settings before continuing.");return
      }
      self.revision=revision;self.baseline=draft
      self.status.stringValue=self.generation != version || self.raw != draft ? "Settings saved; newer field text retained.":"Settings saved. Undo restores the previous values."
      self.controls();self.resumeBoundaries()
    }
  }
  func finishAutomaticEdit(_ done:@escaping (Bool)->Void) {
    guard canLeave(),!uncertain else {done(false);return}
    waiters.append(done);if !pending {resumeBoundaries()}
  }
  private func resumeBoundaries() {guard !waiters.isEmpty else{return};if hasDraft {apply()}else{complete(true)}}
  private func complete(_ accepted:Bool) {let callbacks=waiters;waiters.removeAll();callbacks.forEach{$0(accepted)}}
  private func fail(_ reply:[String:Any],fallback:String) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? fallback;controls();complete(false)}
  private func controls() {
    guard buttons.count==4 else{return}
    buttons[0].isEnabled = !pending;buttons[1].isEnabled = !pending
    buttons[2].isEnabled = !pending && !uncertain && !revision.isEmpty
    buttons[3].isHidden = !uncertain;buttons[3].isEnabled = !pending && observed != nil
  }
  var snapshot:[String:Any] {["capturedDocument":capturedDocument,"revision":revision,"draft":raw,"dirty":hasDraft,"pending":pending,"uncertain":uncertain,"stale":stale,"status":status.stringValue]}
}

final class SongSettingsWindow:NSWindow,NSWindowDelegate {
  let editor=SongSettingsEditor(frame:.zero)
  init() {
    super.init(contentRect:NSRect(x:0,y:0,width:680,height:440),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    title="Song settings";isReleasedWhenClosed=false;contentView=editor;delegate=self;minSize=NSSize(width:680,height:440)
    editor.onClose={[weak self] in self?.performClose(nil)}
  }
  func windowShouldClose(_ sender:NSWindow)->Bool {editor.canLeave()}
}
