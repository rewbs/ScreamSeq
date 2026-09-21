import AppKit
extension InterfaceTests {
  static func workspaceChecks() throws {
    let draftName=NSTextField(string:"Saved"),draftVolume=NSTextField(string:"64")
    let draft=AssetFieldDraft(["name":draftName,"volume":draftVolume]);draft.begin(index:1)()
    draftName.stringValue="Unsaved title";let restore=draft.begin(index:1);draftName.stringValue="Saved";draftVolume.stringValue="32";restore()
    try require(draftName.stringValue=="Unsaved title" && draftVolume.stringValue=="32" && draft.hasDraft,"Background refresh preserves pending fields while following unrelated saved changes")
    draft.accept(["volume":32].keys);try require(draft.hasDraft,"Applying another field does not clear a title draft")
    draft.accept(["name":"Unsaved title"].keys);try require(!draft.hasDraft,"Successful field edits release automatic following")
    draftName.stringValue="Another draft";let switchAsset=draft.begin(index:2);draftName.stringValue="Second";switchAsset();try require(!draft.hasDraft && draftName.stringValue=="Second","Explicit asset changes use the new asset's values")
    let pattern=NSTextField(string:"pattern"),notes=NSTextField(string:"notes"),automation=NSTextField(string:"automation")
    let workspace=DockWorkspace(patternView:pattern);workspace.frame=NSRect(x:0,y:0,width:2200,height:1100)
    let a=WorkspacePanel(id:"notes",title:"Notes",view:notes),b=WorkspacePanel(id:"automation",title:"Automation",view:automation)
    workspace.register(a,location:"right");workspace.register(b,location:"secondary")
    workspace.layoutSubtreeIfNeeded()
    try require(workspace.right.buttons.first?.title.contains("⌃⌥1")==true,"Inspector tabs expose a direct keyboard shortcut")
    let plugin=WorkspacePanel(id:"plugins",title:"Plugin controls",view:NSView())
    workspace.register(plugin,location:"right");workspace.right.choose("plugins")
    try require(workspace.right.selected=="plugins" && workspace.right.host.subviews.first===plugin,"Tabs select retained inspectors directly")
    workspace.show("notes")
    a.pinned=true
    workspace.preset("Pattern focus")
    try require(workspace.focusLayout && workspace.visibleIDs.isEmpty && workspace.pattern.superview != nil,"Pattern focus removes docked views without destroying their editor instances")
    workspace.show("notes")
    try require(!workspace.focusLayout && workspace.visibleIDs.contains("notes") && a.pinned && a.content.subviews.contains(notes),"Opening a panel restores docks and preserves its independent pin and editor")
    workspace.place("notes",at:"bottom");try require(workspace.locations["notes"]=="bottom" && a.pinned,"Docking preserves panel identity and context")
    let saved=workspace.state
    workspace.place("notes",at:"hide");a.pinned=false
    workspace.restore(saved)
    try require(workspace.locations["notes"]=="bottom" && a.pinned && workspace.panels["notes"] === a,"Saved layouts restore targets by stable panel identity")
    let before=notes.superview;workspace.show("notes");workspace.show("notes")
    try require(notes.superview === before,"Repeated focus does not rebuild editor controls")
    let compact=PatternAutomationEditor(frame:NSRect(x:0,y:0,width:1040,height:365));compact.configureDocked();compact.layoutSubtreeIfNeeded()
    try require(compact.canvas.bounds.height>=150,"Compact automation retains an editable envelope")
    let shortcuts=WorkspaceSequences();var ran="",hint="";shortcuts.onRun={ran=$0};shortcuts.onHint={hint=$0}
    shortcuts.bindings=["mixer":[WorkspaceStroke("ctrl+g")!,WorkspaceStroke("m")!],"graph":[WorkspaceStroke("ctrl+g")!,WorkspaceStroke("g")!]]
    func event(_ key:String,_ flags:NSEvent.ModifierFlags=[],code:UInt16=5)->NSEvent{NSEvent.keyEvent(with:.keyDown,location:.zero,modifierFlags:flags,timestamp:0,windowNumber:0,context:nil,characters:key,charactersIgnoringModifiers:key,isARepeat:false,keyCode:code)!}
    try require(shortcuts.handle(event("g",.control)) && !hint.isEmpty && ran.isEmpty,"Modified prefix waits for a discoverable continuation")
    try require(shortcuts.handle(event("m")) && ran=="mixer" && shortcuts.prefix.isEmpty,"Multi-key command executes exactly its completed binding")
    try require(!shortcuts.handle(event("z")),"Ordinary musical typing remains available outside a command prefix")
    _=shortcuts.handle(event("g",.control));_=shortcuts.handle(event("z"));try require(shortcuts.prefix.isEmpty && ran=="mixer","Unknown continuations clear the prefix without triggering an unrelated command")
    try require(shortcuts.conflict([WorkspaceStroke("ctrl+g")!],except:"other") && !shortcuts.conflict([WorkspaceStroke("ctrl+g")!,WorkspaceStroke("r")!],except:"other"),"Shortcut validation rejects ambiguous prefixes but permits distinct continuations")
    let palette=WorkspaceCommandPalette();let one=NSMenuItem(title:"One",action:#selector(NSObject.description),keyEquivalent:""),two=NSMenuItem(title:"Two",action:#selector(NSObject.description),keyEquivalent:"x");two.keyEquivalentModifierMask = .command
    palette.entries=[.init(item:one,path:"One"),.init(item:two,path:"Two")];let command=palette.entries[0].id
    try require(palette.setShortcut(command,keys:["cmd+x","m"],persist:false) != nil && palette.setShortcut(command,keys:["g","m"],persist:false) != nil,"Existing menu commands and plain note-entry prefixes are protected")
    try require(palette.setShortcut(command,keys:["ctrl+g","m"],persist:false)==nil && palette.shortcutCommands()[0]["keys"] as? [String]==["ctrl+g","m"],"Palette exposes configured sequences without changing the song")
    print("PASS connected workspace: retained panels/pins, focus layout, placement, saved layout, compact automation")
  }
}
