import AppKit

/// One captured instrument (or one creation), independent of cursor and rack selection.
final class InstrumentPluginEditor: NSView, NSTextFieldDelegate {
  let plugin = NSPopUpButton(), channel = NSPopUpButton(), status = Theme.label("", size: 12, color: Theme.muted)
  let name = NSTextField(string: "Plugin instrument"), workflow: InstrumentPluginWorkflow
  let isCreating: Bool
  var instrument: Int { workflow.target.slot }
  var revision: String { workflow.target.context.revision }
  var pending: Bool { workflow.pending }
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)? {
    didSet { workflow.onRequest = onRequest }
  }
  var onSaved: ((Int) -> Void)?, onAddPlugin: (() -> Void)?, onRack: (() -> Void)?
  var applyButton: ActionButton!, reviewButton: ActionButton!, acceptButton: ActionButton!, reloadButton: ActionButton!, addButton: ActionButton!
  private(set) var generation: UInt64 = 0
  private var baseline: InstrumentPluginFields, setting = false
  var rawFields: InstrumentPluginFields {
    .init(name: isCreating ? ((name.currentEditor() as? NSTextView)?.string ?? name.stringValue) : "",
      plugin: plugin.selectedItem?.representedObject as? String ?? "", channel: channel.selectedTag())
  }
  var draft: InstrumentPluginDraft { .init(generation: generation, fields: rawFields) }
  var composing: Bool { (name.currentEditor() as? NSTextView)?.hasMarkedText() == true }
  var hasDraft: Bool {
    guard !workflow.retired else { return false }
    let fields = rawFields
    return fields.name != baseline.name || fields.plugin != baseline.plugin || (!fields.plugin.isEmpty && fields.channel != baseline.channel)
  }
  var needsCompletion: Bool { !workflow.retired && (workflow.needsCompletion || hasDraft || composing) }
  init(instrument: Int, model: PatternModel, selectedPlugin: String? = nil) {
    isCreating = instrument == 0
    let context = SampleRecordingContext(document: String(model.revisionToken.split(separator: ":").first ?? ""), revision: model.revisionToken)
    workflow = InstrumentPluginWorkflow(target: .init(context: context, slot: instrument,
      identity: model.instruments.first(where: { $0["index"] as? Int == instrument })?["id"] as? String ?? ""))
    baseline = .init(name: "", plugin: "", channel: 1)
    super.init(frame: NSRect(x: 0, y: 0, width: 640, height: 370))
    plugin.addItem(withTitle: "No plugin · use sample keymap"); plugin.lastItem?.representedObject = ""
    var selectedChannel = 1
    for item in model.nativePlugins where item["isInstrument"] as? Bool == true {
      guard let id = item["instanceID"] as? String else { continue }
      plugin.addItem(withTitle: item["name"] as? String ?? "Instrument plugin"); plugin.lastItem?.representedObject = id
      let route = (item["instrumentAssignments"] as? [[String: Any]] ?? []).first { $0["instrument"] as? Int == instrument }
      if (instrument > 0 && (route != nil || item["instrument"] as? Int == instrument)) || id == selectedPlugin {
        plugin.selectItem(at: plugin.numberOfItems - 1); selectedChannel = route?["channel"] as? Int ?? 1
      }
    }
    if isCreating, selectedPlugin == nil, plugin.numberOfItems > 1 { plugin.selectItem(at: 1) }
    for n in 1...16 { channel.addItem(withTitle: "MIDI channel \(n)"); channel.lastItem?.tag = n }
    channel.selectItem(withTag: selectedChannel)
    if isCreating { name.stringValue = plugin.indexOfSelectedItem > 0 ? plugin.titleOfSelectedItem ?? "Plugin instrument" : "Plugin instrument" }
    baseline = rawFields
    plugin.setAccessibilityLabel("Instrument sound source"); channel.setAccessibilityLabel("Instrument MIDI channel")
    name.setAccessibilityLabel("New plugin instrument name"); name.delegate = self
    plugin.target = self; plugin.action = #selector(assignmentChanged); channel.target = self; channel.action = #selector(assignmentChanged)
    applyButton = ActionButton(isCreating ? "Create & assign instrument" : "Apply assignment", prominent: true) { [weak self] in self?.apply() }
    reviewButton = ActionButton("Review assignment") { [weak self] in self?.workflow.review() }
    acceptButton = ActionButton("Accept observed assignment") { [weak self] in self?.workflow.accept() }
    reloadButton = ActionButton("Reload / discard") { [weak self] in self?.workflow.reload() }
    addButton = ActionButton("Add instrument plugin…", symbol: "plus") { [weak self] in guard let self, !self.workflow.needsCompletion, !self.composing else { return }; self.onAddPlugin?() }
    let rack = ActionButton("Plugin rack…") { [weak self] in self?.onRack?() }
    status.maximumNumberOfLines = 5; status.lineBreakMode = .byWordWrapping; status.preferredMaxLayoutWidth = 600
    status.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    status.stringValue = isCreating ? "Create one empty tracker instrument, then assign this plugin. Each write has its own Undo step." : "Plugin and channel changes save immediately. The sample keymap and other assignments are preserved."
    let body = stack(.vertical, [Theme.label(isCreating ? "New plugin trigger instrument" : "Instrument \(instrument) · Sound source", size: 18, weight: .semibold),
      name, stack(.horizontal, [plugin, addButton!]), stack(.horizontal, [channel, rack]), status,
      stack(.horizontal, [reloadButton!, NSView(), applyButton!]), stack(.horizontal, [reviewButton!, acceptButton!, NSView()])], spacing: 12)
    name.isHidden = !isCreating; body.stretchAcrossAxis(); body.fill(self, inset: 20)
    workflow.onDraft = { [weak self] in self!.draft }; workflow.composing = { [weak self] in self?.composing == true }
    workflow.onChange = { [weak self] in guard let self else { return }; self.status.stringValue = self.workflow.status; self.updateControls() }
    workflow.onObserved = { [weak self] snapshot, slot, observedName, replace in self?.adopt(snapshot, slot: slot, name: observedName, replace: replace) }
    workflow.onSaved = { [weak self] slot, _ in self?.onSaved?(slot) }
    workflow.onRetired = { [weak self] in self?.window?.close() }
    updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  func controlTextDidChange(_ notification: Notification) { generation &+= 1; updateControls() }
  @objc private func assignmentChanged() {
    guard !setting else { return }; generation &+= 1; updateControls()
    if !isCreating && !workflow.needsCompletion { apply() }
  }
  func apply() {
    if workflow.needsCompletion { workflow.apply(); return }
    if isCreating, instrument > 0, rawFields.name != baseline.name {
      status.stringValue = "Instrument already created. Newer name text is kept; Reload / discard before another assignment. Rename the instrument in its inspector."; return
    }
    workflow.apply()
  }
  func refreshIfClean() {
    guard !needsCompletion, workflow.onContext?()?.document == workflow.target.context.document,
      workflow.onContext?() != workflow.target.context, name.currentEditor() == nil else { return }
    workflow.reload()
  }
  private func adopt(_ snapshot: InstrumentPluginSnapshot, slot: Int, name observedName: String, replace: Bool) {
    let route = snapshot.routes[slot]
    baseline = .init(name: isCreating ? observedName : "", plugin: route?.plugin ?? "", channel: route?.channel ?? rawFields.channel)
    if replace {
      setting = true
      let desired = slot == 0 ? rawFields.plugin : route?.plugin ?? ""
      plugin.removeAllItems(); plugin.addItem(withTitle: "No plugin · use sample keymap"); plugin.lastItem?.representedObject = ""
      for item in snapshot.plugins { plugin.addItem(withTitle: item.name); plugin.lastItem?.representedObject = item.identity }
      if let item = plugin.itemArray.first(where: { $0.representedObject as? String == desired }) { plugin.select(item) }
      else if slot == 0, plugin.numberOfItems > 1 { plugin.selectItem(at: 1) }
      if slot > 0 { channel.selectItem(withTag: route?.channel ?? 1) }
      if isCreating && rawFields.name != observedName {
        let editor = name.currentEditor() as? NSTextView, caret = editor?.selectedRange()
        name.stringValue = observedName
        if let editor, let caret {
          editor.string = observedName
          let location = min(caret.location, observedName.utf16.count)
          editor.setSelectedRange(NSRange(location: location, length: min(caret.length, observedName.utf16.count - location)))
        }
      }
      generation &+= 1; setting = false
      if slot == 0 { baseline = rawFields }
    }
    updateControls()
  }
  private func updateControls() {
    acceptButton?.title = workflow.observedRemoval ? "Discard removed assignment" : "Accept observed assignment"
    func enabled(_ control: NSControl?, _ available: Bool) {
      guard let control else { return }; control.isEnabled = available || window?.firstResponder === control || (control as? NSTextField)?.currentEditor() != nil
    }
    enabled(plugin, !workflow.needsCompletion); enabled(channel, !workflow.needsCompletion && !rawFields.plugin.isEmpty)
    enabled(name, !workflow.needsCompletion && instrument == 0)
    applyButton?.title = instrument == 0 ? "Create & assign instrument" : "Apply assignment"
    enabled(applyButton, !workflow.needsCompletion && (instrument > 0 || !rawFields.plugin.isEmpty))
    enabled(addButton, !workflow.needsCompletion); enabled(reloadButton, !workflow.needsCompletion)
    for (button, available) in [(reviewButton, workflow.canReview), (acceptButton, workflow.canAccept)] {
      guard let button else { continue }
      if !workflow.needsCompletion, window?.firstResponder === button { window?.makeFirstResponder(plugin) }
      button.isHidden = !workflow.needsCompletion; enabled(button, available)
    }
  }
}
