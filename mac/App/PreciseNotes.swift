import AppKit

struct PreciseNote {
  let channel:Int,position:Int,note:Int,instrument:Int,velocity:Int,effect:Int,parameter:Int
  var row:Int {position/65536}
  var name:String {Self.name(note)}
  var dictionary:[String:Int] {
    var data=["channel":channel,"position":position,"note":note,"instrument":instrument,"velocity":velocity]
    if effect != 0 {data["effect"]=effect;data["parameter"]=parameter};return data
  }
  static func name(_ note:Int)->String {
    if note==255{return "Note off"};if note==254{return "Cut"}
    guard (1...120).contains(note) else{return "---"}
    return ["C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"][(note-1)%12]+String((note-1)/12)
  }
  init(_ data:[String:Any]) {channel=data["channel"] as? Int ?? 0;position=data["position"] as? Int ?? 0;note=data["note"] as? Int ?? 0;instrument=data["instrument"] as? Int ?? 0;velocity=data["velocity"] as? Int ?? 127;effect=data["effect"] as? Int ?? 0;parameter=data["parameter"] as? Int ?? 0}
}

// One detail view of a row, with its containing beat above it. All editing is
// in integer native positions; the beat display never changes stored timing.
final class PreciseNoteTimeline:NSView {
  var events=[PreciseNote](),selected:Int?,row=0,rowsPerBeat=4,snapBeats=0.0,enabled=true
  var onSelect:((Int)->Bool)?,onMove:((Int,Int,Int,Bool)->Void)?,onInsert:((Int)->Void)?,onDelete:(()->Void)?,onDuplicate:(()->Void)?
  private var dragging:Int?,dragVolume=127
  override var isFlipped:Bool {true}
  override var acceptsFirstResponder:Bool {true}
  var plot:NSRect {NSRect(x:32,y:76,width:max(1,bounds.width-52),height:max(30,bounds.height-111))}
  override init(frame:NSRect) {
    super.init(frame:frame);setAccessibilityElement(true);setAccessibilityRole(.group);setAccessibilityLabel("Precise note timeline")
    setAccessibilityHelp("Drag a hit left or right for timing, up or down for volume. Shift keeps its volume. Option bypasses snap. Double-click to add a hit. Arrow keys adjust the selected hit; Delete removes it.")
  }
  required init?(coder:NSCoder){fatalError()}
  func offset(at x:CGFloat,free:Bool=false)->Int {
    var units=Double((x-plot.minX)/plot.width)*65536
    if snapBeats>0 && !free {
      let step=snapBeats*Double(max(1,rowsPerBeat))*65536
      units=((Double(row)*65536+units)/step).rounded()*step-Double(row)*65536
    }
    return max(0,min(65535,Int(max(-65536,min(131072,units)).rounded())))
  }
  func point(_ event:PreciseNote)->NSPoint {
    NSPoint(x:plot.minX+CGFloat(event.position%65536)/65536*plot.width,
      y:event.note<128 ? plot.maxY-CGFloat(event.velocity)/127*plot.height : plot.maxY)
  }
  private func text(_ value:String,_ x:CGFloat,_ y:CGFloat,_ color:NSColor=Theme.muted,_ size:CGFloat=10) {
    (value as NSString).draw(at:NSPoint(x:x,y:y),withAttributes:[.font:NSFont.monospacedSystemFont(ofSize:size,weight:.regular),.foregroundColor:color])
  }
  override func draw(_ dirtyRect:NSRect) {
    NSColor(calibratedRed:0.045,green:0.065,blue:0.085,alpha:1).setFill();bounds.fill()
    let beat=max(1,rowsPerBeat),slot=row%beat,beatStart=row-slot,context=NSRect(x:plot.minX,y:24,width:plot.width,height:20)
    NSColor(calibratedWhite:0.15,alpha:1).setFill();context.fill()
    let highlight=NSRect(x:context.minX+CGFloat(slot)/CGFloat(beat)*context.width,y:context.minY,width:context.width/CGFloat(beat),height:context.height)
    Theme.accent.withAlphaComponent(0.35).setFill();highlight.fill()
    text("BEAT \(row/beat+1) · rows \(beatStart)–\(beatStart+beat-1)",plot.minX,6)
    if beat<=32 {for n in 0...beat {let x=context.minX+CGFloat(n)/CGFloat(beat)*context.width;NSColor.gray.setStroke();let p=NSBezierPath();p.move(to:NSPoint(x:x,y:context.minY));p.line(to:NSPoint(x:x,y:context.maxY));p.stroke()}}
    text("start",context.minX,46);text("end",context.maxX-20,46)
    text("ROW \(row) · drag hits",plot.minX,61,Theme.text)
    for n in 0...4 {
      let x=plot.minX+CGFloat(n)/4*plot.width,p=NSBezierPath();p.move(to:NSPoint(x:x,y:plot.minY));p.line(to:NSPoint(x:x,y:plot.maxY))
      NSColor(calibratedWhite:n==0 || n==4 ? 0.45:0.22,alpha:1).setStroke();p.stroke()
      let label=String(format:"%.4g b",Double(n)/4/Double(beat));text(label,min(x,plot.maxX-44),plot.maxY+7)
    }
    text("127",2,plot.minY);text("1",16,plot.maxY-10)
    for (index,event) in events.enumerated() where event.row==row {
      let point=point(event),color=index==selected ? Theme.accent : NSColor(calibratedRed:0.48,green:0.64,blue:0.78,alpha:1)
      color.setStroke();let line=NSBezierPath();line.lineWidth=index==selected ? 2:1;line.move(to:NSPoint(x:point.x,y:plot.maxY));line.line(to:point);line.stroke()
      color.setFill();NSBezierPath(ovalIn:NSRect(x:point.x-5,y:point.y-5,width:10,height:10)).fill()
      if index==selected {text("\(event.name) · \(event.velocity)",max(plot.minX,min(point.x+8,plot.maxX-98)),max(plot.minY,point.y-16),color,11)}
    }
    if events.isEmpty {text("Double-click to add a hit",plot.minX+12,plot.midY,Theme.muted,12)}
    setAccessibilityValue("Row \(row), \(events.count) events, \(1.0/Double(beat)) beats long")
  }
  override func mouseDown(with event:NSEvent) {
    guard enabled else{return};window?.makeFirstResponder(self)
    let p=convert(event.locationInWindow,from:nil);guard p.y>=plot.minY-10 else{return}
    let hit=events.indices.min {hypot(point(events[$0]).x-p.x,point(events[$0]).y-p.y)<hypot(point(events[$1]).x-p.x,point(events[$1]).y-p.y)}
    if let hit,hypot(point(events[hit]).x-p.x,point(events[hit]).y-p.y)<16 {
      guard onSelect?(hit) != false else{return};selected=hit;dragging=hit;dragVolume=events[hit].velocity;needsDisplay=true
    } else if event.clickCount==2 {onInsert?(offset(at:p.x,free:event.modifierFlags.contains(.option)))}
  }
  override func mouseDragged(with event:NSEvent) {
    guard enabled,let dragging,events.indices.contains(dragging) else{return}
    let p=convert(event.locationInWindow,from:nil)
    let volume=event.modifierFlags.contains(.shift) ? dragVolume : max(1,min(127,Int(((plot.maxY-p.y)/plot.height*127).rounded())))
    onMove?(dragging,offset(at:p.x,free:event.modifierFlags.contains(.option)),volume,false)
  }
  override func mouseUp(with event:NSEvent) {
    if let dragging,events.indices.contains(dragging) {let e=events[dragging];onMove?(dragging,e.position%65536,e.velocity,true)}
    dragging=nil
  }
  override func keyDown(with event:NSEvent) {
    guard enabled else{return}
    if event.keyCode==51 || event.keyCode==117 {onDelete?();return}
    if event.modifierFlags.contains(.command),event.charactersIgnoringModifiers=="d" {onDuplicate?();return}
    guard let selected,events.indices.contains(selected) else{super.keyDown(with:event);return}
    let e=events[selected],step=event.modifierFlags.contains(.option) ? 1 : max(1,snapBeats>0 ? Int((snapBeats*Double(rowsPerBeat)*65536).rounded()):256)
    switch event.keyCode {
    case 123,124:onMove?(selected,max(0,min(65535,e.position%65536+(event.keyCode==123 ? -step:step))),e.velocity,true)
    case 125,126:onMove?(selected,e.position%65536,max(1,min(127,e.velocity+(event.keyCode==125 ? -1:1))),true)
    default:super.keyDown(with:event)
    }
  }
}

