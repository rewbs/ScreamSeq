import AppKit

// Compare against displayed values rather than focus: a musician can leave a
// text field to edit the pattern without losing its uncommitted contents.
final class AssetFieldDraft {
  let controls: [String: NSControl]
  private var baseline = [String: String](), index: Int?
  init(_ controls: [String: NSControl]) { self.controls = controls }
  private func value(_ control: NSControl) -> String {
    if let button = control as? NSPopUpButton { return String(button.indexOfSelectedItem) }
    if let button = control as? NSButton { return String(button.state.rawValue) }
    return control.stringValue
  }
  private var current: [String: String] { controls.mapValues(value) }
  var hasDraft: Bool { !baseline.isEmpty && current != baseline }
  func reset() { baseline = [:]; index = nil }
  func accept(_ keys: Dictionary<String, Any>.Keys) { for key in keys { if let control = controls[key] { baseline[key] = value(control) } } }
  func accept(_ keys: [String]) { for key in keys { if let control = controls[key] { baseline[key] = value(control) } } }
  func begin(index: Int) -> () -> Void {
    let pending = self.index == index ? current.filter { baseline[$0.key] != nil && baseline[$0.key] != $0.value } : [:]
    self.index = index
    return { [self] in
      baseline = current
      for (key, value) in pending {
        guard let control = controls[key] else { continue }
        if let button = control as? NSPopUpButton { button.selectItem(at: Int(value) ?? 0) }
        else if let button = control as? NSButton { button.state = NSControl.StateValue(rawValue: Int(value) ?? 0) }
        else { control.stringValue = value }
      }
    }
  }
}

func numberField(_ value: Int, width: CGFloat = 76, label: String) -> NSTextField {
  let f = NSTextField(string: String(value))
  f.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
  f.fixed(width: width)
  f.setAccessibilityLabel(label)
  return f
}
func labeled(_ label: String, _ control: NSView) -> NSStackView {
  stack(
    .vertical, [Theme.label(label, size: 10, color: Theme.muted, weight: .medium), control],
    spacing: 5)
}

final class SampleEditor: NSView {
  override var acceptsFirstResponder: Bool { true }
  override func mouseDown(with event: NSEvent) { window?.makeFirstResponder(self); super.mouseDown(with:event) }

