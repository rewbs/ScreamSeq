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
    try require(picker.filtered.count==8 && picker.table.selectedRow==1 && picker.parameter.stringValue=="D3", "Picker identifies extended command without losing its low parameter digit")
    let grid = PatternView();grid.model=model;grid.column=4
    try require(grid.currentCommandHelp.contains("Delay in ticks"),"Cursor help explains the exact extended command")
    try require(model.commands.entry(command:20,parameter:211)?.family=="timing" && model.commands.entry(command:20,parameter:129)==nil, "Unknown extended prefixes are not mislabeled")
    try require(model.commands.entry(command:20,parameter:211)?.rgba != model.commands.entry(command:5,parameter:0)?.rgba,"Pitch and timing families have distinct colors")
    var calls = [[String:Any]](), replies = [([String:Any])->Void]()
    picker.onRequest = { params,reply in calls.append(params);replies.append(reply) }
    picker.parameter.stringValue="C2";picker.apply()
    try require(calls.isEmpty,"Picker refuses another extended prefix")
    picker.parameter.stringValue="D4";cursor=6;picker.apply();picker.apply();picker.capture()
    let patch=calls[0]["command"] as! [String:Any]
    try require(calls.count==1 && calls[0]["expectedRevision"] as? String=="original" && calls[0]["row"] as? Int==0 && calls[0]["column"] as? Int==0 && patch.count==3 && patch["parameter"] as? Int==212,
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
    grid.column=3;grid.model=model
    var typed="",typedEdits=0
    grid.onTypedNativeEffect={typed=$0};grid.onTrackerEffect={_,_,_,_,_ in typedEdits+=1}
    key(grid,35,"p");try require(grid.effectPrefix=="P" && typedEdits==0,"First letter waits without writing a stray P command")
    key(grid,1,"s");try require(typed=="parameter-set" && typedEdits==0,"PS opens parameter set directly")
    key(grid,35,"p");key(grid,37,"l");try require(typed=="parameter-slide" && typedEdits==0,"PL opens parameter slide directly")
    key(grid,11,"b");key(grid,37,"l");try require(typed=="pitch-slide","BL opens pitch slide directly")
    var changedCell=[UInt8]();grid.onTrackerEffect={row,ch,_,effect,parameter in changedCell=grid.model.cell(row,ch);changedCell[4]=UInt8(effect);changedCell[5]=UInt8(parameter)}
    key(grid,1,"s");key(grid,2,"d");try require(changedCell[4]==20 && changedCell[5]==211 && grid.column==4,"SD keeps its parameter low nibble and selects the value field")
    var scModel=PatternModel(["format":"IT","commandCatalog":["effect":[clear,delay,pitch,["command":20,"parameterMask":240,"parameterValue":192,"label":"SCx","name":"Note Cut"]]]])
    scModel.replaceCell(0,0,with:[49,2,1,32,20,211])
    grid.model=scModel;grid.column=3
    grid.onTrackerEffect={row,ch,_,effect,parameter in changedCell=grid.model.cell(row,ch);changedCell[4]=UInt8(effect);changedCell[5]=UInt8(parameter);grid.model.replaceCell(row,ch,with:changedCell)}
    key(grid,1,"s");key(grid,8,"c");key(grid,19,"2")
    try require(changedCell[4]==20 && changedCell[5]==0xC2,"SC2 preserves C when typing the tick instead of turning into another S effect")
    key(grid,20,"3");try require(changedCell[5]==0xC3,"Changing the SC tick changes only its low digit")
    grid.model=PatternModel(["channels":1,"rows":64,"effectColumns":[8],"commandCatalog":["effect":[clear,delay,pitch,["command":20,"parameterMask":240,"parameterValue":192,"label":"SCx","name":"Note Cut"]]]])
    var editsByColumn=[Int:Int]()
    grid.onTrackerEffect={row,ch,col,effect,parameter in
      editsByColumn[col]=parameter
      grid.model.performanceCommands[(row*grid.model.channels+ch)*8+col]=NativePatternCommand(["channel":ch,"position":row*65536,"column":col,"kind":"tracker","effect":effect,"parameter":parameter])
    }
    for fx in 0..<8 {
      grid.column=3+2*fx;key(grid,1,"s");key(grid,8,"c");key(grid,19,"2")
      try require(editsByColumn[fx]==0xC2 && grid.column==4+2*fx,"SC and its tick work in every equal FX column")
      grid.column=3+2*fx;key(grid,35,"p");key(grid,37,"l");try require(typed=="parameter-slide","PL is available in every FX column")
    }
    grid.column=18;grid.deferringEffectKeys=true
    key(grid,20,"3");key(grid,21,"4")
    try require(editsByColumn[7]==0xC2,"Pending FX write retains incoming keystrokes")
    grid.finishEffectKeys(success:true)
    try require(editsByColumn[7]==0xC4,"Queued value edits replay in order against the new cell")
    grid.deferringEffectKeys=true;key(grid,23,"5");grid.finishEffectKeys(success:false)
    try require(editsByColumn[7]==0xC4,"Rejected FX edit discards queued input rather than applying to uncertain data")
    grid.model.effectLetters=Array(repeating:".",count:21);grid.model.effectLetters[20]="S"
    grid.column=3;var deferredCommit=0
    grid.canEdit={!grid.deferringEffectKeys}
    grid.onTrackerEffect={_,_,_,_,_ in deferredCommit+=1;grid.deferringEffectKeys=true}
    key(grid,1,"s");key(grid,124,"")
    try require(deferredCommit==1 && grid.column==3,"A pending single-letter effect commits before navigation")
    grid.finishEffectKeys(success:true)
    try require(grid.column==4 && deferredCommit==1,"Navigation after a pending effect survives its asynchronous save exactly once")
    grid.canEdit={true};grid.onTrackerEffect={_,_,_,_,_ in}
    grid.column=3;key(grid,45,"n");key(grid,8,"c");try require(typed=="note-cut","NC opens the precise cut editor")
    grid.column=3;key(grid,35,"p");key(grid,53,"\u{1b}");try require(grid.effectPrefix.isEmpty,"Escape cancels incomplete command entry")
    grid.cyclePositionMode();try require(grid.positionMode == .beats && grid.positionLabels[4]=="1.000" && grid.gutterWidth==92,"Beat ruler uses the pattern signature and expands its gutter")
    grid.positionTimes=(0..<grid.model.rows).map{["patternSeconds":Double($0)*0.12,"songSeconds":12+Double($0)*0.12]}
    grid.cyclePositionMode();try require(grid.positionLabels[4]=="00:00.480","Pattern time uses measured row positions")
    grid.cyclePositionMode();try require(grid.positionLabels[4]=="00:12.480","Song time includes the order occurrence offset")
    grid.cyclePositionMode();try require(grid.positionMode == .rows && grid.gutterWidth==52,"Ruler cycles back without changing note columns")
    let floating=EffectFinderPanel(picker:picker);floating.contentView?.layoutSubtreeIfNeeded()
    try require(floating.canBecomeKey && !floating.canBecomeMain && floating.contentView!.bounds.width>=600,"Finder accepts search focus without replacing the document main window or shrinking its list")

    picker.capture();picker.onRequest=nil
    return picker
  }
}
