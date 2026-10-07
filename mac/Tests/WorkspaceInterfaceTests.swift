import AppKit
extension InterfaceTests {
  static func workspaceChecks() throws {
    let buttonTrace=QualificationButtonDrawTrace(capacity:2)
    let tracedButton=ActionButton("Add hit"){},tracePanel=WorkspacePanel(id:"notes",title:"Notes",view:NSView())
    tracePanel.content.addSubview(tracedButton);tracedButton.frame=NSRect(x:4,y:6,width:80,height:24)
    buttonTrace.record(tracedButton,start:1,end:1.001);tracedButton.isEnabled=false
    buttonTrace.record(tracedButton,start:2,end:2.003);tracedButton.title=String(repeating:"x",count:300)
    buttonTrace.record(tracedButton,start:3,end:3.002)
    let draws=buttonTrace.snapshot()
    try require(draws.count==2 && buttonTrace.overwritten==1 && draws[0].sequence==1 && draws[0].title=="Add hit" && !draws[0].enabled && draws[0].context=="notes" && draws[0].frame==tracedButton.frame && draws[0].identity==draws[1].identity && draws[1].title.count==128,
      "Opt-in button diagnostics retain bounded ordered identity, title, state and panel attribution")
    buttonTrace.reset();try require(buttonTrace.snapshot().isEmpty && buttonTrace.overwritten==0,"Button timing resets at the measured workload boundary")
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
    try qualificationDisplayChecks()
    try workspaceWindowPersistenceChecks()
    try layoutRestoreChecks()
    print("PASS connected workspace: retained panels/pins, focus layout, placement, saved layout, compact automation, changed-field asset apply, restored dividers")
  }
  static func qualificationDisplayChecks() throws {
    let implicit=try QualificationDisplay.requestedID(["app"])
    let explicit=try QualificationDisplay.requestedID(["app","--ui-test-screen","3"])
    try require(implicit==nil,"Ordinary qualification keeps the existing display selection")
    try require(explicit==3,"Explicit qualification display IDs are parsed exactly")
    for args in [["app","--ui-test-screen"],["app","--ui-test-screen","0"],["app","--ui-test-screen","-1"],
      ["app","--ui-test-screen","not-a-display"],["app","--ui-test-screen","4294967296"],
      ["app","--ui-test-screen","1","--ui-test-screen","3"]] {
      do {_=try QualificationDisplay.requestedID(args);throw InterfaceFailure(message:"Invalid display option was accepted")}
      catch is QualificationDisplay.Failure {} // The failure must identify setup, not silently choose another display.
    }
    let visible=NSRect(x:-1920,y:30,width:1920,height:1170),size=NSSize(width:1360,height:872)
    let frame=try QualificationDisplay.centeredFrame(size:size,visibleFrame:visible)
    try require(frame.size==size && visible.contains(frame) && frame.midX==visible.midX && frame.midY==visible.midY,
      "Explicit display placement preserves workload size and centers correctly on a negative-origin monitor")
    do {_=try QualificationDisplay.centeredFrame(size:NSSize(width:2000,height:900),visibleFrame:visible);throw InterfaceFailure(message:"Oversized display workload was silently resized")}
    catch is QualificationDisplay.Failure {}
    do {_=try QualificationDisplay.select(3,screens:[],fallback:nil);throw InterfaceFailure(message:"Missing display silently fell back")}
    catch is QualificationDisplay.Failure {}
    if let screen=NSScreen.screens.first,let id=QualificationDisplay.id(screen) {
      let selected=try QualificationDisplay.select(id,screens:NSScreen.screens,fallback:nil)
      try require(selected === screen,"Qualification selects the requested connected display without changing its mode")
    }
    let args=["app","--ui-test-screen","3","song.screamseq"]
    try require(AppLaunchArguments.documentPath(args,exists:{_ in true})=="song.screamseq" && AppLaunchArguments.isOptionValue("3",arguments:args),
      "Qualification display IDs are never opened as document arguments or AppKit file events")
  }
  static func applicationEncodingTraceChecks() throws {
    try require(!QualificationApplicationEncodeTrace.isEnabled(arguments:["app"]) &&
      !QualificationApplicationEncodeTrace.isEnabled(arguments:["app","--ui-test"]) &&
      !QualificationApplicationEncodeTrace.isEnabled(arguments:["app","--ui-test-window-state-trace"]) &&
      QualificationApplicationEncodeTrace.isEnabled(arguments:["app","--ui-test-window-state-trace","--ui-test"]),
      "Application encoding instrumentation requires both explicit qualification flags")
    var forwards=0,clockReads=0
    QualificationApplicationEncodeTrace.measure(.sync,trace:nil,clock:{clockReads+=1;return 0}){forwards+=1}
    try require(forwards==1 && clockReads==0,"Disabled application tracing forwards once without consulting its clock")
    let trace=QualificationApplicationEncodeTrace(capacity:2)
    var order=[String](),now=10.0
    QualificationApplicationEncodeTrace.measure(.backgroundQueueSynchronous,trace:trace,clock:{clockReads+=1;return now}) {
      forwards+=1;order.append("outer start");now=11
      QualificationApplicationEncodeTrace.measure(.sync,trace:trace,clock:{clockReads+=1;return now}) {
        forwards+=1;order.append("inner");now=12
      }
      order.append("outer end");now=13
    }
    let nested=trace.snapshot()
    try require(forwards==3 && clockReads==2 && order==["outer start","inner","outer end"] &&
      nested.count==1 && nested[0].kind == .backgroundQueueSynchronous && nested[0].start==10 && nested[0].end==13,
      "Both overload paths forward once while a nested synchronous encode produces one complete outer span")
    for index in 0..<2 {
      now=20+Double(index)*10
      QualificationApplicationEncodeTrace.measure(.sync,trace:trace,clock:{now}){forwards+=1;now+=2}
    }
    let bounded=trace.snapshot()
    try require(forwards==5 && bounded.map(\.sequence)==[1,2] && trace.overwritten==1 &&
      bounded[0].start==20 && bounded[0].end==22 && bounded[1].start==30 && bounded[1].end==32,
      "Application encoding evidence retains a bounded chronological ring with an explicit dropped count")
    trace.reset()
    try require(trace.snapshot().isEmpty && trace.overwritten==0,"Measurement reset clears prior application encoding evidence")
    QualificationApplicationEncodeTrace.measure(.sync,trace:trace,clock:{now}){forwards+=1;trace.reset();now+=1}
    try require(trace.snapshot().isEmpty && trace.overwritten==0,
      "A reset during reentrant encoding cannot append a span from the preceding measurement")
    QualificationApplicationEncodeTrace.measure(.sync,trace:trace,clock:{now}){forwards+=1;now+=1}
    try require(forwards==7 && trace.snapshot().count==1 && trace.snapshot()[0].sequence==0 &&
      trace.snapshot()[0].start==33 && trace.snapshot()[0].end==34,
      "The nesting guard unwinds after reset and the next encoding forwards and records normally")
    // These checks exercise the observer around test closures, never invoke
    // AppKit's encoding hooks directly or manufacture a second NSApplication.
  }
  static func workspaceWindowPersistenceChecks() throws {
    try applicationEncodingTraceChecks()
    let name="ScreamSeq-Frame-Test-"+UUID().uuidString
    let style:NSWindow.StyleMask=[.titled,.closable,.resizable]
    let first=UIWorkTrace.window(contentRect:NSRect(x:80,y:90,width:620,height:480),styleMask:style,backing:.buffered,defer:false)
    first.isReleasedWhenClosed=false
    let second=UIWorkTrace.window(contentRect:NSRect(x:140,y:160,width:400,height:300),styleMask:style,backing:.buffered,defer:false)
    second.isReleasedWhenClosed=false
    let normal=NSWindow(contentRect:NSRect(x:140,y:160,width:400,height:300),styleMask:style,backing:.buffered,defer:false)
    normal.isReleasedWhenClosed=false
    defer{first.setFrameAutosaveName("");second.setFrameAutosaveName("");first.close();second.close();normal.close();NSWindow.removeFrame(usingName:name)}
    try require(first.isRestorable==normal.isRestorable && second.isRestorable==normal.isRestorable,
      "Workspace windows preserve AppKit's normal restoration eligibility")
    try require(first.setFrameAutosaveName(name),"Workspace frame autosave has an independent name")
    let saved=first.frame;first.saveFrame(usingName:name);first.setFrameAutosaveName("")
    // AppKit can remap a saved rectangle to the active display. Compare with
    // its normal restoration path, rather than assuming unchanged coordinates.
    let normalLoaded=normal.setFrameUsingName(name),registered=second.setFrameAutosaveName(name)
    let frameDetails="saved=\(saved), restored=\(second.frame), normal=\(normal.frame), registered=\(registered), normalLoaded=\(normalLoaded), name=\(second.frameAutosaveName), stored=\(UserDefaults.standard.string(forKey:"NSWindow Frame "+name) ?? "missing"), screens=\(NSScreen.screens.map{[$0.frame,$0.visibleFrame]}), restoredScreen=\(String(describing:second.screen?.frame))"
    try require(normalLoaded && registered && second.frame==normal.frame && second.frame.size==saved.size,
      "Workspace frame autosave matches normal AppKit frame restoration and preserves size: "+frameDetails)
    let windowTrace=QualificationWindowEncodeTrace(capacity:2)
    windowTrace.record(first,start:1,end:1.02);windowTrace.record(second,start:2,end:2.01);windowTrace.record(first,start:3,end:3.04)
    let encodes=windowTrace.snapshot()
    try require(encodes.count==2 && windowTrace.overwritten==1 && encodes[0].sequence==1 && encodes[0].windowNumber==second.windowNumber && encodes[1].windowNumber==first.windowNumber && encodes[1].restorable==first.isRestorable && encodes[1].start==3,
      "Window encoding diagnostics retain bounded ordered per-window identity and policy")
    windowTrace.reset();try require(windowTrace.snapshot().isEmpty && windowTrace.overwritten==0,"Window encoding trace resets at the measurement boundary")
    // Layout data is also explicitly serialized, independently of window coding.
    let original=DockWorkspace(patternView:NSView()),restored=DockWorkspace(patternView:NSView())
    for dock in [original,restored] {
      dock.frame=NSRect(x:0,y:0,width:1200,height:850)
      dock.register(WorkspacePanel(id:"persistence-notes",title:"Notes",view:NSView()),location:"right")
      dock.register(WorkspacePanel(id:"persistence-automation",title:"Automation",view:NSView()),location:"bottom")
    }
    first.contentView=original;second.contentView=restored
    original.panels["persistence-notes"]?.pinned=true
    original.place("persistence-automation",at:"secondary",select:false)
    original.setFocusLayout(true)
    let data=try PropertyListSerialization.data(fromPropertyList:original.state,format:.binary,options:0)
    let state=try PropertyListSerialization.propertyList(from:data,format:nil) as! [String:Any]
    restored.restore(state)
    try require(restored.locations==original.locations && restored.focusLayout && restored.panels["persistence-notes"]?.pinned==true,
      "Explicit workspace layout survives serialization and recreation without AppKit state archives")
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