  lazy var settingsDraft = AssetFieldDraft(["name":name,"rate":rate,"volume":volume,"pan":pan])
  var hasDraft: Bool { settingsDraft.hasDraft || (loopDraftBaseline != nil && loopDraftBaseline?.isEqual(rawLoopDraft) != true) }
  func resetDocumentContext() { settingsDraft.reset(); sampleGeneration += 1; loopDraftBaseline=nil; loopDraftRevision=nil; loopPreviewSignature=nil; loopPreviewRevision=nil; savedLoopInfo=[:]; retireWaveform(); waveform.selection=nil; waveform.setViewport(nil,notify:false) }
  let waveform = WaveformView(frame: .zero),
    heading = Theme.label("Sample", size: 16, weight: .semibold),
    details = Theme.label("", size: 11, color: Theme.muted),
    rangeLabel = Theme.label("Drag to select a region", size: 11, color: Theme.muted, mono: true)
  let name = NSTextField(string: "")
  let picker = NSPopUpButton(), rate = numberField(44100, label: "Sample rate"),
    volume = numberField(64, label: "Sample volume"), pan = numberField(128, label: "Sample pan"),
    loopStart = numberField(0, width: 100, label: "Loop start frame"),
    loopEnd = numberField(0, width: 100, label: "Loop end frame")
  let looping = NSButton(checkboxWithTitle: "Normal", target: nil, action: nil),
    pingpong = NSButton(checkboxWithTitle: "Ping-pong", target: nil, action: nil)
  let sustainStart = numberField(0, width: 100, label: "Sustain loop start frame"),
    sustainEnd = numberField(0, width: 100, label: "Sustain loop end frame, exclusive")
  let sustaining = NSButton(checkboxWithTitle: "Sustain", target: nil, action: nil),
    sustainPingpong = NSButton(checkboxWithTitle: "Ping-pong", target: nil, action: nil)
  let loopReverse = NSButton(checkboxWithTitle: "Reverse", target: nil, action: nil),
    sustainReverse = NSButton(checkboxWithTitle: "Reverse", target: nil, action: nil)
  let loopsStatus = Theme.label("Sustain loops repeat while a note is held; normal loops follow release.", size: 11, color: Theme.muted)
  var loopsBusy = false
  var loopDraftBaseline: NSDictionary?, loopDraftRevision: String?, loopPreviewSignature: NSDictionary?, loopPreviewRevision: String?
  var savedLoopInfo: [AnyHashable: Any] = [:]
  lazy var loopsPreviewButton = ActionButton("Preview loops") { [weak self] in self?.setLoops(dryRun: true) }
  lazy var loopsApplyButton = ActionButton("Apply loops") { [weak self] in self?.setLoops(dryRun: false) }
  let selectionStart = numberField(0, width: 108, label: "Selection start frame"),
    selectionEnd = numberField(0, width: 108, label: "Selection end frame, exclusive")
  let channelPicker = NSPopUpButton(), operationPicker = NSPopUpButton(), curvePicker = NSPopUpButton()
  let exponent = numberField(3, label: "Fade exponent"), gain = numberField(0, label: "Gain in decibels"),
    target = numberField(0, label: "Normalize target in decibels"), smoothing = numberField(5, label: "Smoothing window in frames")
  lazy var curveGroup = labeled("FADE CURVE", curvePicker)
  lazy var exponentGroup = labeled("EXPONENT", exponent)
  lazy var gainGroup = labeled("GAIN (dB)", gain)
  lazy var targetGroup = labeled("PEAK TARGET (dBFS)", target)
  lazy var smoothGroup = labeled("WINDOW (frames)", smoothing)
  let processingStatus = Theme.label("Preview shows the exact effect of an edit before applying it.", size: 11, color: Theme.muted)
  lazy var previewButton = ActionButton("Preview edit") { [weak self] in self?.process(dryRun: true) }
  lazy var processButton = ActionButton("Apply edit") { [weak self] in self?.process(dryRun: false) }
  let operations = [("Reverse", "reverse"), ("Normalize", "normalize"), ("Fade in", "fade-in"), ("Fade out", "fade-out"),
    ("Gain", "gain"), ("Remove DC offset", "remove-dc"), ("Smooth", "smooth"), ("Invert phase", "invert"),
    ("Silence", "silence"), ("Trim to selection", "trim"), ("Swap channels", "swap-channels"),
    ("Copy left to right", "copy-left"), ("Copy right to left", "copy-right"), ("Average stereo channels", "stereo-average")]
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  private var processing = false
  var waveformRequest = 0, waveformInFlight = false, waveformPending = false, waveformAttempts = 0
  var waveformRetry: DispatchWorkItem?
  var waveformRevision: String?, strokeRevision: String?
  var sampleRevision: String?
  var snapBusy = false, snapPending = false, selectionVersion = 0
  let snapMode = NSPopUpButton()
  let snapRadius = numberField(2048, width: 90, label: "Zero-crossing search radius in frames")
  let snapStep = numberField(64, width: 90, label: "Sample grid step in frames")
  let snapOrigin = numberField(0, width: 90, label: "Sample grid origin frame")
  let snapAutomatically = NSButton(checkboxWithTitle: "Snap after selecting", target: nil, action: nil)
  let snapStatus = Theme.label("Snap adjusts boundaries; it does not change audio.", size: 11, color: Theme.muted)
  lazy var snapRadiusGroup = labeled("SEARCH (frames)", snapRadius)
  lazy var snapStepGroup = labeled("STEP (frames)", snapStep)
  lazy var snapOriginGroup = labeled("ORIGIN (frame)", snapOrigin)
  var drawingBusy = false
  var canBeginDrawing: (() -> Bool)?
  let viewportStatus = Theme.label("Whole sample", size: 11, color: Theme.muted, mono: true)
  let drawToggle = NSButton(checkboxWithTitle: "Draw", target: nil, action: nil)
  let drawingStatus = Theme.label("Zoom to individual frames to draw.", size: 11, color: Theme.muted)
  let crossfadeLoop = NSPopUpButton(), crossfadeMode = NSPopUpButton(), crossfadeCurve = NSPopUpButton()
  let crossfadeFrames = numberField(64, width: 90, label: "Loop crossfade length in frames")
  let crossfadeInfo = Theme.label("Uses saved loop settings; apply loop changes first.", size: 11, color: Theme.muted)
  let crossfadeStatus = Theme.label("Preview shows audio changes and the resulting loop period.", size: 11, color: Theme.muted)
  var crossfadeBusy = false
  var crossfadePreviewSignature: NSDictionary?, crossfadePreviewRevision: String?
  lazy var crossfadePreviewButton = ActionButton("Preview crossfade") { [weak self] in self?.crossfade(dryRun:true) }
  lazy var crossfadeApplyButton = ActionButton("Apply crossfade") { [weak self] in self?.crossfade(dryRun:false) }
  private(set) var sampleGeneration = 0
  let clipboardStatus = Theme.label("Sample clipboard is empty", size: 11, color: Theme.muted)
  let pasteMode = NSPopUpButton(), pasteRate = NSPopUpButton()
  let pasteSourceGain = numberField(0, label: "Clipboard gain in decibels"),
    pasteDestinationGain = numberField(0, label: "Existing audio gain in decibels")
  lazy var pasteDestinationGroup = labeled("EXISTING (dB)", pasteDestinationGain)
  var clipboardBusy = false
  var pastePreviewSignature: NSDictionary?, pastePreviewRevision: String?, pastePreviewID: String?
  private var previewSignature: NSDictionary?, previewRevision: String?
  var index = 1 {
    didSet { if index != oldValue { sampleGeneration += 1; loopDraftBaseline=nil;loopDraftRevision=nil;loopPreviewSignature=nil;loopPreviewRevision=nil;savedLoopInfo=[:]; retireWaveform(); waveform.setViewport(nil, notify: false); waveform.selection = nil; previewSignature = nil; previewRevision = nil; resetPastePreview();crossfadePreviewSignature=nil;crossfadePreviewRevision=nil } }
  }
  var onSelect: ((Int) -> Void)?, onImport: (() -> Void)?, onReplace: (() -> Void)?,
    onPreview: ((Int) -> Void)?,
    onSettings: (([String: Any]) -> Void)?,
    onInstrument: (() -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true
    layer?.backgroundColor = Theme.bg.cgColor
    picker.target = self
    picker.action = #selector(selectSample)
    picker.fixed(width: 180)
    let top = stack(
      .horizontal,
      [
        heading, NSView(), picker,
        ActionButton("Browse…", symbol: "square.and.arrow.down") { [weak self] in self?.onImport?()
        }, ActionButton("Replace…") { [weak self] in self?.onReplace?() },
        ActionButton("Audition", symbol: "play.fill") { [weak self] in self?.onPreview?(61) },
      ], spacing: 8)
    heading.lineBreakMode = .byTruncatingTail
    heading.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    name.setAccessibilityLabel("Sample name")
    waveform.fixed(height: 180)
    for field in [selectionStart, selectionEnd] { field.target = self; field.action = #selector(setRange) }
    channelPicker.addItems(withTitles: ["Both", "Left", "Right"])
    channelPicker.setAccessibilityLabel("Sample channels"); channelPicker.autoenablesItems = false
    channelPicker.target = self; channelPicker.action = #selector(channelChanged); channelPicker.fixed(width: 100)
    let selectionRow = stack(.horizontal, [labeled("START FRAME", selectionStart), labeled("END FRAME", selectionEnd),
      labeled("CHANNELS", channelPicker), NSView(), ActionButton("Select all") { [weak self] in
        guard let self else { return }; self.waveform.selection = 0...self.waveform.frames
      }], spacing: 14)
    operationPicker.addItems(withTitles: operations.map { $0.0 }); operationPicker.fixed(width: 216)
    operationPicker.setAccessibilityLabel("Sample operation")
    operationPicker.target = self; operationPicker.action = #selector(updateProcessingOptions)
    operationPicker.autoenablesItems = false
    curvePicker.addItems(withTitles: ["Linear", "Smooth", "Exponential", "Logarithmic"])
    curvePicker.setAccessibilityLabel("Fade curve"); curvePicker.fixed(width: 148)
    curvePicker.target = self; curvePicker.action = #selector(updateProcessingOptions)
    let editButtons = stack(.horizontal, [operationPicker, NSView(), previewButton, processButton], spacing: 10)
    let options = stack(.horizontal, [curveGroup, exponentGroup, gainGroup, targetGroup, smoothGroup, NSView()], spacing: 16)
    options.setContentHuggingPriority(.required, for: .vertical)
    updateProcessingOptions()
    processingStatus.lineBreakMode = .byWordWrapping; processingStatus.maximumNumberOfLines = 2
    let properties = stack(
      .horizontal,
      [
        labeled("C-5 RATE (Hz)", rate), labeled("VOLUME", volume), labeled("PAN", pan), NSView(),
        ActionButton("Create instrument") { [weak self] in self?.onInstrument?() },
        ActionButton("Apply settings") { [weak self] in self?.apply() },
      ], spacing: 10)
    let content = stack(
      .vertical,
      [
        top, details, waveform, makeWaveformControls(), viewportStatus, selectionRow, rangeLabel,
        ToolSection("Loops", id: "sample.loops", views: [makeLoopRow(sustain: false), makeLoopRow(sustain: true), makeLoopActions(), loopsStatus], expanded: true),
        ToolSection("Edit audio", id: "sample.processing", views: [editButtons, options, processingStatus, drawingStatus]),
        ToolSection("Selection snapping", id: "sample.snap", views: [makeSnapControls(), makeSnapActions(), snapStatus]),
        ToolSection("Clipboard & mixing", id: "sample.clipboard", views: [makeClipboardActions(), makePasteControls(), clipboardStatus]),
        ToolSection("Loop crossfade", id: "sample.crossfade", views: [makeCrossfadeControls(), crossfadeInfo, makeCrossfadeActions(), crossfadeStatus]),
        ToolSection("Sample settings", id: "sample.settings", views: [labeled("SAMPLE NAME", name), properties]), NSView(),
      ],
      spacing: 8)
    content.alignment = .leading
    content.fill(self, inset: 12)
    for row in content.arrangedSubviews {
      row.widthAnchor.constraint(equalTo: content.widthAnchor).isActive = true
    }
    waveform.onSelection = { [weak self] range in self?.selectionChanged(range) }
    waveform.onClipboard = { [weak self] action in self?.clipboardAction(action) }
    waveform.onViewport = { [weak self] in self?.viewportChanged() }
    waveform.onFinishSelection = { [weak self] in self?.snapAfterSelection() }
    waveform.onBeginStroke = { [weak self] in self?.beginDrawing() ?? false }
    waveform.onCancelStroke = { [weak self] in
      guard let self else { return }
      if self.strokeRevision != nil { self.drawingStatus.stringValue = "Stroke cancelled." }
      self.strokeRevision = nil
    }
    waveform.onStroke = { [weak self] points in self?.commitDrawing(points) }
    selectionChanged(nil)
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func selectSample() {
    index = picker.selectedTag()
    onSelect?(index)
  }
  var selectedChannels: String { ["both", "left", "right"][max(0, channelPicker.indexOfSelectedItem)] }
  var selectedOperation: String { operations[max(0, operationPicker.indexOfSelectedItem)].1 }
  func automationContext(frames: Int) -> [String: Any] {
    let range = waveform.selection.flatMap { $0.upperBound <= frames ? $0 : nil }
    return ["sample": index, "start": range?.lowerBound ?? 0,
      "end": range?.upperBound ?? frames, "channels": selectedChannels]
  }
  func selectionChanged(_ range: ClosedRange<Int>?) {
    selectionVersion += 1
    let a = range?.lowerBound ?? 0, b = range?.upperBound ?? waveform.frames
    selectionStart.stringValue = String(a); selectionEnd.stringValue = String(b)
    rangeLabel.stringValue = "\(a) → \(b)   ·   \(b-a) frames   ·   end is exclusive"
  }
  @objc func setRange() {
    guard let a = Int(selectionStart.stringValue), let b = Int(selectionEnd.stringValue),
      a >= 0, a <= b, b <= waveform.frames else {
      processingStatus.stringValue = "Enter a frame range or insertion cursor inside the sample."; return
    }
    waveform.selection = a...b
    snapAfterSelection()
  }
  @objc func updateProcessingOptions() {
    let op = selectedOperation, fades = ["fade-in", "fade-out"].contains(op)
    curveGroup.isHidden = !fades
    exponentGroup.isHidden = !fades || curvePicker.indexOfSelectedItem < 2
    gainGroup.isHidden = op != "gain"
    targetGroup.isHidden = op != "normalize"
    smoothGroup.isHidden = op != "smooth"
  }
  @objc func channelChanged() {
    waveform.cancelStroke()
    refreshWaveform()
  }
  func processingParams() -> [String: Any]? {
    guard let a = Int(selectionStart.stringValue), let b = Int(selectionEnd.stringValue),
      a >= 0, a < b, b <= waveform.frames else {
      processingStatus.stringValue = "Enter a nonempty frame range inside the sample."; return nil
    }
    var p: [String: Any] = ["sample": index, "operation": selectedOperation, "start": a, "end": b, "channels": selectedChannels]
    if ["trim", "swap-channels", "copy-left", "copy-right", "stereo-average"].contains(selectedOperation), selectedChannels != "both" {
      processingStatus.stringValue = "This operation needs Both channels."; return nil
    }
    func number(_ field: NSTextField, _ minimum: Double, _ maximum: Double) -> Double? {
      guard let value = Double(field.stringValue), value.isFinite, value >= minimum, value <= maximum else { return nil }; return value
    }
    if ["fade-in", "fade-out"].contains(selectedOperation) {
      p["curve"] = ["linear", "smooth", "exponential", "logarithmic"][max(0, curvePicker.indexOfSelectedItem)]
      if curvePicker.indexOfSelectedItem >= 2 {
        guard let value = number(exponent, 0.1, 8) else { processingStatus.stringValue = "Fade exponent must be between 0.1 and 8."; return nil }; p["exponent"] = value
      }
    }
    if selectedOperation == "gain" {
      guard let value = number(gain, -96, 24) else { processingStatus.stringValue = "Gain must be between −96 and +24 dB."; return nil }; p["gainDB"] = value
    }
    if selectedOperation == "normalize" {
      guard let value = number(target, -96, 0) else { processingStatus.stringValue = "Peak target must be between −96 and 0 dB."; return nil }; p["targetDB"] = value
    }
    if selectedOperation == "smooth" {
      guard let value = Int(smoothing.stringValue), (3...255).contains(value), value % 2 == 1 else { processingStatus.stringValue = "Smoothing needs an odd window from 3 to 255 frames."; return nil }; p["window"] = value
    }
    waveform.selection = a...b
    return p
  }
  func process(dryRun: Bool) {
    guard !processing, let onRequest, var params = processingParams() else { return }
    let signature = params as NSDictionary
    if !dryRun, previewSignature?.isEqual(signature) == true, let previewRevision { params["expectedRevision"] = previewRevision }
    params["dryRun"] = dryRun
    processing = true; previewButton.isEnabled = false; processButton.isEnabled = false
    processingStatus.stringValue = dryRun ? "Calculating preview…" : "Processing sample…"
    let sample = index, generation = sampleGeneration
    onRequest("sample.process", params) { [weak self] response in
      guard let self else { return }
      self.processing = false; self.previewButton.isEnabled = true; self.processButton.isEnabled = true
      guard sample == self.index, generation == self.sampleGeneration else { return }
      guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
        self.processingStatus.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "Processing failed."
        self.previewSignature = nil; self.previewRevision = nil; return
      }
      let prefix = dryRun ? "Preview" : "Applied"
      if let removed = data["removedFrames"] as? Int {
        self.processingStatus.stringValue = "\(prefix): remove \(removed) frames; keep \(data["resultFrames"] ?? 0)."
      } else {
        let frames = data["changedFrames"] as? Int ?? 0, clipped = data["clippedSamples"] as? Int ?? 0
        let peak = data["peakAfter"] as? Double ?? 0
        let peakText = peak > 0 ? String(format: "%.2f dBFS", 20 * log10(peak)) : "silence"
        self.processingStatus.stringValue = "\(prefix): \(frames) frames change · peak \(peakText)" + (clipped > 0 ? " · \(clipped) clipped values" : "")
      }
      self.previewSignature = dryRun ? signature : nil
      self.previewRevision = dryRun ? result["revision"] as? String : nil
    }
  }
  func update(_ info: [AnyHashable: Any], samples: [[String: Any]], revision: String? = nil) {
    let restoreDraft = settingsDraft.begin(index: index); defer { restoreDraft() }
    picker.removeAllItems()
    for s in samples {
      let i = s["index"] as? Int ?? 1
      picker.addItem(withTitle: String(format: "%02d  ", i) + (s["name"] as? String ?? "Sample"))
      picker.lastItem?.tag = i
    }
    picker.selectItem(withTag: index)
    heading.stringValue = "Sample"
    name.stringValue = info["name"] as? String ?? ""
    let frames = info["frames"] as? Int ?? 0
    let hz = info["rate"] as? Int ?? 44100
    details.stringValue =
      "\(frames) frames   ·   \(info["bits"] ?? 16)-bit   ·   \(info["channels"] ?? 1) channel(s)   ·   \(String(format:"%.2f",Double(frames)/Double(max(1,hz)))) seconds"
    retireWaveform()
    sampleRevision = revision
    waveform.frames = frames
    let stereo = (info["channels"] as? Int ?? 1) == 2
    channelPicker.item(at: 2)?.isEnabled = stereo
    if !stereo && channelPicker.indexOfSelectedItem == 2 { channelPicker.selectItem(at: 0) }
    for i in 10..<operations.count { operationPicker.item(at: i)?.isEnabled = stereo }
    if !stereo && operationPicker.indexOfSelectedItem >= 10 { operationPicker.selectItem(at: 0) }
    updateProcessingOptions()
    waveform.loopStart = (info["loop"] as? Bool ?? false) ? info["loopStart"] as? Int ?? 0 : 0
    waveform.loopEnd = (info["loop"] as? Bool ?? false) ? info["loopEnd"] as? Int ?? 0 : 0
    if waveform.viewport == nil, selectedChannels == "both", let data = info["waveform"] as? Data {
      waveform.peaksRange = nil
      waveform.peaks = data.withUnsafeBytes { Array($0.bindMemory(to: Float.self)) }
      waveformRevision = revision
    } else {
      waveform.peaks = []
    }
    rate.integerValue = hz
    volume.integerValue = info["volume"] as? Int ?? 64
    pan.integerValue = info["pan"] as? Int ?? 128
    updateLoopSettings(info, revision: revision)
    updateCrossfadeInfo(info)
    if let selection = waveform.selection, selection.upperBound > frames {
      waveform.selection = nil
    }
    selectionChanged(waveform.selection)
    if waveform.viewport != nil || selectedChannels != "both" || waveform.drawing { refreshWaveform() }
    updateViewportStatus()
    waveform.needsDisplay = true
  }
  func apply() {
    onSettings?([
      "name": name.stringValue, "rate": rate.integerValue, "volume": volume.integerValue,
      "pan": pan.integerValue,
    ])
  }
}

final class EnvelopeView: NSView {
  var playbackTicks: [Double] = [] { didSet { if playbackTicks != oldValue { needsDisplay=true } } }
  var points: [[Int]] = [] {
    didSet {
      if let selectedNode, selectedNode >= points.count { self.selectedNode = points.indices.last }
      needsDisplay = true
    }
  }
  var onChange: (([[Int]]) -> Void)?
  var onRemove: ((Int, [[Int]]) -> Void)?
  var onSelect: ((Int?) -> Void)?
  var canEdit: () -> Bool = { true }
  var selectedNode: Int? {
    didSet {
      needsDisplay = true
      onSelect?(selectedNode)
    }
  }
  private var dragged: Int?
  var maximumPoints = 25
  override var acceptsFirstResponder: Bool { true }
  override var isFlipped: Bool { true }
  var maxTick: Int { max(64, (points.last?.first ?? 64) + 8) }
  func location(_ point: [Int]) -> NSPoint {
    NSPoint(
      x: 16 + CGFloat(point[0]) / CGFloat(maxTick) * (bounds.width - 32),
      y: 16 + (1 - CGFloat(point[1]) / 64) * (bounds.height - 32))
  }
  override func draw(_ rect: NSRect) {
    Theme.bg.setFill()
    bounds.fill()
    Theme.border.setStroke()
    for value in stride(from: 0, through: 64, by: 16) {
      let y = location([0, value]).y
      let p = NSBezierPath()
      p.move(to: .init(x: 16, y: y))
      p.line(to: .init(x: bounds.width - 16, y: y))
      p.stroke()
    }
    let path = NSBezierPath()
    for (i, p) in points.enumerated() {
      let pos = location(p)
      if i == 0 { path.move(to: pos) } else { path.line(to: pos) }
    }
    Theme.accent.setStroke()
    path.lineWidth = 2
    path.stroke()
    Theme.text.withAlphaComponent(0.85).setFill()
    for tick in playbackTicks where tick>=0 && tick<=Double(maxTick) {
      let x=16+CGFloat(tick)/CGFloat(maxTick)*(bounds.width-32)
      NSRect(x:x,y:16,width:1.5,height:max(0,bounds.height-32)).fill()
      NSBezierPath(ovalIn:NSRect(x:x-3,y:12,width:6,height:6)).fill()
    }
    Theme.gold.setFill()
    for (index, p) in points.enumerated() {
      let pos = location(p)
      NSBezierPath(ovalIn: NSRect(x: pos.x - 4, y: pos.y - 4, width: 8, height: 8)).fill()
      if selectedNode == index {
        Theme.text.setStroke()
        NSBezierPath(ovalIn: NSRect(x: pos.x - 7, y: pos.y - 7, width: 14, height: 14)).stroke()
      }
    }
  }
  override func mouseDown(with event: NSEvent) {
    window?.makeFirstResponder(self)
    let p = convert(event.locationInWindow, from: nil)
    dragged = points.indices.min(by: {
      hypot(location(points[$0]).x - p.x, location(points[$0]).y - p.y)
        < hypot(location(points[$1]).x - p.x, location(points[$1]).y - p.y)
    })
    if let i = dragged, hypot(location(points[i]).x - p.x, location(points[i]).y - p.y) > 14 {
      dragged = nil
    }
    selectedNode = dragged
    if event.clickCount == 2 && dragged == nil && points.count < maximumPoints && canEdit() {
      let tick =
        points.isEmpty
        ? 0 : min(65535, max(0, Int((p.x - 16) / max(1, bounds.width - 32) * CGFloat(maxTick))))
      let value = min(64, max(0, Int((1 - (p.y - 16) / max(1, bounds.height - 32)) * 64)))
      if let existing = points.firstIndex(where: { $0[0] == tick }) {
        selectedNode = existing
        return
      }
      points.append([tick, value])
      points.sort { $0[0] < $1[0] }
      selectedNode = points.firstIndex { $0[0] == tick }
      onChange?(points)
    }
  }
  override func mouseDragged(with event: NSEvent) {
    guard let i = dragged, canEdit() else { return }
    let p = convert(event.locationInWindow, from: nil)
    let tick = max(0, Int((p.x - 16) / max(1, bounds.width - 32) * CGFloat(maxTick)))
    points[i] = [
      boundedTick(tick, at: i),
      min(64, max(0, Int((1 - (p.y - 16) / max(1, bounds.height - 32)) * 64))),
    ]
  }
  override func mouseUp(with event: NSEvent) {
    if dragged != nil && canEdit() { onChange?(points) }
    dragged = nil
  }
  private func boundedTick(_ tick: Int, at index: Int) -> Int {
    if index == 0 { return 0 }
    let lower = points[index - 1][0] + 1
    let upper = index + 1 < points.count ? points[index + 1][0] - 1 : 65535
    // Imported envelopes can contain coincident nodes. Keep their timing when
    // no strict interval exists, while still allowing value edits and deletion.
    return lower <= upper ? max(lower, min(upper, tick)) : points[index][0]
  }
  func updateNode(tick: Int, value: Int) {
    guard let index = selectedNode, points.indices.contains(index), canEdit() else { return }
    points[index] = [boundedTick(tick, at: index), max(0, min(64, value))]
    onSelect?(selectedNode)
    onChange?(points)
  }
  func removeSelectedNode() {
    guard let index = selectedNode, points.indices.contains(index), canEdit() else { return }
    points.remove(at: index)
    if !points.isEmpty { points[0][0] = 0 }
    selectedNode = points.isEmpty ? nil : min(index, points.count - 1)
    if let onRemove { onRemove(index, points) } else { onChange?(points) }
  }
  override func keyDown(with event: NSEvent) {
    guard let index = selectedNode, points.indices.contains(index) else {
      super.keyDown(with: event)
      return
    }
    if event.keyCode == 51 || event.keyCode == 117 {
      removeSelectedNode()
      return
    }
    if [123, 124, 125, 126].contains(Int(event.keyCode)) {
      let amount = event.modifierFlags.contains(.shift) ? 4 : 1
      updateNode(
        tick: points[index][0]
          + (event.keyCode == 123 ? -amount : event.keyCode == 124 ? amount : 0),
        value: points[index][1]
          + (event.keyCode == 125 ? -amount : event.keyCode == 126 ? amount : 0))
      return
    }
    super.keyDown(with: event)
  }
  override func accessibilityValue() -> Any? {
    guard let selectedNode, points.indices.contains(selectedNode) else {
      return "\(points.count) nodes"
    }
    return "Node \(selectedNode), tick \(points[selectedNode][0]), value \(points[selectedNode][1])"
  }
  override init(frame: NSRect) {
    super.init(frame: frame)
    setAccessibilityElement(true)
    setAccessibilityRole(.image)
    setAccessibilityLabel("Volume envelope")
    setAccessibilityHelp(
      "Select a node, then use arrow keys to adjust it or Delete to remove it. Node fields also allow numeric editing."
    )
  }
  required init?(coder: NSCoder) { fatalError() }
}

final class InstrumentEditor: NSView {
  override var acceptsFirstResponder: Bool { true }
  override func mouseDown(with event: NSEvent) { window?.makeFirstResponder(self); super.mouseDown(with:event) }

