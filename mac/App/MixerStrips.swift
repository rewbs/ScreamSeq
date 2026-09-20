import AppKit

final class MixerStripMeter: NSView {
  var left = 0.0, right = 0.0
  func set(_ left: Double, _ right: Double) {
    guard self.left != left || self.right != right else { return }
    self.left = left; self.right = right; needsDisplay = true
  }
  override func draw(_ dirtyRect: NSRect) {
    Theme.bg.setFill(); bounds.fill()
    let half = bounds.width / 2
    for (index, value) in [left, right].enumerated() {
      let fraction = max(0, min(1, (20 * log10(max(0.00001, value)) + 60) / 60))
      (value >= 1 ? NSColor.systemRed : value > 0.5 ? Theme.gold : Theme.accent).setFill()
      NSRect(x: CGFloat(index) * half + 2, y: 0, width: half - 4, height: bounds.height * fraction).fill()
    }
  }
}

final class MixerStrip: NSView, NSTextFieldDelegate {
  var busID = ""
  let title = Theme.label("", size: 12, weight: .semibold)
  let kind = Theme.label("", size: 10, color: Theme.muted)
  let fader = MixerSlider(), pan = MixerSlider(), width = MixerSlider(), prePan = MixerSlider()
  let gain = NSTextField(string: "0"), preGain = NSTextField(string: "0")
  let meter = MixerStripMeter(frame: .zero), peak = Theme.label("−∞ dB", size: 11, mono: true)
  let mute = NSButton(checkboxWithTitle: "Mute", target: nil, action: nil)
  let solo = NSButton(checkboxWithTitle: "Solo", target: nil, action: nil)
  let counts = Theme.label("", size: 10, color: Theme.muted)
  var onControl: ((String, String, Any, Bool) -> Void)?
  var onFinish: ((String) -> Void)?, onInspect: ((String) -> Void)?
  var isTracking: Bool { fader.trackingGesture || pan.trackingGesture || width.trackingGesture || prePan.trackingGesture || gain.currentEditor() != nil || preGain.currentEditor() != nil }
  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true; layer?.cornerRadius = 6; layer?.borderWidth = 1
    title.maximumNumberOfLines = 2; title.fixed(height: 34)
    for slider in [fader, pan, width, prePan] {
      slider.isContinuous = true; slider.target = self; slider.action = #selector(slide(_:))
      slider.onFinish = { [weak self] in guard let self else { return }; self.onFinish?(self.busID) }
    }
    fader.minValue = -96; fader.maxValue = 24; fader.isVertical = true; fader.fixed(width: 34, height: 132)
    pan.minValue = -1; pan.maxValue = 1; width.minValue = 0; width.maxValue = 2
    prePan.minValue = -1; prePan.maxValue = 1
    meter.fixed(width: 34, height: 132)
    gain.delegate = self; preGain.delegate = self
    for button in [mute, solo] { button.target = self; button.action = #selector(toggle(_:)); button.font = .systemFont(ofSize: 11) }
    let meters = stack(.horizontal, [NSView(), fader, meter, NSView()], spacing: 8)
    let content = stack(.vertical, [title, kind, peak, meters,
      stack(.horizontal, [gain, Theme.label("dB", size: 11)]),
      stack(.horizontal, [mute, solo]),
      Theme.label("Balance", size: 10, color: Theme.muted), pan,
      Theme.label("Width", size: 10, color: Theme.muted), width,
      stack(.horizontal, [Theme.label("Pre · dB", size: 10, color: Theme.muted), preGain]),
      Theme.label("Pre balance", size: 10, color: Theme.muted), prePan, counts,
      ActionButton("Routing / effects") { [weak self] in guard let self else { return }; self.onInspect?(self.busID) }
    ], spacing: 7)
    content.stretchAcrossAxis(); content.fill(self, inset: 10)
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ bus: [String: Any], selected: Bool) {
    if isTracking && busID != bus["id"] as? String { return }
    busID = bus["id"] as? String ?? ""
    let name = bus["name"] as? String ?? "Bus"
    title.stringValue = name; kind.stringValue = (bus["kind"] as? String ?? "bus").capitalized
    layer?.backgroundColor = Theme.panel.cgColor; layer?.borderColor = (selected ? Theme.accent : Theme.border).cgColor
    fader.setAccessibilityLabel("\(name) fader in dB"); gain.setAccessibilityLabel("\(name) fader value in dB")
    preGain.setAccessibilityLabel("\(name) pre gain in dB")
    pan.setAccessibilityLabel("\(name) balance"); width.setAccessibilityLabel("\(name) stereo width")
    prePan.setAccessibilityLabel("\(name) balance before effects")
    mute.setAccessibilityLabel("Mute \(name)"); solo.setAccessibilityLabel("Solo \(name)")
    if !fader.trackingGesture { fader.doubleValue = (bus["gainDB"] as? NSNumber)?.doubleValue ?? 0 }
    if gain.currentEditor() == nil && !fader.trackingGesture { gain.stringValue = String(format: "%.1f", fader.doubleValue) }
    if !pan.trackingGesture { pan.doubleValue = (bus["pan"] as? NSNumber)?.doubleValue ?? 0 }
    if !width.trackingGesture { width.doubleValue = (bus["width"] as? NSNumber)?.doubleValue ?? 1 }
    if !prePan.trackingGesture { prePan.doubleValue = (bus["prePan"] as? NSNumber)?.doubleValue ?? 0 }
    if preGain.currentEditor() == nil { preGain.stringValue = String(format: "%.1f", (bus["preGainDB"] as? NSNumber)?.doubleValue ?? 0) }
    mute.state = bus["mute"] as? Bool == true ? .on : .off; solo.state = bus["solo"] as? Bool == true ? .on : .off
    counts.stringValue = "\((bus["inserts"] as? [String] ?? []).count) effects · \((bus["sends"] as? [[String: Any]] ?? []).count) sends"
  }
  func showMeter(_ levels: (Double, Double)) {
    meter.set(levels.0, levels.1)
    let value = max(levels.0, levels.1)
    let text = value > 0.00001 ? String(format: "%.1f dB", 20 * log10(value)) : "−∞ dB"
    if peak.stringValue != text { peak.stringValue = text }
    peak.textColor = value >= 1 ? .systemRed : Theme.muted
  }
  @objc func slide(_ sender: MixerSlider) {
    let key = sender === fader ? "gainDB" : sender === pan ? "pan" : sender === prePan ? "prePan" : "width"
    if sender === fader { gain.stringValue = String(format: "%.1f", sender.doubleValue) }
    onControl?(busID, key, sender.doubleValue, !sender.trackingGesture)
  }
  private func enter(_ field: NSTextField, key: String) {
    guard let value = Double(field.stringValue), value.isFinite, (-96...24).contains(value) else { return }
    if key == "gainDB" { fader.doubleValue = value }
    onControl?(busID, key, value, true)
  }
  func controlTextDidEndEditing(_ notification: Notification) {
    if let field = notification.object as? NSTextField { enter(field, key: field === gain ? "gainDB" : "preGainDB") }
    onFinish?(busID)
  }
  @objc func toggle(_ sender: NSButton) { onControl?(busID, sender === mute ? "mute" : "solo", sender.state == .on, true) }
}

