import AppKit

struct ScratchPatternTarget {
  let pattern:Int,row:Int,channel:Int,column:Int,rows:Int
  var rowsPerBeat=4
  var revision:String
  var command:[String:Any]?
  var title:String {"Pattern \(pattern) · row \(row) · channel \(channel+1) · FX \(column+1)"}
  var gesture:Int? {
    guard command?["native"] as? String == "scratch" else{return nil}
    return ((command?["parameters"] as? [String:Any])?["gesture"] as? NSNumber)?.intValue
  }
}

// Song-local paired curves. The editor owns its draft, target and revision even
// while the pattern cursor moves; an asynchronous reply never replaces new text.
final class ScratchGestureEditor:NSView,NSTextFieldDelegate {
  let motion=AutomationCanvas(frame:.zero),fader=AutomationCanvas(frame:.zero)
  let gestures=NSPopUpButton(),curve=NSPopUpButton(),snap=NSPopUpButton()
  let name=NSTextField(string:""),position=NSTextField(string:"0"),value=NSTextField(string:"0")
  let formula=NSTextField(string:"mix(start,end,t)")
  let status=Theme.label("",size:11,color:Theme.muted),targetLabel=Theme.label("",size:11,color:Theme.muted)
  let linkage=Theme.label("",size:11,color:Theme.muted),pointLabel=Theme.label("Motion point",size:11,weight:.semibold)
  let curves=["step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"]
  var onRequest:EnvelopeRequest?,onCurrentTarget:(()->ScratchPatternTarget?)?,onReturn:((ScratchPatternTarget)->Void)?,onPreview:((ScratchPatternTarget)->Void)?
  var target:ScratchPatternTarget?
  private(set) var revision="",selectedID:Int?,bank=[[String:Any]](),presets=[[String:Any]](),hasDraft=false,pending=false,invalidated=false
  var activeLane=0
  var generation=0,previewGeneration=0,documentGeneration=0
  var saveWork:DispatchWorkItem?,previewWork:DispatchWorkItem?,afterSave:(()->Void)?
  var baseline:[String:Any]?,formulaWorkbench:FormulaWorkbench?
  var formulaRow:NSStackView!,useButton:ActionButton!,playButton:ActionButton!,presetButton:ActionMenuButton!
  var synchronizing=false,pointFieldsDirty=false,changingSelection=false,rebaseAttempts=0
  var activeCanvas:AutomationCanvas {activeLane==0 ? motion:fader}
  var hasPendingEdits:Bool {hasDraft || pointFieldsDirty}

