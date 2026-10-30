import AppKit

extension SampleEditor {
  var numericDrawingInput: [String: String] {
    ["frame": drawingFrame.currentEditor()?.string ?? drawingFrame.stringValue,
      "value": drawingValue.currentEditor()?.string ?? drawingValue.stringValue,
      "interpolation": drawingInterpolation.indexOfSelectedItem == 1 ? "step" : "linear"]
  }
  var hasNumericDrawingInput: Bool { numericDrawingInput != numericDrawingBaseline }
  var protectsDrawingInput: Bool { drawingDraft?.submitted == true || drawingDraft?.numeric == true || hasNumericDrawingInput }
  var hasUnstagedDrawingPoint: Bool {
    ["frame", "value"].contains { numericDrawingInput[$0] != numericDrawingBaseline[$0] }
  }
  var hasOtherDrawingForm: Bool { settingsDraft.hasDraft || hasLoopDraft || settingsPending || loopsBusy || settingsNeedsReload || loopNeedsReload }
  func handleNumericDrawingKey(_ event: NSEvent) -> Bool {
    guard [36, 76].contains(event.keyCode), event.modifierFlags.intersection([.command, .control, .option, .shift]) == .command,
      drawingDraft?.numeric == true, let responder = window?.firstResponder,
      responder === waveform || responder === self || [drawingFrame, drawingValue].contains(where: { $0.currentEditor() === responder }) else { return false }
    saveStagedDrawing(); return true
  }
  func makeNumericDrawingControls() -> NSView {
    for field in [drawingFrame, drawingValue] { field.delegate = self; field.target = self; field.action = #selector(stageDrawingPoint) }
    drawingInterpolation.addItems(withTitles: ["Linear", "Step"])
    drawingInterpolation.setAccessibilityLabel("Drawing interpolation")
    drawingInterpolation.target = self; drawingInterpolation.action = #selector(numericDrawingInterpolationChanged)
    saveDrawingButton.toolTip = "Save the staged batch · ⌘Return from a drawing field or waveform"
    let help = Theme.label("Return stages an exact frame/value point; ⌘Return saves the batch in one Undo step. Values range from −1 to 1. A batch holds up to 4096 points spanning at most 1,048,576 frames. Staging an existing frame replaces its value.", size: 11, color: Theme.muted)
    help.lineBreakMode = .byWordWrapping; help.maximumNumberOfLines = 4; help.preferredMaxLayoutWidth = 600
    help.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    return stack(.vertical, [
      stack(.horizontal, [labeled("FRAME", drawingFrame), labeled("VALUE", drawingValue), labeled("INTERPOLATION", drawingInterpolation), NSView()], spacing: 10),
      stack(.horizontal, [stageDrawingButton, saveDrawingButton, NSView()], spacing: 8), help], spacing: 8)
  }
  @objc func numericDrawingInterpolationChanged() {
    guard !hasOtherDrawingForm else {
      drawingInterpolation.selectItem(at: numericDrawingBaseline["interpolation"] == "step" ? 1 : 0)
      drawingStatus.stringValue = "Finish sample settings and loops before entering drawing fields."; return
    }
    numericDrawingInputChanged()
  }
  @objc func numericDrawingInputChanged() {
    if hasNumericDrawingInput, numericDrawingRevision == nil { numericDrawingRevision = drawingDraft?.revision ?? waveformRevision ?? sampleRevision }
    if !hasNumericDrawingInput, drawingDraft?.numeric != true { numericDrawingRevision = nil }
    if let draft = drawingDraft, draft.numeric, !draft.submitted {
      waveform.strokeInterpolation = numericDrawingInput["interpolation"]!
    }
    updateDrawingActions()
  }
  func resetNumericDrawingInput() {
    drawingFrame.stringValue = "0"; drawingValue.stringValue = "0"; drawingInterpolation.selectItem(at: 0)
    numericDrawingBaseline = numericDrawingInput; numericDrawingRevision = nil
  }
  func canEditNumericDrawing() -> Bool {
    guard !hasSampleComposition, !sampleOperationPending, sampleSelectionQuery == nil, waveformQuery == nil, !waveformInFlight,
      canBeginDrawing?() ?? true else { drawingStatus.stringValue = "Finish composing and wait for sample operations before staging points."; return false }
    guard drawingDraft == nil || drawingDraft?.numeric == true && drawingDraft?.submitted == false else {
      drawingStatus.stringValue = "Finish, review or discard the retained stroke before staging points."; return false
    }
    guard !settingsDraft.hasDraft, !hasLoopDraft, !settingsNeedsReload, !loopNeedsReload else {
      drawingStatus.stringValue = "Finish or reload sample settings and loops before staging points."; return false
    }
    let revision = drawingDraft?.revision ?? numericDrawingRevision ?? waveformRevision
    guard let revision, !settingsIdentity.isEmpty, waveform.frames > 0, waveformRevision == revision, sampleRevision == revision,
      (onSampleRevision?() ?? sampleRevision) == revision,
      catalogue.contains(where: { $0.index == index && $0.identity == settingsIdentity }) else {
      drawingStatus.stringValue = "Drawing fields belong to an earlier or unavailable sample. Reload or discard them before staging."; return false
    }
    return true
  }
  @objc func stageDrawingPoint() {
    guard canEditNumericDrawing() else { return }
    let input = numericDrawingInput
    guard let frame = Int(input["frame"]!.trimmingCharacters(in: .whitespaces)), (0..<waveform.frames).contains(frame),
      let value = Double(input["value"]!.trimmingCharacters(in: .whitespaces)), value.isFinite, (-1...1).contains(value) else {
      numericDrawingInputChanged(); drawingStatus.stringValue = "Use a whole frame inside the sample and a finite value from −1 to 1. Fields kept."; return
    }
    guard waveform.stroke[frame] != nil || waveform.stroke.count < 4096 else {
      drawingStatus.stringValue = "A batch holds at most 4096 points. Save or discard it before adding more."; return
    }
    let first = min(waveform.stroke.keys.min() ?? frame, frame), last = max(waveform.stroke.keys.max() ?? frame, frame)
    // Match maximumSampleDrawFrames in the shared sample drawing API. The
    // inclusive span must remain writable before the batch becomes submitted.
    guard last - first < 1_048_576 else {
      drawingStatus.stringValue = "A drawing batch spans at most 1,048,576 frames. Points and fields kept; choose a nearer frame."; return
    }
    let draft = drawingDraft ?? SampleDrawingDraft(self, revision: waveformRevision!, numeric: true)
    drawingDraft = draft; strokeRevision = draft.revision
    var points = waveform.stroke; points[frame] = value
    draft.points = points.keys.sorted().map { SampleStrokePoint(frame: $0, value: points[$0]!) }
    waveform.retainsStroke = true; waveform.stagePoints(draft.points, interpolation: input["interpolation"]!)
    numericDrawingBaseline = input; numericDrawingRevision = draft.revision
    updateDrawingActions()
    drawingStatus.stringValue = "\(draft.points.count) points staged · Audio unchanged · Save staged points creates one Undo step."
  }
  func saveStagedDrawing() {
    guard canEditNumericDrawing(), let draft = drawingDraft, draft.numeric, !draft.points.isEmpty else { return }
    guard !hasUnstagedDrawingPoint else { drawingStatus.stringValue = "Stage the newer frame/value first, or discard the drawing fields."; return }
    // Freeze the explicit batch, interpolation and all current raw fields at
    // submission. Retried writes reuse this exact captured payload.
    draft.interpolation = numericDrawingInput["interpolation"]!
    draft.fields = drawingFields; draft.settingsGeneration = settingsEditGeneration; draft.loopGeneration = loopEditGeneration
    submitDrawing(draft)
  }
}