private final class MixerStripDocument: NSView { override var isFlipped: Bool { true } }

// Native controls are allocated only for the visible horizontal range plus two
// neighboring strips. Scrolling recycles them; metering never rebuilds controls.
final class MixerStrips: NSView {
  let scroll = NSScrollView()
  private let document = MixerStripDocument()
  private var observers = [NSObjectProtocol]()
  private var spare = [MixerStrip](), meters = [String: (Double, Double)]()
  private(set) var visible = [Int: MixerStrip](), buses = [[String: Any]](), createdCount = 0
  var selectedID: String?
  var onControl: ((String, String, Any, Bool) -> Void)?
  var onFinish: ((String) -> Void)?, onInspect: ((String) -> Void)?
  private var updating = false
  let stripWidth: CGFloat = 160, stripHeight: CGFloat = 508
  override init(frame: NSRect) {
    super.init(frame: frame)
    scroll.documentView = document; scroll.hasHorizontalScroller = true; scroll.hasVerticalScroller = true
    scroll.drawsBackground = false; scroll.fill(self)
    scroll.contentView.postsBoundsChangedNotifications = true
    scroll.contentView.postsFrameChangedNotifications = true
    for name in [NSView.boundsDidChangeNotification, NSView.frameDidChangeNotification] {
      observers.append(NotificationCenter.default.addObserver(forName: name, object: scroll.contentView, queue: .main) { [weak self] _ in self?.refreshVisible() })
    }
    setAccessibilityElement(true); setAccessibilityRole(.group); setAccessibilityLabel("Mixer strip overview")
  }
  required init?(coder: NSCoder) { fatalError() }
  deinit { for observer in observers { NotificationCenter.default.removeObserver(observer) } }
  override func layout() {
    super.layout()
    document.setFrameSize(NSSize(width: CGFloat(buses.count) * stripWidth, height: stripHeight))
    refreshVisible()
  }
  func update(_ buses: [[String: Any]], selected: String?) {
    self.buses = buses; selectedID = selected
    document.setFrameSize(NSSize(width: CGFloat(buses.count) * stripWidth, height: stripHeight))
    refreshVisible()
    for (index, strip) in visible where buses.indices.contains(index) {
      strip.update(buses[index], selected: buses[index]["id"] as? String == selectedID)
      strip.showMeter(meters[strip.busID] ?? (0, 0))
    }
  }
  func refreshVisible() {
    guard !updating else { return }; updating = true; defer { updating = false }
    let viewport = scroll.contentView.bounds
    let first = max(0, min(buses.count, Int(floor(viewport.minX / stripWidth)) - 1))
    let last = viewport.width > 0 ? max(first, min(buses.count, Int(ceil(viewport.maxX / stripWidth)) + 1)) : first
    let wanted = Set(first..<last)
    for index in Array(visible.keys) where !wanted.contains(index) {
      if visible[index]?.isTracking == true { continue }
      if let strip = visible.removeValue(forKey: index) { strip.removeFromSuperview(); spare.append(strip) }
    }
    for index in first..<last where visible[index] == nil {
      let strip: MixerStrip
      if let reused = spare.popLast() { strip = reused } else { strip = MixerStrip(frame: .zero); createdCount += 1 }
      strip.onControl = { [weak self] id, key, value, final in self?.onControl?(id, key, value, final) }
      strip.onFinish = { [weak self] id in
        guard let self else { return }; self.onFinish?(id)
        self.update(self.buses, selected: self.selectedID)
      }
      strip.onInspect = { [weak self] id in self?.onInspect?(id) }
      strip.frame = NSRect(x: CGFloat(index) * stripWidth + 4, y: 4, width: stripWidth - 8, height: stripHeight - 8)
      strip.update(buses[index], selected: buses[index]["id"] as? String == selectedID)
      strip.showMeter(meters[strip.busID] ?? (0, 0)); document.addSubview(strip); visible[index] = strip
    }
    if spare.count > 2 { spare.removeFirst(spare.count - 2) }
  }
  func showMeters(_ values: [[AnyHashable: Any]]) {
    guard !isHiddenOrHasHiddenAncestor else { return }
    meters.removeAll(keepingCapacity: true)
    for value in values { if let id = value["bus"] as? String {
      meters[id] = ((value["left"] as? NSNumber)?.doubleValue ?? 0, (value["right"] as? NSNumber)?.doubleValue ?? 0)
    } }
    for strip in visible.values { strip.showMeter(meters[strip.busID] ?? (0, 0)) }
  }
  func reveal(_ id: String) {
    guard let index = buses.firstIndex(where: { $0["id"] as? String == id }) else { return }
    document.scrollToVisible(NSRect(x: CGFloat(index) * stripWidth, y: 0, width: stripWidth, height: 1))
    refreshVisible()
  }
}
