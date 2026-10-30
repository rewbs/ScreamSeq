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
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 600), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
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
  func renderPatternSelection(createInstrument: Bool) {
    guard !busy, model.editable else { return }
    guard patternView.playbackSelection != nil else {
      statusLabel.stringValue = "Select pattern rows and channels first (drag or Shift + arrow keys), then Record selection."
      window.makeKeyAndOrderFront(nil); window.makeFirstResponder(patternView); return
    }
    let selection = patternView.automationSelection
    let first = selection["startRow"] ?? patternView.cursorRow, last = selection["endRow"] ?? first
    let p: [String: Any] = ["pattern": model.pattern, "firstRow": first, "lastRow": last,
      "firstChannel": selection["startChannel"] ?? patternView.cursorChannel,
      "lastChannel": selection["endChannel"] ?? patternView.cursorChannel,
      "name": "Pattern \(model.pattern) rows \(first)–\(last)", "createInstrument": createInstrument,
      "expectedRevision": session.automationRevision]
    statusLabel.stringValue = "Recording selected rows and channels with their effects…"
    handleAutomation("sample.renderSelection", params: p) { [weak self] response in
      guard let self else { return }
      if let error = response["error"] as? [String: Any] {
        self.statusLabel.stringValue = error["message"] as? String ?? "Selection recording failed; the original pattern is unchanged."
        return
      }
      guard let data = (response["result"] as? [String: Any])?["data"] as? [String: Any], let sample = data["sample"] as? Int else { return }
      self.selectRecordedSample(sample, instrument: data["instrument"] as? Int ?? 0)
      self.statusLabel.stringValue = "Recorded rows \(first)–\(last) to sample \(sample), including channel effects · ⌘Z undoes it."
    }
  }
}
