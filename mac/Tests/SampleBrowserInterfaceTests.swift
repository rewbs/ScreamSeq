import AppKit
extension InterfaceTests {
  static func multisampleGroupFixture() -> MultisampleGroup {
    let entries = (0...108).map { key -> SampleLibraryEntry in
      let pitch = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][key%12]
      let name = String(format: "%03d Clav Junos %@%d.wav", key, pitch, key/12-2)
      return SampleLibraryEntry(path: "/Samples/Junos From Mars/WAV/01. Keys/Clav/"+name, root: "/Samples", name: name,
        folders: ["Samples","Junos From Mars","WAV","01. Keys","Clav"], bytes: 438000, modified: 0)
    }
    return MultisampleGroup.detect(entries)[0]
  }
  static func multisampleImportFixture() -> MultisampleImportView {
    MultisampleImportView(group: multisampleGroupFixture(), revision: "song:1")
  }
  static func multisampleImportChecks() throws {
    let view = multisampleImportFixture()
    try require(view.rootNotes.first == 1 && view.rootNotes.last == 109 && view.importButton.isEnabled, "Review offers the entire Clav family with numbered-octave alignment")
    view.octave.selectItem(at: 4); view.changeOffset()
    try require(!view.importButton.isEnabled && view.status.stringValue.contains("outside"), "Unsupported octave mapping is shown and cannot import")
    view.octave.selectItem(at: 6); view.changeOffset(); view.name.stringValue = "My clav"
    var requests = [[String:Any]](), replies = [([String:Any])->Void](), applied = 0
    view.onRequest = { method, params, reply in
      try! require(method == "instrument.importMultisample", "Review uses the same agent import method")
      requests.append(params); replies.append(reply)
    }
    view.onApplied = { applied = $0 }
    view.apply(dryRun: true); view.apply(dryRun: false)
    try require(requests.count == 1 && view.pending && !view.octave.isEnabled, "Pending review pins roots and blocks duplicate dispatch")
    try require((requests[0]["samples"] as? [[String:Any]])?.count == 109 && requests[0]["name"] as? String == "My clav", "Import uses all reviewed notes and the visible instrument name")
    replies.removeFirst()(["result":["revision":"song:1","data":["instrument":4,"dryRun":true]]])
    try require(applied == 0 && !view.pending, "Checking an import never commits or closes the review")
    view.apply(dryRun: false)
    replies.removeFirst()(["error":["code":-32001,"message":"Song changed; reopen review"]])
    try require(applied == 0 && view.status.stringValue.contains("Song changed"), "Stale song is reported without silently rebasing an import")
    view.apply(dryRun: false)
    replies.removeFirst()(["result":["revision":"song:2","data":["instrument":4,"dryRun":false]]])
    try require(applied == 4, "Successful import selects the new instrument")
    print("PASS multisample review: full family, octave correction/bounds, visible name, dry run, stale guard and duplicate dispatch")
  }
  static func sampleBrowserFixture() -> SampleBrowser {
    let view = SampleBrowser(frame: .zero)
    let entries = (0..<15).map { SampleLibraryEntry(path: "/Samples/808 From Mars/WAV/Kicks/BD Smooth \($0).wav", root: "/Samples",
      name: "BD Smooth \($0).wav", folders: ["Samples", "808 From Mars", "WAV", "Kicks"], bytes: 221984, modified: 0) }
    view.updateLibrary(["roots": ["/Samples"], "count": 22387, "indexing": false])
    view.updateResults(SampleLibraryResults(items: entries, total: 1200, tags: [("808 From Mars", 1200), ("Kicks", 800), ("WAV", 1000)], offset: 0))
    view.metadata.stringValue = "0.784 s · 44100 Hz · Mono"; view.waveform.frames = 34574
    view.waveform.peaks = (0..<256).flatMap { i -> [Float] in let value = Float(exp(-Double(i)/70)) * Float(abs(sin(Double(i)*0.7))); return [-value, value] }
    return view
  }
  static func sampleBrowserChecks() throws {
    let view = sampleBrowserFixture(); var queries = [SampleLibraryQuery](), searches = [(SampleLibraryResults) -> Void]()
    var previews = [(String, Bool)](), previewReplies = [(Result<SampleAuditionData, Error>) -> Void](), stopped = 0
    view.onSearch = { query, reply in queries.append(query); searches.append(reply) }
    view.onInspect = { path, audible, reply in previews.append((path, audible)); previewReplies.append(reply) }; view.onStop = { stopped += 1 }
    view.search.stringValue = "808 kick"; view.requestSearch(); view.search.stringValue = "snare"; view.requestSearch()
    searches.removeFirst()(SampleLibraryResults(items: [], total: 0, tags: [], offset: 0))
    try require(view.entries.count == 15, "Retired search results cannot replace a newer query")
    searches.removeFirst()(SampleLibraryResults(items: view.entries, total: 20, tags: view.tags, offset: 0))
    try require(previews.last?.1 == false, "Search results inspect metadata without unexpectedly starting audio")
    view.table.selectRowIndexes(IndexSet(integer: 1), byExtendingSelection: false)
    try require(previews.last?.1 == true && previews.last?.0.hasSuffix("1.wav") == true, "Moving through results can auto-preview each selected sample")
    view.autoPreview.state = .off; view.table.selectRowIndexes(IndexSet(integer: 2), byExtendingSelection: false)
    try require(previews.last?.1 == false, "Auto-preview can be disabled")
    let space = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0, context: nil, characters: " ", charactersIgnoringModifiers: " ", isARepeat: false, keyCode: 49)!
    view.table.keyDown(with: space); try require(previews.last?.1 == true, "Space auditions without loading")
    var imported = [[String]](), replies = [(Result<Int, Error>) -> Void]()
    view.onImport = { paths, _, reply in imported.append(paths); replies.append(reply) }
    view.table.selectRowIndexes(IndexSet(integersIn: 1...3), byExtendingSelection: false)
    view.loadSelection(); view.loadSelection()
    try require(imported.count == 1 && imported[0].count == 3 && view.importing && !view.loadButton.isEnabled, "Bulk load pins selection and rejects duplicate clicks")
    replies.removeFirst()(.success(3)); try require(!view.importing && view.status.stringValue.contains("one Undo"), "Bulk completion keeps the browser usable")
    view.tagTable.selectRowIndexes(IndexSet(integer: 1), byExtendingSelection: false)
    try require(queries.last?.tags == ["Kicks"], "Clicking a directory tag searches all inherited descendants")
    view.clearFilters(); try require(queries.last?.text == "" && queries.last?.tags.isEmpty == true && stopped > 0, "Clear filters retires preview and removes search constraints")
    var groupReplies = [(MultisampleGroup?)->Void]()
    view.onFindMultisample = { _, reply in groupReplies.append(reply) }
    view.findMultisample(); view.table.selectRowIndexes(IndexSet(integer: 5), byExtendingSelection: false)
    groupReplies.removeFirst()(multisampleGroupFixture())
    try require(view.multisample == nil, "Late family detection cannot target a new selection")
    groupReplies.removeFirst()(multisampleGroupFixture())
    try require(view.multisampleButton.isEnabled && view.multisampleHint.stringValue.contains("109"), "Detected family is offered independently of the result page size")
    try multisampleImportChecks()
    print("PASS sample browser: stale searches, inherited folder filters, keyboard preview, multi-selection and one-shot bulk dispatch")
  }
}