  init(target:ScratchPatternTarget?=nil) {
    self.target=target
    super.init(frame:.zero)
    gestures.target=self;gestures.action = #selector(selectGesture);gestures.setAccessibilityLabel("Song scratch phrase")
    name.placeholderString="Phrase name";name.delegate=self;name.setAccessibilityLabel("Scratch phrase name")
    name.setContentHuggingPriority(.defaultLow,for:.horizontal)
    curve.addItems(withTitles:["Step","Linear","Smooth","Exponential","Logarithmic","Step at start","Exponential reversed","Logarithmic reversed","Scripted"])
    curve.selectItem(at:2);curve.target=self;curve.action = #selector(changeCurve);curve.setAccessibilityLabel("Scratch point outgoing curve")
    snap.addItems(withTitles:["1/16 cycle","1/32 cycle","1/64 cycle","Free"]);snap.target=self;snap.action = #selector(changeSnap);snap.setAccessibilityLabel("Scratch cycle grid")
    for (index,canvas) in [motion,fader].enumerated() {
      canvas.rows=257;canvas.positionLimit=65536;canvas.axisUnits=65536;canvas.fixedPositions=[0,65536];canvas.pointLimit=256;canvas.snap=4096
      canvas.setAccessibilityLabel(index==0 ? "Scratch record motion envelope":"Scratch fader envelope")
      canvas.setAccessibilityHelp("Time runs from zero to one cycle. Click to add a point, drag to move it; Tab selects points, arrows move them, Shift gives fine adjustment. End points remain at the cycle boundaries. Changes save on release.")
      canvas.heightAnchor.constraint(greaterThanOrEqualToConstant:140).isActive=true
      canvas.onSelect = {[weak self] in guard let self else{return};self.activeLane=index;self.showPoint()}
      canvas.onEdit = {[weak self] in self?.markDraft()}
      canvas.onEditFinished = {[weak self] in self?.save()}
      canvas.onViewport = {[weak self] in self?.synchronizeViewport(from:index)}
    }
    for field in [position,value] {field.fixed(width:80);field.delegate=self;field.target=self;field.action = #selector(commitPoint);field.toolTip="Return or leave the field to commit. Time is a fraction of one complete cycle."}
    position.setAccessibilityLabel("Scratch point cycle position");value.setAccessibilityLabel("Scratch point value percent")
    formula.font = .monospacedSystemFont(ofSize:11,weight:.regular);formula.delegate=self;formula.setAccessibilityLabel("Scratch point formula")
    formula.setContentHuggingPriority(.defaultLow,for:.horizontal)
    formulaRow=stack(.horizontal,[formula,ActionButton("Expand…"){[weak self] in self?.expandFormula()},ActionButton("Reference"){[weak self] in self?.showFormulaReference()}],spacing:5)
    formulaRow.isHidden=true
    presetButton=ActionMenuButton("New phrase…"){[weak self] in self?.presetMenu() ?? NSMenu()};presetButton.toolTip="Create a song phrase from a scratch technique"
    useButton=ActionButton("Use in pattern"){[weak self] in self?.useInPattern()};useButton.toolTip="Place SK at the captured FX cell; duration and travel remain editable directly in the pattern."
    playButton=ActionButton("Play from row",symbol:"play.fill"){[weak self] in self?.play()};playButton.toolTip="Audition the captured pattern row. Space remains the global playback shortcut."
    let more=ActionMenuButton{[weak self] in self?.actionsMenu() ?? NSMenu()}
    let intro=Theme.label("Two hands, one phrase: Motion moves through the sample; Fader cuts the sound. SK in the pattern sets duration, travel and repeats.",size:12,color:Theme.muted)
    intro.maximumNumberOfLines=2;intro.lineBreakMode = .byWordWrapping;intro.preferredMaxLayoutWidth=700
    let motionHelp=Theme.label("RECORD MOTION · rising = forward · falling = reverse · flat = held",size:10,color:Theme.muted)
    let faderHelp=Theme.label("FADER · 100% = open · 0% = closed · sample motion continues while closed",size:10,color:Theme.muted)
    let top=stack(.horizontal,[gestures,name,presetButton!,more],spacing:6)
    let pointRow=stack(.horizontal,[pointLabel,Theme.label("Cycle",size:11),position,Theme.label("%",size:11),value,curve,snap,NSView(),ActionButton("−"){[weak self] in self?.activeCanvas.zoom(0.5)},ActionButton("+"){[weak self] in self?.activeCanvas.zoom(2)},ActionButton("Fit"){[weak self] in self?.activeCanvas.fit()}],spacing:5)
    let bottom=stack(.horizontal,[targetLabel,NSView(),ActionButton("Current cursor"){[weak self] in self?.captureCurrent()},ActionButton("Return to row"){[weak self] in guard let self,let target=self.target else{return};self.onReturn?(target)},playButton!,useButton!],spacing:6)
    let body=stack(.vertical,[intro,top,linkage,motionHelp,motion,faderHelp,fader,pointRow,formulaRow!,bottom,status],spacing:6)
    body.stretchAcrossAxis();body.fill(self,inset:12)
    motion.heightAnchor.constraint(equalTo:fader.heightAnchor).isActive=true
    for label in [status,linkage,targetLabel] {label.lineBreakMode = .byTruncatingTail;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    for popup in [gestures,curve,snap] {popup.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    targetLabel.stringValue=target?.title ?? "Select a pattern FX cell to place this phrase"
    controls()
  }
  required init?(coder:NSCoder){fatalError()}
  deinit {saveWork?.cancel();previewWork?.cancel()}

  static func points(_ raw:Any?)->[EnvelopePoint] {
    (raw as? [[String:Any]] ?? []).compactMap { item in
      guard let p=(item["position"] as? NSNumber)?.intValue,let v=(item["value"] as? NSNumber)?.doubleValue,(0...65536).contains(p),v.isFinite else{return nil}
      return EnvelopePoint(position:p,value:v,curve:item["curve"] as? String ?? "linear",formula:item["formula"] as? String ?? "")
    }.sorted{$0.position<$1.position}
  }
  static func content(_ item:[String:Any])->NSDictionary {NSDictionary(dictionary:item.filter{["id","name","motion","fader"].contains($0.key)})}
  static func encoded(_ points:[EnvelopePoint])->[[String:Any]] {points.map{point in var raw=point.dictionary;if point.curve != "scripted"{raw.removeValue(forKey:"formula")};return raw}}
  var cycleBeats:Double {let p=target?.command?["parameters"] as? [String:Any] ?? [:];return (p["beats"] as? NSNumber)?.doubleValue ?? 1}
  var previewBeats:Double {let p=target?.command?["parameters"] as? [String:Any] ?? [:];return cycleBeats/max(1,(p["repeats"] as? NSNumber)?.doubleValue ?? 1)}
  var draft:[String:Any] {var result:[String:Any]=["name":name.stringValue,"motion":Self.encoded(motion.points),"fader":Self.encoded(fader.points)];if let selectedID{result["id"]=selectedID};return result}
  func controls(){
    let editable=selectedID != nil && !invalidated && !changingSelection
    motion.allowsEditing=editable;fader.allowsEditing=editable;name.isEnabled=editable
    gestures.isEnabled = !pending && !hasDraft && !invalidated;presetButton?.isEnabled = !pending && !hasDraft && !invalidated
    useButton?.isEnabled=editable && target != nil && !pending;playButton?.isEnabled=target != nil && !pending && !hasDraft && !invalidated
    let chosen=activeCanvas.selected.flatMap{activeCanvas.points.indices.contains($0) ? activeCanvas.points[$0]:nil}
    position.isEnabled=editable && chosen != nil && ![0,65536].contains(chosen?.position ?? -1);value.isEnabled=editable && chosen != nil
    curve.isEnabled=editable && chosen != nil;formula.isEnabled=editable && chosen != nil
    if let item=bank.first(where:{$0["id"] as? Int==selectedID}) {let uses=item["uses"] as? Int ?? 0;linkage.stringValue="Song phrase \(selectedID ?? 0) · \(uses) linked pattern use\(uses==1 ? "":"s") · edits update every use · Make unique creates a variation"}
    else {linkage.stringValue="Create a phrase from the New phrase menu, then use it at any SK cell."}
  }
  func load(select:Int?=nil){
    guard !pending,!hasPendingEdits,!invalidated,let onRequest else{return}
    pending=true;changingSelection=true;controls();let token=documentGeneration,wanted=select ?? selectedID ?? target?.gesture
    onRequest("scratch.gestures.get",[:]){[weak self] response in
      guard let self,self.documentGeneration==token else{return};self.pending=false;self.changingSelection=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{self.error(response);return}
      let nextRevision=result["revision"] as? String ?? ""
      if !self.revision.isEmpty,self.revision.split(separator:":").first != nextRevision.split(separator:":").first {self.invalidate();return}
      self.revision=nextRevision;self.bank=data["gestures"] as? [[String:Any]] ?? [];self.presets=data["presets"] as? [[String:Any]] ?? []
      self.rebuildGestureMenu(select:wanted)
      self.showGesture();self.status.stringValue=self.bank.isEmpty ? "Choose New phrase to start with a scratch technique.":"Changes save immediately · Undo restores edits · End points stay at cycle 0 and 1"
    }
  }
  func rebuildGestureMenu(select wanted:Int?){
    let choices=bank.compactMap{item->(Int,String)? in guard let id=item["id"] as? Int else{return nil};return(id,"\(id) · \(item["name"] as? String ?? "Scratch")")}
    if gestures.numberOfItems != choices.count || zip(gestures.itemArray,choices).contains(where:{$0.0.representedObject as? Int != $0.1.0 || $0.0.title != $0.1.1}) {
      gestures.removeAllItems()
      for (id,title) in choices {let item=NSMenuItem(title:title,action:nil,keyEquivalent:"");item.representedObject=id;gestures.menu?.addItem(item)}
    }
    if let wanted,let index=choices.firstIndex(where:{$0.0==wanted}){gestures.selectItem(at:index)}
    else if wanted != nil{gestures.select(nil)}
  }
  func showGesture(){
    selectedID=gestures.selectedItem?.representedObject as? Int
    baseline=bank.first{$0["id"] as? Int==selectedID};name.stringValue=baseline?["name"] as? String ?? ""
    motion.points=Self.points(baseline?["motion"]);fader.points=Self.points(baseline?["fader"])
    motion.selected=nil;fader.selected=nil;pointFieldsDirty=false;hasDraft=false;generation+=1
    showPoint();preview();controls()
  }
  @objc func selectGesture(){guard !pending,!hasDraft else{return};showGesture()}
  @objc func changeSnap(){let step=[4096,2048,1024,1][max(0,snap.indexOfSelectedItem)];motion.snap=step;fader.snap=step}
  func synchronizeViewport(from lane:Int){
    guard !synchronizing else{return};synchronizing=true
    let source=lane==0 ? motion:fader,other=lane==0 ? fader:motion
    other.visibleStart=source.visibleStart;other.visibleEnd=source.visibleEnd;other.needsDisplay=true;synchronizing=false;preview()
  }
  func showPoint(){
    let canvas=activeCanvas;pointLabel.stringValue=activeLane==0 ? "Motion point":"Fader point"
    guard let index=canvas.selected,canvas.points.indices.contains(index) else{formulaRow.isHidden=true;controls();return}
    let p=canvas.points[index];position.stringValue=String(format:"%.8g",Double(p.position)/65536);value.stringValue=String(format:"%.7g",p.value*100)
    curve.selectItem(at:curves.firstIndex(of:p.curve) ?? 1);canvas.curve=p.curve
    formula.stringValue=p.formula;formulaRow.isHidden=p.curve != "scripted";if p.curve=="scripted"{FormulaCatalog.load(onRequest)}
    controls()
  }
  @objc func changeCurve(){
    let canvas=activeCanvas;guard let i=canvas.selected,canvas.points.indices.contains(i) else{return}
    let p=canvas.points[i];canvas.replaceSelected(position:p.position,value:p.value,curve:curves[max(0,curve.indexOfSelectedItem)])
  }
  func controlTextDidChange(_ notification:Notification){
    guard !invalidated else{return}
    if notification.object as? NSTextField === name {markDraft()}
    else if notification.object as? NSTextField === formula,let i=activeCanvas.selected,activeCanvas.points.indices.contains(i){activeCanvas.points[i].formula=formula.stringValue;FormulaCatalog.suggest(formula.currentEditor() as? NSTextView);markDraft()}
    else {pointFieldsDirty=true}
  }
  func controlTextDidEndEditing(_ notification:Notification){if notification.object as? NSTextField === position || notification.object as? NSTextField === value{commitPoint()}}
  func control(_ control:NSControl,textView:NSTextView,completions words:[String],forPartialWordRange range:NSRange,indexOfSelectedItem index:UnsafeMutablePointer<Int>)->[String]{guard control === formula else{return words};index.pointee = -1;return FormulaCatalog.completions(textView.string,range:range)}
  @objc func commitPoint(){
    guard pointFieldsDirty else{return}
    guard let p=Double(position.stringValue),let v=Double(value.stringValue),p.isFinite,v.isFinite,(0...1).contains(p),(0...100).contains(v),let i=activeCanvas.selected,activeCanvas.points.indices.contains(i) else{status.stringValue="Cycle position must be 0–1 and value 0–100%. Your entry is retained.";return}
    let next=Int((p*65536).rounded());guard !activeCanvas.points.enumerated().contains(where:{$0.offset != i && $0.element.position==next}) else{status.stringValue="Another point is already at that cycle position.";return}
    pointFieldsDirty=false;activeCanvas.replaceSelected(position:next,value:v/100,curve:curves[max(0,curve.indexOfSelectedItem)]);save()
  }
  func markDraft(){guard selectedID != nil,!invalidated else{return};rebaseAttempts=0;hasDraft=true;generation+=1;status.stringValue=motion.isDragging || fader.isDragging ? "Release to save this gesture · Undo restores it":"Saving phrase…";controls();preview();saveSoon()}
  func saveSoon(){
    saveWork?.cancel();saveWork=nil
    guard hasDraft,!pending,!pointFieldsDirty,!motion.isDragging,!fader.isDragging,!invalidated else{return}
    let token=generation,document=documentGeneration
    let work=DispatchWorkItem{[weak self] in guard let self,self.generation==token,self.documentGeneration==document else{return};self.saveWork=nil;self.save()};saveWork=work
    DispatchQueue.main.asyncAfter(deadline:.now()+0.18,execute:work)
  }
  func save(){
    guard hasDraft,!pending,!pointFieldsDirty,!motion.isDragging,!fader.isDragging,!invalidated else{return}
    saveWork?.cancel();saveWork=nil
    let sent=draft,token=generation,previous=revision
    var params=sent;params["expectedRevision"]=previous;pending=true;controls()
    request("scratch.gestures.set",params){[weak self] response in
      guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any] else{if (response["error"] as? [String:Any])?["code"] as? Int == -32001,self.rebaseAttempts<2 {self.rebaseAttempts+=1;self.retry();return};self.afterSave=nil;self.error(response);return}
      self.acceptRevision(result["revision"] as? String ?? previous,previous:previous);self.baseline=sent
      self.gestures.selectedItem?.title="\(self.selectedID ?? 0) · \(sent["name"] as? String ?? "Scratch")"
      if let index=self.bank.firstIndex(where:{$0["id"] as? Int==self.selectedID}){let uses=self.bank[index]["uses"];self.bank[index]=sent;self.bank[index]["uses"]=uses}
      if self.generation==token {self.hasDraft=false;self.status.stringValue="Saved · all linked uses play this motion and fader";self.controls();let action=self.afterSave;self.afterSave=nil;action?()}
      else {self.controls();self.saveSoon()}
    }
  }
  private func acceptRevision(_ next:String,previous:String){revision=next;if target?.revision==previous{target?.revision=next}}
  private func request(_ method:String,_ params:[String:Any],attempt:Int=0,completion:@escaping ([String:Any])->Void){
    guard let onRequest,!invalidated else{return};let document=documentGeneration
    onRequest(method,params){[weak self] response in
      guard let self,self.documentGeneration==document,!self.invalidated else{return}
      if (response["error"] as? [String:Any])?["code"] as? Int == -32002,attempt<5 {DispatchQueue.main.asyncAfter(deadline:.now()+0.05*Double(attempt+1)){[weak self] in self?.request(method,params,attempt:attempt+1,completion:completion)}}
      else{completion(response)}
    }
  }
  func error(_ response:[String:Any]){status.stringValue=((response["error"] as? [String:Any])?["message"] as? String ?? "Scratch operation failed")+(hasDraft ? " · Your draft is retained. Retry or reload in More.":"");controls()}
  func presetMenu()->NSMenu {
    let menu=NSMenu();menu.autoenablesItems=false
    for preset in presets {guard let id=preset["id"] as? String else{continue};menu.addItem(ContextAction(preset["name"] as? String ?? id,enabled:!pending && !hasDraft){[weak self] in self?.create(preset:id)})}
    if presets.isEmpty {let item=NSMenuItem(title:"Loading scratch techniques…",action:nil,keyEquivalent:"");item.isEnabled=false;menu.addItem(item)}
    return menu
  }
  func actionsMenu()->NSMenu {
    let menu=NSMenu();menu.autoenablesItems=false
    menu.addItem(ContextAction("Duplicate phrase",enabled:selectedID != nil && !pending && !hasDraft){[weak self] in self?.duplicate(use:false)})
    menu.addItem(ContextAction("Make unique at captured row",enabled:selectedID != nil && target?.gesture==selectedID && !pending && !hasDraft){[weak self] in self?.duplicate(use:true)})
    menu.addItem(.separator())
    menu.addItem(ContextAction("Delete selected interior point",key:"\u{7f}",enabled:activeCanvas.selected.map{activeCanvas.points.indices.contains($0) && ![0,65536].contains(activeCanvas.points[$0].position)} ?? false){[weak self] in self?.activeCanvas.removeSelected()})
    menu.addItem(ContextAction("Delete unused phrase",enabled:selectedID != nil && !pending && !hasDraft && (bank.first{$0["id"] as? Int==selectedID}?["uses"] as? Int ?? 0)==0){[weak self] in self?.remove()})
    menu.addItem(.separator())
    menu.addItem(ContextAction("Retry saving retained changes",enabled:hasDraft && !pending){[weak self] in self?.retry()})
    menu.addItem(ContextAction("Reload / discard retained changes",enabled:!pending){[weak self] in self?.hasDraft=false;self?.pointFieldsDirty=false;self?.load()})
    return menu
  }
  func duplicate(use:Bool){
    guard let selectedID,!pending,!hasDraft,!invalidated else{return}
    guard !use || target?.gesture==selectedID else{status.stringValue="Choose the phrase used by the captured row before making it unique.";return}
    let captured=target,previous=use ? captured!.revision:revision
    let copyName=name.stringValue.lengthOfBytes(using:.utf8)<=251 ? name.stringValue+" copy":name.stringValue
    var params:[String:Any]=["id":selectedID,"name":copyName,"expectedRevision":previous]
    if use,let captured{params["target"]=["pattern":captured.pattern,"row":captured.row,"channel":captured.channel,"column":captured.column]}
    pending=true;changingSelection=true;controls()
    request("scratch.gestures.clone",params){[weak self] response in
      guard let self else{return};self.pending=false;self.changingSelection=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let id=data["id"] as? Int else{self.error(response);return}
      self.acceptRevision(result["revision"] as? String ?? previous,previous:previous)
      if use,var command=captured?.command {
        var parameters=command["parameters"] as? [String:Any] ?? [:];parameters["gesture"]=id;command["parameters"]=parameters;self.target?.command=command
      }
      self.selectedID=id;self.load(select:id)
    }
  }
  func create(preset:String){
    guard !pending,!hasDraft,!invalidated else{return};var params:[String:Any]=["preset":preset];let previous=revision;params["expectedRevision"]=previous;pending=true;changingSelection=true;controls()
    request("scratch.gestures.set",params){[weak self] response in
      guard let self else{return};self.pending=false;self.changingSelection=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let id=data["id"] as? Int else{self.error(response);return}
      self.acceptRevision(result["revision"] as? String ?? previous,previous:previous);self.selectedID=id
      self.load(select:id)
    }
  }
  func remove(){
    guard let selectedID,!pending,!hasDraft else{return};pending=true;controls();let previous=revision
    request("scratch.gestures.remove",["id":selectedID,"expectedRevision":previous]){[weak self] response in
      guard let self else{return};self.pending=false;guard let result=response["result"] as? [String:Any] else{self.error(response);return}
      self.acceptRevision(result["revision"] as? String ?? previous,previous:previous);self.selectedID=nil;self.load()
    }
  }
  func useInPattern(){guard let id=selectedID,!invalidated else{return};commitPoint();guard !pointFieldsDirty else{status.stringValue="Finish the cycle position and value before placing this phrase.";return};if hasDraft || pending{afterSave={[weak self] in self?.writePattern(id:id)};save()}else{writePattern(id:id)}}
  private func writePattern(id:Int,then:(()->Void)?=nil){
    guard !pending,let captured=target,!invalidated else{return}
    var command=captured.command?["native"] as? String=="scratch" ? captured.command!:["kind":"native","native":"scratch","parameters":["beats":min(1,Double(max(1,captured.rows-captured.row))/Double(max(1,captured.rowsPerBeat))),"travelMs":200.0,"repeats":1.0,"reverse":false]]
    var params=command["parameters"] as? [String:Any] ?? [:];params["gesture"]=id;command["parameters"]=params
    pending=true;controls()
    request("pattern.effect.set",["pattern":captured.pattern,"row":captured.row,"channel":captured.channel,"column":captured.column,"command":command,"expectedRevision":captured.revision]){[weak self] response in
      guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any] else{self.error(response);then?();return}
      self.acceptRevision(result["revision"] as? String ?? self.revision,previous:captured.revision);self.target?.command=command
      if captured.gesture != id {
        for index in self.bank.indices {
          let phraseID=self.bank[index]["id"] as? Int,uses=self.bank[index]["uses"] as? Int ?? 0
          if phraseID==captured.gesture {self.bank[index]["uses"]=max(0,uses-1)}
          if phraseID==id {self.bank[index]["uses"]=uses+1}
        }
      }
      self.status.stringValue="SK \(id) placed at \(captured.title) · Return in the pattern edits duration, travel and repeats";self.controls();then?()
    }
  }
  func captureCurrent(){
    guard !pending,!invalidated,let next=onCurrentTarget?() else{return}
    commitPoint();guard !pointFieldsDirty else{status.stringValue="Finish the point value before changing the captured pattern destination.";return}
    target=next;targetLabel.stringValue=next.title
    if !hasDraft {load(select:next.gesture ?? selectedID)}else{controls()}
  }
  func play(){guard !pending,!hasDraft,!invalidated,let target else{return};onPreview?(target)}
  func observeRevision(_ next:String){
    guard !revision.isEmpty,next != revision,!invalidated else{return}
    guard revision.split(separator:":").first==next.split(separator:":").first else{invalidate();return}
    guard !pending,!hasDraft,!pointFieldsDirty,!motion.isDragging,!fader.isDragging else{return}
    let document=documentGeneration,token=generation
    request("scratch.gestures.get",[:]){[weak self] response in
      guard let self,self.documentGeneration==document,self.generation==token,!self.pending,!self.hasDraft,!self.pointFieldsDirty else{return}
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let next=result["revision"] as? String else{return}
      let items=data["gestures"] as? [[String:Any]] ?? [],current=items.first{$0["id"] as? Int==self.selectedID}
      self.revision=next;self.bank=items;self.rebuildGestureMenu(select:self.selectedID)
      if let current,let baseline=self.baseline,Self.content(current).isEqual(Self.content(baseline)){self.controls();return}
      let motionPoint=self.motion.selected.flatMap{self.motion.points.indices.contains($0) ? self.motion.points[$0].position:nil}
      let faderPoint=self.fader.selected.flatMap{self.fader.points.indices.contains($0) ? self.fader.points[$0].position:nil}
      self.baseline=current;self.name.stringValue=current?["name"] as? String ?? "";self.motion.points=Self.points(current?["motion"]);self.fader.points=Self.points(current?["fader"])
      self.motion.selected=self.motion.points.firstIndex{$0.position==motionPoint};self.fader.selected=self.fader.points.firstIndex{$0.position==faderPoint}
      if current==nil{self.selectedID=nil};self.generation+=1;self.showPoint();self.preview();self.status.stringValue="Phrase refreshed from the song · Current cursor refreshes the pattern destination"
    }
  }
  func invalidate(){invalidated=true;documentGeneration+=1;pending=false;saveWork?.cancel();previewWork?.cancel();afterSave=nil;status.stringValue="A different song is open. This phrase draft is retained; reopen Scratch phrases for the new song.";controls()}
  // Rebase a retained bank draft only when its original phrase is unchanged.
  // Pattern insertion deliberately keeps its separate captured-cell revision.
  func retry(){
    guard hasDraft,!pending,!invalidated,let baseline,let onRequest else{return};pending=true;controls();let document=documentGeneration
    onRequest("scratch.gestures.get",[:]){[weak self] response in
      guard let self,self.documentGeneration==document else{return};self.pending=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let current=(data["gestures"] as? [[String:Any]])?.first(where:{$0["id"] as? Int==self.selectedID}),let next=result["revision"] as? String else{self.error(response);return}
      guard self.revision.split(separator:":").first==next.split(separator:":").first,Self.content(current).isEqual(Self.content(baseline)) else{self.afterSave=nil;self.status.stringValue="This song phrase changed elsewhere. Your draft is retained; reload to choose the current version.";self.controls();return}
      self.revision=next;self.save()
    }
  }
  func preview(){
    previewGeneration+=1;let token=previewGeneration;previewWork?.cancel()
    guard onRequest != nil,[motion,fader].contains(where:{$0.points.contains(where:{$0.curve=="scripted"})}) else{return}
    let work=DispatchWorkItem{[weak self] in
      guard let self,self.previewGeneration==token else{return}
      for canvas in [self.motion,self.fader] where canvas.points.contains(where:{$0.curve=="scripted"}) {
        let params:[String:Any]=["points":Self.encoded(canvas.points),"rows":257,"rowsPerBeat":256,"span":65537,"scratchBeats":self.previewBeats,"start":canvas.visibleStart,"end":canvas.horizontalEnd,"samples":1024]
        self.request("automation.formula.preview",params){[weak self,weak canvas] response in
          guard let self,self.previewGeneration==token,let canvas else{return}
          if let values=((response["result"] as? [String:Any])?["data"] as? [String:Any])?["values"] as? [[Double]] {canvas.previewValues=values.filter{$0.count==2}.map{($0[0],$0[1])}}
        }
      }
    };previewWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.12,execute:work)
  }
  func showFormulaReference(){FormulaWorkbench.showReference(onRequest,scratchContext:true)}
  func expandFormula(){
    if let formulaWorkbench,formulaWorkbench.window?.isVisible==true{formulaWorkbench.window?.makeKeyAndOrderFront(nil);return}
    let canvas=activeCanvas;guard let index=canvas.selected,canvas.points.indices.contains(index),canvas.points[index].curve=="scripted" else{return}
    let token=generation,id=selectedID,lane=activeLane,document=documentGeneration
    formulaWorkbench=FormulaWorkbench(source:canvas.points[index].formula,title:"\(name.stringValue) · \(lane==0 ? "motion":"fader")",points:Self.encoded(canvas.points),selected:index,rows:257,rowsPerBeat:256,span:65537,scratchContext:true,request:{[weak self] method,params,reply in guard let self else{return};var params=params;if method=="automation.formula.preview"{params["scratchBeats"]=self.previewBeats};self.request(method,params,completion:reply)}){[weak self] text in
      guard let self,self.generation==token,self.documentGeneration==document,self.selectedID==id,self.activeLane==lane,canvas.selected==index,canvas.points.indices.contains(index),!self.invalidated else{return false}
      canvas.points[index].formula=text;self.formula.stringValue=text;self.markDraft();return true
    }
  }
}
