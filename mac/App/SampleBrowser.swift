import AppKit

final class SampleBrowserTable: NSTableView {
  var onPreview: (() -> Void)?, onLoad: (() -> Void)?, onStop: (() -> Void)?
  override func keyDown(with event: NSEvent) {
    if event.keyCode == 49 && event.modifierFlags.intersection([.command,.control,.option,.shift]).isEmpty { onPreview?() }
    else if event.keyCode == 36 || event.keyCode == 76 { onLoad?() }
    else if event.keyCode == 53 { onStop?() }
    else { super.keyDown(with: event) }
  }
}

final class SampleBrowser: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let search = NSSearchField(), tagSearch = NSSearchField(), root = NSPopUpButton()
  let table = SampleBrowserTable(), tagTable = NSTableView(), waveform = WaveformView(frame: .zero)
  let autoPreview = NSButton(checkboxWithTitle: "Auto-preview", target: nil, action: nil)
  let createInstruments = NSButton(checkboxWithTitle: "Create an instrument for each sample", target: nil, action: nil)
  let volume = NSSlider(value: -12, minValue: -60, maxValue: 0, target: nil, action: nil)
  let volumeLabel = Theme.label("−12 dB", size: 11, mono: true)
  let summary = Theme.label("Indexing sample folders…", size: 12, color: Theme.muted)
  let filters = Theme.label("All folders", size: 11, color: Theme.muted)
  let detail = Theme.label("Select a sample to see its waveform", size: 13, weight: .medium)
  let metadata = Theme.label("↑/↓ browse · Space preview · Return load · ⌘-click or Shift-click to select several", size: 11, color: Theme.muted)
  let status = Theme.label("", size: 12, color: Theme.muted)
  let multisampleHint = Theme.label("Select a note-named sample to find related notes", size: 11, color: Theme.muted)
  private(set) var multisample: MultisampleGroup?
  private var groupGeneration = 0
  var multisampleButton: ActionButton!
  var onFindMultisample: ((String, @escaping (MultisampleGroup?) -> Void) -> Void)?
  var onImportMultisample: ((MultisampleGroup) -> Void)?
  private(set) var entries = [SampleLibraryEntry](), tags = [(String, Int)](), selectedTags = [String](), total = 0
  private(set) var importing = false
  private var roots = [String](), restoringSelection = false, searchGeneration = 0, detailGeneration = 0
  private var debounce: DispatchWorkItem?
  var loadButton: ActionButton!, moreButton: ActionButton!, previewButton: ActionButton!, rescanButton: ActionButton!
  var onSearch: ((SampleLibraryQuery, @escaping (SampleLibraryResults) -> Void) -> Void)?
  var onInspect: ((String, Bool, @escaping (Result<SampleAuditionData, Error>) -> Void) -> Void)?
  var onImport: (([String], Bool, @escaping (Result<Int, Error>) -> Void) -> Void)?
  var onAddFolder: (() -> Void)?, onRemoveFolder: ((String) -> Void)?, onRescan: (() -> Void)?
  var onChooseFiles: (() -> Void)?, onStop: (() -> Void)?, onVolume: ((Double) -> Void)?
  var selectedPaths: [String] { table.selectedRowIndexes.compactMap { entries.indices.contains($0) ? entries[$0].path : nil } }
  var selectedPath: String? { table.selectedRow >= 0 && entries.indices.contains(table.selectedRow) ? entries[table.selectedRow].path : nil }
  override init(frame: NSRect) {
    super.init(frame: frame); wantsLayer = true; layer?.backgroundColor = Theme.bg.cgColor
    search.placeholderString = "Search samples and folders — e.g. 808 \"bass drum\", junos chords, -maschine"
    search.delegate = self; search.sendsSearchStringImmediately = true; search.setAccessibilityLabel("Search sample names and parent folders")
    tagSearch.placeholderString = "Find a folder tag"; tagSearch.delegate = self; tagSearch.sendsSearchStringImmediately = true
    tagSearch.setAccessibilityLabel("Filter folder tags")
    root.target = self; root.action = #selector(changedRoot); root.setAccessibilityLabel("Sample library folder")
    root.addItem(withTitle: "All libraries"); root.widthAnchor.constraint(lessThanOrEqualToConstant: 460).isActive = true
    rescanButton = ActionButton("Rescan") { [weak self] in self?.onRescan?() }
    let locations = stack(.horizontal, [Theme.label("LIBRARY", size: 10, color: Theme.muted, weight: .semibold), root,
      ActionButton("Add folder…") { [weak self] in self?.onAddFolder?() },
      ActionButton("Remove folder") { [weak self] in guard let self, self.roots.indices.contains(self.root.indexOfSelectedItem - 1) else { return }; self.onRemoveFolder?(self.roots[self.root.indexOfSelectedItem - 1]) },
      NSView(), rescanButton], spacing: 10)
    let tagColumn = NSTableColumn(identifier: .init("tag")); tagColumn.title = "Folder tags"; tagColumn.width = 225; tagTable.addTableColumn(tagColumn)
    tagTable.delegate = self; tagTable.dataSource = self; tagTable.allowsMultipleSelection = true; tagTable.rowHeight = 27
    tagTable.setAccessibilityLabel("Folder tags inherited by samples; select several to match all")
    let tagScroll = NSScrollView(); tagScroll.documentView = tagTable; tagScroll.hasVerticalScroller = true
    let hint = Theme.label("Folder names act as tags. Select a pack or category to include all its nested samples. ⌘-click combines tags.", size: 11, color: Theme.muted)
    hint.maximumNumberOfLines = 4; hint.lineBreakMode = .byWordWrapping; hint.preferredMaxLayoutWidth = 235
    let tagsPane = stack(.vertical, [tagSearch, tagScroll, hint], spacing: 10); tagsPane.stretchAcrossAxis(); tagsPane.fixed(width: 245)
    for (id, title, width) in [("name", "Sample", 255.0), ("folder", "Folders", 380.0), ("size", "Size", 75.0)] {
      let column = NSTableColumn(identifier: .init(id)); column.title = title; column.width = width; table.addTableColumn(column)
    }
    table.dataSource = self; table.delegate = self; table.rowHeight = 28; table.allowsMultipleSelection = true
    table.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle; table.setAccessibilityLabel("Sample search results")
    table.target = self; table.doubleAction = #selector(loadSelection)
    table.onPreview = { [weak self] in self?.preview() }; table.onLoad = { [weak self] in self?.loadSelection() }
    table.onStop = { [weak self] in self?.stop() }
    let resultsScroll = NSScrollView(); resultsScroll.documentView = table; resultsScroll.hasVerticalScroller = true; resultsScroll.hasHorizontalScroller = true
    moreButton = ActionButton("Show more") { [weak self] in self?.requestSearch(append: true) }
    let resultPane = stack(.vertical, [resultsScroll, stack(.horizontal, [summary, NSView(), moreButton])], spacing: 8); resultPane.stretchAcrossAxis()
    let body = stack(.horizontal, [tagsPane, resultPane], spacing: 16)
    body.alignment = .top
    tagsPane.heightAnchor.constraint(equalTo: body.heightAnchor).isActive = true; resultPane.heightAnchor.constraint(equalTo: body.heightAnchor).isActive = true
    body.heightAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true
    filters.lineBreakMode = .byTruncatingMiddle
    let filtering = stack(.horizontal, [filters, NSView(), ActionButton("Clear filters") { [weak self] in self?.clearFilters() }])
    waveform.fixed(height: 80); waveform.setAccessibilityLabel("Selected sample preview waveform")
    detail.lineBreakMode = .byTruncatingMiddle; metadata.lineBreakMode = .byTruncatingTail
    previewButton = ActionButton("Preview", symbol: "play.fill") { [weak self] in self?.preview() }
    autoPreview.state = .on; autoPreview.target = self; autoPreview.action = #selector(changedAutoPreview)
    volume.target = self; volume.action = #selector(changedVolume); volume.fixed(width: 110); volumeLabel.fixed(width: 55)
    volume.setAccessibilityLabel("Preview volume in decibels")
    let audition = stack(.horizontal, [previewButton, ActionButton("Stop") { [weak self] in self?.stop() }, autoPreview,
      Theme.label("Preview level", size: 11, color: Theme.muted), volume, volumeLabel, NSView(),
      ActionButton("Reveal in Finder") { [weak self] in if let path = self?.selectedPath { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) } }], spacing: 10)
    loadButton = ActionButton("Load selected") { [weak self] in self?.loadSelection() }
    multisampleButton = ActionButton("Import as one instrument…") { [weak self] in
      guard let self, !self.importing, let group = self.multisample else { return }; self.stop(); self.onImportMultisample?(group)
    }
    multisampleHint.lineBreakMode = .byTruncatingMiddle
    let multisampling = stack(.horizontal, [multisampleHint, NSView(), multisampleButton], spacing: 12)
    let loading = stack(.horizontal, [ActionButton("Choose files…") { [weak self] in self?.onChooseFiles?() }, createInstruments, NSView(), loadButton], spacing: 12)
    status.maximumNumberOfLines = 2; status.lineBreakMode = .byWordWrapping
    let shortcuts = Theme.label("↑/↓ browse · Space preview · Return load · ⌘/Shift-click select", size: 11, color: Theme.muted)
    let content = stack(.vertical, [stack(.horizontal, [Theme.label("Sample library", size: 23, weight: .semibold), NSView(), shortcuts]),
      locations, search, filtering, body, detail, metadata, waveform, audition, multisampling, loading, status], spacing: 12)
    content.stretchAcrossAxis(); content.fill(self, inset: 22); updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  func updateLibrary(_ data: [String: Any]) {
    let selectedRoot = roots.indices.contains(root.indexOfSelectedItem - 1) ? roots[root.indexOfSelectedItem - 1] : nil
    roots = data["roots"] as? [String] ?? []; root.removeAllItems(); root.addItem(withTitle: "All libraries")
    for path in roots { root.addItem(withTitle: URL(fileURLWithPath: path).lastPathComponent); root.lastItem?.toolTip = path }
    if let selectedRoot, let index = roots.firstIndex(of: selectedRoot) { root.selectItem(at: index + 1) }
    rescanButton.isEnabled = data["indexing"] as? Bool != true
    if let error = data["error"] as? String { status.stringValue = error }
    else if data["indexing"] as? Bool == true { status.stringValue = "Indexing in the background. Cached results remain available." }
    else if let warning = (data["warnings"] as? [String])?.first { status.stringValue = warning }
    else { status.stringValue = "\(data["count"] as? Int ?? 0) indexed samples · Rescan after changing your sample folders." }
    requestSearch()
  }
  var query: SampleLibraryQuery {
    SampleLibraryQuery(text: search.stringValue, tags: selectedTags,
      root: roots.indices.contains(root.indexOfSelectedItem - 1) ? roots[root.indexOfSelectedItem - 1] : nil,
      tagText: tagSearch.stringValue)
  }
  func requestSearch(append: Bool = false) {
    debounce?.cancel(); searchGeneration += 1; let generation = searchGeneration
    var query = query; query.offset = append ? entries.count : 0
    onSearch?(query) { [weak self] result in
      guard let self, self.searchGeneration == generation else { return }
      self.updateResults(result, append: append)
    }
  }
  func updateResults(_ results: SampleLibraryResults, append: Bool = false) {
    let selected = Set(selectedPaths); restoringSelection = true
    entries = append ? entries + results.items : results.items; total = results.total; tags = results.tags
    table.reloadData(); tagTable.reloadData()
    let selectedRows = IndexSet(entries.indices.filter { selected.contains(entries[$0].path) })
    table.selectRowIndexes(selectedRows.isEmpty && !entries.isEmpty ? IndexSet(integer: 0) : selectedRows, byExtendingSelection: false)
    let selectedKeys = Set(selectedTags.map(SampleLibraryIndex.fold))
    tagTable.selectRowIndexes(IndexSet(tags.indices.filter { selectedKeys.contains(SampleLibraryIndex.fold(tags[$0].0)) }), byExtendingSelection: false)
    restoringSelection = false
    summary.stringValue = "\(entries.count) of \(total) matches"; moreButton.isHidden = entries.count >= total
    filters.stringValue = selectedTags.isEmpty ? "All folder tags · search matches names and ancestor folders" : "Tags: " + selectedTags.joined(separator: " + ")
    inspect(audible: false); findMultisample(); updateControls()
  }
  func controlTextDidChange(_ notification: Notification) {
    stop(); searchGeneration += 1; debounce?.cancel()
    let item = DispatchWorkItem { [weak self] in self?.requestSearch() }; debounce = item
    DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(120), execute: item)
  }
  func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
    if control === search, selector == #selector(NSResponder.moveDown(_:)), !entries.isEmpty {
      window?.makeFirstResponder(table); table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false); inspect(audible: autoPreview.state == .on); return true
    }
    return false
  }
  @objc func changedRoot() { stop(); selectedTags = []; requestSearch() }
  @objc func changedAutoPreview() { if autoPreview.state == .off { stop() } }
  @objc func changedVolume() { volumeLabel.stringValue = String(format: "%.0f dB", volume.doubleValue); onVolume?(volume.doubleValue) }
  func clearFilters() { stop(); search.stringValue = ""; tagSearch.stringValue = ""; selectedTags = []; root.selectItem(at: 0); requestSearch() }
  func numberOfRows(in tableView: NSTableView) -> Int { tableView === tagTable ? tags.count : entries.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    let id = NSUserInterfaceItemIdentifier(tableView === tagTable ? "tag" : column?.identifier.rawValue ?? "name")
    let label = tableView.makeView(withIdentifier: id, owner: self) as? NSTextField ?? Theme.label("", size: 12)
    label.identifier = id; label.lineBreakMode = .byTruncatingMiddle
    if tableView === tagTable {
      guard tags.indices.contains(row) else { return nil }; label.stringValue = "\(tags[row].0)  ·  \(tags[row].1)"; label.toolTip = tags[row].0
    } else {
      guard entries.indices.contains(row) else { return nil }; let item = entries[row]
      switch id.rawValue { case "folder": label.stringValue = item.folders.joined(separator: " / ")
      case "size": label.stringValue = ByteCountFormatter.string(fromByteCount: item.bytes, countStyle: .file)
      default: label.stringValue = item.name }
      label.toolTip = item.path
    }
    return label
  }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard !restoringSelection else { return }
    if notification.object as? NSTableView === tagTable {
      selectedTags = tagTable.selectedRowIndexes.compactMap { tags.indices.contains($0) ? tags[$0].0 : nil }; stop(); requestSearch()
    } else { inspect(audible: autoPreview.state == .on); findMultisample(); updateControls() }
  }
  func findMultisample() {
    groupGeneration += 1; let generation = groupGeneration; multisample = nil; updateControls()
    multisampleHint.stringValue = "Select a note-named sample to find related notes"
    guard let path = selectedPath, let onFindMultisample else { return }
    onFindMultisample(path) { [weak self] group in
      guard let self, self.groupGeneration == generation, self.selectedPath == path else { return }
      self.multisample = group
      self.multisampleHint.stringValue = group.map { "Detected \($0.members.count) note variations · \($0.name)" } ?? "No related note variations detected in this folder"
      self.updateControls()
    }
  }
  func stop() { detailGeneration += 1; onStop?() }
  func preview() { inspect(audible: true) }
  private func inspect(audible: Bool) {
    stop(); let generation = detailGeneration
    guard let path = selectedPath else { waveform.peaks = []; waveform.frames = 0; detail.stringValue = "No matching samples"; detail.toolTip = nil; metadata.stringValue = "Try fewer search terms or clear the folder filters."; return }
    detail.stringValue = path; detail.toolTip = path; metadata.stringValue = "Preparing waveform…"
    waveform.peaks = []; waveform.frames = 0
    onInspect?(path, audible) { [weak self] result in
      guard let self, self.detailGeneration == generation, self.selectedPath == path else { return }
      switch result {
      case .success(let data):
        self.waveform.frames = data.frames; self.waveform.peaks = data.peaks
        self.metadata.stringValue = String(format: "%.3f s · %.0f Hz · %@%@", data.seconds, data.rate, data.channels == 1 ? "Mono" : "Stereo",
          data.frames < data.totalFrames ? String(format: " · Preview: first %.1f s", data.previewSeconds) : "")
      case .failure(let error): self.waveform.peaks = []; self.waveform.frames = 0; self.metadata.stringValue = error.localizedDescription
      }
    }
  }
  @objc func loadSelection() {
    loadPaths(selectedPaths)
  }
  func loadPaths(_ paths: [String]) {
    guard !importing, !paths.isEmpty, let onImport else { return }
    guard paths.count <= 128 else { status.stringValue = "Load at most 128 samples at a time. Choose a smaller batch."; return }
    stop(); importing = true; updateControls(); status.stringValue = "Loading \(paths.count) sample\(paths.count == 1 ? "" : "s")…"
    onImport(paths, createInstruments.state == .on) { [weak self] result in
      guard let self else { return }; self.importing = false; self.updateControls()
      switch result { case .success(let count): self.status.stringValue = "Loaded \(count) sample\(count == 1 ? "" : "s") · one Undo step"
      case .failure(let error): self.status.stringValue = error.localizedDescription }
    }
  }
  private func updateControls() {
    let count = selectedPaths.count
    loadButton?.title = count > 0 ? "Load \(count) selected" : "Load selected"
    loadButton?.isEnabled = !importing && count > 0 && count <= 128
    loadButton?.toolTip = "Loads up to 128 selected files into new sample slots, with one Undo step"
    previewButton?.isEnabled = count > 0; createInstruments.isEnabled = !importing
    multisampleButton?.isEnabled = !importing && multisample != nil
    if count > 128 { status.stringValue = "Select at most 128 samples per load. ⌘-click or Shift-click to choose a smaller batch." }
  }
  override func performKeyEquivalent(with event: NSEvent) -> Bool {
    if event.modifierFlags.contains(.command), event.charactersIgnoringModifiers == "f" { window?.makeFirstResponder(search); return true }
    return super.performKeyEquivalent(with: event)
  }
}
