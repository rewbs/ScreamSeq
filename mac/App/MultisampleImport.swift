import AppKit

final class MultisampleImportView: NSView, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate {
  let group: MultisampleGroup
  let name = NSTextField(), octave = NSPopUpButton(), table = SampleBrowserTable()
  let status = Theme.label("", size: 12, color: Theme.muted), coverage = Theme.label("", size: 12, color: Theme.muted)
  private(set) var pending = false, revision: String
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onClose: (() -> Void)?, onApplied: ((Int) -> Void)?, onPreview: ((String) -> Void)?, onStop: (() -> Void)?
  var importButton: ActionButton!, checkButton: ActionButton!
  init(group: MultisampleGroup, revision: String) {
    self.group = group; self.revision = revision; super.init(frame: .zero)
    wantsLayer = true; layer?.backgroundColor = Theme.bg.cgColor
    name.stringValue = group.name; name.delegate = self; name.setAccessibilityLabel("Multi-sample instrument name")
    for value in -4...4 { octave.addItem(withTitle: "\(value >= 0 ? "+" : "")\(value)") }
    octave.selectItem(at: group.suggestedOctaveShift + 4); octave.target = self; octave.action = #selector(changeOffset)
    octave.setAccessibilityLabel("Filename octave offset to tracker notes")
    let explanation = Theme.label("Found \(group.members.count) note variations in this folder, including files outside the current search. \(group.explanation)", size: 12, color: Theme.muted)
    let folder = Theme.label(group.folder, size: 11, color: Theme.muted); folder.lineBreakMode = .byTruncatingMiddle; folder.toolTip = group.folder
    for label in [explanation, coverage, status] { label.maximumNumberOfLines = 3; label.lineBreakMode = .byWordWrapping; label.preferredMaxLayoutWidth = 700 }
    for (id, title, width) in [("name", "Sample file", 340.0), ("source", "File note", 80.0), ("root", "Tracker root", 100.0), ("range", "Key range", 150.0)] {
      let column = NSTableColumn(identifier: .init(id)); column.title = title; column.width = width; table.addTableColumn(column)
    }
    table.dataSource = self; table.delegate = self; table.rowHeight = 26
    table.setAccessibilityLabel("Detected sample roots and playable key ranges")
    table.onPreview = { [weak self] in self?.preview() }; table.onStop = { [weak self] in self?.onStop?() }
    let scroll = NSScrollView(); scroll.documentView = table; scroll.hasVerticalScroller = true; scroll.hasHorizontalScroller = true
    scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true
    checkButton = ActionButton("Check import") { [weak self] in self?.apply(dryRun: true) }
    importButton = ActionButton("Import one instrument") { [weak self] in self?.apply(dryRun: false) }
    let content = stack(.vertical, [Theme.label("Multi-sample instrument", size: 22, weight: .semibold), explanation, folder,
      stack(.horizontal, [Theme.label("Name", size: 12), name, Theme.label("Octave offset", size: 12), octave]),
      coverage, scroll, stack(.horizontal, [ActionButton("Preview selected") { [weak self] in self?.preview() }, NSView(),
        ActionButton("Cancel") { [weak self] in guard self?.pending != true else { return }; self?.onClose?() }, checkButton, importButton]), status], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 24)
    status.heightAnchor.constraint(greaterThanOrEqualToConstant: 42).isActive = true
    update()
  }
  required init?(coder: NSCoder) { fatalError() }
  var octaveShift: Int { octave.indexOfSelectedItem - 4 }
  var rootNotes: [Int] { group.members.map { $0.note.semitone + octaveShift * 12 + 1 } }
  func range(_ row: Int) -> (Int, Int) {
    let notes = rootNotes
    return (row == 0 ? notes[row] : (notes[row-1] + notes[row])/2 + 1,
      row + 1 == notes.count ? notes[row] : (notes[row] + notes[row+1])/2)
  }
  @objc func changeOffset() { onStop?(); update() }
  func controlTextDidChange(_ notification: Notification) { update() }
  func update() {
    table.reloadData()
    do {
      _ = try group.sources(octaveShift: octaveShift)
      let text = name.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
      guard !text.isEmpty, text.utf8.count <= 128, !text.contains("\0") else { throw MultisampleGroup.error("Enter a name of 1 to 128 UTF-8 bytes.") }
      coverage.stringValue = "\(MultisampleGroup.noteName(rootNotes.first!))–\(MultisampleGroup.noteName(rootNotes.last!)): nearest sample fills gaps; notes outside this range are unmapped."
      status.stringValue = "Each root plays at the recorded pitch. Import appends \(group.members.count) samples and one instrument in one Undo step."
      checkButton?.isEnabled = !pending; importButton?.isEnabled = !pending
    } catch { coverage.stringValue = "Review required"; status.stringValue = error.localizedDescription; checkButton?.isEnabled = false; importButton?.isEnabled = false }
    name.isEnabled = !pending; octave.isEnabled = !pending
  }
  func preview() { if group.members.indices.contains(table.selectedRow) { onPreview?(group.members[table.selectedRow].entry.path) } }
  func apply(dryRun: Bool) {
    guard !pending, importButton.isEnabled, let onRequest else { return }
    do {
      let sources = try group.sources(octaveShift: octaveShift), title = name.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
      pending = true; onStop?(); update(); status.stringValue = dryRun ? "Checking every sample…" : "Importing the instrument…"
      onRequest("instrument.importMultisample", ["samples": sources, "name": title, "dryRun": dryRun, "expectedRevision": revision]) { [weak self] reply in
        guard let self else { return }; self.pending = false; self.update()
        guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any], let instrument = data["instrument"] as? Int, let revision = result["revision"] as? String else {
          self.status.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Import failed"; return
        }
        self.revision = revision
        if dryRun { self.status.stringValue = "All files validated. Ready to create instrument \(instrument)." }
        else { self.onApplied?(instrument) }
      }
    } catch { status.stringValue = error.localizedDescription }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { group.members.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard group.members.indices.contains(row) else { return nil }
    let id = column?.identifier ?? .init("name")
    let cell = tableView.makeView(withIdentifier: id, owner: self) as? NSTextField ?? Theme.label("", size: 12)
    cell.identifier = id; cell.lineBreakMode = .byTruncatingMiddle; cell.toolTip = group.members[row].entry.path
    switch id.rawValue {
    case "source": cell.stringValue = group.members[row].note.label
    case "root": cell.stringValue = MultisampleGroup.noteName(rootNotes[row])
    case "range": let (low, high) = range(row); cell.stringValue = low == high ? MultisampleGroup.noteName(low) : "\(MultisampleGroup.noteName(low))–\(MultisampleGroup.noteName(high))"
    default: cell.stringValue = group.members[row].entry.name
    }
    return cell
  }
}
