import AppKit

final class InstrumentPluginFixture {
  let owner: InstrumentPluginWorkflow
  var context = SampleRecordingContext(document: "song", revision: "song:1")
  var input = InstrumentPluginDraft(generation: 1, fields: .init(name: "Trigger", plugin: "synth", channel: 3))
  var identities = [1: "i1", 2: "i2"], names = [1: "Existing", 2: "Other"]
  var mappings = [1: [Int](repeating: 1, count: 128), 2: [Int](repeating: 1, count: 128)]
  var routes: [Int: InstrumentPluginRoute] = [2: .init(plugin: "other", channel: 7)]
  var allowed = true, composing = false, reveal = true, revealContext = "cursor:1"
  var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
  var observed = [(Int, Bool)](), saved = [Int]()
  var writes: [(String, [String: Any])] { calls.filter { !$0.0.hasSuffix(".get") } }
  init(creating: Bool = false, workflow: InstrumentPluginWorkflow? = nil) {
    owner = workflow ?? InstrumentPluginWorkflow(target: .init(context: context, slot: creating ? 0 : 1, identity: creating ? "" : "i1"))
    owner.onContext = { [weak self] in self?.context }
    if owner.onDraft == nil { owner.onDraft = { [weak self] in self!.input } }
    if owner.composing == nil { owner.composing = { [weak self] in self?.composing == true } }
    owner.canBegin = { [weak self] in self?.allowed == true }; owner.canReveal = { [weak self] in self?.reveal == true }
    owner.onRevealContext = { [weak self] in self?.revealContext ?? "" }
    owner.onRequest = { [weak self] in self?.calls.append(($0, $1)); self?.replies.append($2) }
    let adopted = owner.onObserved
    owner.onObserved = { [weak self] snapshot, slot, name, replace in self?.observed.append((slot, replace)); adopted?(snapshot, slot, name, replace) }
    owner.onSaved = { [weak self] slot, _ in self?.saved.append(slot) }
  }
  var model: PatternModel { PatternModel(["revisionToken": context.revision, "format": "MPTM", "samples": [["index": 1, "id": "s1", "name": "Sample"]],
    "instruments": identities.keys.sorted().map { ["index": $0, "id": identities[$0]!, "name": names[$0]!] as [String: Any] }, "nativePlugins": plugins]) }
  var plugins: [[String: Any]] {
    ["synth", "other"].map { id in ["instanceID": id, "name": id == "synth" ? "Fixture synth" : "Other synth", "isInstrument": true,
      "instrumentAssignments": routes.keys.sorted().filter { routes[$0]?.plugin == id }.map { ["instrument": $0, "channel": routes[$0]!.channel, "available": identities[$0] != nil] }] }
  }
  func answer(_ response: [String: Any]) { replies.removeFirst()(response) }
  func result(_ data: [String: Any]) -> [String: Any] { ["result": ["documentId": context.document, "revision": context.revision, "data": data]] }
  func document() -> [String: Any] { result(["samples": [["index": 1, "id": "s1"]], "instruments": identities.keys.sorted().map { ["index": $0, "id": identities[$0]!] as [String: Any] }, "nativePlugins": plugins]) }
  func metadata(_ slot: Int? = nil) -> [String: Any] { let slot = slot ?? owner.operation?.target.slot ?? owner.target.slot; return result(["name": names[slot] ?? "", "mapping": mappings[slot] ?? []]) }
  func advance() { context = .init(document: "song", revision: "song:\((Int(context.revision.split(separator: ":").last!) ?? 1) + 1)") }
  func prepare() { owner.apply(); answer(document()); if owner.target.slot > 0 { answer(metadata()) } }
  func createReceipt() -> [String: Any] {
    advance(); identities[3] = "i3"; names[3] = writes.last!.1["name"] as? String ?? ""; mappings[3] = Array(repeating: 0, count: 128)
    return ["result": ["documentId": context.document, "revision": context.revision, "changed": true, "data": ["instrument": 3, "empty": true, "dryRun": false]]]
  }
  func completeCreation() { answer(createReceipt()); answer(document()); answer(metadata(3)) }
  func assignmentReceipt(changed: Bool = true) -> [String: Any] {
    let params = writes.last!.1, slot = params["instrument"] as! Int, plugin = params["plugin"] as! String, channel = params["channel"] as! Int
    routes[slot] = plugin.isEmpty ? nil : .init(plugin: plugin, channel: channel)
    if changed { advance() }
    return ["result": ["documentId": context.document, "revision": context.revision, "changed": changed,
      "data": ["instrument": slot, "plugin": plugin, "channel": channel, "wouldChange": changed, "dryRun": false]]]
  }
  func completeAssignment(changed: Bool = true) { answer(assignmentReceipt(changed: changed)); answer(document()); answer(metadata()) }
  func review() { owner.review(); answer(document()); if owner.target.slot > 0 { answer(metadata()) } }
}

