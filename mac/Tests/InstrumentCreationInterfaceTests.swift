import AppKit

private final class InstrumentCreationFixture {
  let owner = InstrumentCreationWorkflow()
  var target = InstrumentCreationTarget(context: .init(document: "song", revision: "song:1"), sample: 1, sampleIdentity: "s1", relatedState: "bank:0", instruments: [1: "i1"])
  var draft = InstrumentCreationDraft(index: 1, identity: "i1", generation: 1, fields: ["name": "Original"], points: [[0, 64]], mapping: [1])
  var allowed = true, composing = false, reveal = true
  var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), created = [(Int, String)]()
  init() {
    owner.onTarget = { [weak self] in self?.target }
    owner.onDraft = { [weak self] in self!.draft }
    owner.canBegin = { [weak self] in self?.allowed == true }
    owner.composing = { [weak self] in self?.composing == true }
    owner.canReveal = { [weak self] in self?.reveal == true }
    owner.onRequest = { [weak self] in self?.calls.append(($0, $1)); self?.replies.append($2) }
    owner.onCreated = { [weak self] in self?.created.append(($0, $1)) }
  }
  func revision(_ value: String, document: String = "song", sampleIdentity: String = "s1", related: String = "bank:0") {
    target = .init(context: .init(document: document, revision: value), sample: 1, sampleIdentity: sampleIdentity, relatedState: related, instruments: target.instruments)
  }
  func edit(_ text: String, generation: UInt64 = 2) {
    draft = .init(index: draft.index, identity: draft.identity, generation: generation, fields: ["name": text], points: draft.points, mapping: draft.mapping)
  }
  func answer(_ response: [String: Any]) { replies.removeFirst()(response) }
  func receipt(_ slot: Any = 3, importing: Bool = false, document: String = "song", revision: String = "song:2", changed: Any = true) -> [String: Any] {
    ["result": ["documentId": document, "revision": revision, "changed": changed, "data": [importing ? "index" : "instrument": slot]]]
  }
  func catalogue(revision: String = "song:2", instruments: [[String: Any]]? = nil) -> [String: Any] {
    ["result": ["documentId": "song", "revision": revision, "data": ["samples": [["id": "s1", "index": 1]],
      "instruments": instruments ?? [["id": "i1", "index": 1], ["id": "i3", "index": 3]]]]]
  }
}

