import AppKit

extension SampleEditor {
  func resetPastePreview() { pastePreviewSignature = nil; pastePreviewRevision = nil; pastePreviewID = nil }
  func makeClipboardActions() -> NSView {
    stack(.horizontal, [
      ActionButton("Copy") { [weak self] in self?.clipboardAction("copy") },
      ActionButton("Cut") { [weak self] in self?.clipboardAction("cut") },
      ActionButton("Delete") { [weak self] in self?.clipboardAction("delete") },
      ActionButton("Copy to new") { [weak self] in self?.clipboardAction("new") }, NSView(),
      ActionButton("Preview paste") { [weak self] in self?.pasteClipboard(dryRun: true) },
      ActionButton("Paste") { [weak self] in self?.pasteClipboard(dryRun: false) },
    ], spacing: 10)
  }
  func makePasteControls() -> NSView {
    pasteMode.addItems(withTitles: ["Insert", "Overwrite", "Mix", "Replace selection"])
    pasteMode.setAccessibilityLabel("Sample paste mode"); pasteMode.fixed(width: 150)
    pasteMode.target = self; pasteMode.action = #selector(pasteModeChanged)
    pasteRate.addItems(withTitles: ["Resample to target", "Keep frame count"])
    pasteRate.setAccessibilityLabel("Sample paste rate conversion"); pasteRate.fixed(width: 150)
    pasteDestinationGroup.isHidden = true
    return stack(.horizontal, [labeled("PASTE MODE", pasteMode), labeled("RATE", pasteRate),
      labeled("CLIPBOARD (dB)", pasteSourceGain), pasteDestinationGroup, NSView()], spacing: 12)
  }
  @objc func pasteModeChanged() { pasteDestinationGroup.isHidden = pasteMode.indexOfSelectedItem != 2 }
  func clipboardAction(_ action: String) {
    if action == "paste" { pasteClipboard(dryRun: false); return }
    guard let first = Int(selectionStart.stringValue), let last = Int(selectionEnd.stringValue),
      first >= 0, first < last, last <= waveform.frames else {
      clipboardStatus.stringValue = "Select a nonempty region to copy, cut or delete."; return
    }
    if ["cut", "delete"].contains(action) && selectedChannels != "both" {
      clipboardStatus.stringValue = "Cut and Delete remove time from both channels. Select Both channels."; return
    }
    var params: [String: Any] = ["sample": index, "start": first, "end": last]
    if ["copy", "new"].contains(action) { params["channels"] = selectedChannels }
    let method = action == "new" ? "sample.copyToNew" : action == "copy" ? "sample.clipboard.copy" : action == "cut" ? "sample.cut" : "sample.delete"
    guard let operation = beginOperation(method, params: params) else { return }
    submitOperation(operation)
  }
  func pasteParams() -> [String: Any]? {
    guard let first = Int(selectionStart.stringValue), first >= 0, first <= waveform.frames,
      let source = Double(pasteSourceGain.stringValue), source.isFinite, (-96...24).contains(source) else {
      clipboardStatus.stringValue = "Enter a valid paste position and clipboard gain (−96 to +24 dB)."; return nil
    }
    let mode = ["insert", "overwrite", "mix", "replace"][max(0, pasteMode.indexOfSelectedItem)]
    if ["insert", "replace"].contains(mode) && selectedChannels != "both" {
      clipboardStatus.stringValue = "Insert and Replace change time in both channels. Select Both channels."; return nil
    }
    var p: [String: Any] = ["sample": index, "at": first, "mode": mode, "channels": selectedChannels,
      "sourceGainDB": source, "rateMode": pasteRate.indexOfSelectedItem == 1 ? "keep-frames" : "resample"]
    if mode == "replace" {
      guard let last = Int(selectionEnd.stringValue), last >= first, last <= waveform.frames else {
        clipboardStatus.stringValue = "Enter the exclusive end of the region to replace."; return nil
      }; p["end"] = last
    }
    if mode == "mix" {
      guard let gain = Double(pasteDestinationGain.stringValue), gain.isFinite, (-96...24).contains(gain) else {
        clipboardStatus.stringValue = "Existing audio gain must be −96 to +24 dB."; return nil
      }; p["destinationGainDB"] = gain
    }
    return p
  }
  func pasteClipboard(dryRun: Bool) {
    guard let base = pasteParams() else { return }
    let matching = !dryRun && pastePreviewSignature?.isEqual(base as NSDictionary) == true
    guard let operation = beginOperation("sample.paste", params: base, dryRun: dryRun, previewRevision: matching ? pastePreviewRevision : nil) else { return }
    if matching, let id = pastePreviewID {
      operation.params["clipboardId"] = id; submitOperation(operation); return
    }
    requestOperation(operation, "sample.clipboard.get", [:]) { [weak self] response in
      guard let self else { return }
      guard self.operationFieldsUnchanged(operation), (self.onSampleRevision?() ?? operation.revision) == operation.revision,
        let result = self.operationResult(response, operation, revision: operation.revision), let data = result["data"] as? [String: Any],
        self.validOperationClipboard(data), data["available"] as? Bool == true, let id = data["clipboardId"] as? String else {
        self.retainOperation(operation, "Clipboard lookup failed, is empty or the captured input changed. Prepare the paste again."); return
      }
      operation.params["clipboardId"] = id; self.submitOperation(operation)
    }
  }
}