final class PreciseNotesEditor:NSView,NSTableViewDataSource,NSTableViewDelegate,NSTextFieldDelegate {
  var onContext:(()->(PatternModel,Int,Int,Int))?
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  let location=Theme.label("",size:12),status=Theme.label("",size:12,color:Theme.muted),table=NSTableView(),timeline=PreciseNoteTimeline()
  let note=NSPopUpButton(),instrument=NSTextField(string:"1"),velocity=NSTextField(string:"127"),offset=NSTextField(string:"0"),units=NSPopUpButton(),snap=NSPopUpButton()
  let effect=NSPopUpButton(),parameter=NSTextField(string:"00"),effectHint=Theme.label("",size:11,color:Theme.muted),offsetHint=Theme.label("",size:11,color:Theme.muted)
  let repeatCount=NSTextField(string:"4"),endVolume=NSTextField(string:"127")
  let replaceLegacy=NSButton(checkboxWithTitle:"Replace the ordinary note in this row",target:nil,action:nil)
  var reloadButton:ActionButton!,addButton:ActionButton!,updateButton:ActionButton!,removeButton:ActionButton!,previewButton:ActionButton!,applyButton:ActionButton!,repeatButton:ActionButton!
  private(set) var revision:String?,events=[[String:Any]](),draft=[[String:Any]](),pending=false,capturedPattern=0,capturedRow=0,capturedChannel=0,rowsPerBeat=4
  private var editingRow:Int?,updatingTable=false,effects=[PatternCommand](),displayBeats=true,moveLegacyEffect=false
  private var effectValues=[Set<Int>?]()
  private var originalDraft = [[String:Any]](), originalFields = [String]()
  private var fieldValues:[String] { [offset.stringValue,String(note.selectedTag()),instrument.stringValue,velocity.stringValue,String(effect.indexOfSelectedItem),parameter.stringValue] }
  var hasDraft:Bool { !NSArray(array:draft).isEqual(to:originalDraft) || (!originalFields.isEmpty && fieldValues != originalFields) }