extension InterfaceTests {
  static func instrumentCreationChecks() throws {
    let happy = InstrumentCreationFixture()
    happy.owner.create(); happy.owner.create()
    try require(happy.calls.count == 1 && happy.calls[0].0 == "instrument.create" && happy.calls[0].1["sample"] as? Int == 1 &&
      happy.calls[0].1["expectedRevision"] as? String == "song:1" && happy.owner.needsCompletion && happy.owner.busy,
      "One creation owns the original sample/revision and excludes another append")
    let duplicate = happy.replies[0]
    happy.revision("song:2"); happy.answer(happy.receipt()); duplicate(happy.receipt())
    try require(happy.calls.map(\.0) == ["instrument.create", "document.get"] && happy.created.isEmpty,
      "A valid receipt needs catalogue readback; duplicate callback cannot start another read or write")
    happy.answer(happy.catalogue()); duplicate(happy.receipt())
    try require(happy.created.count == 1 && happy.created[0].0 == 3 && happy.created[0].1 == "i3" && !happy.owner.needsCompletion,
      "Only matching catalogue identity is revealed and duplicate completion remains retired")

    for change in ["revision", "document", "sample", "bank", "raw", "generation", "composition", "draft"] {
      let f = InstrumentCreationFixture(); let operation = f.owner.begin(importing: true)!
      try require(f.owner.begin(importing: true) == nil && f.calls.isEmpty, "An open chooser owns the operation")
      switch change {
      case "revision": f.revision("song:2")
      case "document": f.revision("other:1", document: "other")
      case "sample": f.revision("song:1", sampleIdentity: "replacement-sample")
      case "bank": f.revision("song:1", related: "bank:2")
      case "raw": f.edit("Newer")
      case "generation": f.edit("Original")
      case "composition": f.composing = true
      default: f.allowed = false
      }
      try require(!f.owner.originalInputIsCurrent(operation), "Deferred admission must reject changed \(change)")
      f.owner.finishImport(operation, path: "/tmp/fixture.iti")
      try require(f.calls.isEmpty && !f.owner.needsCompletion, "Chooser completion cannot rebase changed \(change)")
    }
    let cancel = InstrumentCreationFixture(), old = InstrumentCreationFixture()
    let cancelled = cancel.owner.begin(importing: true)!
    cancel.owner.finishImport(cancelled, path: nil)
    let next = cancel.owner.begin(importing: true)!
    cancel.owner.finishImport(cancelled, path: "/tmp/late.iti")
    try require(cancel.owner.operation === next && cancel.calls.isEmpty, "Late cancelled picker cannot replace a newer chooser owner")
    cancel.owner.finishImport(next, path: "/tmp/fixture.iti")
    try require(cancel.calls.count == 1 && cancel.calls[0].0 == "instrument.import" && cancel.calls[0].1["slot"] as? Int == 0 &&
      cancel.calls[0].1["path"] as? String == "/tmp/fixture.iti" && cancel.calls[0].1["expectedRevision"] as? String == "song:1",
      "Import captures the exact append path and original revision")
    cancel.revision("song:2"); cancel.answer(cancel.receipt(importing: true)); cancel.answer(cancel.catalogue())
    try require(cancel.created.count == 1 && !cancel.owner.needsCompletion, "Import validates index receipts and completes matching readback")
    let invalidPath = old.owner.begin(importing: true)!
    old.owner.finishImport(invalidPath, path: "relative.iti")
    try require(old.calls.isEmpty && !old.owner.needsCompletion, "Invalid chooser paths cannot write")

    let refusal = InstrumentCreationFixture(); refusal.owner.create()
    refusal.answer(["error": ["code": -32002, "message": "Busy", "data": ["writeOutcome": "notCommitted"]]])
    try require(!refusal.owner.needsCompletion && refusal.calls.count == 1, "Only explicit notCommitted failure releases creation without Review")
    refusal.revision("song:3"); refusal.owner.create()
    try require(refusal.calls.count == 2 && refusal.calls[1].1["expectedRevision"] as? String == "song:3", "A later explicit creation captures fresh intent after definite rejection")

    let invalid: [[String: Any]] = [[:], ["error": ["code": -32002, "message": "Busy without outcome"]],
      happy.receipt(true), happy.receipt(0), happy.receipt(3.5), happy.receipt(document: "other"), happy.receipt(revision: ""),
      happy.receipt(revision: "song:1"), happy.receipt(changed: false), happy.receipt(changed: 1)]
    for response in invalid {
      let f = InstrumentCreationFixture(); f.owner.create(); f.answer(response)
      try require(f.owner.canReview && f.created.isEmpty && f.calls.count == 1, "Unverified creation stays retained")
      f.revision("song:2"); f.owner.create(); f.owner.review()
      try require(f.calls.count == 2 && f.calls.last?.0 == "document.get", "Repeating Create reviews instead of appending and repeated Review is bounded")
      f.answer(f.catalogue()); f.edit("Draft after Review")
      try require(f.owner.canAccept && f.owner.needsCompletion && f.created.isEmpty, "A catalogue observation does not invent write authorship or select a guessed asset")
      f.owner.accept()
      try require(!f.owner.needsCompletion && f.calls.count == 2 && f.draft.fields["name"] == "Draft after Review", "Explicit acknowledgement preserves raw drafts and performs no write")
    }
    for catalogue in [happy.catalogue(instruments: [["id": "duplicate", "index": 1], ["id": "duplicate", "index": 3]]),
      happy.catalogue(instruments: [["id": "i3", "index": true]]), happy.catalogue(revision: "old")] {
      let f = InstrumentCreationFixture(); f.owner.create(); f.answer([:]); f.revision("song:2"); f.owner.review(); f.answer(catalogue)
      try require(f.owner.canReview && !f.owner.canAccept, "Malformed, duplicate or stale catalogues cannot enable acknowledgement")
      f.owner.review(); f.answer(f.catalogue()); f.revision("song:3"); f.owner.accept()
      try require(f.owner.needsCompletion && !f.owner.canAccept, "Song changes invalidate an observation without retrying creation")
    }

    for newer in ["raw", "sameTextNewGeneration", "bank", "focus", "composition"] {
      let f = InstrumentCreationFixture(); f.owner.create(); f.revision("song:2"); f.answer(f.receipt())
      switch newer {
      case "raw": f.edit("New text")
      case "sameTextNewGeneration": f.edit("Original")
      case "bank": f.revision("song:2", related: "bank:2")
      case "composition": f.composing = true
      default: f.reveal = false
      }
      f.answer(f.catalogue())
      try require(!f.owner.needsCompletion && f.created.isEmpty && f.owner.status.contains("selection kept"),
        "Verified creation cannot redirect newer \(newer)")
    }
    let failedRead = InstrumentCreationFixture(); failedRead.owner.create(); failedRead.revision("song:2"); failedRead.answer(failedRead.receipt())
    failedRead.answer([:]); failedRead.owner.review(); failedRead.answer(failedRead.catalogue()); failedRead.owner.accept()
    try require(failedRead.calls.map(\.0) == ["instrument.create", "document.get", "document.get"] && failedRead.created.isEmpty && !failedRead.owner.needsCompletion,
      "Accepted creation with failed readback can be reconciled without repeating the append or stealing selection")
    for slot in [1, 3] {
      let f = InstrumentCreationFixture(); f.owner.create(); f.revision("song:2"); f.answer(f.receipt(slot))
      f.answer(f.catalogue(instruments: [["id": "i1", "index": slot]]))
      try require(f.owner.canReview && f.created.isEmpty, "A receipt naming a prior slot or reused identity cannot claim an existing asset as newly created")
    }
    let stale = InstrumentCreationFixture(); stale.owner.create(); stale.revision("other:2", document: "other"); stale.answer(stale.receipt())
    stale.owner.review(); try require(stale.calls.count == 1 && stale.owner.needsCompletion, "Old-song receipt neither reveals nor inspects the wrong song")

    try instrumentCreationEditorChecks()
  }

