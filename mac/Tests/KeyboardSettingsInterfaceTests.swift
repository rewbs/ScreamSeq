import AppKit

extension InterfaceTests {
  static func keyboardSettingsChecks() throws {
    let oldMapping=KeyboardSettings.volatileMapping,oldProvider=KeyboardSettings.playStopKeysProvider
    defer {KeyboardSettings.volatileMapping=oldMapping;KeyboardSettings.playStopKeysProvider=oldProvider}
    let mapping=try KeyboardSettings.validated("ZSXDCVGBHNJM","Q2W3ER5T6Y7UI")
    KeyboardSettings.publish(mapping,persist:false)
    try require(KeyboardSettings.note(for:"i")==24 && KeyboardSettings.note(for:"z")==0,"Optional upper C extends the musical keyboard to 25 chromatic notes")
    for rows in [("duplicate","q2w3er5t6y7ui"),("zsxdcvgbhnjm","q2w3er5t6y7um"),("zsxdcvgbhnjm","q2w3er5t6y7u "),("zsxdcvgbhnjm","q2w3er5t6y7u😀")] {
      do {_=try KeyboardSettings.validated(rows.0,rows.1);throw InterfaceFailure(message:"Invalid note row accepted")}catch is KeyboardSettings.Failure {}
    }
    let palette=WorkspaceCommandPalette();palette.persistPreferences=false
    let transport=NSMenuItem(title:"Play / Stop",action:NSSelectorFromString("playOrStop"),keyEquivalent:"")
    let command=WorkspaceCommandPalette.Entry(item:transport,path:"Playback / Play / Stop")
    let occupied=NSMenuItem(title:"Commands",action:#selector(NSObject.description),keyEquivalent:"k");occupied.keyEquivalentModifierMask = .command
    palette.entries=[command,.init(item:occupied,path:"Workspace / Commands")]
    KeyboardSettings.playStopKeysProvider={palette.shortcutKeys(command)}
    try require(palette.setShortcut(command.id,keys:["return"])==nil && KeyboardSettings.transportKey==36 && transport.keyEquivalent.isEmpty,
      "A remapped Return transport is routed locally instead of stealing native text/menu keys")
    let before=KeyboardSettings.snapshot(keys:palette.shortcutKeys(command))
    try require(palette.setShortcut(command.id,keys:["cmd+k"]) != nil && KeyboardSettings.snapshot(keys:palette.shortcutKeys(command))["revision"] as? String==before["revision"] as? String,
      "Conflicting transport shortcuts reject without changing the keyboard revision")
    try require(palette.setShortcut(command.id,keys:[])==nil && KeyboardSettings.transportKey==UInt16.max,
      "Disabling Play/Stop also disables the pattern editor's legacy Space/Return interception")
    try require(palette.setShortcut(command.id,keys:["ctrl+g","p"])==nil && KeyboardSettings.playStopKeys==["ctrl+g","p"] && palette.sequences.bindings[command.id]?.count==2,
      "Custom transport sequences share the command palette's dispatch and visible binding")
    try require(palette.setShortcut(command.id,keys:["z"]) != nil,"Transport remapping cannot swallow ordinary musical note input")
    _=palette.setShortcut(command.id,keys:["return"])
    let grid=PatternView();var toggles=0;grid.onTransport={toggles+=1}
    func event(_ code:UInt16,_ text:String,_ flags:NSEvent.ModifierFlags=[])->NSEvent {NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:flags,timestamp:0,windowNumber:0,context:nil,characters:text,charactersIgnoringModifiers:text,isARepeat:false,keyCode:code)!}
    grid.keyDown(with:event(49," "));grid.keyDown(with:event(36,"\r"))
    try require(toggles==1,"Changing Play/Stop to Return removes the old unmodified Space binding")
    grid.keyDown(with:event(36,"\r",.option))
    try require(toggles==1,"Option-Return cannot fall through into the unmodified transport binding")
    _=palette.setShortcut(command.id,keys:[]);grid.keyDown(with:event(36,"\r"))
    try require(toggles==1,"A disabled transport stays disabled in the pattern responder")
    grid.model=PatternModel(["channels":4,"rows":64]);grid.octave=4;grid.instrument=1
    var notes=[(Int,Int,Int,Bool)]();grid.onAudition={notes.append(($0,$1,$2,$3))}
    grid.keyDown(with:event(6,"z"))
    KeyboardSettings.publish(try KeyboardSettings.validated("sxdcvgbhnjmz","q2w3er5t6y7ui"),persist:false)
    grid.instrument=2;grid.cursorChannel=1
    grid.keyUp(with:NSEvent.keyEvent(with:.keyUp,location:.zero,modifierFlags:[],timestamp:0,windowNumber:0,context:nil,characters:"z",charactersIgnoringModifiers:"z",isARepeat:false,keyCode:6)!)
    try require(notes.count==2 && notes[0].0==49 && notes[1].0==49 && notes[1].1==1 && notes[1].2==0 && !notes[1].3,
      "Key-up after remapping releases the pitch, instrument and channel captured at key-down")
    KeyboardSettings.publish(mapping,persist:false)
    let editor=KeyboardSettings();editor.persistPreferences=false
    editor.onRead={KeyboardSettings.snapshot(keys:palette.shortcutKeys(command))};editor.load()
    editor.low.stringValue="unfinished mapping"
    editor.onWrite={_ in throw KeyboardSettings.Failure(code:-32001,message:"Stale keyboard settings")}
    editor.apply()
    try require(editor.low.stringValue=="unfinished mapping" && editor.status.stringValue=="Stale keyboard settings",
      "Refused keyboard settings retain raw text and the captured revision until explicit reload")
    editor.load();try require(editor.low.stringValue=="ZSXDCVGBHNJM","Explicit keyboard Reload discards only the preference draft")
  }
}
