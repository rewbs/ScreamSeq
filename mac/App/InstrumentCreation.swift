import AppKit

struct InstrumentCreationTarget: Equatable {
  let context: SampleRecordingContext
  let sample: Int, sampleIdentity: String, relatedState: String
  var instruments: [Int: String] = [:]
}

// Includes raw field-editor text, not just the controls' committed values.
// Navigation/focus is independent; a later completion must not steal it.
struct InstrumentCreationDraft: Equatable {
  let index: Int, identity: String, generation: UInt64
  let fields: [String: String], points: [[Int]], mapping: [Int]
}

final class InstrumentReloadOperation {
  let context: SampleRecordingContext, draft: InstrumentCreationDraft, identity: String
  init(context: SampleRecordingContext, draft: InstrumentCreationDraft, identity: String) {
    self.context = context; self.draft = draft; self.identity = identity
  }
}

final class InstrumentCreationOperation {
  enum Phase { case choosing, writing, reading, retained, reviewing }
  let target: InstrumentCreationTarget, draft: InstrumentCreationDraft, method: String
  var params: [String: Any], phase: Phase = .choosing
  var receipt: (context: SampleRecordingContext, slot: Int)?
  var observation: SampleRecordingContext?
  init(target: InstrumentCreationTarget, draft: InstrumentCreationDraft, importing: Bool) {
    self.target = target; self.draft = draft
    method = importing ? "instrument.import" : "instrument.create"
    params = ["expectedRevision": target.context.revision]
    if importing { params["slot"] = 0 } else { params["sample"] = target.sample }
  }
}

