import AppKit

/// Retained presentation shortcuts. Musical targets and drafts stay in their editors.
final class WorkspaceContextPanel: NSView, NSMenuDelegate, NSTableViewDataSource, NSTableViewDelegate {
  struct Choice: Equatable { let id: String, title: String; var enabled = true }
  struct Action: Equatable { let id: String, title: String; var enabled = true }
  let heading = Theme.label("PATTERN", size: 11, color: Theme.accent, weight: .semibold)
  let scroll = verticalScrollView(), rows = NSStackView(), selectors = NSStackView()
  let patterns = NSPopUpButton(), orders = NSPopUpButton(), octaves = NSPopUpButton(), steps = NSPopUpButton(), sounds = NSPopUpButton()
  let samples = NSTableView(), sampleScroll = verticalScrollView()
  private(set) var buttons = [ActionButton](), actions = [Action](), sampleChoices = [Choice]()
  private(set) var editor = "pattern", stamp = ""
  private var menus = Set<ObjectIdentifier>(), updating = false
  var onAction: ((String) -> Void)?, onChoice: ((String, String, String) -> Void)?
  var isTracking: Bool { !menus.isEmpty }
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
    setAccessibilityLabel("Context panel")
    rows.orientation = .vertical; rows.spacing = 8; rows.alignment = .leading
    selectors.orientation = .vertical; selectors.spacing = 6; selectors.alignment = .leading
    for (name, picker) in [("pattern",patterns),("order",orders),("octave",octaves),("step",steps),("sound",sounds)] {
      picker.identifier = NSUserInterfaceItemIdentifier(name); picker.target = self; picker.action = #selector(choose(_:))
      picker.setAccessibilityLabel("Context \(name)"); picker.menu?.delegate = self; picker.menu?.autoenablesItems = false
      picker.font = .systemFont(ofSize: 12); picker.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
      selectors.addArrangedSubview(picker)
    }
    selectors.stretchAcrossAxis()
    let column = NSTableColumn(identifier: .init("sample")); column.title = "Samples"; samples.addTableColumn(column)
    samples.headerView = nil; samples.rowHeight = 24; samples.dataSource = self; samples.delegate = self
    samples.allowsEmptySelection = true
    samples.setAccessibilityLabel("Context sample list"); sampleScroll.documentView = samples; sampleScroll.fixed(height: 176)
    rows.addArrangedSubview(heading); rows.addArrangedSubview(selectors); rows.addArrangedSubview(sampleScroll)
    for _ in 0..<6 {
      let button = ActionButton("") {}
      button.alignment = .left; button.lineBreakMode = .byTruncatingTail
      button.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
      buttons.append(button); rows.addArrangedSubview(button)
    }
    rows.stretchAcrossAxis(); scroll.documentView = rows; scroll.fill(self, inset: 10)
    rows.translatesAutoresizingMaskIntoConstraints = false
    NSLayoutConstraint.activate([rows.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
      rows.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
      rows.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor)])
  }
  required init?(coder: NSCoder) { fatalError() }
  func menuWillOpen(_ menu: NSMenu) { menus.insert(ObjectIdentifier(menu)) }
  func menuDidClose(_ menu: NSMenu) { menus.remove(ObjectIdentifier(menu)) }
  func owns(_ responder: NSResponder?) -> Bool {
    var view = responder as? NSView
    if let text = view as? NSTextView, let owner = text.delegate as? NSView { view = owner }
    return view?.isDescendant(of: self) == true
  }
  func update(editor: String, title: String, actions: [Action], available: Bool, stamp: String) {
    guard !isTracking else { return }
    self.editor = editor; self.stamp = stamp
    if heading.stringValue != title { heading.stringValue = title }
    selectors.isHidden = !["pattern","notes","gate","effects","graphCommands"].contains(editor)
    sampleScroll.isHidden = editor != "samples"
    self.actions = actions
    for (index, button) in buttons.enumerated() {
      guard actions.indices.contains(index) else { button.isHidden = true; continue }
      let action = actions[index]; button.isHidden = false
      if button.title != action.title { button.title = action.title; button.toolTip = action.title }
      button.isEnabled = available && action.enabled
      button.identifier = .init("context." + action.id)
      button.handler = { [weak self, weak button] in guard button?.isEnabled == true else { return }; self?.onAction?(action.id) }
    }
    for picker in [patterns,orders,octaves,steps,sounds] { picker.isEnabled = available && picker.itemArray.contains(where: \.isEnabled) }
    samples.isEnabled = available
  }
  func choices(_ picker: NSPopUpButton, _ values: [Choice], selected: String?, stamp: String) {
    guard !isTracking else { return }
    let previous = picker.itemArray.map { Choice(id: ($0.representedObject as? [String:String])?["id"] ?? "", title: $0.title, enabled: $0.isEnabled) }
    if previous != values {
      picker.removeAllItems()
      for value in values { picker.addItem(withTitle: value.title); picker.lastItem?.isEnabled = value.enabled }
    }
    for (item, value) in zip(picker.itemArray, values) { item.representedObject = ["id":value.id,"stamp":stamp] }
    if let selected, let index = values.firstIndex(where: { $0.id == selected }) { picker.selectItem(at: index) }
    else { picker.select(nil) }
  }
  func sampleCatalogue(_ values: [Choice], selected: String?, stamp: String) {
    guard !isTracking else { return }
    updating = true; defer { updating = false }; self.stamp = stamp
    if sampleChoices != values { sampleChoices = values; samples.reloadData() }
    if let selected, let row = values.firstIndex(where: { $0.id == selected }) {
      if samples.selectedRow != row { samples.selectRowIndexes(IndexSet(integer:row), byExtendingSelection:false) }
    } else if samples.selectedRow != -1 { samples.deselectAll(nil) }
  }
  @objc private func choose(_ sender: NSPopUpButton) {
    guard sender.isEnabled, let target = sender.selectedItem?.representedObject as? [String:String], let id = target["id"], let stamp = target["stamp"] else { return }
    onChoice?(sender.identifier!.rawValue, id, stamp)
  }
  @objc private func chooseSample() {
    guard !updating, samples.isEnabled, sampleChoices.indices.contains(samples.selectedRow) else { return }
    onChoice?("sample", sampleChoices[samples.selectedRow].id, stamp)
  }
  func tableViewSelectionDidChange(_ notification: Notification) { chooseSample() }
  func numberOfRows(in tableView: NSTableView) -> Int { sampleChoices.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard sampleChoices.indices.contains(row) else { return nil }
    let label = Theme.label(sampleChoices[row].title, size: 11); label.toolTip = label.stringValue; return label
  }
  var snapshot: [String:Any] { ["editor":editor,"actions":zip(actions,buttons).map { action,button in ["command":action.id,"label":action.title,"visible":!button.isHiddenOrHasHiddenAncestor,"enabled":button.isEnabled] as [String:Any] }] }
}
