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
    guard !clipboardBusy, let onRequest else { return }
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
    clipboardBusy = true
    let sample = index, generation = sampleGeneration
    onRequest(method, params) { [weak self] response in
      guard let self else { return }; self.clipboardBusy = false
      guard self.index == sample, self.sampleGeneration == generation else { return }
      guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
        self.clipboardStatus.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "Sample edit failed."; return
      }
      self.resetPastePreview()
      if action == "new", let index = data["sample"] as? Int {
        self.clipboardStatus.stringValue = "Copied \(data["frames"] ?? 0) frames to sample \(index)."
        self.index = index; self.onSelect?(index)
      } else if action == "copy" { self.clipboardStatus.stringValue = "Copied \(data["frames"] ?? 0) frames · \(data["channels"] ?? 0) channel(s) · \(data["rate"] ?? 0) Hz" }
      else {
        self.clipboardStatus.stringValue = "\(action == "cut" ? "Cut" : "Deleted") \(data["removedFrames"] ?? 0) frames. Undo restores the audio and loops."
        self.waveform.selection = first...first
      }
    }
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
    guard !clipboardBusy, let onRequest, let base = pasteParams() else { return }
    clipboardBusy = true
    let sample = index, generation = sampleGeneration, signature = base as NSDictionary
    let send: (String, String?) -> Void = { [weak self] clipboardID, revision in
      guard let self else { return }
      var p = base; p["clipboardId"] = clipboardID; p["dryRun"] = dryRun
      if let revision { p["expectedRevision"] = revision }
      onRequest("sample.paste", p) { [weak self] response in
        guard let self else { return }; self.clipboardBusy = false
        guard self.index == sample, self.sampleGeneration == generation else { return }
        guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
          self.clipboardStatus.stringValue = (response["error"] as? [String: Any])?["message"] as? String ?? "Paste failed."
          self.resetPastePreview(); return
        }
        let clipped = data["clippedSamples"] as? Int ?? 0
        self.clipboardStatus.stringValue = "\(dryRun ? "Preview" : "Pasted"): \(data["insertedFrames"] ?? 0) frames · result \(data["resultFrames"] ?? 0) frames" + (clipped > 0 ? " · \(clipped) clipped values" : "")
        self.pastePreviewSignature = dryRun ? signature : nil
        self.pastePreviewRevision = dryRun ? result["revision"] as? String : nil
        self.pastePreviewID = dryRun ? clipboardID : nil
        if !dryRun, let first = data["start"] as? Int, let frames = data["insertedFrames"] as? Int { self.waveform.selection = first...(first + frames) }
      }
    }
    if !dryRun, pastePreviewSignature?.isEqual(signature) == true, let id = pastePreviewID, let revision = pastePreviewRevision {
      send(id, revision)
    } else {
      onRequest("sample.clipboard.get", [:]) { [weak self] response in
        guard let self else { return }
        guard self.index == sample, self.sampleGeneration == generation else { self.clipboardBusy = false; return }
        guard let result = response["result"] as? [String: Any], let data = result["data"] as? [String: Any], let id = data["clipboardId"] as? String else {
          self.clipboardBusy = false; self.clipboardStatus.stringValue = "Sample clipboard is empty. Copy a region first."; return
        }
        send(id, result["revision"] as? String)
      }
    }
  }
}
