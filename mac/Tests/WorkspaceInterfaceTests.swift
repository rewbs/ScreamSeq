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
    try require(workspace.right.buttons.first?.title.contains("⌃⌥1")==false && workspace.right.buttons.first?.toolTip?.contains("⌃⌥1")==true,"Inspector shortcuts are discoverable in tooltips without cluttering tabs")
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
    workspace.preset("Compose"); workspace.layoutSubtreeIfNeeded()
    workspace.show("automation", focus:true); workspace.layoutSubtreeIfNeeded()
    try require(workspace.visibleIDs.contains("automation") && workspace.lower.frame.height > 150, "A bridge from Compose must reveal an actual usable lower panel, not a zero-height tab")
    workspace.preset("Compose");workspace.layoutSubtreeIfNeeded()
    workspace.place("automation",at:"bottom");workspace.layoutSubtreeIfNeeded()
    try require(!workspace.bottom.isHidden && workspace.visibleIDs.contains("automation") && workspace.lower.frame.height>150,"Dock below reveals a usable hidden destination instead of a selected zero-height panel")
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
    try require(palette.entries[0].id != palette.entries[1].id,"AppKit's automatic selector identifiers must not merge different legacy commands")
    try require(palette.setShortcut(command,keys:["cmd+x","m"],persist:false) != nil && palette.setShortcut(command,keys:["g","m"],persist:false) != nil,"Existing menu commands and plain note-entry prefixes are protected")
    try require(palette.setShortcut(command,keys:["ctrl+g","m"],persist:false)==nil && palette.shortcutCommands()[0]["keys"] as? [String]==["ctrl+g","m"],"Palette exposes configured sequences without changing the song")
    let contextual=WorkspaceCommandPalette()
    contextual.additionalMenus = {
      let menu=NSMenu(title:"Sample panel")
      menu.addItem(ContextAction("Select All",key:"a",modifiers:.command){ran="wrong panel"})
      return [menu]
    }
    contextual.collect()
    try require(!contextual.handleAdditionalShortcut(event("a",.command,code:0)), "Inferred context-menu shortcuts must not steal Select All from a text field")
    let unavailable=GraphCommand.parent.item("Parent graph",reason:"Already at the song graph"){ran="unavailable action"}
    try require(!contextual.isAvailable(unavailable,from:nil),"Palette must honor a contextual action's disabled state even though its invoke selector exists")
    contextual.table.dataSource=contextual
    contextual.filtered=[.init(item:unavailable,path:"Graph / Parent graph")];contextual.table.reloadData();contextual.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    contextual.run()
    try require(ran != "unavailable action" && contextual.status.stringValue=="Already at the song graph","Unavailable palette actions explain their precise reason and cannot execute")
    let graphCatalog=WorkspaceCommandPalette()
    graphCatalog.additionalMenus={
      let menu=NSMenu(title:"Graph")
      menu.addItem(GraphCommand.bypass.item("Toggle plugin bypass"){})
      menu.addItem(GraphCommand.bypass.item("Bypass selected plugin"){})
      return [menu]
    }
    graphCatalog.collect()
    try require(graphCatalog.entries.filter{$0.id==GraphCommand.bypass.id}.count==1,"One graph command stays one configurable entry when exposed by multiple menus")
    try assetApplyChecks()
    try layoutRestoreChecks()
    print("PASS connected workspace: retained panels/pins, focus layout, placement, saved layout, compact automation, changed-field asset apply, restored dividers")
  }
  static func assetApplyChecks() throws {
    let sample=SampleEditor(frame:NSRect(x:0,y:0,width:729,height:1400))
    var sent=[[String:Any]](),messages=[String]()
    sample.onSettings={sent.append($0)};sample.onMessage={messages.append($0)}
    sample.update(["name":"Kick","frames":1000,"rate":8363,"volume":64,"pan":128],samples:[["index":1,"name":"Kick"]],revision:"r1")
    sample.apply()
    try require(sent.isEmpty && messages.count==1,"Applying untouched sample settings sends nothing and says so")
    sample.name.stringValue="Kick 2";sample.apply()
    try require(sent.count==1 && Set(sent[0].keys)==["name"] && sent[0]["name"] as? String=="Kick 2","Renaming a sample sends only its name: no pan override, no retune")
    sample.settingsDraft.accept(sent[0].keys)
    sample.rate.stringValue="44.1k";sample.apply()
    try require(sent.count==1 && messages.last?.contains("44.1k")==true,"A rate that is not a whole number is reported instead of being read as 44")
    sample.rate.stringValue=" 44100 ";sample.pan.stringValue="64";sample.apply()
    try require(sent.count==2 && Set(sent[1].keys)==["rate","pan"] && sent[1]["rate"] as? Int==44100 && sent[1]["pan"] as? Int==64,"Changed numeric sample fields are sent exactly")
    let model=PatternModel(["format":"IT","instruments":[["index":1,"name":"Lead"]],"samples":[["index":1,"name":"Kick"]]])
    let instrument=InstrumentEditor(frame:.zero);var applied=[[String:Any]](),notes=[String]()
    instrument.onApply={applied.append($0)};instrument.onMessage={notes.append($0)}
    let points=[[0,64],[10,32],[20,0]]
    let envelope:[String:Any]=["points":points,"enabled":true,"sustain":true,"sustainPoint":1,"sustainEnd":2,"loop":false,"loopStart":0,"loopEnd":0]
    instrument.update(["name":"Lead","volume":64,"pan":128,"fadeout":256,"nna":0,"dct":0,"dna":0,"envelopes":[envelope,[:],[:]]],model:model)
    instrument.apply()
    try require(applied.isEmpty && notes.count==1,"Applying untouched instrument settings sends nothing and says so")
    instrument.name.stringValue="Lead 2";instrument.apply()
    try require(applied.count==1 && Set(applied[0].keys)==["name","envelope"],"Renaming an instrument sends no pan, volume, envelope flags or points")
    instrument.settingsDraft.accept(applied[0].keys)
    instrument.sustainPoint.stringValue="0";instrument.nna.selectItem(at:2);instrument.apply()
    try require(applied.count==2 && Set(applied[1].keys)==["sustainPoint","sustainEnd","nna","envelope"] && applied[1]["sustainEnd"] as? Int==2 && applied[1]["nna"] as? Int==2,
      "A moved sustain start keeps the displayed end; other settings stay untouched")
    instrument.settingsDraft.accept(applied[1].keys)
    instrument.fade.stringValue="1e3";instrument.apply()
    try require(applied.count==2 && notes.last?.contains("1e3")==true,"Instrument numbers that are not whole are reported, not truncated")
    instrument.fade.stringValue="512";instrument.envelope.points=[[0,64],[10,48],[20,0]];instrument.apply()
    try require(applied.count==3 && Set(applied[2].keys)==["fadeout","points","envelope"],"Envelope nodes that did not reach the song are sent with the next apply")
  }
  static func layoutRestoreChecks() throws {
    let workspace=DockWorkspace(patternView:NSView())
    let host=NSWindow(contentRect:NSRect(x:0,y:0,width:2000,height:1000),styleMask:[.titled,.resizable],backing:.buffered,defer:true)
    host.isReleasedWhenClosed=false;workspace.fill(host.contentView!);host.contentView!.layoutSubtreeIfNeeded()
    for (id,place) in [("notes","right"),("graph","bottom"),("automation","secondary")] { workspace.register(WorkspacePanel(id:id,title:id,view:NSView()),location:place) }
    workspace.layoutSubtreeIfNeeded()
    func settle(){RunLoop.current.run(until:Date().addingTimeInterval(0.05));workspace.layoutSubtreeIfNeeded()}
    func fractions()->[Double]{let state=workspace.state;return ["vertical","upper","lower"].map{(state[$0] as? NSNumber)?.doubleValue ?? -1}}
    workspace.preset("Compose");settle()
    let defaults=fractions()
    try require(abs(defaults[0]-0.4)>0.05 && abs(defaults[2]-0.3)>0.05,"The preset's own deferred dividers differ from the layout saved below")
    var saved=workspace.state;saved["vertical"]=0.4;saved["upper"]=0.5;saved["lower"]=0.3
    // Launch order: the preset queues its defaults, then the saved layout is restored.
    workspace.preset("Compose");workspace.restore(saved);settle()
    let restored=fractions()
    try require(abs(restored[0]-0.4)<0.02 && abs(restored[1]-0.5)<0.02 && abs(restored[2]-0.3)<0.02,"A restored layout keeps its dividers after the preset's deferred defaults")
  }
}
