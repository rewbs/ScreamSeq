import AppKit

/// An in-cell draft: one guarded edit for both numbers, pinned to its opening row.
final class PatternNudgeEditor: NSView, NSTextFieldDelegate {
  let strength = NSTextField(), duration = NSTextField()
  let target: EditorNavigation, fx: Int, revision: String, kind: String, offset: Int
  private let maximumDuration: Int, originalValue: Double, originalStrength: String
  private(set) var pending = false
  var onRequest: (([String:Any], @escaping ([String:Any])->Void)->Void)?
  var onMessage: ((String)->Void)?, onFinish: ((Bool)->Void)?
  static let help = "Strength (%) / duration (rows) · Tab switches values · Return saves and advances · Esc cancels"

  init(model: PatternModel, target: EditorNavigation, kind: String) {
    self.target=target;self.fx=max(0,(target.column-3)/2);revision=model.revisionToken;self.kind=kind
    let old=model.nativeCommand(target.row,target.channel,fx)
    let existing=old?.kind.hasPrefix("nudge-")==true ? old : nil
    offset=existing.map{$0.position%65536} ?? 0
    maximumDuration=(model.rows-target.row)*65536-offset
    originalValue=existing?.value ?? 0.75
    originalStrength=String(format:"%.15g",originalValue*100)
    super.init(frame:.zero)
    strength.stringValue=originalStrength
    duration.stringValue=String(format:"%.15g",Double(existing?.duration ?? min(65536,maximumDuration))/65536)
    strength.setAccessibilityLabel("Nudge strength percent");duration.setAccessibilityLabel("Nudge duration rows")
    strength.toolTip="Strength: 0–100%. An opposing push above 50% reverses playback."
    duration.toolTip="Duration in rows, including recovery. Fractions are supported (1/65536-row resolution)."
    for field in [strength,duration] {field.delegate=self;field.font=NSFont.monospacedDigitSystemFont(ofSize:11,weight:.medium);field.focusRingType = .none;field.isBezeled=false;field.drawsBackground=true;field.backgroundColor=Theme.raised;field.textColor=Theme.text;addSubview(field)}
    wantsLayer=true;layer?.backgroundColor=Theme.raised.cgColor;layer?.borderColor=Theme.accent.cgColor;layer?.borderWidth=1
  }
  required init?(coder:NSCoder){fatalError()}
  override func layout() {super.layout();let half=(bounds.width-13)/2;strength.frame=NSRect(x:2,y:1,width:half,height:bounds.height-2);duration.frame=NSRect(x:half+11,y:1,width:half,height:bounds.height-2)}
  override func draw(_ dirty:NSRect) {super.draw(dirty);("/" as NSString).draw(at:NSPoint(x:(bounds.width-13)/2+3,y:2),withAttributes:[.font:NSFont.systemFont(ofSize:12),.foregroundColor:Theme.muted])}
  func focus(_ field: NSTextField? = nil, replacing: String? = nil) {
    let selected=field ?? strength;window?.makeFirstResponder(selected);selected.selectText(nil)
    if let replacing {selected.stringValue=replacing;(selected.currentEditor() as? NSTextView)?.setSelectedRange(NSRange(location:(replacing as NSString).length,length:0))}
    onMessage?(Self.help)
  }
  func commit(advance: Bool) {
    guard !pending,let onRequest else{return}
    guard let percent=Double(strength.stringValue),percent.isFinite,(0...100).contains(percent),
      let rows=Double(duration.stringValue),rows.isFinite,rows>0,rows<=Double(maximumDuration)/65536,
      (rows*65536).rounded()>=1 else {onMessage?("Use strength 0–100% and duration 1/65536–\(Double(maximumDuration)/65536) rows.");return}
    let value=strength.stringValue==originalStrength ? originalValue : percent/100
    let command:[String:Any]=["kind":kind,"value":value,"duration":Int((rows*65536).rounded()),"offset":offset]
    pending=true;strength.isEnabled=false;duration.isEnabled=false
    onRequest(["pattern":target.pattern,"row":target.row,"channel":target.channel,"column":fx,"expectedRevision":revision,"command":command]) {[weak self] reply in
      guard let self else{return};self.pending=false;self.strength.isEnabled=true;self.duration.isEnabled=true
      if let error=reply["error"] as? [String:Any] {self.onMessage?((error["message"] as? String ?? "Could not save effect")+" · draft retained; Esc cancels so you can reopen the current cell.")}
      else {self.onFinish?(advance)}
    }
  }
  func control(_ control:NSControl,textView:NSTextView,doCommandBy selector:Selector)->Bool {
    switch selector {
    case #selector(NSResponder.insertTab(_:)),#selector(NSResponder.insertBacktab(_:)):
      focus(control===strength ? duration : strength);return true
    case #selector(NSResponder.insertNewline(_:)):commit(advance:true);return true
    case #selector(NSResponder.cancelOperation(_:)):if !pending {onFinish?(false)};return true
    default:return false
    }
  }
}

extension PatternView {
  func positionNudgeEditor() {
    guard let editor=nudgeEditor else{return}
    let target=editor.target
    let x=CGFloat(channelX(target.channel)+fieldOffset(4+editor.fx*2))
    let y=CGFloat(headerHeight+Float(target.row-firstRow)*rowHeight)
    editor.frame=NSRect(x:max(CGFloat(gutterWidth),min(x,bounds.width-CGFloat(fieldWidth(4)))),y:max(CGFloat(headerHeight),min(y,bounds.height-CGFloat(rowHeight))),width:CGFloat(fieldWidth(4)),height:CGFloat(rowHeight))
    editor.toolTip="Editing P\(target.pattern), row \(target.row), channel \(target.channel+1), FX \(editor.fx+1)"
  }
  @discardableResult func beginNudgeEdit(kind:String?=nil,replacing:String?=nil)->Bool {
    let command=model.nativeCommand(cursorRow,cursorChannel,effectColumn)
    guard let selectedKind=kind ?? command?.kind,selectedKind.hasPrefix("nudge-"),column>=3,canEdit() else{return false}
    if let nudgeEditor {nudgeEditor.focus();return true}
    isFollowing=false
    column=4+effectColumn*2;revealCursor()
    let editor=PatternNudgeEditor(model:model,target:navigation,kind:selectedKind)
    nudgeEditor=editor;addSubview(editor);editor.frame=cursorRect;editor.layoutSubtreeIfNeeded()
    editor.onRequest = {[weak self] params,reply in self?.onNudgeRequest?(params,reply)}
    editor.onMessage = {[weak self] text in self?.onMessage?(text)}
    editor.onFinish = {[weak self,weak editor] advance in
      guard let self,let editor,self.nudgeEditor===editor else{return}
      editor.removeFromSuperview();self.nudgeEditor=nil;self.onMessage?("")
      if advance && self.navigation==editor.target {self.cursorRow=min(self.model.rows-1,self.cursorRow+self.step)}
      self.window?.makeFirstResponder(self);self.revealCursor()
    }
    editor.focus(replacing:replacing);onCursor?();return true
  }
}
