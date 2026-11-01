import AppKit

struct SampleLibraryImportTarget: Equatable {
  let context: SampleRecordingContext
  let samples: [Int: String], instruments: [Int: String], relatedState: String
  var instrumentDraft: InstrumentCreationDraft? = nil
}

struct SampleBrowserDraft: Equatable {
  let generation: UInt64, fields: [String: String], paths: [String], tags: [String]
}

final class SampleLibraryImportOperation {
  enum Phase { case choosing, writing, reading, retained, reviewing }
  let target: SampleLibraryImportTarget, draft: SampleBrowserDraft, instruments: Bool
  var params: [String: Any], phase: Phase = .choosing
  var receipt: (context: SampleRecordingContext, items: [(sample: Int, instrument: Int)])?
  var observation: SampleRecordingContext?
  init(target: SampleLibraryImportTarget, draft: SampleBrowserDraft, instruments: Bool) {
    self.target = target; self.draft = draft; self.instruments = instruments
    params = ["expectedRevision": target.context.revision, "createInstruments": instruments]
  }
}

final class SampleLibraryImportWorkflow {
  var onTarget: (() -> SampleLibraryImportTarget?)?, onDraft: (() -> SampleBrowserDraft)?
  var canBegin: (() -> Bool)?, canReveal: (() -> Bool)?, composing: (() -> Bool)?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onImported: ((Int, String, Int) -> Void)?, onChange: (() -> Void)?
  private(set) var operation: SampleLibraryImportOperation?, status = ""
  var needsCompletion: Bool { operation != nil }
  var busy: Bool { operation.map { $0.phase != .retained } ?? false }
  var canReview: Bool { operation?.phase == .retained }
  var canAccept: Bool { canReview && operation?.observation != nil }
  private func show(_ text: String) { status = text; onChange?() }
  func begin(instruments: Bool) -> SampleLibraryImportOperation? {
    if operation != nil {
      if canReview { review() } else { show("Wait for the retained sample import or file chooser.") }; return nil
    }
    guard canBegin?() == true, composing?() != true, let target = onTarget?(), let draft = onDraft?(),
      !target.context.document.isEmpty, !target.context.revision.isEmpty else {
      show("Finish or review the retained sample, instrument and library work before importing."); return nil
    }
    let owner = SampleLibraryImportOperation(target: target, draft: draft, instruments: instruments)
    operation = owner; show("Sample import keeps the original song and selection while choosing files."); return owner
  }
  func originalInputIsCurrent(_ owner: SampleLibraryImportOperation) -> Bool {
    operation === owner && onTarget?() == owner.target && onDraft?() == owner.draft && canBegin?() == true && composing?() != true
  }
  func load(_ paths: [String], instruments: Bool) {
    guard let owner = begin(instruments: instruments) else { return }; finishChoosing(owner, paths: paths)
  }
  func finishChoosing(_ owner: SampleLibraryImportOperation, paths: [String]?) {
    guard operation === owner, owner.phase == .choosing else { return }
    guard let paths, !paths.isEmpty else { operation = nil; show("Sample import cancelled. Song and browser fields kept."); return }
    guard paths.count <= 128, paths.allSatisfy({ ($0 as NSString).isAbsolutePath && $0.count <= 4096 && !$0.contains("\0") }),
      originalInputIsCurrent(owner), let onRequest else {
      operation = nil; show("Files, song or browser input changed. No samples imported; choose again."); return
    }
    owner.params["paths"] = paths; owner.phase = .writing; show("Loading \(paths.count) samples…")
    onRequest("sample.importMany", owner.params) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .writing else { return }
      if let error = response["error"] as? [String: Any], (error["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted" {
        self.operation = nil; self.show((error["message"] as? String ?? "Import rejected.") + " No write committed; selection kept."); return
      }
      guard response["error"] == nil, let result = response["result"] as? [String: Any],
        result["documentId"] as? String == owner.target.context.document, SampleRecordingRead.boolean(result["changed"]) == true,
        let revision = result["revision"] as? String, !revision.isEmpty, revision != owner.target.context.revision,
        let data = result["data"] as? [String: Any], SampleRecordingRead.boolean(data["dryRun"]) == false,
        let items = data["samples"] as? [[String: Any]], items.count == paths.count,
        SampleRecordingRead.integer(data["count"], maximum: 128, minimum: 1) == items.count else {
        self.retain(owner, "Import result is unverified. Review before another import; no write will be repeated."); return
      }
      var slots = [(sample: Int, instrument: Int)](), samples = Set<Int>(), instruments = Set<Int>()
      for (item, path) in zip(items, paths) {
        guard item["path"] as? String == path,
          let sample = SampleRecordingRead.integer(item["sample"], maximum: 65535, minimum: 1), samples.insert(sample).inserted,
          let instrument = SampleRecordingRead.integer(item["instrument"], maximum: 65535),
          owner.instruments ? (instrument > 0 && instruments.insert(instrument).inserted) : instrument == 0 else {
          self.retain(owner, "Import receipt does not match the captured files. Review the current catalogue."); return
        }
        slots.append((sample, instrument))
      }
      let context = SampleRecordingContext(document: owner.target.context.document, revision: revision)
      owner.receipt = (context, slots)
      guard self.onTarget?()?.context == context else { self.retain(owner, "Song changed after import. Review the retained result."); return }
      owner.phase = .reading; self.show("Checking imported samples and instruments…")
      onRequest("document.get", [:]) { [weak self] response in
        guard let self, self.operation === owner, owner.phase == .reading else { return }
        guard self.onTarget?()?.context == context, let data = SampleRecordingRead.result(response, context: context),
          SampleRecordingRead.validAssets(data), let sampleItems = data["samples"] as? [[String: Any]],
          let instrumentItems = data["instruments"] as? [[String: Any]] else {
          self.retain(owner, "Import readback failed or changed. Review without repeating the import."); return
        }
        let sampleIDs = Dictionary(uniqueKeysWithValues: sampleItems.map { (SampleRecordingRead.integer($0["index"])!, $0["id"] as! String) })
        let instrumentIDs = Dictionary(uniqueKeysWithValues: instrumentItems.map { (SampleRecordingRead.integer($0["index"])!, $0["id"] as! String) })
        guard slots.allSatisfy({ item in
          guard let sampleID = sampleIDs[item.sample], owner.target.samples[item.sample] == nil, !owner.target.samples.values.contains(sampleID) else { return false }
          if item.instrument == 0 { return true }
          guard let instrumentID = instrumentIDs[item.instrument] else { return false }
          return owner.target.instruments[item.instrument] == nil && !owner.target.instruments.values.contains(instrumentID)
        }), let first = slots.first, let identity = sampleIDs[first.sample] else {
          self.retain(owner, "Imported asset identities could not be verified. Review the retained result."); return
        }
        let reveal = self.onDraft?() == owner.draft && self.onTarget?()?.relatedState == owner.target.relatedState &&
          self.onTarget?()?.instrumentDraft == owner.target.instrumentDraft &&
          self.canBegin?() == true && self.canReveal?() != false && self.composing?() != true
        self.operation = nil
        if reveal { self.onImported?(first.sample, identity, first.instrument) }
        self.show("Loaded \(slots.count) samples · one Undo step." + (reveal ? "" : " Current fields and selection kept."))
      }
    }
  }
  private func retain(_ owner: SampleLibraryImportOperation, _ text: String) {
    guard operation === owner else { return }; owner.phase = .retained; owner.observation = nil; show(text)
  }
  func review() {
    guard let owner = operation, owner.phase == .retained, let onRequest else { return }
    owner.observation = nil
    guard composing?() != true, let context = onTarget?()?.context, context.document == owner.target.context.document else {
      show("Return to the original song and finish composing before reviewing this import."); return
    }
    owner.phase = .reviewing; show("Inspecting current samples and instruments…")
    onRequest("document.get", [:]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .reviewing else { return }
      guard self.onTarget?()?.context == context, self.composing?() != true,
        let data = SampleRecordingRead.result(response, context: context), SampleRecordingRead.validAssets(data) else {
        self.retain(owner, "Catalogue readback failed or changed. Import remains retained; Review again."); return
      }
      owner.observation = context; owner.phase = .retained
      self.show("Current catalogue inspected. Accept observed import acknowledges the earlier result without repeating it or changing selection.")
    }
  }
  func accept() {
    guard let owner = operation, owner.phase == .retained, let observed = owner.observation else { return }
    guard composing?() != true, onTarget?()?.context == observed else { owner.observation = nil; show("Song changed since Review. Review again before accepting."); return }
    operation = nil; show("Earlier import acknowledged. No write repeated; browser fields and selection kept.")
  }
}

// Library roots and scans belong to the application, not a song's history.
final class SampleLibraryChangeOperation {
  enum Phase { case choosing, writing, reading, retained, reviewing }
  let method: String, revision: String, draft: SampleBrowserDraft, roots: [String]
  var params: [String: Any], phase: Phase = .choosing, returned = false
  var observation: [String: Any]?
  init(method: String, revision: String, draft: SampleBrowserDraft, roots: [String], params: [String: Any]) {
    self.method = method; self.revision = revision; self.draft = draft; self.roots = roots; self.params = params
    self.params["expectedLibraryRevision"] = revision
  }
}

final class SampleLibraryChangeWorkflow {
  var onState: (() -> [String: Any])?, onDraft: (() -> SampleBrowserDraft)?
  var canBegin: (() -> Bool)?, composing: (() -> Bool)?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onAdopt: (([String: Any]) -> Void)?, onChange: (() -> Void)?
  private(set) var operation: SampleLibraryChangeOperation?, status = ""
  var needsCompletion: Bool { operation != nil }
  var canReview: Bool { operation?.phase == .retained }
  var canAccept: Bool { canReview && operation?.observation != nil }
  private func show(_ text: String) { status = text; onChange?() }
  static func validState(_ data: [String: Any]) -> Bool {
    guard let revision = data["libraryRevision"] as? String, !revision.isEmpty,
      let roots = data["roots"] as? [String], roots.count <= 32,
      roots.allSatisfy({ ($0 as NSString).isAbsolutePath && !$0.contains("\0") }),
      data["warnings"] is [String], data["extensions"] is [String],
      data["error"] is NSNull || data["error"] is String,
      SampleRecordingRead.integer(data["count"], maximum: 250_000) != nil,
      SampleRecordingRead.boolean(data["indexing"]) != nil, SampleRecordingRead.boolean(data["ready"]) != nil else { return false }
    return true
  }
  func begin(_ method: String, params: [String: Any] = [:]) -> SampleLibraryChangeOperation? {
    if operation != nil { if canReview { review() } else { show("Wait for the retained library request or folder chooser.") }; return nil }
    guard ["sample.library.roots.set", "sample.library.rescan"].contains(method), canBegin?() == true, composing?() != true,
      let state = onState?(), Self.validState(state), let revision = state["libraryRevision"] as? String, let draft = onDraft?() else {
      show("Finish or review the current library work before changing folders or rescanning."); return nil
    }
    let owner = SampleLibraryChangeOperation(method: method, revision: revision, draft: draft, roots: state["roots"] as! [String], params: params)
    operation = owner; show("Library request retains its original folders and revision."); return owner
  }
  func originalInputIsCurrent(_ owner: SampleLibraryChangeOperation) -> Bool {
    operation === owner && onState?()["libraryRevision"] as? String == owner.revision && onDraft?() == owner.draft && canBegin?() == true && composing?() != true
  }
  func finishChoosing(_ owner: SampleLibraryChangeOperation, roots: [String]?) {
    guard operation === owner, owner.phase == .choosing else { return }
    guard let roots else { operation = nil; show("Folder chooser cancelled. Library and browser fields kept."); return }
    owner.params["roots"] = roots; submit(owner)
  }
  func submit(_ owner: SampleLibraryChangeOperation) {
    guard operation === owner, owner.phase == .choosing else { return }
    guard originalInputIsCurrent(owner), let onRequest else { operation = nil; show("Library or browser input changed. No library request submitted."); return }
    owner.phase = .writing; show("Updating sample library…")
    onRequest(owner.method, owner.params) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == .writing else { return }
      if let error = response["error"] as? [String: Any], (error["data"] as? [String: Any])?["writeOutcome"] as? String == "notCommitted" {
        self.operation = nil; self.show((error["message"] as? String ?? "Library request rejected.") + " No request committed."); return
      }
      var expectedRoots = owner.roots
      if owner.method == "sample.library.roots.set" {
        guard let paths = owner.params["roots"] as? [String] else {
          self.retain(owner, "Library folders could not be verified. Review without repeating the request."); return
        }
        expectedRoots = []
        for path in paths {
          let root = SampleLibraryIndex.canonicalPath(path)
          if !expectedRoots.contains(root) { expectedRoots.append(root) }
        }
      }
      guard let data = self.read(response), data["roots"] as? [String] == expectedRoots,
        owner.method != "sample.library.roots.set" || data["libraryRevision"] as? String != owner.revision else {
        self.retain(owner, "Library outcome is unverified. Review before another change; no request will be repeated."); return
      }
      owner.returned = true; owner.phase = .reading
      self.inspect(owner, known: true)
    }
  }
  private func read(_ response: [String: Any]) -> [String: Any]? {
    guard response["error"] == nil, let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any],
      Self.validState(data), SampleRecordingRead.boolean(result["changed"]) == false,
      result["revision"] as? String == "library:" + (data["libraryRevision"] as! String) else { return nil }
    return data
  }
  private func retain(_ owner: SampleLibraryChangeOperation, _ text: String) {
    guard operation === owner else { return }; owner.phase = .retained; owner.observation = nil; show(text)
  }
  private func inspect(_ owner: SampleLibraryChangeOperation, known: Bool) {
    guard let onRequest else { retain(owner, "Library readback is unavailable. Review again."); return }
    let expectedPhase = owner.phase
    onRequest("sample.library.get", [:]) { [weak self] response in
      guard let self, self.operation === owner, owner.phase == expectedPhase else { return }
      guard self.composing?() != true, let data = self.read(response), let current = self.onState?(),
        NSDictionary(dictionary: current).isEqual(to: data) else { self.retain(owner, "Library readback failed or changed. Review again; request remains retained."); return }
      if known && self.onDraft?() == owner.draft {
        self.operation = nil; self.onAdopt?(data)
        self.show("Library request completed." + (data["indexing"] as? Bool == true ? " Indexing continues in the background." : " Song and Undo history unchanged."))
      } else {
        owner.observation = data; owner.phase = .retained
        self.show("Current library inspected. Accept observed library adopts it without repeating the earlier request.")
      }
    }
  }
  func review() {
    guard let owner = operation, owner.phase == .retained else { return }
    owner.observation = nil
    guard composing?() != true else { show("Finish composing before reviewing the library request."); return }
    owner.phase = .reviewing; show("Inspecting the current sample library…"); inspect(owner, known: owner.returned)
  }
  func accept() {
    guard let owner = operation, owner.phase == .retained, let observed = owner.observation else { return }
    guard composing?() != true, let current = onState?(), NSDictionary(dictionary: current).isEqual(to: observed) else {
      owner.observation = nil; show("Library changed since Review. Review again before accepting."); return
    }
    operation = nil; onAdopt?(observed); show("Earlier library request acknowledged. No scan or folder change repeated.")
  }
}
