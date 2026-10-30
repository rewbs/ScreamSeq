import AppKit

struct SampleViewRegion: Equatable {
  let frames: Int, viewport: Range<Int>?, selection: ClosedRange<Int>?
  init(frames: Int, viewport: Range<Int>? = nil, selection: ClosedRange<Int>? = nil) {
    self.frames = frames; self.viewport = viewport; self.selection = selection
  }
  init(_ editor: SampleEditor) {
    self.init(frames: editor.waveform.frames, viewport: editor.waveform.viewport, selection: editor.waveform.selection)
  }
  func clamped(to count: Int) -> SampleViewRegion {
    var view: Range<Int>?, selected: ClosedRange<Int>?
    if let viewport, count > 0 {
      let end = min(count, viewport.upperBound == frames ? count : viewport.upperBound)
      let start = min(max(0, viewport.lowerBound), max(0, end - 1))
      if start != 0 || end != count { view = start..<end }
    }
    if let selection {
      let end = min(count, selection.upperBound == frames ? count : selection.upperBound)
      selected = min(max(0, selection.lowerBound), end)...end
    }
    return SampleViewRegion(frames: count, viewport: view, selection: selected)
  }
  var range: Range<Int> { viewport ?? 0..<frames }
}

final class SampleSelectionQuery {
  enum Kind { case select, reload, refresh }
  let kind: Kind, sample: Int, originalSample: Int, generation: Int, settingsGeneration: Int, loopGeneration: Int
  let identity: String, originalIdentity: String, revision: String?, documentID: String?, channels: String, originalChannels: String
  let originalRegion: SampleViewRegion, region: SampleViewRegion
  let fields: [String: String], options: [Int], stroke: [Int: Double], selectInput: Bool
  weak var responder: NSResponder?
  let focusedField: Int?, caret: NSRange?
  init(_ editor: SampleEditor, sample: Int, identity: String, kind: Kind, selectInput: Bool) {
    self.kind = kind; self.sample = sample; self.identity = identity; self.selectInput = selectInput
    originalSample = editor.index; originalIdentity = editor.settingsIdentity; generation = editor.sampleGeneration
    settingsGeneration = editor.settingsEditGeneration; loopGeneration = editor.loopEditGeneration
    revision = editor.onSampleRevision?() ?? editor.sampleRevision; documentID = editor.waveformDocumentID
    originalRegion = SampleViewRegion(editor)
    region = identity == originalIdentity ? originalRegion : editor.sampleViewStates[identity] ?? SampleViewRegion(frames: 0)
    originalChannels = editor.waveformChannels; channels = identity == originalIdentity ? originalChannels : "both"
    fields = editor.drawingFields; options = editor.sampleSelectionOptions; stroke = editor.waveform.stroke
    responder = editor.window?.firstResponder; focusedField = editor.focusedSampleField
    caret = focusedField.flatMap { (editor.sampleEditingFields[$0].currentEditor() as? NSTextView)?.selectedRange() }
  }
}

struct SampleSelectionRead {
  let info: [String: Any], revision: String, documentID: String?, region: SampleViewRegion, channels: String, peaks: [Float]
  var range: Range<Int> { region.range }
}