final class InstrumentCreationWorkflow {
  var onTarget: (() -> InstrumentCreationTarget?)?
  var onDraft: (() -> InstrumentCreationDraft)?
  var canBegin: (() -> Bool)?, canReveal: (() -> Bool)?, composing: (() -> Bool)?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onCreated: ((Int, String) -> Void)?, onChange: (() -> Void)?
  private(set) var operation: InstrumentCreationOperation?, status = ""
  var needsCompletion: Bool { operation != nil }
  var busy: Bool { operation.map { $0.phase != .retained } ?? false }
  var canReview: Bool { operation?.phase == .retained }
  var canAccept: Bool { canReview && operation?.observation != nil }
  private func show(_ message: String) { status = message; onChange?() }
  func begin(importing: Bool) -> InstrumentCreationOperation? {
    if operation != nil {
      if canReview { review() } else { show("Wait for the current instrument creation or file chooser.") }
      return nil
    }
    guard canBegin?() == true, composing?() != true else {
      show("Finish or reload the sample, instrument and envelope-bank drafts before creating an instrument."); return nil
    }
    guard let target = onTarget?(), !target.context.document.isEmpty, !target.context.revision.isEmpty,
      target.sample >= 0, target.sample == 0 || !target.sampleIdentity.isEmpty, let draft = onDraft?() else {
      show("The original song or source sample is unavailable. Reload before creating an instrument."); return nil
    }
    let owner = InstrumentCreationOperation(target: target, draft: draft, importing: importing)
    operation = owner
    show(importing ? "Choose an instrument file. The original song and editor are retained." : "Creating an instrument from the captured sample…")
    return owner
  }
  func originalInputIsCurrent(_ owner: InstrumentCreationOperation) -> Bool {
    operation === owner && onTarget?() == owner.target && onDraft?() == owner.draft && canBegin?() == true && composing?() != true
  }
  func finishImport(_ owner: InstrumentCreationOperation, path: String?) {
    guard operation === owner, owner.phase == .choosing, owner.method == "instrument.import" else { return }
    guard let path else { operation = nil; show("Instrument import cancelled. Existing instruments and fields kept."); return }
    guard (path as NSString).isAbsolutePath, !path.contains("\0"), originalInputIsCurrent(owner) else {
      operation = nil; show("Song, source or draft changed while choosing an instrument. Import cancelled; choose again."); return
    }
    owner.params["path"] = path; submit(owner)
  }
  func create() {
    guard let owner = begin(importing: false) else { return }; submit(owner)
  }
  func submit(_ owner: InstrumentCreationOperation) {
    guard operation === owner, owner.phase == .choosing else { return }
    guard originalInputIsCurrent(owner), let onRequest else {
      operation = nil; show("The captured song or editor changed. No instrument was created."); return
    }
    owner.phase = .writing; show("Creating instrument…")
    onRequest(owner.method, owner.params) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .writing else { return }
      if let error = response["error"] as? [String: Any],
        (error["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted" {
        self.operation = nil; self.show((error["message"] as? String ?? "Instrument creation was rejected.") + " No write committed; fields kept."); return
      }
      guard response["error"] == nil, let result = response["result"] as? [String: Any],
        result["documentId"] as? String == owner.target.context.document,
        SampleRecordingRead.boolean(result["changed"]) == true,
        let revision = result["revision"] as? String, !revision.isEmpty, revision != owner.target.context.revision,
        let data = result["data"] as? [String: Any],
        let slot = SampleRecordingRead.integer(data[owner.method == "instrument.import" ? "index" : "instrument"], maximum: 65535, minimum: 1) else {
        self.retain(owner, "Instrument creation could not be verified. Review before another creation; no write will be repeated."); return
      }
      let context = SampleRecordingContext(document: owner.target.context.document, revision: revision)
      owner.receipt = (context, slot)
      guard self.onTarget?()?.context == context else {
        self.retain(owner, "Instrument creation returned a receipt, but the song has changed. Review the current catalogue."); return
      }
      owner.phase = .reading; self.show("Checking the created instrument…")
      onRequest("document.get", [:]) { [weak self] response in
        guard let self, self.operation === owner, owner.phase == .reading else { return }
        guard self.onTarget?()?.context == context,
          let data = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(data),
          let item = (data["instruments"] as? [[String: Any]])?.first(where: { SampleRecordingRead.integer($0["index"]) == slot }),
          let identity = item["id"] as? String, !identity.isEmpty,
          owner.target.instruments[slot] == nil, !owner.target.instruments.values.contains(identity) else {
          self.retain(owner, "Created instrument readback failed or changed. Review the retained result; no write will be repeated."); return
        }
        let reveal = self.onDraft?() == owner.draft && self.canBegin?() == true && self.canReveal?() != false && self.composing?() != true &&
          self.onTarget?()?.relatedState == owner.target.relatedState
        self.operation = nil
        if reveal { self.onCreated?(slot, identity) }
        self.show("Created instrument \(slot) · one Undo." + (reveal ? "" : " Current editor and selection kept."))
      }
    }
  }
  private func retain(_ owner: InstrumentCreationOperation, _ message: String) {
    guard operation === owner else { return }; owner.phase = .retained; owner.observation = nil; show(message)
  }
  func review() {
    guard let owner = operation, owner.phase == .retained, let onRequest else { return }
    owner.observation = nil
    guard composing?() != true, let context = onTarget?()?.context, context.document == owner.target.context.document else {
      show("Return to the original song and finish composing before reviewing this instrument creation."); return
    }
    owner.phase = .reviewing; show("Inspecting current samples and instruments…")
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .reviewing else { return }
      guard self.onTarget?()?.context == context, self.composing?() != true,
        let data = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(data) else {
        self.retain(owner, "The catalogue could not be verified or the song changed. Review again; creation remains retained."); return
      }
      owner.observation = context; owner.phase = .retained
      let samples = (data["samples"] as? [[String: Any]])?.count ?? 0, instruments = (data["instruments"] as? [[String: Any]])?.count ?? 0
      self.show("Inspected \(samples) samples and \(instruments) instruments. Accept observed state acknowledges the earlier creation without repeating it or replacing editor drafts.")
    }
  }
  func accept() {
    guard let owner = operation, owner.phase == .retained, let observed = owner.observation else { return }
    guard composing?() != true, onTarget?()?.context == observed else {
      owner.observation = nil; show("Song changed since Review. Review again before accepting."); return
    }
    operation = nil; show("Earlier creation acknowledged. No write repeated; current fields and selection kept.")
  }
}

