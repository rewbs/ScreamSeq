import AppKit
import AVFoundation

extension AppController {
  @objc func showSampleRecorder() {
    guard !busy else { return }
    if let win = sampleRecordingWindow, let view = win.contentView as? SampleRecordingView {
      win.makeKeyAndOrderFront(nil); view.activate(); return
    }
    let view = SampleRecordingView(frame: .zero)
    view.instrument.state = model.instruments.isEmpty ? .off : .on
    view.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    view.onRevision = { [weak self] in
      guard let self, !self.shuttingDown else { return "" }
      return self.model.revisionToken
    }
    view.onDocument = { [weak self] in
      guard let self, !self.shuttingDown else { return nil }
      // This main-owned identity changes for New, Open and recovery restore.
      return String(self.model.revisionToken.split(separator: ":").first ?? "")
    }
    view.establishSetupBaseline()
    view.onAdded = { [weak self] sample, instrument in self?.selectRecordedSample(sample, instrument: instrument) }
    view.onPermission = { reply in
      switch AVCaptureDevice.authorizationStatus(for: .audio) {
      case .authorized: reply(nil)
      case .notDetermined:
        AVCaptureDevice.requestAccess(for: .audio) { allowed in
          DispatchQueue.main.async { reply(allowed ? nil : "Microphone access was denied. Enable ScreamSeq in System Settings → Privacy & Security → Microphone, then Record again.") }
        }
      default: reply("Microphone access is unavailable. Enable ScreamSeq in System Settings → Privacy & Security → Microphone, then Record again.")
      }
    }
    let win = SampleRecordingWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 600), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Record sample"; win.contentMinSize = NSSize(width: 610, height: 580)
    win.isReleasedWhenClosed = false; win.contentView = view; win.delegate = view
    win.autorecalculatesKeyViewLoop = false; win.initialFirstResponder = view.name
    sampleRecordingWindow = win; win.center(); win.makeKeyAndOrderFront(nil); view.activate()
  }
  func selectRecordedSample(_ sample: Int, instrument: Int) {
    // Completion may arrive while the musician edits another asset. Creating
    // audio does not authorize replacing that editor's retained raw input.
    guard !sampleEditor.hasDraft, instrument == 0 || !instrumentEditor.hasDraft else {
      statusLabel.stringValue = "Created sample \(sample). Finish the current asset edit before selecting it."
      return
    }
    guard model.samples.contains(where: { $0["index"] as? Int == sample }),
      instrument == 0 || model.instruments.contains(where: { $0["index"] as? Int == instrument }) else { return }
    sampleEditor.index = sample
    if instrument > 0 { instrumentEditor.index = instrument; patternView.instrument = instrument }
    else if model.instruments.isEmpty { patternView.instrument = sample }
    showEditor(1); refreshAssets()
  }
  @objc func recordSelectionToSample() { renderPatternSelection(createInstrument: false) }
  @objc func recordSelectionToInstrument() { renderPatternSelection(createInstrument: true) }
  func currentPatternSampleTarget() -> PatternSampleTarget? {
    guard !busy, !shuttingDown, model.editable, !model.revisionToken.isEmpty else { return nil }
    let selection = patternView.automationSelection
    let context = SampleRecordingContext(document: String(model.revisionToken.split(separator: ":").first ?? ""), revision: model.revisionToken)
    return PatternSampleTarget(context: context, pattern: model.pattern,
      firstRow: selection["startRow"] ?? patternView.cursorRow, lastRow: selection["endRow"] ?? patternView.cursorRow,
      firstChannel: selection["startChannel"] ?? patternView.cursorChannel, lastChannel: selection["endChannel"] ?? patternView.cursorChannel)
  }
  func selectionRenderer(direct: Bool) -> PatternSampleRenderView {
    if let view = direct ? directSampleRender : patternSampleRender { return view }
    let view = PatternSampleRenderView(direct: direct)
    if direct { directSampleRender = view } else { patternSampleRender = view }
    view.onTarget = { [weak self] in self?.currentPatternSampleTarget() }
    view.onRequest = { [weak self] method, params, reply in
      guard let self else { return }
      self.handleAutomation(method, params: params) { [weak self] response in
        // Reconcile the main-owned catalogue before testing an observation's
        // context. This read cannot change a render's captured target or draft.
        if method == "document.get", response["error"] == nil { self?.refreshAll() }
        reply(response)
      }
    }
    view.onAdded = { [weak self] sample, instrument in self?.selectRecordedSample(sample, instrument: instrument) }
    view.onStatus = { [weak self] message in
      guard let self else { return }
      self.statusLabel.stringValue = message
      // The other entry point can become available after this owner's callback,
      // later than the model refresh that preceded the callback.
      (direct ? self.patternSampleRender : self.directSampleRender)?.updateControls()
    }
    view.otherHasUnresolvedResult = { [weak self] in
      guard let self else { return false }
      return (direct ? self.patternSampleRender : self.directSampleRender)?.hasUnresolvedResult == true
    }
    view.onReviewNeeded = { [weak self, weak view] in
      guard direct, let self, let view else { return }
      self.showSelectionRenderer(view)
    }
    return view
  }
  func showSelectionRenderer(_ view: PatternSampleRenderView) {
    let retainedWindow = view.direct ? directSampleRenderWindow : patternSampleRenderWindow
    let win: NSWindow
    if let retainedWindow { win = retainedWindow }
    else {
      win = PatternSampleRenderWindow(contentRect: NSRect(x: 0, y: 0, width: 690, height: 610),
        styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
      win.title = view.direct ? "Selection render result" : "Render pattern selection"
      win.contentMinSize = NSSize(width: 650, height: 580); win.isReleasedWhenClosed = false
      win.contentView = view; win.delegate = view; win.autorecalculatesKeyViewLoop = false
      win.initialFirstResponder = view.direct ? view.render : view.name
      if view.direct { directSampleRenderWindow = win } else { patternSampleRenderWindow = win }
      win.center()
    }
    view.updateControls(); win.makeKeyAndOrderFront(nil)
  }
  @objc func showSelectionRenderOptions() {
    guard !busy, model.editable else { return }
    if let view = directSampleRender, view.hasUnresolvedResult { showSelectionRenderer(view); return }
    let view = selectionRenderer(direct: false)
    view.open(instrument: !model.instruments.isEmpty); showSelectionRenderer(view)
  }
  func renderPatternSelection(createInstrument: Bool) {
    guard !busy, model.editable else { return }
    if let view = patternSampleRender, view.hasUnresolvedResult {
      showSelectionRenderer(view)
      statusLabel.stringValue = "Review the retained Render options result before rendering another selection."; return
    }
    selectionRenderer(direct: true).runDirect(instrument: createInstrument)
  }
}
