import AppKit

extension InterfaceTests {
  static func sampleSelectionChecks() throws {
    let catalogue: [[String: Any]] = [["index": 1, "id": "first", "name": "First"], ["index": 2, "id": "second", "name": "Second"]]
    func info(_ sample: Int, frames: Int = 1024) -> [String: Any] {
      ["index": sample, "name": sample == 1 ? "First" : "Second", "frames": frames, "channels": 2, "rate": 44100, "volume": 64, "pan": 128,
        "loop": false, "loopStart": 0, "loopEnd": 0, "pingpong": false, "reverseLoop": false,
        "sustainLoop": false, "sustainStart": 0, "sustainEnd": 0, "sustainPingpong": false, "sustainReverse": false]
    }
    func makeEditor() -> SampleEditor {
      let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
      editor.update(info(1), samples: catalogue, revision: "current")
      editor.waveform.frame = NSRect(x: 0, y: 0, width: 512, height: 180)
      editor.waveform.setViewport(128..<160, notify: false); editor.waveform.selection = 130...140
      editor.waveform.peaksRange = 128..<160; editor.waveform.peaks = Array(repeating: 0.25, count: 64)
      editor.waveformRevision = "current"; editor.waveformDocumentID = "song"
      return editor
    }
    func response(_ method: String, _ params: [String: Any], revision: String = "current", frames: Int = 1024) -> [String: Any] {
      var data = method == "sample.get" ? info(params["sample"] as! Int, frames: frames) : params
      if method == "sample.waveform.get" { data["peaks"] = Array(repeating: 0.5, count: (params["bins"] as! Int) * 2) }
      return ["result": ["revision": revision, "documentId": "song", "data": data]]
    }
    for reload in [false, true] {
      for stage in ["sample.get", "sample.waveform.get"] {
        for fault in ["failure", "malformed", "newer-fields", "newer-options", "revision", "missing-target", "wrong-revision", "wrong-document", "wrong-sample"] {
          let editor = makeEditor()
          if reload {
            editor.waveform.drawing = true
            try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 132, value: 0.75)), "Reload fixture starts a local stroke")
            editor.name.stringValue = "Raw name"; editor.loopStart.stringValue = "-"; editor.selectionStart.stringValue = "007"; editor.pasteSourceGain.stringValue = "-6.00"
          }
          let fields = editor.drawingFields, stroke = editor.waveform.stroke, region = SampleViewRegion(editor), peaks = editor.waveform.peaks
          var revision = "current", requests = [(String, [String: Any], String)](), replies = [([String: Any]) -> Void]()
          editor.onSampleRevision = { revision }
          editor.onSettingsRequest = { method, params, identity, done in requests.append((method, params, identity)); replies.append(done) }
          var input = 1
          editor.onSampleSelected = { input = $0 }
          let request = {
            if reload { editor.reloadSample() }
            else { editor.picker.selectItem(withTag: 2); editor.selectSample() }
          }
          request()
          try require(editor.index == 1 && editor.picker.selectedTag() == 1 && editor.drawingFields == fields && editor.waveform.stroke == stroke && input == 1,
            "Pending reload/selection retains the original target, picker, raw fields and local stroke")
          if stage == "sample.waveform.get" { replies.removeFirst()(response(requests.last!.0, requests.last!.1)) }
          try require(requests.last!.0 == stage, "Fixture reaches the requested readback stage")
          if fault == "newer-fields" { editor.name.stringValue = "Newer name" }
          if fault == "newer-options" { editor.operationPicker.selectItem(at: 1) }
          if fault == "revision" { revision = "later" }
          if fault == "missing-target" { editor.updateCatalogue([catalogue[0]]) }
          var result = response(requests.last!.0, requests.last!.1)
          if fault == "failure" { result = ["error": ["message": "Injected read failure"]] }
          if fault == "malformed" { result = ["result": ["revision": "current", "documentId": "song", "data": ["sample": 1, "frames": "broken"]]] }
          if fault.hasPrefix("wrong-") {
            var value = result["result"] as! [String: Any]
            if fault == "wrong-revision" { value["revision"] = "wrong" }
            if fault == "wrong-document" { value["documentId"] = "wrong" }
            if fault == "wrong-sample" {
              var data = value["data"] as! [String: Any]; data[stage == "sample.get" ? "index" : "sample"] = 99; value["data"] = data
            }
            result["result"] = value
          }
          // A reload of sample 1 still has its target; remove its stable ID too.
          if reload && fault == "missing-target" { editor.updateCatalogue([["index": 1, "id": "replacement", "name": "Replacement"]]) }
          replies.removeFirst()(result)
          try require(editor.index == 1 && editor.picker.selectedTag() == 1 && SampleViewRegion(editor) == region && editor.waveform.peaks == peaks && editor.waveform.stroke == stroke && input == 1 && editor.sampleSelectionQuery == nil,
            "Failed, malformed or stale sample reads preserve target, view, waveform, stroke and input")
          var expectedFields = fields
          if fault == "newer-fields" { expectedFields["name"] = "Newer name"; expectedFields["field0"] = "Newer name" }
          try require(editor.drawingFields == expectedFields, "Readback failure preserves every raw field")
          if fault == "newer-options" { try require(editor.operationPicker.indexOfSelectedItem == 1, "Newer processing options remain selected") }
          editor.updateCatalogue(catalogue)
          if !reload && fault == "newer-fields" {
            // The musician explicitly abandons this newer name before retrying the picker.
            editor.name.stringValue = "First"
          }
          request()
          while !replies.isEmpty { replies.removeFirst()(response(requests.last!.0, requests.last!.1, revision: revision)) }
          try require(editor.index == (reload ? 1 : 2) && editor.picker.selectedTag() == editor.index && editor.waveform.stroke.isEmpty && !editor.hasDraft && editor.sampleRevision == revision && input == (reload ? 1 : 2),
            "A complete explicit retry adopts the requested sample and retires only the authorized old drafts")
          try require(requests.allSatisfy { $0.0 == "sample.get" || $0.0 == "sample.waveform.get" }, "Selection and reload never write music")
        }
      }
    }
    let editor = makeEditor()
    var requests = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onSettingsRequest = { method, params, _, done in requests.append((method, params)); replies.append(done) }
    func drain(frames: Int = 1024) {
      while !replies.isEmpty { replies.removeFirst()(response(requests.last!.0, requests.last!.1, frames: frames)) }
    }
    editor.requestSampleSelection(2); editor.requestSampleSelection(1); editor.requestSampleSelection(2)
    try require(requests.count == 1, "Rapid sample choices have only one metadata read in flight")
    replies.removeFirst()(response(requests[0].0, requests[0].1))
    try require(requests.count == 2 && requests[1].0 == "sample.get" && editor.index == 1, "Superseded metadata skips its waveform and starts only the latest choice")
    drain(); try require(editor.index == 2 && editor.waveform.visibleRange == 0..<1024, "Latest sample loads its whole view")
    editor.requestSampleSelection(1); drain()
    try require(editor.waveform.visibleRange == 128..<160 && editor.waveform.selection == 130...140, "Revisiting a stable sample restores its accepted view and selection")
    editor.selectionStart.stringValue = "00130"; editor.name.stringValue = "Retained name"
    editor.refreshSample(samples: catalogue); drain()
    try require(editor.selectionStart.stringValue == "00130" && editor.name.stringValue == "Retained name" && editor.hasDraft, "Automatic refresh retains raw selection and settings fields")
    editor.reloadSample(); drain(frames: 80)
    try require(editor.waveform.visibleRange == 79..<80 && editor.waveform.selection == 80...80 && editor.selectionStart.stringValue == "80", "Explicit reload clamps saved view/selection to shorter audio")
    editor.reloadSample(); editor.resetDocumentContext(); drain()
    try require(editor.settingsIdentity.isEmpty && editor.sampleSelectionQuery == nil && !editor.sampleSelectionInFlight && editor.sampleViewStates.isEmpty, "Document retirement rejects late selection readback and clears only its own cache")
    // New/Open retire the old song's presentation, including while a read is
    // outstanding. Same-document read failures above must still preserve it.
    let replaced = makeEditor()
    let replacementWindow = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 800, height: 900), styleMask: [.titled], backing: .buffered, defer: false)
    replacementWindow.isReleasedWhenClosed = false; replacementWindow.contentView = replaced
    defer { replacementWindow.orderOut(nil); replacementWindow.contentView = nil }
    var replacementRevision = "current", replacementRequests = [(String, [String: Any])](), replacementReplies = [([String: Any]) -> Void]()
    replaced.onSampleRevision = { replacementRevision }
    replaced.onSettingsRequest = { method, params, _, done in replacementRequests.append((method, params)); replacementReplies.append(done) }
    replacementWindow.makeFirstResponder(replaced.name)
    replaced.refreshSample(samples: catalogue)
    try require(replacementReplies.count == 1, "Replacement fixture has an old-document metadata read in flight")
    replacementRevision = "new-song:0"
    replaced.adoptDocument(samples: [])
    replaced.refreshSample(samples: [])
    replacementReplies.removeFirst()(response(replacementRequests[0].0, replacementRequests[0].1))
    try require(!replaced.hasSamplePresentation && replaced.sampleBody?.isHidden == true && replaced.sampleTargetActions.isHidden && !replaced.emptySample.isHidden,
      "Empty adopted documents show creation actions without old sample editing controls")
    try require(replaced.name.stringValue.isEmpty && replaced.selectionStart.stringValue == "0" && replaced.selectionEnd.stringValue == "0" && replaced.loopEnd.stringValue == "0" && replaced.waveform.frames == 0 && replaced.waveform.peaks.isEmpty && replaced.waveform.loopMarkers.isEmpty,
      "Successful document replacement clears old sample fields, region, loops and waveform")
    try require(replaced.settingsIdentity.isEmpty && replaced.sampleRevision == nil && replaced.waveformDocumentID == nil && !replaced.hasDraft && replacementRequests.count == 1 && replaced.name.currentEditor() == nil,
      "Replacement retires focused ownership and rejects old replies without issuing reads or writes for an empty song")
    let onlySample = Array(catalogue.prefix(1))
    replaced.updateCatalogue(onlySample)
    try require(!replaced.reloadSampleButton.isHiddenOrHasHiddenAncestor, "Importing the first sample makes retry available before its first read finishes")
    replaced.adoptDocument(samples: onlySample)
    replaced.refreshSample(samples: onlySample)
    replacementReplies.removeFirst()(["error": ["message": "Read unavailable"]])
    try require(replaced.sampleBody?.isHidden == true && !replaced.drawingStatus.isHiddenOrHasHiddenAncestor && replaced.drawingStatus.stringValue.contains("Reload sample") && !replaced.reloadSampleButton.isHiddenOrHasHiddenAncestor && replaced.reloadSampleButton.isEnabled,
      "A failed first load exposes its error and Reload outside the empty presentation, even when the only selected slot has not changed")
    replaced.reloadSample()
    while !replacementReplies.isEmpty {
      var reply = response(replacementRequests.last!.0, replacementRequests.last!.1, revision: replacementRevision)
      var result = reply["result"] as! [String: Any]; result["documentId"] = "new-song"; reply["result"] = result
      replacementReplies.removeFirst()(reply)
    }
    try require(replaced.hasSamplePresentation && replaced.sampleBody?.isHidden == false && !replaced.sampleTargetActions.isHidden && replaced.emptySample.isHidden && replaced.settingsIdentity == "first" && replaced.waveformDocumentID == "new-song" && replaced.sampleRevision == replacementRevision && !replaced.hasDraft,
      "A new document may reuse slot and stable asset identifiers; only its complete new readback exposes editing again")
    let gesture = makeEditor()
    gesture.waveform.drawing = true
    try require(gesture.waveform.beginStroke(SampleStrokePoint(frame: 132, value: 0.75)), "Automatic refresh fixture starts a local gesture")
    gesture.onSampleRevision = { "later" }
    var gestureRequest = ("", [String: Any]()), gestureReply: (([String: Any]) -> Void)?
    gesture.onSettingsRequest = { method, params, _, done in gestureRequest = (method, params); gestureReply = done }
    gesture.refreshSample(samples: catalogue); gestureReply?(["error": ["message": "Read failed"]])
    try require(!gesture.waveform.stroke.isEmpty && gesture.sampleRevision == "current", "Failed automatic refresh keeps a gesture even after the song revision changes")
    gesture.refreshSample(samples: catalogue)
    gestureReply?(response(gestureRequest.0, gestureRequest.1, revision: "later"))
    gestureReply?(response(gestureRequest.0, gestureRequest.1, revision: "later"))
    try require(gesture.waveform.stroke.isEmpty && gesture.sampleRevision == "later", "Complete replacement presentation retires the obsolete unsubmitted gesture")
    try sampleSelectionFocusChecks()
  }
  static func sampleSelectionFocusChecks() throws {
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    editor.update(["index": 1, "name": "Focus", "frames": 32], samples: [["index": 1, "id": "first"], ["index": 2, "id": "second"]], revision: "current")
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 800, height: 900), styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = editor
    defer { editor.settingsWork?.cancel(); window.orderOut(nil); window.contentView = nil }
    var reply: (([String: Any]) -> Void)?, requests = 0
    editor.onSettingsRequest = { _, _, _, done in requests += 1; reply = done }
    func metadata(_ sample: Int) -> [String: Any] {
      ["result": ["revision": "current", "data": ["index": sample, "name": "Loaded", "frames": 32, "channels": 1, "rate": 44100, "volume": 64, "pan": 128,
        "loop": false, "loopStart": 0, "loopEnd": 0, "pingpong": false, "reverseLoop": false,
        "sustainLoop": false, "sustainStart": 0, "sustainEnd": 0, "sustainPingpong": false, "sustainReverse": false]]]
    }
    editor.requestSampleSelection(2)
    window.makeFirstResponder(editor.name)
    guard let field = editor.name.currentEditor() as? NSTextView else { throw InterfaceFailure(message: "Selection fixture requires a native field editor") }
    field.setSelectedRange(NSRange(location: 2, length: 0))
    let responder = window.firstResponder
    reply?(metadata(2))
    try require(editor.index == 1 && window.firstResponder === responder && field.selectedRange().location == 2 && editor.name.stringValue == "Focus" && requests == 1, "A late sample choice preserves newer native focus, caret and fields before requesting waveform")
    editor.reloadSample()
    window.makeFirstResponder(editor.rate)
    reply?(metadata(1))
    try require(editor.sampleSelectionQuery == nil && requests == 2 && editor.rate.currentEditor() != nil, "Moving between fields retires a read even when AppKit reuses its field editor")
    window.makeFirstResponder(editor.name)
    editor.reloadSample()
    field.setMarkedText("Compose", selectedRange: NSRange(location: 3, length: 0), replacementRange: NSRange(location: 0, length: field.string.utf16.count))
    reply?(metadata(1))
    try require(field.hasMarkedText() && field.selectedRange().location == 3 && editor.sampleSelectionQuery == nil && requests == 3, "Reload readback cannot erase native composition")
    field.unmarkText()
  }
}
