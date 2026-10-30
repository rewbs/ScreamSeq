import AppKit

final class SampleLibraryImportFixture {
  let owner: SampleLibraryImportWorkflow
  var target = SampleLibraryImportTarget(context: .init(document: "song", revision: "song:1"), samples: [1: "s1"], instruments: [1: "i1"], relatedState: "editor:0")
  var draft = SampleBrowserDraft(generation: 1, fields: ["search": "kick"], paths: ["/samples/kick.wav"], tags: ["drums"])
  var allowed = true, composing = false, reveal = true
  var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), revealed = [(Int, String, Int)]()
  var writes: [(String, [String: Any])] { calls.filter { $0.0 == "sample.importMany" } }
  init(_ workflow: SampleLibraryImportWorkflow = SampleLibraryImportWorkflow()) {
    owner = workflow
    owner.onTarget = { [weak self] in self?.target }
    if owner.onDraft == nil { owner.onDraft = { [weak self] in self!.draft } }
    if owner.composing == nil { owner.composing = { [weak self] in self?.composing == true } }
    owner.canBegin = { [weak self] in self?.allowed == true }
    owner.canReveal = { [weak self] in self?.reveal == true }
    owner.onRequest = { [weak self] in self?.calls.append(($0, $1)); self?.replies.append($2) }
    owner.onImported = { [weak self] in self?.revealed.append(($0, $1, $2)) }
  }
  func advance(_ revision: String = "song:2", document: String = "song", related: String = "editor:0") {
    target = .init(context: .init(document: document, revision: revision), samples: target.samples, instruments: target.instruments, relatedState: related)
  }
  func answer(_ response: [String: Any]) { replies.removeFirst()(response) }
  func receipt() -> [String: Any] {
    let params = writes.last!.1, paths = params["paths"] as! [String], instruments = params["createInstruments"] as! Bool
    return ["result": ["documentId": target.context.document, "revision": target.context.revision, "changed": true,
      "data": ["count": paths.count, "dryRun": false, "samples": paths.enumerated().map { ["path": $1, "sample": $0 + 2, "instrument": instruments ? $0 + 2 : 0] as [String: Any] }]]]
  }
  func catalogue(_ sampleItems: [[String: Any]]? = nil) -> [String: Any] {
    let paths = writes.last!.1["paths"] as! [String], instruments = writes.last!.1["createInstruments"] as! Bool
    let importedSamples: [[String: Any]] = paths.indices.map { ["index": $0 + 2, "id": "s\($0 + 2)"] }
    let importedInstruments: [[String: Any]] = instruments ? paths.indices.map { ["index": $0 + 2, "id": "i\($0 + 2)"] } : []
    let samples: [[String: Any]] = sampleItems ?? ([["index": 1, "id": "s1"]] + importedSamples)
    let mapped: [[String: Any]] = [["index": 1, "id": "i1"]] + importedInstruments
    return ["result": ["documentId": target.context.document, "revision": target.context.revision,
      "data": ["samples": samples, "instruments": mapped]]]
  }
  func complete() { advance("song:\(writes.count + 1)"); answer(receipt()); answer(catalogue()) }
}

private final class LibraryChangeFixture {
  let owner = SampleLibraryChangeWorkflow()
  var state: [String: Any] = ["roots": ["/samples"], "count": 4, "indexing": false, "ready": true,
    "libraryRevision": "l1", "warnings": [String](), "extensions": ["wav"], "error": NSNull()]
  var draft = SampleBrowserDraft(generation: 1, fields: ["search": "kick"], paths: [], tags: [])
  var allowed = true, composing = false, adopted = [[String: Any]]()
  var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
  init() {
    owner.onState = { [weak self] in self?.state ?? [:] }; owner.onDraft = { [weak self] in self!.draft }
    owner.canBegin = { [weak self] in self?.allowed == true }; owner.composing = { [weak self] in self?.composing == true }
    owner.onRequest = { [weak self] in self?.calls.append(($0, $1)); self?.replies.append($2) }
    owner.onAdopt = { [weak self] in self?.adopted.append($0) }
  }
  func answer(_ response: [String: Any]) { replies.removeFirst()(response) }
  func response(_ data: [String: Any]? = nil) -> [String: Any] {
    let data = data ?? state
    return ["result": ["revision": "library:" + (data["libraryRevision"] as? String ?? ""), "changed": false, "data": data]]
  }
}

