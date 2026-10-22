import AppKit

/// Native presentation of the shared retained-take API. A lifecycle click is
/// captured once, including its document, revision and loss acknowledgement.
/// Background reads may delay it, but must never redirect or silently drop it.
final class PluginRecordingView: NSView, NSTableViewDataSource, NSTableViewDelegate {
  typealias Reply = ([String: Any]) -> Void
  var onRequest: ((String, [String: Any], @escaping Reply) -> Void)?
  var onDocument: (() -> String?)?
  var onPlugins: (() -> [(id: String, name: String)])?
  var onDelivery: (() -> [String: Any])?
  var onDeliveryPage: ((Int) -> [String: Any])?
  var onDeliveryLoss: (() -> NSDictionary)?
  var onInputProtected: (() -> Bool)?
  let scope = NSPopUpButton()
  let table = NSTableView()
  let reviewMode = NSPopUpButton()
  lazy var previousInputs = ActionButton("Previous") { [weak self] in self?.changeInputPage(-1) }
  lazy var nextInputs = ActionButton("Next") { [weak self] in self?.changeInputPage(1) }
  let heading = Theme.label("Plugin automation take", size: 18, weight: .semibold)
  let status = Theme.label("Start song playback, then Record gestures from plugin windows.", size: 12)
  let detail = Theme.label("Stop ends this take without stopping song playback. Keep adds one Undo step.", size: 11, color: Theme.muted)
  let inputDetail = Theme.label("", size: 11, color: Theme.muted)
  let acknowledge = NSButton(checkboxWithTitle: "Keep the captured input despite the reported loss", target: nil, action: nil)
  lazy var record = ActionButton("Record", symbol: "record.circle", prominent: true) { [weak self] in self?.act(.start) }
  lazy var stop = ActionButton("Stop recording", symbol: "stop.fill") { [weak self] in self?.act(.stop) }
  lazy var keep = ActionButton("Keep take", prominent: true) { [weak self] in self?.act(.keep) }
  lazy var discard = ActionButton("Discard take") { [weak self] in self?.act(.discard) }
  lazy var refresh = ActionButton("Check take", symbol: "arrow.clockwise") { [weak self] in self?.poll(explicit: true) }
  lazy var recovery = ActionButton("Save recovery copy", symbol: "square.and.arrow.down") { [weak self] in self?.saveRecovery() }
  enum Action { case start, stop, keep, discard }
  private struct Intent {
    let action: Action, document: String, revision: String, take: String
    let capturing: Bool, completed: Bool, loss: NSDictionary, params: [String: Any]
  }
  private(set) var take = "", revision = "", document = ""
  private(set) var capturing = false, completed = false, retained = false
  private(set) var polling = false, pending = false, needsReadback = false
  private(set) var targets = [[String: Any]]()
  private var state = [String: Any](), queued: Intent?, timer: Timer?
  private var displayedDeliveryLoss = NSDictionary()
  private var generation: UInt64 = 0
  private(set) var inputRows = [[String: Any]](), inputOffset = 0, inputTotal = 0
  private var inputDocument = "", inputTake = ""
  var reviewingInputs: Bool { reviewMode.indexOfSelectedItem == 1 }
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
    scope.setAccessibilityLabel("Plugin recording scope")
    scope.addItem(withTitle: "All rack plugins")
    table.dataSource = self; table.delegate = self; table.rowHeight = 25
    table.usesAlternatingRowBackgroundColors = true; table.backgroundColor = Theme.bg
    table.setAccessibilityLabel("Recorded plugin parameters and lost input")
    reviewMode.addItems(withTitles: ["Recorded parameters", "Original native input"])
    reviewMode.setAccessibilityLabel("Plugin take review content")
    reviewMode.target = self; reviewMode.action = #selector(changeReviewMode)
    inputDetail.isSelectable = true
    for (id, title, width) in [("plugin", "Plugin", 210.0), ("parameter", "Parameter ID", 100.0),
                               ("events", "Captured", 75.0), ("loss", "Unmapped / overflow", 145.0), ("sourceLoss", "Source lost", 85.0)] {
      let column = NSTableColumn(identifier: .init(id)); column.title = title; column.width = width
      table.addTableColumn(column)
    }
    let scroll = verticalScrollView(); scroll.documentView = table
    // Compact windows retain four visible rows and native scrolling; paging
    // bounds original input to ten rows without widening the take window.
    scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 104).isActive = true
    acknowledge.target = self; acknowledge.action = #selector(acknowledgementChanged)
    acknowledge.setAccessibilityLabel("Acknowledge reported plugin recording loss")
    scope.widthAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true
    let content = stack(.vertical, [stack(.horizontal, [heading, NSView(), scope], spacing: 12),
      stack(.horizontal, [record, stop, NSView(), refresh], spacing: 8), detail,
      stack(.horizontal, [reviewMode, NSView(), previousInputs, nextInputs], spacing: 8), inputDetail, scroll, acknowledge,
      stack(.horizontal, [discard, recovery, NSView(), keep], spacing: 8), status], spacing: 10)
    content.stretchAcrossAxis(); content.fill(self, inset: 16)
    for label in [status, detail, inputDetail] {
      label.lineBreakMode = .byWordWrapping; label.maximumNumberOfLines = 3
      label.preferredMaxLayoutWidth = 620
      label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    }
    scope.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func changeReviewMode() {
    let titles = reviewingInputs ? ["Plugin", "Parameter ID", "Value", "Delivery", "Original host time"] :
      ["Plugin", "Parameter ID", "Captured", "Unmapped / overflow", "Source lost"]
    for (column, title) in zip(table.tableColumns, titles) { column.title = title }
    table.setAccessibilityLabel(reviewingInputs ? "Original native input; read only, never replayed" : "Recorded plugin parameters and lost input")
    table.deselectAll(nil); table.reloadData(); updateControls()
  }
  func changeInputPage(_ delta: Int) {
    guard reviewingInputs else { return }
    inputOffset = max(0, min(max(0, (inputTotal - 1) / 10 * 10), inputOffset + delta * 10))
    refreshInputPage(); table.deselectAll(nil); showInputDetail()
  }
  private func refreshInputPage() {
    guard reviewingInputs else { previousInputs.isHidden = true; nextInputs.isHidden = true; return }
    previousInputs.isHidden = false; nextInputs.isHidden = false
    let selected = inputRows.indices.contains(table.selectedRow) ? inputRows[table.selectedRow]["id"] as? String : nil
    var page = onDeliveryPage?(inputOffset) ?? [:]
    let nextDocument = page["document"] as? String ?? "", nextTake = page["take"] as? String ?? ""
    let sameOwner = nextDocument == inputDocument && nextTake == inputTake
    inputTotal = max(0, page["total"] as? Int ?? 0)
    let offset = !sameOwner ? 0 : min(inputOffset, max(0, (inputTotal - 1) / 10 * 10))
    if offset != inputOffset { inputOffset = offset; page = onDeliveryPage?(inputOffset) ?? [:] }
    let next = Array((page["events"] as? [[String: Any]] ?? []).prefix(10))
    if !sameOwner || !(next as NSArray).isEqual(to: inputRows) {
      inputDocument = nextDocument; inputTake = nextTake; inputRows = next; table.reloadData()
      if sameOwner, let selected, let row = inputRows.firstIndex(where: { $0["id"] as? String == selected }) {
        table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
      } else { table.deselectAll(nil) }
    }
    previousInputs.isEnabled = inputOffset > 0; nextInputs.isEnabled = inputOffset + inputRows.count < inputTotal
    showInputDetail()
  }
  private func showInputDetail() {
    guard reviewingInputs else { return }
    inputDetail.isHidden = false
    let text: String
    if inputRows.indices.contains(table.selectedRow) {
      let row = inputRows[table.selectedRow], plugin = row["plugin"] as? String ?? ""
      let name = onPlugins?().first(where: { $0.id == plugin })?.name ?? plugin
      text = "\(name) / Parameter \(row["parameter"] ?? "") · \(row["value"] ?? "")\n\(row["state"] ?? ""): \(row["error"] ?? "")\nOriginal host time: \(row["hostTime"] ?? "") · Read only; never replayed."
    } else {
      text = inputTotal == 0 ? "No original native input remains to review." :
        "\(inputOffset + 1)–\(inputOffset + inputRows.count) of \(inputTotal) original inputs. Select one to read its exact value, timestamp and delivery result."
    }
    if inputDetail.stringValue != text { inputDetail.stringValue = text; inputDetail.toolTip = text }
  }
  func tableViewSelectionDidChange(_ notification: Notification) { showInputDetail() }
  deinit { timer?.invalidate() }
  func activate() {
    if !retained && !pending { loadScopes() }
    poll(explicit: true)
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
      guard let self, self.window?.isVisible == true else { return }; self.poll()
    }
  }
  func loadScopes() {
    let selected = scope.selectedItem?.representedObject as? String
    scope.removeAllItems(); scope.addItem(withTitle: "All rack plugins")
    for plugin in onPlugins?() ?? [] {
      scope.addItem(withTitle: plugin.name); scope.lastItem?.representedObject = plugin.id
      if plugin.id == selected { scope.selectItem(at: scope.numberOfItems - 1) }
    }
  }
  private var loss: NSDictionary {
    ["rejected": state["rejected"] ?? 0, "failure": state["failure"] ?? 0,
     "targets": targets.map { ["plugin": $0["plugin"] ?? "", "parameter": $0["parameter"] ?? 0,
       "unmapped": $0["unmapped"] ?? 0, "overflow": $0["overflow"] ?? 0] },
     "sources": state["sources"] ?? [], "nativeInput": onDeliveryLoss?() ?? [:]] as NSDictionary
  }
  private var hasLoss: Bool {
    onDelivery?()["needsReview"] as? Bool == true ||
      ((state["rejected"] as? NSNumber)?.uint64Value ?? 0) > 0 ||
      ((state["failure"] as? NSNumber)?.uint64Value ?? 0) > 0 ||
      targets.contains { (($0["unmapped"] as? NSNumber)?.uint64Value ?? 0) > 0 || (($0["overflow"] as? NSNumber)?.uint64Value ?? 0) > 0 } ||
      (state["sources"] as? [[String: Any]] ?? []).contains { (($0["lost"] as? NSNumber)?.uint64Value ?? 0) > 0 }
  }
  @objc private func acknowledgementChanged() { updateControls() }
  private func decoded(_ response: [String: Any]) -> (revision: String, data: [String: Any])? {
    guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String, !revision.isEmpty,
      let data = result["data"] as? [String: Any], let retained = data["retained"] as? Bool,
      let capturing = data["capturing"] as? Bool, let take = data["take"] as? String,
      !capturing || retained else { return nil }
    if retained && (take.isEmpty || data["completed"] as? Bool == nil) { return nil }
    return (revision, data)
  }
  private func updateControls() {
    let currentLoss = onDeliveryLoss?() ?? [:]
    if !displayedDeliveryLoss.isEqual(currentLoss) { acknowledge.state = .off; displayedDeliveryLoss = currentLoss }
    let input = onDelivery?() ?? [:]
    if !reviewingInputs {
      inputDetail.isHidden = input.isEmpty || (!(onInputProtected?() ?? false) && !retained)
      inputDetail.stringValue = "Native controls: \(input["accepted"] ?? "0") accepted · \(input["waiting"] ?? 0) waiting · \(input["submitted"] ?? 0) in flight · \(input["refused"] ?? 0) refused · \(input["unknown"] ?? 0) uncertain · \(input["lost"] ?? "0") overflow. Refused and uncertain input is retained in recovery copies and is never replayed automatically."
    }
    let ready = !pending && !needsReadback && !revision.isEmpty && onDocument?() == document
    record.isEnabled = ready && !retained && !(onInputProtected?() ?? false) && !(onPlugins?().isEmpty ?? true)
    stop.isEnabled = ready && retained && capturing
    keep.isEnabled = ready && retained && !capturing && (completed || state["compatible"] as? Bool == true) &&
      (completed || !hasLoss || acknowledge.state == .on)
    discard.isEnabled = ready && retained && !capturing && !completed
    scope.isEnabled = !pending && !retained && !needsReadback
    acknowledge.isHidden = !retained || !hasLoss || completed
    acknowledge.isEnabled = ready && !capturing
    keep.title = completed ? "Finish restoration" : "Keep take"
    refresh.isEnabled = !pending && !polling
    recovery.isEnabled = !pending && !polling && (retained || (onInputProtected?() ?? false))
    refreshInputPage()
    // Polling does not disable action buttons or invalidate native field focus.
  }
  func saveRecovery() {
    guard recovery.isEnabled, let onRequest else { return }
    pending = true; updateControls()
    onRequest("recovery.save", [:]) { [weak self] response in
      guard let self else { return }; self.pending = false
      if response["result"] != nil { self.status.stringValue = "Recovery copy saved with retained original input." }
      else { self.status.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "The recovery copy could not be saved." }
      self.updateControls()
    }
  }
  private func accept(_ data: [String: Any], revision: String, document: String) {
    let oldLoss = loss, oldTake = take
    let selected = table.selectedRow >= 0 && table.selectedRow < targets.count ? targets[table.selectedRow] : nil
    self.document = document; self.revision = revision; state = data
    retained = data["retained"] as? Bool == true
    take = retained ? data["take"] as? String ?? "" : ""
    capturing = retained && data["capturing"] as? Bool == true
    completed = retained && data["completed"] as? Bool == true
    let updated = retained ? data["targets"] as? [[String: Any]] ?? [] : []
    if !(updated as NSArray).isEqual(to: targets) {
      targets = updated; if !reviewingInputs { table.reloadData() }
      if !reviewingInputs, let selected, let row = targets.firstIndex(where: {
        $0["plugin"] as? String == selected["plugin"] as? String && $0["parameter"] as? Int == selected["parameter"] as? Int
      }) { table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false) }
    }
    if oldTake != take || !oldLoss.isEqual(loss) { acknowledge.state = .off }
    let count = data["eventCount"] as? Int ?? 0
    let rejected = (data["rejected"] as? NSNumber)?.stringValue ?? "0"
    var sourceLost: UInt64 = 0, saturated = false
    for source in data["sources"] as? [[String: Any]] ?? [] {
      let addition = sourceLost.addingReportingOverflow((source["lost"] as? NSNumber)?.uint64Value ?? 0)
      if addition.overflow { sourceLost = .max; saturated = true } else { sourceLost = addition.partialValue }
    }
    let sourceCount = (saturated ? "at least " : "") + String(sourceLost)
    let boundary = ((data["failure"] as? NSNumber)?.uint64Value ?? 0) > 0 ? " · input boundary failed" : ""
    detail.stringValue = retained ? "\(count) captured inputs · \(targets.count) parameters · \(rejected) rejected · \(sourceCount) source lost\(boundary). Stop ends this take; song playback continues." :
      "Stop ends this take without stopping song playback. Keep adds one Undo step."
    if let failure = data["restorationError"] as? String ?? data["intakeError"] as? String {
      status.stringValue = failure
    } else if completed {
      status.stringValue = "The take is kept. Stop song playback, then finish native restoration; this adds no second Undo step."
    } else if retained && state["compatible"] as? Bool != true {
      status.stringValue = "The original song or plugin context has changed. Input remains available for recovery; this take cannot be kept in the changed context."
    } else if capturing { status.stringValue = "Recording plugin gestures. Closing this window retains the take." }
    else if retained { status.stringValue = hasLoss ? "Some input was lost. Review the counts before keeping this take." : "Take stopped. Keep it as one Undo step, or discard it." }
    else { status.stringValue = "Start song playback, then Record gestures from plugin windows." }
  }
  func poll(explicit: Bool = false) {
    guard !pending, !polling, let onRequest, let currentDocument = onDocument?() else { return }
    if needsReadback && !explicit { return }
    polling = true; generation &+= 1; let requestGeneration = generation
    onRequest("automation.recording.get", [:]) { [weak self] response in
      guard let self, self.generation == requestGeneration else { return }
      self.polling = false
      guard self.onDocument?() == currentDocument else {
        self.queued = nil; self.pending = false; self.revision = ""
        self.status.stringValue = "The song changed. Check its current take before recording."; self.updateControls(); return
      }
      guard let read = self.decoded(response) else {
        self.queued = nil; self.pending = false
        self.status.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "Cannot read the retained take. Check again."
        self.updateControls(); return
      }
      self.accept(read.data, revision: read.revision, document: currentDocument)
      if explicit { self.needsReadback = false }
      if let intent = self.queued { self.queued = nil; self.submit(intent) }
      self.updateControls()
    }
  }
  func act(_ action: Action) {
    guard !pending, !needsReadback, !revision.isEmpty, let currentDocument = onDocument?(), currentDocument == document else { return }
    switch action {
    case .start: guard !retained, !(onInputProtected?() ?? false) else { return }
    case .stop: guard retained && capturing else { return }
    case .keep: guard retained && !capturing && (completed || state["compatible"] as? Bool == true) &&
      (completed || !hasLoss || acknowledge.state == .on) else { return }
    case .discard: guard retained && !capturing && !completed else { return }
    }
    var params: [String: Any] = ["expectedRevision": revision]
    if action == .start {
      let selected = scope.selectedItem?.representedObject as? String
      let plugins = (onPlugins?() ?? []).filter { selected == nil || $0.id == selected }.map(\.id)
      guard !plugins.isEmpty else { status.stringValue = "Add a plugin to the rack before recording."; return }
      params["plugins"] = plugins
    } else { params["take"] = take }
    if action == .keep { params["acknowledgeLoss"] = acknowledge.state == .on }
    let intent = Intent(action: action, document: currentDocument, revision: revision, take: take,
      capturing: capturing, completed: completed, loss: loss, params: params)
    pending = true; updateControls()
    if polling { queued = intent } else { submit(intent) }
  }
  private func submit(_ intent: Intent) {
    guard onDocument?() == intent.document, document == intent.document, revision == intent.revision,
      take == intent.take, capturing == intent.capturing, completed == intent.completed,
      intent.action != .keep || intent.loss.isEqual(loss), let onRequest else {
      pending = false; status.stringValue = "The take changed before this action. Review it and choose again."; updateControls(); return
    }
    let operation: String
    switch intent.action { case .start: operation = "start"; case .stop: operation = "stop"; case .keep: operation = "commit"; case .discard: operation = "discard" }
    onRequest("automation.recording." + operation, intent.params) { [weak self] response in
      guard let self else { return }; self.pending = false
      guard self.onDocument?() == intent.document else {
        self.needsReadback = true; self.status.stringValue = "The song changed while this action completed. Check the current take."; self.updateControls(); return
      }
      if let read = self.decoded(response) {
        self.accept(read.data, revision: read.revision, document: intent.document)
      } else {
        // No automatic replay, including after a transport/error reply. An
        // explicit read observes the serial session after this completion.
        self.needsReadback = true
        self.status.stringValue = ((response["error"] as? [String: Any])?["message"] as? String ?? "The action's outcome is unavailable.") + " Check the take before choosing another action."
      }
      self.updateControls()
    }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { reviewingInputs ? inputRows.count : targets.count }
  func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
    if reviewingInputs {
      guard inputRows.indices.contains(row) else { return nil }; let input = inputRows[row]
      let text: String
      switch column?.identifier.rawValue {
      case "plugin":
        let id = input["plugin"] as? String ?? ""; text = onPlugins?().first(where: { $0.id == id })?.name ?? id
      case "parameter": text = "\(input["parameter"] ?? "")"
      case "events": text = "\(input["value"] ?? "")"
      case "sourceLoss": text = input["hostTime"] as? String ?? ""
      default: text = input["state"] as? String ?? ""
      }
      let label = Theme.label(text, size: 12); label.toolTip = text; return label
    }
    guard targets.indices.contains(row) else { return nil }; let target = targets[row]
    let text: String
    switch column?.identifier.rawValue {
    case "plugin":
      let id = target["plugin"] as? String ?? ""
      text = onPlugins?().first(where: { $0.id == id })?.name ?? id
    case "parameter": text = String(target["parameter"] as? Int ?? 0)
    case "events": text = String(target["eventCount"] as? Int ?? 0)
    case "sourceLoss":
      let source = (state["sources"] as? [[String: Any]] ?? []).first { $0["plugin"] as? String == target["plugin"] as? String }
      text = (source?["lost"] as? NSNumber)?.stringValue ?? "0"
    default: text = "\(target["unmapped"] as? Int ?? 0) / \(target["overflow"] as? Int ?? 0)"
    }
    let label = Theme.label(text, size: 12); label.toolTip = text; return label
  }
}
