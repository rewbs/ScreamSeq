import AppKit

final class PluginBrowser: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let search = NSSearchField(), format = NSPopUpButton(), kind = NSPopUpButton(), category = NSPopUpButton()
  let favoritesOnly = NSButton(checkboxWithTitle: "Favorites", target: nil, action: nil)
  let showHidden = NSButton(checkboxWithTitle: "Show hidden", target: nil, action: nil)
  let favorite = NSButton(checkboxWithTitle: "Favorite", target: nil, action: nil)
  let hiddenToggle = NSButton(checkboxWithTitle: "Hide from browser", target: nil, action: nil)
  let categoryField = NSTextField(string: ""), table = NSTableView()
  let status = Theme.label("", size: 12, color: Theme.muted)
  let detail = Theme.label("Select a plugin", size: 12)
  private(set) var plugins = [[String: Any]](), visible = [[String: Any]](), libraryRevision = "", pending = false
  private var refreshing = false
  private var preferenceWarning = ""
  let builtInOnly: Bool
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onChoose: (([String: Any]) -> Void)?
  private(set) var addButton: ActionButton!, saveButton: ActionButton!
  init(builtInOnly: Bool = false) {
    self.builtInOnly = builtInOnly; super.init(frame: .zero)
    search.placeholderString = "Search plugins or categories"; search.delegate = self
    format.addItems(withTitles: builtInOnly ? ["Built-in"] : ["All formats", "AU", "VST3", "Built-in"])
    kind.addItems(withTitles: ["Effects and instruments", "Effects", "Instruments"])
    category.addItem(withTitle: "All categories")
    for popup in [format, kind, category] { popup.target = self; popup.action = #selector(filterChanged) }
    for button in [favoritesOnly, showHidden] { button.target = self; button.action = #selector(filterChanged) }
    format.fixed(width: 115); kind.fixed(width: 185); category.fixed(width: 180)
    table.headerView = nil; table.rowHeight = 29; table.dataSource = self; table.delegate = self
    let name = NSTableColumn(identifier: .init("name")); name.width = 350; name.minWidth = 160
    let type = NSTableColumn(identifier: .init("kind")); type.width = 160; type.minWidth = 125; type.maxWidth = 180
    let group = NSTableColumn(identifier: .init("category")); group.width = 140; group.minWidth = 70
    table.addTableColumn(name); table.addTableColumn(type); table.addTableColumn(group)
    table.setAccessibilityLabel("Plugin browser results")
    let list = verticalScrollView(); list.documentView = table
    list.heightAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
    categoryField.placeholderString = "Default category"; categoryField.setAccessibilityLabel("Custom plugin category")
    categoryField.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    detail.maximumNumberOfLines = 2; detail.lineBreakMode = .byTruncatingMiddle
    detail.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    status.maximumNumberOfLines = 3; status.lineBreakMode = .byWordWrapping
    status.preferredMaxLayoutWidth = 690; status.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    status.heightAnchor.constraint(greaterThanOrEqualToConstant: 42).isActive = true
    saveButton = ActionButton("Save preferences") { [weak self] in self?.savePreferences() }
    addButton = ActionButton("Add plugin") { [weak self] in self?.choose() }
    var reload: [NSView] = [ActionButton("Reload list") { [weak self] in self?.load() }]
    if !builtInOnly { reload.append(ActionButton("Rescan installed plugins") { [weak self] in self?.load(rescan: true) }) }
    let content = stack(.vertical, [Theme.label(builtInOnly ? "Built-in effects" : "Effects and instruments", size: 20, weight: .semibold),
      stack(.horizontal, [search] + reload), stack(.horizontal, [format, kind, category, NSView()]),
      stack(.horizontal, [favoritesOnly, showHidden, NSView()]), list, detail,
      stack(.horizontal, [favorite, hiddenToggle, NSView()]),
      stack(.horizontal, [Theme.label("Category", size: 12), categoryField, saveButton]),
      stack(.horizontal, [addButton, NSView()]), status], spacing: 12)
    content.stretchAcrossAxis(); content.fill(self, inset: 20); selectionChanged()
  }
  required init?(coder: NSCoder) { fatalError() }
  var selected: [String: Any]? { visible.indices.contains(table.selectedRow) ? visible[table.selectedRow] : nil }
  func load(rescan: Bool = false) {
    guard !pending, let onRequest else { return }
    pending = true; updateControls(); status.stringValue = rescan ? "Rescanning installed plugins…" : "Loading cached plugins…"
    var params: [String: Any] = ["includeHidden": true, "rescan": rescan]
    if builtInOnly { params["format"] = "Built-in" }
    onRequest("plugin.library.get", params) { [weak self] response in
      guard let self else { return }; self.pending = false
      if let error = response["error"] as? [String: Any] {
        self.status.stringValue = error["message"] as? String ?? "Could not load plugins"; self.updateControls(); return
      }
      guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else { self.updateControls(); return }
      self.update(data)
    }
  }
  func update(_ data: [String: Any]) {
    let keep = selected?["catalogID"] as? String
    preferenceWarning = data["warning"] as? String ?? ""
    plugins = data["plugins"] as? [[String: Any]] ?? []; libraryRevision = data["libraryRevision"] as? String ?? ""
    rebuildCategories(); filter(keeping: keep)
  }
  private func rebuildCategories() {
    let previous = category.indexOfSelectedItem > 0 ? category.titleOfSelectedItem : nil
    let names = Set(plugins.compactMap { $0["category"] as? String }).sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    category.removeAllItems(); category.addItem(withTitle: "All categories")
    for name in names { category.menu?.addItem(NSMenuItem(title: name, action: nil, keyEquivalent: "")) }
    if let previous, let index = names.firstIndex(of: previous) { category.selectItem(at: index + 1) }
  }
  @objc func filterChanged() { filter(keeping: selected?["catalogID"] as? String) }
  func controlTextDidChange(_ notification: Notification) { filterChanged() }
  func filter(keeping id: String? = nil) {
    let query = search.stringValue, chosenFormat = format.titleOfSelectedItem ?? "All formats"
    visible = plugins.filter { entry in
      if !builtInOnly && format.indexOfSelectedItem > 0 && entry["format"] as? String != chosenFormat { return false }
      if favoritesOnly.state == .on && entry["favorite"] as? Bool != true { return false }
      if showHidden.state != .on && entry["hidden"] as? Bool == true { return false }
      let instrument = entry["isInstrument"] as? Bool == true
      if (kind.indexOfSelectedItem == 1 && instrument) || (kind.indexOfSelectedItem == 2 && !instrument) { return false }
      if category.indexOfSelectedItem > 0 && entry["category"] as? String != category.titleOfSelectedItem { return false }
      let text = ["name","format","category"].map { entry[$0] as? String ?? "" }.joined(separator: " ")
      return query.isEmpty || text.range(of: query, options: [.caseInsensitive, .diacriticInsensitive]) != nil
    }
    refreshing = true; table.reloadData()
    if let id, let row = visible.firstIndex(where: { $0["catalogID"] as? String == id }) { table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false) }
    else { table.deselectAll(nil) }
    refreshing = false; selectionChanged()
    status.stringValue = preferenceWarning.isEmpty ? "\(visible.count) of \(plugins.count) plugins · Favorites and categories stay with the plugin after a rescan." : preferenceWarning
  }
  func numberOfRows(in tableView: NSTableView) -> Int { visible.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard visible.indices.contains(row) else { return nil }; let entry = visible[row]
    let identifier = column?.identifier.rawValue ?? "name"
    let value: String
    if identifier == "kind" { value = "\(entry["format"] as? String ?? "") · \(entry["isInstrument"] as? Bool == true ? "Instrument" : "Effect")" }
    else if identifier == "category" { value = entry["category"] as? String ?? "" }
    else { value = (entry["favorite"] as? Bool == true ? "★ " : "") + (entry["name"] as? String ?? "Plugin") + (entry["hidden"] as? Bool == true ? " (hidden)" : "") }
    return Theme.label(value, size: 12)
  }
  func tableViewSelectionDidChange(_ notification: Notification) { if !refreshing { selectionChanged() } }
  func selectionChanged() {
    let entry = selected
    favorite.state = entry?["favorite"] as? Bool == true ? .on : .off
    hiddenToggle.state = entry?["hidden"] as? Bool == true ? .on : .off
    categoryField.stringValue = entry?["customCategory"] as? String ?? ""
    detail.stringValue = entry.map { "\($0["name"] as? String ?? "Plugin") · \($0["path"] as? String ?? "")" } ?? "Select a plugin"
    updateControls()
  }
  private func updateControls() {
    let enabled = selected != nil && !pending
    addButton?.isEnabled = enabled; saveButton?.isEnabled = enabled && !libraryRevision.isEmpty
    favorite.isEnabled = enabled && !libraryRevision.isEmpty; hiddenToggle.isEnabled = favorite.isEnabled; categoryField.isEnabled = favorite.isEnabled
  }
  func savePreferences() {
    guard !pending, let entry = selected, let id = entry["catalogID"] as? String, !libraryRevision.isEmpty, let onRequest else { return }
    let params: [String: Any] = ["expectedLibraryRevision": libraryRevision, "catalogID": id, "favorite": favorite.state == .on,
      "hidden": hiddenToggle.state == .on, "category": categoryField.stringValue]
    pending = true; updateControls()
    onRequest("plugin.library.set", params) { [weak self] response in
      guard let self else { return }; self.pending = false
      if let error = response["error"] as? [String: Any] {
        self.status.stringValue = (error["message"] as? String ?? "Preferences failed") + " Reload the list to refresh."; self.updateControls(); return
      }
      guard let data = (response["result"] as? [String: Any])?["data"] as? [String: Any],
        let revision = data["libraryRevision"] as? String, let preferences = data["preferences"] as? [String: Any] else { self.updateControls(); return }
      self.libraryRevision = revision
      for i in self.plugins.indices where self.plugins[i]["catalogID"] as? String == id {
        self.plugins[i]["favorite"] = preferences["favorite"]; self.plugins[i]["hidden"] = preferences["hidden"]
        let category = preferences["category"] as? String ?? ""
        self.plugins[i]["customCategory"] = category
        self.plugins[i]["category"] = category.isEmpty ? (self.plugins[i]["isInstrument"] as? Bool == true ? "Instruments" : "Effects") : category
      }
      self.rebuildCategories(); self.filter(keeping: self.selected?["catalogID"] as? String)
    }
  }
  func choose() {
    guard !pending, let entry = selected, let descriptor = entry["descriptor"] as? [String: Any] else { return }
    onChoose?(descriptor)
  }
}
