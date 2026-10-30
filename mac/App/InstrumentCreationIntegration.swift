import AppKit
import UniformTypeIdentifiers

extension AppController {
  var instrumentCreationBanks: [EnvelopeBankWindow] {
    let tools = instrumentEnvelopeToolsWindow?.contentView as? InstrumentEnvelopeToolsEditor
    return [instrumentEnvelopeBank, tools?.bankWindow].compactMap { $0 }
  }
  var instrumentCreationRelatedState: String {
    instrumentCreationBanks.map { "\(ObjectIdentifier($0)):\($0.generation):\($0.workbench?.generation ?? 0)" }.joined(separator: "|") +
      "|input:\(patternView.instrument)|cursor:\(patternView.contextToken)"
  }
  var instrumentCreationCanBegin: Bool {
    let tools = instrumentEnvelopeToolsWindow?.contentView as? InstrumentEnvelopeToolsEditor
    return !busy && !shuttingDown && model.editable && !instrumentEditor.hasEditingDraft && !sampleEditor.hasDraft &&
      !instrumentCreationBanks.contains { $0.dirty || $0.pending || $0.workbench?.hasDraft == true || ($0.window?.firstResponder as? NSTextView)?.hasMarkedText() == true } &&
      tools?.pending != true && tools?.preview == nil && (tools?.window?.firstResponder as? NSTextView)?.hasMarkedText() != true &&
      (instrumentPluginWindow?.contentView as? InstrumentPluginEditor)?.pending != true
  }
  func configureInstrumentCreation() {
    let creation = instrumentEditor.creation
    creation.onTarget = { [weak self] in
      guard let self, !self.shuttingDown, !self.model.revisionToken.isEmpty,
        SampleRecordingRead.validAssets(["samples": self.model.samples, "instruments": self.model.instruments]) else { return nil }
      let sample = self.model.samples.first { $0["index"] as? Int == self.sampleEditor.index }
      let instruments = Dictionary(uniqueKeysWithValues: self.model.instruments.map { (SampleRecordingRead.integer($0["index"])!, $0["id"] as! String) })
      return InstrumentCreationTarget(context: .init(document: String(self.model.revisionToken.split(separator: ":").first ?? ""), revision: self.model.revisionToken),
        sample: sample?["index"] as? Int ?? 0, sampleIdentity: sample?["id"] as? String ?? "", relatedState: self.instrumentCreationRelatedState, instruments: instruments)
    }
    creation.canBegin = { [weak self] in self?.instrumentCreationCanBegin == true }
    creation.canReveal = { [weak self] in
      guard let self else { return false }
      return !self.instrumentEditor.creationFields.contains { $0.currentEditor() != nil } &&
        self.sampleEditor.focusedSampleField == nil && self.patternView.deferredKeyCount == 0 && NSApp.keyWindow === self.instrumentEditor.window
    }
    creation.onRequest = { [weak self] method, params, reply in self?.submitInstrumentCreationRequest(method, params: params, reply: reply) }
    instrumentEditor.onReloadInstrument = { [weak self] in
      guard let self, let owner = self.instrumentEditor.prepareInstrumentReload(self.model) else { return }
      self.readInstrumentReload(owner)
    }
    creation.onCreated = { [weak self] slot, identity in
      guard let self, self.instrumentCreationCanBegin,
        self.model.instruments.contains(where: { $0["index"] as? Int == slot && $0["id"] as? String == identity }) else { return }
      self.instrumentEditor.index = slot; self.patternView.instrument = slot
      self.workspaceContextTokens["instruments"] = WorkspaceAssetContext.token(panel: "instruments", model: self.model,
        row: self.patternView.cursorRow, channel: self.patternView.cursorChannel, input: slot)
      self.refreshWorkspaceAsset("instruments")
    }
  }
  func readInstrumentReload(_ owner: InstrumentReloadOperation) {
    guard instrumentEditor.reloadOwner === owner else { return }
    if busy || sessionReading {
      deferUntilIdle { [weak self] in self?.readInstrumentReload(owner) }; return
    }
    guard model.revisionToken == owner.context.revision,
      model.instruments.contains(where: { $0["index"] as? Int == owner.draft.index && $0["id"] as? String == owner.identity }) else {
      instrumentEditor.finishInstrumentReload(owner, response: [:], model: model); return
    }
    handleAutomation("instrument.get", params: ["instrument": owner.draft.index]) { [weak self] response in
      guard let self else { return }; self.instrumentEditor.finishInstrumentReload(owner, response: response, model: self.model)
    }
  }
  func submitInstrumentCreationRequest(_ method: String, params: [String: Any], reply: @escaping ([String: Any]) -> Void) {
    if busy || sessionReading {
      deferUntilIdle({ [weak self] in self?.submitInstrumentCreationRequest(method, params: params, reply: reply) },
        cancel: { reply(AutomationServer.notAdmittedBusy("The application is shutting down; no creation was submitted.")) })
      return
    }
    if method != "document.get" {
      let creation = instrumentEditor.creation
      guard let owner = creation.operation, owner.phase == .writing, owner.method == method,
        NSDictionary(dictionary: params).isEqual(to: owner.params), creation.originalInputIsCurrent(owner) else {
        reply(AutomationServer.error(-32001, "Song, source sample or editor changed before instrument creation.", data: ["writeOutcome": "notCommitted"])); return
      }
    }
    handleAutomation(method, params: params) { [weak self] response in
      if method == "document.get", response["error"] == nil { self?.refreshAll() }
      reply(response)
    }
  }
  func importInstrument() {
    guard !busy, window.attachedSheet == nil, NSApp.modalWindow == nil else { return }
    if instrumentEditor.creation.needsCompletion { showEditor(2); instrumentEditor.creation.review(); return }
    guard let owner = instrumentEditor.creation.begin(importing: true) else { return }
    showEditor(2)
    let panel = NSOpenPanel()
    panel.title = "Import instrument"
    panel.allowedContentTypes = ["iti", "xi", "pat", "sfz"].compactMap { UTType(filenameExtension: $0) }
    panel.allowsOtherFileTypes = true
    panel.beginSheetModal(for: window) { [weak self] result in
      let path = result == .OK ? panel.url?.path : nil
      DispatchQueue.main.async { [weak self] in self?.instrumentEditor.creation.finishImport(owner, path: path) }
    }
  }
  func addInstrument() {
    guard !busy, window.attachedSheet == nil, NSApp.modalWindow == nil else { return }
    if instrumentEditor.creation.needsCompletion { showEditor(2); instrumentEditor.creation.review(); return }
    guard model.samples.isEmpty || model.samples.contains(where: { $0["index"] as? Int == sampleEditor.index }) else {
      statusLabel.stringValue = "Select an available source sample before creating an instrument."; return
    }
    guard let owner = instrumentEditor.creation.begin(importing: false) else { return }
    showEditor(2); instrumentEditor.creation.submit(owner)
  }
}