extension InterfaceTests {
  static func sampleLibraryCompletionChecks() throws {
    let happy = SampleLibraryImportFixture()
    happy.owner.load(["/samples/a.wav", "/samples/b.wav"], instruments: true)
    happy.owner.load(["/samples/duplicate.wav"], instruments: false)
    try require(happy.writes.count == 1 && happy.writes[0].1["expectedRevision"] as? String == "song:1" && happy.owner.busy,
      "One import owns exact paths/mapping/revision and excludes duplicate appends")
    let duplicate = happy.replies[0]; happy.advance(); happy.answer(happy.receipt()); duplicate(happy.receipt())
    try require(happy.calls.count == 2 && happy.calls.last?.0 == "document.get" && happy.revealed.isEmpty, "Receipt requires catalogue readback; duplicate response cannot replay")
    happy.answer(happy.catalogue()); duplicate(happy.receipt())
    try require(!happy.owner.needsCompletion && happy.revealed.count == 1 && happy.revealed[0].0 == 2 && happy.revealed[0].1 == "s2" && happy.revealed[0].2 == 2,
      "Verified new asset identity completes one batch and reveals only the first imported sample")

    for change in ["document", "revision", "raw", "generation", "related", "composition", "draft"] {
      let f = SampleLibraryImportFixture(), owner = f.owner.begin(instruments: true)!
      try require(f.owner.begin(instruments: false) == nil, "Chooser excludes another chooser")
      switch change {
      case "document": f.advance("other:1", document: "other")
      case "revision": f.advance()
      case "related": f.advance("song:1", related: "changed")
      case "composition": f.composing = true
      case "draft": f.allowed = false
      default: f.draft = .init(generation: 2, fields: ["search": change == "raw" ? "snare" : "kick"], paths: f.draft.paths, tags: f.draft.tags)
      }
      try require(!f.owner.originalInputIsCurrent(owner), "Queued admission rejects changed \(change)")
      f.owner.finishChoosing(owner, paths: ["/samples/a.wav"])
      try require(f.calls.isEmpty && !f.owner.needsCompletion, "Stale chooser never rebases \(change)")
    }
    let cancel = SampleLibraryImportFixture(), first = cancel.owner.begin(instruments: false)!
    cancel.owner.finishChoosing(first, paths: nil); let second = cancel.owner.begin(instruments: true)!
    cancel.owner.finishChoosing(first, paths: ["/samples/late.wav"])
    try require(cancel.owner.operation === second && cancel.calls.isEmpty, "Late Cancel callback cannot replace new chooser")
    cancel.owner.finishChoosing(second, paths: ["relative.wav"])
    try require(cancel.calls.isEmpty && !cancel.owner.needsCompletion, "Invalid path is rejected before mutation")

    for bad in ["unknown", "busy", "boolean", "same-revision", "count", "dry-run", "path", "slot", "instrument", "duplicate"] {
      let f = SampleLibraryImportFixture(); f.owner.load(["/samples/a.wav", "/samples/b.wav"], instruments: true); f.advance()
      var response = f.receipt(), result = response["result"] as! [String: Any], data = result["data"] as! [String: Any]
      var items = data["samples"] as! [[String: Any]]
      switch bad {
      case "unknown": response = [:]
      case "busy": response = ["error": ["code": -32002, "message": "Unknown completion"]]
      case "boolean": result["changed"] = 1
      case "same-revision": result["revision"] = "song:1"
      case "count": data["count"] = true
      case "dry-run": data["dryRun"] = true
      case "path": items[0]["path"] = "/wrong.wav"
      case "slot": items[0]["sample"] = 2.5
      case "instrument": items[0]["instrument"] = 0
      default: items[1]["sample"] = 2
      }
      if bad != "unknown" && bad != "busy" { data["samples"] = items; result["data"] = data; response["result"] = result }
      f.answer(response)
      try require(f.owner.canReview && f.writes.count == 1 && f.revealed.isEmpty, "Malformed \(bad) cannot release or replay an import")
      f.owner.load(["/samples/again.wav"], instruments: false); f.owner.review()
      try require(f.calls.count == 2 && f.calls.last?.0 == "document.get", "Repeated import becomes one read-only Review")
      f.answer(f.catalogue()); f.draft = .init(generation: 2, fields: ["search": "newer"], paths: [], tags: [])
      f.owner.accept()
      try require(!f.owner.needsCompletion && f.writes.count == 1 && f.revealed.isEmpty && f.draft.fields["search"] == "newer", "Accept preserves newer input without guessing result authorship")
    }
    let rejection = SampleLibraryImportFixture(); rejection.owner.load(["/samples/a.wav"], instruments: false)
    rejection.answer(["error": ["message": "Rejected", "data": ["writeOutcome": "notCommitted"]]])
    try require(!rejection.owner.needsCompletion, "Explicit notCommitted permits fresh intent")
    for invalid in [[["index": 1, "id": "s1"], ["index": 2, "id": "s1"]], [["index": 1, "id": "s1"], ["index": 2, "id": "s2"], ["index": 2, "id": "s3"]]] {
      let f = SampleLibraryImportFixture(); f.owner.load(["/samples/a.wav"], instruments: false); f.advance(); f.answer(f.receipt()); f.answer(f.catalogue(invalid))
      try require(f.owner.canReview && f.revealed.isEmpty, "Duplicate catalogue IDs/slots remain retained without a dictionary trap")
    }
    let reused = SampleLibraryImportFixture()
    reused.target = .init(context: reused.target.context, samples: [1: "s1", 2: "s2"], instruments: [1: "i1"], relatedState: "editor:0")
    reused.owner.load(["/samples/a.wav"], instruments: false); reused.advance(); reused.answer(reused.receipt()); reused.answer(reused.catalogue())
    try require(reused.owner.canReview && reused.revealed.isEmpty, "A pre-existing slot cannot be attributed to the import")
    for changed in ["raw", "composition", "focus", "related"] {
      let f = SampleLibraryImportFixture(); f.owner.load(["/samples/a.wav"], instruments: false); f.advance(); f.answer(f.receipt())
      switch changed {
      case "composition": f.composing = true
      case "focus": f.reveal = false
      case "related": f.advance(related: "new-target")
      default: f.draft = .init(generation: 2, fields: f.draft.fields, paths: f.draft.paths, tags: f.draft.tags)
      }
      f.answer(f.catalogue()); try require(!f.owner.needsCompletion && f.revealed.isEmpty, "Known import preserves newer \(changed)")
    }
    let stale = SampleLibraryImportFixture(); stale.owner.load(["/samples/a.wav"], instruments: false); stale.answer([:]); stale.advance("other:1", document: "other")
    stale.owner.review(); try require(stale.calls.count == 1 && stale.owner.canReview, "Original-song Review never reads the replacement song")
    stale.advance(); stale.owner.review(); stale.answer([:]); try require(stale.owner.canReview && !stale.owner.canAccept, "Failed Review retains import")
    stale.owner.review(); stale.answer(stale.catalogue()); stale.advance("song:3"); stale.owner.accept()
    try require(stale.owner.canReview && !stale.owner.canAccept, "Stale observation cannot acknowledge import")

    for method in ["sample.library.roots.set", "sample.library.rescan"] {
      for unknown in [false, true] {
        let f = LibraryChangeFixture(), owner = f.owner.begin(method, params: method.hasSuffix("set") ? ["roots": ["/samples/new"]] : [:])!
        f.owner.submit(owner); f.owner.submit(owner)
        try require(f.calls.count == 1 && f.calls[0].1["expectedLibraryRevision"] as? String == "l1", "Library write retains exact original revision")
        f.state["libraryRevision"] = "l2"; f.state["indexing"] = true
        if method.hasSuffix("set") { f.state["roots"] = ["/samples/new"] }
        f.answer(unknown ? [:] : f.response())
        if !unknown { f.answer([:]) }
        try require(f.owner.canReview && f.adopted.isEmpty, "Unknown outcome or failed read retains library owner")
        f.owner.review(); var malformed = f.state; malformed["roots"] = false; f.answer(f.response(malformed))
        try require(f.owner.canReview && !f.owner.canAccept, "Malformed library observation cannot release owner")
        f.owner.review(); f.answer(f.response())
        if unknown {
          try require(f.owner.canAccept && f.adopted.isEmpty, "Unknown library read requires explicit acknowledgement")
          f.state["indexing"] = false; f.owner.accept(); try require(f.owner.canReview && !f.owner.canAccept, "Same-revision status change invalidates library observation")
          f.owner.review(); f.answer(f.response()); f.owner.accept()
        }
        try require(!f.owner.needsCompletion && f.adopted.count == 1 && f.calls.filter { !$0.0.hasSuffix(".get") }.count == 1,
          "Library Review/Accept performs reads only and never acquires song history")
      }
    }
    for method in ["sample.library.roots.set", "sample.library.rescan"] {
      let f = LibraryChangeFixture(), owner = f.owner.begin(method, params: ["roots": ["/samples/new"]])!
      f.owner.submit(owner); f.state["libraryRevision"] = "l2"; f.state["roots"] = ["/wrong"]
      f.answer(f.response())
      try require(f.owner.canReview && f.calls.count == 1 && f.adopted.isEmpty,
        "A structurally valid receipt for different folders cannot complete the captured library request")
      f.owner.review(); f.answer(f.response())
      try require(f.owner.canAccept && f.adopted.isEmpty, "Mismatched receipt remains unknown after read-only Review")
      f.owner.accept(); try require(f.adopted.count == 1, "Explicit acknowledgement adopts current folders without replay")
    }
    let normalized = LibraryChangeFixture()
    let folderWrite = normalized.owner.begin("sample.library.roots.set", params: ["roots": ["/tmp", "/tmp"]])!
    normalized.owner.submit(folderWrite); normalized.state["roots"] = [SampleLibraryIndex.canonicalPath("/tmp")]
    normalized.state["libraryRevision"] = "l2"; normalized.answer(normalized.response()); normalized.answer(normalized.response())
    try require(!normalized.owner.needsCompletion && normalized.adopted.count == 1, "Receipt accepts the service's canonical, deduplicated roots")
    let unchanged = LibraryChangeFixture(), unchangedWrite = unchanged.owner.begin("sample.library.roots.set", params: ["roots": ["/samples"]])!
    unchanged.owner.submit(unchangedWrite); unchanged.answer(unchanged.response())
    try require(unchanged.owner.canReview && unchanged.adopted.isEmpty, "A folder write must advance the library revision")
    let picker = LibraryChangeFixture(), owner = picker.owner.begin("sample.library.roots.set", params: ["roots": ["/samples"]])!
    picker.state["libraryRevision"] = "newer"; picker.owner.finishChoosing(owner, roots: ["/samples", "/new"])
    try require(!picker.owner.needsCompletion && picker.calls.isEmpty, "Folder chooser cannot rebase the original library revision")
    let newer = LibraryChangeFixture(), write = newer.owner.begin("sample.library.rescan")!; newer.owner.submit(write)
    newer.draft = .init(generation: 2, fields: ["search": "newer raw"], paths: [], tags: [])
    newer.answer(newer.response()); newer.answer(newer.response())
    try require(newer.owner.canAccept && newer.adopted.isEmpty, "Known library completion retains newer raw input for explicit adoption")
    newer.composing = true; newer.owner.accept(); try require(newer.owner.needsCompletion, "Library acknowledgement does not cancel composition")
    newer.composing = false; newer.owner.review(); newer.answer(newer.response()); newer.owner.accept()
    try require(newer.draft.fields["search"] == "newer raw" && newer.adopted.count == 1, "Explicit library adoption keeps newer fields")

    let browser = sampleBrowserFixture(), f = SampleLibraryImportFixture(browser.imports)
    browser.loadSelection(); f.answer([:]); let text = browser.status.stringValue
    try require(browser.needsDocumentCompletion && !browser.needsApplicationCompletion, "Song import participates in Save/history/departure even while browser is hidden")
    browser.updateLibrary(["roots": ["/changed"], "count": 0, "indexing": true])
    browser.updateResults(.init(items: [], total: 0, tags: [], offset: 0))
    try require(browser.entries.count == 15 && browser.status.stringValue == text && browser.importReview.isEnabled && !browser.chooseFilesButton.isEnabled,
      "Background index/search results cannot erase retained import state or enable another chooser")
    let menu = ContextActions.controls(in: browser, title: "Sample library")
    try require(menu.items.contains { $0.title == "Review import" }, "Recovery action is exposed to command search")
    f.advance(); browser.imports.review(); f.answer(f.catalogue()); browser.imports.accept()
    try require(!browser.importing && browser.chooseFilesButton.isEnabled, "Acknowledgement releases native controls without another append")
    browser.libraryChanges.onState = { LibraryChangeFixture().state }; browser.libraryChanges.canBegin = { true }
    let libraryChooser = browser.libraryChanges.begin("sample.library.roots.set")!
    try require(!browser.needsDocumentCompletion && browser.needsApplicationCompletion, "App-wide library chooser protects Quit without acquiring a song draft")
    browser.libraryChanges.finishChoosing(libraryChooser, roots: nil)
    try require(!browser.needsApplicationCompletion, "Cancel releases the app-wide chooser")

    let queued = sampleBrowserFixture(), queuedImport = SampleLibraryImportFixture(queued.imports)
    var queries = [SampleLibraryQuery]()
    queued.onSearch = { query, _ in queries.append(query) }
    queued.loadSelection(); queuedImport.answer([:])
    queued.search.stringValue = "newer query"; queued.requestSearch(loadWhenReady: true)
    try require(queries.isEmpty, "A retained import postpones the latest search")
    queued.imports.review(); queuedImport.answer(queuedImport.catalogue()); queued.imports.accept()
    try require(queries.count == 1 && queries[0].text == "newer query" && queuedImport.writes.count == 1,
      "Completion resumes the latest query without replaying the old load-when-ready intent")
    let debounced = sampleBrowserFixture(), cancelledImport = SampleLibraryImportFixture(debounced.imports)
    var afterCancel = [SampleLibraryQuery]()
    debounced.onSearch = { query, _ in afterCancel.append(query) }
    debounced.search.stringValue = "before chooser"; debounced.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: debounced.search))
    let chooser = debounced.imports.begin(instruments: false)!
    debounced.imports.finishChoosing(chooser, paths: nil)
    try require(afterCancel.count == 1 && afterCancel[0].text == "before chooser" && cancelledImport.writes.isEmpty,
      "Cancelling a chooser resumes the search whose debounce it interrupted")
    let active = sampleBrowserFixture(), activeImport = SampleLibraryImportFixture(active.imports)
    var activeSearches = [(SampleLibraryResults) -> Void]()
    active.onSearch = { _, reply in activeSearches.append(reply) }; active.requestSearch()
    let activeChooser = active.imports.begin(instruments: false)!
    active.imports.finishChoosing(activeChooser, paths: nil)
    try require(activeSearches.count == 2 && activeImport.writes.isEmpty, "Chooser completion resumes an interrupted in-flight search")
    activeSearches[0](.init(items: [], total: 0, tags: [], offset: 0))
    try require(active.entries.count == 15, "Retired pre-chooser response cannot overwrite the resumed search")

    let input = sampleBrowserFixture(), native = SampleLibraryImportFixture(input.imports)
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1140, height: 820), styleMask: [.titled], backing: .buffered, defer: false)
    window.contentView = input; window.makeFirstResponder(input.search)
    guard let editor = input.search.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Sample browser field editor did not attach") }
    editor.setMarkedText("kick", selectedRange: NSRange(location: 1, length: 1), replacementRange: NSRange(location: 0, length: editor.string.utf16.count))
    input.loadSelection(); input.updateLibrary(["roots": ["/changed"], "count": 0, "indexing": true])
    try require(native.calls.isEmpty && editor.hasMarkedText() && input.search.currentEditor() === editor && editor.string == "kick",
      "Import and background library updates preserve active native composition")
    editor.unmarkText(); input.loadSelection(); native.answer([:]); native.advance(); input.imports.review(); native.answer(native.catalogue())
    editor.insertText("newer", replacementRange: NSRange(location: 0, length: editor.string.utf16.count))
    let caret = editor.selectedRange(); input.imports.accept()
    try require(native.writes.count == 1 && editor.string == "newer" && input.search.currentEditor() === editor && editor.selectedRange() == caret,
      "Read-only acceptance keeps newer active search text, focus and caret")
    window.makeFirstResponder(nil); window.orderOut(nil)
    print("PASS sample library completion: guarded chooser/import, strict receipts, read-only recovery, independent library requests and retained browser state")
  }
}
