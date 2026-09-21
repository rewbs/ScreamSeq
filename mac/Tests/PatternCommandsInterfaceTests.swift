import AppKit
extension InterfaceTests {
  static func patternCommandsChecks() throws -> PatternCommandPicker {
    let clear: [String: Any] = ["command":0,"label":"...","name":"Clear command","description":"Clear the chosen command.","maximum":0]
    let delay: [String: Any] = ["command":20,"parameterMask":240,"parameterValue":208,"suggestedParameter":208,"minimum":208,"maximum":223,"label":"SDx","name":"Note Delay","family":"timing","description":"Delay in ticks, not rows."]
    let pitch: [String: Any] = ["command":5,"label":"Hxx","name":"Vibrato","family":"pitch","description":"Speed and depth."]
    let volume: [String: Any] = ["command":1,"label":"vxx","name":"Set Volume","family":"volume","description":"0 to 64.","maximum":64]
    var model = PatternModel(["format":"IT","revisionToken":"original","commandCatalog":["effect":[clear,delay,pitch],"volume":[clear,volume]]])
    model.replaceCell(0, 0, with:[49,2,1,32,20,211])
    var cursor = 0
    let picker = PatternCommandPicker(frame:.zero)
    picker.onContext = { (model,cursor,0,4) }
    picker.capture()
    try require(picker.filtered.count==7 && picker.table.selectedRow==1 && picker.parameter.stringValue=="D3", "Picker identifies extended command without losing its low parameter digit")
    let grid = PatternView();grid.model=model;grid.column=4
    try require(grid.currentCommandHelp.contains("Delay in ticks"),"Cursor help explains the exact extended command")
    try require(model.commands.entry(command:20,parameter:211)?.family=="timing" && model.commands.entry(command:20,parameter:129)==nil, "Unknown extended prefixes are not mislabeled")
    try require(model.commands.entry(command:20,parameter:211)?.rgba != model.commands.entry(command:5,parameter:0)?.rgba,"Pitch and timing families have distinct colors")
    var calls = [[String:Any]](), replies = [([String:Any])->Void]()
    picker.onRequest = { params,reply in calls.append(params);replies.append(reply) }
    picker.parameter.stringValue="C2";picker.apply()
    try require(calls.isEmpty,"Picker refuses another extended prefix")
    picker.parameter.stringValue="D4";cursor=6;picker.apply();picker.apply();picker.capture()
    let patch=(calls[0]["cells"] as! [[String:Any]])[0]
    try require(calls.count==1 && calls[0]["expectedRevision"] as? String=="original" && patch["row"] as? Int==0 && patch.count==5 && patch["parameter"] as? Int==212,
      "Picker applies only two chosen fields to its pinned cell with the displayed revision; no duplicate or rebase while pending")
    replies.removeFirst()(["error":["message":"Song changed"]])
    try require(picker.captured.revisionToken=="original" && picker.status.stringValue.contains("reopen"),"Rejected edits require an explicit context refresh")
    picker.capture();try require(picker.row==6,"Use cursor explicitly changes the edit target")
    picker.search.stringValue="vibrato";picker.reload(selectCurrent:false);picker.parameter.stringValue="34";picker.apply()
    replies.removeFirst()(["result":["revision":"saved","data":["changedCells":1]]])
    try require(picker.captured.cell(6,0)[4]==5 && picker.captured.cell(6,0)[5]==52 && picker.captured.cell(0,0)==model.cell(0,0),"Completed edit updates only the pinned local cell")
    picker.columnPicker.selectItem(at:1);picker.search.stringValue="volume";picker.changeColumn()
    picker.parameter.stringValue="65";let count=calls.count;picker.apply()
    try require(calls.count==count,"Volume picker refuses out-of-range values")
    picker.parameter.stringValue="40";picker.apply()
    let volumePatch=(calls.last!["cells"] as! [[String:Any]])[0]
    try require(volumePatch["volume"] as? Int==40 && volumePatch["effect"]==nil,"Volume values use decimal and preserve effect fields")
    replies.removeFirst()(["result":["revision":"saved-volume","data":["changedCells":1]]])
    picker.search.stringValue="nothing matches";picker.reload(selectCurrent:false);let emptyCount=calls.count;picker.apply()
    try require(calls.count==emptyCount,"An empty search cannot dispatch a command")
    model.replaceCell(0,0,with:[252,1,1,0,20,211]);cursor=0;picker.capture();let pcCount=calls.count;picker.apply()
    try require(calls.count==pcCount && picker.status.stringValue.contains("parameter-control"),"Command picker cannot reinterpret imported parameter-control columns")
    model.replaceCell(0,0,with:[49,2,1,32,20,211]);picker.capture()
    picker.search.stringValue="slide plugin";picker.reload(selectCurrent:false)
    var selectedKind="",dismissed=false
    picker.onNativeCommand={kind,_,_,_,_ in selectedKind=kind};picker.onDismiss={dismissed=true}
    try require(picker.filtered.count==1 && picker.filtered[0].displayCode=="PL","Native effects use explicit multi-character codes and multi-word search")
    picker.apply();try require(selectedKind=="parameter-slide","Selecting a native effect opens its high-resolution target editor")
    _=picker.control(picker.search,textView:NSTextView(),doCommandBy:#selector(NSResponder.cancelOperation(_:)))
    try require(dismissed,"Escape dismisses effect search without writing a cell")
    var searches=0;grid.onEffectPicker={searches += 1};key(grid,44,"?",flags:.shift)
    try require(searches==1,"Question mark in an effect cell opens search without entering a note")
    key(grid,44,"/",flags:.shift);try require(searches==2,"Shift slash also works when the keyboard source supplies an unshifted character")
    let floating=EffectFinderPanel(picker:picker);floating.contentView?.layoutSubtreeIfNeeded()
    try require(floating.canBecomeKey && !floating.canBecomeMain && floating.contentView!.bounds.width>=600,"Finder accepts search focus without replacing the document main window or shrinking its list")

    picker.capture();picker.onRequest=nil
    return picker
  }
}
