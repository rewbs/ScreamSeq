import AppKit
extension InterfaceTests {
  static func recoveryChecks() throws -> RecoveryBrowser {
    try documentLoadReportChecks()
    let view = RecoveryBrowser(frame: .zero)
    try require(!view.restore.isEnabled, "Empty recovery browser cannot restore")
    let entry = RecoveryEntry(url: URL(fileURLWithPath: "/private/tmp/song.resonance"), date: Date(timeIntervalSince1970: 1789894811),
      title: "Midnight Circuit", source: "/Music/Midnight Circuit.resonance", hasRecording: true)
    let older = RecoveryEntry(url: URL(fileURLWithPath: "/private/tmp/older.resonance"), date: entry.date.addingTimeInterval(-10),
      title: "Midnight Circuit", source: nil, hasRecording: false)
    var selected = [String]()
    view.onRestore = { selected.append($0) }
    view.update([entry, older])
    try require(view.table.selectedRow == 0 && view.restore.isEnabled && selected.isEmpty,
      "Listing recovery copies selects the newest without changing the song")
    view.restore.invoke()
    view.table.selectRowIndexes(IndexSet(integer: 1), byExtendingSelection: false)
    view.restore.invoke()
    try require(selected == ["song.resonance", "older.resonance"], "Recovery uses the selected opaque filename, including older copies")
    view.table.deselectAll(nil)
    try require(!view.restore.isEnabled, "Cleared selection cannot restore a stale copy")
    view.update([entry, older])
    return view
  }
  static func documentLoadReportChecks() throws {
    let source = "/Users/musician/Songs/Original take.screamseq"
    let warning = "Converted NF duration to beats."
    var snapshot: [String: Any] = ["revisionToken": "document-a:0:0", "title": "Different song title",
      "loadWarnings": [warning, "Ignored unsupported graph field."], "requiresSaveAs": true,
      "loadSourcePath": source, "issues": [warning, "A plug-in is unavailable."]]
    var state = DocumentLoadReport()
    state.update(PatternModel(snapshot))
    try require(state.requiresSaveAs && state.isVisible && state.sourcePath == source,
      "A recovered load protects its original save destination and retains source provenance")
    try require(state.suggestedFilename(title: "Different song title") == "Original take Recovered.screamseq",
      "Save As uses the original basename with a Recovered suffix, rather than suggesting its original path")
    try require(state.summary == "Recovered project · 2 warnings · Save a copy" &&
      state.text.components(separatedBy: warning).count == 2 && state.text.contains("A plug-in is unavailable."),
      "Recovery warnings have a persistent count and the report includes ordinary import notes without duplicates")
    let banner = DocumentLoadBanner(frame: NSRect(x: 0, y: 0, width: 940, height: 42))
    let report = DocumentLoadReportView(frame: NSRect(x: 0, y: 0, width: 680, height: 440))
    var reports = 0, saves = 0
    banner.report.handler = { reports += 1 }; banner.saveCopy.handler = { saves += 1 }
    banner.update(state); report.update(state)
    try require(!banner.isHidden && !banner.saveCopy.isHidden && !report.saveCopy.isHidden &&
      !report.text.isEditable && report.text.isSelectable && report.scroll.hasVerticalScroller &&
      report.text.string.contains(source),
      "Recovery feedback is a visible actionable banner and a selectable scrollable report, not a blocking alert")
    banner.report.invoke(); banner.saveCopy.invoke()
    try require(reports == 1 && saves == 1, "The persistent warning exposes report and save-copy actions")
    snapshot["revisionToken"] = "document-a:4:2"
    state.update(PatternModel(snapshot)); banner.update(state)
    try require(state.requiresSaveAs && !banner.isHidden,
      "Normal edit and playback snapshots cannot discard pending recovery protection or its banner")
    state.didSave(); state.update(PatternModel(snapshot)); banner.update(state); report.update(state)
    try require(!state.requiresSaveAs && state.isVisible && banner.saveCopy.isHidden && report.saveCopy.isHidden &&
      state.suggestedFilename(title: "Saved copy") == "Saved copy.screamseq" && state.text.contains(warning),
      "UI or API save acknowledgement keeps the warning report but does not detach the new copy on a stale snapshot")
    snapshot["revisionToken"] = "document-b:0:0"
    state.update(PatternModel(snapshot))
    try require(state.requiresSaveAs, "Opening the source again re-arms protection independently of a previous saved copy")
    state.update(PatternModel(["revisionToken": "document-c:0:0", "issues": ["External sample missing."]]))
    banner.update(state); report.update(state)
    try require(!state.requiresSaveAs && !state.isVisible && banner.isHidden &&
      report.text.string.contains("External sample missing.") && !report.text.string.contains(source),
      "Ordinary imports retain their report without false recovery warnings or a previous document's provenance")
    var migration = DocumentLoadReport()
    migration.update(PatternModel(["revisionToken": "document-d:0:0", "requiresSaveAs": true]))
    try require(migration.isVisible && migration.suggestedFilename(title: "") == "Untitled Recovered.screamseq",
      "A protected migration still exposes Save a copy even if it supplied no detailed warnings")
  }
}
