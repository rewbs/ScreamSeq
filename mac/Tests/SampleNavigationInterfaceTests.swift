import AppKit

extension InterfaceTests {
  static func sampleNavigationChecks() throws {
    let info: [String: Any] = ["name": "Navigation", "frames": 1024, "channels": 2, "rate": 44100, "volume": 64, "pan": 128]
    let catalogue: [[String: Any]] = [["index": 1, "id": "first", "name": "Navigation"], ["index": 2, "id": "second", "name": "Other"]]
    func makeEditor() -> SampleEditor {
      let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
      editor.update(info, samples: catalogue, revision: "current")
      editor.waveform.frame = NSRect(x: 0, y: 0, width: 512, height: 180)
      editor.waveform.setViewport(128..<160, notify: false); editor.waveform.peaksRange = 128..<160
      editor.waveform.peaks = Array(repeating: 0.25, count: 64)
      editor.waveformRevision = "current"; editor.waveformDocumentID = "song"
      return editor
    }
    func reply(_ params: [String: Any], revision: String = "current", change: ([String: Any]) -> [String: Any] = { $0 }) -> [String: Any] {
      var data = params; data["peaks"] = Array(repeating: 0.5, count: (params["bins"] as? Int ?? 0) * 2)
      return ["result": ["revision": revision, "documentId": "song", "data": change(data)]]
    }
    for channel in [false, true] {
      for fault in ["failure", "malformed", "newer-fields", "revision", "non-finite"] {
        let editor = makeEditor(), oldPeaks = Array(repeating: Float(0.25), count: 64)
        var requests = [(String, [String: Any], String)](), replies = [([String: Any]) -> Void]()
        editor.onSettingsRequest = { method, params, identity, done in requests.append((method, params, identity)); replies.append(done) }
        let navigate = {
          if channel { editor.channelPicker.selectItem(at: 1); editor.channelChanged() }
          else { editor.waveform.setViewport(256..<288) }
        }
        navigate()
        try require(requests.count == 1 && requests[0].0 == "sample.waveform.get" && requests[0].2 == "first" && editor.waveform.visibleRange == 128..<160 && editor.selectedChannels == "both" && editor.waveform.peaks == oldPeaks,
          "Pending navigation retains the accepted target, viewport, channel menu and waveform")
        let context = editor.viewportContext(frames: 1024, revision: "current")
        try require(context["start"] as? Int == 128 && context["channels"] as? String == "both" && context["loaded"] as? Bool == true,
          "Agent context continues to describe the accepted loaded view while navigation is pending")
        editor.drawToggle.state = .on; editor.waveform.drawing = true
        try require(!editor.waveform.beginStroke(SampleStrokePoint(frame: 140, value: 0)), "Pending view readback cannot start a drawing gesture")
        if fault == "newer-fields" {
          editor.name.stringValue = "Newer raw name"; editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: editor.name))
        }
        var response = reply(requests.last!.1, revision: fault == "revision" ? "later" : "current")
        if fault == "failure" { response = ["error": ["code": -32003, "message": "Read failed"]] }
        if fault == "malformed" { response = reply(requests.last!.1) { data in var data = data; data["start"] = 999; return data } }
        if fault == "non-finite" { response = reply(requests.last!.1) { data in var data = data; data["peaks"] = Array(repeating: Double.nan, count: 64); return data } }
        replies.removeFirst()(response)
        try require(editor.waveform.visibleRange == 128..<160 && editor.selectedChannels == "both" && editor.waveform.peaks == oldPeaks && editor.waveformRevision == "current" && editor.waveformQuery == nil && editor.waveform.requestedRange == nil,
          "Failed, malformed, stale or superseded navigation preserves the complete accepted presentation")
        if fault == "newer-fields" { try require(editor.name.stringValue == "Newer raw name" && editor.settingsRevision == "current", "Read failure retains newer raw fields and their revision") }
        navigate(); replies.removeFirst()(reply(requests.last!.1))
        try require(editor.waveform.visibleRange == (channel ? 128..<160 : 256..<288) && editor.selectedChannels == (channel ? "left" : "both") && editor.waveform.peaks == Array(repeating: 0.5, count: 64) && requests.allSatisfy { $0.0 == "sample.waveform.get" },
          "A complete explicit retry adopts only the requested view and never writes music")
        if fault == "newer-fields" { try require(editor.name.stringValue == "Newer raw name" && editor.hasDraft, "Successful navigation leaves retained settings untouched") }
        editor.settingsWork?.cancel()
      }
    }
    let editor = makeEditor()
    var requests = [[String: Any]](), replies = [([String: Any]) -> Void]()
    editor.onSettingsRequest = { _, params, _, done in requests.append(params); replies.append(done) }
    editor.waveform.zoom(2); editor.waveform.zoom(2)
    editor.channelPicker.selectItem(at: 1); editor.channelChanged()
    editor.drawToggle.state = .on; editor.toggleDrawing()
    try require(requests.count == 1 && editor.waveform.visibleRange == 128..<160 && editor.selectedChannels == "both", "Rapid zoom and channel changes keep a single read in flight")
    replies.removeFirst()(reply(requests[0]))
    try require(requests.count == 2 && requests[1]["start"] as? Int == 140 && requests[1]["end"] as? Int == 148 && requests[1]["channels"] as? String == "left", "Repeated zoom accumulates intent and coalesces with the latest channel")
    replies.removeFirst()(reply(requests[1]))
    try require(editor.waveform.visibleRange == 140..<148 && editor.selectedChannels == "left" && editor.waveform.peaks.count == 16, "Latest viewport, channel and peaks become visible together")
    editor.waveform.setViewport(256..<288)
    let oldReply = replies.removeFirst(), oldRequest = requests.last!
    editor.index = 2; var shorter = info; shorter["frames"] = 32; shorter["name"] = "Other"
    editor.onSettingsRequest = nil; editor.update(shorter, samples: catalogue, revision: "other")
    oldReply(reply(oldRequest))
    try require(editor.index == 2 && editor.waveform.visibleRange == 0..<32 && editor.name.stringValue == "Other" && editor.waveformRevision == nil, "Retired view replies cannot overwrite another sample")
    editor.waveform.drawing = false
    editor.update(["frames": 0, "channels": 1], samples: catalogue, revision: "empty")
    editor.channelPicker.selectItem(at: 0); editor.waveformChannels = "both"
    var unexpectedRead = false
    editor.onSettingsRequest = { _, _, _, _ in unexpectedRead = true }
    editor.channelPicker.selectItem(at: 1); editor.channelChanged()
    try require(editor.selectedChannels == "left" && editor.waveform.visibleRange.isEmpty && editor.waveform.peaks.isEmpty && !unexpectedRead, "An empty sample adopts a valid channel without inventing waveform data")
    try sampleNavigationCompositionChecks()
  }
  static func sampleNavigationCompositionChecks() throws {
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    editor.update(["name": "Focus", "frames": 1024, "channels": 2], samples: [["index": 1, "id": "focus"]], revision: "current")
    editor.waveform.setViewport(128..<160, notify: false); editor.waveform.peaksRange = 128..<160
    editor.waveform.peaks = Array(repeating: 0.25, count: 64); editor.waveformRevision = "current"
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 800, height: 900), styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = editor
    defer { editor.settingsWork?.cancel(); window.orderOut(nil); window.contentView = nil }
    var params = [String: Any](), callback: (([String: Any]) -> Void)?
    editor.onSettingsRequest = { _, request, _, done in params = request; callback = done }
    editor.waveform.setViewport(256..<288)
    window.makeFirstResponder(editor.name)
    guard let field = editor.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Navigation fixture requires a native field editor") }
    field.setMarkedText("Composing", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    let selection = field.selectedRange(), focus = window.firstResponder
    params["peaks"] = Array(repeating: 0.5, count: 64)
    callback?(["result": ["revision": "current", "data": params]])
    try require(editor.waveform.visibleRange == 128..<160 && field.hasMarkedText() && field.selectedRange() == selection && window.firstResponder === focus,
      "A late view read leaves native composition, selection and newer focus intact")
    field.unmarkText()
  }
}
