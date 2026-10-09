import AppKit

/// A take is session state until Add succeeds. Closing this window stops input,
/// but never silently throws away recorded audio or starts a second take.
final class SampleRecordingView: NSView, NSWindowDelegate {
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
  private(set) var take = "", capturing = false, frames = 0, pending = false
  private var deviceData = [[String: Any]](), channelChoices = [(Int, Int)]()
  private var timer: Timer?, closing = false, permissionPending = false
  private var polling = false, pollGeneration: UInt64 = 0
  private var afterPoll: (() -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
    devices.setAccessibilityLabel("Recording input device"); channels.setAccessibilityLabel("Recording input channels")
    name.setAccessibilityLabel("Recorded sample name"); limit.setAccessibilityLabel("Recording time limit")
    devices.target = self; devices.action = #selector(deviceChanged)
    limit.addItems(withTitles: ["60 seconds", "5 minutes"])
    meter.levelIndicatorStyle = .continuousCapacity; meter.minValue = 0; meter.maxValue = 1
    meter.warningValue = 0.8; meter.criticalValue = 0.99; meter.setAccessibilityLabel("Recorded input peak")
    meter.fixed(height: 14)
    status.lineBreakMode = .byWordWrapping; status.maximumNumberOfLines = 4
    status.preferredMaxLayoutWidth = 580
    details.lineBreakMode = .byWordWrapping; details.maximumNumberOfLines = 2
    let input = stack(.horizontal, [labeled("INPUT", devices), labeled("CHANNELS", channels),
      ActionButton("Refresh", symbol: "arrow.clockwise") { [weak self] in self?.loadDevices() }], spacing: 12)
    devices.widthAnchor.constraint(greaterThanOrEqualToConstant: 250).isActive = true
    channels.widthAnchor.constraint(greaterThanOrEqualToConstant: 190).isActive = true
    let capture = stack(.horizontal, [record, stop, NSView(), labeled("LIMIT", limit)], spacing: 10)
    let destination = stack(.horizontal, [instrument, NSView(), discard, add], spacing: 10)
    let content = stack(.vertical, [Theme.label("Record a sample", size: 18, weight: .semibold), input,
      stack(.horizontal, [clock, NSView()]), meter, details, capture,
      labeled("SAMPLE NAME", name), destination, status, NSView()], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 18); updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  deinit { timer?.invalidate() }
  func activate() {
    closing = false; loadDevices(); poll()
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
      guard let self, self.window?.isVisible == true else { return }; self.poll()
    }
  }
  private func request(_ method: String, _ params: [String: Any], document: String? = nil, done: @escaping ([String: Any]) -> Void) {
    guard !pending, let onRequest else { return }
    pending = true; pollGeneration &+= 1; updateControls()
    let send = { [weak self] in
      guard let self else { return }
      if let document, self.onDocument?() != document {
        self.pending = false; self.closing = false
        self.status.stringValue = "The song changed before recording could start. Press Record to start a take for this song."
        self.updateControls(); return
      }
      var currentParams = params
      if currentParams["expectedRevision"] != nil { currentParams["expectedRevision"] = self.onRevision?() ?? "" }
      onRequest(method, currentParams) { [weak self] response in
        guard let self else { return }; self.pending = false
        if let error = response["error"] as? [String: Any] {
          self.status.stringValue = error["message"] as? String ?? "Recording operation failed. The take is retained."
          self.closing = false; self.updateControls(); return
        }
        guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
          self.status.stringValue = "No recording response. Reopen this window to check the take."
          self.closing = false; self.updateControls(); return
        }
        done(data); self.updateControls()
      }
    }
    // Reads must never blink buttons/field editors off. Serialize a deliberate
    // action after the current poll instead of dropping it as a busy request.
    if polling { afterPoll = send } else { send() }
  }
  func loadDevices() {
    request("sample.recording.devices", [:]) { [weak self] data in
      guard let self else { return }
      let previous = self.devices.selectedItem?.representedObject as? String
      self.deviceData = data["devices"] as? [[String: Any]] ?? []
      self.devices.removeAllItems()
      for device in self.deviceData {
        let title = (device["name"] as? String ?? "Input") + (device["default"] as? Bool == true ? " (default)" : "")
        self.devices.addItem(withTitle: title); self.devices.lastItem?.representedObject = device["id"] as? String
      }
      if let index = self.deviceData.firstIndex(where: { $0["id"] as? String == previous }) { self.devices.selectItem(at: index) }
      else if let index = self.deviceData.firstIndex(where: { $0["default"] as? Bool == true }) { self.devices.selectItem(at: index) }
      self.deviceChanged()
      if self.deviceData.isEmpty { self.status.stringValue = "No input device is available. Connect a microphone or audio interface, then refresh inputs." }
      self.poll()
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
  func poll() {
    guard !pending, !permissionPending, !polling, let onRequest else { return }
    polling = true; let generation = pollGeneration
    onRequest("sample.recording.get", [:]) { [weak self] response in
      guard let self else { return }; self.polling = false
      if let action = self.afterPoll { self.afterPoll = nil; action(); return }
      guard generation == self.pollGeneration, !self.pending, !self.permissionPending else { return }
      if let error = response["error"] as? [String: Any] {
        if error["code"] as? Int != -32002 { self.status.stringValue = error["message"] as? String ?? "Cannot read the recorded take. Try reopening the recorder." }
        return
      }
      if let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] { self.apply(data) }
    }
  }
  func apply(_ data: [String: Any]) {
    let wasCapturing = capturing
    take = data["take"] as? String ?? ""; capturing = data["capturing"] as? Bool ?? false; frames = data["frames"] as? Int ?? 0
    let seconds = max(0, data["seconds"] as? Double ?? 0)
    clock.stringValue = String(format: "%d:%04.1f", Int(seconds) / 60, seconds.truncatingRemainder(dividingBy: 60))
    meter.doubleValue = min(1, max(0, data["peak"] as? Double ?? 0))
    if !take.isEmpty {
      let rate = Int(data["sampleRate"] as? Double ?? 0), count = data["channels"] as? Int ?? 1
      let maximum = data["maxSeconds"] as? Double ?? 60
      if let device = data["device"] as? String, !device.isEmpty {
        let first = data["firstChannel"] as? Int ?? 0
        if !deviceData.contains(where: { $0["id"] as? String == device }) {
          let label = data["deviceName"] as? String ?? "Recorded input"
          deviceData.append(["id": device, "name": label, "channels": first + count])
          devices.addItem(withTitle: label); devices.lastItem?.representedObject = device
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
    guard !pending, !permissionPending, take.isEmpty, channelChoices.indices.contains(channels.indexOfSelectedItem), let permission = onPermission else { return }
    guard let document = onDocument?(), !document.isEmpty else {
      status.stringValue = "Wait for the song operation to finish, then Record again."; return
    }
    permissionPending = true; pollGeneration &+= 1; updateControls()
    permission { [weak self] error in
      guard let self else { return }; self.permissionPending = false
      if let error { self.status.stringValue = error; self.updateControls(); return }
      guard self.onDocument?() == document else {
        self.status.stringValue = "The song changed while microphone access was requested. Press Record to start a take for this song."
        self.updateControls(); return
      }
      guard self.channelChoices.indices.contains(self.channels.indexOfSelectedItem) else { self.updateControls(); return }
      let pair = self.channelChoices[self.channels.indexOfSelectedItem]
      let p: [String: Any] = ["device": self.devices.selectedItem?.representedObject as? String ?? "", "firstChannel": pair.0,
        "channels": pair.1, "maxSeconds": self.limit.indexOfSelectedItem == 1 ? 300 : 60, "expectedRevision": self.onRevision?() ?? ""]
      self.request("sample.recording.start", p, document: document) { [weak self] data in self?.apply(data) }
    }
  }
  func stopRecording() {
    guard !take.isEmpty, !pending else { return }
    request("sample.recording.stop", ["take": take]) { [weak self] data in
      guard let self else { return }; self.apply(data)
      if self.closing { self.closing = false; self.window?.close() }
    }
  }
  func commit() {
    guard !pending, !capturing, !take.isEmpty, frames > 0 else { return }
    request("sample.recording.commit", ["take": take, "name": name.stringValue, "createInstrument": instrument.state == .on,
      "expectedRevision": onRevision?() ?? ""]) { [weak self] data in
      guard let self, let sample = data["sample"] as? Int else { return }
      self.take = ""; self.frames = 0; self.meter.doubleValue = 0
      self.status.stringValue = "Added sample \(sample) · ⌘Z undoes the sample and instrument together."
      self.onAdded?(sample, data["instrument"] as? Int ?? 0)
    }
  }
  func discardTake() {
    guard !pending, !capturing, !take.isEmpty else { return }
    request("sample.recording.discard", ["take": take]) { [weak self] data in
      self?.apply(["take": "", "capturing": false, "frames": 0]); self?.status.stringValue = "Take discarded. Ready to record."
    }
  }
  private func updateControls() {
    let idle = !pending && !permissionPending
    record.isEnabled = idle && take.isEmpty && !channelChoices.isEmpty
    stop.isEnabled = idle && capturing; add.isEnabled = idle && !capturing && frames > 0 && !take.isEmpty
    discard.isEnabled = idle && !capturing && !take.isEmpty
    for control in [devices, channels, limit] { control.isEnabled = idle && take.isEmpty }
    name.isEnabled = idle && !capturing; instrument.isEnabled = idle && !capturing
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    guard !pending && !permissionPending else { status.stringValue = "Finishing the recording operation…"; return false }
    if capturing { closing = true; stopRecording(); return false }
    return true
  }
  func windowWillClose(_ notification: Notification) { timer?.invalidate(); timer = nil }
}
