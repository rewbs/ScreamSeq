import AppKit

/// A take is session state until Add succeeds. Closing this window stops input,
/// but never silently throws away recorded audio or starts a second take.
final class SampleRecordingView: NSView, NSWindowDelegate, NSTextFieldDelegate {
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onPermission: ((@escaping (String?) -> Void) -> Void)?
  var onRevision: (() -> String)?
  // Main-thread document identity; nil means replacement/work is in flight.
  var onDocument: (() -> String?)?
  var onAdded: ((Int, Int) -> Void)?
  let devices = NSPopUpButton(), channels = NSPopUpButton()
  let name = NSTextField(string: "Recording")
  let instrument = NSButton(checkboxWithTitle: "Create an instrument", target: nil, action: nil)
  let limit = NSPopUpButton()
  let clock = Theme.label("0:00.0", size: 26, mono: true)
  let meter = NSLevelIndicator()
  let details = Theme.label("Mono or stereo input · no monitoring", size: 11, color: Theme.muted)
  let status = Theme.label("Choose an input, then Record. Your system audio settings stay unchanged.", size: 12)
  lazy var record = ActionButton("Record", symbol: "record.circle", prominent: true) { [weak self] in self?.start() }
  lazy var stop = ActionButton("Stop", symbol: "stop.fill") { [weak self] in self?.stopRecording() }
  lazy var add = ActionButton("Add to song", symbol: "plus", prominent: true) { [weak self] in self?.commit() }
  lazy var discard = ActionButton("Discard take") { [weak self] in self?.discardTake() }
  lazy var refresh = ActionButton("Refresh", symbol: "arrow.clockwise") { [weak self] in self?.loadDevices() }
  lazy var review = ActionButton("Review current take") { [weak self] in self?.reviewOperation() }
  lazy var acceptState = ActionButton("Accept observed state") { [weak self] in self?.acceptObservation() }
  lazy var discardSetup = ActionButton("Discard setup") { [weak self] in self?.resetSetup() }
  var take = "", takeDocument = "", capturing = false, frames = 0, pending = false
  var operation: SampleRecordingOperation?
  var baseline = SampleRecordingSetup(name: "Recording", instrument: false)
  var observedSetup = SampleRecordingSetup(name: "Recording", instrument: false)
  var setupContext: SampleRecordingContext?, permissionContext: SampleRecordingContext?
  var setupGeneration: UInt64 = 0
  var needsDocumentCompletion: Bool {
    syncSetup()
    return pending || permissionPending || operation != nil || capturing || !take.isEmpty || rawSetup != baseline || composingName
  }
  private var deviceData = [[String: Any]](), channelChoices = [(Int, Int)]()
  private var timer: Timer?
  var closing = false, permissionPending = false
  var polling = false, pollGeneration: UInt64 = 0
  var afterPoll: (() -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
    devices.setAccessibilityLabel("Recording input device"); channels.setAccessibilityLabel("Recording input channels")
    name.setAccessibilityLabel("Recorded sample name"); limit.setAccessibilityLabel("Recording time limit")
    name.delegate = self; instrument.target = self; instrument.action = #selector(setupChanged)
    devices.target = self; devices.action = #selector(deviceChanged)
    limit.addItems(withTitles: ["60 seconds", "5 minutes"])
    meter.levelIndicatorStyle = .continuousCapacity; meter.minValue = 0; meter.maxValue = 1
    meter.warningValue = 0.8; meter.criticalValue = 0.99; meter.setAccessibilityLabel("Recorded input peak")
    meter.fixed(height: 14)
    status.lineBreakMode = .byWordWrapping; status.maximumNumberOfLines = 4
    status.preferredMaxLayoutWidth = 580
    details.lineBreakMode = .byWordWrapping; details.maximumNumberOfLines = 2
    let input = stack(.horizontal, [labeled("INPUT", devices), labeled("CHANNELS", channels),
      refresh], spacing: 12)
    devices.widthAnchor.constraint(greaterThanOrEqualToConstant: 250).isActive = true
    channels.widthAnchor.constraint(greaterThanOrEqualToConstant: 190).isActive = true
    let capture = stack(.horizontal, [record, stop, NSView(), labeled("LIMIT", limit)], spacing: 10)
    let destination = stack(.horizontal, [instrument, NSView(), discard, add], spacing: 10)
    let content = stack(.vertical, [Theme.label("Record a sample", size: 18, weight: .semibold), input,
      stack(.horizontal, [clock, NSView()]), meter, details, capture,
      labeled("SAMPLE NAME", name), destination, stack(.horizontal, [review, acceptState, discardSetup], spacing: 10), status, NSView()], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 18)
    for index in keyboardOrder.indices { keyboardOrder[index].nextKeyView = keyboardOrder[(index + 1) % keyboardOrder.count] }
    updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  deinit { timer?.invalidate() }
  func activate() {
    closing = false
    if operation == nil { loadDevices(adoptTake: true) }
    focusEntry()
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
      guard let self, self.window?.isVisible == true else { return }; self.poll()
    }
  }
  // Read-only requests can share the deliberate-action queue. Writes have a
  // separate retained owner that survives missing replies and failed readback.
  private func request(_ method: String, _ params: [String: Any], done: @escaping ([String: Any]) -> Void) {
    guard !pending, !permissionPending, operation == nil, let onRequest else { return }
    pending = true; pollGeneration &+= 1; updateControls()
    let send = { [weak self] in
      guard let self else { return }
      onRequest(method, params) { [weak self] response in
        guard let self else { return }; self.pending = false
        if let data = (response["result"] as? [String: Any])?["data"] as? [String: Any] { done(data) }
        else { self.status.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "Could not read recording inputs. Refresh to try again." }
        self.updateControls()
      }
    }
    if polling { afterPoll = send } else { send() }
  }
  private var inputAvailable: Bool {
    guard deviceData.indices.contains(devices.indexOfSelectedItem) else { return false }
    return deviceData[devices.indexOfSelectedItem]["unavailable"] as? Bool != true
  }
  private func appendDevice(_ device: [String: Any]) {
    var title = device["name"] as? String ?? "Input"
    if device["unavailable"] as? Bool == true { title += " (disconnected)" }
    else if device["default"] as? Bool == true { title += " (default)" }
    // addItem(withTitle:) replaces an existing equal title. Each hardware UID
    // must keep its own item even when multiple interfaces have the same name.
    let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
    item.representedObject = device["id"] as? String; devices.menu?.addItem(item)
  }
  func loadDevices(adoptTake: Bool = false) {
    request("sample.recording.devices", [:]) { [weak self] data in
      guard let self else { return }
      let previous = self.devices.selectedItem?.representedObject as? String
      let previousDevice = self.deviceData.first { $0["id"] as? String == previous }
      self.deviceData = data["devices"] as? [[String: Any]] ?? []
      if let previous, var missing = previousDevice, !self.deviceData.contains(where: { $0["id"] as? String == previous }) {
        missing["unavailable"] = true; self.deviceData.append(missing)
      }
      self.devices.removeAllItems()
      for device in self.deviceData { self.appendDevice(device) }
      if let index = self.deviceData.firstIndex(where: { $0["id"] as? String == previous }) { self.devices.selectItem(at: index) }
      else if let index = self.deviceData.firstIndex(where: { $0["default"] as? Bool == true }) { self.devices.selectItem(at: index) }
      self.deviceChanged()
      if self.deviceData.isEmpty { self.status.stringValue = "No input device is available. Connect a microphone or audio interface, then refresh inputs." }
      else if !self.inputAvailable { self.status.stringValue = "The selected input is disconnected. Reconnect it or choose another input; your setup is retained." }
      self.poll(adopt: adoptTake)
    }
  }
  @objc func deviceChanged() {
    let previous = channelChoices.indices.contains(channels.indexOfSelectedItem) ? channelChoices[channels.indexOfSelectedItem] : nil
    channelChoices = []; channels.removeAllItems()
    guard deviceData.indices.contains(devices.indexOfSelectedItem) else { return }
    let count = min(256, max(0, deviceData[devices.indexOfSelectedItem]["channels"] as? Int ?? 0))
    for channel in 0..<count {
      channels.addItem(withTitle: "Mono · Input \(channel + 1)"); channelChoices.append((channel, 1))
      if channel + 1 < count { channels.addItem(withTitle: "Stereo · Inputs \(channel + 1)–\(channel + 2)"); channelChoices.append((channel, 2)) }
    }
    if let previous, let index = channelChoices.firstIndex(where: { $0 == previous }) { channels.selectItem(at: index) }
    updateControls()
  }
  func poll(adopt: Bool = false) {
    guard !pending, !permissionPending, operation == nil, !polling, let onRequest else { return }
    polling = true; let generation = pollGeneration, identity = take
    let params: [String: Any] = !adopt && !identity.isEmpty ? ["take": identity] : [:]
    onRequest("sample.recording.get", params) { [weak self] response in
      guard let self else { return }; self.polling = false
      if let action = self.afterPoll { self.afterPoll = nil; action(); return }
      guard generation == self.pollGeneration, !self.pending, !self.permissionPending, self.operation == nil else { return }
      if let error = response["error"] as? [String: Any] {
        if error["code"] as? Int != -32002 { self.status.stringValue = error["message"] as? String ?? "Cannot read the recorded take. Try reopening the recorder." }
        return
      }
      if let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] {
        guard adopt || identity.isEmpty || SampleRecordingRead.identity(data) == identity else {
          self.status.stringValue = "The take changed. Reopen the recorder to inspect the current take."; return
        }
        self.apply(data)
      }
    }
  }
  func apply(_ data: [String: Any]) {
    guard SampleRecordingRead.validTake(data) else {
      status.stringValue = "Could not validate the current take. The retained take is unchanged."; return
    }
    let wasCapturing = capturing
    takeDocument = data["documentId"] as? String ?? ""
    take = data["take"] as? String ?? ""; capturing = data["capturing"] as? Bool ?? false; frames = data["frames"] as? Int ?? 0
    let elapsed = data["seconds"] as? Double ?? 0
    let seconds = elapsed.isFinite ? min(300, max(0, elapsed)) : 0
    clock.stringValue = String(format: "%d:%04.1f", Int(seconds) / 60, seconds.truncatingRemainder(dividingBy: 60))
    meter.doubleValue = min(1, max(0, data["peak"] as? Double ?? 0))
    if !take.isEmpty {
      let rate = SampleRecordingRead.integer(data["sampleRate"], maximum: 768000) ?? 0, count = data["channels"] as? Int ?? 1
      let maximum = data["maxSeconds"] as? Double ?? 60
      if let device = data["device"] as? String, !device.isEmpty {
        let first = data["firstChannel"] as? Int ?? 0
        if !deviceData.contains(where: { $0["id"] as? String == device }) {
          let label = data["deviceName"] as? String ?? "Recorded input"
          let missing: [String: Any] = ["id": device, "name": label, "channels": first + count, "unavailable": true]
          deviceData.append(missing); appendDevice(missing)
        }
        if let index = deviceData.firstIndex(where: { $0["id"] as? String == device }) {
          if devices.indexOfSelectedItem != index { devices.selectItem(at: index); deviceChanged() }
          if let pair = channelChoices.firstIndex(where: { $0.0 == first && $0.1 == count }) { channels.selectItem(at: pair) }
        }
      }
      details.stringValue = "\(rate) Hz · \(count == 1 ? "Mono" : "Stereo") · \(frames) frames · limit \(String(format: "%.1f", maximum)) s"
    } else {
      details.stringValue = "Mono or stereo input · no monitoring"
    }
    if let error = data["error"] as? String, !error.isEmpty { status.stringValue = error }
    else if data["limitReached"] as? Bool == true { status.stringValue = "Recording limit reached. Add this take to your song or discard it." }
    else if capturing { status.stringValue = (data["clipped"] as? Int ?? 0) > 0 ? "Recording · input clipped. Lower the input level on your device." : "Recording · Stop keeps the take ready to add. No microphone monitoring." }
    else if wasCapturing { status.stringValue = "Take ready. Add creates a new sample; the original song audio stays intact." }
    updateControls()
  }
  func start() {
    syncSetup()
    guard !pending, !permissionPending, operation == nil, take.isEmpty, inputAvailable,
      channelChoices.indices.contains(channels.indexOfSelectedItem), let permission = onPermission,
      let context = currentContext, requireSetupDocument(context) else { return }
    let pair = channelChoices[channels.indexOfSelectedItem]
    let params: [String: Any] = ["device": devices.selectedItem?.representedObject as? String ?? "", "firstChannel": pair.0,
      "channels": pair.1, "maxSeconds": limit.indexOfSelectedItem == 1 ? 300 : 60, "expectedRevision": context.revision]
    permissionContext = context; permissionPending = true; pollGeneration &+= 1; updateControls()
    permission { [weak self] error in
      guard let self else { return }; self.syncSetup(); self.permissionPending = false; self.permissionContext = nil
      if let error { self.status.stringValue = error; self.updateControls(); return }
      guard self.onDocument?() == context.document else {
        self.status.stringValue = "The song changed while microphone access was requested. Press Record to start a take for this song."
        self.updateControls(); return
      }
      self.submit("sample.recording.start", params: params, context: context)
    }
  }
  func stopRecording() {
    guard !take.isEmpty, !pending, !permissionPending, operation == nil else { return }
    submit("sample.recording.stop", params: ["take": take])
  }
  func commit() {
    guard !pending, !permissionPending, operation == nil, !capturing, !take.isEmpty, frames > 0 else { return }
    syncSetup()
    guard !composingName else { status.stringValue = "Finish composing the sample name before adding the take."; return }
    guard let context = currentContext, requireSetupDocument(context), context.document == takeDocument else {
      status.stringValue = "The take or sample setup belongs to another song. The take and raw setup are retained."; return
    }
    let fields = rawSetup
    guard !fields.name.isEmpty, fields.name.utf8.count <= 128 else { status.stringValue = "Enter a sample name using 1–128 UTF-8 bytes."; return }
    submit("sample.recording.commit", params: ["take": take, "name": fields.name,
      "createInstrument": fields.instrument, "expectedRevision": context.revision], context: context)
  }
  func discardTake() {
    guard !pending, !permissionPending, operation == nil, !capturing, !take.isEmpty else { return }
    submit("sample.recording.discard", params: ["take": take])
  }
  func updateControls() {
    let idle = !pending && !permissionPending && operation == nil
    record.isEnabled = idle && take.isEmpty && inputAvailable && !channelChoices.isEmpty
    stop.isEnabled = idle && capturing; add.isEnabled = idle && !capturing && frames > 0 && !take.isEmpty
    discard.isEnabled = idle && !capturing && !take.isEmpty
    add.isEnabled = add.isEnabled && takeDocument == onDocument?()
    refresh.isEnabled = idle
    review.isHidden = operation == nil; review.isEnabled = !pending && operation != nil
    review.title = operation?.isCommit == true ? "Review Add result" : "Review current take"
    acceptState.isHidden = operation?.isCommit != true || operation?.receipt != nil
    acceptState.isEnabled = !pending && operation?.observation != nil
    discardSetup.isEnabled = idle
    for control in [devices, channels, limit] { control.isEnabled = idle && take.isEmpty }
    // These are local setup fields until Add captures them. Disabling an active
    // NSTextField for a device read aborts its field editor and loses raw text.
    // Keep setup editable through reads/capture; an in-flight Add owns its copy.
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    guard !pending && !permissionPending else { status.stringValue = "Finishing the recording operation…"; return false }
    guard operation == nil else {
      status.stringValue = "Review the recording result before closing. The microphone may still be active."; return false
    }
    if capturing { closing = true; stopRecording(); return false }
    return true
  }
  var keyboardOrder: [NSControl] { [devices, refresh, channels, limit, name, instrument, review, acceptState, record, stop, add, discard, discardSetup] }
  @discardableResult func advanceKeyboardFocus(reverse: Bool) -> Bool {
    guard let window, !composingName else { return false }
    let order = keyboardOrder, responder = window.firstResponder
    let current = order.firstIndex { control in
      responder === control || (responder != nil && responder === control.currentEditor()) ||
        (responder as? NSView)?.isDescendant(of: control) == true
    }
    let start = current ?? (reverse ? 0 : order.count - 1)
    for offset in 1...order.count {
      let index = (start + (reverse ? -offset : offset) + order.count) % order.count
      let control = order[index]
      if control.isEnabled, !control.isHiddenOrHasHiddenAncestor, window.makeFirstResponder(control) { return true }
    }
    return false
  }
  func focusTabEntry(reverse: Bool = false) {
    let order = reverse ? Array(keyboardOrder.reversed()) : keyboardOrder
    if let entry = order.first(where: { $0.isEnabled && !$0.isHiddenOrHasHiddenAncestor }) { window?.makeFirstResponder(entry) }
  }
  override var acceptsFirstResponder: Bool { true }
  override func keyDown(with event: NSEvent) {
    if event.keyCode == 48, !event.modifierFlags.contains(.command), !event.modifierFlags.contains(.control) {
      focusTabEntry(reverse: event.modifierFlags.contains(.shift))
    } else { super.keyDown(with: event) }
  }
  func focusEntry() {
    guard let window, window.isKeyWindow, window.firstResponder == nil || window.firstResponder === window || window.firstResponder === self else { return }
    focusTabEntry()
  }
  func windowDidBecomeKey(_ notification: Notification) { focusEntry() }
  func windowWillClose(_ notification: Notification) { timer?.invalidate(); timer = nil }
}

/// AppKit's default key loop can skip every non-text control when macOS full
/// keyboard navigation is off. This tool still exposes its complete workflow.
final class SampleRecordingWindow: NSWindow {
  override func sendEvent(_ event: NSEvent) {
    if event.type == .keyDown, event.keyCode == 48,
      event.modifierFlags.intersection([.command, .control, .option]).isEmpty,
      let recorder = contentView as? SampleRecordingView,
      recorder.advanceKeyboardFocus(reverse: event.modifierFlags.contains(.shift)) { return }
    super.sendEvent(event)
  }
}
