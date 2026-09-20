import AppKit

enum Theme {
  static let bg = NSColor(srgbRed: 0.055, green: 0.069, blue: 0.088, alpha: 1)
  static let panel = NSColor(srgbRed: 0.080, green: 0.097, blue: 0.121, alpha: 1)
  static let raised = NSColor(srgbRed: 0.111, green: 0.133, blue: 0.16, alpha: 1)
  static let border = NSColor(srgbRed: 0.18, green: 0.21, blue: 0.25, alpha: 1)
  static let text = NSColor(srgbRed: 0.86, green: 0.89, blue: 0.92, alpha: 1)
  static let muted = NSColor(srgbRed: 0.46, green: 0.52, blue: 0.59, alpha: 1)
  static let accent = NSColor(srgbRed: 0.37, green: 0.88, blue: 0.72, alpha: 1)
  static let gold = NSColor(srgbRed: 0.93, green: 0.72, blue: 0.39, alpha: 1)
  static func label(
    _ text: String, size: CGFloat = 12, color: NSColor = Theme.text,
    weight: NSFont.Weight = .regular, mono: Bool = false
  ) -> NSTextField {
    let l = NSTextField(labelWithString: text)
    l.font =
      mono
      ? .monospacedSystemFont(ofSize: size, weight: weight)
      : .systemFont(ofSize: size, weight: weight)
    l.textColor = color
    l.lineBreakMode = .byTruncatingTail
    return l
  }
}
final class Panel: NSView {
  init(_ color: NSColor = Theme.panel) {
    super.init(frame: .zero)
    wantsLayer = true
    layer?.backgroundColor = color.cgColor
  }
  required init?(coder: NSCoder) { fatalError() }
}
// A flipped clip view anchors short documents at the top and makes scrolling
// consistent with the pattern editor's top-to-bottom coordinates.
final class TopAlignedClipView: NSClipView {
  override var isFlipped: Bool { true }
}
func verticalScrollView() -> NSScrollView {
  let scroll = NSScrollView()
  scroll.contentView = TopAlignedClipView()
  scroll.drawsBackground = false
  scroll.hasVerticalScroller = true
  return scroll
}
extension NSStackView {
  func stretchAcrossAxis() {
    alignment = orientation == .vertical ? .leading : .top
    for view in arrangedSubviews {
      if orientation == .vertical {
        view.widthAnchor.constraint(equalTo: widthAnchor).isActive = true
      } else {
        view.heightAnchor.constraint(equalTo: heightAnchor).isActive = true
      }
    }
  }
}
final class ActionButton: NSButton {
  var handler: (() -> Void)?
  init(_ title: String, symbol: String? = nil, action: @escaping () -> Void) {
    super.init(frame: .zero)
    self.title = title
    handler = action
    target = self
    self.action = #selector(invoke)
    bezelStyle = .rounded
    font = .systemFont(ofSize: 12, weight: .medium)
    if let symbol {
      image = NSImage(systemSymbolName: symbol, accessibilityDescription: title)
      imagePosition = title.isEmpty ? .imageOnly : .imageLeading
    }
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc func invoke() { handler?() }
}
// Keep tools mounted when collapsed so selection, previews and uncommitted fields survive.
final class ToolSection: NSView {
  let content: NSStackView
  let toggle: ActionButton
  let preference: String
  private(set) var expanded: Bool
  init(_ title: String, id: String, views: [NSView], expanded defaultExpanded: Bool = false) {
    preference = "toolSection.\(id)"
    expanded = UserDefaults.standard.object(forKey: preference) as? Bool ?? defaultExpanded
    content = stack(.vertical, views, spacing: 8)
    toggle = ActionButton(title, symbol: "chevron.right", action: {})
    super.init(frame: .zero)
    toggle.bezelStyle = .inline
    toggle.contentTintColor = Theme.muted
    toggle.setAccessibilityLabel(title)
    toggle.handler = { [weak self] in guard let self else { return }; self.setExpanded(!self.expanded) }
    content.stretchAcrossAxis()
    let rows = stack(.vertical, [toggle, content], spacing: 8)
    rows.fill(self)
    content.widthAnchor.constraint(equalTo: rows.widthAnchor).isActive = true
    setExpanded(expanded, persist: false)
  }
  required init?(coder: NSCoder) { fatalError() }
  func setExpanded(_ value: Bool, persist: Bool = true) {
    expanded = value
    content.isHidden = !value
    toggle.image = NSImage(systemSymbolName: value ? "chevron.down" : "chevron.right", accessibilityDescription: nil)
    toggle.setAccessibilityValue(value ? "Expanded" : "Collapsed")
    if persist { UserDefaults.standard.set(value, forKey: preference) }
  }
}
extension NSView {
  func fixed(width: CGFloat? = nil, height: CGFloat? = nil) {
    translatesAutoresizingMaskIntoConstraints = false
    if let width { widthAnchor.constraint(equalToConstant: width).isActive = true }
    if let height { heightAnchor.constraint(equalToConstant: height).isActive = true }
  }
  func fill(_ parent: NSView, inset: CGFloat = 0) {
    translatesAutoresizingMaskIntoConstraints = false
    parent.addSubview(self)
    NSLayoutConstraint.activate([
      leadingAnchor.constraint(equalTo: parent.leadingAnchor, constant: inset),
      trailingAnchor.constraint(equalTo: parent.trailingAnchor, constant: -inset),
      topAnchor.constraint(equalTo: parent.topAnchor, constant: inset),
      bottomAnchor.constraint(equalTo: parent.bottomAnchor, constant: -inset),
    ])
  }
}
func stack(_ orientation: NSUserInterfaceLayoutOrientation, _ views: [NSView], spacing: CGFloat = 8)
  -> NSStackView
{
  let s = NSStackView(views: views)
  s.orientation = orientation
  s.distribution = .fill
  s.spacing = spacing
  s.alignment = orientation == .horizontal ? .centerY : .leading
  return s
}

struct NoteTrack {
  let id: String, name: String
  let channels: [Int]
  let color: Int
}
struct PatternModel {
  var title = "Untitled", format = "MPTM"
  var channels = 8, rows = 64, pattern = 0, speed = 6, noteMin = 1, noteMax = 120
  var tempo = 125.0
  var rowsPerBeat = 4, rowsPerMeasure = 16
  var tempoText: String { String(format: "%.4f", tempo).replacingOccurrences(of: #"\.?0+$"#, with: "", options: .regularExpression) }
  var effectLetters = [String](), volumeLetters = [String]()
  var commands = PatternCommandCatalog.empty
  var issues = [String]()
  var nativePlugins = [[String: Any]](), pluginError = "", automationPoints = 0
  var canUndoEffect = false, canRedoEffect = false
  var editable = true, hasNativeMetadata = false
  var orderMetadata = [[String: Any]](), tracks = [[String: Any]]()
  var noteTracks = [NoteTrack](), noteTrackByChannel = [Int: NoteTrack]()
  var trackDestinations = [[String: Any]]()
  var maximumColumns = 127
  var mutedColumns = Set<Int>()
  var graphLanes=[GraphPatternLane](),graphCommands=[String:GraphPatternCommand]()
  var preciseNotes = [Int:[PreciseNote]]()
  var preciseNoteEffects = [[String:Any]]()
  func notes(_ row:Int,_ channel:Int)->[PreciseNote] {preciseNotes[row*channels+channel] ?? []}
  var extraEffectColumns = [Int](), performanceCommands = [Int: NativePatternCommand]()
  var channelOffsets: [Float] = []
  func extraColumns(_ channel: Int) -> Int { extraEffectColumns.indices.contains(channel) ? extraEffectColumns[channel] : 0 }
  func nativeCommand(_ row: Int, _ channel: Int, _ column: Int) -> NativePatternCommand? { performanceCommands[(row * channels + channel) * 8 + column] }
  func channelOffset(_ channel: Int) -> Float { channelOffsets.indices.contains(channel) ? channelOffsets[channel] : Float(channel) * 162 }
  func channelWidth(_ channel: Int) -> Float { 162 + Float(extraColumns(channel)) * 96 }
  var revisionToken = ""
  var sequence = 0, sequences = [[String: Any]]()
  var cells = [UInt8](repeating: 0, count: 64 * 8 * 6)
  var orders = [0], patterns = [[String: Any]](), samples = [[String: Any]](),
    instruments = [[String: Any]]()
  init(_ dictionary: [AnyHashable: Any]) {
    orderMetadata = dictionary["orderMetadata"] as? [[String: Any]] ?? []
    tracks = dictionary["tracks"] as? [[String: Any]] ?? []
    for lane in dictionary["graphLanes"] as? [[String:Any]] ?? [] {for column in 0..<max(0,min(8,lane["count"] as? Int ?? 0)){graphLanes.append(GraphPatternLane(target:lane["target"] as? String ?? "",name:lane["name"] as? String ?? "Bus",column:column))}}
    for c in dictionary["graphCommands"] as? [[String:Any]] ?? [] {let command=GraphPatternCommand(target:c["target"] as? String ?? "",graph:c["graph"] as? String ?? "",kind:c["kind"] as? String ?? "",position:c["position"] as? Int ?? 0,column:c["column"] as? Int ?? 0,number:c["number"] as? Int ?? 0,amount:c[(c["kind"] as? String)=="wet" ? "wet" : "amount"] as? Double ?? 1);graphCommands[GraphLaneStrip.key(command.row,command.target,command.column)]=command}
    mutedColumns = Set(tracks.compactMap { ($0["mute"] as? Bool == true) ? $0["index"] as? Int : nil })
    if let layout = dictionary["trackLayout"] as? [String: Any] {
      maximumColumns = layout["maximumColumns"] as? Int ?? 127
      trackDestinations = layout["destinations"] as? [[String: Any]] ?? []
      for raw in layout["noteTracks"] as? [[String: Any]] ?? [] {
        guard let id = raw["id"] as? String, let channels = raw["channels"] as? [Int], !channels.isEmpty else { continue }
        let track = NoteTrack(id: id, name: raw["name"] as? String ?? "Track", channels: channels, color: raw["color"] as? Int ?? 0)
        noteTracks.append(track)
        for channel in channels { noteTrackByChannel[channel] = track }
      }
    }
    revisionToken = dictionary["revisionToken"] as? String ?? ""
    hasNativeMetadata = dictionary["hasNativeMetadata"] as? Bool ?? false
    issues = dictionary["issues"] as? [String] ?? []
    nativePlugins = dictionary["nativePlugins"] as? [[String: Any]] ?? []
    pluginError = dictionary["pluginError"] as? String ?? ""
    automationPoints = dictionary["automationPoints"] as? Int ?? 0
    canUndoEffect = dictionary["canUndoEffect"] as? Bool ?? false
    canRedoEffect = dictionary["canRedoEffect"] as? Bool ?? false
    editable = dictionary["editable"] as? Bool ?? true
    sequence = dictionary["sequence"] as? Int ?? 0
    sequences = dictionary["sequences"] as? [[String: Any]] ?? []
    effectLetters = dictionary["effectLetters"] as? [String] ?? []
    if let catalog = dictionary["commandCatalog"] as? [String: Any] { commands = PatternCommandCatalog(catalog) }
    volumeLetters = dictionary["volumeLetters"] as? [String] ?? []
    title = dictionary["title"] as? String ?? title
    format = dictionary["format"] as? String ?? format
    noteMin = dictionary["noteMin"] as? Int ?? 1
    noteMax = dictionary["noteMax"] as? Int ?? 120
    channels = dictionary["channels"] as? Int ?? channels
    rows = dictionary["rows"] as? Int ?? rows
    extraEffectColumns = (dictionary["extraEffectColumns"] as? [Int] ?? []).map { max(0,min(8,$0)) }
    var x: Float = 0
    for channel in 0..<channels { channelOffsets.append(x); x += channelWidth(channel) }
    channelOffsets.append(x)
    for raw in dictionary["performanceCommands"] as? [[String: Any]] ?? [] {
      let command = NativePatternCommand(raw)
      guard command.row >= 0, command.row < rows, command.channel >= 0, command.channel < channels, command.column >= 0, command.column < extraColumns(command.channel) else { continue }
      performanceCommands[(command.row * channels + command.channel) * 8 + command.column] = command
    }
    for raw in dictionary["preciseNotes"] as? [[String:Any]] ?? [] {
      let event=PreciseNote(raw)
      if event.channel>=0 && event.channel<channels && event.row>=0 && event.row<rows {preciseNotes[event.row*channels+event.channel,default:[]].append(event)}
    }
    preciseNoteEffects = dictionary["preciseNoteEffects"] as? [[String:Any]] ?? []
    pattern = dictionary["pattern"] as? Int ?? 0
    tempo = (dictionary["tempo"] as? NSNumber)?.doubleValue ?? tempo
    let beat = dictionary["displayRowsPerBeat"] as? Int ?? 4
    let measure = dictionary["displayRowsPerMeasure"] as? Int ?? 16
    rowsPerBeat = beat > 0 ? beat : 4
    rowsPerMeasure = max(rowsPerBeat, measure > 0 ? measure : 16)
    speed = dictionary["speed"] as? Int ?? speed
    if let data = dictionary["cells"] as? Data { cells = Array(data) }
    orders = dictionary["orders"] as? [Int] ?? [0]
    patterns = dictionary["patterns"] as? [[String: Any]] ?? []
    samples = dictionary["samples"] as? [[String: Any]] ?? []
    instruments = dictionary["instruments"] as? [[String: Any]] ?? []
  }
  func drawCell(_ row: Int, _ channel: Int) -> (
    note: UInt8, instrument: UInt8, volumeCommand: UInt8, volume: UInt8, effect: UInt8,
    parameter: UInt8
  ) {
    let i = (row * channels + channel) * 6
    guard i >= 0, i + 6 <= cells.count else { return (0, 0, 0, 0, 0, 0) }
    return (cells[i], cells[i + 1], cells[i + 2], cells[i + 3], cells[i + 4], cells[i + 5])
  }
  func cell(_ row: Int, _ channel: Int) -> [UInt8] {
    let i = (row * channels + channel) * 6
    guard i >= 0, i + 6 <= cells.count else { return [UInt8](repeating: 0, count: 6) }
    return Array(cells[i..<i + 6])
  }
  mutating func replaceCell(_ row: Int, _ channel: Int, with values: [UInt8]) {
    guard row >= 0, row < rows, channel >= 0, channel < channels, values.count == 6 else { return }
    let index = (row * channels + channel) * 6
    guard index + 6 <= cells.count else { return }
    cells.replaceSubrange(index..<index + 6, with: values)
  }
}
