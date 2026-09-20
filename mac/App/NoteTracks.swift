import AppKit

/// A revision-pinned editor. Selection changes behind this window never retarget its request.
final class NoteTrackEditor: NSView {
  let nameField = NSTextField(string: ""), countField = NSTextField(string: "3"), destination = NSPopUpButton()
  let status = Theme.label("", size: 12, color: Theme.muted)
  let target = Theme.label("", size: 12)
  let model: PatternModel, channels: [Int], creating: Bool
  private(set) var pending = false, completed = false
  private var applyButton: NSButton?
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  init(model: PatternModel, channels: [Int], creating: Bool) {
    self.model = model; self.channels = channels; self.creating = creating
    super.init(frame: .zero)
    nameField.placeholderString = "Track name"; nameField.setAccessibilityLabel("Track name")
    countField.fixed(width: 64); countField.setAccessibilityLabel("Number of note columns")
    destination.addItem(withTitle: "Keep current destination")
    for bus in model.trackDestinations { destination.addItem(withTitle: bus["name"] as? String ?? "Mixer bus") }
    destination.setAccessibilityLabel("Shared track output")
    target.stringValue = creating ? "Append empty note columns to all patterns." : "Group columns \((channels.first ?? 0) + 1)–\((channels.last ?? 0) + 1). Existing notes stay in place."
    target.maximumNumberOfLines = 2; target.lineBreakMode = .byWordWrapping
    status.maximumNumberOfLines = 3; status.lineBreakMode = .byWordWrapping
    status.heightAnchor.constraint(greaterThanOrEqualToConstant: 48).isActive = true
    let detail = Theme.label("Columns share a mixer group and keep their own note and command data. Click a column header to mute it. Route plugin audio outputs in the Mixer.", size: 12, color: Theme.muted)
    detail.maximumNumberOfLines = 4; detail.lineBreakMode = .byWordWrapping
    for label in [target, detail, status] {
      label.preferredMaxLayoutWidth = 536
      label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    }
    nameField.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    destination.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    var rows: [NSView] = [Theme.label(creating ? "New note track" : "Group note columns", size: 20, weight: .semibold), target,
      stack(.horizontal, [Theme.label("Name", size: 12), nameField])]
    if creating { rows.append(stack(.horizontal, [Theme.label("Note columns", size: 12), countField, Theme.label("\(max(0, model.maximumColumns - model.channels)) available in this format", size: 12, color: Theme.muted), NSView()])) }
    let submit = ActionButton(creating ? "Create track" : "Group columns") { [weak self] in self?.apply() }
    applyButton = submit
    rows += [stack(.horizontal, [Theme.label("Output", size: 12), destination]), detail,
      stack(.horizontal, [submit, NSView()]), status]
    let content = stack(.vertical, rows, spacing: 14)
    content.stretchAcrossAxis()
    content.translatesAutoresizingMaskIntoConstraints = false; addSubview(content)
    NSLayoutConstraint.activate([content.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 22),
      content.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -22), content.topAnchor.constraint(equalTo: topAnchor, constant: 22),
      content.bottomAnchor.constraint(lessThanOrEqualTo: bottomAnchor, constant: -18)])
  }
  required init?(coder: NSCoder) { fatalError() }
  func apply() {
    guard !pending, !completed, model.editable, let onRequest else { return }
    var params: [String: Any] = ["expectedRevision": model.revisionToken, "name": nameField.stringValue]
    if creating {
      guard let count = Int(countField.stringValue), count >= 1, count <= min(127, model.maximumColumns - model.channels) else {
        status.stringValue = "Choose a column count within the available format limit."; return
      }
      params["columns"] = count
    } else { params["channels"] = channels }
    if destination.indexOfSelectedItem > 0 {
      params["output"] = model.trackDestinations[destination.indexOfSelectedItem - 1]["id"]
    }
    pending = true; applyButton?.isEnabled = false; status.stringValue = "Updating track…"
    onRequest(creating ? "track.create" : "track.group", params) { [weak self] response in
      guard let self else { return }; self.pending = false
      if let error = response["error"] as? [String: Any] {
        self.applyButton?.isEnabled = true; self.status.stringValue = error["message"] as? String ?? "Track edit failed"; return
      }
      guard response["result"] != nil else { self.applyButton?.isEnabled = true; self.status.stringValue = "No result returned."; return }
      self.completed = true
      self.status.stringValue = "Track ready. Adjust its shared processing in the Mixer."
    }
  }
}
