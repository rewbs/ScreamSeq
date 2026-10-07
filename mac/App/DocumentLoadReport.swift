import AppKit

// Load provenance outlives status messages and a successful Save As. The save
// acknowledgement belongs to one document, so a later open protects its source
// independently even if it has the same title or filename.
struct DocumentLoadReport {
  private(set) var warnings = [String]()
  private(set) var issues = [String]()
  private(set) var sourcePath = ""
  private(set) var requiresSaveAs = false
  private var documentID = ""
  private var saved = false

  mutating func update(_ model: PatternModel) {
    let id = String(model.revisionToken.split(separator: ":").first ?? "")
    if id != documentID { documentID = id; saved = false }
    warnings = model.loadWarnings
    issues = model.issues
    sourcePath = model.loadSourcePath
    requiresSaveAs = model.requiresSaveAs && !saved
  }
  mutating func didSave() { saved = true; requiresSaveAs = false }
  var isVisible: Bool { requiresSaveAs || !warnings.isEmpty }
  var summary: String {
    let count = warnings.isEmpty ? "" : " · \(warnings.count) \(warnings.count == 1 ? "warning" : "warnings")"
    return "Recovered project\(count)" + (requiresSaveAs ? " · Save a copy" : " · Review load report")
  }
  func suggestedFilename(title: String) -> String {
    let title = title.isEmpty ? "Untitled" : title
    guard requiresSaveAs else { return title + ".screamseq" }
    let original = sourcePath.isEmpty ? title : URL(fileURLWithPath: sourcePath).deletingPathExtension().lastPathComponent
    return (original.isEmpty ? title : original) + " Recovered.screamseq"
  }
  var text: String {
    var sections = [String]()
    if !sourcePath.isEmpty { sections.append("Source file\n" + sourcePath) }
    if requiresSaveAs {
      sections.append("This project was recovered from an incompatible file. Some data may have been converted or omitted; review the warnings below. The original is protected from overwrite. Use Save a copy to keep this recovered version.")
    }
    var seen = Set<String>()
    let entries = (warnings + issues).filter { !$0.isEmpty && seen.insert($0).inserted }
    if entries.isEmpty {
      sections.append("No missing samples or inactive tracker plug-ins were detected. Native editing supports MOD, XM, S3M, IT, and MPTM. Other formats are preview-only.")
    } else {
      sections.append(entries.enumerated().map { "\($0.offset + 1). \($0.element)" }.joined(separator: "\n\n"))
    }
    return sections.joined(separator: "\n\n")
  }
}

final class DocumentLoadBanner: NSView {
  let message = Theme.label("", size: 12, color: Theme.gold, weight: .semibold)
  let report = ActionButton("View report…", symbol: "exclamationmark.triangle") {}
  let saveCopy = ActionButton("Save a copy…") {}
  override init(frame: NSRect) {
    super.init(frame: frame)
    fixed(height: 42)
    message.lineBreakMode = .byTruncatingTail
    message.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    stack(.horizontal, [message, NSView(), report, saveCopy], spacing: 12).fill(self, inset: 8)
    setAccessibilityIdentifier("document-load-warning")
    isHidden = true
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ state: DocumentLoadReport) {
    isHidden = !state.isVisible
    message.stringValue = state.summary
    message.toolTip = state.summary
    saveCopy.isHidden = !state.requiresSaveAs
  }
}

final class DocumentLoadReportView: NSView {
  let text = NSTextView()
  let scroll = NSScrollView()
  let saveCopy = ActionButton("Save a copy…") {}
  override init(frame: NSRect) {
    super.init(frame: frame)
    text.isEditable = false; text.isSelectable = true
    text.isRichText = false; text.font = .systemFont(ofSize: 13)
    text.textColor = Theme.text; text.backgroundColor = Theme.bg
    text.textContainerInset = NSSize(width: 12, height: 12)
    text.isVerticallyResizable = true; text.isHorizontallyResizable = false
    text.minSize = .zero; text.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
    text.autoresizingMask = [.width]
    text.textContainer?.containerSize = NSSize(width: 640, height: CGFloat.greatestFiniteMagnitude)
    text.textContainer?.widthTracksTextView = true
    text.setAccessibilityLabel("Project load and import report")
    scroll.documentView = text; scroll.hasVerticalScroller = true
    scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 220).isActive = true
    let body = stack(.vertical, [Theme.label("Project load report", size: 22, weight: .semibold), scroll,
      stack(.horizontal, [NSView(), saveCopy])], spacing: 14)
    body.stretchAcrossAxis(); body.fill(self, inset: 20)
  }
  required init?(coder: NSCoder) { fatalError() }
  func update(_ state: DocumentLoadReport) {
    if text.string != state.text { text.string = state.text }
    saveCopy.isHidden = !state.requiresSaveAs
  }
}
