import AppKit

// One native owner survives the write and all of its readback. A failed read
// never grants permission to repeat an admitted musical operation.
final class SampleOperation {
  let method: String, sample: Int, identity: String, revision: String, documentID: String, generation: Int
  let dryRun: Bool, signature: NSDictionary, fields: [String: String], options: [Int]
  let settingsGeneration: Int, loopGeneration: Int, region: SampleViewRegion, channels: String
  var params: [String: Any]
  var busy = true, submitted = false, retryable = false
  var acceptedRevision: String?, result: [String: Any]?, reviewed: SampleOperationRead?
  init(_ editor: SampleEditor, method: String, params: [String: Any], dryRun: Bool, revision: String, documentID: String) {
    self.method = method; self.params = params; self.dryRun = dryRun; self.revision = revision; self.documentID = documentID
    signature = params as NSDictionary; sample = editor.index; identity = editor.settingsIdentity; generation = editor.sampleGeneration
    fields = editor.drawingFields; options = editor.sampleSelectionOptions
    settingsGeneration = editor.settingsEditGeneration; loopGeneration = editor.loopEditGeneration
    region = SampleViewRegion(editor); channels = editor.waveformChannels
    self.params["expectedRevision"] = revision
    if method != "sample.clipboard.copy" { self.params["dryRun"] = dryRun }
  }
  var usesClipboard: Bool { ["sample.clipboard.copy", "sample.cut", "sample.paste"].contains(method) }
}

struct SampleOperationRead {
  let revision: String, samples: [[String: Any]], sample: Int?, identity: String?, view: SampleSelectionRead?, clipboard: NSDictionary?
}

