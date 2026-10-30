import AppKit
import CoreFoundation

struct SampleRecordingContext: Equatable {
  let document: String, revision: String
}
struct SampleRecordingSetup: Equatable {
  let name: String, instrument: Bool
}

/// A native write owns its original payload until a receipt or an explicit
/// read-only reconciliation resolves it. No completion path resubmits a write.
final class SampleRecordingOperation {
  let method: String, params: [String: Any], context: SampleRecordingContext
  let take: String, setup: SampleRecordingSetup, generation: UInt64
  var receipt: [String: Any]?, receiptRevision: String?
  struct Observation {
    let context: SampleRecordingContext, generation: UInt64, setup: SampleRecordingSetup
    let assets: [String: Any], take: [String: Any]
  }
  var observation: Observation?
  var isCommit: Bool { method == "sample.recording.commit" }
  init(_ method: String, params: [String: Any], context: SampleRecordingContext,
       take: String, setup: SampleRecordingSetup, generation: UInt64) {
    self.method = method; self.params = params; self.context = context
    self.take = take; self.setup = setup; self.generation = generation
  }
}

enum SampleRecordingRead {
  static func integer(_ value: Any?, maximum: Int = Int.max, minimum: Int = 0) -> Int? {
    guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
      number.doubleValue.isFinite, number.doubleValue.rounded() == number.doubleValue,
      number.doubleValue >= Double(minimum), number.doubleValue < Double(Int.max),
      number.doubleValue <= Double(maximum) else { return nil }
    return number.intValue
  }
  static func boolean(_ value: Any?) -> Bool? {
    guard let number = value as? NSNumber, CFGetTypeID(number) == CFBooleanGetTypeID() else { return nil }
    return number.boolValue
  }
  static func identity(_ data: [String: Any]) -> String? {
    data["take"] is NSNull ? "" : data["take"] as? String
  }
  static func validTake(_ data: [String: Any]) -> Bool {
    guard let take = identity(data), let capturing = boolean(data["capturing"]),
      let frames = integer(data["frames"]) else { return false }
    if take.isEmpty { return !capturing && frames == 0 }
    return (data["documentId"] as? String)?.isEmpty == false && (data["baseRevision"] as? String)?.isEmpty == false
  }
  static func validAssets(_ data: [String: Any]) -> Bool {
    for key in ["samples", "instruments"] {
      guard let assets = data[key] as? [[String: Any]], assets.count <= 65535 else { return false }
      var ids = Set<String>(), slots = Set<Int>()
      for asset in assets {
        guard let id = asset["id"] as? String, !id.isEmpty,
          let slot = integer(asset["index"], maximum: 65535, minimum: 1),
          ids.insert(id).inserted, slots.insert(slot).inserted else { return false }
      }
    }
    return true
  }
  static func result(_ response: [String: Any], context: SampleRecordingContext? = nil) -> [String: Any]? {
    guard response["error"] == nil, let result = response["result"] as? [String: Any] else { return nil }
    if let context {
      guard result["documentId"] as? String == context.document, result["revision"] as? String == context.revision else { return nil }
    }
    return result["data"] as? [String: Any]
  }
}

