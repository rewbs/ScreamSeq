import AppKit

struct InstrumentPluginFields: Equatable {
  let name: String, plugin: String, channel: Int
}
struct InstrumentPluginDraft: Equatable {
  let generation: UInt64, fields: InstrumentPluginFields
}
struct InstrumentPluginTarget: Equatable {
  let context: SampleRecordingContext, slot: Int, identity: String
}
struct InstrumentPluginRoute: Equatable {
  let plugin: String, channel: Int
}
struct InstrumentPluginSnapshot {
  let context: SampleRecordingContext, instruments: [Int: String]
  let plugins: [(identity: String, name: String)], routes: [Int: InstrumentPluginRoute]
  static func read(_ response: [String: Any], context: SampleRecordingContext) -> Self? {
    guard let data = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(data),
      let instruments = data["instruments"] as? [[String: Any]], let plugins = data["nativePlugins"] as? [[String: Any]] else { return nil }
    let identities = Dictionary(uniqueKeysWithValues: instruments.map { (SampleRecordingRead.integer($0["index"])!, $0["id"] as! String) })
    var choices = [(identity: String, name: String)](), routes = [Int: InstrumentPluginRoute](), seen = Set<String>()
    for plugin in plugins {
      guard let id = plugin["instanceID"] as? String, !id.isEmpty, seen.insert(id).inserted,
        let instrument = SampleRecordingRead.boolean(plugin["isInstrument"]), let name = plugin["name"] as? String else { return nil }
      if instrument { choices.append((id, name)) }
      var assignments: [[String: Any]]
      if let raw = plugin["instrumentAssignments"] { guard let list = raw as? [[String: Any]] else { return nil }; assignments = list }
      else if let legacy = SampleRecordingRead.integer(plugin["instrument"]), legacy > 0 { assignments = [["instrument": legacy, "channel": 1]] }
      else { assignments = [] }
      for assignment in assignments {
        guard instrument, let slot = SampleRecordingRead.integer(assignment["instrument"], maximum: 65535, minimum: 1),
          identities[slot] != nil, routes[slot] == nil,
          let channel = SampleRecordingRead.integer(assignment["channel"], maximum: 16, minimum: 1) else { return nil }
        routes[slot] = .init(plugin: id, channel: channel)
      }
    }
    return .init(context: context, instruments: identities, plugins: choices, routes: routes)
  }
  static func mapping(_ response: [String: Any], context: SampleRecordingContext) -> (name: String, mapping: [Int])? {
    guard let data = SampleRecordingRead.result(response, context: context), let name = data["name"] as? String,
      let raw = data["mapping"] as? [Any], raw.count == 128 else { return nil }
    let mapping = raw.compactMap { SampleRecordingRead.integer($0, maximum: 65535) }
    return mapping.count == raw.count ? (name, mapping) : nil
  }
}

final class InstrumentPluginOperation {
  enum Phase { case preparing, preparingMapping, creating, createdCatalogue, createdMapping, assigning, assignedCatalogue, assignedMapping, retained, reviewing, reviewingMapping, reloading, reloadingMapping }
  let draft: InstrumentPluginDraft, original: InstrumentPluginTarget
  let revealContext: String
  var target: InstrumentPluginTarget, phase: Phase = .preparing, params = [String: Any]()
  var wrote = false, before: InstrumentPluginSnapshot?, mapping: [Int]?
  var observation: (snapshot: InstrumentPluginSnapshot, slot: Int, name: String)?
  init(target: InstrumentPluginTarget, draft: InstrumentPluginDraft, revealContext: String = "") { self.target = target; original = target; self.draft = draft; self.revealContext = revealContext }
}

