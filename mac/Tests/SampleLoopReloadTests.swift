import AppKit

extension InterfaceTests {
  static func sampleLoopReloadChecks() throws {
    func metadata(_ f: SampleOperationFixture, frames: Int = 128) -> [String: Any] {
      var info = f.info; info["frames"] = frames; info["loopStart"] = 0; info["loopEnd"] = frames
      info["loop"] = frames > 0; info["sustainLoop"] = false; info["sustainStart"] = 0; info["sustainEnd"] = 0
      return info
    }
    for failure in ["none", "metadata", "waveform", "revision", "document", "identity", "shape"] {
      let f = SampleOperationFixture(), e = f.editor
      f.revision = "latest"; e.loopStart.stringValue = "-"; e.loopNeedsReload = true
      let peaks = e.waveform.peaks, region = SampleViewRegion(e), loops = e.rawLoopDraft
      var done = [Bool](); e.reloadLoops(); e.finishAutomaticEdit { done.append($0) }
      try require(f.calls.last?.0 == "sample.get" && f.calls.last?.2 == "sample-one" && e.loopsBusy && done.isEmpty, "Loop reload owns its captured sample and waiting Save")
      if failure == "metadata" { try f.fail() }
      else {
        try f.answer(metadata(f))
        try require(e.rawLoopDraft.isEqual(loops) && e.waveform.peaks == peaks && SampleViewRegion(e) == region && done.isEmpty,
          "Loop metadata alone cannot replace fields, geometry, waveform or finish Save")
        if failure == "waveform" { try f.fail() }
        else {
          var wave = f.pending[0].1; wave["peaks"] = Array(repeating: 0.5, count: (wave["bins"] as! Int) * 2)
          if failure == "identity" { e.updateCatalogue([["index": 1, "id": "replacement", "name": "Replacement"]]) }
          if failure == "shape" { wave["peaks"] = [0.0, Double.nan] }
          try f.answer(wave, revision: failure == "revision" ? "changed" : "latest", document: failure == "document" ? "different-song" : "song")
        }
      }
      if failure == "none" {
        try require(done == [true] && !e.loopsBusy && !e.loopNeedsReload && e.loopStart.stringValue == "0" && e.loopEnd.stringValue == "128" && e.waveform.frames == 128 && e.waveform.peaks.first == 0.5 && e.waveformRevision == "latest" && e.loopDraftRevision == "latest",
          "Matching metadata and waveform adopt loops and audio presentation together")
      } else {
        try require(done == [false] && !e.loopsBusy && e.loopNeedsReload && e.rawLoopDraft.isEqual(loops) && e.waveform.peaks == peaks && SampleViewRegion(e) == region && e.loopDraftRevision == "original",
          "Failed or mismatched \(failure) readback settles Save while retaining the complete previous view")
      }
    }
    for change in ["loop", "settings", "processing", "selection", "option", "revision"] {
      let f = SampleOperationFixture(), e = f.editor
      f.revision = "latest"; e.reloadLoops(); try f.answer(metadata(f))
      switch change {
      case "loop": e.loopStart.stringValue = "newer"
      case "settings": e.name.stringValue = "New name"
      case "processing": e.gain.stringValue = "-12"
      case "selection": e.selectionStart.stringValue = "0050"
      case "option": e.crossfadeCurve.selectItem(at: 1)
      default: f.revision = "changed"
      }
      let fields = e.drawingFields, options = e.sampleSelectionOptions
      try f.finishRead()
      try require(e.loopNeedsReload && !e.loopsBusy && e.waveformRevision == "original" && e.drawingFields == fields && e.sampleSelectionOptions == options,
        "A late loop reload preserves newer \(change) input without rebasing or partial adoption")
    }
    for malformed in ["fractional", "negative", "past end", "reversed", "empty enabled", "directions", "identity", "rate", "volume", "pan"] {
      let f = SampleOperationFixture(), e = f.editor
      f.revision = "latest"; e.reloadLoops(); var info = metadata(f)
      switch malformed {
      case "fractional": info["loopStart"] = 0.5
      case "negative": info["loopStart"] = -1
      case "past end": info["loopEnd"] = 129
      case "reversed": info["loopStart"] = 20; info["loopEnd"] = 10
      case "empty enabled": info["loopEnd"] = 0
      case "directions": info["pingpong"] = true; info["reverseLoop"] = true
      case "rate": info["rate"] = 44100.5
      case "volume": info["volume"] = -1
      case "pan": info["pan"] = Double.nan
      default: info["id"] = "other-sample"
      }
      try f.answer(info)
      try require(e.loopNeedsReload && !e.loopsBusy && f.calls.count == 1 && e.loopEnd.stringValue == "512", "Malformed saved loop \(malformed) cannot be converted to default or truncated fields")
    }
    let unavailable = SampleOperationFixture(), u = unavailable.editor
    unavailable.revision = "latest"; u.reloadLoops(); u.onRequest = nil; u.onSettingsRequest = nil
    var unavailableDone = [Bool](); u.finishAutomaticEdit { unavailableDone.append($0) }
    try unavailable.answer(metadata(unavailable))
    try require(unavailableDone == [false] && !u.loopsBusy && u.loopNeedsReload, "Losing the read transport between stages settles pending actions")
    let retained = SampleOperationFixture(), r = retained.editor
    retained.revision = "latest"; r.name.stringValue = "Draft name"; r.selectionStart.stringValue = "0010"; r.selectionEnd.stringValue = "unfinished"; r.gain.stringValue = "-12"
    var done = [Bool](); r.reloadLoops(); r.finishAutomaticEdit { done.append($0) }
    try retained.answer(metadata(retained)); try retained.finishRead()
    try require(r.loopDraftRevision == "latest" && r.settingsRevision == "original" && r.settingsNeedsReload && r.name.stringValue == "Draft name" && r.gain.stringValue == "-12" && r.selectionStart.stringValue == "0010" && r.selectionEnd.stringValue == "unfinished" && done == [false] && retained.calls.allSatisfy { $0.0.hasSuffix(".get") },
      "Loop-only reload keeps unrelated raw fields and settings ownership without automatically resubmitting them")
    let empty = SampleOperationFixture(), z = empty.editor
    empty.revision = "latest"; z.reloadLoops(); try empty.answer(metadata(empty, frames: 0))
    try require(empty.pending.isEmpty && !z.loopsBusy && z.waveform.frames == 0 && z.waveform.peaks.isEmpty && z.waveform.selection == 0...0 && z.waveformRevision == "latest", "An empty sample adopts a validated empty waveform without an invalid zero-bin request")
    let mono = SampleOperationFixture(), m = mono.editor
    mono.revision = "latest"; m.channelPicker.selectItem(at: 2); m.waveformChannels = "right"; m.reloadLoops()
    var one = metadata(mono); one["channels"] = 1; try mono.answer(one)
    try require(m.loopNeedsReload && m.selectedChannels == "right" && m.waveform.frames == 1024 && mono.pending.isEmpty, "A vanished right channel retains the accepted view for explicit full reload")
    let retired = SampleOperationFixture(), e = retired.editor
    retired.revision = "latest"; e.reloadLoops(); let previous = retired.pending.removeFirst().2
    var first = [Bool](); e.finishAutomaticEdit { first.append($0) }; e.resetDocumentContext(); retired.load(); retired.connect()
    e.reloadLoops(); var next = [Bool](); e.finishAutomaticEdit { next.append($0) }
    previous(["result": ["revision": "latest", "documentId": "song", "data": metadata(retired)]])
    try require(first == [false] && next.isEmpty && e.loopsBusy && retired.pending.count == 1, "Retired loop reads cannot release a newer reload or its waiters")
    try retired.answer(metadata(retired)); try retired.finishRead(); try require(next == [true], "Only the current loop read owner can complete Save")
    try sampleLoopReloadFocusChecks()
  }
  static func sampleLoopReloadFocusChecks() throws {
    let f = SampleOperationFixture(), e = f.editor
    let window = NSWindow(contentRect: e.frame, styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = e
    defer { e.settingsWork?.cancel(); window.orderOut(nil); window.contentView = nil }
    func expand(_ view: NSView) {
      if let section = view as? ToolSection, section.preference == "toolSection.sample.settings" { section.setExpanded(true, persist: false) }
      view.subviews.forEach(expand)
    }
    expand(e); window.layoutIfNeeded(); f.revision = "latest"
    e.reloadLoops(); try f.answer(f.info)
    window.makeFirstResponder(e.name)
    guard let field = e.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Missing sample reload field editor") }
    field.setMarkedText("Composing", selectedRange: NSRange(location: 3, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    let text = field.string, caret = field.selectedRange(); try f.finishRead()
    try require(e.loopNeedsReload && e.loopDraftRevision == "original" && field.hasMarkedText() && field.string == text && field.selectedRange() == caret && e.name.currentEditor() === field,
      "Late loop readback preserves newer native focus, composition and caret")
    field.unmarkText(); e.settingsWork?.cancel()
  }
}