  override init(frame:NSRect) {
    super.init(frame:frame)
    for n in 1...120 {note.addItem(withTitle:PreciseNote.name(n));note.lastItem?.tag=n}
    note.addItem(withTitle:"Note off");note.lastItem?.tag=255;note.addItem(withTitle:"Cut");note.lastItem?.tag=254
    note.selectItem(withTag:61);note.target=self;note.action=#selector(noteChanged);replaceLegacy.state = .on
    units.addItems(withTitles:["Beats","Rows"]);units.target=self;units.action=#selector(changeUnits)
    snap.addItems(withTitles:["Free","1/16 beat","1/32 beat","1/64 beat"]);snap.target=self;snap.action=#selector(changeSnap)
    effect.target=self;effect.action=#selector(changeEffect);setEffects([])
    for (id,title,width) in [("beats","Beat offset",85.0),("offset","Row offset",85.0),("note","Note",65.0),("instrument","Ins",45.0),("velocity","Vol",45.0),("effect","Effect",110.0)] {
      let column=NSTableColumn(identifier:NSUserInterfaceItemIdentifier(id));column.title=title;column.width=width;table.addTableColumn(column)
    }
    table.dataSource=self;table.delegate=self;table.rowHeight=25;table.allowsMultipleSelection=false
    table.setAccessibilityLabel("Precise notes in this row")
    let scroll=NSScrollView();scroll.documentView=table;scroll.hasVerticalScroller=true;scroll.hasHorizontalScroller=true;scroll.heightAnchor.constraint(equalToConstant:130).isActive=true
    timeline.heightAnchor.constraint(equalToConstant:205).isActive=true
    timeline.onSelect = {[weak self] index in self?.selectEvent(index) ?? false}
    timeline.onMove = {[weak self] index,offset,volume,finished in self?.moveEvent(index,offset:offset,volume:volume,finished:finished)}
    timeline.onInsert = {[weak self] offset in self?.insertAt(offset)}
    timeline.onDelete = {[weak self] in self?.remove()};timeline.onDuplicate = {[weak self] in self?.duplicate()}
    reloadButton=ActionButton("Use current cursor"){[weak self] in self?.capture()}
    addButton=ActionButton("Add hit"){[weak self] in guard let self else{return};if self.editingRow != nil {self.duplicate()} else {self.put(replacing:false)}};updateButton=ActionButton("Update selected"){[weak self] in self?.put(replacing:true)}
    removeButton=ActionButton("Remove"){[weak self] in self?.remove()}
    previewButton=ActionButton("Check edit"){[weak self] in self?.apply(dryRun:true)};applyButton=ActionButton("Apply"){[weak self] in self?.apply(dryRun:false)}
    repeatButton=ActionButton("Fill to row end"){[weak self] in self?.makeRetriggers()}
    for (view,label) in [(offset,"Note offset"),(instrument,"Instrument"),(velocity,"Volume / velocity"),(parameter,"Effect parameter (hex)"),(repeatCount,"Retrigger count"),(endVolume,"Last retrigger volume")] {view.setAccessibilityLabel(label)}
    units.setAccessibilityLabel("Offset units");snap.setAccessibilityLabel("Timeline snap");effect.setAccessibilityLabel("Selected hit effect")
    for field in [instrument,velocity,repeatCount,endVolume,parameter] {field.fixed(width:56)}
    offset.fixed(width:104);units.fixed(width:88);note.fixed(width:100)
    offset.delegate=self;velocity.delegate=self
    effectHint.maximumNumberOfLines=2;effectHint.lineBreakMode = .byWordWrapping;effectHint.preferredMaxLayoutWidth=500
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=510
    func label(_ s:String)->NSTextField {Theme.label(s,size:12,color:Theme.muted)}
    let body=stack(.vertical,[stack(.horizontal,[Theme.label("Precise notes",size:20,weight:.semibold),NSView(),reloadButton!]),location,
      stack(.horizontal,[label("Snap"),snap,NSView(),label("Drag: timing + volume · Shift: timing")]),timeline,scroll,
      stack(.horizontal,[label("Offset"),offset,units,NSView(),offsetHint]),
      stack(.horizontal,[label("Note"),note,label("Ins"),instrument,label("Volume"),velocity,NSView()]),
      stack(.horizontal,[label("Effect"),effect,parameter]),effectHint,
      stack(.horizontal,[addButton!,updateButton!,removeButton!,NSView()]),
      stack(.horizontal,[label("Retriggers"),repeatCount,label("End volume"),endVolume,repeatButton!,NSView()]),
      replaceLegacy,stack(.horizontal,[status,NSView(),previewButton!,applyButton!])],spacing:10)
    body.stretchAcrossAxis();body.fill(self,inset:16);controls()
  }
  required init?(coder:NSCoder){fatalError()}
  // Fractions are useful musical input: 1/8 beat, 3/16 beat, etc.
  static func number(_ string:String)->Double? {
    let parts=string.trimmingCharacters(in:.whitespacesAndNewlines).split(separator:"/",omittingEmptySubsequences:false)
    guard parts.count==1 || parts.count==2,let a=Double(parts[0].trimmingCharacters(in:.whitespaces)),a.isFinite else{return nil}
    if parts.count==1 {return a}
    guard let b=Double(parts[1].trimmingCharacters(in:.whitespaces)),b.isFinite,b != 0 else{return nil};let result=a/b;return result.isFinite ? result:nil
  }
  private func offsetText(_ units:Int)->String {String(format:"%.10g",Double(units)/65536/(displayBeats ? Double(rowsPerBeat):1))}
  private func rowOffset()->Double? {guard let n=Self.number(offset.stringValue) else{return nil};return n*(displayBeats ? Double(rowsPerBeat):1)}
  @objc func changeUnits() {
    let next=units.indexOfSelectedItem==0;guard next != displayBeats else{return}
    guard let value=rowOffset(),value>=0,value<1 else{units.selectItem(at:displayBeats ? 0:1);status.stringValue="Correct the offset before changing units.";return}
    let clean=fieldValues==originalFields;displayBeats=next;offset.stringValue=String(format:"%.10g",value/(next ? Double(rowsPerBeat):1))
    if clean {originalFields=fieldValues};showOffsetHint()
  }
  @objc func changeSnap() {timeline.snapBeats=[0,1.0/16,1.0/32,1.0/64][max(0,snap.indexOfSelectedItem)]}
  @objc func noteChanged() {controls()}
  func controlTextDidChange(_ notification:Notification) {
    guard !pending,let index=editingRow,saveSelectedFields() else{return}
    table.reloadData(forRowIndexes:IndexSet(integer:index),columnIndexes:IndexSet(integersIn:0..<table.numberOfColumns))
    syncTimeline();showOffsetHint();status.stringValue="Draft · Apply saves the visible timing and volume."
  }
  @objc func changeEffect() {
    if effects.indices.contains(effect.indexOfSelectedItem) {let e=effects[effect.indexOfSelectedItem];parameter.stringValue=String(format:"%02X",e.suggested);effectHint.stringValue=e.hint}
  }
  private func setEffects(_ items:[[String:Any]]) {
    effects=items.map(PatternCommand.init)
    effectValues=items.map{($0["allowedParameters"] as? [Int]).map(Set.init)}
    if !effects.contains(where:{$0.command==0}) {effects.insert(PatternCommand(["command":0,"name":"None","label":"—","maximum":0,"description":"No continuing effect for this hit. Ends the previous hit's effect; ordinary row effects stay active until a hit overrides them."]),at:0);effectValues.insert(Set([0]),at:0)}
    effect.removeAllItems();for e in effects {effect.addItem(withTitle:e.command==0 ? "None":"\(e.label) · \(e.name)")};effect.selectItem(at:0);changeEffect()
  }
  func capture() {
    guard !pending,let context=onContext?(),let request=onRequest else{return}
    let sameTarget=revision != nil && capturedPattern==context.0.pattern && capturedRow==context.1 && capturedChannel==context.2
    let previousSelection=sameTarget ? editingRow.flatMap{draft.indices.contains($0) ? PreciseNote(draft[$0]):nil}:nil
    capturedPattern=context.0.pattern;capturedRow=context.1;capturedChannel=context.2;rowsPerBeat=max(1,context.0.rowsPerBeat);instrument.stringValue=String(context.3);moveLegacyEffect=false
    location.stringValue="Pattern \(capturedPattern) · Row \(capturedRow) · Channel \(capturedChannel+1) · \(rowsPerBeat) rows/beat"
    pending=true;revision=nil;controls()
    request("pattern.notes.get",["pattern":capturedPattern]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String,let data=result["data"] as? [String:Any],data["pattern"] as? Int==self.capturedPattern else{self.failure(reply);return}
      self.rowsPerBeat=max(1,data["rowsPerBeat"] as? Int ?? self.rowsPerBeat);self.setEffects(data["effects"] as? [[String:Any]] ?? context.0.preciseNoteEffects)
      self.revision=revision;self.events=data["events"] as? [[String:Any]] ?? [];self.draft=self.events.filter{self.belongs($0)}
      if self.draft.isEmpty {
        let cell=context.0.cell(context.1,context.2).map(Int.init)
        if (1...120).contains(cell[0]) || cell[0]==254 || cell[0]==255 {
          var event:[String:Any]=["channel":context.2,"position":context.1*65536,"note":cell[0],"instrument":cell[0]<128 ? cell[1]:0,"velocity":cell[0]<128 && cell[2]==1 ? max(1,min(127,cell[3]*127/64)):127]
          if cell[0]<128 && cell[4]>0 && self.effects.contains(where:{$0.command==cell[4] && cell[5]&$0.mask==$0.value}) {event["effect"]=cell[4];event["parameter"]=cell[5];self.moveLegacyEffect=true}
          self.draft=[event]
        }
      }
      let retained=previousSelection.flatMap {old in self.draft.firstIndex {let e=PreciseNote($0);return e.position==old.position && e.note==old.note}}
      self.refreshDraft(selecting:retained ?? (self.draft.isEmpty ? nil:0))
      if self.draft.isEmpty {self.offset.stringValue="0";self.velocity.stringValue="127";self.note.selectItem(withTag:61)}
      if !sameTarget {self.endVolume.stringValue=self.velocity.stringValue};self.originalDraft=self.draft;self.originalFields=self.fieldValues
      self.status.stringValue=self.moveLegacyEffect ? "Draft · the ordinary note and its effect move together on Apply." : self.events.contains(where:self.belongs) ? "\(self.draft.count) saved hits · edits use one Undo." : "Draft · Apply saves all hits as one Undo.";self.controls()
    }
  }
  private func belongs(_ e:[String:Any])->Bool {e["channel"] as? Int==capturedChannel && (e["position"] as? Int ?? 0)/65536==capturedRow}
  private func collision(_ item:[String:Any],excluding:Int?)->Bool {
    draft.enumerated().contains {index,e in index != excluding && e["position"] as? Int==item["position"] as? Int && ((e["note"] as? Int ?? 0)<128)==((item["note"] as? Int ?? 0)<128)}
  }
  private func editedEvent(replacing selected:Int?)->[String:Any]? {
    func text(_ field:NSTextField)->String {field.stringValue.trimmingCharacters(in:.whitespacesAndNewlines)}
    guard let start=rowOffset(),start.isFinite,start>=0,start<1,let ins=Int(text(instrument)),(0...255).contains(ins),let vol=Int(text(velocity)),(1...127).contains(vol) else {
      status.stringValue="Offset: 0 to less than \(String(format:"%.6g",1.0/Double(rowsPerBeat))) beats (1 row). Instrument: 0–255. Volume: 1–127.";return nil
    }
    let n=note.selectedTag(),position=capturedRow*65536+min(65535,Int((start*65536).rounded()))
    var item:[String:Any]=["channel":capturedChannel,"position":position,"note":n,"instrument":n<128 ? ins:0,"velocity":n<128 ? vol:127]
    if n<128,effects.indices.contains(effect.indexOfSelectedItem) {
      let e=effects[effect.indexOfSelectedItem]
      guard let amount=Int(text(parameter),radix:16),(e.minimum...e.maximum).contains(amount),amount&e.mask==e.value,e.command != 0 || amount==0,effectValues[effect.indexOfSelectedItem]?.contains(amount) != false else{status.stringValue="Enter a supported hexadecimal parameter for this effect. See its description.";return nil}
      if e.command != 0 {item["effect"]=e.command;item["parameter"]=amount}
    }
    if collision(item,excluding:selected) {status.stringValue="Another event of this kind is already at that offset.";return nil}
    return item
  }
  private func saveSelectedFields()->Bool {
    guard let index=editingRow,draft.indices.contains(index) else{return true}
    guard let item=editedEvent(replacing:index) else{return false};draft[index]=item;return true
  }
  private func syncTimeline() {
    timeline.events=draft.map(PreciseNote.init);timeline.selected=editingRow;timeline.row=capturedRow;timeline.rowsPerBeat=rowsPerBeat;timeline.needsDisplay=true
  }
  private func refreshDraft(selecting index:Int?) {
    updatingTable=true;editingRow=nil;table.reloadData()
    if let index,draft.indices.contains(index) {table.selectRowIndexes(IndexSet(integer:index),byExtendingSelection:false)} else {table.deselectAll(nil)}
    updatingTable=false;loadSelectedFields();syncTimeline()
  }
  private func sortDraft(selecting item:[String:Any]?) {
    draft.sort {let a=PreciseNote($0),b=PreciseNote($1);return a.position==b.position ? a.note>b.note:a.position<b.position}
    refreshDraft(selecting:item.flatMap {item in draft.firstIndex {NSDictionary(dictionary:$0).isEqual(to:item)}})
  }
  func selectEvent(_ index:Int)->Bool {
    guard !pending,draft.indices.contains(index),saveSelectedFields() else{return false};refreshDraft(selecting:index);return true
  }
  func moveEvent(_ index:Int,offset:Int,volume:Int,finished:Bool) {
    guard !pending,draft.indices.contains(index) else{return}
    var item=draft[index];item["position"]=capturedRow*65536+max(0,min(65535,offset));if (item["note"] as? Int ?? 0)<128 {item["velocity"]=max(1,min(127,volume))}
    guard !collision(item,excluding:index) else{status.stringValue="That position already has a hit. Drag to a different offset.";return}
    draft[index]=item
    if finished {sortDraft(selecting:item)} else {refreshDraft(selecting:index)}
    status.stringValue="Draft · Apply saves the timeline."
  }
  func insertAt(_ position:Int) {
    guard !pending,saveSelectedFields() else{return}
    var item: [String:Any]
    if let index=editingRow,draft.indices.contains(index) {item=draft[index]} else {let n=note.selectedTag();item=["channel":capturedChannel,"note":n,"instrument":n<128 ? (Int(instrument.stringValue) ?? 1):0,"velocity":n<128 ? (Int(velocity.stringValue) ?? 127):127]}
    item["position"]=capturedRow*65536+max(0,min(65535,position))
    guard !collision(item,excluding:nil) else{status.stringValue="A hit already exists at this position.";return}
    draft.append(item);sortDraft(selecting:item);status.stringValue="Hit added to draft."
  }
  func duplicate() {
    guard let index=editingRow,saveSelectedFields() else{return}
    let pos=(draft[index]["position"] as? Int ?? 0)%65536
    let used=Set(draft.map{($0["position"] as? Int ?? 0)%65536}),preferred=min(65535,pos+4096)
    let later=(preferred..<65536).first(where:{!used.contains($0)})
    let earlier=pos+1<preferred ? ((pos+1)..<preferred).first(where:{!used.contains($0)}):nil
    if let next=later ?? earlier {insertAt(next)} else {status.stringValue="Move the selected hit earlier to leave room for another hit."}
  }
  func makeRetriggers() {
    guard !pending,let index=editingRow,saveSelectedFields(),draft.indices.contains(index),
      let count=Int(repeatCount.stringValue),(2...64).contains(count),let last=Int(endVolume.stringValue),(1...127).contains(last) else {status.stringValue="Select a hit; use 2–64 retriggers and end volume 1–127.";return}
    let original=draft[index],event=PreciseNote(original);guard event.note<128 else{status.stringValue="Choose a note onset to retrigger.";return}
    let start=event.position%65536,remaining=65536-start;guard remaining>=count else{status.stringValue="Move the first hit earlier to fit this many retriggers.";return}
    var copies=[[String:Any]]()
    for n in 0..<count {
      var item=original;item["position"]=capturedRow*65536+start+n*remaining/count
      item["velocity"]=Int((Double(event.velocity)+Double(last-event.velocity)*Double(n)/Double(count-1)).rounded())
      guard !collision(item,excluding:index) else{status.stringValue="Retriggers overlap an existing hit. Remove it or choose a different count/start.";return};copies.append(item)
    }
    draft.remove(at:index);draft.append(contentsOf:copies);sortDraft(selecting:copies.first);status.stringValue="\(count) hits in draft · drag or select any hit to refine it."
  }
  func put(replacing:Bool) {
    guard !pending,!replacing || editingRow != nil,let item=editedEvent(replacing:replacing ? editingRow:nil) else{return}
    if replacing,let index=editingRow {draft[index]=item} else {draft.append(item)}
    sortDraft(selecting:item);status.stringValue="Draft updated. Apply saves it to the song.";controls()
  }
  func remove() {
    guard !pending,let index=editingRow,draft.indices.contains(index) else{return}
    draft.remove(at:index);refreshDraft(selecting:draft.isEmpty ? nil:min(index,draft.count-1));status.stringValue="Event removed from the draft.";controls()
  }
  func apply(dryRun:Bool) {
    guard !pending,let revision,let request=onRequest,saveSelectedFields() else{return}
    sortDraft(selecting:editingRow.map{draft[$0]});let replacement=events.filter{!belongs($0)}+draft
    pending=true;controls()
    request("pattern.notes.set",["pattern":capturedPattern,"events":replacement,"clearRows":replaceLegacy.state == .on ? [["row":capturedRow,"channel":capturedChannel]]:[],"clearRowEffects":moveLegacyEffect && replaceLegacy.state == .on,"expectedRevision":revision,"dryRun":dryRun]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let next=result["revision"] as? String else{self.failure(reply);return}
      self.revision=next;if !dryRun {self.events=replacement;self.originalDraft=self.draft;self.originalFields=self.fieldValues;self.moveLegacyEffect=false}
      self.status.stringValue=dryRun ? "Edit is valid. Apply saves this draft; Check edit does not play audio.":"Precise notes saved. Undo restores the previous notes.";self.controls()
    }
  }
  func numberOfRows(in tableView:NSTableView)->Int {draft.count}
  func tableView(_ tableView:NSTableView,viewFor column:NSTableColumn?,row:Int)->NSView? {
    guard draft.indices.contains(row) else{return nil};let event=PreciseNote(draft[row]);let text:String
    switch column?.identifier.rawValue {
    case "beats":text=String(format:"%.7g",Double(event.position%65536)/65536/Double(rowsPerBeat))
    case "offset":text=String(format:"%.7g",Double(event.position%65536)/65536)
    case "note":text=event.name
    case "instrument":text=event.note<128 ? String(event.instrument):"—"
    case "effect":text=event.effect==0 ? "—":(effects.first{$0.command==event.effect && event.parameter&$0.mask==$0.value}?.label ?? "?")+String(format:" %02X",event.parameter)
    default:text=event.note<128 ? String(event.velocity):"—"
    };return Theme.label(text,size:12)
  }
  func tableViewSelectionDidChange(_ notification:Notification) {
    guard !updatingTable else{return}
    if !saveSelectedFields() {updatingTable=true;if let editingRow {table.selectRowIndexes(IndexSet(integer:editingRow),byExtendingSelection:false)};updatingTable=false;return}
    if let editingRow {table.reloadData(forRowIndexes:IndexSet(integer:editingRow),columnIndexes:IndexSet(integersIn:0..<table.numberOfColumns))}
    loadSelectedFields();syncTimeline()
  }
  private func loadSelectedFields() {
    editingRow=draft.indices.contains(table.selectedRow) ? table.selectedRow:nil
    guard let editingRow else{controls();return}
    let e=PreciseNote(draft[editingRow]);offset.stringValue=offsetText(e.position%65536);note.selectItem(withTag:e.note);instrument.stringValue=String(e.instrument);velocity.stringValue=String(e.velocity)
    effect.selectItem(at:effects.firstIndex{$0.command==e.effect && e.parameter&$0.mask==$0.value} ?? 0);parameter.stringValue=String(format:"%02X",e.parameter)
    effectHint.stringValue=effects.indices.contains(effect.indexOfSelectedItem) ? effects[effect.indexOfSelectedItem].hint:""
    originalFields=fieldValues;showOffsetHint();controls()
  }
  private func showOffsetHint() {
    if let value=rowOffset() {offsetHint.stringValue=displayBeats ? String(format:"= %.6g row",value):String(format:"= %.6g beat",value/Double(rowsPerBeat))}
  }
  private func failure(_ reply:[String:Any]) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed. Use current cursor to reload.";controls()}
  private func controls() {
    table.isEnabled = !pending;timeline.enabled = !pending && revision != nil
    for view in [note,instrument,velocity,offset,units,snap,replaceLegacy,repeatCount,endVolume] as [NSControl] {view.isEnabled = !pending}
    effect.isEnabled = !pending && note.selectedTag()<128;parameter.isEnabled=effect.isEnabled
    reloadButton?.isEnabled = !pending;addButton?.isEnabled = !pending && revision != nil
    updateButton?.isEnabled = !pending && draft.indices.contains(table.selectedRow);removeButton?.isEnabled=updateButton?.isEnabled ?? false
    repeatButton?.isEnabled = !pending && editingRow != nil && note.selectedTag()<128
    previewButton?.isEnabled = !pending && revision != nil;applyButton?.isEnabled=previewButton?.isEnabled ?? false
  }
}
