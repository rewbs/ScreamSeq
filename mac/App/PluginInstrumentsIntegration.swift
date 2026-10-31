import AppKit
extension AppController {
  var instrumentPluginEditor: InstrumentPluginEditor? { instrumentPluginWindow?.contentView as? InstrumentPluginEditor }
  func retainInstrumentPluginWindow(creating: Bool, identity: String = "") -> Bool {
    guard let window = instrumentPluginWindow, let editor = instrumentPluginEditor, !editor.workflow.retired else { return false }
    let document = String(model.revisionToken.split(separator: ":").first ?? "")
    if editor.needsCompletion || (editor.workflow.target.context.document == document &&
      (creating ? editor.isCreating && editor.instrument == 0 : editor.workflow.target.identity == identity)) {
      window.makeKeyAndOrderFront(nil); editor.refreshIfClean()
      if editor.needsCompletion { statusLabel.stringValue = "Finish or review the retained plugin instrument before opening another target." }
      return true
    }
    return false
  }
  func showNewPluginInstrument(slot: Int? = nil) {
    guard !busy, model.editable else { return }
    if retainInstrumentPluginWindow(creating: true) { return }
    let identity = slot.flatMap { model.nativePlugins.indices.contains($0) ? model.nativePlugins[$0]["instanceID"] as? String : nil }
    let editor = InstrumentPluginEditor(instrument: 0, model: model, selectedPlugin: identity)
    configureInstrumentPluginBridge(editor)
    editor.onSaved = { [weak self, weak editor] index in
      guard let self, let editor, self.model.revisionToken == editor.workflow.target.context.revision,
        self.model.instruments.contains(where: { $0["index"] as? Int == index && $0["id"] as? String == editor.workflow.target.identity }) else { return }
      self.instrumentEditor.index = index; self.patternView.instrument = index
      self.workspaceContextTokens["instruments"] = WorkspaceAssetContext.token(panel: "instruments", model: self.model,
        row: self.patternView.cursorRow, channel: self.patternView.cursorChannel, input: index)
      self.workspace?.panels["instruments"]?.target.stringValue = "Instrument \(index)"
      self.refreshAll(); self.workspace?.show("instruments", focus: true); self.refreshWorkspaceAsset("instruments")
    }
    presentInstrumentPlugin(editor, title: "New plugin instrument")
  }
  func showInstrumentPluginAssignment() {
    if instrumentPluginEditor?.needsCompletion == true { _ = retainInstrumentPluginWindow(creating: false); return }
    guard !busy, let item = model.instruments.first(where: { $0["index"] as? Int == instrumentEditor.index }),
      let identity = item["id"] as? String else { statusLabel.stringValue = "Create a tracker instrument first."; return }
    if retainInstrumentPluginWindow(creating: false, identity: identity) { return }
    let editor = InstrumentPluginEditor(instrument: instrumentEditor.index, model: model)
    configureInstrumentPluginBridge(editor)
    editor.onSaved = { [weak self] _ in self?.refreshAll() }
    presentInstrumentPlugin(editor, title: "Instrument sound source")
  }
  private func presentInstrumentPlugin(_ editor: InstrumentPluginEditor, title: String) {
    let win = NSWindow(contentRect: editor.frame, styleMask: [.titled, .closable], backing: .buffered, defer: false)
    win.title = title; win.isReleasedWhenClosed = false; win.contentView = editor
    instrumentPluginWindow?.close(); instrumentPluginWindow = win; win.center(); win.makeKeyAndOrderFront(nil)
  }
  func configureInstrumentPluginBridge(_ editor: InstrumentPluginEditor) {
    editor.workflow.onRetired = { [weak self, weak editor] in
      guard let self, let editor, self.instrumentPluginEditor === editor else { return }
      self.instrumentPluginWindow?.close(); self.instrumentPluginWindow = nil
    }
    editor.workflow.onContext = { [weak self] in
      guard let self, !self.shuttingDown else { return nil }
      return .init(document: String(self.model.revisionToken.split(separator: ":").first ?? ""), revision: self.model.revisionToken)
    }
    editor.workflow.canBegin = { [weak self] in
      guard let self else { return false }
      return !self.busy && !self.shuttingDown && self.model.editable && !self.instrumentEditor.creation.needsCompletion && self.sampleBrowser?.imports.needsCompletion != true
    }
    editor.workflow.onRevealContext = { [weak self] in self?.instrumentCreationRelatedState ?? "" }
    editor.workflow.canReveal = { [weak self, weak editor] in
      guard let self, let editor else { return false }
      return NSApp.keyWindow === editor.window && !(editor.window?.firstResponder is NSTextView) &&
        !self.instrumentEditor.hasDraft && !self.sampleEditor.hasDraft && self.patternView.deferredKeyCount == 0 &&
        !self.instrumentCreationBanks.contains { $0.dirty || $0.pending || $0.workbench?.hasDraft == true }
    }
    editor.onRequest = { [weak self, weak editor] method, params, reply in
      guard let self, let editor else { reply(AutomationServer.notAdmittedBusy("The captured plugin instrument is unavailable.")); return }
      self.submitInstrumentPluginRequest(editor, method: method, params: params, reply: reply)
    }
    editor.onRack = { [weak self] in self?.showEditor(3) }
    editor.onAddPlugin = { [weak self, weak editor] in
      guard let self, let editor, !editor.workflow.needsCompletion, !editor.composing else { return }
      self.addPlugin(instrumentOnly: true, added: { [weak self, weak editor] _ in
        guard let self, let editor, self.instrumentPluginEditor === editor else { return }
        // A clean captured chooser can refresh its catalogue. Dirty fields keep
        // their original revision; adding a plugin never assigns it implicitly.
        editor.refreshIfClean()
        if editor.hasDraft { editor.status.stringValue = "Plugin added to the rack. Fields kept; Reload / discard to refresh plugin choices." }
      })
    }
  }
  func submitInstrumentPluginRequest(_ editor: InstrumentPluginEditor, method: String, params: [String: Any], reply: @escaping ([String: Any]) -> Void) {
    if busy || sessionReading {
      deferUntilIdle({ [weak self, weak editor] in
        guard let self, let editor else { reply(AutomationServer.notAdmittedBusy("The captured editor is unavailable.")); return }
        self.submitInstrumentPluginRequest(editor, method: method, params: params, reply: reply)
      }, cancel: { reply(AutomationServer.notAdmittedBusy("The application is shutting down; no instrument write was submitted.")) }); return
    }
    if !method.hasSuffix(".get") {
      guard let owner = editor.workflow.operation,
        (method == "instrument.create" && owner.phase == .creating) || (method == "instrument.plugin.set" && owner.phase == .assigning),
        NSDictionary(dictionary: params).isEqual(to: owner.params), editor.workflow.originalInputIsCurrent(owner) else {
        reply(AutomationServer.error(-32001, "Captured song, instrument or fields changed before submission.", data: ["writeOutcome": "notCommitted"])); return
      }
    }
    handleAutomation(method, params: params) { [weak self] response in
      if method == "document.get", response["error"] == nil { self?.refreshAll() }
      reply(response)
    }
  }

  func showPluginInstruments(_ slot:Int) {
    guard !busy,model.nativePlugins.indices.contains(slot),model.nativePlugins[slot]["isInstrument"] as? Bool==true,
      let identity=model.nativePlugins[slot]["instanceID"] as? String else{return}
    let editor=PluginInstrumentsEditor(plugin:identity)
    editor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    pluginInstrumentsWindow?.close()
    let win=NSWindow(contentRect:NSRect(x:0,y:0,width:680,height:520),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    win.title="Plugin instruments";win.minSize=NSSize(width:620,height:460);win.isReleasedWhenClosed=false;win.contentView=editor;pluginInstrumentsWindow=win
    win.center();win.makeKeyAndOrderFront(nil);editor.load()
  }
}
