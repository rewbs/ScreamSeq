import AppKit
extension InterfaceTests {
  static func recoveryChecks() throws -> RecoveryBrowser {
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
}