extension InstrumentEditor {
  var creationFields: [NSTextField] {
    [name, volume, pan, fade, nodeTick, nodeValue, releaseNode, sustainPoint, sustainEnd, loopStart, loopEnd, mapFrom, mapTo]
  }
  var hasInstrumentComposition: Bool { creationFields.contains { ($0.currentEditor() as? NSTextView)?.hasMarkedText() == true } }
  var creationDraft: InstrumentCreationDraft {
    var fields = settingsDraft.snapshot
    for (index, field) in creationFields.enumerated() { fields["raw\(index)"] = (field.currentEditor() as? NSTextView)?.string ?? field.stringValue }
    fields["envelope"] = String(envelopeType.indexOfSelectedItem)
    fields["node"] = String(envelope.selectedNode ?? -1); fields["mappedSample"] = String(mapSample.selectedTag())
    return .init(index: index, identity: capturedInstrument, generation: creationGeneration, fields: fields, points: envelope.points, mapping: keymap)
  }
  func updateCreationControls() {
    let retained = creation.needsCompletion
    for button in [reviewCreationButton, acceptCreationButton] {
      if !retained, window?.firstResponder === button { window?.makeFirstResponder(self) }
      button.isHidden = !retained
    }
    reviewCreationButton.isEnabled = creation.canReview || window?.firstResponder === reviewCreationButton
    acceptCreationButton.isEnabled = creation.canAccept || window?.firstResponder === acceptCreationButton
    creationStatus.stringValue = creation.status; creationStatus.isHidden = creation.status.isEmpty
    onMessage?(creation.status)
  }
  func prepareInstrumentReload(_ model: PatternModel) -> InstrumentReloadOperation? {
    guard !creation.needsCompletion, reloadOwner == nil, !pending, !hasInstrumentComposition, !envelope.isDragging,
      let identity = model.instruments.first(where: { $0["index"] as? Int == index })?["id"] as? String,
      !identity.isEmpty, capturedInstrument.isEmpty || capturedInstrument == identity else {
      onMessage?("Finish composing or review the retained instrument operation before reloading."); return nil
    }
    let owner = InstrumentReloadOperation(context: .init(document: String(model.revisionToken.split(separator: ":").first ?? ""), revision: model.revisionToken),
      draft: creationDraft, identity: identity)
    reloadOwner = owner; onMessage?("Reloading the captured instrument. Fields stay until readback is verified."); return owner
  }
  func finishInstrumentReload(_ owner: InstrumentReloadOperation, response: [String: Any], model: PatternModel) {
    guard reloadOwner === owner else { return }; reloadOwner = nil
    guard creationDraft == owner.draft, !hasInstrumentComposition, !envelope.isDragging,
      model.revisionToken == owner.context.revision,
      model.instruments.contains(where: { $0["index"] as? Int == owner.draft.index && $0["id"] as? String == owner.identity }),
      let data = SampleRecordingRead.result(response, context: owner.context), data["name"] is String,
      SampleRecordingRead.integer(data["volume"], maximum: 64) != nil,
      SampleRecordingRead.integer(data["pan"], maximum: 256) != nil,
      SampleRecordingRead.integer(data["fadeout"], maximum: 32768) != nil,
      let mapping = data["mapping"] as? [Any], mapping.count == 128,
      mapping.allSatisfy({ SampleRecordingRead.integer($0, maximum: 65535) != nil }),
      let envelopes = data["envelopes"] as? [[String: Any]], envelopes.count == 3,
      envelopes.allSatisfy({ item in
        guard let points = item["points"] as? [[Any]] else { return false }
        return points.allSatisfy { $0.count == 2 && SampleRecordingRead.integer($0[0], maximum: 65535) != nil && SampleRecordingRead.integer($0[1], maximum: 64) != nil }
      }) else {
      onMessage?("Instrument readback failed, changed or newer input arrived. Fields kept; Reload instrument to try again."); return
    }
    let focused = creationFields.first { $0.currentEditor() != nil }
    let text = focused?.currentEditor() as? NSTextView, caret = text?.selectedRange()
    adoptInstrumentReload(data, model: model)
    if let focused, let text, let caret, focused.currentEditor() === text, window?.firstResponder === text {
      text.string = focused.stringValue
      let location = min(caret.location, text.string.utf16.count)
      text.setSelectedRange(NSRange(location: location, length: min(caret.length, text.string.utf16.count - location)))
    }
    onMessage?("Instrument reloaded. The inspected settings replace the previous draft.")
  }
}
