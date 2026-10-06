import AppKit

/// A captured command draft. Each descriptor has its own aligned field; saving
/// sends one guarded edit and leaves every untouched stored scalar unchanged.
final class PatternNudgeEditor: NSView, NSTextFieldDelegate {
  let fields:[NSTextField],descriptors:[PatternEffectField],slotWidths:[Float]
  let target:EditorNavigation,fx:Int,revision:String,kind:String,native:String?
  let model:PatternModel,timingUnit:PatternTimingUnit
  private let original:[String:Any],originalText:[String]
  private(set) var pending=false
  var contentOffset:CGFloat=0 {didSet {needsLayout=true}}
  var onRequest:(([String:Any],@escaping([String:Any])->Void)->Void)?
  var onMessage:((String)->Void)?,onFinish:((Bool)->Void)?,onFocus:((Int)->Void)?
  var strength:NSTextField {fields[descriptors.firstIndex{$0.storage=="value"} ?? 0]}
  var duration:NSTextField {fields[descriptors.firstIndex{$0.storage=="duration" || $0.storage=="durationBeats"} ?? 0]}
  static let help="Tab switches parameters · Return saves one command and advances · Esc cancels"
  private var rateMode:String? {
    if let index=descriptors.firstIndex(where:{$0.storage=="parameters.rateMode"}) {return fields[index].stringValue.lowercased()}
    return (original["parameters"] as? [String:Any])?["rateMode"] as? String
  }
  init(model:PatternModel,target:EditorNavigation,schema:PatternEffectSchema,timing:PatternTimingUnit,allParameters:Bool=false,draft:PatternNudgeEditor?=nil) {
    self.model=model;self.target=target;fx=max(0,(target.column-3)/2);revision=model.revisionToken
    kind=schema.kind;native=schema.native;timingUnit=timing;descriptors=allParameters ? schema.fields : schema.inlineFields
    let old=model.nativeCommand(target.row,target.channel,fx)
    let existing=old?.kind==kind && old?.native==native ? old : nil
    var command=draft?.original ?? existing?.editCommand ?? ["kind":kind,"offset":0]
    if let native {command["native"]=native;command["parameters"]=command["parameters"] as? [String:Any] ?? [:]}
    for field in schema.fields {
      if field.storage.hasPrefix("parameters.") {
        var values=command["parameters"] as? [String:Any] ?? [:]
        let key=String(field.storage.dropFirst(11));if values[key]==nil {values[key]=field.defaultValue};command["parameters"]=values
      } else if command[field.storage]==nil {command[field.storage]=field.defaultValue}
    }
    if existing==nil,draft==nil,kind.hasPrefix("nudge-"),let beats=(command["durationBeats"] as? NSNumber)?.doubleValue {
      command["durationBeats"]=min(beats,Double(max(0,model.rows-target.row))/Double(max(1,model.rowsPerBeat)))
    }
    if existing==nil,draft==nil,native=="scratch",var values=command["parameters"] as? [String:Any] {
      let beats=(values["beats"] as? NSNumber)?.doubleValue ?? 1
      values["beats"]=min(beats,Double(max(0,model.rows-target.row))/Double(max(1,model.rowsPerBeat)))
      if !model.scratchGestures.contains(where:{$0["id"] as? Int==(values["gesture"] as? NSNumber)?.intValue}),let id=model.scratchGestures.first?["id"] {values["gesture"]=id}
      command["parameters"]=values
    }
    if existing==nil,command["binding"] != nil,let binding=model.effectBindings.first?["id"] as? Int {command["binding"]=binding}
    original=command
    func stored(_ field:PatternEffectField)->Any {
      if field.storage.hasPrefix("parameters.") {return (command["parameters"] as? [String:Any])?[String(field.storage.dropFirst(11))] ?? field.defaultValue}
      return command[field.storage] ?? field.defaultValue
    }
    let initialRateMode=(command["parameters"] as? [String:Any])?["rateMode"] as? String
    let initialText=descriptors.map{$0.text(stored($0),model:model,timing:timing,editing:true,rateMode:initialRateMode)}
    originalText=initialText
    fields=descriptors.enumerated().map{index,field in
      let text=draft.flatMap{prior in prior.descriptors.firstIndex{$0.storage==field.storage}.map{prior.fields[$0].stringValue}} ?? initialText[index]
      return NSTextField(string:text)
    }
    let layout=model.effectLayout(target.channel,fx)
    slotWidths=descriptors.enumerated().map{index,field in max(field.width,index<layout.slotWidths.count ? layout.slotWidths[index] : 0)}
    super.init(frame:.zero)
    for (index,field) in fields.enumerated() {
      let descriptor=descriptors[index]
      field.delegate=self;field.font=NSFont.monospacedDigitSystemFont(ofSize:11,weight:.medium);field.focusRingType = .none
      field.isBezeled=false;field.drawsBackground=true;field.backgroundColor=Theme.raised;field.textColor=Theme.text
      field.setAccessibilityLabel(descriptor.help(model:model,timing:timing,rateMode:rateMode))
      field.toolTip=descriptor.help(model:model,timing:timing,rateMode:rateMode)+" · "+Self.help
      addSubview(field)
    }
    wantsLayer=true;layer?.backgroundColor=Theme.raised.cgColor;layer?.borderColor=Theme.accent.cgColor;layer?.borderWidth=1;layer?.masksToBounds=true
  }
  required init?(coder:NSCoder){fatalError()}
  override func layout() {
    super.layout();var x=contentOffset
    for (index,field) in fields.enumerated() {field.frame=NSRect(x:x+2,y:1,width:CGFloat(slotWidths[index])-6,height:bounds.height-2);x += CGFloat(slotWidths[index])}
  }
  func focus(_ field:NSTextField?=nil,replacing:String?=nil,index:Int?=nil) {
    let selected=index.map{fields[max(0,min(fields.count-1,$0))]} ?? field ?? fields[0]
    let at=fields.firstIndex(of:selected) ?? 0;onFocus?(at)
    window?.makeFirstResponder(selected);selected.selectText(nil)
    if let replacing {selected.stringValue=replacing;(selected.currentEditor() as? NSTextView)?.setSelectedRange(NSRange(location:(replacing as NSString).length,length:0))}
    onMessage?(descriptors[at].help(model:model,timing:timingUnit,rateMode:rateMode)+" · "+Self.help)
  }
  func controlTextDidBeginEditing(_ notification:Notification) {
    if let field=notification.object as? NSTextField,let index=fields.firstIndex(of:field) {onFocus?(index);onMessage?(descriptors[index].help(model:model,timing:timingUnit,rateMode:rateMode)+" · "+Self.help)}
  }
  func commit(advance:Bool) {
    guard !pending,let onRequest else{return}
    var command=original
    for (index,field) in descriptors.enumerated() where fields[index].stringValue != originalText[index] {
      let text=fields[index].stringValue.trimmingCharacters(in:.whitespacesAndNewlines)
      let value:Any
      if !field.choices.isEmpty || field.type=="choice" {
        guard let choice=field.choices.first(where:{$0.caseInsensitiveCompare(text) == .orderedSame}) else {onMessage?(field.name+": choose "+field.choices.joined(separator:", "));return};value=choice
      } else if field.type=="boolean" || field.type=="bool" {
        guard ["on","off","true","false","1","0"].contains(text.lowercased()) else {onMessage?(field.name+": use on or off.");return};value=["on","true","1"].contains(text.lowercased())
      } else {
        guard let number=Double(text),number.isFinite else {onMessage?(field.name+": enter a finite number.");return}
        let stored:Double
        if field.rowUnits {stored=(number*65536*timingUnit.rowScale(model)).rounded()}
        else if field.unit=="beats" {stored=timingUnit == .beats ? number : number/Double(max(1,model.rowsPerBeat))}
        else if field.unit=="cycles-per-beat-or-hz",rateMode != "hz",timingUnit == .rows {stored=number*Double(max(1,model.rowsPerBeat))}
        else {stored=field.percent ? number/100 : number}
        guard stored.isFinite,stored>=field.minimum,stored<=field.maximum else {onMessage?(field.help(model:model,timing:timingUnit,rateMode:rateMode)+": value is outside the supported range.");return}
        if field.rowUnits || field.type=="integer" || field.type=="int" {
          guard stored.rounded()==stored,stored>=Double(Int.min),stored<Double(Int.max) else {onMessage?(field.name+": enter a whole number.");return};value=Int(stored)
        } else {value=stored}
      }
      if field.storage.hasPrefix("parameters.") {var values=command["parameters"] as? [String:Any] ?? [:];values[String(field.storage.dropFirst(11))]=value;command["parameters"]=values}
      else {command[field.storage]=value}
    }
    let offset=(command["offset"] as? NSNumber)?.intValue ?? 0,duration=(command["duration"] as? NSNumber)?.intValue ?? 0
    guard offset>=0,offset<65536,duration>=0,duration<=max(0,model.rows-target.row)*65536-offset else {onMessage?("Timing must remain inside this row and pattern.");return}
    if kind.hasPrefix("nudge-") {
      let maximum=(Double(max(0,model.rows-target.row))-Double(offset)/65536)/Double(max(1,model.rowsPerBeat))
      guard let beats=(command["durationBeats"] as? NSNumber)?.doubleValue,beats.isFinite,beats>=1.0/65536,beats<=65536,beats<=maximum else {
        onMessage?("Nudge duration must be positive and end within this pattern.");return
      }
    }
    pending=true;fields.forEach{$0.isEnabled=false}
    onRequest(["pattern":target.pattern,"row":target.row,"channel":target.channel,"column":fx,"expectedRevision":revision,"command":command]) {[weak self] reply in
      guard let self else{return};self.pending=false;self.fields.forEach{$0.isEnabled=true}
      if let error=reply["error"] as? [String:Any] {self.onMessage?((error["message"] as? String ?? "Could not save effect")+" · draft retained; Esc cancels so you can reopen the current cell.")}
      else {self.onFinish?(advance)}
    }
  }
  func control(_ control:NSControl,textView:NSTextView,doCommandBy selector:Selector)->Bool {
    let index=fields.firstIndex{$0===control} ?? 0
    switch selector {
    case #selector(NSResponder.insertTab(_:)):focus(index:(index+1)%fields.count);return true
    case #selector(NSResponder.insertBacktab(_:)):focus(index:(index+fields.count-1)%fields.count);return true
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
    let x=CGFloat(channelX(target.channel)+model.effectOffset(target.channel,editor.fx)+30)
    let y=CGFloat(headerHeight+Float(target.row-firstRow)*rowHeight)
    let width=CGFloat(editor.slotWidths.reduce(0,+)),left=max(CGFloat(gutterWidth),x),right=min(bounds.width,x+width)
    editor.frame=NSRect(x:left,y:max(CGFloat(headerHeight),min(y,bounds.height-CGFloat(rowHeight))),width:max(0,right-left),height:CGFloat(rowHeight))
    editor.contentOffset=x-left
    editor.toolTip="Editing P\(target.pattern), row \(target.row), channel \(target.channel+1), FX \(editor.fx+1)"
  }
  @discardableResult func beginNudgeEdit(kind:String?=nil,native:String?=nil,replacing:String?=nil,allParameters:Bool=false)->Bool {
    let prior=nudgeEditor,source=prior?.model ?? model
    let command=source.nativeCommand(prior?.target.row ?? cursorRow,prior?.target.channel ?? cursorChannel,prior?.fx ?? effectColumn)
    guard let kind=prior?.kind ?? kind ?? command?.kind,let schema=source.commands.schema(kind:kind,native:prior?.native ?? native ?? command?.native),!schema.inlineFields.isEmpty,column>=3,canEdit() else{return false}
    if let prior {
      if !allParameters || prior.descriptors.count==schema.fields.count {prior.focus(index:selectedParameterIndex);return true}
      guard !prior.pending else {onMessage?("Wait for this effect edit to finish before showing more fields.");return false}
      // End text composition before copying the draft; expanding is not a save.
      guard window?.makeFirstResponder(self) != false else{return false}
    }
    isFollowing=false
    let selectedStorage=prior.flatMap{editor in editor.descriptors.indices.contains(selectedParameterIndex) ? editor.descriptors[selectedParameterIndex].storage : nil}
    let fields=allParameters ? schema.fields : schema.inlineFields
    let selected=selectedStorage.flatMap{storage in fields.firstIndex{$0.storage==storage}} ?? selectedParameterIndex
    let target=prior?.target
    prior?.removeFromSuperview();nudgeEditor=nil
    if let target {cursorRow=target.row;cursorChannel=target.channel;column=target.column}
    inlineDraftLayout=(cursorChannel,effectColumn,fields);model.rebuildEffectLayout()
    column=4+effectColumn*2;parameterIndex=selected
    let editor=PatternNudgeEditor(model:source,target:target ?? navigation,schema:schema,timing:prior?.timingUnit ?? timingUnit,allParameters:allParameters,draft:prior)
    nudgeEditor=editor;addSubview(editor)
    editor.onRequest = {[weak self] params,reply in self?.onNudgeRequest?(params,reply)}
    editor.onMessage = {[weak self] text in self?.onMessage?(text)}
    editor.onFocus = {[weak self] index in self?.parameterIndex=index;self?.revealCursor();self?.positionNudgeEditor()}
    editor.onFinish = {[weak self,weak editor] advance in
      guard let self,let editor,self.nudgeEditor===editor else{return}
      editor.removeFromSuperview();self.nudgeEditor=nil;self.inlineDraftLayout=nil;self.model.rebuildEffectLayout();self.onMessage?("")
      if advance && self.navigation==editor.target {self.cursorRow=min(self.model.rows-1,self.cursorRow+self.step)}
      self.window?.makeFirstResponder(self);self.revealCursor()
    }
    positionNudgeEditor();editor.layoutSubtreeIfNeeded();editor.focus(replacing:replacing,index:selected);onCursor?();return true
  }
}
