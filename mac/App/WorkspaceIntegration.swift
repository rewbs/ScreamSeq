import AppKit

extension AppController {
  func installWorkspace() {
    let dock = DockWorkspace(patternView:patternGraphHost); workspace = dock; dock.fill(editorHost)
    func add(_ id:String,_ title:String,_ view:NSView,_ location:String,_ height:CGFloat? = nil) {
      let panel=WorkspacePanel(id:id,title:title,view:view,height:height)
      panel.onFollow = {[weak self] in self?.followWorkspacePanel(id,force:true)}
      panel.onReturn = {[weak self] in self?.returnToPanel(id)}
      dock.register(panel,location:location)
    }
    add("notes","Note inspector",workspaceNotes,"right",570)
    add("samples","Sample & loops",sampleEditor,"right",620)
    add("instruments","Instrument & envelopes",instrumentEditor,"right",560)
    add("plugins","Plugin controls",pluginEditor,"right",600)
    add("mixer","Mixer",mixerEditor,"bottom",340)
    add("graph","Audio & modulation graph",signalGraphEditor,"bottom")
    add("graphPlugins","Graph effect browser",graphPluginBrowser,"right",660)
    add("graphCommands","Pattern graph commands",graphCommandsEditor,"right",450)
    graphCommandsEditor.onContext = {[weak self] in guard let self else{return (PatternModel([:]),0,0)};return (self.model,self.patternView.cursorRow,self.patternView.cursorChannel)}
    graphCommandsEditor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    graphCommandsEditor.onGraph = {[weak self] id in self?.signalGraphEditor.graphID=id;self?.showSignalGraph()}
    signalGraphEditor.onPatternCommands = {[weak self] target in self?.openGraphCommand(target:target)}
    patternGraphHost.lanes.onEdit = {[weak self] target,column,row in self?.openGraphCommand(target:target,column:column,row:row)}
    patternGraphHost.lanes.onClear = {[weak self] target,column,row in self?.clearGraphCommand(target:target,column:column,row:row)}
    signalGraphEditor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    signalGraphEditor.onPlugin = {[weak self] id in self?.openWorkspacePlugin(id)}
    signalGraphEditor.onBus = {[weak self] id in guard let self else{return};self.showMixer();self.mixerEditor.selectBus(id)}
    signalGraphEditor.onChoosePlugin = {[weak self] choose in guard let self else{return};self.graphPluginBrowser.kind.selectItem(at:1);self.graphPluginBrowser.onChoose = {[weak self] descriptor in guard descriptor["isInstrument"] as? Bool != true else{return};choose(descriptor);self?.workspace?.show("graph")};self.workspace?.show("graphPlugins");self.graphPluginBrowser.load()}
    graphPluginBrowser.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    workspaceAutomation.configureDocked()
    add("automation","Pattern automation",workspaceAutomation,"secondary",340)
    workspaceNotes.onContext = {[weak self] in guard let self else{return(PatternModel([:]),0,0,1)};return(self.model,self.patternView.cursorRow,self.patternView.cursorChannel,self.patternView.instrument)}
    workspaceNotes.onRequest = {[weak self] method,params,reply in
      guard let self else{return}
      if method=="pattern.notes.get",params["pattern"] as? Int==self.model.pattern {
        let all: [PreciseNote] = self.model.preciseNotes.values.flatMap { $0 }
        let sorted = all.sorted { a,b in a.position == b.position ? a.channel < b.channel : a.position < b.position }
        let events: [[String:Int]] = sorted.map { $0.dictionary }
        reply(["result":["revision":self.model.revisionToken,"data":["pattern":self.model.pattern,"events":events,"rowsPerBeat":self.model.rowsPerBeat,"effects":self.model.preciseNoteEffects]]])
      } else {self.handleAutomation(method,params:params,reply:reply)}
    }
    workspaceAutomation.onContext = {[weak self] in self?.workspaceAutomationModel ?? self?.model ?? PatternModel([:])}
    workspaceAutomation.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    configureWorkspaceMixer()
    installContextMenus()
    dock.onSelection = {[weak self] id in
      guard let self else{return}
      if self.workspaceReturnPoints[id] == nil {self.workspaceReturnPoints[id]=self.patternView.navigation}
      if id=="samples" {self.editorMode=1} else if id=="instruments" {self.editorMode=2} else if id=="plugins" {self.editorMode=3}
      self.followWorkspacePanel(id,force:false)
    }
    workspaceInputMonitor=NSEvent.addLocalMonitorForEvents(matching:[.keyDown,.keyUp]){[weak self] event in
      guard let self else{return event}
      if self.commandPalette.window?.isKeyWindow != true && self.commandPalette.sequences.handle(event){return nil}
      if self.handlePlaybackKey(event) || self.handleInspectorNote(event) { return nil }
      guard self.liveKeyboard,NSApp.isActive,self.commandPalette.window?.isVisible != true,
        NSApp.modalWindow == nil,self.window.attachedSheet == nil else{return event}
      if let text=self.focusedView(event) as? NSTextView,text.isEditable,event.type == .keyDown{return event}
      if self.focusedView(event) is NSTextField,event.type == .keyDown{return event}
      if event.type == .keyUp,let note=self.workspaceHeldKeys.removeValue(forKey:event.keyCode){self.audition(note:note,on:false);return nil}
      guard event.modifierFlags.intersection([.command,.control,.option]).isEmpty,
        let key=event.charactersIgnoringModifiers?.lowercased(),let offset=KeyboardSettings.note(for:key) else{return event}
      if event.type == .keyDown,!event.isARepeat,self.workspaceHeldKeys[event.keyCode]==nil {let note=min(120,self.patternView.octave*12+offset+1);self.workspaceHeldKeys[event.keyCode]=note;self.audition(note:note,on:true)}
      return nil
    }
    NotificationCenter.default.addObserver(forName:NSApplication.didResignActiveNotification,object:nil,queue:.main){[weak self] _ in self?.releaseWorkspaceKeys();self?.releaseInspectorKeys();self?.commandPalette.sequences.cancel()}
    commandPalette.sequences.onHint = {[weak self] hint in self?.window.subtitle=hint}
    commandPalette.collect()
    for tabs in [dock.right,dock.bottom,dock.secondary] {
      tabs.shortcutLabel = {[weak self] id in
        guard let self,let entry=self.commandPalette.entries.first(where:{$0.item.representedObject as? String==id}) else{return ""}
        if let sequence=self.commandPalette.sequences.bindings[entry.id]{return sequence.map(\.encoded).joined(separator:" → ")}
        let item=entry.item,mask=item.keyEquivalentModifierMask
        guard !item.keyEquivalent.isEmpty else{return ""}
        return (mask.contains(.control) ? "⌃" : "")+(mask.contains(.option) ? "⌥" : "")+(mask.contains(.shift) ? "⇧" : "")+(mask.contains(.command) ? "⌘" : "")+item.keyEquivalent.uppercased()
      };tabs.reload()
    }
    commandPalette.onShortcutsChanged = {[weak dock] in guard let dock else{return};for tabs in [dock.right,dock.bottom,dock.secondary]{tabs.reload()}}
    DispatchQueue.main.async {[weak self] in
      guard let self else{return}; self.workspace?.preset("Compose")
      if !self.automationTest && !self.inspectionTest,let saved=UserDefaults.standard.dictionary(forKey:"workspaceLastLayout"){self.workspace?.restore(saved)}
    }
  }
  func configureWorkspaceMixer(){
    mixerEditor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    mixerEditor.onSidechains = {[weak self] in self?.showSidechains()}
    mixerEditor.onConfigurePlugin = {[weak self] slot in self?.showPluginPorts(slot)}
    mixerEditor.onPluginControls = {[weak self] id in self?.inspectWorkspacePlugin(id)}
    mixerEditor.onOpenPlugin = {[weak self] id in self?.openWorkspacePlugin(id)}
  }
  func openWorkspacePlugin(_ id:String) {
    guard !busy,let slot=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==id}) else{return}
    if model.nativePlugins[slot]["format"] as? String=="Built-in" { inspectWorkspacePlugin(id) }
    else { do { try session.showPluginEditor(slot) } catch { show(error) } }
  }
  func inspectWorkspacePlugin(_ id:String){
    guard let slot=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==id}) else{return}
    pluginEditor.selected=slot;showEditor(3)
    if workspace?.panels["automation"]?.pinned != true && !workspaceAutomation.hasDraft {
      workspaceAutomationModel=model;workspaceAutomation.pluginIndex=slot;workspaceAutomation.load()
    }
  }
  func followWorkspacePanel(_ id:String,force:Bool){
    guard !busy,let dock=workspace,let panel=dock.panels[id],force || workspaceContextTokens[id] == nil || (!panel.pinned && !dock.containsFocus(id)) else{return}
    let position=patternView.navigation
    if workspaceReturnPoints[id]==nil {workspaceReturnPoints[id]=position}
    let cursor="P\(model.pattern) · R\(position.row) · CH\(position.channel+1)"
    switch id {
    case "notes":
      if workspaceNotes.hasDraft && !force {panel.target.stringValue="Draft held · "+panel.target.stringValue.replacingOccurrences(of:"Draft held · ",with:"");return}
      let token="\(position.pattern):\(position.row):\(position.channel):\(model.revisionToken)"
      guard !workspaceNotes.pending,force || workspaceContextTokens[id] != token else{return}
      workspaceContextTokens[id]=token;panel.target.stringValue=cursor;workspaceNotes.capture()
    case "samples","instruments":
      if !force && (id=="samples" ? sampleEditor.hasDraft : instrumentEditor.hasDraft) {panel.target.stringValue="Draft held · "+panel.target.stringValue.replacingOccurrences(of:"Draft held · ",with:"");return}
      let cell=model.cell(position.row,position.channel),instrument=cell[1]>0 ? Int(cell[1]) : patternView.instrument
      let sample=instrument
      let token="asset:\(position.pattern):\(position.row):\(position.channel)"
      guard force || workspaceContextTokens[id] != token else{return};workspaceContextTokens[id]=token
      if id=="samples" {
        if !model.instruments.isEmpty {
          handleAutomation("instrument.get",params:["instrument":instrument]){[weak self] reply in
            guard let self,self.workspaceContextTokens[id]==token,let data=(reply["result"] as? [String:Any])?["data"] as? [String:Any],let mapping=data["mapping"] as? [Int] else{return}
            let key=max(0,min(127,Int(cell[0])-1));let mapped=mapping.indices.contains(key) ? mapping[key] : 0
            if mapped>0 {self.sampleEditor.index=mapped;self.workspace?.panels[id]?.target.stringValue="Sample \(mapped)";self.refreshWorkspaceAsset(id)}
          };return
        }
        sampleEditor.index=sample
      } else {instrumentEditor.index=instrument}
      panel.target.stringValue="\(id=="samples" ? "Sample" : "Instrument") \(id=="samples" ? sample : instrument)";refreshWorkspaceAsset(id)
    case "automation":
      guard !workspaceAutomation.loading,!workspaceAutomation.hasDraft || force else{panel.target.stringValue="Draft held";return}
      let token="\(model.pattern):\(pluginEditor.selected):\(model.revisionToken)"
      guard force || workspaceContextTokens[id] != token else{return};workspaceContextTokens[id]=token
      workspaceAutomationModel=model;workspaceAutomation.pluginIndex=max(0,pluginEditor.selected);panel.target.stringValue="Pattern \(model.pattern)";workspaceAutomation.load()
    case "plugins":
      let token="\(pluginEditor.selected):\(model.revisionToken)";guard force || workspaceContextTokens[id] != token else{return};workspaceContextTokens[id]=token;refreshPlugins()
    case "graphCommands":
      guard !graphCommandsEditor.pending,!graphCommandsEditor.hasDraft || force else{return}
      let token="\(model.pattern):\(position.row):\(position.channel):\(model.revisionToken)"
      if force || workspaceContextTokens[id] != token {workspaceContextTokens[id]=token;graphCommandsEditor.preferredTarget=nil;graphCommandsEditor.preferredRow=position.row;graphCommandsEditor.capture()};panel.target.stringValue=cursor
    case "graph":
      guard force || !signalGraphEditor.hasDraft else{panel.target.stringValue="Draft held · shared graph";return}
      if force || workspaceContextTokens[id] != model.revisionToken {workspaceContextTokens[id]=model.revisionToken;signalGraphEditor.load()}
      panel.target.stringValue="Shared song graph"
    case "mixer":if force || workspaceContextTokens[id]==nil {workspaceContextTokens[id]="loaded";mixerEditor.load()}
    default:break
    }
  }
  func updateWorkspaceContext(){
    guard let workspace else{return};workspace.refreshFocus()
    for id in workspace.visibleIDs {followWorkspacePanel(id,force:false)}
  }
  func returnToPanel(_ id:String){
    guard let target=workspaceReturnPoints[id],model.patterns.contains(where:{$0["index"] as? Int==target.pattern}) else{return}
    if target.pattern != model.pattern {model.pattern=target.pattern;refreshPattern()}
    var destination=target;destination.following=false;patternView.navigate(destination,clearSelection:true);window.makeFirstResponder(patternView)
  }
  func openGraphCommand(target:String?=nil,column:Int=0,row:Int?=nil){
    workspaceContextTokens["graphCommands"]="\(model.pattern):\(patternView.cursorRow):\(patternView.cursorChannel):\(model.revisionToken)";workspace?.show("graphCommands",focus:true)
    graphCommandsEditor.preferredTarget=target;graphCommandsEditor.preferredColumn=column;graphCommandsEditor.preferredRow=row ?? patternView.cursorRow;graphCommandsEditor.capture()
  }
  func clearGraphCommand(target:String,column:Int,row:Int){
    let pattern=model.pattern,revision=model.revisionToken,patternID=model.patterns.first{$0["index"] as? Int==model.pattern}?["id"] as? String ?? ""
    handleAutomation("graph.get",params:["includeState":false]){[weak self] response in guard let self else{return};guard let result=response["result"] as? [String:Any],result["revision"] as? String==revision,let data=result["data"] as? [String:Any]else{self.statusLabel.stringValue="Song changed; select the command again";return}
      let commands=(data["commands"] as? [[String:Any]] ?? []).filter{$0["pattern"] as? String==patternID}.filter{!($0["target"] as? String==target && $0["column"] as? Int==column && ($0["position"] as? Int ?? 0)/65536==row)}.map{event -> [String:Any] in var c=event;c.removeValue(forKey:"pattern");return c}
      self.handleAutomation("graph.commands.set",params:["pattern":pattern,"commands":commands,"expectedRevision":revision]){reply in if let error=reply["error"] as? [String:Any]{self.statusLabel.stringValue=error["message"] as? String ?? "Could not remove graph command"}}
    }
  }
  @objc func showGraphCommands(){openGraphCommand()}
  @objc func showSignalGraph(){workspace?.show("graph");followWorkspacePanel("graph",force:true)}
  @objc func showCommandPalette(){commandPalette.show()}
  @objc func focusInspector(_ sender:NSMenuItem) { if let id=sender.representedObject as? String { workspace?.show(id,focus:true) } }
  @objc func focusPattern(){window.makeKeyAndOrderFront(nil);window.makeFirstResponder(patternView);editorMode=0}
  @objc func focusNextPanel(){
    guard let workspace else{return};let ids=workspace.visibleIDs
    let index=workspace.focusedPanel().flatMap{ids.firstIndex(of:$0.id)} ?? -1
    if index+1<ids.count {workspace.show(ids[index+1],focus:true)} else {focusPattern()}
  }
  @objc func pinFocusedPanel(){if let panel=workspace?.focusedPanel(){panel.pinned.toggle()}}
  @objc func followFocusedPanel(){if let panel=workspace?.focusedPanel(){panel.pinned=false;panel.onFollow?()}}
  @objc func returnFocusedPanel(){workspace?.focusedPanel()?.onReturn?()}
  @objc func floatFocusedPanel(){if let panel=workspace?.focusedPanel(){workspace?.place(panel.id,at:"float")}}
  @objc func dockFocusedPanel(){if let panel=workspace?.focusedPanel(){workspace?.place(panel.id,at:"right")}}
  @objc func toggleFollow(){patternView.isFollowing.toggle()}
  @objc func composeLayout(){workspace?.preset("Compose")}
  @objc func soundLayout(){workspace?.preset("Sound design")}
  @objc func patternLayout(){workspace?.preset("Pattern focus");focusPattern()}
  @objc func saveWorkspaceLayout(){if let state=workspace?.state{UserDefaults.standard.set(state,forKey:"workspaceSavedLayout");statusLabel.stringValue="Custom workspace layout saved"}}
  @objc func restoreWorkspaceLayout(){if let state=UserDefaults.standard.dictionary(forKey:"workspaceSavedLayout"){workspace?.restore(state)}}
  @objc func toggleLiveKeyboard(){liveKeyboard.toggle();if !liveKeyboard{releaseWorkspaceKeys()};liveKeyboardItem?.state=liveKeyboard ? .on : .off;liveKeyboardButton?.title=liveKeyboard ? "LIVE KEYS ON" : "Live keys";liveKeyboardButton?.contentTintColor=liveKeyboard ? Theme.gold : Theme.muted;statusLabel.stringValue=liveKeyboard ? "Live keys: musical typing stays active across panels · ⌘⌥L releases it" : "Keyboard follows panel focus"}
  func releaseWorkspaceKeys(){for note in workspaceHeldKeys.values{audition(note:note,on:false)};workspaceHeldKeys.removeAll()}
  func handleWorkspaceAutomation(_ method:String,params:[String:Any],reply:@escaping AutomationServer.Reply)->Bool{
    guard method.hasPrefix("workspace.") else{return false}
    func fail(_ message:String){reply(AutomationServer.error(-32602,message))}
    if method=="workspace.ruler" {
      guard Set(params.keys)==["mode"],let name=params["mode"] as? String,let mode=PatternPositionMode(rawValue:name) else{reply(AutomationServer.error(-32602,"Choose rows, beats, patternTime or songTime"));return true}
      patternView.positionMode=mode
      reply(["result":["revision":session.automationRevision,"data":["mode":mode.rawValue],"changed":false,"playbackStopped":false]]);return true
    }
    if method=="workspace.commands.get" || method=="workspace.shortcut.set" {
    if method=="workspace.commands.get"{guard params.isEmpty else{fail("workspace.commands.get has no parameters");return true}}
      else {guard Set(params.keys)==["command","keys"],let id=params["command"] as? String,let keys=params["keys"] as? [String] else{fail("Supply a command ID and key sequence");return true};if let error=commandPalette.setShortcut(id,keys:keys){fail(error);return true}}
      reply(["result":["revision":session.automationRevision,"data":["commands":commandPalette.shortcutCommands()],"changed":false,"playbackStopped":false]]);return true
    }
    if method=="workspace.get" {guard params.isEmpty else{fail("workspace.get has no parameters");return true}}
    else if method=="workspace.panel" {
      guard Set(params.keys).isSubset(of:["panel","placement","pinned","focus","follow","return"]),let id=params["panel"] as? String,let panel=workspace?.panels[id] else{fail("Choose a known workspace panel");return true}
      if let raw=params["placement"] {guard let place=raw as? String,["right","bottom","secondary","float","hide"].contains(place) else{fail("Invalid panel placement");return true}}
      for key in ["pinned","focus","follow","return"] where params[key] != nil {guard let number=params[key] as? NSNumber,CFGetTypeID(number)==CFBooleanGetTypeID() else{fail("\(key) must be boolean");return true}}
      if let place=params["placement"] as? String{workspace?.place(id,at:place,select:false)}
      if let pin=params["pinned"] as? Bool{panel.pinned=pin};if params["follow"] as? Bool==true{panel.pinned=false;panel.onFollow?()};if params["return"] as? Bool==true{panel.onReturn?()};if params["focus"] as? Bool==true{workspace?.show(id,focus:true)}
    } else if method=="workspace.layout" {
      guard Set(params.keys)==["name"],let name=params["name"] as? String,["Compose","Sound design","Pattern focus","Save custom","Restore custom"].contains(name) else{fail("Choose a workspace layout");return true}
      if name=="Save custom"{saveWorkspaceLayout()}else if name=="Restore custom"{restoreWorkspaceLayout()}else{workspace?.preset(name)}
    } else {reply(AutomationServer.error(-32601,"Unknown workspace method"));return true}
    var state=workspace?.state ?? [:];state["panels"]=workspace?.panels.keys.sorted() ?? [];state["visible"]=workspace?.visibleIDs ?? [];state["focus"]=workspace?.focusedPanel()?.id ?? "pattern";state["liveKeyboard"]=liveKeyboard;state["positionMode"]=patternView.positionMode.rawValue
    state["targets"]=workspace?.panels.mapValues{$0.target.stringValue};state["screens"]=NSScreen.screens.map{["name":$0.localizedName,"width":$0.visibleFrame.width,"height":$0.visibleFrame.height,"scale":$0.backingScaleFactor]}
    reply(["result":["revision":session.automationRevision,"data":state,"changed":false,"playbackStopped":false]]);return true
  }
}
