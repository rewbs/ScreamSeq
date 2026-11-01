import AppKit

// Settings and take review have separate lifetimes. Polling updates only the
// read-only take; it never replaces unfinished settings or chooses a new source.
final class MIDIRecordingEditor: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let source = NSPopUpButton(), grid = NSPopUpButton(), columns = NSPopUpButton()
  let latency = NSTextField(string: "0")
  let armed = NSButton(title: "Arm notes", target: nil, action: nil)
  let sound = Theme.label("",size:12)
  let status = Theme.label("", size: 12, color: Theme.muted), takeStatus = Theme.label("No recording take", size: 12)
  let table = DirectActionTable()
  var onSources: (() -> [[String: Any]])?
  var onReloadSettings: (() -> Void)?
  var onSettings: ((Int, Bool, Int, Int, Double, @escaping (String?) -> Void) -> Void)?
  var onArm: ((Bool, @escaping (String?) -> Void) -> Void)?, onChooseSound:(()->Void)?
  var onAcceptedArm:(()->Bool)?, onSound:(()->String)?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onFinish: ((String) -> Void)?, onDiscard: ((String) -> Void)?
  var onNavigate: (([String: Any], String) -> Void)?, onReturn: (() -> Void)?
  private(set) var take = "", revision = "", events = [[String: Any]](), pending = false
  private var capturing = false, initialized = false
  private(set) var settingsPending=false,acceptedArmed=false
  var settingsStamp:String {[String(source.selectedTag()),String(grid.selectedTag()),String(columns.selectedTag()),latency.stringValue].joined(separator:"\n")}
  private var timer: Timer?
  var finishButton: ActionButton!, discardButton: ActionButton!

  override init(frame: NSRect) {
    super.init(frame: frame)
    armed.target=self;armed.action=#selector(toggleArm);armed.bezelStyle = .rounded
    for (title, units) in [("Keep exact timing", 0), ("1/16 row", 4096), ("1/4 row", 16384), ("Whole row", 65536)] {
      grid.addItem(withTitle: title); grid.lastItem?.tag = units
    }
    for count in 1...127 { columns.addItem(withTitle: "\(count) note columns"); columns.lastItem?.tag = count }
    for (control, name) in [(source as NSControl, "MIDI source"), (grid, "Recording grid"), (columns, "Recording note columns"), (latency, "Input adjustment milliseconds")] { control.setAccessibilityLabel(name) }
    for (id, title, width) in [("patternID", "Pattern", 90.0), ("track", "Track", 90.0), ("position", "Row + offset", 130.0), ("note", "Note", 80.0), ("instrument", "Sound", 65.0), ("velocity", "Velocity", 65.0)] {
      let column = NSTableColumn(identifier: .init(id)); column.title = title; column.width = width; table.addTableColumn(column)
    }
    table.dataSource = self; table.delegate = self; table.rowHeight = 25
    table.setAccessibilityLabel("Recorded MIDI events")
    table.activate = { [weak self] in self?.openEvent() }
    let scroll = NSScrollView(); scroll.documentView = table; scroll.hasVerticalScroller = true; scroll.hasHorizontalScroller = true
    finishButton = ActionButton("Finish take") { [weak self] in guard let self, !self.take.isEmpty else { return }; self.onFinish?(self.take) }
    discardButton = ActionButton("Discard take") { [weak self] in guard let self, !self.take.isEmpty else { return }; self.onDiscard?(self.take) }
    let help = Theme.label("Armed playback captures precise timing. Stopped input enters at the cursor. Positive input adjustment places notes earlier. A take with lost input stays here for review; Finish saves its captured events as one Undo.", size: 11, color: Theme.muted)
    for label in [help, status, takeStatus] { label.maximumNumberOfLines = 3; label.lineBreakMode = .byWordWrapping; label.preferredMaxLayoutWidth = 660; label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal) }
    latency.fixed(width: 90)
    let body = stack(.vertical, [
      Theme.label("MIDI & recording", size: 20, weight: .semibold),
      stack(.horizontal, [Theme.label("Source", size: 12), source, ActionButton("Rescan") { [weak self] in self?.rescan() }]),
      stack(.horizontal,[armed,sound,ActionButton("Choose sound…"){[weak self] in self?.onChooseSound?()},NSView()]),
      stack(.horizontal, [grid, columns, Theme.label("Adjustment (ms)", size: 12), latency, NSView()]),
      stack(.horizontal, [ActionButton("Apply settings") { [weak self] in self?.applySettings() }, ActionButton("Reload settings") { [weak self] in self?.onReloadSettings?() }, status, NSView()]),
      help, takeStatus, scroll,
      stack(.horizontal, [ActionButton("Open event") { [weak self] in self?.openEvent() }, ActionButton("Refresh take") { [weak self] in self?.refresh() }, finishButton!, discardButton!, NSView(), ActionButton("Return to pattern") { [weak self] in self?.onReturn?() }]),
    ], spacing: 12)
    body.fill(self, inset: 20); body.stretchAcrossAxis(); updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  deinit { timer?.invalidate() }

  func configure(source: Int, armed: Bool, quantum: Int, columns: Int, latency: Double, reload: Bool = false) {
    guard !initialized || reload else { return }; initialized = true
    updateArm(armed)
    if grid.itemArray.allSatisfy({ $0.tag != quantum }) { grid.addItem(withTitle: "\(quantum)/65536 row"); grid.lastItem?.tag = quantum }
    grid.selectItem(withTag: quantum)
    self.columns.selectItem(withTag: columns); self.latency.stringValue = String(latency)
    rescan(selected: source)
  }
  func rescan(selected: Int? = nil) {
    let selected = selected ?? max(0, source.selectedTag())
    source.removeAllItems()
    let items = [["id": 0, "name": "Virtual input only"]] + (onSources?() ?? [])
    for item in items {
      let menuItem = NSMenuItem(title: item["name"] as? String ?? "MIDI source", action: nil, keyEquivalent: "")
      menuItem.tag = item["id"] as? Int ?? 0; source.menu?.addItem(menuItem)
    }
    if source.itemArray.allSatisfy({ $0.tag != selected }) {
      let missing = NSMenuItem(title: "Disconnected source \(selected)", action: nil, keyEquivalent: ""); missing.tag = selected; source.menu?.addItem(missing)
    }
    source.selectItem(withTag: selected)
  }
  func applySettings() {
    guard !settingsPending else{return}
    guard let adjustment = Double(latency.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)), adjustment.isFinite, (-500...500).contains(adjustment) else { status.stringValue = "Input adjustment must be −500 to +500 ms"; return }
    guard let apply = onSettings else { return }
    settingsPending=true;armed.isEnabled=false;status.stringValue="Applying settings…"
    apply(source.selectedTag(), acceptedArmed, grid.selectedTag(), columns.selectedTag(), adjustment){[weak self] error in
      guard let self else{return};self.settingsPending=false;self.armed.isEnabled=true
      self.status.stringValue=error ?? "Settings applied";if let accepted=self.onAcceptedArm?(){self.updateArm(accepted)}
    }
  }
  func updateArm(_ value:Bool){acceptedArmed=value;armed.title=value ? "Disarm notes":"Arm notes";armed.setAccessibilityValue(value ? "Armed":"Disarmed")}
  @objc func toggleArm(){
    guard !settingsPending,let onArm else{return}
    settingsPending=true;armed.isEnabled=false;status.stringValue="Changing arm state…"
    onArm(!acceptedArmed){[weak self] error in
      guard let self else{return};self.settingsPending=false;self.armed.isEnabled=true
      if let accepted=self.onAcceptedArm?(){self.updateArm(accepted)}
      self.status.stringValue=error ?? "Arm uses saved settings; unfinished timing and source fields are retained."
    }
  }
  static func lossSummary(_ data: [String: Any]) -> String? {
    let missing = data["missingTime"] as? Int ?? 0, exhausted = data["exhaustedVoices"] as? Int ?? 0, overflow = data["overflow"] as? Int ?? 0
    let reason = data["inputError"] as? String ?? ""
    guard missing > 0 || exhausted > 0 || overflow > 0 || !reason.isEmpty else { return nil }
    return "\(missing) missing timestamps · \(exhausted) unavailable voices · \(overflow) overflow" + (reason.isEmpty ? "" : " · \(reason)")
  }
  func startReview() {
    refresh()
    guard timer == nil else { return }
    let timer = Timer(timeInterval: 0.75, repeats: true) { [weak self] _ in
      guard let self, self.window?.isVisible == true else { return }; self.refresh()
    }
    self.timer = timer; RunLoop.main.add(timer, forMode: .common)
  }
  private func eventIdentity(_ event: [String: Any]) -> String {
    ["patternID", "track", "position", "note"].map { String(describing: event[$0] ?? "") }.joined(separator: ":")
  }
  func refresh() {
    if let accepted=onAcceptedArm?(){updateArm(accepted)}
    if let label=onSound?(),sound.stringValue != label{sound.stringValue=label}
    guard !pending, let request = onRequest else { return }; pending = true
    request("recording.get", [:]) { [weak self] reply in
      guard let self else { return }; self.pending = false
      guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any], let revision = result["revision"] as? String else { return }
      let nextTake = data["take"] as? String ?? "", nextEvents = data["events"] as? [[String: Any]] ?? []
      let selection = nextTake == self.take && self.events.indices.contains(self.table.selectedRow) ? self.eventIdentity(self.events[self.table.selectedRow]) : nil
      let origin = self.table.enclosingScrollView?.contentView.bounds.origin
      if nextTake != self.take || !NSArray(array: nextEvents).isEqual(to: self.events) {
        self.events = nextEvents; self.table.reloadData()
        if let selection, let index = nextEvents.firstIndex(where: { self.eventIdentity($0) == selection }) { self.table.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false) }
        else { self.table.deselectAll(nil) }
        if nextTake == self.take, let origin, let scroll = self.table.enclosingScrollView { scroll.contentView.scroll(to: origin); scroll.reflectScrolledClipView(scroll.contentView) }
      }
      self.take = nextTake; self.revision = revision; self.capturing = data["capturing"] as? Bool ?? false
      let summary = nextTake.isEmpty ? "No recording take" : "\(self.capturing ? "Recording" : "Retained take") · \(data["eventCount"] as? Int ?? nextEvents.count) events"
      let loss = Self.lossSummary(data)
      let stale = !nextTake.isEmpty && data["baseRevision"] as? String != revision
      let text = summary + (loss.map { "\n" + $0 } ?? "") + (stale ? "\nSong changed since this take; captured events remain available for review." : "")
      if self.takeStatus.stringValue != text { self.takeStatus.stringValue = text }
      self.updateControls()
    }
  }
  private func updateControls() { finishButton.isEnabled = !take.isEmpty; discardButton.isEnabled = !take.isEmpty }
  func openEvent() {
    guard events.indices.contains(table.selectedRow) else { return }
    onNavigate?(events[table.selectedRow], revision)
  }
  func numberOfRows(in tableView: NSTableView) -> Int { events.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    guard events.indices.contains(row), let column else { return nil }
    let event = events[row], id = column.identifier.rawValue
    let cell = tableView.makeView(withIdentifier: column.identifier, owner: self) as? NSTextField ?? Theme.label("", size: 12, mono: true)
    cell.identifier = column.identifier
    if id == "position" { let position = event[id] as? Int ?? 0; cell.stringValue = "\(position / 65536) + \(String(format: "%.5g", Double(position % 65536) / 65536))" }
    else if id == "note" { cell.stringValue = PreciseNote.name(event[id] as? Int ?? 0) }
    else { cell.stringValue = String(describing: event[id] ?? "—") }
    return cell
  }
}