extension SampleEditor {
  // Only successful document adoption authorizes retiring the old presentation.
  // A failed read within the same document still keeps its fields and waveform.
  func adoptDocument(samples: [[String: Any]]) {
    settingsPending = true // Suppress a focus-end settings submission.
    if sampleEditingFields.contains(where: { $0.currentEditor() != nil }) ||
      (window?.firstResponder as? NSView)?.isDescendant(of: self) == true {
      window?.makeFirstResponder(self)
    }
    resetDocumentContext()
    index = samples.first?["index"] as? Int ?? 1
    updateCatalogue(samples)
    name.stringValue = ""; rate.stringValue = "44100"; volume.stringValue = "64"; pan.stringValue = "128"
    for field in [loopStart, loopEnd, sustainStart, sustainEnd] { field.stringValue = "0" }
    for control in [looping, pingpong, loopReverse, sustaining, sustainPingpong, sustainReverse] { control.state = .off }
    waveform.frames = 0; waveform.peaks = []; waveform.peaksRange = nil
    waveform.loopStart = 0; waveform.loopEnd = 0; waveform.sustainStart = 0; waveform.sustainEnd = 0
    waveformChannels = "both"; channelPicker.selectItem(at: 0)
    waveform.selection = nil; selectionChanged(nil); loopFieldSelections = [:]
    previewSignature = nil; previewRevision = nil; resetPastePreview()
    crossfadePreviewSignature = nil; crossfadePreviewRevision = nil
    details.stringValue = ""; processingStatus.stringValue = "Preview shows the exact effect of an edit before applying it."
    settingsStatus.stringValue = "Changes save automatically · ⌘Z to undo"
    loopsStatus.stringValue = "Sustain loops repeat while a note is held; normal loops follow release."
    snapStatus.stringValue = "Snap adjusts boundaries; it does not change audio."
    drawingStatus.stringValue = ""; updateViewportStatus(); waveform.needsDisplay = true
    emptySample.stringValue = samples.isEmpty ? "No sample selected · Browse or record audio to begin." : "Select a sample to edit."
    showSamplePresentation(false); onSamplePresentation?()
  }
  var sampleSelectionOptions: [Int] {
    [operationPicker, curvePicker, crossfadeLoop, crossfadeMode, crossfadeCurve, pasteMode, pasteRate, snapMode, snapDirection].map(\.indexOfSelectedItem) +
      [snapAutomatically.state.rawValue, drawToggle.state.rawValue]
  }
  var focusedSampleField: Int? { sampleEditingFields.firstIndex { $0.currentEditor() != nil } }
  var hasSelectionInput: Bool {
    selectionStart.currentEditor() != nil || selectionEnd.currentEditor() != nil ||
      selectionStart.stringValue != String(waveform.selection?.lowerBound ?? 0) ||
      selectionEnd.stringValue != String(waveform.selection?.upperBound ?? waveform.frames)
  }
  var sampleOperationPending: Bool { settingsPending || loopsBusy || drawingBusy || processing || crossfadeBusy || clipboardBusy || snapBusy }
  func cancelSampleSelection() { sampleSelectionQuery = nil }
  func requestSampleSelection(_ sample: Int, selectInput: Bool = false) {
    guard !hasDraft else { drawingStatus.stringValue = "Finish the sample edit before selecting another sample."; return }
    // Standalone views without a session retain their legacy callback contract.
    guard onSettingsRequest != nil else { index = sample; onSelect?(sample); return }
    startSampleSelection(sample, kind: .select, selectInput: selectInput)
  }
  func reloadSample() {
    guard sampleOperation == nil else { operationStatus.stringValue = "Review the retained operation before reloading."; return }
    guard drawingDraft?.submitted != true else { drawingStatus.stringValue = "Review or discard the retained stroke before reloading."; return }
    guard !sampleOperationPending, !hasSampleComposition else { return }
    settingsWork?.cancel(); settingsWork = nil; loopSaveWork?.cancel(); loopSaveWork = nil
    startSampleSelection(index, kind: .reload)
  }
  func refreshSample(samples: [[String: Any]]) {
    updateCatalogue(samples)
    if catalogue.isEmpty, !hasSamplePresentation { return }
    guard !protectsSampleOperation, sampleSelectionQuery == nil, !sampleOperationPending, drawingDraft?.submitted != true, drawingDraft?.numeric != true, !hasSampleComposition else { return }
    let revision = onSampleRevision?() ?? sampleRevision
    if !waveform.stroke.isEmpty {
      guard revision != sampleRevision else { return }
      // A newer song revision retires an unsubmitted gesture only after its
      // replacement metadata and waveform are both ready to adopt.
    }
    if waveformQuery != nil, revision == sampleRevision { return }
    startSampleSelection(index, kind: .refresh)
  }
  func startSampleSelection(_ sample: Int, kind: SampleSelectionQuery.Kind, selectInput: Bool = false) {
    guard !sampleOperationPending, !waveform.draggingLoop, !hasSampleComposition, let target = catalogue.first(where: { $0.index == sample }), !target.identity.isEmpty,
      onSettingsRequest != nil else { drawingStatus.stringValue = "Sample unavailable · Previous sample and fields kept."; return }
    if kind != .select, !settingsIdentity.isEmpty, target.identity != settingsIdentity,
      kind == .reload || hasDraft || hasSelectionInput || focusedSampleField != nil {
      drawingStatus.stringValue = "The captured sample is unavailable · Previous sample and fields kept."; return
    }
    cancelWaveformQuery()
    sampleSelectionQuery = SampleSelectionQuery(self, sample: sample, identity: target.identity, kind: kind, selectInput: selectInput)
    drawingStatus.stringValue = "Loading sample…"; pumpSampleSelection()
  }
  func sampleSelectionUnchanged(_ query: SampleSelectionQuery) -> Bool {
    index == query.originalSample && sampleGeneration == query.generation && settingsIdentity == query.originalIdentity &&
      (onSampleRevision?() ?? sampleRevision) == query.revision &&
      catalogue.contains(where: { $0.index == query.sample && $0.identity == query.identity }) &&
      settingsEditGeneration == query.settingsGeneration && loopEditGeneration == query.loopGeneration && drawingFields == query.fields && sampleSelectionOptions == query.options &&
      SampleViewRegion(self) == query.originalRegion && waveformChannels == query.originalChannels &&
      waveform.stroke == query.stroke && !sampleOperationPending && !hasSampleComposition &&
      window?.firstResponder === query.responder && focusedSampleField == query.focusedField &&
      focusedSampleField.flatMap({ (sampleEditingFields[$0].currentEditor() as? NSTextView)?.selectedRange() }) == query.caret
  }
  func failSampleSelection(_ message: String) {
    cancelSampleSelection()
    drawingStatus.stringValue = message + (hasSamplePresentation ? " · Previous sample and fields kept." : " · Reload sample to try again.")
    onSamplePresentation?()
  }
  func continueSampleSelection(_ query: SampleSelectionQuery) -> Bool {
    guard sampleSelectionQuery === query else { sampleSelectionInFlight = false; pumpSampleSelection(); return false }
    guard sampleSelectionUnchanged(query) else {
      sampleSelectionInFlight = false; failSampleSelection("Sample, fields or focus changed while loading"); return false
    }
    return true
  }
  func pumpSampleSelection() {
    guard !sampleSelectionInFlight, let query = sampleSelectionQuery, let onSettingsRequest else { return }
    guard sampleSelectionUnchanged(query) else { failSampleSelection("Sample, fields or focus changed while loading"); return }
    sampleSelectionInFlight = true
    onSettingsRequest("sample.get", ["sample": query.sample], query.identity) { [weak self] response in
      guard let self, self.continueSampleSelection(query) else { return }
      guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
        query.revision == nil || revision == query.revision,
        query.documentID == nil || query.documentID == result["documentId"] as? String,
        let info = result["data"] as? [String: Any], self.validDrawingInfo(info, query.sample), let frames = info["frames"] as? Int else {
        self.sampleSelectionInFlight = false; self.failSampleSelection("Sample metadata read failed or changed"); return
      }
      let region = query.region.clamped(to: frames), range = region.range
      let channels = query.channels == "right" && info["channels"] as? Int == 1 ? "both" : query.channels
      let document = result["documentId"] as? String
      func adopt(_ peaks: [Float]) {
        self.sampleSelectionInFlight = false
        self.adoptSampleSelection(SampleSelectionRead(info: info, revision: revision, documentID: document, region: region, channels: channels, peaks: peaks), query: query)
      }
      if range.isEmpty { adopt([]); return }
      let bins = range.count <= 4096 ? range.count : 2048
      onSettingsRequest("sample.waveform.get", ["sample": query.sample, "start": range.lowerBound, "end": range.upperBound, "channels": channels, "bins": bins], query.identity) { [weak self] response in
        guard let self, self.continueSampleSelection(query) else { return }
        guard let result = response["result"] as? [String: Any], result["revision"] as? String == revision,
          result["documentId"] as? String == document,
          let peaks = self.validWaveformPeaks(result, sample: query.sample, range: range, channels: channels, bins: bins) else {
          self.sampleSelectionInFlight = false; self.failSampleSelection("Sample waveform read failed or changed"); return
        }
        adopt(peaks)
      }
    }
  }
  func adoptSampleSelection(_ read: SampleSelectionRead, query: SampleSelectionQuery) {
    guard sampleSelectionQuery === query, sampleSelectionUnchanged(query) else { failSampleSelection("Sample or fields changed while loading"); return }
    cancelSampleSelection()
    if !query.originalIdentity.isEmpty { sampleViewStates[query.originalIdentity] = query.originalRegion }
    let preserve = query.kind == .refresh && query.identity == settingsIdentity
    if !preserve {
      let regions = sampleViewStates
      resetDocumentContext(); sampleViewStates = regions; index = query.sample
    }
    sampleViewStates[query.identity] = read.region
    preserveSelectionFields = preserve && hasSelectionInput
    update(read.info, samples: catalogue.map { ["index": $0.index, "id": $0.identity, "name": $0.name] }, revision: read.revision, loaded: read)
    preserveSelectionFields = false
    drawingStatus.stringValue = "Sample loaded."
    onSamplePresentation?()
    if query.selectInput { onSampleSelected?(index) }
  }
}
