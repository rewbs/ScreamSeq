import AppKit

// NSTableView recycles visible rows, so every order remains reachable without
// creating thousands of buttons in the main window's quick-access strip.
final class OrderEditor: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let table = NSTableView(), picker = NSPopUpButton(), sequencePicker = NSPopUpButton()
  let count = Theme.label("", size: 12, color: Theme.muted)
  var model = PatternModel([:])
  var selected = 0, updating = false
  var onSelect: ((Int) -> Void)?, onChange: ((Int, Int, String) -> Void)?
  var onSequence: ((Int) -> Void)?
  var onMatrix: (() -> Void)?
  var onAnnotate: (([String: Any]) -> Void)?
  let sectionName = NSTextField(), patternName = NSTextField(), patternNotes = NSTextField()
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
            Theme.label("Pattern", size: 12), picker,
            ActionButton("Assign to order") { [weak self] in self?.change("assign") }, NSView(),
          ]),
        operations,
        stack(.horizontal, [
          ActionButton("Previous section") { [weak self] in self?.navigateSection(-1) },
          ActionButton("Next section") { [weak self] in self?.navigateSection(1) }, NSView(),
        ]),
        stack(.horizontal, [Theme.label("Section", size: 12), sectionName,
          ActionButton("Set section") { [weak self] in self?.saveSection() }]),
        stack(.horizontal, [Theme.label("Pattern", size: 12), patternName,
          ActionButton("Save details") { [weak self] in self?.savePatternDetails() }]),
        patternNotes,
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
    self.model = model
    self.selected = max(0, min(selected, model.orders.count - 1))
    count.stringValue = "\(model.orders.count) orders"
    sequencePicker.removeAllItems()
    for (index, sequence) in model.sequences.enumerated() {
      let name = sequence["name"] as? String ?? ""
      sequencePicker.addItem(withTitle: name.isEmpty ? "Sequence \(index + 1)" : name)
    }
    if model.sequence < model.sequences.count { sequencePicker.selectItem(at: model.sequence) }
    sequencePicker.isHidden = model.sequences.count <= 1
    picker.removeAllItems()
    for pattern in model.patterns {
      let index = pattern["index"] as? Int ?? 0
      picker.addItem(withTitle: "Pattern \(index) · \(pattern["rows"] ?? 0) rows")
      picker.lastItem?.tag = index
    }
    if !model.orders.isEmpty { picker.selectItem(withTag: model.orders[self.selected]) }
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
    selected = table.selectedRow
    picker.selectItem(withTag: model.orders[selected])
    updateDetails()
    onSelect?(selected)
  }
  private func updateDetails() {
    sectionName.stringValue = selected < model.orderMetadata.count ? model.orderMetadata[selected]["name"] as? String ?? "" : ""
    let pattern = selected < model.orders.count ? model.patterns.first { $0["index"] as? Int == model.orders[selected] } : nil
    patternName.stringValue = pattern?["name"] as? String ?? ""
    patternNotes.stringValue = pattern?["annotation"] as? String ?? ""
    patternName.isEnabled = pattern != nil
    patternNotes.isEnabled = pattern != nil
  }
  func navigateSection(_ direction: Int) {
    let markers = model.orderMetadata.indices.filter { !(model.orderMetadata[$0]["name"] as? String ?? "").isEmpty }
    let target = direction < 0 ? markers.last { $0 < selected } : markers.first { $0 > selected }
    guard let target else { return }
    table.selectRowIndexes(IndexSet(integer: target), byExtendingSelection: false)
    table.scrollRowToVisible(target)
  }
  func saveSection() {
    guard selected < model.orderMetadata.count, let id = model.orderMetadata[selected]["id"] as? String else { return }
    onAnnotate?(["id": id, "name": sectionName.stringValue, "expectedRevision": model.revisionToken])
  }
  func savePatternDetails() {
    guard selected < model.orders.count,
      let pattern = model.patterns.first(where: { $0["index"] as? Int == model.orders[selected] }),
      let id = pattern["id"] as? String else { return }
    onAnnotate?(["id": id, "name": patternName.stringValue, "annotation": patternNotes.stringValue, "expectedRevision": model.revisionToken])
  }
  private func change(_ operation: String) { onChange?(selected, picker.selectedTag(), operation) }
  @objc private func selectSequence() { onSequence?(sequencePicker.indexOfSelectedItem) }
}