  lazy var settingsDraft = AssetFieldDraft(["name":name,"volume":volume,"pan":pan,"fadeout":fade,"nna":nna,"dct":dct,"dna":dna,"enabled":enabled,"sustain":sustain,"sustainPoint":sustainPoint,"sustainEnd":sustainEnd,"loop":looping,"loopStart":loopStart,"loopEnd":loopEnd,"filter":filter])
  var hasDraft: Bool { settingsDraft.hasDraft }
  let picker = NSPopUpButton(), name = NSTextField(string: ""),
    volume = numberField(64, label: "Instrument volume"),
    pan = numberField(128, label: "Instrument pan"), fade = numberField(256, label: "Fade out"),
    sustainPoint = numberField(0, width: 50, label: "Sustain start node"),
    sustainEnd = numberField(0, width: 50, label: "Sustain end node"),
    loopStart = numberField(0, width: 50, label: "Envelope loop start node"),
    loopEnd = numberField(0, width: 50, label: "Envelope loop end node")
  let nna = NSPopUpButton(), dct = NSPopUpButton(), dna = NSPopUpButton(),
    enabled = NSButton(checkboxWithTitle: "Enable envelope", target: nil, action: nil),
    sustain = NSButton(checkboxWithTitle: "Sustain", target: nil, action: nil)
  let envelopeType = NSPopUpButton(),
    looping = NSButton(checkboxWithTitle: "Loop", target: nil, action: nil),
    filter = NSButton(checkboxWithTitle: "Filter envelope", target: nil, action: nil)
  let nodePicker = NSPopUpButton(), nodeTick = numberField(0, label: "Selected envelope node tick"),
    nodeValue = numberField(64, label: "Selected envelope node value")
  private var envelopeData = [[String: Any]]()
  private var displayedEnvelope: Int?
  let envelope = EnvelopeView(frame: .zero), mapping = NSTextField(wrappingLabelWithString: ""),
    mapFrom = numberField(0, label: "Keymap first note"),
    mapTo = numberField(119, label: "Keymap last note"), mapSample = NSPopUpButton()
  var index = 1, keymap = [Int](repeating: 0, count: 128)
  var onEnvelopeTools: ((Int) -> Void)?
  var onEnvelopeBank: ((Int) -> Void)?
  var onPluginAssignment: (() -> Void)?
  var onNewPluginInstrument:(()->Void)?
  let pluginSummary = Theme.label("Sample instrument", size: 12, color: Theme.muted)
  var envelopeToolsButton: ActionButton!
  var onSelect: ((Int) -> Void)?, onApply: (([String: Any]) -> Void)?, onCreate: (() -> Void)?,
    onImport: (() -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    picker.target = self
    picker.action = #selector(selectInstrument)
    picker.fixed(width: 230)
    enabled.target = self; enabled.action = #selector(toggleEnvelopeEnabled)
    nna.addItems(withTitles: ["Cut", "Continue", "Note off", "Fade"])
    dct.addItems(withTitles: ["Off", "Note", "Sample", "Instrument", "Plugin"])
    dna.addItems(withTitles: ["Cut", "Note off", "Fade"])
    envelopeType.addItems(withTitles: [
      "Volume envelope", "Pan envelope", "Pitch / filter envelope",
    ])
    envelopeType.target = self
    envelopeType.action = #selector(selectEnvelope)
    envelopeToolsButton=ActionButton("Envelope tools…"){[weak self] in
      guard let self,self.envelope.canEdit() else{return};self.onEnvelopeTools?(self.envelopeType.indexOfSelectedItem)
    }
    nodePicker.target = self
    nodePicker.action = #selector(selectNode)
    nodePicker.fixed(width: 80)
    envelope.fixed(height: 185)
    name.fixed(width: 240)
    mapSample.fixed(width: 220)
    mapping.font = .monospacedSystemFont(ofSize: 10, weight: .regular)
    mapping.textColor = Theme.muted
    let top = stack(
      .horizontal,
      [
        Theme.label("Instruments", size: 16, weight: .semibold), NSView(), picker,

      ], spacing: 14)
    let props = stack(
      .horizontal,
      [
        labeled("NAME", name), labeled("VOLUME", volume), labeled("PAN", pan),
        labeled("FADE OUT", fade), NSView(),
      ], spacing: 18)
    let actions = stack(
      .horizontal,
      [
        labeled("NEW NOTE ACTION", nna), labeled("DUPLICATE CHECK", dct),
        labeled("DUPLICATE ACTION", dna), NSView(),
      ], spacing: 18)
    let env = stack(
      .horizontal,
      [
        enabled, sustain, labeled("START NODE", sustainPoint), labeled("END NODE", sustainEnd),
        NSView(),
        ActionButton("ADSR preset") { [weak self] in
          guard let self, self.envelope.canEdit() else { return }
          self.envelope.points = [[0, 0], [2, 64], [12, 48], [32, 48], [48, 0]]
          self.envelope.selectedNode = 0
          self.enabled.state = .on
          self.onApply?([
            "envelope": self.envelopeType.indexOfSelectedItem,
            "points": self.envelope.points, "enabled": true,
          ])
        },
      ], spacing: 16)
    let map = stack(
      .horizontal,
      [
        Theme.label("KEYMAP", size: 10, color: Theme.muted), labeled("FROM NOTE", mapFrom),
        labeled("TO NOTE", mapTo), labeled("SAMPLE", mapSample),
        ActionButton("Assign range") { [weak self] in
          guard let self else { return }
          let first = max(0, min(127, self.mapFrom.integerValue))
          let last = max(0, min(127, self.mapTo.integerValue))
          if first <= last {
            for i in first...last { self.keymap[i] = self.mapSample.selectedTag() }
            self.onApply?(["mapping": self.keymap])
          }
        },
      ], spacing: 16)
    let content = stack(
      .vertical,
      [
        top,
        stack(.horizontal,[ActionButton("New sample instrument",prominent:true){[weak self] in self?.onCreate?()},
          ActionButton("New plugin instrument…",prominent:true){[weak self] in self?.onNewPluginInstrument?()},
          ActionButton("Import…"){[weak self] in self?.onImport?()},NSView()],spacing:8),
        stack(.horizontal, [ActionButton("Play instrument with keys") { [weak self] in guard let self else{return};self.window?.makeFirstResponder(self) }, pluginSummary, NSView(), ActionButton("Assign instrument plugin…") { [weak self] in self?.onPluginAssignment?() }], spacing: 8),
        Theme.label("Z–M / Q–U preview this instrument with its keymap and enabled envelopes. Sample inspector previews raw samples.", size: 11, color: Theme.muted),
        stack(.horizontal, [envelopeType, envelopeToolsButton!, ActionButton("Envelope bank…"){[weak self] in guard let self,self.envelope.canEdit() else{return};self.onEnvelopeBank?(self.envelopeType.indexOfSelectedItem)}, NSView(), filter], spacing: 8),
        envelope,
        stack(
          .horizontal,
          [
            labeled("NODE", nodePicker), labeled("TICK", nodeTick), labeled("VALUE", nodeValue),
            ActionButton("Apply node") { [weak self] in
              guard let self else { return }
              self.envelope.updateNode(
                tick: self.nodeTick.integerValue, value: self.nodeValue.integerValue)
            }, ActionButton("Delete node") { [weak self] in self?.envelope.removeSelectedNode() },
            NSView(),
          ], spacing: 14),
        env,
        stack(
          .horizontal,
          [
            looping, labeled("LOOP START NODE", loopStart), labeled("LOOP END NODE", loopEnd),
            NSView(), ActionButton("Apply instrument") { [weak self] in self?.apply() },
          ], spacing: 14),
        Theme.label(
          "Drag nodes or use the node fields. Double-click to add; Delete removes the selected node. Note numbers: 0–119.",
          size: 11, color: Theme.muted),
        ToolSection("Instrument settings & note behaviour", id: "instrument.settings", views: [props, actions]),
        ToolSection("Sample keymap", id: "instrument.keymap", views: [map, mapping]), NSView(),
      ], spacing: 8)
    content.alignment = .leading
    content.fill(self, inset: 12)
    for row in content.arrangedSubviews {
      row.widthAnchor.constraint(equalTo: content.widthAnchor).isActive = true
    }
    envelope.onChange = { [weak self] points in
      guard let self else { return }
      self.onApply?(["envelope": self.envelopeType.indexOfSelectedItem, "points": points])
    }
    envelope.onSelect = { [weak self] _ in self?.updateNodeFields() }
    envelope.onRemove = { [weak self] removed, points in
      guard let self else { return }
      func shifted(_ index: Int) -> Int {
        min(max(0, points.count - 1), index > removed ? index - 1 : index)
      }
      self.onApply?([
        "envelope": self.envelopeType.indexOfSelectedItem, "points": points,
        "enabled": !points.isEmpty && self.enabled.state == .on,
        "sustainPoint": shifted(self.sustainPoint.integerValue),
        "sustainEnd": shifted(self.sustainEnd.integerValue),
        "loopStart": shifted(self.loopStart.integerValue),
        "loopEnd": shifted(self.loopEnd.integerValue),
      ])
    }
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func selectInstrument() {
    index = picker.selectedTag()
    onSelect?(index)
  }
  @objc func toggleEnvelopeEnabled() {
    guard envelope.canEdit() else { return }
    onApply?(["envelope": envelopeType.indexOfSelectedItem, "enabled": enabled.state == .on])
  }
  func update(_ info: [AnyHashable: Any], model: PatternModel) {
    let restoreDraft = settingsDraft.begin(index: index); defer { restoreDraft() }
    picker.removeAllItems()
    for i in model.instruments {
      let n = i["index"] as? Int ?? 1
      picker.addItem(withTitle: "\(n)  \(i["name"] ?? "Instrument")")
      picker.lastItem?.tag = n
    }
    picker.selectItem(withTag: index)
    let owner = model.nativePlugins.first { plugin in
      (plugin["instrument"] as? Int == index) || (plugin["instrumentAssignments"] as? [[String:Any]] ?? []).contains { $0["instrument"] as? Int == index }
    }
    pluginSummary.stringValue = owner.map { "Plugin: \($0["name"] as? String ?? "Instrument")" } ?? (model.instruments.isEmpty ? "No instrument yet · choose New" : "Sample instrument")
    mapSample.removeAllItems()
    mapSample.addItem(withTitle: "None")
    mapSample.lastItem?.tag = 0
    for s in model.samples {
      mapSample.addItem(withTitle: "\(s["index"] ?? 0) \(s["name"] ?? "Sample")")
      mapSample.lastItem?.tag = s["index"] as? Int ?? 0
    }
    name.stringValue = info["name"] as? String ?? ""
    volume.integerValue = info["volume"] as? Int ?? 64
    pan.integerValue = info["pan"] as? Int ?? 128
    fade.integerValue = info["fadeout"] as? Int ?? 256
    nna.selectItem(at: info["nna"] as? Int ?? 0)
    dct.selectItem(at: info["dct"] as? Int ?? 0)
    dna.selectItem(at: info["dna"] as? Int ?? 0)
    envelopeData = info["envelopes"] as? [[String: Any]] ?? []
    envelopeType.item(at: 2)?.isEnabled = model.format != "XM"
    if model.format == "XM" && envelopeType.indexOfSelectedItem == 2 {
      envelopeType.selectItem(at: 0)
    }
    selectEnvelope()
    keymap = info["mapping"] as? [Int] ?? [Int](repeating: 0, count: 128)
    mapping.stringValue = stride(from: 0, to: 120, by: 12).map { start in
      "Oct \(start/12): "
        + keymap[start..<start + 12].map { String(format: "%02d", $0) }.joined(separator: " ")
    }.joined(separator: "   ")
  }
  @objc func selectEnvelope() {
    let kind = max(0, envelopeType.indexOfSelectedItem)
    let info: [String: Any] = kind < envelopeData.count ? envelopeData[kind] : [:]
    envelope.points = info["points"] as? [[Int]] ?? []
    nodePicker.removeAllItems()
    for index in envelope.points.indices { nodePicker.addItem(withTitle: String(index)) }
    if envelope.selectedNode == nil { envelope.selectedNode = envelope.points.indices.first }
    updateNodeFields()
    envelope.maximumPoints = info["maxPoints"] as? Int ?? 25
    envelope.setAccessibilityLabel(envelopeType.titleOfSelectedItem ?? "Envelope")
    enabled.state = (info["enabled"] as? Bool ?? false) ? .on : .off
    sustain.state = (info["sustain"] as? Bool ?? false) ? .on : .off
    sustainPoint.integerValue = info["sustainPoint"] as? Int ?? 0
    sustainEnd.integerValue = info["sustainEnd"] as? Int ?? 0
    looping.state = (info["loop"] as? Bool ?? false) ? .on : .off
    loopStart.integerValue = info["loopStart"] as? Int ?? 0
    loopEnd.integerValue = info["loopEnd"] as? Int ?? 0
    filter.isEnabled = kind == 2
    filter.state = (info["filter"] as? Bool ?? false) ? .on : .off
    if let displayedEnvelope, displayedEnvelope != kind { settingsDraft.accept(["enabled","sustain","sustainPoint","sustainEnd","loop","loopStart","loopEnd","filter"]) }
    displayedEnvelope = kind
  }
  @objc func selectNode() {
    envelope.selectedNode =
      nodePicker.indexOfSelectedItem >= 0 ? nodePicker.indexOfSelectedItem : nil
  }
  func updateNodeFields() {
    if nodePicker.numberOfItems != envelope.points.count {
      nodePicker.removeAllItems()
      for index in envelope.points.indices { nodePicker.addItem(withTitle: String(index)) }
    }
    guard let index = envelope.selectedNode, envelope.points.indices.contains(index) else {
      nodePicker.isEnabled = false
      nodeTick.isEnabled = false
      nodeValue.isEnabled = false
      return
    }
    nodePicker.isEnabled = true
    nodeTick.isEnabled = true
    nodeValue.isEnabled = true
    nodePicker.selectItem(at: index)
    nodeTick.integerValue = envelope.points[index][0]
    nodeValue.integerValue = envelope.points[index][1]
  }
  func apply() {
    onApply?([
      "name": name.stringValue, "volume": volume.integerValue, "pan": pan.integerValue,
      "fadeout": fade.integerValue, "nna": nna.indexOfSelectedItem, "dct": dct.indexOfSelectedItem,
      "dna": dna.indexOfSelectedItem, "enabled": enabled.state == .on,
      "envelope": envelopeType.indexOfSelectedItem, "sustain": sustain.state == .on,
      "sustainPoint": sustainPoint.integerValue, "sustainEnd": sustainEnd.integerValue,
      "loop": looping.state == .on, "loopStart": loopStart.integerValue,
      "loopEnd": loopEnd.integerValue,
      "filter": filter.state == .on, "points": envelope.points,
    ])
  }
}
