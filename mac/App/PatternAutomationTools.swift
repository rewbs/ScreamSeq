import AppKit

extension PatternAutomationEditor {
  func makeTools() -> NSView {
    toolOperation.addItems(withTitles: ["Flip time", "Flip values", "Shift", "Scale", "Ramp", "Sine", "Humanize", "Paste", "Insert paste"])
    toolOperation.target = self; toolOperation.action = #selector(changeTool)
    toolOperation.fixed(width: 150)
    for (field, label) in [(toolStart, "Automation range start row"), (toolEnd, "Automation range end row, exclusive")] {
      field.fixed(width: 70); field.setAccessibilityLabel(label)
    }
    for field in toolValues { field.fixed(width: 72) }
    toolValueGroups = zip(toolLabels, toolValues).map { label, field in stack(.horizontal, [label, field]) }
    let range = stack(.horizontal, [Theme.label("Range (rows)", size: 12), toolStart, Theme.label("to", size: 12), toolEnd,
      toolOperation, ActionButton("Preview tool") { [weak self] in self?.previewTool() },
      ActionButton("Copy range") { [weak self] in self?.copyRange() }, NSView()])
    let settings = stack(.horizontal, toolValueGroups + [NSView()])
    changeTool()
    return stack(.vertical, [range, settings], spacing: 8)
  }
  @objc func changeTool() {
    let configs: [[(String, String)]] = [[], [], [("Shift (rows)", "1")], [("Multiply", "1"), ("Add (%)", "0")],
      [("From (%)", "0"), ("To (%)", "100")], [("Center (%)", "50"), ("Amplitude (%)", "50"), ("Cycles", "1"), ("Phase (°)", "0")],
      [("Value jitter (%)", "5"), ("Time jitter (rows)", "0"), ("Seed", "0")], [("Repeats", "1")], [("Repeats", "1")]]
    let config = configs[max(0, toolOperation.indexOfSelectedItem)]
    for i in 0..<4 {
      toolValueGroups[i].isHidden = i >= config.count
      if i < config.count { toolLabels[i].stringValue = config[i].0; toolValues[i].stringValue = config[i].1; toolValues[i].setAccessibilityLabel(config[i].0) }
    }
  }
  func toolRange() -> (Int, Int)? {
    guard let first = Double(toolStart.stringValue), let last = Double(toolEnd.stringValue), first.isFinite, last.isFinite,
      first >= 0, first < last, last <= Double(canvas.rows), (first * 256).rounded() == first * 256,
      (last * 256).rounded() == last * 256 else {
      status.stringValue = "Choose a nonempty range inside the pattern, in steps of 1/256 row."; return nil
    }
    return (Int(first * 256), Int(last * 256))
  }
  func copyRange() {
    guard !loading, !hasDraft, let laneID, let (start, end) = toolRange() else {
      if hasDraft || laneID == nil { status.stringValue = "Apply the envelope before copying or transforming its saved points." }; return
    }
    request("automation.pattern.copy", ["lane": laneID, "start": start, "end": end]) { data in
      self.envelopeClipboard = data
      self.status.stringValue = "Copied \((data["points"] as? [Any])?.count ?? 0) points. Choose Paste or Insert paste and a start row."
    }
  }
  func previewTool() {
    guard !loading, !hasDraft, let laneID, let (start, end) = toolRange() else {
      if hasDraft || laneID == nil { status.stringValue = "Apply the envelope before copying or transforming its saved points." }; return
    }
    let operation = ["flip-time", "flip-values", "shift", "scale", "ramp", "sine", "humanize", "paste", "insert"][max(0, toolOperation.indexOfSelectedItem)]
    var numbers = [Double]()
    for i in toolValues.indices where !toolValueGroups[i].isHidden {
      guard let n = Double(toolValues[i].stringValue), n.isFinite else { status.stringValue = "Enter finite numbers in the tool settings."; return }
      numbers.append(n)
    }
    var options: [String: Any] = [:]
    switch operation {
    case "shift": options = ["amount": numbers[0] * 256]
    case "scale": options = ["amount": numbers[0], "offset": numbers[1] / 100]
    case "ramp": options = ["from": numbers[0] / 100, "to": numbers[1] / 100, "curve": canvas.curve]
    case "sine": options = ["center": numbers[0] / 100, "amplitude": numbers[1] / 100, "cycles": numbers[2], "phase": numbers[3], "spacing": canvas.snap, "curve": canvas.curve]
    case "humanize": options = ["amount": numbers[0] / 100, "jitter": numbers[1] * 256, "seed": numbers[2]]
    case "paste", "insert":
      guard let envelopeClipboard else { status.stringValue = "Copy an envelope range first."; return }
      options = ["clip": envelopeClipboard, "repeats": numbers[0]]
    default: break
    }
    var params: [String: Any] = ["expectedRevision": revision, "lane": laneID, "operation": operation, "start": start, "options": options, "dryRun": true]
    if operation != "paste" && operation != "insert" { params["end"] = end }
    let generation = draftGeneration
    let signature = toolSignature
    request("automation.pattern.transform", params) { data in
      guard self.draftGeneration == generation, self.laneID == laneID, self.toolSignature.isEqual(signature) else {
        self.status.stringValue = "The envelope or tool settings changed; preview discarded."; return
      }
      guard data["wouldChange"] as? Bool == true else { self.status.stringValue = "The tool leaves this envelope unchanged."; return }
      self.canvas.points = (data["after"] as? [[String: Any]] ?? []).map {
        EnvelopePoint(position: $0["position"] as? Int ?? 0, value: $0["value"] as? Double ?? 0, curve: $0["curve"] as? String ?? "linear", formula: $0["formula"] as? String ?? "")
      }
      self.canvas.selected = nil; self.markDraft()
      let clipped = data["clippedValues"] as? Int ?? 0
      self.status.stringValue = "Preview: \(self.canvas.points.count) points, \(clipped) values clipped. Apply saves one Undo step; Reload discards."
    }
  }
  var toolSignature: NSDictionary {
    ["operation": toolOperation.indexOfSelectedItem, "start": toolStart.stringValue, "end": toolEnd.stringValue,
     "values": toolValues.map(\.stringValue), "snap": canvas.snap, "curve": canvas.curve] as NSDictionary
  }
}