final class InstrumentPluginWorkflow {
  var onContext: (() -> SampleRecordingContext?)?, onDraft: (() -> InstrumentPluginDraft)?
  var onRevealContext: (() -> String)?
  var canBegin: (() -> Bool)?, canReveal: (() -> Bool)?, composing: (() -> Bool)?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onObserved: ((InstrumentPluginSnapshot, Int, String, Bool) -> Void)?, onSaved: ((Int, String) -> Void)?, onChange: (() -> Void)?
  private(set) var target: InstrumentPluginTarget, operation: InstrumentPluginOperation?, status = ""
  init(target: InstrumentPluginTarget) { self.target = target }
  var needsCompletion: Bool { operation != nil }
  var pending: Bool { operation.map { $0.phase != .retained } ?? false }
  var canReview: Bool { operation?.phase == .retained }
  var canAccept: Bool { canReview && operation?.observation != nil }
  private func show(_ message: String) { status = message; onChange?() }
  private func current(_ owner: InstrumentPluginOperation) -> Bool {
    operation === owner && onContext?() == owner.target.context
  }
  func originalInputIsCurrent(_ owner: InstrumentPluginOperation) -> Bool {
    current(owner) && onDraft?() == owner.draft && composing?() != true && canBegin?() == true
  }
  func apply() {
    if operation != nil { if canReview { review() }; return }
    guard onContext?() == target.context, !target.context.document.isEmpty, !target.context.revision.isEmpty,
      canBegin?() == true, composing?() != true, let draft = onDraft?(), let onRequest,
      (1...16).contains(draft.fields.channel), draft.fields.name.count <= 200, !draft.fields.name.contains("\0"),
      target.slot > 0 || !draft.fields.plugin.isEmpty else { show("Song or fields changed. Reload the captured instrument before applying."); return }
    let owner = InstrumentPluginOperation(target: target, draft: draft, revealContext: onRevealContext?() ?? ""); operation = owner; show("Checking the captured instrument and routing…")
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .preparing else { return }
      guard self.originalInputIsCurrent(owner), let snapshot = InstrumentPluginSnapshot.read(response, context: owner.target.context),
        owner.target.slot == 0 || snapshot.instruments[owner.target.slot] == owner.target.identity,
        owner.draft.fields.plugin.isEmpty || snapshot.plugins.contains(where: { $0.identity == owner.draft.fields.plugin }) else {
        self.refuse(owner, "Captured instrument, plugin or fields changed. Nothing submitted; Reload before applying."); return
      }
      owner.before = snapshot
      if owner.target.slot == 0 { self.create(owner) }
      else { self.readMapping(owner, phase: .preparingMapping) { name, mapping in owner.mapping = mapping; self.assign(owner) } }
    }
  }
  private func refuse(_ owner: InstrumentPluginOperation, _ message: String) {
    guard operation === owner else { return }
    if owner.wrote { retain(owner, message) } else { operation = nil; show(message) }
  }
  private func retain(_ owner: InstrumentPluginOperation, _ message: String) {
    guard operation === owner else { return }; owner.phase = .retained; owner.observation = nil; show(message)
  }
  private func readMapping(_ owner: InstrumentPluginOperation, phase: InstrumentPluginOperation.Phase,
    completion: @escaping (String, [Int]) -> Void) {
    guard let onRequest else { refuse(owner, "Instrument readback is unavailable."); return }
    owner.phase = phase
    onRequest("instrument.get", ["instrument": owner.target.slot]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == phase else { return }
      guard self.current(owner), let value = InstrumentPluginSnapshot.mapping(response, context: owner.target.context) else {
        self.refuse(owner, "Instrument readback failed or changed. Review the current song without repeating a write."); return
      }
      completion(value.name, value.mapping)
    }
  }
  private func create(_ owner: InstrumentPluginOperation) {
    guard originalInputIsCurrent(owner), let onRequest else { refuse(owner, "Fields or song changed before creation. No creation submitted."); return }
    owner.params = ["empty": true, "name": owner.draft.fields.name, "expectedRevision": owner.target.context.revision]
    owner.phase = .creating; show("Creating the captured plugin instrument…")
    onRequest("instrument.create", owner.params) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .creating else { return }
      if self.notCommitted(response) { self.refuse(owner, "Creation was not committed. Fields kept; Reload or apply a new request."); return }
      owner.wrote = true
      guard let (context, data) = self.receipt(response, owner: owner), SampleRecordingRead.boolean(data["empty"]) == true,
        SampleRecordingRead.boolean(data["dryRun"]) == false, context.revision != owner.target.context.revision,
        let slot = SampleRecordingRead.integer(data["instrument"], maximum: 65535, minimum: 1), owner.before?.instruments[slot] == nil else {
        self.retain(owner, "Creation outcome is unverified. Review before another creation; no write will be repeated."); return
      }
      owner.target = .init(context: context, slot: slot, identity: ""); owner.phase = .createdCatalogue
      onRequest("document.get", [:]) { [weak self] response in
        guard let self, self.operation === owner, owner.phase == .createdCatalogue else { return }
        guard self.current(owner), let snapshot = InstrumentPluginSnapshot.read(response, context: context), let id = snapshot.instruments[slot],
          owner.before?.instruments.values.contains(id) == false else {
          self.retain(owner, "Created instrument identity could not be verified. Review without creating again."); return
        }
        owner.target = .init(context: context, slot: slot, identity: id); self.target = owner.target; owner.before = snapshot
        self.readMapping(owner, phase: .createdMapping) { name, mapping in
          guard name == owner.draft.fields.name, mapping.allSatisfy({ $0 == 0 }) else {
            self.retain(owner, "Created instrument is not the captured empty trigger. Review its current state."); return
          }
          owner.mapping = mapping; self.assign(owner)
        }
      }
    }
  }
  private func notCommitted(_ response: [String: Any]) -> Bool {
    ((response["error"] as? [String: Any])?["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted"
  }
  private func receipt(_ response: [String: Any], owner: InstrumentPluginOperation) -> (SampleRecordingContext, [String: Any])? {
    guard response["error"] == nil, let result = response["result"] as? [String: Any], result["documentId"] as? String == owner.target.context.document,
      let changed = SampleRecordingRead.boolean(result["changed"]), let revision = result["revision"] as? String, !revision.isEmpty,
      changed ? revision != owner.target.context.revision : revision == owner.target.context.revision,
      let data = result["data"] as? [String: Any] else { return nil }
    return (.init(document: owner.target.context.document, revision: revision), data)
  }
  private func assign(_ owner: InstrumentPluginOperation) {
    guard originalInputIsCurrent(owner), let onRequest, owner.mapping != nil else {
      refuse(owner, "Instrument created or read, but newer fields or song changes prevent assignment. Review the current state."); return
    }
    let fields = owner.draft.fields
    owner.params = ["instrument": owner.target.slot, "plugin": fields.plugin, "channel": fields.channel, "expectedRevision": owner.target.context.revision]
    owner.phase = .assigning; show("Saving the captured plugin assignment…")
    onRequest("instrument.plugin.set", owner.params) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .assigning else { return }
      if self.notCommitted(response) { self.refuse(owner, "Assignment was not committed. Captured fields and any created instrument are kept."); return }
      owner.wrote = true
      guard let (context, data) = self.receipt(response, owner: owner), SampleRecordingRead.boolean(data["dryRun"]) == false,
        SampleRecordingRead.boolean(data["wouldChange"]) == SampleRecordingRead.boolean((response["result"] as? [String: Any])?["changed"]),
        SampleRecordingRead.integer(data["instrument"]) == owner.target.slot, data["plugin"] as? String == fields.plugin,
        SampleRecordingRead.integer(data["channel"]) == fields.channel else {
        self.retain(owner, "Assignment outcome is unverified. Review without repeating it or creating another instrument."); return
      }
      owner.target = .init(context: context, slot: owner.target.slot, identity: owner.target.identity); owner.phase = .assignedCatalogue
      onRequest("document.get", [:]) { [weak self] response in
        guard let self, self.operation === owner, owner.phase == .assignedCatalogue else { return }
        guard self.current(owner), let snapshot = InstrumentPluginSnapshot.read(response, context: context),
          snapshot.instruments[owner.target.slot] == owner.target.identity,
          snapshot.routes[owner.target.slot] == (fields.plugin.isEmpty ? nil : InstrumentPluginRoute(plugin: fields.plugin, channel: fields.channel)),
          snapshot.routes.filter({ $0.key != owner.target.slot }) == owner.before?.routes.filter({ $0.key != owner.target.slot }) else {
          self.retain(owner, "Assignment readback does not match the captured route. Review the retained result."); return
        }
        self.readMapping(owner, phase: .assignedMapping) { name, mapping in
          guard mapping == owner.mapping else { self.retain(owner, "Instrument sample mapping changed during assignment. Review the current song."); return }
          let reveal = self.onDraft?() == owner.draft && self.composing?() != true && self.canReveal?() == true && (self.onRevealContext?() ?? "") == owner.revealContext
          self.target = owner.target; self.operation = nil; self.onObserved?(snapshot, owner.target.slot, name, false)
          if reveal { self.onSaved?(owner.target.slot, owner.target.identity) }
          self.show("Plugin assignment verified · sample mapping kept · Undo restores the previous assignment." + (reveal ? "" : " Current focus and fields kept."))
        }
      }
    }
  }
  func review() {
    guard let owner = operation, owner.phase == .retained, composing?() != true,
      let context = onContext?(), context.document == owner.original.context.document else { return }
    owner.observation = nil; inspect(owner, context: context, reload: false)
  }
  func reload() {
    guard operation == nil, composing?() != true, let context = onContext?(), context.document == target.context.document, let draft = onDraft?() else { return }
    let owner = InstrumentPluginOperation(target: target, draft: draft); operation = owner; inspect(owner, context: context, reload: true)
  }
  private func inspect(_ owner: InstrumentPluginOperation, context: SampleRecordingContext, reload: Bool) {
    guard let onRequest else { refuse(owner, "Readback is unavailable."); return }
    owner.phase = reload ? .reloading : .reviewing; let phase = owner.phase; show("Inspecting the captured instrument and current routing…")
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == phase else { return }
      guard self.onContext?() == context, self.composing?() != true, let snapshot = InstrumentPluginSnapshot.read(response, context: context) else {
        self.refuse(owner, "Catalogue readback failed or changed. Fields and retained result kept."); return
      }
      let slot = self.target.identity.isEmpty ? 0 : snapshot.instruments.first(where: { $0.value == self.target.identity })?.key
      guard let slot else { self.refuse(owner, "The captured instrument was removed. Its retained result and fields are kept."); return }
      func observed(_ name: String) {
        guard self.onContext?() == context, self.composing?() != true else { self.refuse(owner, "Song changed while reading. Review again."); return }
        if reload {
          guard self.onDraft?() == owner.draft else { self.operation = nil; self.show("Newer fields kept. Reload again to discard them explicitly."); return }
          self.target = .init(context: context, slot: slot, identity: slot == 0 ? "" : self.target.identity)
          self.operation = nil; self.onObserved?(snapshot, slot, name, true); self.show("Current instrument and plugin choices loaded.")
        } else {
          owner.observation = (snapshot, slot, name); owner.phase = .retained
          let observed: String
          if slot == 0 { observed = "Catalogue inspected: \(snapshot.instruments.count) instruments. The earlier creation remains unattributed." }
          else if let route = snapshot.routes[slot] {
            let pluginName = snapshot.plugins.first(where: { $0.identity == route.plugin })?.name ?? route.plugin
            observed = "Observed instrument \(slot): \(pluginName) · MIDI \(route.channel)."
          } else { observed = "Observed instrument \(slot): sample keymap only, no plugin." }
          self.show(observed + " Accept observed result acknowledges this without repeating creation or assignment.")
        }
      }
      if slot == 0 { observed(owner.draft.fields.name); return }
      owner.target = .init(context: context, slot: slot, identity: self.target.identity)
      self.readMapping(owner, phase: reload ? .reloadingMapping : .reviewingMapping) { name, _ in observed(name) }
    }
  }
  func accept() {
    guard let owner = operation, owner.phase == .retained, let observed = owner.observation else { return }
    guard onContext?() == observed.snapshot.context, composing?() != true else {
      owner.observation = nil; show("Song changed since Review. Review again before accepting."); return
    }
    target = .init(context: observed.snapshot.context, slot: observed.slot, identity: observed.slot == 0 ? "" : target.identity)
    operation = nil; onObserved?(observed.snapshot, observed.slot, observed.name, false)
    show("Earlier result acknowledged. No creation or assignment repeated; current fields and focus kept.")
  }
}
