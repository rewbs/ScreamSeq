import AppKit

enum SampleSnapTarget { case selection, normal, sustain }

final class SampleSnapQuery {
  let captured: SampleSelectionQuery, target: SampleSnapTarget, params: [String: Any], positions: [Int]
  let enabled: Bool
  var resumeAutomaticEdit: Bool
  init(_ editor: SampleEditor, target: SampleSnapTarget, params: [String: Any], positions: [Int]) {
    captured = SampleSelectionQuery(editor, sample: editor.index, identity: editor.settingsIdentity, kind: .reload, selectInput: false)
    self.target = target; self.params = params; self.positions = positions
    enabled = (target == .sustain ? editor.sustaining : editor.looping).state == .on
    resumeAutomaticEdit = editor.settingsWork != nil || editor.loopSaveWork != nil
  }
}

extension SampleEditor {
  func makeSnapControls() -> NSView {
    snapMode.addItems(withTitles: ["Zero crossings", "Frame grid"])
    snapMode.setAccessibilityLabel("Sample boundary snapping mode"); snapMode.fixed(width: 155)
    snapMode.target = self; snapMode.action = #selector(updateSnapOptions)
    snapDirection.addItems(withTitles: ["Nearest", "Before", "After"])
    snapDirection.setAccessibilityLabel("Sample snapping direction"); snapDirection.fixed(width: 110)
    snapStatus.maximumNumberOfLines = 3; snapStatus.lineBreakMode = .byWordWrapping
    updateSnapOptions()
    return stack(.horizontal, [labeled("SNAP", snapMode), labeled("DIRECTION", snapDirection), snapRadiusGroup, snapStepGroup, snapOriginGroup, NSView()], spacing: 14)
  }
  func makeSnapActions() -> NSView {
    stack(.horizontal, [ActionButton("Snap selection") { [weak self] in self?.snapBoundaries(target: .selection) },
      ActionButton("Snap normal") { [weak self] in self?.snapBoundaries(target: .normal) },
      ActionButton("Snap sustain") { [weak self] in self?.snapBoundaries(target: .sustain) }, NSView(), snapAutomatically], spacing: 10)
  }
  @objc func updateSnapOptions() {
    let grid = snapMode.indexOfSelectedItem == 1
    snapRadiusGroup.isHidden = grid; snapStepGroup.isHidden = !grid; snapOriginGroup.isHidden = !grid
  }
  func snapAfterSelection() {
    guard snapAutomatically.state == .on else { return }
    if snapBusy { snapPending = true } else { snapBoundaries(target: .selection) }
  }
  func finishSnapWaiters(_ accepted: Bool) {
    let callbacks = snapWaiters; snapWaiters = []; callbacks.forEach { $0(accepted) }
  }
  func cancelSnap() {
    snapQuery = nil; snapPending = false; finishSnapWaiters(false)
  }
  func finishSnap(_ query: SampleSnapQuery, accepted: Bool, message: String) {
    guard snapQuery === query else { return }
    snapQuery = nil; snapStatus.stringValue = message
    let pending = snapPending; snapPending = false
    if pending, snapAutomatically.state == .on {
      snapBoundaries(target: .selection)
      if let next = snapQuery { next.resumeAutomaticEdit = next.resumeAutomaticEdit || query.resumeAutomaticEdit; return }
      finishSnapWaiters(false)
    } else { finishSnapWaiters(accepted) }
    // Automatic fields are suspended during the read. Resume their normal
    // guarded completion without rebasing their original musical revision.
    if query.resumeAutomaticEdit, index == query.captured.sample, sampleGeneration == query.captured.generation, settingsIdentity == query.captured.identity { finishAutomaticEdit { _ in } }
  }
  func snapUnchanged(_ query: SampleSnapQuery) -> Bool {
    let captured = query.captured
    return index == captured.sample && sampleGeneration == captured.generation && settingsIdentity == captured.identity &&
      waveformDocumentID == captured.documentID && sampleRevision == captured.revision && waveformRevision == captured.revision &&
      (onSampleRevision?() ?? sampleRevision) == captured.revision &&
      catalogue.contains(where: { $0.index == captured.sample && $0.identity == captured.identity }) &&
      settingsEditGeneration == captured.settingsGeneration && loopEditGeneration == captured.loopGeneration &&
      drawingFields == captured.fields && sampleSelectionOptions == captured.options && !hasSampleComposition &&
      SampleViewRegion(self) == captured.originalRegion &&
      waveformChannels == captured.channels && selectedChannels == captured.channels && waveform.stroke == captured.stroke
  }
  func snapBoundaries(target: SampleSnapTarget) {
    guard !sampleOperationPending, !protectsSampleOperation, !protectsDrawingInput, waveform.stroke.isEmpty,
      !waveform.draggingLoop, !hasSampleComposition, sampleSelectionQuery == nil, !sampleSelectionInFlight,
      waveformQuery == nil, !waveformInFlight, loopReloadQuery == nil,
      onSettingsRequest != nil || onRequest != nil else {
      snapStatus.stringValue = "Finish the current sample action or drawing before snapping."; return
    }
    guard let revision = sampleRevision, !revision.isEmpty, waveformRevision == revision,
      (onSampleRevision?() ?? revision) == revision, waveformDocumentID?.isEmpty == false,
      !settingsIdentity.isEmpty, catalogue.contains(where: { $0.index == index && $0.identity == settingsIdentity }) else {
      snapStatus.stringValue = "The sample changed or is unavailable. Reload before snapping."; return
    }
    if target != .selection {
      guard !loopNeedsReload, loopDraftRevision == revision else { snapStatus.stringValue = "Reload the retained loop fields before snapping."; return }
      for (first, last, enabled, ping, reverse) in [(loopStart, loopEnd, looping, pingpong, loopReverse),
        (sustainStart, sustainEnd, sustaining, sustainPingpong, sustainReverse)] {
        guard let start = Int(snapText(first)), let end = Int(snapText(last)), start >= 0, start <= end, end <= waveform.frames,
          enabled.state != .on || start < end, ping.state != .on || reverse.state != .on else {
          snapStatus.stringValue = "Enter valid normal and sustain loop boundaries before snapping."; return
        }
      }
    }
    let original = snapFields(target).map(snapText)
    guard let first = Int(original[0]), let last = Int(original[1]), first >= 0, first <= last, last <= waveform.frames,
      (0...2).contains(snapDirection.indexOfSelectedItem), (0...1).contains(snapMode.indexOfSelectedItem) else {
      snapStatus.stringValue = "Enter boundaries inside the sample and choose a snap mode and direction."; return
    }
    var params: [String: Any] = ["sample": index, "positions": [first, last], "direction": ["nearest", "before", "after"][snapDirection.indexOfSelectedItem]]
    if snapMode.indexOfSelectedItem == 1 {
      guard let step = Int(snapText(snapStep)), (1...268435456).contains(step),
        let origin = Int(snapText(snapOrigin)), (0...waveform.frames).contains(origin) else {
        snapStatus.stringValue = "Enter a positive grid step and an origin inside the sample."; return
      }
      params["mode"] = "grid"; params["step"] = step; params["origin"] = origin
    } else {
      guard let radius = Int(snapText(snapRadius)), (0...65536).contains(radius) else {
        snapStatus.stringValue = "Search radius must be 0…65,536 frames."; return
      }
      params["mode"] = "zero"; params["radius"] = radius; params["channels"] = selectedChannels
    }
    let query = SampleSnapQuery(self, target: target, params: params, positions: [first, last])
    settingsWork?.cancel(); settingsWork = nil; loopSaveWork?.cancel(); loopSaveWork = nil
    snapQuery = query; snapStatus.stringValue = "Finding boundaries…"
    let receive: ([String: Any]) -> Void = { [weak self] response in self?.receiveSnap(response, query: query) }
    if let onSettingsRequest { onSettingsRequest("sample.snap.get", params, query.captured.identity, receive) }
    else { onRequest?("sample.snap.get", params, receive) }
  }
  func snapFields(_ target: SampleSnapTarget) -> [NSTextField] {
    switch target { case .selection: return [selectionStart, selectionEnd]; case .normal: return [loopStart, loopEnd]; case .sustain: return [sustainStart, sustainEnd] }
  }
  private func snapText(_ field: NSTextField) -> String { field.currentEditor()?.string ?? field.stringValue }
  private func snapInteger(_ raw: Any?, maximum: Int) -> Int? {
    guard let number = raw as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
    let value = number.doubleValue
    guard value.isFinite, value >= 0, value <= Double(maximum), value.rounded(.towardZero) == value else { return nil }
    return Int(value)
  }
  func receiveSnap(_ response: [String: Any], query: SampleSnapQuery) {
    guard snapQuery === query else { return }
    func fail(_ message: String) { finishSnap(query, accepted: false, message: message + " · Previous boundaries and fields kept.") }
    guard snapUnchanged(query) else { fail("Sample, boundaries or input changed while snapping"); return }
    guard let result = response["result"] as? [String: Any], result["revision"] as? String == query.captured.revision,
      result["documentId"] as? String == query.captured.documentID, let data = result["data"] as? [String: Any],
      snapInteger(data["sample"], maximum: Int(UInt32.max)) == query.captured.sample,
      data["mode"] as? String == query.params["mode"] as? String, data["direction"] as? String == query.params["direction"] as? String,
      let positions = data["positions"] as? [[String: Any]], positions.count == 2 else {
      fail((response["error"] as? [String: Any])?["message"] as? String ?? "Snap response was incomplete or belonged to another sample"); return
    }
    var boundaries = [Int](), matched = 0
    for (i, point) in positions.enumerated() {
      guard snapInteger(point["before"], maximum: waveform.frames) == query.positions[i],
        let after = snapInteger(point["after"], maximum: waveform.frames), let found = point["matched"] as? NSNumber,
        CFGetTypeID(found) == CFBooleanGetTypeID(), found.boolValue || after == query.positions[i],
        query.params["direction"] as? String != "before" || after <= query.positions[i],
        query.params["direction"] as? String != "after" || after >= query.positions[i] else {
        fail("Snap positions were incomplete or inconsistent with the requested direction"); return
      }
      boundaries.append(after); if found.boolValue { matched += 1 }
    }
    let start = boundaries[0], end = boundaries[1]
    guard start <= end, query.target == .selection || !query.enabled || start < end else {
      fail("Snapping would reverse the boundaries or collapse an enabled loop"); return
    }
    // Geometry replacement is intentional, but pure focus/caret movement is
    // not new input. Preserve the current native responder and its selection.
    for (field, value) in zip(snapFields(query.target), boundaries) {
      let native = field.currentEditor() as? NSTextView, caret = native?.selectedRange(), text = String(value)
      field.stringValue = text
      if let native, let caret {
        native.string = text
        let start = min(caret.location, text.utf16.count)
        native.setSelectedRange(NSRange(location: start, length: min(caret.length, text.utf16.count - start)))
      }
    }
    if query.target == .selection {
      preserveSelectionFields = true; waveform.selection = start...end; preserveSelectionFields = false
    } else {
      loopEditGeneration += 1; syncLoopMarkers(); query.resumeAutomaticEdit = true
    }
    finishSnap(query, accepted: true, message: "Matched \(matched)/2 boundaries · \(start) → \(end)." + (query.target == .selection ? " Selection only." : " Saving loop…"))
  }
}
