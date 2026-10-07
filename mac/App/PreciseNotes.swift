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
  override var isOpaque:Bool {true}
  override var acceptsFirstResponder:Bool {true}
  var plot:NSRect {NSRect(x:32,y:76,width:max(1,bounds.width-52),height:max(30,bounds.height-111))}
  override init(frame:NSRect) {
    super.init(frame:frame);wantsLayer=true;setAccessibilityElement(true);setAccessibilityRole(.group);setAccessibilityLabel("Precise note timeline")
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

// A compact tracker: arrows select a cell; typing replaces its value. Native
// text editing takes over for fractions and numbers, with Tab moving between cells.
final class PreciseNoteCell:NSTextField {
  var onChoose:(()->Bool)?
  override func mouseDown(with event:NSEvent) {
    guard onChoose?() != false else{return}
    if event.clickCount>1 {super.mouseDown(with:event)}
  }
}
final class PreciseNoteTable:NSTableView {
  var activeColumn=0
  var onType:((Int,Int,NSEvent)->Bool)?,onBeginEdit:((Int,Int,String?)->Void)?,onRemove:(()->Void)?
  override func mouseDown(with event:NSEvent) {
    let p=convert(event.locationInWindow,from:nil),col=column(at:p),r=row(at:p)
    if col>=0 {activeColumn=col};super.mouseDown(with:event)
    if event.clickCount==2,r>=0 {onBeginEdit?(r,activeColumn,nil)}
    needsDisplay=true
  }
  override func draw(_ rect:NSRect) {
    super.draw(rect)
    if selectedRow>=0,activeColumn<numberOfColumns,window?.firstResponder===self {
      Theme.selectionMark.setStroke();let path=NSBezierPath(rect:frameOfCell(atColumn:activeColumn,row:selectedRow).insetBy(dx:1,dy:1));path.lineWidth=1;path.stroke()
    }
  }
  func moveCell(_ delta:Int) {
    guard numberOfRows>0 else{return}
    let next=max(0,min(numberOfRows*numberOfColumns-1,max(0,selectedRow)*numberOfColumns+activeColumn+delta))
    activeColumn=next%numberOfColumns;selectRowIndexes(IndexSet(integer:next/numberOfColumns),byExtendingSelection:false)
    scrollRowToVisible(selectedRow);scrollColumnToVisible(activeColumn);window?.makeFirstResponder(self);needsDisplay=true
  }
  override func keyDown(with event:NSEvent) {
    guard event.modifierFlags.intersection([.command,.control,.option]).isEmpty else {super.keyDown(with:event);return}
    if event.keyCode==48 {moveCell(event.modifierFlags.contains(.shift) ? -1:1);return}
    if event.keyCode==123 || event.keyCode==124 {moveCell(event.keyCode==123 ? -1:1);return}
    if event.keyCode==125 || event.keyCode==126 {moveCell(event.keyCode==125 ? numberOfColumns:-numberOfColumns);return}
    if event.keyCode==51 || event.keyCode==117 {onRemove?();return}
    guard selectedRow>=0 else{super.keyDown(with:event);return}
    if event.keyCode==36 || event.keyCode==76 {onBeginEdit?(selectedRow,activeColumn,nil);return}
    if onType?(selectedRow,activeColumn,event)==true {return}
    if let text=event.characters,!text.isEmpty {onBeginEdit?(selectedRow,activeColumn,text);return}
    super.keyDown(with:event)
  }
}

final class PreciseNotesEditor:NSView,NSTableViewDataSource,NSTableViewDelegate,NSTextFieldDelegate {
  var onContext:(()->(PatternModel,Int,Int,Int))?
  var inputOctave:(()->Int)?
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  let location=Theme.label("",size:12),status=Theme.label("",size:12,color:Theme.muted),table=PreciseNoteTable(),timeline=PreciseNoteTimeline()
  let note=NSPopUpButton(),instrument=NSTextField(string:"1"),velocity=NSTextField(string:"127"),offset=NSTextField(string:"0"),units=NSPopUpButton(),snap=NSPopUpButton()
  let effect=NSPopUpButton(),parameter=NSTextField(string:"00"),effectHint=Theme.label("",size:11,color:Theme.muted),offsetHint=Theme.label("",size:11,color:Theme.muted)
  let repeatCount=NSTextField(string:"4"),endVolume=NSTextField(string:"127")
  let replaceLegacy=NSButton(checkboxWithTitle:"Replace the ordinary note in this row",target:nil,action:nil)
  var reloadButton:ActionButton!,addButton:ActionButton!,updateButton:ActionButton!,removeButton:ActionButton!,previewButton:ActionButton!,applyButton:ActionButton!,repeatButton:ActionButton!
  private(set) var revision:String?,events=[[String:Any]](),draft=[[String:Any]](),pending=false,capturedPattern=0,capturedRow=0,capturedChannel=0,rowsPerBeat=4
  private var editingRow:Int?,updatingTable=false,effects=[PatternCommand](),displayBeats=true,moveLegacyEffect=false
  private var effectValues=[Set<Int>?]()
  private var effectCatalog:NSArray?
  private var originalDraft = [[String:Any]](), originalFields = [String]()
  private var fieldValues:[String] { [offset.stringValue,String(note.selectedTag()),instrument.stringValue,velocity.stringValue,String(effect.indexOfSelectedItem),parameter.stringValue] }
  private var autoSave:DispatchWorkItem?,inlineInvalid=false
  private var canEdit:Bool {revision != nil}
  var hasDraft:Bool { inlineInvalid || !NSArray(array:draft).isEqual(to:originalDraft) || (!originalFields.isEmpty && fieldValues != originalFields) }

  override init(frame:NSRect) {
    super.init(frame:frame)
    for n in 1...120 {note.addItem(withTitle:PreciseNote.name(n));note.lastItem?.tag=n}
    note.addItem(withTitle:"Note off");note.lastItem?.tag=255;note.addItem(withTitle:"Cut");note.lastItem?.tag=254
    note.selectItem(withTag:61);note.target=self;note.action=#selector(noteChanged);replaceLegacy.state = .on
    units.addItems(withTitles:["Beats","Rows"]);units.target=self;units.action=#selector(changeUnits)
    snap.addItems(withTitles:["Free","1/16 beat","1/32 beat","1/64 beat"]);snap.target=self;snap.action=#selector(changeSnap)
    effect.target=self;effect.action=#selector(changeEffect);setEffects([])
    for (id,title,width) in [("beats","Beat offset",85.0),("offset","Row offset",85.0),("note","Note",65.0),("instrument","Ins",45.0),("velocity","Vol",45.0),("effect","FX",55.0),("parameter","Value",60.0)] {
      let column=NSTableColumn(identifier:NSUserInterfaceItemIdentifier(id));column.title=title;column.width=width;table.addTableColumn(column)
    }
    table.dataSource=self;table.delegate=self;table.rowHeight=25;table.allowsMultipleSelection=false
    table.setAccessibilityLabel("Precise notes in this row")
    table.toolTip="Arrows select cells. Type to edit; Tab moves to the next cell. In Note, use normal note keys (1 = off). Return or double-click edits text. Delete removes the hit. Changes save automatically."
    table.onBeginEdit = {[weak self] row,column,text in self?.beginCellEdit(row,column,text)}
    table.onRemove = {[weak self] in self?.remove()}
    table.onType = {[weak self] row,column,event in
      guard let self,self.table.tableColumns[column].identifier.rawValue=="note",let key=event.charactersIgnoringModifiers?.lowercased() else{return false}
      let n=key=="1" ? 255 : KeyboardSettings.note(for:key).map{max(1,min(120,(self.inputOctave?() ?? 4)*12+$0+1))}
      guard let n else{return false};_ = self.editCell(row,"note",PreciseNote.name(n));self.refreshDraft(selecting:row);return true
    }
    let scroll=NSScrollView();scroll.documentView=table;scroll.hasVerticalScroller=true;scroll.hasHorizontalScroller=true;scroll.heightAnchor.constraint(equalToConstant:170).isActive=true
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
    // These secondary controls change availability while following rows. The
    // native outlined bezel avoids rebuilding the heavier rounded material.
    for button in [removeButton!,repeatButton!] {button.bezelStyle = .texturedRounded}
    for (view,label) in [(offset,"Note offset"),(instrument,"Instrument"),(velocity,"Volume / velocity"),(parameter,"Effect parameter (hex)"),(repeatCount,"Retrigger count"),(endVolume,"Last retrigger volume")] {view.setAccessibilityLabel(label)}
    units.setAccessibilityLabel("Offset units");snap.setAccessibilityLabel("Timeline snap");effect.setAccessibilityLabel("Selected hit effect")
    for field in [instrument,velocity,repeatCount,endVolume,parameter] {field.fixed(width:56)}
    offset.fixed(width:104);units.fixed(width:88);note.fixed(width:100)
    for field in [offset,instrument,velocity,parameter] {field.delegate=self}
    replaceLegacy.target=self;replaceLegacy.action=#selector(noteChanged)
    effectHint.maximumNumberOfLines=2;effectHint.lineBreakMode = .byWordWrapping;effectHint.preferredMaxLayoutWidth=500
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=510
    func label(_ s:String)->NSTextField {Theme.label(s,size:12,color:Theme.muted)}
    let details=ToolSection("Selected hit details",id:"precise-note-details",views:[
      stack(.horizontal,[label("Offset"),offset,units,NSView(),offsetHint]),
      stack(.horizontal,[label("Note"),note,label("Ins"),instrument,label("Volume"),velocity,NSView()]),
      stack(.horizontal,[label("Effect"),effect,parameter]),effectHint,replaceLegacy])
    let body=stack(.vertical,[stack(.horizontal,[Theme.label("Precise notes",size:20,weight:.semibold),NSView(),reloadButton!]),location,
      stack(.horizontal,[label("Snap"),snap,NSView(),label("Drag timing / volume")]),timeline,
      label("Type in cells · Z–M notes · Tab / arrows navigate · edits save automatically"),scroll,
      stack(.horizontal,[addButton!,removeButton!,NSView()]),details,
      stack(.horizontal,[label("Retriggers"),repeatCount,label("End volume"),endVolume,repeatButton!,NSView()]),status,NSView()],spacing:10)
    for row in body.arrangedSubviews.dropLast() { row.setContentHuggingPriority(.required,for:.vertical) }
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
  @objc func noteChanged() {if canEdit,saveSelectedFields(){refreshDraft(selecting:editingRow);scheduleSave()};controls()}
  func controlTextDidChange(_ notification:Notification) {
    guard canEdit,let field=notification.object as? NSTextField else{return}
    if field.tag>=1000 {
      let row=(field.tag-1000)/10,column=(field.tag-1000)%10
      inlineInvalid = !editCell(row,table.tableColumns[column].identifier.rawValue,field.stringValue)
      field.textColor=inlineInvalid ? Theme.gold:Theme.text
      return
    }
    guard let index=editingRow,saveSelectedFields() else{autoSave?.cancel();return}
    refreshVisibleCells(row:index)
    syncTimeline();showOffsetHint();scheduleSave()
  }
  func control(_ control:NSControl,textView:NSTextView,doCommandBy selector:Selector)->Bool {
    guard let field=control as? NSTextField,field.tag>=1000 else{return false}
    if selector == #selector(NSResponder.cancelOperation(_:)) {
      inlineInvalid=false;window?.makeFirstResponder(table);refreshDraft(selecting:editingRow);scheduleSave();return true
    }
    if selector == #selector(NSResponder.insertTab(_:)) || selector == #selector(NSResponder.insertBacktab(_:)) || selector == #selector(NSResponder.insertNewline(_:)) {
      guard !inlineInvalid else{return true}
      window?.makeFirstResponder(table);sortDraft(selecting:editingRow.map{draft[$0]})
      if selector != #selector(NSResponder.insertNewline(_:)) {table.moveCell(selector == #selector(NSResponder.insertBacktab(_:)) ? -1:1)}
      return true
    }
    return false
  }
  func controlTextDidEndEditing(_ notification:Notification) {
    guard let field=notification.object as? NSTextField,field.tag>=1000,!inlineInvalid else{return}
    // Do not reload here: Tab may already be moving to a new field editor.
    scheduleSave()
  }
  private func beginCellEdit(_ row:Int,_ column:Int,_ text:String?) {
    guard canEdit,!inlineInvalid,draft.indices.contains(row),let field=table.view(atColumn:column,row:row,makeIfNecessary:true) as? NSTextField else{return}
    table.activeColumn=column;table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false)
    window?.makeFirstResponder(field);field.selectText(nil)
    if let text {field.stringValue=text;field.currentEditor()?.string=text;controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:field));field.currentEditor()?.selectedRange=NSRange(location:text.utf16.count,length:0)}
  }
  @discardableResult func editCell(_ row:Int,_ column:String,_ text:String)->Bool {
    guard canEdit,draft.indices.contains(row) else{return false}
    var item=draft[row];let value=text.trimmingCharacters(in:.whitespacesAndNewlines),e=PreciseNote(item)
    func invalid(_ message:String)->Bool {status.stringValue=message;autoSave?.cancel();return false}
    switch column {
    case "beats","offset":
      guard let number=Self.number(value) else{return invalid("Enter a number or fraction, e.g. 1/16.")}
      let offset=number*(column=="beats" ? Double(rowsPerBeat):1)
      guard offset>=0,offset<1 else{return invalid("The hit must remain inside this row.")}
      item["position"]=capturedRow*65536+min(65535,Int((offset*65536).rounded()))
    case "note":
      let n=(1...120).first{PreciseNote.name($0).lowercased()==value.lowercased() || PreciseNote.name($0).replacingOccurrences(of:"-",with:"").lowercased()==value.lowercased()} ?? (["off","note off"].contains(value.lowercased()) ? 255:value.lowercased()=="cut" ? 254:0)
      guard n>0 else{return invalid("Use C-4, C#4, Off or Cut; normal note keys work when the cell is selected.")}
      item["note"]=n
      if n>=128 {item["instrument"]=0;item["velocity"]=127;item.removeValue(forKey:"effect");item.removeValue(forKey:"parameter")}
    case "instrument","velocity":
      guard e.note<128,let n=Int(value),(column=="instrument" ? 0...255:1...127).contains(n) else{return invalid(column=="instrument" ? "Instrument: 0–255 (notes only).":"Volume: 1–127 (notes only).")}
      item[column]=n
    case "effect":
      guard e.note<128 else{return invalid("Release events do not have effects.")}
      if ["","..","—","0"].contains(value) {item.removeValue(forKey:"effect");item.removeValue(forKey:"parameter")}
      else {guard let fx=effects.first(where:{$0.command != 0 && ($0.displayCode.lowercased()==value.lowercased() || ($0.mask==0 && String($0.label.prefix(1)).lowercased()==value.lowercased()))}) else{return invalid("Enter a supported hit effect; Selected hit details lists the available effects.")};item["effect"]=fx.command;item["parameter"]=fx.suggested}
    case "parameter":
      guard let index=effects.firstIndex(where:{$0.command==e.effect && e.parameter&$0.mask==$0.value}),let n=Int(value,radix:16),(0...255).contains(n) else{return invalid("Enter a hexadecimal effect value.")}
      let fx=effects[index],amount=fx.mask==0 ? n:fx.value|(n & ~fx.mask)
      guard (fx.minimum...fx.maximum).contains(amount),effectValues[index]?.contains(amount) != false else{return invalid("Value is not supported by this effect.")}
      if e.effect != 0 {item["parameter"]=amount}
    default:return false
    }
    guard !collision(item,excluding:row) else{return invalid("Another event of this kind is already at that offset.")}
    draft[row]=item;inlineInvalid=false
    if row==editingRow {loadSelectedFields()}
    for (index,c) in table.tableColumns.enumerated() where c.identifier.rawValue != column {
      if let field=table.view(atColumn:index,row:row,makeIfNecessary:false) as? NSTextField {field.stringValue=cellText(PreciseNote(item),c.identifier.rawValue)}
    }
    syncTimeline();scheduleSave();return true
  }
  private func scheduleSave() {
    autoSave?.cancel();guard canEdit,!inlineInvalid else{return}
    guard hasDraft || pending else{status.stringValue="Saved · Undo restores the previous notes.";return}
    status.stringValue="Saving…"
    let work=DispatchWorkItem{[weak self] in guard let self,!self.pending,!self.inlineInvalid,self.hasDraft else{return};self.apply(dryRun:false)}
    autoSave=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.18,execute:work)
  }
  @objc func changeEffect() {
    if effects.indices.contains(effect.indexOfSelectedItem) {let e=effects[effect.indexOfSelectedItem];parameter.stringValue=String(format:"%02X",e.suggested);effectHint.stringValue=e.hint}
    if canEdit,saveSelectedFields(){refreshDraft(selecting:editingRow);scheduleSave()}
  }
  private func setEffects(_ items:[[String:Any]]) {
    let previous=effects.indices.contains(effect.indexOfSelectedItem) ? effects[effect.indexOfSelectedItem]:nil
    // Moving the pattern cursor changes the note, not the command catalogue.
    // Rebuilding every native menu item here also invalidates the inspector.
    let catalog=items as NSArray
    if effectCatalog?.isEqual(catalog)==true {return}
    effectCatalog=catalog
    effects=items.map(PatternCommand.init)
    effectValues=items.map{($0["allowedParameters"] as? [Int]).map(Set.init)}
    if !effects.contains(where:{$0.command==0}) {effects.insert(PatternCommand(["command":0,"name":"None","label":"—","maximum":0,"description":"No continuing effect for this hit. Ends the previous hit's effect; ordinary row effects stay active until a hit overrides them."]),at:0);effectValues.insert(Set([0]),at:0)}
    // One menu item per effect, in order: addItem(withTitle:) would drop an
    // earlier effect with the same label and shift every later index.
    effect.removeAllItems();for (index,e) in effects.enumerated() {let item=NSMenuItem(title:e.command==0 ? "None":"\(e.label) · \(e.name)",action:nil,keyEquivalent:"");item.tag=index;effect.menu?.addItem(item)}
    if let previous,let kept=effects.firstIndex(where:{$0.command==previous.command && $0.mask==previous.mask && $0.value==previous.value}) {effect.selectItem(at:kept);effectHint.stringValue=effects[kept].hint}
    else {effect.selectItem(at:0);changeEffect()}
  }
  func capture() {
    guard !pending,let context=onContext?(),let request=onRequest else{return}
    let sameTarget=revision != nil && capturedPattern==context.0.pattern && capturedRow==context.1 && capturedChannel==context.2
    let previousSelection=sameTarget ? editingRow.flatMap{draft.indices.contains($0) ? PreciseNote(draft[$0]):nil}:nil
    autoSave?.cancel();inlineInvalid=false
    capturedPattern=context.0.pattern;capturedRow=context.1;capturedChannel=context.2;rowsPerBeat=max(1,context.0.rowsPerBeat);setText(instrument,String(context.3));moveLegacyEffect=false
    setText(location,"Pattern \(capturedPattern) · Row \(capturedRow) · Channel \(capturedChannel+1) · \(rowsPerBeat) rows/beat")
    pending=true;revision=nil
    request("pattern.notes.get",["pattern":capturedPattern]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String,let data=result["data"] as? [String:Any],data["pattern"] as? Int==self.capturedPattern else{self.failure(reply);return}
      self.revision=nil;self.rowsPerBeat=max(1,data["rowsPerBeat"] as? Int ?? self.rowsPerBeat);self.setEffects(data["effects"] as? [[String:Any]] ?? context.0.preciseNoteEffects)
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
      if self.draft.isEmpty {
        self.setText(self.offset,"0");self.setText(self.velocity,"127");self.select(self.note,tag:61)
        // Reset only blank targets, rather than temporarily clearing every
        // selected hit's effect before immediately selecting it again.
        self.select(self.effect,index:0);self.setText(self.parameter,"00")
        self.setText(self.effectHint,self.effects.first?.hint ?? "");self.showOffsetHint()
      }
      if !sameTarget {self.setText(self.endVolume,self.velocity.stringValue)};self.originalDraft=self.draft;self.originalFields=self.fieldValues
      self.setText(self.status,self.moveLegacyEffect ? "Editing will move the ordinary note and its effect together." : "\(self.draft.count) hits · changes save automatically · Undo restores each edit.");self.controls()
    }
    // The dock reads the current pattern synchronously from its snapshot.
    // Disable controls only if a remote read actually remains outstanding;
    // toggling every button off/on in one stack still triggers native redraws.
    if pending {controls()}
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
    updatingTable=true;editingRow=nil
    // Following a row normally changes values, not the table's structure.
    // Retain the native cells and their backing layers instead of tearing down
    // the entire AppKit table on every cursor movement or timeline drag.
    if table.numberOfRows != draft.count {table.noteNumberOfRowsChanged()}
    refreshVisibleCells()
    let selected=index.flatMap{draft.indices.contains($0) ? $0:nil}
    if let selected {
      if table.selectedRow != selected {table.selectRowIndexes(IndexSet(integer:selected),byExtendingSelection:false)}
    } else if table.selectedRow != -1 {table.deselectAll(nil)}
    updatingTable=false;loadSelectedFields();syncTimeline()
  }
  private func refreshVisibleCells(row:Int?=nil) {
    let visible=table.rows(in:table.visibleRect)
    guard visible.location != NSNotFound else{return}
    let first=max(0,visible.location),end=min(draft.count,NSMaxRange(visible))
    guard first<end else{return}
    for index in first..<end where row==nil || row==index {
      let event=PreciseNote(draft[index])
      for (column,item) in table.tableColumns.enumerated() {
        guard let field=table.view(atColumn:column,row:index,makeIfNecessary:false) as? PreciseNoteCell,field.currentEditor()==nil else{continue}
        setText(field,cellText(event,item.identifier.rawValue))
        if field.textColor != Theme.text {field.textColor=Theme.text}
      }
    }
  }
  private func setText(_ field:NSTextField,_ value:String){if field.stringValue != value {field.stringValue=value}}
  private func select(_ popup:NSPopUpButton,tag:Int){if popup.selectedTag() != tag {popup.selectItem(withTag:tag)}}
  private func select(_ popup:NSPopUpButton,index:Int){if popup.indexOfSelectedItem != index {popup.selectItem(at:index)}}
  private func sortDraft(selecting item:[String:Any]?) {
    draft.sort {let a=PreciseNote($0),b=PreciseNote($1);return a.position==b.position ? a.note>b.note:a.position<b.position}
    refreshDraft(selecting:item.flatMap {item in draft.firstIndex {NSDictionary(dictionary:$0).isEqual(to:item)}})
  }
  func selectEvent(_ index:Int)->Bool {
    guard canEdit,!inlineInvalid,draft.indices.contains(index),saveSelectedFields() else{return false};refreshDraft(selecting:index);return true
  }
  func moveEvent(_ index:Int,offset:Int,volume:Int,finished:Bool) {
    guard canEdit,!inlineInvalid,draft.indices.contains(index) else{return}
    var item=draft[index];item["position"]=capturedRow*65536+max(0,min(65535,offset));if (item["note"] as? Int ?? 0)<128 {item["velocity"]=max(1,min(127,volume))}
    guard !collision(item,excluding:index) else{status.stringValue="That position already has a hit. Drag to a different offset.";return}
    draft[index]=item
    if finished {sortDraft(selecting:item)} else {refreshDraft(selecting:index)}
    if finished {scheduleSave()}
  }
  func insertAt(_ position:Int) {
    guard canEdit,!inlineInvalid,saveSelectedFields() else{return}
    var item: [String:Any]
    if let index=editingRow,draft.indices.contains(index) {item=draft[index]} else {let n=note.selectedTag();item=["channel":capturedChannel,"note":n,"instrument":n<128 ? (Int(instrument.stringValue) ?? 1):0,"velocity":n<128 ? (Int(velocity.stringValue) ?? 127):127]}
    item["position"]=capturedRow*65536+max(0,min(65535,position))
    guard !collision(item,excluding:nil) else{status.stringValue="A hit already exists at this position.";return}
    draft.append(item);sortDraft(selecting:item);scheduleSave()
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
    guard canEdit,!inlineInvalid,let index=editingRow,saveSelectedFields(),draft.indices.contains(index),
      let count=Int(repeatCount.stringValue),(2...64).contains(count),let last=Int(endVolume.stringValue),(1...127).contains(last) else {status.stringValue="Select a hit; use 2–64 retriggers and end volume 1–127.";return}
    let original=draft[index],event=PreciseNote(original);guard event.note<128 else{status.stringValue="Choose a note onset to retrigger.";return}
    let start=event.position%65536,remaining=65536-start;guard remaining>=count else{status.stringValue="Move the first hit earlier to fit this many retriggers.";return}
    var copies=[[String:Any]]()
    for n in 0..<count {
      var item=original;item["position"]=capturedRow*65536+start+n*remaining/count
      item["velocity"]=Int((Double(event.velocity)+Double(last-event.velocity)*Double(n)/Double(count-1)).rounded())
      guard !collision(item,excluding:index) else{status.stringValue="Retriggers overlap an existing hit. Remove it or choose a different count/start.";return};copies.append(item)
    }
    draft.remove(at:index);draft.append(contentsOf:copies);sortDraft(selecting:copies.first);scheduleSave()
  }
  func put(replacing:Bool) {
    guard canEdit,!inlineInvalid,!replacing || editingRow != nil,let item=editedEvent(replacing:replacing ? editingRow:nil) else{return}
    if replacing,let index=editingRow {draft[index]=item} else {draft.append(item)}
    sortDraft(selecting:item);scheduleSave();controls()
  }
  func remove() {
    guard canEdit,!inlineInvalid,let index=editingRow,draft.indices.contains(index) else{return}
    draft.remove(at:index);refreshDraft(selecting:draft.isEmpty ? nil:min(index,draft.count-1));scheduleSave();controls()
  }
  func apply(dryRun:Bool) {
    guard !pending,!inlineInvalid,let revision,let request=onRequest,saveSelectedFields() else{return}
    autoSave?.cancel()
    if !(window?.firstResponder is NSTextView) {sortDraft(selecting:editingRow.map{draft[$0]})}
    let submittedDraft=draft,submittedFields=fieldValues
    let replacement=events.filter{!belongs($0)}+draft
    pending=true;controls()
    request("pattern.notes.set",["pattern":capturedPattern,"events":replacement,"clearRows":replaceLegacy.state == .on ? [["row":capturedRow,"channel":capturedChannel]]:[],"clearRowEffects":moveLegacyEffect && replaceLegacy.state == .on,"expectedRevision":revision,"dryRun":dryRun]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let next=result["revision"] as? String else{self.failure(reply);return}
      self.revision=next;if !dryRun {self.events=replacement;self.originalDraft=submittedDraft;if self.fieldValues==submittedFields {self.originalFields=submittedFields};self.moveLegacyEffect=false}
      self.status.stringValue=dryRun ? "Edit is valid.":"Saved · Undo restores the previous notes.";self.controls()
      if !dryRun,self.hasDraft,!self.inlineInvalid {self.scheduleSave()}
    }
  }
  func numberOfRows(in tableView:NSTableView)->Int {draft.count}
  private func cellText(_ event:PreciseNote,_ column:String)->String {
    switch column {
    case "beats":return String(format:"%.7g",Double(event.position%65536)/65536/Double(rowsPerBeat))
    case "offset":return String(format:"%.7g",Double(event.position%65536)/65536)
    case "note":return event.name
    case "instrument":return event.note<128 ? String(event.instrument):"—"
    case "effect":return event.effect==0 ? "..":(effects.first{$0.command==event.effect && event.parameter&$0.mask==$0.value}?.displayCode ?? "?")
    case "parameter":return String(format:"%02X",event.parameter & ~(effects.first{$0.command==event.effect && event.parameter&$0.mask==$0.value}?.mask ?? 0))
    default:return event.note<128 ? String(event.velocity):"—"
    }
  }
  func tableView(_ tableView:NSTableView,viewFor column:NSTableColumn?,row:Int)->NSView? {
    guard draft.indices.contains(row),let column else{return nil}
    let text=cellText(PreciseNote(draft[row]),column.identifier.rawValue)
    let field:PreciseNoteCell
    if let reused=tableView.makeView(withIdentifier:column.identifier,owner:self) as? PreciseNoteCell {field=reused}
    else {
      field=PreciseNoteCell(string:"");field.identifier=column.identifier
      field.font = .monospacedSystemFont(ofSize:12,weight:.regular);field.isBordered=false;field.drawsBackground=false;field.delegate=self
    }
    setText(field,text);field.textColor=Theme.text;field.tag=1000+row*10+(table.tableColumns.firstIndex(of:column) ?? 0)
    field.onChoose = {[weak self] in
      guard let self,!self.inlineInvalid,self.draft.indices.contains(row) else{return false}
      self.table.activeColumn=self.table.tableColumns.firstIndex(of:column) ?? 0
      self.table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false)
      self.window?.makeFirstResponder(self.table);self.table.needsDisplay=true;return true
    }
    field.setAccessibilityLabel("Hit \(row+1) \(column.title)");return field
  }
  func tableViewSelectionDidChange(_ notification:Notification) {
    guard !updatingTable else{return}
    if inlineInvalid {updatingTable=true;if let editingRow {table.selectRowIndexes(IndexSet(integer:editingRow),byExtendingSelection:false)};updatingTable=false;return}
    if !saveSelectedFields() {updatingTable=true;if let editingRow {table.selectRowIndexes(IndexSet(integer:editingRow),byExtendingSelection:false)};updatingTable=false;return}
    if let editingRow {refreshVisibleCells(row:editingRow)}
    loadSelectedFields();syncTimeline();if hasDraft {scheduleSave()}
  }
  private func loadSelectedFields() {
    editingRow=draft.indices.contains(table.selectedRow) ? table.selectedRow:nil
    guard let editingRow else{controls();return}
    let e=PreciseNote(draft[editingRow]);setText(offset,offsetText(e.position%65536));select(note,tag:e.note);setText(instrument,String(e.instrument));setText(velocity,String(e.velocity))
    select(effect,index:effects.firstIndex{$0.command==e.effect && e.parameter&$0.mask==$0.value} ?? 0);setText(parameter,String(format:"%02X",e.parameter))
    setText(effectHint,effects.indices.contains(effect.indexOfSelectedItem) ? effects[effect.indexOfSelectedItem].hint:"")
    originalFields=fieldValues;showOffsetHint();controls()
  }
  private func showOffsetHint() {
    if let value=rowOffset() {setText(offsetHint,displayBeats ? String(format:"= %.6g row",value):String(format:"= %.6g beat",value/Double(rowsPerBeat)))}
  }
  private func failure(_ reply:[String:Any]) {autoSave?.cancel();status.stringValue=((reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed.")+" Edits retained; use current cursor to reload.";controls()}
  private func controls() {
    table.isEnabled = canEdit;timeline.enabled = canEdit
    func enable(_ control:NSControl?,_ value:Bool) {if let control,control.isEnabled != value{control.isEnabled=value}}
    for view in [note,instrument,velocity,offset,units,snap,replaceLegacy,repeatCount,endVolume] as [NSControl] {enable(view,canEdit)}
    enable(effect,canEdit && note.selectedTag()<128);enable(parameter,effect.isEnabled)
    enable(reloadButton,!pending);enable(addButton,canEdit)
    enable(updateButton,canEdit && draft.indices.contains(table.selectedRow));enable(removeButton,updateButton?.isEnabled ?? false)
    enable(repeatButton,canEdit && editingRow != nil && note.selectedTag()<128)
    enable(previewButton,!pending && revision != nil);enable(applyButton,previewButton?.isEnabled ?? false)
  }
}
