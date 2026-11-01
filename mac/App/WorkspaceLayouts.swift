import AppKit
import Darwin

/// Presentation preferences, deliberately separate from document history. A lock
/// and generation check prevent two running sessions overwriting each other.
final class WorkspaceLayouts {
  struct Failure: LocalizedError { let message: String; var errorDescription: String? { message } }
  private let file: URL?
  private let legacy: [String: Any]?
  private var revision = "empty"
  private(set) var layouts = [String: [String: Any]]()
  private(set) var error: String?
  var names: [String] { layouts.keys.sorted() }
  init(file: URL?, legacy: [String: Any]? = nil) {
    self.file = file
    self.legacy = legacy
    do { try reload() } catch { self.error = error.localizedDescription }
    if revision == "empty", let legacy { var state = legacy; state.removeValue(forKey: "pins"); layouts["Custom"] = state }
  }
  static func validName(_ name: String) -> Bool {
    !name.isEmpty && name.count <= 64 && name == name.trimmingCharacters(in: .whitespacesAndNewlines) &&
      name.unicodeScalars.allSatisfy { !CharacterSet.controlCharacters.contains($0) }
  }
  private func read() throws -> (String, [String: [String: Any]]) {
    guard let file, FileManager.default.fileExists(atPath: file.path) else { return ("empty", [:]) }
    guard (try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0) <= 1_048_576,
      let json = try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [String: Any],
      json["version"] as? Int == 1, let revision = json["revision"] as? String,
      let layouts = json["layouts"] as? [String: [String: Any]], layouts.count <= 24,
      layouts.keys.allSatisfy(Self.validName)
    else { throw Failure(message: "Saved layouts are invalid; the existing file has been preserved") }
    return (revision, layouts)
  }
  func reload() throws {
    guard file != nil else { error = nil; return }
    do {
      (revision, layouts) = try read()
      if revision == "empty", let legacy {var state=legacy;state.removeValue(forKey:"pins");layouts["Custom"]=state}
      error = nil
    }
    catch { self.error = error.localizedDescription; throw error }
  }
  func save(_ name: String, state: [String: Any]) throws {
    guard Self.validName(name) else { throw Failure(message: "Use 1–64 characters without controls or surrounding spaces") }
    var candidate = layouts, presentation = state; presentation.removeValue(forKey: "pins")
    candidate[name] = presentation
    guard candidate.count <= 24 else { throw Failure(message: "Keep at most 24 named layouts") }
    try publish(candidate)
  }
  func delete(_ name: String) throws {
    guard layouts[name] != nil else { throw Failure(message: "Choose a saved layout") }
    var candidate = layouts; candidate.removeValue(forKey: name); try publish(candidate)
  }
  private func publish(_ candidate: [String: [String: Any]]) throws {
    if let error { throw Failure(message: error + " · Refresh saved before writing") }
    let next = UUID().uuidString
    let data = try JSONSerialization.data(withJSONObject: ["version": 1, "revision": next, "layouts": candidate], options: [.sortedKeys])
    guard data.count <= 1_048_576 else { throw Failure(message: "Saved layouts exceed the storage limit") }
    if let file {
      try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
      let descriptor = open(file.appendingPathExtension("lock").path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)
      guard descriptor >= 0 else { throw Failure(message: "Could not open the saved-layout lock") }
      defer { close(descriptor) }
      guard flock(descriptor, LOCK_EX | LOCK_NB) == 0 else { throw Failure(message: "Another session is saving layouts; retry shortly") }
      defer { flock(descriptor, LOCK_UN) }
      guard try read().0 == revision else { throw Failure(message: "Saved layouts changed in another session; Refresh saved before writing") }
      try data.write(to: file, options: .atomic)
    }
    revision = next; layouts = candidate
  }
}

final class WorkspaceLayoutManager: NSObject {
  let window: NSPanel
  let name = NSTextField(string: "Custom"), saved = NSPopUpButton(), preset = NSPopUpButton()
  let status = Theme.label("Layouts preserve editor targets, pins and unfinished edits", size: 11, color: Theme.muted)
  var onAction: ((String, String?) -> String?)?, onNames: (() -> [String])?
  override init() {
    window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 600, height: 205), styleMask: [.titled, .closable], backing: .buffered, defer: false)
    super.init()
    window.title = "Workspace layouts"; window.isReleasedWhenClosed = false
    name.placeholderString = "Layout name"; name.setAccessibilityLabel("Layout name")
    saved.setAccessibilityLabel("Saved layouts"); saved.target = self; saved.action = #selector(selectSaved)
    preset.addItems(withTitles: ["Compose", "Pattern focus", "Sound design", "Connected", "Graph editing"])
    let rows = stack(.vertical, [
      stack(.horizontal, [Theme.label("Preset", size: 12), preset, ActionButton("Apply preset") { [weak self] in self?.act(self?.preset.titleOfSelectedItem ?? "Compose", named: false) }]),
      stack(.horizontal, [Theme.label("Saved", size: 12), saved, ActionButton("Restore") { [weak self] in self?.act("Restore custom") }, ActionButton("Delete") { [weak self] in self?.act("Delete custom") }, ActionButton("Refresh saved") { [weak self] in self?.act("Reload saved", named: false) }]),
      stack(.horizontal, [name, ActionButton("Save layout") { [weak self] in self?.act("Save custom") }]), status
    ], spacing: 12)
    rows.stretchAcrossAxis(); rows.fill(window.contentView!, inset: 16)
  }
  @objc func selectSaved() { if let selected = saved.titleOfSelectedItem { name.stringValue = selected } }
  func refresh() { let selected = saved.titleOfSelectedItem; saved.removeAllItems(); saved.addItems(withTitles: onNames?() ?? []); if let selected { saved.selectItem(withTitle: selected) } }
  func act(_ action: String, named: Bool = true) {
    if let error = onAction?(action, named ? name.stringValue : nil) { status.stringValue = error }
    else { refresh(); status.stringValue = action == "Reload saved" ? "Saved layouts refreshed" : "\(action) · \(named ? name.stringValue : action)" }
  }
  func show() { refresh(); window.center(); window.makeKeyAndOrderFront(nil) }
}
