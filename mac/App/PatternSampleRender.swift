import AppKit

struct PatternSampleTarget: Equatable {
  let context: SampleRecordingContext
  let pattern: Int, firstRow: Int, lastRow: Int, firstChannel: Int, lastChannel: Int
  var range: [String: Int] {
    ["pattern": pattern, "firstRow": firstRow, "lastRow": lastRow,
      "firstChannel": firstChannel, "lastChannel": lastChannel]
  }
}
struct PatternSampleOptions: Equatable {
  let name: String, tail: String, instrument: Bool
}
final class PatternSampleOperation {
  let target: PatternSampleTarget, options: PatternSampleOptions, generation: UInt64
  let params: [String: Any]
  struct Observation {
    let context: SampleRecordingContext, generation: UInt64, options: PatternSampleOptions
    let assets: [String: Any]
  }
  var observation: Observation?
  init(target: PatternSampleTarget, options: PatternSampleOptions, generation: UInt64, params: [String: Any]) {
    self.target = target; self.options = options; self.generation = generation; self.params = params
  }
}

/// The quick action and the options window each own their captured selection.
/// Neither cursor movement nor a read refresh replaces a retained submission.
final class PatternSampleRenderView: NSView, NSWindowDelegate, NSTextFieldDelegate {
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onTarget: (() -> PatternSampleTarget?)?
  var onAdded: ((Int, Int) -> Void)?
  var onStatus: ((String) -> Void)?
  var onReviewNeeded: (() -> Void)?
  var otherHasUnresolvedResult: (() -> Bool)?
  let direct: Bool
  let name = NSTextField(string: "Pattern selection"), tail = NSTextField(string: "0")
  let instrument = NSButton(checkboxWithTitle: "Create a mapped instrument", target: nil, action: nil)
  let targetLabel = Theme.label("Use current selection to capture the rows and channels to render.", size: 12)
  let status = Theme.label("Check validates the selection. Render adds audio with one Undo.", size: 12)
  lazy var useSelection = ActionButton("Use current selection") { [weak self] in self?.captureSelection() }
  lazy var check = ActionButton("Check") { [weak self] in self?.apply(dryRun: true) }
  lazy var render = ActionButton("Render sample", prominent: true) { [weak self] in
    guard let self else { return }; if self.operation != nil { self.reviewResult() } else { self.apply(dryRun: false) }
  }
  lazy var accept = ActionButton("Accept observed state") { [weak self] in self?.acceptObservation() }
  lazy var discard = ActionButton("Discard options") { [weak self] in self?.discardOptions() }
  lazy var closeButton = ActionButton("Close") { [weak self] in self?.window?.performClose(nil) }
  var target: PatternSampleTarget?, operation: PatternSampleOperation?
  var pending = false, needsCapture = false, opened = false, generation: UInt64 = 0
  var baseline = PatternSampleOptions(name: "Pattern selection", tail: "0", instrument: false)
  var observed = PatternSampleOptions(name: "Pattern selection", tail: "0", instrument: false)
  var raw: PatternSampleOptions {
    PatternSampleOptions(name: (name.currentEditor() as? NSTextView)?.string ?? name.stringValue,
      tail: (tail.currentEditor() as? NSTextView)?.string ?? tail.stringValue, instrument: instrument.state == .on)
  }
  var composing: Bool { [name, tail].contains { ($0.currentEditor() as? NSTextView)?.hasMarkedText() == true } }
  var hasUnresolvedResult: Bool { pending || operation != nil }
  var needsDocumentCompletion: Bool { syncOptions(); return hasUnresolvedResult || raw != baseline || composing }
  var current: Bool { !needsCapture && target != nil && target?.context == onTarget?()?.context }
  init(direct: Bool = false) {
    self.direct = direct
    super.init(frame: .zero)
    wantsLayer = true; layer?.backgroundColor = Theme.panel.cgColor
    name.delegate = self; tail.delegate = self; instrument.target = self; instrument.action = #selector(optionsChanged)
    name.setAccessibilityLabel("Rendered sample name"); tail.setAccessibilityLabel("Render tail in seconds")
    name.isEditable = !direct; tail.isEditable = !direct; instrument.isEnabled = !direct
    let help = Theme.label("The captured rows include complete channels and their effects. Moving the cursor keeps this selection. Song edits require a new capture.", size: 11, color: Theme.muted)
    for label in [targetLabel, status, help] {
      label.lineBreakMode = .byWordWrapping; label.maximumNumberOfLines = 4; label.preferredMaxLayoutWidth = 600
      label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    }
    tail.fixed(width: 120)
    let fields = stack(.horizontal, [labeled("SAMPLE NAME", name), labeled("TAIL (SECONDS)", tail)], spacing: 14)
    let content = stack(.vertical, [Theme.label(direct ? "Selection render result" : "Render pattern selection", size: 18, weight: .semibold),
      targetLabel, useSelection, fields, instrument, help, status, NSView(),
      stack(.horizontal, [check, render, accept, NSView(), closeButton], spacing: 10), discard], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 18)
    useSelection.isHidden = direct; check.isHidden = direct; discard.isHidden = direct
    for index in keyboardOrder.indices { keyboardOrder[index].nextKeyView = keyboardOrder[(index + 1) % keyboardOrder.count] }
    updateControls()
  }
  required init?(coder: NSCoder) { fatalError() }
  func syncOptions() { if raw != observed { observed = raw; generation &+= 1 } }
  @objc func optionsChanged() { syncOptions(); operation?.observation = nil; updateControls() }
  func controlTextDidChange(_ notification: Notification) { optionsChanged() }
  func showStatus(_ message: String) { status.stringValue = message; onStatus?(message) }
  func open(instrument: Bool) {
    if !opened {
      opened = true
      self.instrument.state = instrument ? .on : .off
      baseline = raw; observed = raw; captureSelection()
    }
    updateControls()
  }
  func captureSelection() {
    guard !hasUnresolvedResult, let target = onTarget?() else { return }
    self.target = target; needsCapture = false; generation &+= 1
    targetLabel.stringValue = "Pattern \(target.pattern) · rows \(target.firstRow)–\(target.lastRow) · channels \(target.firstChannel + 1)–\(target.lastChannel + 1)\nSource pattern remains unchanged."
    showStatus("Selection captured. Check validates; Render creates a new sample."); updateControls()
  }
  func runDirect(instrument: Bool) {
    guard direct, !pending else { return }
    if operation != nil { onReviewNeeded?(); reviewResult(); return }
    guard otherHasUnresolvedResult?() != true else { showStatus("Review the retained Render options result before rendering another selection."); return }
    name.stringValue = "Rendered selection"; tail.stringValue = "0"; self.instrument.state = instrument ? .on : .off
    baseline = raw; observed = raw; captureSelection(); apply(dryRun: false)
  }
  func discardOptions() {
    guard !hasUnresolvedResult else { return }
    if name.currentEditor() != nil || tail.currentEditor() != nil { window?.makeFirstResponder(self) }
    name.stringValue = "Pattern selection"; tail.stringValue = "0"; instrument.state = .off
    baseline = raw; observed = raw; generation &+= 1; target = nil; needsCapture = true
    targetLabel.stringValue = "Use current selection to capture the rows and channels to render."
    showStatus("Options discarded. Use current selection before rendering."); updateControls()
  }
  func documentChanged() { updateControls() }
  func updateControls() {
    func enable(_ button: NSButton, _ permitted: Bool) {
      // AppKit otherwise resigns a focused button as the request starts. Each
      // action has its own admission guard, including repeated keyboard input.
      button.isEnabled = permitted || window?.firstResponder === button
    }
    enable(useSelection, !hasUnresolvedResult)
    enable(check, !hasUnresolvedResult && current && otherHasUnresolvedResult?() != true)
    render.title = operation == nil ? "Render sample" : "Review result"
    enable(render, !pending && (operation != nil || (!direct && current && otherHasUnresolvedResult?() != true)))
    if operation == nil, window?.firstResponder === accept { window?.makeFirstResponder(direct ? closeButton : useSelection) }
    accept.isHidden = operation == nil; enable(accept, !pending && operation?.observation != nil)
    enable(discard, !hasUnresolvedResult); enable(closeButton, !pending)
    // Raw options remain editable through asynchronous work. A completion owns
    // its submitted copy, never the field editor's newer text or composition.
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    if pending { showStatus("Wait for the render request to finish before closing."); return false }
    return true
  }
  var keyboardOrder: [NSControl] { [useSelection, name, tail, instrument, check, render, accept, discard, closeButton] }
  @discardableResult func advanceKeyboardFocus(reverse: Bool) -> Bool {
    guard let window, !composing else { return false }
    let order = keyboardOrder, responder = window.firstResponder
    let current = order.firstIndex { responder === $0 || (responder != nil && responder === $0.currentEditor()) }
    let start = current ?? (reverse ? 0 : order.count - 1)
    for offset in 1...order.count {
      let control = order[(start + (reverse ? -offset : offset) + order.count) % order.count]
      if control.isEnabled, !control.isHiddenOrHasHiddenAncestor, window.makeFirstResponder(control) { return true }
    }
    return false
  }
  override var acceptsFirstResponder: Bool { true }
}

final class PatternSampleRenderWindow: NSWindow {
  override func sendEvent(_ event: NSEvent) {
    if event.type == .keyDown, let view = contentView as? PatternSampleRenderView,
      event.modifierFlags.intersection([.command, .control, .option]).isEmpty, !view.composing {
      if event.keyCode == 48, view.advanceKeyboardFocus(reverse: event.modifierFlags.contains(.shift)) { return }
      if event.keyCode == 53 { performClose(nil); return }
    }
    super.sendEvent(event)
  }
}
