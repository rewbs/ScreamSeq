import AppKit
extension InterfaceTests {
  static var preciseData:[String:Any] { ["pattern":0,"rows":64,"events":[
    ["channel":0,"position":1234,"note":61,"instrument":2,"velocity":93],
    ["channel":0,"position":9000,"note":255,"instrument":0,"velocity":127],
    ["channel":1,"position":65536,"note":65,"instrument":2,"velocity":100]]] }
  static func preciseNotesFixture()->PreciseNotesEditor {
    let editor=PreciseNotesEditor(frame:.zero);editor.onContext={(PatternModel(["rows":64,"channels":4]),0,0,2)}
    editor.onRequest={_,_,reply in reply(["result":["revision":"notes:1","data":preciseData]])};editor.capture();editor.onRequest=nil;return editor
  }
  static func preciseNotesChecks() throws {
    // Regression for the reported capture() trap: tracker volume is UInt8,
    // but scaling 0...64 to MIDI velocity needs a wider intermediate.
    let ordinary=PreciseNotesEditor(frame:.zero);ordinary.units.selectItem(at:1);ordinary.changeUnits()
    var source=PatternModel(["rows":64,"channels":1])
    ordinary.onContext={(source,0,0,2)}
    ordinary.onRequest={_,_,reply in reply(["result":["revision":"legacy:1","data":["pattern":0,"events":[]]]])}
    for volume in [64] + Array(0...255) {
      source.cells=[61,2,1,UInt8(volume),0,0]
      ordinary.capture()
      try require(ordinary.draft.first?["velocity"] as? Int == max(1,min(127,volume*127/64)),
        "Every stored tracker volume converts without overflow")
      try require(ordinary.note.selectedTag()==61 && ordinary.instrument.stringValue=="2",
        "Ordinary-note drafts preserve numeric types through the editor")
      try require(ordinary.velocity.stringValue==String(max(1,min(127,volume*127/64))),
        "Recapturing the same selected row refreshes its fields")
    }
    for note in [254,255] {
      source.cells=[UInt8(note),2,1,64,0,0];ordinary.capture()
      try require(ordinary.note.selectedTag()==note && ordinary.draft.first?["instrument"] as? Int==0,
        "Ordinary releases retain their kind and clear the instrument")
    }
    let editor=PreciseNotesEditor(frame:.zero);editor.units.selectItem(at:1);editor.changeUnits();var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onContext={(PatternModel(["rows":64,"channels":4]),0,0,2)}
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    editor.capture();editor.capture();try require(calls.count==1 && editor.pending,"Only one precise-note request while pending")
    replies.removeFirst()(["result":["revision":"notes:1","data":preciseData]])
    try require(editor.draft.count==2 && editor.note.selectedTag()==61 && editor.velocity.stringValue=="93","Row editor reads onset and release with exact velocity")
    // Typing a value and clicking Check/Apply must include the visible fields,
    // without a separate, easy-to-miss "Update selected" step.
    editor.offset.stringValue="0.03125"
    editor.apply(dryRun:true);editor.apply(dryRun:false)
    let request=calls.last!.1,events=request["events"] as! [[String:Any]]
    try require(calls.count==2 && request["expectedRevision"] as? String=="notes:1" && request["clearLegacy"]==nil &&
      (request["clearRows"] as? [[String:Int]])==[["row":0,"channel":0]] && events.contains{$0["position"] as? Int==2048} && events.contains{$0["channel"] as? Int==1},"Precise-note preview pins revision and clears only the edited ordinary row")
    replies.removeFirst()(["error":["message":"Song changed"]]);try require(editor.revision=="notes:1" && editor.draft.count==2,"Rejected precise edit retains draft")
    editor.offset.stringValue="nan";editor.put(replacing:false);try require(editor.draft.count==2,"Invalid fractional timing stays local")
    editor.apply(dryRun:false)
    try require(calls.count==2 && !editor.pending,"Apply rejects invalid visible fields instead of submitting an old draft")
    editor.offset.stringValue=" 0.5 ";editor.apply(dryRun:false)
    try require((calls.last!.1["events"] as? [[String:Any]])?.contains{$0["note"] as? Int==61 && $0["position"] as? Int==32768} == true &&
      editor.table.selectedRow==1 && editor.note.selectedTag()==61,"Apply commits the typed half-row offset and keeps the same event selected after sorting")
    replies.removeFirst()(["result":["revision":"notes:2","data":[:]]])
    editor.velocity.stringValue="100";editor.apply(dryRun:false)
    try require((calls.last!.1["events"] as? [[String:Any]])?.contains{$0["note"] as? Int==61 && $0["velocity"] as? Int==100} == true,
      "Editing after reordering still targets the onset, not its release")
    replies.removeFirst()(["result":["revision":"notes:3","data":[:]]])
    editor.offset.stringValue="0.75";editor.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    try require(editor.draft.contains{$0["note"] as? Int==61 && $0["position"] as? Int==49152} && editor.note.selectedTag()==255,
      "Selecting another event retains the edited fields")
    editor.offset.stringValue="1e99";editor.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false)
    try require(editor.table.selectedRow==0 && editor.offset.stringValue=="1e99","Invalid edits stay visible when selection changes")
    editor.offset.stringValue="0.9";editor.apply(dryRun:false)
    let moved=calls.last!.1["events"] as! [[String:Any]]
    try require(moved.contains{$0["note"] as? Int==255 && $0["position"] as? Int==58982} && moved.contains{$0["position"] as? Int==49152},
      "Direct Apply preserves both edited onset and release")
    replies.removeFirst()(["result":["revision":"notes:4","data":[:]]])
    editor.remove();editor.remove();editor.apply(dryRun:false)
    try require((calls.last!.1["events"] as? [[String:Any]])?.count==1 && editor.draft.isEmpty,
      "Removing all row events does not re-add the visible fields on Apply or remove another channel")
    replies.removeFirst()(["result":["revision":"notes:5","data":[:]]])
    try require(editor.previewButton.title=="Check edit","Validation is not labeled as an audible preview")
    source.cells=[61,2,1,64,0,0];ordinary.capture();var saved=[[String:Any]]()
    ordinary.onRequest={_,params,reply in saved=params["events"] as! [[String:Any]];reply(["result":["revision":"legacy:2","data":[:]]])}
    ordinary.offset.stringValue="0.5";ordinary.apply(dryRun:false)
    try require(saved.first?["position"] as? Int==32768 && saved.first?["instrument"] as? Int==2,
      "An ordinary note can be delayed by typing half a row and clicking Apply directly")
    let mini=PreciseNotesEditor(frame:NSRect(x:0,y:0,width:640,height:800));var sent=[String:Any]()
    mini.onContext={(PatternModel(["rows":64,"channels":2,"displayRowsPerBeat":4]),5,0,2)}
    let fx:[[String:Any]]=[["command":9,"label":"Xxx","name":"Panning","minimum":0,"maximum":255,"suggestedParameter":128]]
    mini.onRequest={method,params,reply in
      if method=="pattern.notes.get" {reply(["result":["revision":"mini:1","data":["pattern":0,"rowsPerBeat":4,"effects":fx,"events":[["channel":0,"position":5*65536,"note":61,"instrument":2,"velocity":100,"effect":9,"parameter":128],["channel":1,"position":5*65536,"note":65,"instrument":2,"velocity":70]]]]])}
      else {sent=params;reply(["result":["revision":"mini:2","data":[:]]])}
    }
    mini.capture();mini.offset.stringValue="1/8";mini.put(replacing:true)
    try require(mini.draft[0]["position"] as? Int==5*65536+32768,"Beat fraction enters a precise onset halfway through the captured row")
    mini.units.selectItem(at:1);mini.changeUnits();try require(mini.offset.stringValue=="0.5","Switching units preserves musical position")
    mini.units.selectItem(at:0);mini.changeUnits();try require(mini.offset.stringValue=="0.125","Switching back shows beats without quantization")
    mini.repeatCount.stringValue="4";mini.endVolume.stringValue="40";mini.makeRetriggers()
    try require(mini.draft.map{$0["position"] as! Int}==[360448,368640,376832,385024] && mini.draft.map{$0["velocity"] as! Int}==[100,80,60,40],"Retrigger fill uses remaining row and independent ramped volumes")
    try require(mini.draft.allSatisfy{$0["effect"] as? Int==9 && $0["parameter"] as? Int==128},"Retrigger generation clones its source effect")
    mini.moveEvent(3,offset:100000,volume:2,finished:true)
    try require(mini.draft.last?["position"] as? Int==6*65536-1,"Dragging cannot cross the row end")
    mini.duplicate();try require(mini.draft.count==4,"Duplicating the last possible position cannot trap or cross the row")
    mini.moveEvent(0,offset:-100,volume:200,finished:true)
    try require(mini.draft.first?["position"] as? Int==5*65536 && mini.draft.first?["velocity"] as? Int==127,"Dragging clamps time and volume inside the row")
    mini.timeline.frame=NSRect(x:0,y:0,width:600,height:205)
    try require(mini.timeline.offset(at:mini.timeline.plot.minX-100)==0 && mini.timeline.offset(at:mini.timeline.plot.maxX+100)==65535,"Timeline geometry respects both row boundaries")
    mini.timeline.snapBeats=1.0/16
    try require(mini.timeline.offset(at:mini.timeline.plot.minX+mini.timeline.plot.width*0.28)==16384,"Beat snapping respects the containing beat's grid")
    let before=mini.draft;mini.moveEvent(1,offset:0,volume:60,finished:true)
    try require(NSArray(array:mini.draft).isEqual(to:before),"Colliding drags never silently delete another retrigger")
    mini.offset.stringValue="1/4";mini.apply(dryRun:false);try require(sent.isEmpty,"An offset at the next row is rejected")
    mini.offset.stringValue="0";mini.apply(dryRun:false)
    try require((sent["events"] as? [[String:Any]])?.count==5 && (sent["events"] as? [[String:Any]])?.contains{$0["channel"] as? Int==1} == true,"Mini editor preserves other channels and applies all occurrences in one revision-guarded edit")
    try require(!mini.hasDraft && mini.timeline.events.count==4,"Applied mini timeline settles without a stray dirty state")
    try require(PreciseNotesEditor.number("3/16")==0.1875 && PreciseNotesEditor.number("1/0")==nil && PreciseNotesEditor.number("nan")==nil,"Musical fractions have strict finite validation")
    let retained=preciseNotesFixture();retained.onRequest={_,_,reply in reply(["result":["revision":"notes:2","data":preciseData]])}
    _=retained.selectEvent(1);retained.capture();try require(retained.table.selectedRow==1 && retained.note.selectedTag()==255,"Refreshing the same inspected row after Apply retains the selected occurrence")
    // Exercise the actual AppKit mouse handlers in an offscreen window. This
    // complements native UI inspection without sending system mouse events.
    let dragCanvas=PreciseNoteTimeline(frame:NSRect(x:0,y:0,width:600,height:205))
    let dragWindow=NSWindow(contentRect:dragCanvas.frame,styleMask:[],backing:.buffered,defer:false)
    dragWindow.contentView=dragCanvas;dragCanvas.row=5;dragCanvas.rowsPerBeat=4
    dragCanvas.events=[PreciseNote(["channel":0,"position":5*65536+16384,"note":61,"instrument":2,"velocity":64])]
    dragCanvas.onSelect={_ in true};var moves=[(Int,Int,Bool)]()
    dragCanvas.onMove={index,position,volume,finished in
      moves.append((position,volume,finished));var e=dragCanvas.events[index].dictionary;e["position"]=5*65536+position;e["velocity"]=volume;dragCanvas.events[index]=PreciseNote(e)
    }
    func mouse(_ type:NSEvent.EventType,_ point:NSPoint,_ modifiers:NSEvent.ModifierFlags=[])->NSEvent {
      NSEvent.mouseEvent(with:type,location:dragCanvas.convert(point,to:nil),modifierFlags:modifiers,timestamp:0,windowNumber:dragWindow.windowNumber,context:nil,eventNumber:0,clickCount:1,pressure:1)!
    }
    dragCanvas.mouseDown(with:mouse(.leftMouseDown,dragCanvas.point(dragCanvas.events[0])))
    dragCanvas.mouseDragged(with:mouse(.leftMouseDragged,NSPoint(x:dragCanvas.plot.maxX+200,y:dragCanvas.plot.maxY+50)))
    dragCanvas.mouseUp(with:mouse(.leftMouseUp,NSPoint(x:dragCanvas.plot.maxX+200,y:dragCanvas.plot.maxY+50)))
    try require(moves.last?.0==65535 && moves.last?.1==1 && moves.last?.2==true,"Real timeline drag handlers clamp both axes and finish their edit")
    dragCanvas.mouseDown(with:mouse(.leftMouseDown,dragCanvas.point(dragCanvas.events[0])))
    dragCanvas.mouseDragged(with:mouse(.leftMouseDragged,NSPoint(x:dragCanvas.plot.minX-50,y:dragCanvas.plot.minY),.shift))
    try require(moves.last?.0==0 && moves.last?.1==1,"Shift-drag preserves volume while clamping the start boundary")
    let grid=PatternView();grid.model=PatternModel(["channels":4,"rows":64,"preciseNotes":preciseData["events"]!]);grid.frame=NSRect(x:0,y:0,width:800,height:400)
    var opened=0,cleared=[Int]();grid.onPreciseNotes={opened += 1};grid.onClearPreciseNotes={cleared=[$0,$1]}
    key(grid,36,"\r");key(grid,51,"");try require(opened==1 && cleared==[0,0] && grid.currentCommandHelp.contains("2 precise"),"Precise row keyboard editing and deletion use native events")
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count,let image=grid.offscreenImage(),let bytes=NSBitmapImageRep(cgImage:image).representation(using:.png,properties:[:]) {
      let directory=URL(fileURLWithPath:CommandLine.arguments[index+1],isDirectory:true)
      try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
      try bytes.write(to:directory.appendingPathComponent("PreciseNotesGrid.png"))
    }
  }
}