extension SampleEditor {
  var protectsSampleOperation: Bool { sampleOperation != nil || operationNeedsReload }
  var processing: Bool { sampleOperation?.method == "sample.process" && sampleOperation?.busy == true }
  var crossfadeBusy: Bool { sampleOperation?.method == "sample.crossfade" && sampleOperation?.busy == true }
  var clipboardBusy: Bool { sampleOperation?.busy == true && !processing && !crossfadeBusy }
  func makeOperationActions() -> NSView {
    operationStatus.lineBreakMode = .byWordWrapping; operationStatus.maximumNumberOfLines = 4
    operationStatus.preferredMaxLayoutWidth = 600; operationStatus.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    return stack(.horizontal, [retryOperationButton, reviewOperationButton, acceptOperationButton, NSView()], spacing: 8)
  }
  func updateOperationActions() {
    let busy = sampleOperation?.busy == true, retained = sampleOperation != nil
    let recovery = [retryOperationButton, reviewOperationButton, acceptOperationButton]
    if !retained, recovery.contains(where: { window?.firstResponder === $0 }) { window?.makeFirstResponder(self) }
    recovery.forEach { $0.isHidden = !retained }
    func enable(_ button: NSButton, _ permitted: Bool) {
      // Disabling the focused AppKit button resigns first responder. The owned
      // action guards reject duplicate activation while its focus stays put.
      button.isEnabled = permitted || window?.firstResponder === button
    }
    enable(retryOperationButton, !busy && sampleOperation?.retryable == true)
    enable(reviewOperationButton, !busy && sampleOperation?.submitted == true)
    enable(acceptOperationButton, !busy && sampleOperation?.reviewed != nil)
    enable(previewButton, !protectsSampleOperation); enable(processButton, !protectsSampleOperation)
    enable(crossfadePreviewButton, !protectsSampleOperation); enable(crossfadeApplyButton, !protectsSampleOperation)
  }
  func operationMessage(_ method: String, _ text: String) {
    operationStatus.stringValue = text
    if method == "sample.process" { processingStatus.stringValue = text }
    else if method == "sample.crossfade" { crossfadeStatus.stringValue = text }
    else { clipboardStatus.stringValue = text }
  }
  func finishOperationWaiters(_ accepted: Bool) {
    let waiting = operationWaiters; operationWaiters = []; waiting.forEach { $0(accepted) }
  }
  func resetOperationContext() {
    sampleOperation = nil; operationNeedsReload = false; operationStatus.stringValue = ""
    updateOperationActions(); finishOperationWaiters(false)
  }
  func ownsOperation(_ operation: SampleOperation) -> Bool {
    sampleOperation === operation && index == operation.sample && sampleGeneration == operation.generation && settingsIdentity == operation.identity
  }
  func operationFieldsUnchanged(_ operation: SampleOperation) -> Bool {
    drawingFields == operation.fields && sampleSelectionOptions == operation.options &&
      settingsEditGeneration == operation.settingsGeneration && loopEditGeneration == operation.loopGeneration &&
      SampleViewRegion(self) == operation.region && waveformChannels == operation.channels && !hasSampleComposition
  }
  func beginOperation(_ method: String, params: [String: Any], dryRun: Bool = false, previewRevision: String? = nil) -> SampleOperation? {
    let mayKeepLoops = dryRun || method == "sample.clipboard.copy"
    guard !protectsSampleOperation, !settingsDraft.hasDraft, !settingsNeedsReload, !loopNeedsReload,
      mayKeepLoops || !hasLoopDraft, !protectsDrawingInput, waveform.stroke.isEmpty,
      !sampleOperationPending, sampleSelectionQuery == nil, waveformQuery == nil, !waveformInFlight,
      !waveform.draggingLoop, !hasSampleComposition, canBeginDrawing?() ?? true else {
      operationMessage(method, "Finish, review or reload the retained sample edit before continuing."); return nil
    }
    guard let revision = sampleRevision, !revision.isEmpty, let document = waveformDocumentID, !document.isEmpty,
      !settingsIdentity.isEmpty, catalogue.contains(where: { $0.index == index && $0.identity == settingsIdentity }),
      (onSampleRevision?() ?? revision) == revision, previewRevision == nil || previewRevision == revision else {
      operationMessage(method, "The sample or preview belongs to an earlier revision. Reload before applying."); return nil
    }
    let operation = SampleOperation(self, method: method, params: params, dryRun: dryRun, revision: revision, documentID: document)
    sampleOperation = operation; updateOperationActions(); operationMessage(method, dryRun ? "Calculating preview…" : "Applying sample operation…")
    return operation
  }
  func requestOperation(_ operation: SampleOperation, _ method: String, _ params: [String: Any], identity: String? = nil, _ reply: @escaping ([String: Any]) -> Void) {
    guard ownsOperation(operation) else { return }
    let receive: ([String: Any]) -> Void = { [weak self] response in
      guard let self, self.ownsOperation(operation) else { return }; reply(response)
    }
    if params["sample"] != nil, let onSettingsRequest { onSettingsRequest(method, params, identity ?? operation.identity, receive) }
    else if let onRequest { onRequest(method, params, receive) }
    else { receive(["error": ["message": "Sample requests are unavailable."]]) }
  }
  func clearOperationPreview(_ operation: SampleOperation) {
    if operation.method == "sample.process" { previewSignature = nil; previewRevision = nil }
    else if operation.method == "sample.crossfade" { crossfadePreviewSignature = nil; crossfadePreviewRevision = nil }
    else { resetPastePreview() }
  }
  func retainOperation(_ operation: SampleOperation, _ message: String) {
    guard ownsOperation(operation) else { return }
    clearOperationPreview(operation); operation.busy = false; operation.reviewed = nil
    if operation.dryRun || !operation.submitted { sampleOperation = nil }
    updateOperationActions()
    operationMessage(operation.method, message + (sampleOperation == nil ? "" : operation.retryable ? " · Retained. Retry uses the original payload and revision." : " · Retained. Review the result; no write will be repeated."))
    finishOperationWaiters(false)
  }
  func submitOperation(_ operation: SampleOperation) {
    guard ownsOperation(operation), operationFieldsUnchanged(operation) else { retainOperation(operation, "Newer sample input kept"); return }
    operation.submitted = true; operation.busy = true; operation.retryable = false; operation.reviewed = nil; updateOperationActions()
    requestOperation(operation, operation.method, operation.params) { [weak self] response in
      guard let self else { return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String, !revision.isEmpty,
        result["documentId"] as? String == operation.documentID, let data = result["data"] as? [String: Any] else {
        operation.retryable = !operation.dryRun && NativeRequestOutcome.canRetryWrite(response)
        self.retainOperation(operation, (response["error"] as? [String: Any])?["message"] as? String ?? "Sample operation outcome is uncertain"); return
      }
      operation.result = data; operation.acceptedRevision = revision
      guard self.validOperationReport(operation, data), self.operationFieldsUnchanged(operation),
        (self.onSampleRevision?() ?? revision) == revision else { self.retainOperation(operation, "Result could not be adopted or newer input arrived"); return }
      if operation.dryRun {
        guard revision == operation.revision else { self.retainOperation(operation, "Song changed during preview"); return }
        self.sampleOperation = nil
        if operation.method == "sample.process" { self.previewSignature = operation.signature; self.previewRevision = revision }
        else if operation.method == "sample.crossfade" { self.crossfadePreviewSignature = operation.signature; self.crossfadePreviewRevision = revision }
        else if operation.method == "sample.paste" { self.pastePreviewSignature = operation.signature; self.pastePreviewRevision = revision; self.pastePreviewID = operation.params["clipboardId"] as? String }
        self.updateOperationActions(); self.operationMessage(operation.method, self.operationReport(operation)); self.finishOperationWaiters(true); return
      }
      self.readOperation(operation, revision: revision) { [weak self] read in
        guard let self else { return }
        guard let read, self.operationFieldsUnchanged(operation), (self.onSampleRevision?() ?? read.revision) == read.revision,
          read.view != nil else { self.retainOperation(operation, "Sample readback failed, changed or newer input arrived"); return }
        self.adoptOperation(read, operation: operation)
      }
    }
  }
  func retryOperation() {
    guard let operation = sampleOperation, !operation.busy, operation.retryable else { return }
    guard operationFieldsUnchanged(operation) else { retainOperation(operation, "Newer sample input kept"); return }
    submitOperation(operation)
  }
  func operationResult(_ response: [String: Any], _ operation: SampleOperation, revision: String?) -> [String: Any]? {
    guard let result = response["result"] as? [String: Any], let current = result["revision"] as? String, !current.isEmpty,
      result["documentId"] as? String == operation.documentID, revision == nil || current == revision else { return nil }
    return result
  }
  func validOperationClipboard(_ value: [String: Any]) -> Bool {
    guard let available = value["available"] as? Bool else { return false }
    return !available || ((value["clipboardId"] as? String)?.isEmpty == false &&
      (value["frames"] as? Int).map { $0 > 0 } == true && [1, 2].contains(value["channels"] as? Int ?? 0) && (value["rate"] as? Int).map { $0 > 0 } == true)
  }
  func readOperation(_ operation: SampleOperation, revision: String?, _ done: @escaping (SampleOperationRead?) -> Void) {
    requestOperation(operation, "document.get", [:]) { [weak self] response in
      guard let self else { return }
      guard let result = self.operationResult(response, operation, revision: revision), let current = result["revision"] as? String,
        let data = result["data"] as? [String: Any], let samples = data["samples"] as? [[String: Any]],
        samples.allSatisfy({ ($0["id"] as? String)?.isEmpty == false && ($0["index"] as? Int).map { $0 > 0 } == true }),
        Set(samples.compactMap { $0["id"] as? String }).count == samples.count,
        Set(samples.compactMap { $0["index"] as? Int }).count == samples.count else { done(nil); return }
      let copied = operation.method == "sample.copyToNew" && (operation.result?["id"] as? String)?.isEmpty == false && (operation.result?["sample"] as? Int).map { $0 > 0 } == true
      let identity = copied ? operation.result?["id"] as? String : operation.identity
      let target = samples.first { $0["id"] as? String == identity }
      let sample = target?["index"] as? Int
      // Only an explicit review may inspect an absent target. A successful
      // completion must still find the exact slot/identity from its receipt.
      if revision != nil && (sample == nil || sample != (copied ? operation.result?["sample"] as? Int : operation.sample)) { done(nil); return }
      func finish(_ view: SampleSelectionRead?, _ clipboard: NSDictionary?) {
        done(SampleOperationRead(revision: current, samples: samples, sample: sample, identity: identity, view: view, clipboard: clipboard))
      }
      func clipboard(_ view: SampleSelectionRead?) {
        guard operation.usesClipboard else { finish(view, nil); return }
        self.requestOperation(operation, "sample.clipboard.get", [:]) { response in
          guard let result = self.operationResult(response, operation, revision: current), let data = result["data"] as? [String: Any], self.validOperationClipboard(data) else { done(nil); return }
          if revision != nil, let expected = operation.result?["clipboardId"] as? String, data["clipboardId"] as? String != expected { done(nil); return }
          finish(view, data as NSDictionary)
        }
      }
      guard let sample, let identity else { clipboard(nil); return }
      self.requestOperation(operation, "sample.get", ["sample": sample], identity: identity) { response in
        guard let result = self.operationResult(response, operation, revision: current), let info = result["data"] as? [String: Any], self.validDrawingInfo(info, sample), let frames = info["frames"] as? Int else { done(nil); return }
        var region = (copied ? SampleViewRegion(frames: 0) : operation.region).clamped(to: frames)
        if revision != nil {
          if ["sample.process", "sample.clipboard.copy"].contains(operation.method),
            let first = operation.params["start"] as? Int, let last = operation.params["end"] as? Int {
            let selection = operation.params["operation"] as? String == "trim" ? 0...frames : min(first, frames)...min(last, frames)
            region = SampleViewRegion(frames: frames, viewport: region.viewport, selection: selection)
          } else if ["sample.cut", "sample.delete"].contains(operation.method), let first = operation.params["start"] as? Int, first <= frames {
            region = SampleViewRegion(frames: frames, viewport: region.viewport, selection: first...first)
          } else if operation.method == "sample.paste", let first = operation.result?["start"] as? Int, let count = operation.result?["insertedFrames"] as? Int,
            first >= 0, count >= 0, first <= frames, count <= frames - first {
            region = SampleViewRegion(frames: frames, viewport: region.viewport, selection: first...(first + count))
          }
        }
        let channels = copied || operation.channels == "right" && info["channels"] as? Int == 1 ? "both" : operation.channels
        let range = region.range
        func wave(_ peaks: [Float]) { clipboard(SampleSelectionRead(info: info, revision: current, documentID: operation.documentID, region: region, channels: channels, peaks: peaks)) }
        if range.isEmpty { wave([]); return }
        let bins = range.count <= 4096 ? range.count : 2048
        self.requestOperation(operation, "sample.waveform.get", ["sample": sample, "start": range.lowerBound, "end": range.upperBound, "channels": channels, "bins": bins], identity: identity) { response in
          guard let result = self.operationResult(response, operation, revision: current), let peaks = self.validWaveformPeaks(result, sample: sample, range: range, channels: channels, bins: bins) else { done(nil); return }
          wave(peaks)
        }
      }
    }
  }
  func reviewOperation() {
    guard let operation = sampleOperation, !operation.busy, operation.submitted else { return }
    operation.busy = true; operation.reviewed = nil; updateOperationActions()
    readOperation(operation, revision: nil) { [weak self] read in
      guard let self else { return }
      operation.busy = false; operation.reviewed = read; self.updateOperationActions(); self.finishOperationWaiters(false)
      self.operationMessage(operation.method, read == nil ? "Review failed. Operation and fields kept." :
        (operation.acceptedRevision == nil ? "Earlier outcome remains unverified. " : "The write returned a receipt. ") +
        "Current state inspected. Accept acknowledges without repeating the write; Reload explicitly to adopt audio.")
    }
  }
  func acceptOperationReview() {
    guard let operation = sampleOperation, !operation.busy, let reviewed = operation.reviewed else { return }
    operation.busy = true; updateOperationActions()
    // Re-read every affected domain: clipboard changes need not bump the song revision.
    readOperation(operation, revision: nil) { [weak self] read in
      guard let self else { return }
      guard let read, read.revision == reviewed.revision, read.sample == reviewed.sample, read.identity == reviewed.identity,
        read.clipboard == reviewed.clipboard, (self.onSampleRevision?() ?? read.revision) == read.revision else {
        self.retainOperation(operation, "State changed after review. Review again"); return
      }
      self.sampleOperation = nil; self.operationNeedsReload = true; self.clearOperationPreview(operation); self.updateOperationActions()
      self.operationMessage(operation.method, "Accepted the inspected state. No write repeated. Fields kept; Reload sample to adopt current audio.")
      self.finishOperationWaiters(false)
    }
  }
  func adoptOperation(_ read: SampleOperationRead, operation: SampleOperation) {
    guard let view = read.view, let sample = read.sample else { retainOperation(operation, "Captured sample unavailable"); return }
    // The caller has verified every raw field and edit generation. Focus and
    // caret movement are independent: preserve the field editor active now,
    // including when accepted metadata legitimately changes its displayed value.
    let field = focusedSampleField.map { sampleEditingFields[$0] }
    let text = field?.currentEditor() as? NSTextView, caret = text?.selectedRange()
    let waiters = operationWaiters; operationWaiters = []
    sampleOperation = nil; clearOperationPreview(operation); updateCatalogue(read.samples)
    if sample != index || read.identity != settingsIdentity {
      // Copy-to-new has a verified destination identity, metadata and waveform.
      resetDocumentContext(); index = sample
    }
    preserveSelectionFields = sample == operation.sample && view.region.selection == operation.region.selection
    update(view.info, samples: read.samples, revision: read.revision, loaded: view, acceptFocusedSettings: true)
    preserveSelectionFields = false; updateOperationActions()
    if let field, let text, let caret, field.currentEditor() === text, window?.firstResponder === text {
      if text.string != field.stringValue { text.string = field.stringValue }
      let location = min(caret.location, text.string.utf16.count)
      text.setSelectedRange(NSRange(location: location, length: min(caret.length, text.string.utf16.count - location)))
    }
    operationMessage(operation.method, operationReport(operation)); onSamplePresentation?()
    if sample != operation.sample { onSampleSelected?(sample) }
    waiters.forEach { $0(true) }
  }
  func validOperationReport(_ operation: SampleOperation, _ data: [String: Any]) -> Bool {
    func count(_ key: String) -> Bool { (data[key] as? Int).map { $0 >= 0 } == true }
    if operation.method == "sample.clipboard.copy" { return validOperationClipboard(data) && data["available"] as? Bool == true }
    if operation.method == "sample.copyToNew" { return (data["sample"] as? Int).map { $0 > 0 } == true && (data["id"] as? String)?.isEmpty == false && count("frames") }
    guard data["sample"] as? Int == operation.sample else { return false }
    if operation.method == "sample.crossfade" {
      return count("changedFrames") && (data["loopBefore"] as? [String: Any])?["frames"] is NSNumber && (data["loopAfter"] as? [String: Any])?["frames"] is NSNumber
    }
    if operation.method == "sample.process" { return count("changedFrames") || count("removedFrames") && count("resultFrames") }
    return count("removedFrames") && count("insertedFrames") && count("resultFrames") && count("start")
  }
  func operationReport(_ operation: SampleOperation) -> String {
    let data = operation.result ?? [:], prefix = operation.dryRun ? "Preview" : "Applied", clipped = data["clippedSamples"] as? Int ?? 0
    let clipping = clipped > 0 ? " · \(clipped) clipped values" : ""
    switch operation.method {
    case "sample.crossfade":
      let before = data["loopBefore"] as? [String: Any] ?? [:], after = data["loopAfter"] as? [String: Any] ?? [:]
      return "\(prefix): loop \(before["frames"] ?? 0) → \(after["frames"] ?? 0) frames; \(data["changedFrames"] ?? 0) audio frames change." + clipping
    case "sample.clipboard.copy": return "Copied \(data["frames"] ?? 0) frames · \(data["channels"] ?? 0) channel(s) · \(data["rate"] ?? 0) Hz"
    case "sample.copyToNew": return "Copied \(data["frames"] ?? 0) frames to sample \(data["sample"] ?? 0)."
    case "sample.cut", "sample.delete": return "\(operation.method == "sample.cut" ? "Cut" : "Deleted") \(data["removedFrames"] ?? 0) frames. Undo restores the audio and loops."
    case "sample.paste": return "\(prefix): \(data["insertedFrames"] ?? 0) frames · result \(data["resultFrames"] ?? 0) frames" + clipping
    default:
      if let removed = data["removedFrames"] as? Int { return "\(prefix): remove \(removed) frames; keep \(data["resultFrames"] ?? 0)." }
      let peak = data["peakAfter"] as? Double ?? 0, peakText = peak > 0 ? String(format: "%.2f dBFS", 20 * log10(peak)) : "silence"
      return "\(prefix): \(data["changedFrames"] ?? 0) frames change · peak \(peakText)" + clipping
    }
  }
}
