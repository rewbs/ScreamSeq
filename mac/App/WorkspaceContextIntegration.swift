import AppKit

extension AppController {
  var contextChoiceStamp: String { "\(model.revisionToken):\(patternView.navigation.token):\(selectedOrder):\(patternView.instrument):\(patternView.octave):\(patternView.step):\(sampleEditor.index)" }
  func installContextPanel() {
    contextPanel.onAction = { [weak self] in self?.performContextAction($0) }
    contextPanel.onChoice = { [weak self] in self?.chooseContext($0, id:$1, stamp:$2) }
    workspace?.onContextVisibility = { [weak self] visible in self?.presentContextPanel(visible) }
    presentContextPanel(workspace?.contextPanelVisible ?? true)
  }
  func presentContextPanel(_ visible: Bool) {
    if !visible, contextPanel.owns(NSApp.keyWindow?.firstResponder) { focusPattern() }
    contextPanel.isHidden = !visible; contextRule.isHidden = !visible
    contextPanelButton?.title = visible ? "Hide context" : "Show context"
    contextPanelButton?.setAccessibilityLabel(visible ? "Hide context panel" : "Show context panel")
    contextPanelButton?.setAccessibilityValue(visible ? "Shown" : "Hidden")
    contextPanelMenuItem?.state = visible ? .on : .off
    window?.contentView?.layoutSubtreeIfNeeded()
  }
  @objc func toggleContextPanel() {
    guard !busy, !sessionReading, !shuttingDown else {
      statusLabel.stringValue = "Wait for the current operation before changing the context panel."; return
    }
    // Native mouse tracking must finish before changing the canvas geometry.
    guard NSEvent.pressedMouseButtons == 0 || NSApp.currentEvent?.type == .leftMouseUp else {
      statusLabel.stringValue = "Finish the current mouse gesture before changing the context panel."; return
    }
    guard !signalGraphEditor.canvas.isEditing, !workspaceAutomation.canvas.isDragging,
      !signalGraphEditor.envelopeEditor.canvas.isDragging, sampleEditor.waveform.stroke.isEmpty,
      !sampleEditor.waveform.draggingLoop else {
      statusLabel.stringValue = "Finish the current mouse gesture before changing the context panel."; return
    }
    workspace?.setContextPanelVisible(!(workspace?.contextPanelVisible ?? true))
  }
  func refreshContextPanel() {
    guard !contextPanel.isTracking else { return }
    var editor = contextPanel.editor
    if !busy, !sessionReading, let host = NSApp.keyWindow, let responder = host.firstResponder as? NSView {
      let focused = (responder as? NSTextView)?.delegate as? NSView ?? responder
      if !contextPanel.owns(responder), focused !== contextPanelButton {
        if let panel = workspace?.panels.values.first(where: { $0.window === host && focused.isDescendant(of:$0) }) { editor = panel.id }
        else if host === window, focused === patternView || focused.isDescendant(of:patternGraphHost) { editor = "pattern" }
        else if host === songAutomationWindow?.window { editor = "songAutomation" }
      }
    }
    let plugin = contextPluginTarget(editor), available = !busy && !sessionReading && !shuttingDown
    typealias A = WorkspaceContextPanel.Action
    let actions: [A], title: String
    switch editor {
    case "graphPlugins":
      title = "GRAPH EFFECT BROWSER"
      actions = [A(id:"graph",title:"Return to graph"),A(id:"plugins",title:"Plugin rack panel"),A(id:"addPlugin",title:"Add plugins to rack…")]
    case "graph":
      if signalGraphEditor.graphID == nil {
        title = "SONG MIXING GRAPH"
        actions = [A(id:"pattern",title:"Return to Pattern"),A(id:"floatGraph",title:"Float graph"),A(id:"dockGraph",title:"Dock next to Pattern"),A(id:"mixer",title:"Mixer panel"),A(id:"plugins",title:"Plugin rack panel"),A(id:"connected",title:"Connected layout")]
      } else {
        title = "GRAPH RECIPE"
        actions = [A(id:"graphPlugin",title:"Plugin parameters…",enabled:signalGraphEditor.selectedNode?["kind"] as? String == "plugin"),A(id:"fitGraph",title:"Fit graph"),A(id:"graphActions",title:"Groups & modulation…"),A(id:"graphTrims",title:"Audio port trims…",enabled:!signalGraphEditor.trimControls.isHidden),A(id:"addPlugin",title:"Add plugins to rack…"),A(id:"songGraph",title:"Song mixing graph panel")]
      }
    case "mixer":
      title = "MIXER"
      actions = [A(id:"songGraph",title:"Song mixing graph panel"),A(id:"busEffect",title:"Add bus effect…",enabled:mixerEditor.selectedID != nil),A(id:"busInstrument",title:"Route instrument…",enabled:mixerEditor.selectedID != nil),A(id:"busGroup",title:"Add group"),A(id:"busReturn",title:"Add return"),A(id:"busDetails",title:"Bus details…")]
    case "plugins":
      title = "PLUGIN RACK"
      actions = [A(id:"addPlugin",title:"Add plugins…"),A(id:"plugins",title:"Parameter controls…",enabled:plugin != nil),A(id:"automate",title:"Automate parameter…",enabled:plugin?.1 != nil),A(id:"activity",title:"Parameter activity…",enabled:plugin != nil),A(id:"pluginInstruments",title:"Instrument assignments…",enabled:plugin != nil),A(id:"songGraph",title:"Song mixing graph panel")]
    case "samples":
      title = "SAMPLES"
      actions = [A(id:"samples",title:"Waveform & loops…"),A(id:"browseSamples",title:"Browse & preview…"),A(id:"importSample",title:"Import sample…"),A(id:"recordSample",title:"Record audio…"),A(id:"instruments",title:"Instrument mapping…")]
    case "instruments":
      title = "INSTRUMENT"
      actions = [A(id:"plugins",title:"Plugin rack"),A(id:"addPlugin",title:"Add plugins…"),A(id:"browseSamples",title:"Browse samples…"),A(id:"songGraph",title:"Song mixing graph panel"),A(id:"activity",title:"Parameter activity…",enabled:plugin != nil)]
    case "automation", "graphCurve", "songAutomation", "parameterActivity":
      title = editor == "graphCurve" ? "GRAPH CURVE" : editor == "parameterActivity" ? "PARAMETER ACTIVITY" : "AUTOMATION"
      actions = [A(id:"activity",title:"Parameter activity…",enabled:plugin != nil),A(id:"graph",title:"Modulation & sources…"),A(id:"songAutomation",title:"Song automation…",enabled:plugin != nil),A(id:"graph",title:"Graph recipes"),A(id:"songGraph",title:"Song mixing graph panel")]
    default:
      title = editor == "gate" ? "GATE" : editor == "notes" ? "NOTES" : editor == "graphCommands" ? "PATTERN EFFECTS" : "PATTERN"
      actions = [A(id:"notes",title:"Precise notes…"),A(id:"patternTools",title:"Selection tools…"),A(id:"arrange",title:"Arrange patterns…"),A(id:"timing",title:"Tempo & groove…"),A(id:"browseSamples",title:"Browse samples…"),A(id:"render",title:"Render selection…")]
    }
    let stamp = contextChoiceStamp
    func choice(_ item:[String:Any], prefix:String) -> WorkspaceContextPanel.Choice? {
      guard let index=item["index"] as? Int, let id=item["id"] as? String else { return nil }
      let name=item["name"] as? String ?? ""
      return .init(id:id,title:"\(prefix) \(index)" + (name.isEmpty ? "" : " · \(name)"))
    }
    contextPanel.choices(contextPanel.patterns,model.patterns.compactMap{choice($0,prefix:"Pattern")},selected:model.patterns.first{$0["index"] as? Int==model.pattern}?["id"] as? String,stamp:stamp)
    contextPanel.choices(contextPanel.orders,model.orders.enumerated().map { index,pattern in
      .init(id:String(index),title:"Order \(index) · \(pattern==65535 ? "Stop" : pattern==65534 ? "Skip" : "P\(pattern)")",enabled:model.patterns.contains{$0["index"] as? Int==pattern})
    },selected:String(selectedOrder),stamp:stamp)
    contextPanel.choices(contextPanel.octaves,(0...8).map{.init(id:String($0),title:"Octave \($0)")},selected:String(patternView.octave),stamp:stamp)
    contextPanel.choices(contextPanel.steps,[0,1,2,4,8,16].map{.init(id:String($0),title:"Step \($0)")},selected:String(patternView.step),stamp:stamp)
    let sounds = model.instruments.isEmpty ? model.samples:model.instruments
    contextPanel.choices(contextPanel.sounds,sounds.compactMap{choice($0,prefix:"Sound")},selected:sounds.first{$0["index"] as? Int==patternView.instrument}?["id"] as? String,stamp:stamp)
    contextPanel.sampleCatalogue(model.samples.compactMap{choice($0,prefix:"")},selected:model.samples.first{$0["index"] as? Int==sampleEditor.index}?["id"] as? String,stamp:stamp)
    contextPanel.update(editor:editor,title:title,actions:actions,available:available,stamp:stamp)
  }
  func contextPluginTarget(_ editor: String? = nil) -> (String,Int?)? {
    switch editor ?? contextPanel.editor {
    case "automation":
      return workspaceAutomation.selectedPluginID.map { ($0,workspaceAutomation.parameterID) }
    case "songAutomation":
      guard let editor=songAutomationWindow?.editor,!editor.pluginID.isEmpty else{return nil}
      return (editor.pluginID,editor.parameterID)
    case "parameterActivity":
      return (parameterActivity.target?["plugin"] as? String).map { ($0,parameterActivity.parameterID) }
    case "graphCurve", "graphPlugins":return nil
    default:break
    }
    guard model.nativePlugins.indices.contains(pluginEditor.selected),let plugin=model.nativePlugins[pluginEditor.selected]["instanceID"] as? String else { return nil }
    let row=pluginEditor.parameters.selectedRow
    return (plugin,pluginEditor.filteredValues.indices.contains(row) ? pluginEditor.filteredValues[row]["id"] as? Int:nil)
  }
  func chooseContext(_ kind:String,id:String,stamp:String) {
    guard !busy,!sessionReading,contextChoiceStamp==stamp else {statusLabel.stringValue="The editing context changed; choose again.";return}
    switch kind {
    case "octave": if let value=Int(id),(0...8).contains(value) {patternView.octave=value;updateInputContext()}
    case "step": if let value=Int(id),[0,1,2,4,8,16].contains(value) {patternView.step=value;stepPicker.selectItem(withTag:value)}
    case "sound":
      let items=model.instruments.isEmpty ? model.samples:model.instruments
      if let index=items.first(where:{$0["id"] as? String==id})?["index"] as? Int {patternView.instrument=index;updateInputContext()}
    case "sample":
      guard !sampleEditor.hasDraft,let index=model.samples.first(where:{$0["id"] as? String==id})?["index"] as? Int else {statusLabel.stringValue="Finish the sample edit before selecting another sample.";return}
      guard sampleEditor.index != index else{return}
      editorMode=1
      workspaceContextTokens["samples"]=WorkspaceAssetContext.token(panel:"samples",model:model,row:patternView.cursorRow,channel:patternView.cursorChannel,input:patternView.instrument)
      sampleEditor.updateCatalogue(model.samples);sampleEditor.requestSampleSelection(index)
    case "pattern", "order":
      let pattern:Int?
      if kind=="order",let order=Int(id),model.orders.indices.contains(order),model.patterns.contains(where:{$0["index"] as? Int==model.orders[order]}) {pattern=model.orders[order];selectedOrder=order}
      else {pattern=model.patterns.first{$0["id"] as? String==id}?["index"] as? Int}
      guard let pattern,model.patterns.contains(where:{$0["index"] as? Int==pattern}) else{return}
      model.pattern=pattern;patternView.isFollowing=false;refreshPattern();patternView.revealCursor();refreshOrders()
    default:return
    }
    refreshContextPanel()
  }
  func performContextAction(_ id:String) {
    guard !busy,!sessionReading,workspace?.contextPanelVisible==true,
      contextPanel.actions.contains(where:{$0.id==id && $0.enabled}) else{return}
    let target=contextPluginTarget()
    func invoke(_ menu:NSMenu,_ title:String) {
      func find(_ source:NSMenu)->NSMenuItem? {for item in source.items {if item.title==title{return item};if let submenu=item.submenu,let found=find(submenu){return found}};return nil}
      if let item=find(menu),item.isEnabled,let action=item.action {NSApp.sendAction(action,to:item.target,from:item)}
    }
    switch id {
    case "pattern":focusPattern()
    case "notes":showPreciseNotes()
    case "patternTools":showPatternTools()
    case "arrange":showArrangementMatrix()
    case "timing":showSongTiming()
    case "render":showSelectionRenderOptions()
    case "browseSamples":showSampleBrowser()
    case "importSample":importSample()
    case "recordSample":showSampleRecorder()
    case "samples","instruments","plugins":workspace?.show(id,focus:true)
    case "mixer":showMixer()
    case "graph":showSignalGraph()
    case "songGraph":showSignalGraph();signalGraphEditor.navigate(graph:nil)
    case "floatGraph":workspace?.place("graph",at:"float")
    case "dockGraph":workspace?.place("graph",at:"right")
    case "connected":workspace?.preset("Connected")
    case "fitGraph":signalGraphEditor.fit()
    case "graphPlugin":signalGraphEditor.openSelectedPlugin()
    case "graphActions":let menu=ContextActions.controls(in:signalGraphEditor,title:"Graph actions");menu.popUp(positioning:nil,at:NSPoint(x:10,y:contextPanel.bounds.height-40),in:contextPanel)
    case "graphTrims":
      workspace?.show("graph",focus:true)
      signalGraphEditor.trimControls.scrollToVisible(signalGraphEditor.trimControls.bounds)
      signalGraphEditor.window?.makeFirstResponder(signalGraphEditor.trimControls.port)
    case "addPlugin":addPlugin()
    case "busEffect":if let bus=mixerEditor.selectedID{addPlugin(target:bus)}
    case "busGroup":invoke(mixerEditor.busMenu(),"Add group")
    case "busReturn":invoke(mixerEditor.busMenu(),"Add return")
    case "busDetails":mixerEditor.viewMode.selectedSegment=1;mixerEditor.changeViewMode();workspace?.show("mixer",focus:true)
    case "busInstrument":
      mixerEditor.viewMode.selectedSegment=1;mixerEditor.changeViewMode();workspace?.show("mixer",focus:true)
      mixerEditor.source.scrollToVisible(mixerEditor.source.bounds)
      mixerEditor.window?.makeFirstResponder(mixerEditor.source)
    case "pluginInstruments":if let target,let slot=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==target.0}){pluginEditor.onInstruments?(slot)}
    case "automate":if let target,let parameter=target.1{automateParameter(plugin:target.0,parameter:parameter)}
    case "activity":if let target{showParameterActivity(plugin:target.0,parameter:target.1)}
    case "songAutomation":if let target{openSongAutomation(plugin:target.0,parameter:target.1)}
    default:break
    }
  }
}