extension SampleRecordingView {
  var rawSetup: SampleRecordingSetup {
    SampleRecordingSetup(name: (name.currentEditor() as? NSTextView)?.string ?? name.stringValue, instrument: instrument.state == .on)
  }
  var composingName: Bool { (name.currentEditor() as? NSTextView)?.hasMarkedText() == true }
  var currentContext: SampleRecordingContext? {
    guard let document = onDocument?(), !document.isEmpty, let revision = onRevision?(), !revision.isEmpty else { return nil }
    return SampleRecordingContext(document: document, revision: revision)
  }
  func establishSetupBaseline() {
    baseline = rawSetup; observedSetup = baseline; setupContext = currentContext
  }
  func syncSetup() {
    let raw = rawSetup
    guard raw != observedSetup else { return }
    if observedSetup == baseline { setupContext = operation?.context ?? permissionContext ?? currentContext }
    observedSetup = raw; setupGeneration &+= 1
  }
  @objc func setupChanged() { syncSetup(); updateControls() }
  func controlTextDidChange(_ notification: Notification) { setupChanged() }
  func requireSetupDocument(_ context: SampleRecordingContext) -> Bool {
    guard rawSetup == baseline || setupContext?.document == context.document else {
      status.stringValue = "Sample setup belongs to the previous song. Discard setup before recording into another song."; return false
    }
    return true
  }
  func resetSetup() {
    guard !pending, !permissionPending, operation == nil else { return }
    // Explicit local discard may end composition; background reads never do.
    if name.currentEditor() != nil { window?.makeFirstResponder(self) }
    name.stringValue = "Recording"; instrument.state = .off; setupGeneration &+= 1
    establishSetupBaseline(); updateControls()
    status.stringValue = "Sample name and output reset. The retained take is unchanged."
  }
  func retain(_ owner: SampleRecordingOperation, _ message: String) {
    guard operation === owner else { return }
    pending = false; closing = false
    status.stringValue = message + " Review the result before another recording operation; the write will not be repeated."
    updateControls()
  }
  func submit(_ method: String, params: [String: Any], context: SampleRecordingContext? = nil) {
    guard operation == nil, !pending, !permissionPending, let onRequest else { return }
    syncSetup()
    let captured = context ?? currentContext ?? SampleRecordingContext(document: takeDocument, revision: "")
    let owner = SampleRecordingOperation(method, params: params, context: captured,
      take: take, setup: rawSetup, generation: setupGeneration)
    operation = owner; pending = true; pollGeneration &+= 1; updateControls()
    let send = { [weak self] in
      guard let self, self.operation === owner else { return }
      if params["expectedRevision"] != nil, self.onDocument?() != captured.document {
        self.operation = nil; self.pending = false; self.closing = false
        self.status.stringValue = "The song changed before recording could start or be added. The operation was not sent."
        self.updateControls(); return
      }
      onRequest(method, params) { [weak self] response in
        guard let self, self.operation === owner else { return }
        if let error = response["error"] as? [String: Any] {
          if (error["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted" {
            self.operation = nil; self.pending = false; self.closing = false
            self.status.stringValue = error["message"] as? String ?? "The recording operation was not committed. Your take and setup are retained."
            self.updateControls()
          } else { self.retain(owner, error["message"] as? String ?? "The recording outcome is uncertain.") }
          return
        }
        guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
          self.retain(owner, "No recording receipt was returned."); return
        }
        if owner.isCommit {
          guard result["documentId"] as? String == captured.document,
            let revision = result["revision"] as? String, !revision.isEmpty,
            data["take"] as? String == owner.take,
            SampleRecordingRead.integer(data["sample"], maximum: 65535, minimum: 1) != nil,
            SampleRecordingRead.integer(data["instrument"], maximum: 65535) != nil,
            SampleRecordingRead.boolean(data["dryRun"]) == false else {
            self.retain(owner, "The Add receipt could not be matched to this take and song."); return
          }
          owner.receipt = data; owner.receiptRevision = revision
          self.finishCommit(owner)
        } else if method == "sample.recording.discard" {
          guard data["take"] as? String == owner.take, SampleRecordingRead.boolean(data["discarded"]) == true else {
            self.retain(owner, "The discard receipt could not be matched to this take."); return
          }
          self.readTake(owner) { current, _ in
            guard SampleRecordingRead.identity(current) != owner.take else { self.retain(owner, "The discarded take is still present."); return }
            self.apply(current); self.operation = nil; self.pending = false
            self.status.stringValue = "Take discarded. Current take inspected."; self.updateControls()
          }
        } else {
          guard SampleRecordingRead.validTake(data), SampleRecordingRead.identity(data)?.isEmpty == false,
            (method == "sample.recording.start" ? data["documentId"] as? String == captured.document : data["take"] as? String == owner.take),
            method != "sample.recording.stop" || data["capturing"] as? Bool == false else {
            self.retain(owner, "The recording receipt could not be matched to the requested take."); return
          }
          self.apply(data); self.operation = nil; self.pending = false; self.updateControls()
          if self.closing { self.closing = false; self.window?.close() }
        }
      }
    }
    if polling { afterPoll = send } else { send() }
  }
  func readTake(_ owner: SampleRecordingOperation, done: @escaping ([String: Any], [String: Any]) -> Void) {
    guard operation === owner, let onRequest else { return }
    pending = true; updateControls()
    onRequest("sample.recording.get", [:]) { [weak self] response in
      guard let self, self.operation === owner else { return }
      guard let data = SampleRecordingRead.result(response), SampleRecordingRead.validTake(data) else {
        self.retain(owner, "Current take could not be read."); return
      }
      done(data, response)
    }
  }
  func finishCommit(_ owner: SampleRecordingOperation) {
    guard let receipt = owner.receipt, let revision = owner.receiptRevision else { return }
    readTake(owner) { [weak self] current, response in
      guard let self else { return }
      guard SampleRecordingRead.identity(current) != owner.take else {
        self.retain(owner, "The Add result still has its original take."); return
      }
      self.syncSetup()
      let accepted = SampleRecordingContext(document: owner.context.document, revision: revision)
      let select = self.currentContext == accepted && SampleRecordingRead.result(response, context: accepted) != nil
      // A different API-created take is observed, never cleared or discarded.
      self.apply(current); self.baseline = owner.setup
      self.operation = nil; self.pending = false
      let sample = receipt["sample"] as! Int, instrument = receipt["instrument"] as! Int
      self.status.stringValue = "Added sample \(sample). " + (select ? "⌘Z undoes the sample and instrument together." : "The song changed since completion; selection was left unchanged.")
      self.updateControls()
      if select { self.onAdded?(sample, instrument) }
    }
  }
  func reviewOperation() {
    guard !pending, !permissionPending, let owner = operation else { return }
    if owner.isCommit, owner.receipt != nil { finishCommit(owner); return }
    if !owner.isCommit {
      readTake(owner) { [weak self] current, _ in
        guard let self else { return }
        self.apply(current); self.operation = nil; self.pending = false
        self.status.stringValue = "Current take inspected. The earlier operation was not repeated; choose the next action for this observed take."
        self.updateControls()
      }
      return
    }
    syncSetup(); owner.observation = nil
    guard !composingName, let context = currentContext, context.document == owner.context.document, let onRequest else {
      retain(owner, "Finish name composition and return to the original song before Review."); return
    }
    let generation = setupGeneration, setup = rawSetup
    pending = true; updateControls()
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner else { return }
      guard let assets = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(assets) else {
        self.retain(owner, "Current asset catalogue could not be validated."); return
      }
      self.readTake(owner) { [weak self] current, response in
        guard let self else { return }; self.syncSetup()
        guard !self.composingName, self.currentContext == context, self.setupGeneration == generation, self.rawSetup == setup,
          SampleRecordingRead.result(response, context: context) != nil else {
          self.retain(owner, "The song or setup changed during Review."); return
        }
        owner.observation = .init(context: context, generation: generation, setup: setup, assets: assets, take: current)
        self.pending = false
        self.status.stringValue = "Current assets and take inspected. The earlier Add remains unverified; Accept observed state acknowledges this without adding again."
        self.updateControls()
      }
    }
  }
  func observationMatches(_ observation: SampleRecordingOperation.Observation) -> Bool {
    syncSetup()
    return !composingName && currentContext == observation.context && setupGeneration == observation.generation && rawSetup == observation.setup
  }
  func acceptObservation() {
    guard !pending, let owner = operation, owner.isCommit, owner.receipt == nil, let observation = owner.observation else { return }
    guard observationMatches(observation) else { owner.observation = nil; retain(owner, "The song or setup changed after Review."); return }
    readTake(owner) { [weak self] current, response in
      guard let self else { return }
      guard self.observationMatches(observation), SampleRecordingRead.result(response, context: observation.context) != nil,
        SampleRecordingRead.identity(current) == SampleRecordingRead.identity(observation.take),
        current["capturing"] as? Bool == observation.take["capturing"] as? Bool else {
        owner.observation = nil; self.retain(owner, "The song, setup or take changed after Review."); return
      }
      // Frames/meters may advance during capture; identity and capture state
      // must still match. Acknowledgement makes no claim about authorship.
      self.apply(current); self.operation = nil; self.pending = false
      self.status.stringValue = "Unverified Add acknowledged. Current take and raw setup retained; no write was repeated."
      self.updateControls()
    }
  }
}
