import AppKit

// NSTableView recycles visible rows, so every order remains reachable without
// creating thousands of buttons in the main window's quick-access strip.
final class OrderEditor: NSView, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate {
  let table = DirectActionTable(), picker = NSPopUpButton(), sequencePicker = NSPopUpButton()
  let count = Theme.label("", size: 12, color: Theme.muted)
  var model = PatternModel([:])
  var selected = 0, updating = false
  var onReorder: ((Int, Int, String) -> Void)?, onOpen: (() -> Void)?
  private let dragType = NSPasteboard.PasteboardType("org.screamseq.order")
  private var dragRow: Int?, dragRevision = ""
  var onSelect: ((Int) -> Void)?, onChange: ((Int, Int, String) -> Void)?
  var onSequence: ((Int) -> Void)?
  var onMatrix: (() -> Void)?
  var onAnnotate: (([String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  let sectionName = NSTextField(), patternName = NSTextField(), patternNotes = NSTextField()
  private struct AnnotationDraft {
    var id: String, revision: String, name: String, notes: String
    var savedName: String, savedNotes: String
    var dirty: Bool { name != savedName || notes != savedNotes }
  }
  private var annotationDrafts = [String: AnnotationDraft]()
  private var sectionTarget: String?, patternTarget: String?
  private var annotationPending = Set<String>()
  let detailStatus = Theme.label("", size: 11, color: Theme.muted)
  private var documentIdentity = ""
  override init(frame: NSRect) {
    super.init(frame: frame)
    for (id, title, width) in [
      ("order", "Order", 70.0), ("section", "Section", 180.0), ("pattern", "Pattern", 220.0), ("rows", "Rows", 70.0),
    ] {
      let column = NSTableColumn(identifier: .init(id))
      column.title = title
      column.width = width
      table.addTableColumn(column)
    }
    table.registerForDraggedTypes([dragType]); table.setDraggingSourceOperationMask(.move,forLocal:true)
    table.activate = { [weak self] in self?.onOpen?() }
    table.remove = { [weak self] in self?.change("remove") }
    table.actions = { [weak self] in self?.actionMenu() ?? NSMenu() }
    table.delegate = self
    table.dataSource = self
    table.usesAlternatingRowBackgroundColors = true
    table.rowHeight = 28
    table.allowsEmptySelection = false
    table.setAccessibilityLabel("Complete order list")
    let scroll = NSScrollView()
    scroll.hasVerticalScroller = true
    scroll.documentView = table
    picker.fixed(width: 210)
    picker.target = self; picker.action = #selector(assignPattern)
    sequencePicker.fixed(width: 210)
    sequencePicker.target = self
    sequencePicker.action = #selector(selectSequence)
    sequencePicker.setAccessibilityLabel("Song sequence")
    let operations = stack(.horizontal, [], spacing: 8)
    for (title, action) in [
      ("Insert before", "before"), ("Insert after", "after"),
      ("Move up", "up"), ("Move down", "down"), ("Remove", "remove"),
    ] {
      operations.addArrangedSubview(ActionButton(title) { [weak self] in self?.change(action) })
    }
    operations.addArrangedSubview(NSView())
    for (field, label) in [(sectionName, "Section beginning at this order"), (patternName, "Pattern name"), (patternNotes, "Pattern notes")] {
      field.placeholderString = label
      field.delegate = self; field.target = self; field.action = #selector(commitDetails(_:))
      field.setAccessibilityLabel(label)
      field.widthAnchor.constraint(greaterThanOrEqualToConstant: 160).isActive = true
    }
    let content = stack(
      .vertical,
      [
        stack(
          .horizontal,
          [
            Theme.label("Arrange orders", size: 20, weight: .semibold), NSView(), sequencePicker,
            count, ActionButton("Matrix…") { [weak self] in self?.onMatrix?() },
          ]),
        scroll,
        stack(
          .horizontal,
          [
            Theme.label("Pattern", size: 12), picker, NSView(),
          ]),
        stack(.horizontal, [ActionButton("Insert after"){[weak self] in self?.change("after")}, ActionMenuButton { [weak self] in self?.actionMenu() ?? NSMenu() }, NSView()]),
        stack(.horizontal, [
          ActionButton("Previous section") { [weak self] in self?.navigateSection(-1) },
          ActionButton("Next section") { [weak self] in self?.navigateSection(1) }, NSView(),
        ]),
        stack(.horizontal, [Theme.label("Section", size: 12), sectionName]),
        stack(.horizontal, [Theme.label("Pattern", size: 12), patternName]),
        patternNotes,
        stack(.horizontal, [detailStatus, NSView(), ActionButton("Reload details") { [weak self] in self?.reloadDetails() }]),
        Theme.label(
          "Sections move with their first order. An empty section name removes that marker. Pattern details are shared.",
          size: 11, color: Theme.muted),
      ], spacing: 14)
    content.fill(self, inset: 20)
    content.stretchAcrossAxis()
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ model: PatternModel, selected: Int) {
    updating = true
    retainDetails()
    let document = String(model.revisionToken.split(separator: ":").first ?? "")
    if document != documentIdentity {
      annotationDrafts.removeAll(); sectionTarget = nil; patternTarget = nil
      documentIdentity = document
    }
    self.model = model
    self.selected = max(0, min(selected, model.orders.count - 1))
    count.stringValue = "\(model.orders.count) orders"
    // Sequences may share a name, and addItem(withTitle:) would drop the
    // earlier one. Each item carries its own sequence index instead.
    sequencePicker.removeAllItems()
    for (index, sequence) in model.sequences.enumerated() {
      let name = sequence["name"] as? String ?? ""
      let item = NSMenuItem(title: name.isEmpty ? "Sequence \(index + 1)" : name, action: nil, keyEquivalent: "")
      item.tag = index; sequencePicker.menu?.addItem(item)
    }
    if model.sequence < model.sequences.count { sequencePicker.selectItem(withTag: model.sequence) }
    sequencePicker.isHidden = model.sequences.count <= 1
    let assigned = self.selected < model.orders.count ? model.orders[self.selected] : nil
    let samePattern = assigned != nil && assigned == shownAssignment
    let chosenPattern = picker.selectedItem?.tag
    picker.removeAllItems()
    for pattern in model.patterns {
      let index = pattern["index"] as? Int ?? 0
      let item = NSMenuItem(title: "Pattern \(index) · \(pattern["rows"] ?? 0) rows", action: nil, keyEquivalent: "")
      item.tag = index; picker.menu?.addItem(item)
    }
    // A pattern chosen for "Assign to order" but not yet assigned is kept
    // while the same order still holds the same pattern.
    if let chosenPattern, samePattern, picker.selectItem(withTag: chosenPattern) {
    } else if let assigned { picker.selectItem(withTag: assigned) }
    shownAssignment = assigned
    table.reloadData()
    table.selectRowIndexes(IndexSet(integer: self.selected), byExtendingSelection: false)
    table.scrollRowToVisible(self.selected)
    updateDetails()
    updating = false
  }
  func numberOfRows(in tableView: NSTableView) -> Int { model.orders.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard row < model.orders.count, let column else { return nil }
    let cell =
      tableView.makeView(withIdentifier: column.identifier, owner: self) as? NSTextField
      ?? Theme.label("", size: 12, mono: true)
    cell.identifier = column.identifier
    let pattern = model.orders[row]
    if column.identifier.rawValue == "order" {
      cell.stringValue = String(format: "%03d", row)
    } else if column.identifier.rawValue == "section" {
      cell.stringValue = row < model.orderMetadata.count ? model.orderMetadata[row]["name"] as? String ?? "" : ""
      cell.textColor = Theme.accent
    } else if column.identifier.rawValue == "pattern" {
      let name = model.patterns.first { $0["index"] as? Int == pattern }?["name"] as? String ?? ""
      cell.stringValue =
        pattern == 65535 ? "End" : pattern == 65534 ? "Skip" : String(format: "%03d", pattern) + (name.isEmpty ? "" : "  " + name)
    } else {
      let found = model.patterns.first { $0["index"] as? Int == pattern }
      cell.stringValue = found.map { String($0["rows"] as? Int ?? 0) } ?? "—"
    }
    return cell
  }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard !updating, table.selectedRow >= 0 else { return }
    retainDetails()
    selected = table.selectedRow
    picker.selectItem(withTag: model.orders[selected]); shownAssignment = model.orders[selected]
    updateDetails()
    onSelect?(selected)
  }
  private var shownAssignment: Int?
  private func retainDetails() {
    if let key = sectionTarget { annotationDrafts[key]?.name = sectionName.stringValue }
    if let key = patternTarget {
      annotationDrafts[key]?.name = patternName.stringValue
      annotationDrafts[key]?.notes = patternNotes.stringValue
    }
  }
  var hasUncommittedDetails: Bool {
    retainDetails()
    return annotationDrafts.values.contains { $0.dirty }
  }
  private func captureDetails(_ entity: [String: Any]?, kind: String) -> String? {
    guard let entity, let id = entity["id"] as? String else { return nil }
    let key = "\(documentIdentity):\(kind):\(id)"
    if annotationDrafts[key]?.dirty != true && !annotationPending.contains(key) {
      let name = entity["name"] as? String ?? "", notes = entity["annotation"] as? String ?? ""
      annotationDrafts[key] = AnnotationDraft(id: id, revision: model.revisionToken, name: name, notes: notes, savedName: name, savedNotes: notes)
    }
    return key
  }
  private func updateDetails() {
    let section = model.orderMetadata.indices.contains(selected) ? model.orderMetadata[selected] : nil
    let pattern = model.orders.indices.contains(selected) ? model.patterns.first { $0["index"] as? Int == model.orders[selected] } : nil
    sectionTarget = captureDetails(section, kind: "order")
    patternTarget = captureDetails(pattern, kind: "pattern")
    let sectionDraft = sectionTarget.flatMap { annotationDrafts[$0] }
    let patternDraft = patternTarget.flatMap { annotationDrafts[$0] }
    if sectionName.stringValue != sectionDraft?.name ?? "" { sectionName.stringValue = sectionDraft?.name ?? "" }
    if patternName.stringValue != patternDraft?.name ?? "" { patternName.stringValue = patternDraft?.name ?? "" }
    if patternNotes.stringValue != patternDraft?.notes ?? "" { patternNotes.stringValue = patternDraft?.notes ?? "" }
    sectionName.isEnabled = section != nil; patternName.isEnabled = pattern != nil; patternNotes.isEnabled = pattern != nil
    let stale = [sectionDraft, patternDraft].compactMap { $0 }.contains { $0.dirty && $0.revision != model.revisionToken }
    detailStatus.stringValue = stale ? "Draft retained · song changed · Reload details to recapture" : "Return saves · drafts follow their original section or pattern"
  }
  func reloadDetails() {
    for key in [sectionTarget, patternTarget].compactMap({ $0 }) where !annotationPending.contains(key) { annotationDrafts.removeValue(forKey: key) }
    updateDetails()
  }
  private func saveDetails(_ key: String?, pattern: Bool) {
    retainDetails()
    guard let key, let draft = annotationDrafts[key], draft.dirty, !annotationPending.contains(key), let request = onAnnotate else { return }
    var params: [String: Any] = ["id": draft.id, "name": draft.name, "expectedRevision": draft.revision]
    if pattern { params["annotation"] = draft.notes }
    annotationPending.insert(key)
    request(params) { [weak self] reply in
      guard let self else { return }; self.annotationPending.remove(key)
      self.retainDetails()
      if let result = reply["result"] as? [String: Any], let revision = result["revision"] as? String {
        self.annotationDrafts[key]?.savedName = draft.name
        self.annotationDrafts[key]?.savedNotes = draft.notes
        self.annotationDrafts[key]?.revision = revision
        self.detailStatus.stringValue = "Details saved · Undo restores the previous text"
      } else {
        self.detailStatus.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Could not save details; draft retained"
      }
    }
  }
  func navigateSection(_ direction: Int) {
    let markers = model.orderMetadata.indices.filter { !(model.orderMetadata[$0]["name"] as? String ?? "").isEmpty }
    let target = direction < 0 ? markers.last { $0 < selected } : markers.first { $0 > selected }
    guard let target else { return }
    table.selectRowIndexes(IndexSet(integer: target), byExtendingSelection: false)
    table.scrollRowToVisible(target)
  }
  func saveSection() { saveDetails(sectionTarget, pattern: false) }
  func savePatternDetails() { saveDetails(patternTarget, pattern: true) }
  private func change(_ operation: String) { onChange?(selected, picker.selectedTag(), operation) }
  func actionMenu() -> NSMenu {
    let menu = NSMenu(title: "Orders"); menu.autoenablesItems=false
    for (label, operation) in [("Insert before","before"),("Insert after","after"),("Move up","up"),("Move down","down"),("Remove order","remove")] {
      let enabled = operation == "up" ? selected>0 : operation == "down" ? selected+1<model.orders.count : operation == "remove" ? model.orders.count>1 : !model.orders.isEmpty
      menu.addItem(ContextAction(label,enabled:enabled){[weak self] in self?.change(operation)})
    }
    return menu
  }
  func tableView(_ tableView:NSTableView,pasteboardWriterForRow row:Int)->NSPasteboardWriting? {
    guard model.orders.indices.contains(row) else{return nil}
    dragRow=row;dragRevision=model.revisionToken
    let item=NSPasteboardItem();item.setString(String(row),forType:dragType);return item
  }
  func tableView(_ tableView:NSTableView,validateDrop info:NSDraggingInfo,proposedRow row:Int,proposedDropOperation operation:NSTableView.DropOperation)->NSDragOperation {
    guard info.draggingSource as? NSTableView === table,dragRow != nil,dragRevision==model.revisionToken,(0...model.orders.count).contains(row) else{return []}
    table.setDropRow(row,dropOperation:.above);return .move
  }
  func tableView(_ tableView:NSTableView,acceptDrop info:NSDraggingInfo,row:Int,dropOperation operation:NSTableView.DropOperation)->Bool {
    guard info.draggingSource as? NSTableView === table,let from=dragRow,dragRevision==model.revisionToken,(0...model.orders.count).contains(row) else{return false}
    let to=row>from ? row-1:row
    if to != from{onReorder?(from,to,dragRevision)};dragRow=nil;return true
  }
  @objc private func assignPattern() { if !updating { change("assign") } }
  @objc private func commitDetails(_ field: NSTextField) {
    guard !updating else { return }
    if field === sectionName { saveSection() } else { savePatternDetails() }
  }
  func controlTextDidEndEditing(_ notification: Notification) { if let field = notification.object as? NSTextField { commitDetails(field) } }
  @objc private func selectSequence() { guard let item = sequencePicker.selectedItem else { return }; onSequence?(item.tag) }
}