extension InterfaceTests {
  static func instrumentPluginCompletionChecks() throws {
    for name in [String(repeating: "a", count: 32), String(repeating: "é", count: 16), String(repeating: "🎹", count: 8)] {
      let f = InstrumentPluginFixture(creating: true)
      f.input = .init(generation: 1, fields: .init(name: name, plugin: "synth", channel: 3))
      f.owner.apply()
      try require(f.calls.isEmpty && !f.owner.needsCompletion && f.owner.status.contains("31 UTF-8 bytes"),
        "An overlong stored name is rejected before creation without losing its text")
    }
    for name in [String(repeating: "a", count: 31), String(repeating: "é", count: 15) + "a"] {
      let f = InstrumentPluginFixture(creating: true)
      f.input = .init(generation: 1, fields: .init(name: name, plugin: "synth", channel: 3))
      f.prepare(); f.completeCreation(); f.completeAssignment()
      try require(!f.owner.needsCompletion && f.names[3] == name && f.routes[3]?.plugin == "synth",
        "An exact 31-byte name completes creation and assignment")
    }
    for creating in [false, true] {
      let f = InstrumentPluginFixture(creating: creating); f.prepare()
      if creating {
        try require(f.writes.count == 1 && f.writes[0].0 == "instrument.create" && f.writes[0].1["name"] as? String == "Trigger", "Creation captures the empty-trigger name exactly once")
        let callback = f.replies[0]; f.completeCreation(); callback([:])
        try require(f.owner.target.slot == 3 && f.owner.target.identity == "i3", "Verified creation keeps its stable identity before assignment")
      }
      let params = f.writes.last!.1
      try require(f.writes.last?.0 == "instrument.plugin.set" && params["plugin"] as? String == "synth" && params["channel"] as? Int == 3,
        "Assignment uses captured plugin and MIDI channel after preflight")
      f.completeAssignment()
      try require(!f.owner.needsCompletion && f.saved == [creating ? 3 : 1] && f.routes[2] == .init(plugin: "other", channel: 7),
        "Verified assignment completes with original keymap and unrelated routing intact")
    }
    let noOp = InstrumentPluginFixture(); noOp.routes[1] = .init(plugin: "synth", channel: 3); noOp.prepare(); noOp.completeAssignment(changed: false)
    try require(!noOp.owner.needsCompletion && noOp.context.revision == "song:1", "Valid no-op assignment does not require a revision advance")
    let duplicate = InstrumentPluginFixture(); duplicate.owner.apply(); let firstRead = duplicate.replies[0]
    duplicate.answer(duplicate.document()); firstRead(duplicate.document())
    try require(duplicate.calls.count == 2, "Duplicate preflight catalogue response cannot start another metadata read")
    duplicate.answer(duplicate.metadata()); let writeReply = duplicate.replies[0], receipt = duplicate.assignmentReceipt()
    duplicate.answer(receipt); writeReply(receipt); duplicate.answer(duplicate.document()); duplicate.answer(duplicate.metadata()); writeReply(receipt)
    try require(duplicate.writes.count == 1 && duplicate.saved.count == 1 && !duplicate.owner.needsCompletion,
      "Duplicate write callbacks cannot replay or reveal another assignment")

    let unknown = InstrumentPluginFixture(creating: true); unknown.prepare(); _ = unknown.createReceipt(); unknown.answer([:])
    unknown.owner.apply(); unknown.answer(unknown.document()); unknown.owner.accept()
    try require(unknown.writes.count == 1 && unknown.owner.target.slot == 0 && unknown.saved.isEmpty && !unknown.owner.needsCompletion,
      "Unknown creation Review never infers a new target or replays an append")
    let partial = InstrumentPluginFixture(creating: true); partial.prepare(); partial.completeCreation(); _ = partial.assignmentReceipt(); partial.answer([:])
    partial.owner.apply(); partial.answer(partial.document()); partial.answer(partial.metadata()); partial.owner.accept()
    partial.prepare()
    try require(partial.writes.filter { $0.0 == "instrument.create" }.count == 1 && partial.writes.last?.1["instrument"] as? Int == 3,
      "After uncertain assignment, a new explicit Apply reuses the verified created instrument")
    partial.completeAssignment(changed: false)

    for creating in [false, true] {
      let f = InstrumentPluginFixture(creating: creating); f.prepare()
      if creating { f.completeCreation() }
      f.answer([:]); let slot = f.owner.target.slot
      f.identities[slot] = "replacement-at-same-slot"; f.advance()
      var retired = 0; f.owner.onRetired = { retired += 1 }
      f.owner.review(); f.answer(f.document())
      try require(f.owner.canAccept && f.owner.observedRemoval && f.replies.isEmpty,
        "Review can acknowledge removal by stable identity without reading a replacement slot")
      f.advance(); f.owner.accept()
      try require(retired == 0 && f.owner.needsCompletion && !f.owner.canAccept, "Stale removal evidence cannot discard the retained owner")
      f.owner.review(); f.answer(f.document()); let writes = f.writes.count
      f.owner.accept(); f.owner.accept(); f.owner.apply(); f.owner.reload()
      try require(retired == 1 && f.owner.retired && !f.owner.needsCompletion && f.writes.count == writes && f.replies.isEmpty,
        "Explicit removal acknowledgement retires once without replay, retargeting or future writes")
    }
    let dangling = InstrumentPluginFixture(); dangling.prepare(); _ = dangling.assignmentReceipt(); dangling.answer([:])
    dangling.identities[1] = nil; dangling.advance(); dangling.owner.review(); dangling.answer(dangling.document())
    try require(dangling.owner.canAccept && dangling.owner.observedRemoval,
      "A retained plugin route explicitly marked unavailable does not prevent reviewing the removed instrument")
    dangling.owner.accept()
    try require(dangling.owner.retired && dangling.writes.count == 1 && dangling.routes[1] != nil,
      "Acknowledging removal leaves the actual dangling route unchanged for separate repair")

    for stage in ["catalogue", "mapping", "created-catalogue", "created-mapping", "assigned-catalogue", "assigned-mapping"] {
      let f = InstrumentPluginFixture(creating: stage.hasPrefix("created")); f.owner.apply()
      if stage == "catalogue" { f.answer([:]) }
      else if stage == "mapping" { f.answer(f.document()); f.answer([:]) }
      else if stage.hasPrefix("created") {
        f.answer(f.document()); f.answer(f.createReceipt())
        if stage == "created-mapping" { f.answer(f.document()) }; f.answer([:])
      } else {
        f.answer(f.document()); f.answer(f.metadata()); f.answer(f.assignmentReceipt())
        if stage == "assigned-mapping" { f.answer(f.document()) }; f.answer([:])
      }
      try require(f.owner.needsCompletion == !(stage == "catalogue" || stage == "mapping"), "Only writes retain uncertainty after failed \(stage) read")
    }
    for malformed in ["unknown", "document", "revision", "boolean", "slot", "plugin", "channel", "dryRun", "changed"] {
      let f = InstrumentPluginFixture(); f.prepare(); var response = f.assignmentReceipt(), result = response["result"] as! [String: Any], data = result["data"] as! [String: Any]
      switch malformed {
      case "unknown": response = [:]
      case "document": result["documentId"] = "other"
      case "revision": result["revision"] = "song:1"
      case "boolean": result["changed"] = 1
      case "slot": data["instrument"] = 2
      case "plugin": data["plugin"] = "other"
      case "channel": data["channel"] = true
      case "dryRun": data["dryRun"] = true
      default: data["wouldChange"] = false
      }
      if malformed != "unknown" { result["data"] = data; response["result"] = result }
      f.answer(response); try require(f.owner.canReview && f.saved.isEmpty && f.writes.count == 1, "Malformed \(malformed) receipt cannot complete or repeat assignment")
    }
    for changed in ["identity", "route", "other-route", "mapping"] {
      let f = InstrumentPluginFixture(); f.prepare(); f.answer(f.assignmentReceipt())
      if changed == "identity" { f.identities[1] = "replacement" }
      if changed == "route" { f.routes[1] = .init(plugin: "other", channel: 2) }
      if changed == "other-route" { f.routes[2] = nil }
      f.answer(f.document())
      if changed == "mapping" { f.mappings[1] = Array(repeating: 2, count: 128); f.answer(f.metadata()) }
      try require(f.owner.canReview && f.saved.isEmpty, "Changed \(changed) readback retains the result")
    }
    let refusal = InstrumentPluginFixture(); refusal.prepare(); refusal.answer(["error": ["data": ["writeOutcome": "notCommitted"]]])
    try require(!refusal.owner.needsCompletion, "Explicit non-commit releases the assignment owner while fields remain")
    let stale = InstrumentPluginFixture(); stale.owner.apply(); stale.advance(); stale.answer(stale.document())
    try require(stale.writes.isEmpty && !stale.owner.needsCompletion, "Queued preflight cannot rebase the captured revision")
    let newer = InstrumentPluginFixture(creating: true); newer.prepare(); newer.answer(newer.createReceipt()); newer.answer(newer.document())
    newer.input = .init(generation: 2, fields: .init(name: "Later name", plugin: "other", channel: 8)); newer.answer(newer.metadata(3))
    try require(newer.owner.canReview && newer.writes.count == 1 && newer.owner.target.slot == 3, "Newer fields after creation prevent automatic assignment and keep the verified target")
    newer.review(); newer.owner.accept()
    try require(newer.input.fields.name == "Later name" && newer.writes.count == 1, "Acknowledgement preserves newer fields without assigning them")
    for change in ["focus", "cursor", "composition", "fields"] {
      let f = InstrumentPluginFixture(); f.prepare(); f.answer(f.assignmentReceipt()); f.answer(f.document())
      switch change {
      case "focus": f.reveal = false
      case "cursor": f.revealContext = "cursor:2"
      case "composition": f.composing = true
      default: f.input = .init(generation: 2, fields: f.input.fields)
      }
      f.answer(f.metadata()); try require(!f.owner.needsCompletion && f.saved.isEmpty, "Verified completion preserves newer \(change)")
    }
    let observed = InstrumentPluginFixture(); observed.prepare(); observed.answer([:]); observed.review(); observed.advance(); observed.owner.accept()
    try require(observed.owner.canReview && !observed.owner.canAccept, "Stale Review cannot acknowledge the earlier result")
    observed.context = .init(document: "other", revision: "other:1"); let calls = observed.calls.count; observed.owner.review()
    try require(observed.calls.count == calls, "Review does not inspect a replacement song")
    let moved = InstrumentPluginFixture(); moved.advance()
    moved.identities[4] = moved.identities.removeValue(forKey: 1); moved.names[4] = moved.names.removeValue(forKey: 1); moved.mappings[4] = moved.mappings.removeValue(forKey: 1)
    moved.routes[4] = .init(plugin: "synth", channel: 11)
    moved.owner.reload(); moved.answer(moved.document()); moved.answer(moved.metadata(4))
    try require(moved.owner.target.slot == 4 && moved.owner.target.identity == "i1" && moved.owner.target.context == moved.context && moved.writes.isEmpty,
      "Explicit Reload resolves the original stable identity after reordering without a write")
    let editing = InstrumentPluginFixture(); editing.owner.reload(); editing.answer(editing.document())
    editing.input = .init(generation: 2, fields: .init(name: "Newer", plugin: "other", channel: 4)); editing.answer(editing.metadata())
    try require(editing.observed.isEmpty && !editing.owner.needsCompletion && editing.input.fields.plugin == "other", "Newer fields prevent asynchronous Reload adoption")

    let seed = InstrumentPluginFixture(), editor = InstrumentPluginEditor(instrument: 1, model: seed.model)
    let native = InstrumentPluginFixture(workflow: editor.workflow)
    let host = NSWindow(contentRect: editor.frame, styleMask: [.titled], backing: .buffered, defer: false); host.contentView = editor
    editor.plugin.selectItem(at: 1); editor.channel.selectItem(withTag: 3)
    try require(editor.hasDraft && editor.needsCompletion, "Unsubmitted native plugin/channel changes join departure admission")
    editor.apply(); native.answer(native.document()); native.answer(native.metadata()); native.answer([:])
    try require(editor.workflow.canReview && editor.needsCompletion && editor.reviewButton.isEnabled, "Uncertain native assignment exposes retained recovery")
    editor.workflow.review(); native.answer(native.document()); native.answer(native.metadata()); editor.workflow.accept()
    try require(editor.hasDraft && editor.rawFields.plugin == "synth", "Read-only acknowledgement does not label an uncommitted desired route as saved")
    editor.workflow.reload(); native.answer(native.document()); native.answer(native.metadata())
    try require(!editor.hasDraft && !editor.needsCompletion && editor.rawFields.plugin.isEmpty, "Explicit Reload adopts current routing and clears the discarded draft")
    let menu = ContextActions.controls(in: editor, title: "Plugin instrument")
    try require(menu.items.contains { $0.title == "Review assignment" }, "Assignment recovery is exposed to command search")
    editor.plugin.selectItem(at: 1); editor.apply(); native.answer(native.document()); native.answer(native.metadata()); native.answer([:])
    native.identities[1] = nil; native.advance(); editor.workflow.review(); native.answer(native.document())
    try require(editor.acceptButton.title == "Discard removed assignment" && editor.needsCompletion,
      "The native removal action explicitly names discarded work before releasing it")
    editor.workflow.accept()
    try require(!editor.needsCompletion && !editor.hasDraft && editor.workflow.retired,
      "Discarding a removed assignment releases native Save/history/departure admission")
    host.makeFirstResponder(nil); host.orderOut(nil)

    let creator = InstrumentPluginEditor(instrument: 0, model: seed.model), typed = InstrumentPluginFixture(creating: true, workflow: creator.workflow)
    let window = NSWindow(contentRect: creator.frame, styleMask: [.titled], backing: .buffered, defer: false); window.contentView = creator; window.makeFirstResponder(creator.name)
    guard let field = creator.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Plugin name field editor unavailable") }
    field.setMarkedText("New trigger", selectedRange: NSRange(location: 1, length: 1), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    creator.apply(); creator.workflow.reload()
    try require(typed.calls.isEmpty && field.hasMarkedText() && creator.name.currentEditor() === field, "Create and Reload preserve active native name composition")
    field.unmarkText(); creator.apply(); typed.answer(typed.document()); typed.answer([:]); creator.workflow.review(); typed.answer(typed.document())
    field.insertText("newer raw name", replacementRange: NSRange(location: 0, length: field.string.utf16.count)); let caret = field.selectedRange(); creator.workflow.accept()
    try require(typed.writes.count == 1 && field.string == "newer raw name" && creator.name.currentEditor() === field && field.selectedRange() == caret,
      "Unknown creation acknowledgement preserves newer native text, caret and focus")
    window.makeFirstResponder(nil); window.orderOut(nil)
    let created = InstrumentPluginEditor(instrument: 0, model: seed.model), typedCreated = InstrumentPluginFixture(creating: true, workflow: created.workflow)
    let createdWindow = NSWindow(contentRect: created.frame, styleMask: [.titled], backing: .buffered, defer: false); createdWindow.contentView = created; createdWindow.makeFirstResponder(created.name)
    guard let nameEditor = created.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Created name field editor unavailable") }
    created.apply(); typedCreated.answer(typedCreated.document()); typedCreated.completeCreation()
    nameEditor.insertText("later name", replacementRange: NSRange(location: 0, length: nameEditor.string.utf16.count))
    _ = typedCreated.assignmentReceipt(); typedCreated.answer([:]); created.apply()
    try require(typedCreated.calls.last?.0 == "document.get" && typedCreated.writes.count == 2, "Apply reaches read-only Review despite a newer name after creation")
    typedCreated.answer(typedCreated.document()); typedCreated.answer(typedCreated.metadata()); created.workflow.accept()
    try require(created.hasDraft && created.name.currentEditor() === nameEditor && nameEditor.string == "later name", "Creation/assignment completion retains a newer focused name")
    nameEditor.setSelectedRange(NSRange(location: 2, length: 0)); created.workflow.reload(); typedCreated.answer(typedCreated.document()); typedCreated.answer(typedCreated.metadata())
    try require(!created.hasDraft && created.name.currentEditor() === nameEditor && nameEditor.string == "Fixture synth" && nameEditor.selectedRange() == NSRange(location: 2, length: 0),
      "Explicit Reload restores the created name while preserving current field focus and caret")
    createdWindow.makeFirstResponder(nil); createdWindow.orderOut(nil)
    print("PASS plugin instrument completion: captured stages, strict receipts, read-only recovery, stable target, keymap/routes, newer input and composition")
  }
}