  static func instrumentCreationEditorChecks() throws {
    let editor = InstrumentEditor(frame: NSRect(x: 0, y: 0, width: 900, height: 1100))
    let model = PatternModel(["revisionToken": "song:1", "samples": [["id": "s1", "index": 1]], "instruments": [["id": "i1", "index": 1]], "format": "MPTM"])
    let info: [String: Any] = ["name": "Original", "volume": 64, "mapping": [Int](repeating: 1, count: 128)]
    editor.update(info, model: model)
    let f = InstrumentCreationFixture()
    editor.creation.onTarget = { f.target }; editor.creation.canBegin = { !editor.hasEditingDraft }
    editor.creation.onRequest = { f.calls.append(($0, $1)); f.replies.append($2) }
    let operation = editor.creation.begin(importing: true)!
    try require(editor.hasDraft && !editor.hasEditingDraft && !editor.reviewCreationButton.isHidden, "A hidden chooser participates in the native instrument draft census")
    try require(editor.prepareInstrumentReload(model) == nil, "Reload cannot discard an unresolved creation owner")
    var accepted: Bool?; editor.finishAutomaticEdit { accepted = $0 }
    try require(accepted == false && f.calls.isEmpty, "Save/history cannot silently apply or discard unresolved creation")
    editor.update(["name": "Refresh would replace raw input"], model: model)
    try require(editor.name.stringValue == "Original", "Reads during chooser ownership cannot replace raw instrument controls")
    editor.creation.finishImport(operation, path: nil)
    try require(!editor.hasDraft && editor.reviewCreationButton.isHidden, "Cancel releases only the chooser and retains the accepted editor")

    func expand(_ view: NSView) {
      if let section = view as? ToolSection, section.preference == "toolSection.instrument.settings" { section.setExpanded(true, persist: false) }
      view.subviews.forEach(expand)
    }
    expand(editor)
    let window = NSWindow(contentRect: editor.frame, styleMask: [.titled], backing: .buffered, defer: false)
    window.contentView = editor; window.makeFirstResponder(editor.name)
    guard let text = editor.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Instrument creation field editor did not attach") }
    text.setSelectedRange(NSRange(location: 2, length: 2))
    text.setMarkedText("Original", selectedRange: NSRange(location: 2, length: 2), replacementRange: NSRange(location: 0, length: text.string.utf16.count))
    let generation = editor.creationGeneration
    editor.finishAutomaticEdit { accepted = $0 }; editor.creation.create(); editor.update(["name": "Should not replace composition"], model: model)
    try require(accepted == false && text.hasMarkedText() && editor.hasDraft && f.calls.isEmpty && editor.name.currentEditor() === text && text.string == "Original",
      "Marked text counts as retained work even when identical, and blocks creation, Save and refresh without cancelling the IME")
    try require(editor.prepareInstrumentReload(model) == nil && text.hasMarkedText(), "Explicit Reload also preserves active composition")
    text.unmarkText(); text.insertText("Changed", replacementRange: NSRange(location: 0, length: text.string.utf16.count))
    try require(editor.creationGeneration > generation && editor.creationDraft.fields.values.contains("Changed"), "Native typing advances the creation generation and captures active field-editor text")
    var loaded = info
    loaded["name"] = "Reloaded instrument"; loaded["pan"] = 128; loaded["fadeout"] = 256
    loaded["envelopes"] = Array(repeating: ["points": [[0, 64], [8, 0]], "enabled": true] as [String: Any], count: 3)
    func read(_ data: [String: Any], revision: String = "song:1") -> [String: Any] {
      ["result": ["documentId": "song", "revision": revision, "data": data]]
    }
    let reload = editor.prepareInstrumentReload(model)!
    editor.finishAutomaticEdit { accepted = $0 }
    try require(accepted == false && editor.hasDraft, "Save waits for explicit Reload to finish")
    text.insertText("Newer while reloading", replacementRange: NSRange(location: 0, length: text.string.utf16.count))
    editor.finishInstrumentReload(reload, response: read(loaded), model: model)
    try require(text.string == "Newer while reloading" && editor.hasDraft && editor.reloadOwner == nil,
      "Reload completion cannot overwrite newer raw input or its focus")
    let staleReload = editor.prepareInstrumentReload(model)!
    editor.finishInstrumentReload(staleReload, response: read(loaded, revision: "song:2"), model: model)
    try require(text.string == "Newer while reloading" && editor.hasDraft, "A different read revision cannot replace the captured draft")
    let malformedReload = editor.prepareInstrumentReload(model)!
    editor.finishInstrumentReload(malformedReload, response: read(["name": "Incomplete"]), model: model)
    try require(text.string == "Newer while reloading" && editor.hasDraft, "Malformed instrument readback retains existing fields")
    let acceptedReload = editor.prepareInstrumentReload(model)!
    text.setSelectedRange(NSRange(location: 2, length: 3))
    editor.finishInstrumentReload(acceptedReload, response: read(loaded), model: model)
    try require(!editor.hasDraft && text.string == "Reloaded instrument" && editor.name.currentEditor() === text && text.selectedRange() == NSRange(location: 2, length: 3),
      "Verified explicit Reload adopts the current baseline while keeping focus and caret")
    editor.layoutSubtreeIfNeeded()
    let point = editor.envelope.convert(editor.envelope.location([0, 64]), to: nil)
    let down = NSEvent.mouseEvent(with: .leftMouseDown, location: point, modifierFlags: [], timestamp: 0,
      windowNumber: window.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
    editor.envelope.mouseDown(with: down)
    try require(editor.envelope.isDragging && editor.hasDraft, "Holding an unchanged native envelope node retains the drag")
    var saveAttempts = 0, completions = [Bool]()
    func saveBoundary() {
      saveAttempts += 1
      guard saveAttempts <= 3, editor.hasDraft else { return }
      editor.finishAutomaticEdit { ok in completions.append(ok); if ok { saveBoundary() } }
    }
    saveBoundary()
    try require(saveAttempts == 1 && completions == [false] && editor.envelope.isDragging,
      "Save during an unmoved envelope drag refuses once instead of recursively re-entering Save")
    let up = NSEvent.mouseEvent(with: .leftMouseUp, location: point, modifierFlags: [], timestamp: 1,
      windowNumber: window.windowNumber, context: nil, eventNumber: 2, clickCount: 1, pressure: 0)!
    editor.envelope.mouseUp(with: up); editor.finishAutomaticEdit { accepted = $0 }
    try require(!editor.envelope.isDragging && accepted == true && !editor.hasDraft, "Releasing an unchanged drag restores ordinary Save completion")
    editor.creation.create(); f.answer([:])
    let commands = ContextActions.controls(in: editor, title: "Instrument")
    try require(commands.items.contains { $0.title == "Review creation" && $0.isEnabled },
      "Retained creation Review is reachable through the shared command-palette catalogue")
    window.makeFirstResponder(nil); window.orderOut(nil)
  }
}
