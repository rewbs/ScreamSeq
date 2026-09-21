import AppKit

struct InterfaceFailure: Error { let message: String }
@main struct InterfaceTests {
  static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
    if !value() { throw InterfaceFailure(message: message) }
  }
  static func key(
    _ view: PatternView, _ code: UInt16, _ text: String,
    flags: NSEvent.ModifierFlags = [], repeatKey: Bool = false, up: Bool = false
  ) {
    let event = NSEvent.keyEvent(
      with: up ? .keyUp : .keyDown, location: .zero,
      modifierFlags: flags, timestamp: 0, windowNumber: 0, context: nil,
      characters: text, charactersIgnoringModifiers: text, isARepeat: repeatKey, keyCode: code)!
    if up { view.keyUp(with: event) } else { view.keyDown(with: event) }
  }
  static func waveformChecks() throws -> SampleEditor {
    let view = WaveformView(frame: NSRect(x: 0, y: 0, width: 256, height: 180))
    view.frames = 1000; view.selection = 270...300; view.setViewport(200..<456)
    try require(view.frameAt(x: 0) == 200 && view.frameAt(x: 256) == 456 && view.frameAt(x: 256, insertion: false) == 455,
      "Zoomed hit testing uses absolute frames and distinguishes insertion boundaries from samples")
    try require(view.frameAt(x: -50) == 200 && view.frameAt(x: 999) == 456, "Hit testing clamps to the visible region")
    view.zoomSelection(); try require(view.visibleRange == 270..<300 && view.selection == 270...300, "Zoom to selection preserves absolute selection")
    view.zoom(2, center: 280); try require(view.visibleRange == 275..<290, "Zoom preserves the anchor's screen position")
    view.panFrames(10000); try require(view.visibleRange == 985..<1000, "Panning clamps at the sample end")
    view.panFrames(Int.max); try require(view.visibleRange == 985..<1000, "Huge positive pan clamps before integer addition can overflow")
    view.panFrames(Int.min); try require(view.visibleRange == 0..<15, "Huge negative pan safely reaches the sample start")
    let viewport=view.visibleRange
    for center in [Double.nan, .infinity, -.infinity] {view.zoom(2,center:center)}
    try require(view.visibleRange==viewport,"Non-finite zoom anchors cannot trap or corrupt the viewport")
    view.zoom(1,center:Double.greatestFiniteMagnitude)
    try require(view.visibleRange==985..<1000,"Finite zoom anchors are bounded before converting to integer frames")
    view.panFrames(-10000); try require(view.visibleRange == 0..<15, "Panning clamps at the sample start")
    view.setViewport(nil); try require(view.visibleRange == 0..<1000, "Whole view restores the full waveform")
    view.setViewport(900..<1000); view.frames = 10
    try require(view.visibleRange == 0..<10, "A shorter edited sample retires an invalid viewport")
    view.frames = 0; view.zoom(2); view.panFrames(1); try require(view.visibleRange.isEmpty, "Empty samples have a safe empty viewport")
    let editor = SampleEditor(frame: NSRect(x: 0, y: 0, width: 729, height: 1400))
    let samples: [[String: Any]] = [["index": 1, "name": "Drawing fixture"]]
    editor.update(["frames": 1000, "channels": 2], samples: samples, revision: "initial")
    editor.waveform.frame = NSRect(x: 0, y: 0, width: 512, height: 180)
    var requests = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onRequest = { method, params, reply in requests.append((method, params)); replies.append(reply) }
    func answer(_ revision: String, _ count: Int = 32) {
      replies.removeFirst()(["result": ["revision": revision, "data": ["peaks": Array(repeating: 0.25, count: count * 2)]]])
    }
    editor.waveform.setViewport(240..<272); editor.waveform.setViewport(248..<280); editor.waveform.setViewport(256..<288)
    try require(requests.count == 1, "Rapid viewport changes retain only one in-flight waveform read")
    answer("old")
    try require(requests.count == 2 && requests.last?.1["start"] as? Int == 256 && requests.last?.1["end"] as? Int == 288 && requests.last?.1["bins"] as? Int == 32,
      "Retired replies dispatch only the latest viewport, with one bin per frame at high zoom")
    try require(editor.waveform.peaks.isEmpty, "Retired waveform data never replaces the visible region")
    answer("fresh")
    try require(editor.waveform.precise && editor.waveformRevision == "fresh", "Drawing readiness is tied to the displayed waveform revision")
    let freshViewport=editor.viewportContext(frames:1000,revision:"fresh")
    try require(freshViewport["start"] as? Int == 256 && freshViewport["precise"] as? Bool == true,
      "Agent viewport reports absolute coordinates and loaded precision")
    try require(editor.viewportContext(frames:1000,revision:"newer")["loaded"] as? Bool == false,
      "A stale same-length waveform is not reported as loaded for the current document")
    let retiredViewport=editor.viewportContext(frames:10,revision:"shorter")
    try require(retiredViewport["start"] as? Int == 0 && retiredViewport["end"] as? Int == 10 && retiredViewport["loaded"] as? Bool == false,
      "Hidden stale sample views return bounded whole-sample context until refreshed")
    editor.drawToggle.state = .on; editor.toggleDrawing(); answer("fresh")
    try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 260, value: 0.5)), "A precise, loaded waveform accepts a stroke")
    editor.waveform.extendStroke(SampleStrokePoint(frame: 266, value: -0.5))
    editor.waveform.extendStroke(SampleStrokePoint(frame: 263, value: 0.75))
    try require(editor.waveform.stroke.count == 7 && editor.waveform.stroke[263] == 0.75 && abs(editor.waveform.stroke[264]! - 1.0/3.0) < 1e-12,
      "Backtracking redraws crossed frames with the latest gesture segment")
    editor.waveformRevision = "changed-during-gesture"
    let beforeStroke = requests.count
    editor.waveform.finishStroke()
    try require(requests.count == beforeStroke + 1 && requests.last?.0 == "sample.draw" && requests.last?.1["expectedRevision"] as? String == "fresh" &&
      (requests.last?.1["points"] as? [[String: Any]])?.count == 7, "One stroke is one API command with the original displayed revision")
    try require(!editor.waveform.beginStroke(SampleStrokePoint(frame: 264, value: 0)), "A pending drawing edit cannot be submitted twice")
    replies.removeFirst()(["error": ["code": -32001, "message": "Song changed"]])
    try require(editor.drawingStatus.stringValue == "Song changed" && editor.waveform.stroke.isEmpty && requests.last?.0 == "sample.waveform.get",
      "Stale drawings show the failure, discard the overlay and reload the waveform")
    answer("next")
    try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 269, value: -1)), "A refreshed waveform accepts the next gesture")
    editor.waveform.finishStroke()
    replies.removeFirst()(["result": ["revision": "drawn", "data": ["changedFrames": 1]]])
    answer("drawn")
    try require(editor.drawingStatus.stringValue.contains("Drew 1 frame"), "A completed drawing reports its result")
    let beforeCancel = requests.count
    try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 264, value: 0)), "Begin cancellable gesture")
    editor.waveform.cancelStroke(); editor.waveform.finishStroke()
    try require(requests.count == beforeCancel && editor.strokeRevision == nil, "Cancelling a gesture sends no edit and discards its revision")
    editor.waveform.setViewport(300..<332)
    replies.removeFirst()(["error": ["code": -32002, "message": "Busy"]])
    let retry = editor.waveformRetry
    try require(retry != nil && editor.waveformPending, "Busy reads schedule a bounded retry")
    editor.waveform.setViewport(400..<432)
    try require(retry?.isCancelled == true && requests.last?.1["start"] as? Int == 400, "A new viewport retires the old busy retry")
    answer("moved")
    try require(editor.waveform.beginStroke(SampleStrokePoint(frame: 410, value: 0)), "Begin before a document refresh")
    editor.update(["frames": 1000, "channels": 2], samples: samples, revision: "external")
    let afterRefresh = requests.count
    editor.waveform.finishStroke()
    try require(requests.count == afterRefresh && editor.strokeRevision == nil && editor.waveform.stroke.isEmpty,
      "A document refresh cancels an in-progress drawing instead of rebasing it")
    answer("external")
    editor.waveform.setViewport(500..<532); editor.index = 2
    let beforeRetired = requests.count
    answer("retired")
    try require(requests.count == beforeRetired && editor.waveformRevision == nil, "Changing sample selection retires an in-flight read")
    editor.onRequest = nil; editor.index = 1
    editor.update(["name": "Waveform drawing", "frames": 16384, "channels": 1, "rate": 48000, "loop": true, "loopStart": 1008, "loopEnd": 1056], samples: samples, revision: "snapshot")
    editor.waveform.setViewport(1000..<1064); editor.waveform.peaksRange = 1000..<1064
    editor.waveform.peaks = (0..<64).flatMap { i -> [Float] in let value = Float(sin(Double(i)*0.24)*0.7); return [value, value] }
    editor.waveformRevision = "snapshot"; editor.waveform.selection = 1004...1040
    _ = editor.waveform.beginStroke(SampleStrokePoint(frame: 1020, value: 0.2))
    editor.waveform.extendStroke(SampleStrokePoint(frame: 1032, value: -0.5))
    return editor
  }
  static func parameterBoundaryChecks() throws {
    let editor=PluginEditor(frame:.zero),model=PatternModel(["nativePlugins":[["name":"Boundary fixture","format":"AU"]]])
    var edits=[Double]()
    editor.onParameter={_,_,value,_ in edits.append(value)}
    func row(_ values:[AnyHashable:Any])->PluginParameterRow {
      editor.update(model:model,values:[values])
      return editor.tableView(editor.parameters,viewFor:nil,row:0) as! PluginParameterRow
    }
    for value in [Double.nan,.infinity,-.infinity,Double.greatestFiniteMagnitude] {
      let r=row(["id":0,"min":0.0,"max":1.0,"value":value])
      try require(!r.slider.isEnabled && !r.reading.isEditable && r.slider.doubleValue.isFinite,
        "Invalid vendor values disable edits without integer conversion traps")
      r.slider.changed?(0.5);r.reading.commit?("0.5")
    }
    for bounds in [(Double.nan,1.0), (0.0,Double.infinity), (2.0,1.0), (-Double.greatestFiniteMagnitude,Double.greatestFiniteMagnitude)] {
      let r=row(["id":0,"min":bounds.0,"max":bounds.1,"value":0.5])
      try require(!r.slider.isEnabled && r.slider.minValue.isFinite && r.slider.maxValue.isFinite,
        "Invalid or overflowing vendor ranges never reach AppKit")
      r.slider.changed?(0.5)
    }
    try require(edits.isEmpty,"Invalid plugin metadata cannot emit a fabricated parameter change")
    let huge=row(["id":0,"min":0.0,"max":1e30,"value":1e25])
    try require(huge.slider.isEnabled && huge.reading.stringValue=="1e+25","Large valid continuous parameters do not pass through Int")
    huge.reading.commit?("2e25");try require(edits.last==2e25,"Large finite parameter edits retain their value")
    let logarithmic=row(["id":0,"min":1e-300,"max":1e300,"value":1.0,"displayScale":"logarithmic"])
    try require(abs(logarithmic.slider.doubleValue-0.5)<1e-12,"Log sliders avoid overflowing maximum/minimum ratios")
    logarithmic.slider.changed?(0.75)
    try require(abs(log(edits.last!)-log(1e150))<1e-10,"Logarithmic edits remain finite over a wide range")
    let tinyStep=row(["id":0,"min":0.0,"max":1.0,"value":0.0,"step":Double.leastNonzeroMagnitude])
    tinyStep.reading.commit?("0.5");try require(edits.last==0.5,"A sub-precision step does not turn an edit into infinity")
    let count=edits.count;tinyStep.slider.changed?(.nan);tinyStep.slider.changed?(.infinity)
    try require(edits.count==count,"Non-finite slider input is ignored")
  }
  static func layoutChecks() throws {
    let recovery = try recoveryChecks()
    let commands = try patternCommandsChecks()
    try sampleCrossfadeChecks()
    try sampleLoopChecks()
    try sampleSnapChecks()
    let zoomedSample = try waveformChecks()
    let samples: [[String: Any]] = [
      ["index": 1, "name": String(repeating: "Long sample name ", count: 8)]
    ]
    let instruments: [[String: Any]] = [["index": 1, "name": "Lead"]]
    let model = PatternModel([
      "samples": samples, "instruments": instruments,
      "patterns": [["index": 0, "rows": 1024]], "orders": Array(repeating: 0, count: 5000),
      "sequences": [["name": "First sequence"], ["name": "Second sequence"]],
      "sequence": 1, "nativePlugins": [["name": "Apple: AULowpass"]],
      "canUndoEffect": true,
    ])
    let sample = SampleEditor(frame: .zero)
    sample.update(["name": "Long sample", "frames": 5_000_000], samples: samples)
    sample.snapMode.selectItem(at:1);sample.updateSnapOptions()
    sample.waveform.selection = 251...769
    try require(sample.selectionStart.stringValue == "251" && sample.selectionEnd.stringValue == "769", "Dragging reflects exact exclusive frame boundaries")
    sample.selectionStart.stringValue = "300"; sample.selectionEnd.stringValue = "301"; sample.setRange()
    try require(sample.waveform.selection == 300...301, "Single-frame selection is retained")
    sample.selectionEnd.stringValue = "300.5"; sample.setRange()
    try require(sample.waveform.selection == 300...301, "Fractional frame input cannot truncate or mutate selection")
    sample.selectionEnd.stringValue = "301"
    var sampleRequests = [(String, [String: Any])]()
    var sampleReplies = [([String: Any]) -> Void]()
    sample.onRequest = { method, params, reply in sampleRequests.append((method, params)); sampleReplies.append(reply) }
    sample.operationPicker.selectItem(at: 2); sample.curvePicker.selectItem(at: 2); sample.exponent.stringValue = "2.5"
    sample.updateProcessingOptions(); sample.process(dryRun: true)
    try require(sampleRequests.count == 1 && sampleRequests[0].0 == "sample.process" && sampleRequests[0].1["exponent"] as? Double == 2.5 && sampleRequests[0].1["start"] as? Int == 300, "Native fades send exact controls through the shared API")
    try require(!sample.previewButton.isEnabled && !sample.processButton.isEnabled, "Processing cannot be submitted twice while pending")
    sampleReplies.removeFirst()(["result": ["revision": "sample-preview", "data": ["changedFrames": 1, "peakAfter": 0.0]]])
    sample.process(dryRun: false)
    try require(sampleRequests.last?.1["expectedRevision"] as? String == "sample-preview", "Apply uses the preview revision rather than silently rebasing it")
    sampleReplies.removeFirst()(["error": ["code": -32001, "message": "Song changed"]])
    try require(sample.processingStatus.stringValue == "Song changed", "Stale sample previews surface the API error")
    sample.operationPicker.selectItem(at: 6); sample.updateProcessingOptions(); sample.smoothing.stringValue = "4"; sample.process(dryRun: true)
    try require(sampleRequests.count == 2 && sample.processingStatus.stringValue.contains("odd"), "Invalid smoothing is rejected before dispatch")
    sample.smoothing.stringValue = "5"; sample.process(dryRun: true)
    sampleReplies.removeFirst()(["result": ["revision": "old-sample", "data": ["changedFrames": 0]]])
    sample.index = 2; sample.index = 1; sample.selectionStart.stringValue = "300"; sample.selectionEnd.stringValue = "301"; sample.process(dryRun: false)
    try require(sampleRequests.last?.1["expectedRevision"] == nil, "Changing sample selection retires its preview")
    sampleReplies.removeFirst()(["result": ["revision": "new-sample", "data": ["changedFrames": 1]]])
    sample.selectionStart.stringValue = "300"; sample.selectionEnd.stringValue = "300"; sample.setRange()
    try require(sample.waveform.selection == 300...300, "A zero-length range is a retained insertion cursor")
    var clipboardCount = sampleRequests.count
    sample.clipboardAction("copy")
    try require(sampleRequests.count == clipboardCount && sample.clipboardStatus.stringValue.contains("nonempty"), "Copy rejects an insertion cursor before dispatch")
    sample.selectionEnd.stringValue = "310"; sample.setRange()
    sample.waveform.copy(nil)
    try require(sampleRequests.last?.0 == "sample.clipboard.copy" && sampleRequests.last?.1["start"] as? Int == 300 && sampleRequests.last?.1["end"] as? Int == 310, "Waveform Copy dispatches the exact half-open region through the API")
    sample.waveform.cut(nil)
    try require(sampleRequests.count == clipboardCount + 1, "Clipboard controls cannot submit twice while pending")
    sampleReplies.removeFirst()(["result": ["revision": "copy-revision", "data": ["frames": 10, "channels": 1, "rate": 44100]]])
    sample.pasteMode.selectItem(at: 2); sample.pasteModeChanged()
    sample.pasteSourceGain.stringValue = "-3"; sample.pasteDestinationGain.stringValue = "-6"
    sample.pasteClipboard(dryRun: true)
    try require(sampleRequests.last?.0 == "sample.clipboard.get" && !sample.pasteDestinationGroup.isHidden, "Mix preview reads the clipboard identity and exposes existing-audio gain")
    sampleReplies.removeFirst()(["result": ["revision": "clipboard-read", "data": ["clipboardId": "clipboard-A"]]])
    try require(sampleRequests.last?.0 == "sample.paste" && sampleRequests.last?.1["sourceGainDB"] as? Double == -3 && sampleRequests.last?.1["destinationGainDB"] as? Double == -6 && sampleRequests.last?.1["expectedRevision"] as? String == "clipboard-read", "Mix preview preserves exact gains, identity and the read revision")
    sampleReplies.removeFirst()(["result": ["revision": "paste-preview", "data": ["insertedFrames": 10, "resultFrames": 5_000_000, "clippedSamples": 2]]])
    try require(sample.clipboardStatus.stringValue.contains("2 clipped"), "Clipping is reported before applying a paste")
    clipboardCount = sampleRequests.count
    sample.waveform.paste(nil)
    try require(sampleRequests.count == clipboardCount + 1 && sampleRequests.last?.0 == "sample.paste" && sampleRequests.last?.1["clipboardId"] as? String == "clipboard-A" && sampleRequests.last?.1["expectedRevision"] as? String == "paste-preview", "Applying a matching preview freezes both document and clipboard identities without rereading")
    sampleReplies.removeFirst()(["error": ["code": -32001, "message": "Clipboard changed"]])
    try require(sample.clipboardStatus.stringValue == "Clipboard changed" && sample.pastePreviewID == nil, "Stale clipboard failures surface and retire the preview")
    sample.pasteClipboard(dryRun: true)
    sample.index = 2
    clipboardCount = sampleRequests.count
    sampleReplies.removeFirst()(["result": ["revision": "retired", "data": ["clipboardId": "retired"]]])
    try require(sampleRequests.count == clipboardCount && !sample.clipboardBusy, "A delayed clipboard lookup cannot paste into a newly selected sample")
    sample.index = 1; sample.selectionStart.stringValue = "300"; sample.selectionEnd.stringValue = "310"; sample.setRange()
    sample.clipboardAction("cut")
    sampleReplies.removeFirst()(["result": ["revision": "cut", "data": ["removedFrames": 10]]])
    try require(sample.waveform.selection == 300...300, "Cut leaves an insertion cursor at the removed region")
    sample.selectionEnd.stringValue = "310"; sample.setRange()
    var copiedSample = 0
    sample.onSelect = { copiedSample = $0 }
    sample.clipboardAction("new")
    try require(sampleRequests.last?.0 == "sample.copyToNew" && sampleRequests.last?.1["start"] as? Int == 300 && sampleRequests.last?.1["end"] as? Int == 310,
      "Copy to new sends the selected region through the shared API")
    sampleReplies.removeFirst()(["result": ["revision": "new-copy", "data": ["sample": 2, "frames": 10]]])
    try require(sample.index == 2 && copiedSample == 2 && sample.waveform.selection == nil,
      "Copy to new selects the returned slot and retires the old region")
    sample.onSelect = nil; sample.index = 1
    sample.pasteSourceGain.stringValue = "0"; sample.pasteDestinationGain.stringValue = "0"
    sample.pasteMode.selectItem(at: 0); sample.pasteModeChanged()
    sample.onRequest = nil
    sample.update(["name": "Long sample", "frames": 5_000_000, "channels": 2], samples: samples)
    sample.operationPicker.selectItem(at: 3); sample.curvePicker.selectItem(at: 3); sample.exponent.stringValue = "3"; sample.updateProcessingOptions()
    sample.waveform.peaks = (0..<2048).flatMap { i -> [Float] in let x = Float(i) / 2048; let envelope = exp(-3*x); return [-envelope * abs(sin(x*131)), envelope * abs(sin(x*173))] }
    sample.selectionStart.stringValue = "251000"; sample.selectionEnd.stringValue = "3269000"; sample.setRange()
    let displayPeaks = sample.waveform.peaks
    sample.onRequest = { method, params, reply in sampleRequests.append((method, params)); sampleReplies.append(reply) }
    sample.channelPicker.selectItem(at: 1); sample.channelChanged()
    sample.channelPicker.selectItem(at: 2); sample.channelChanged()
    try require(sampleRequests.last?.0 == "sample.waveform.get" && sampleRequests.last?.1["channels"] as? String == "left", "Rapid channel requests are coalesced while one read is pending")
    sampleReplies.removeFirst()(["result": ["data": ["peaks": [-0.5, 0.5]]]])
    try require(sampleRequests.last?.1["channels"] as? String == "right", "The latest channel query uses the shared read API after the first completes")
    try require(sample.waveform.peaks == displayPeaks, "Out-of-order channel waveform replies cannot replace the active channel")
    sampleReplies.removeFirst()(["result": ["data": ["peaks": [-0.25, 0.25]]]])
    try require(sample.waveform.peaks == [-0.25, 0.25], "Active channel waveform response is displayed")
    try require(sample.automationContext(frames: 5_000_000)["start"] as? Int == 251000 && sample.automationContext(frames: 256)["end"] as? Int == 256, "Agent sample context uses current inventory length and retires invalid stale ranges")
    sample.onRequest = nil; sample.channelPicker.selectItem(at: 0); sample.waveform.peaks = displayPeaks
    sample.processingStatus.stringValue = "Preview: 3,018,000 frames change · peak −0.26 dBFS"
    let instrument = InstrumentEditor(frame: .zero)
    instrument.update(["name": "Lead"], model: model)
    let effects = PluginEditor(frame: .zero)
    effects.update(
      model: model,
      values: [["id": 0, "name": "Cutoff frequency", "min": 10, "max": 22050, "value": 4200]])
    let manyParameters: [[AnyHashable: Any]] = (0..<4096).map {
      ["id": $0, "name": "Parameter \($0)", "min": 0.0, "max": 1.0, "value": 0.5]
    }
    effects.update(model: model, values: manyParameters)
    try require(effects.parameters.numberOfRows == 4096, "All plugin parameters remain reachable")
    effects.search.stringValue = "Parameter 4095"
    effects.filterParameters()
    try require(
      effects.parameters.numberOfRows == 1, "Plugin parameter search finds the last parameter")
    effects.search.stringValue = ""
    effects.filterParameters()
    let builtin = PluginEditor(frame: .zero)
    builtin.update(model: PatternModel(["nativePlugins": [["name": "Gainer", "format": "Built-in"]]]), values: [
      ["id": 1, "name": "Gain", "min": -96.0, "max": 24.0, "value": -12.0, "unitLabel": "dB"],
      ["id": 3, "name": "Left polarity", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Normal", "Inverted"]]])
    let gainRow = builtin.tableView(builtin.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    let phaseRow = builtin.tableView(builtin.parameters, viewFor: nil, row: 1) as! PluginParameterRow
    try require(!builtin.openButton.isEnabled && gainRow.reading.stringValue == "-12 dB", "Built-in panel displays units and uses native parameter controls")
    try require(phaseRow.slider.allowsTickMarkValuesOnly && phaseRow.slider.numberOfTickMarks == 2 && phaseRow.reading.stringValue == "Inverted", "Discrete built-in choices are labeled and snapped")
    var builtinEdit = -1.0
    builtin.onParameter = { slot, id, value, record in if slot == 0 && id == 3 { builtinEdit = value } }
    phaseRow.slider.doubleValue = 0; phaseRow.slider.update()
    try require(builtinEdit == 0 && phaseRow.reading.stringValue == "Normal", "Discrete controls dispatch the shared parameter edit")
    let equalizer = PluginEditor(frame: .zero)
    let eqValues: [[AnyHashable: Any]] = [
      ["id": 0, "name": "Enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 1, "name": "Channels", "min": 0.0, "max": 4.0, "value": 3.0, "choices": ["Stereo", "Left", "Right", "Mid", "Side"]],
      ["id": 2, "name": "Output gain", "min": -24.0, "max": 24.0, "value": 0.0, "unitLabel": "dB"],
      ["id": 10, "name": "Band 1 frequency", "min": 20.0, "max": 20000.0, "value": 1000.0, "unitLabel": "Hz", "displayScale": "logarithmic"],
      ["id": 11, "name": "Band 1 gain", "min": -18.0, "max": 18.0, "value": 6.0, "unitLabel": "dB"],
      ["id": 12, "name": "Band 1 Q", "min": 0.1, "max": 12.0, "value": 0.70710678, "unitLabel": "Q", "displayScale": "logarithmic"],
      ["id": 13, "name": "Band 1 shape", "min": 0.0, "max": 5.0, "value": 0.0, "choices": ["Bell", "Low shelf", "High shelf", "Low-pass", "High-pass", "Notch"]]]
    equalizer.update(model: PatternModel(["nativePlugins": [["name": "EQ10", "format": "Built-in"]]]), values: eqValues)
    let frequencyRow = equalizer.tableView(equalizer.parameters, viewFor: nil, row: 3) as! PluginParameterRow
    var frequencyEdits = [Double]()
    equalizer.onParameter = { slot, id, value, _ in if slot == 0 && id == 10 { frequencyEdits.append(value) } }
    try require(abs(frequencyRow.slider.doubleValue - log(50) / log(1000)) < 1e-12, "Frequency slider displays logarithmic position")
    frequencyRow.slider.doubleValue = 0.5; frequencyRow.slider.update()
    try require(abs(frequencyEdits.last! - sqrt(20 * 20000)) < 1e-9, "Logarithmic midpoint emits actual Hz through the shared edit path")
    frequencyRow.reading.stringValue = "1234.56789 Hz"; frequencyRow.reading.submit()
    try require(frequencyEdits.last == 1234.56789 && abs(frequencyRow.slider.doubleValue - log(1234.56789 / 20) / log(1000)) < 1e-12, "Typed precision updates the slider and parameter in actual units")
    let count = frequencyEdits.count
    frequencyRow.reading.submit()
    frequencyRow.reading.stringValue = "nan"; frequencyRow.reading.submit()
    frequencyRow.reading.stringValue = "25000"; frequencyRow.reading.submit()
    try require(frequencyEdits.count == count, "Repeated, invalid and out-of-range text does not create edits")
    frequencyRow.reading.controlTextDidBeginEditing(Notification(name: NSControl.textDidBeginEditingNotification))
    try require(frequencyRow.reading.stringValue == "1234.56789", "Editing exposes full precision, without unit suffix")
    frequencyRow.reading.submit()
    try require(frequencyEdits.count == count, "Focusing and leaving a numeric field does not round its value")
    equalizer.filterParameters()
    frequencyRow.reading.stringValue = "1400"; frequencyRow.reading.submit()
    try require(frequencyEdits.count == count, "A retired parameter row cannot send a delayed edit after table reload or device selection")
    let comb = PluginEditor(frame: .zero)
    comb.update(model: PatternModel(["nativePlugins": [["name": "Comb Filter", "format": "Built-in"]]]), values: [
      ["id": 0, "name": "Enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 1, "name": "Note", "min": 12.0, "max": 127.0, "value": 69.0, "unitLabel": "MIDI", "step": 1.0],
      ["id": 2, "name": "Transpose", "min": -12.0, "max": 12.0, "value": 0.0, "unitLabel": "st"],
      ["id": 3, "name": "Feedback", "min": -95.0, "max": 95.0, "value": 50.0, "unitLabel": "%"],
      ["id": 4, "name": "Dry / wet", "min": 0.0, "max": 100.0, "value": 50.0, "unitLabel": "%"],
      ["id": 5, "name": "Inertia", "min": 5.0, "max": 1000.0, "value": 20.0, "unitLabel": "ms", "displayScale": "logarithmic"],
      ["id": 6, "name": "Channels", "min": 0.0, "max": 4.0, "value": 0.0, "choices": ["Stereo", "Left", "Right", "Mid", "Side"]],
      ["id": 7, "name": "Output gain", "min": -24.0, "max": 24.0, "value": 0.0, "unitLabel": "dB"]])
    let noteRow = comb.tableView(comb.parameters, viewFor: nil, row: 1) as! PluginParameterRow
    var notes = [Double]()
    comb.onParameter = { _, id, value, _ in if id == 1 { notes.append(value) } }
    try require(noteRow.reading.stringValue == "A4 (69)", "Musical controls display note name and MIDI number")
    noteRow.slider.doubleValue = 70.4; noteRow.slider.update()
    try require(notes == [70] && noteRow.reading.stringValue == "A♯4 (70)", "Note slider quantizes before dispatch and reads actual note")
    noteRow.reading.stringValue = "60.7"; noteRow.reading.submit()
    try require(notes == [70, 61] && noteRow.reading.stringValue == "C♯4 (61)", "Typed fractional note agrees with processor quantization")
    noteRow.reading.stringValue = "B♭3"; noteRow.reading.submit()
    try require(notes.last == 58 && noteRow.reading.stringValue == "A♯3 (58)", "Typed flat note names convert to standard MIDI pitch")
    noteRow.reading.stringValue = "C#4"; noteRow.reading.submit()
    try require(notes.last == 61, "Typed sharp note names convert to MIDI pitch")
    let noteCount = notes.count
    noteRow.reading.stringValue = "C-1"; noteRow.reading.submit()
    noteRow.reading.stringValue = "H4"; noteRow.reading.submit()
    try require(notes.count == noteCount, "Out-of-range and malformed note names reject without changes")
    let distortion = PluginEditor(frame: .zero)
    distortion.update(model: PatternModel(["nativePlugins": [["name": "Distortion", "format": "Built-in"]]]), values: [
      ["id": 0, "name": "Enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 1, "name": "Drive", "min": 0.0, "max": 36.0, "value": 6.0, "unitLabel": "dB"],
      ["id": 2, "name": "Mode", "min": 0.0, "max": 3.0, "value": 0.0, "choices": ["Soft clip", "Hard clip", "Fold", "Wrap"]],
      ["id": 3, "name": "Tone", "min": -100.0, "max": 100.0, "value": 0.0, "unitLabel": "%"],
      ["id": 4, "name": "Dry mix", "min": 0.0, "max": 100.0, "value": 0.0, "unitLabel": "%"],
      ["id": 5, "name": "Wet mix", "min": 0.0, "max": 100.0, "value": 100.0, "unitLabel": "%"],
      ["id": 6, "name": "Output gain", "min": -24.0, "max": 24.0, "value": -6.0, "unitLabel": "dB"]])
    let modeRow = distortion.tableView(distortion.parameters, viewFor: nil, row: 2) as! PluginParameterRow
    var distortionEdit = -1.0
    distortion.onParameter = { _, id, value, _ in if id == 2 { distortionEdit = value } }
    modeRow.slider.doubleValue = 3; modeRow.slider.update()
    try require(distortionEdit == 3 && modeRow.reading.stringValue == "Wrap" && modeRow.slider.numberOfTickMarks == 4,
      "Distortion exposes all four modes through labeled, snapped controls and shared edits")
    let lofi = PluginEditor(frame: .zero)
    lofi.update(model: PatternModel(["nativePlugins": [["name": "LofiMat", "format": "Built-in"]]]), values: [
      ["id": 0, "name": "Enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 1, "name": "Bit depth", "min": 1.0, "max": 24.0, "value": 16.0, "unitLabel": "bits"],
      ["id": 2, "name": "Rate", "min": 20.0, "max": 384000.0, "value": 48000.0, "unitLabel": "Hz", "displayScale": "logarithmic"],
      ["id": 3, "name": "Noise", "min": 0.0, "max": 100.0, "value": 0.0, "unitLabel": "%"],
      ["id": 4, "name": "Smooth", "min": 0.0, "max": 1.0, "value": 0.0, "choices": ["Off", "On"]],
      ["id": 5, "name": "Dry mix", "min": 0.0, "max": 100.0, "value": 0.0, "unitLabel": "%"],
      ["id": 6, "name": "Wet mix", "min": 0.0, "max": 100.0, "value": 100.0, "unitLabel": "%"],
      ["id": 7, "name": "Output gain", "min": -24.0, "max": 24.0, "value": 0.0, "unitLabel": "dB"],
      ["id": 8, "name": "Noise seed", "min": 1.0, "max": 16777215.0, "value": 1234567.0, "step": 1.0]])
    let seedRow = lofi.tableView(lofi.parameters, viewFor: nil, row: 8) as! PluginParameterRow
    var seedEdits = [Double]()
    lofi.onParameter = { _, id, value, _ in if id == 8 { seedEdits.append(value) } }
    try require(seedRow.reading.stringValue == "1234567", "Integer seeds display all digits without scientific rounding")
    seedRow.reading.stringValue = "16777214.7"; seedRow.reading.submit()
    try require(seedEdits == [16777215] && seedRow.reading.stringValue == "16777215", "Typed seeds quantize consistently before dispatch")
    seedRow.reading.submit(); seedRow.reading.stringValue = "16777216"; seedRow.reading.submit()
    try require(seedEdits.count == 1, "Unchanged or out-of-range seed input creates no extra edit")
    let cabinet = PluginEditor(frame: .zero)
    let cabinetNames = ["Open 1x8", "Open 1x10", "Open 1x12", "Open 2x12", "Closed 1x12", "Closed 2x12", "Closed 4x12", "Bright 4x12", "Dark 4x12", "Vintage 1x12", "Vintage 2x12", "Bass 1x15", "Bass 2x10", "Bass 4x10", "Bass 8x10", "Small radio", "Lo-fi box", "Wide-range"]
    let routes = ["Preamp → Cabinet → EQ", "Preamp → EQ → Cabinet", "Cabinet → Preamp → EQ", "Cabinet → EQ → Preamp", "EQ → Preamp → Cabinet", "EQ → Cabinet → Preamp"]
    var cabinetValues: [[AnyHashable: Any]] = [
      ["id": 0, "name": "Enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 1, "name": "Cabinet model", "min": 0.0, "max": 17.0, "value": 2.0, "choices": cabinetNames],
      ["id": 2, "name": "Routing", "min": 0.0, "max": 5.0, "value": 0.0, "choices": routes],
      ["id": 3, "name": "Preamp gain", "min": 0.0, "max": 36.0, "value": 6.0, "unitLabel": "dB"],
      ["id": 4, "name": "Channels", "min": 0.0, "max": 1.0, "value": 0.0, "choices": ["Stereo", "Mono"]],
      ["id": 5, "name": "Dry mix", "min": 0.0, "max": 100.0, "value": 0.0, "unitLabel": "%"],
      ["id": 6, "name": "Wet mix", "min": 0.0, "max": 100.0, "value": 100.0, "unitLabel": "%"],
      ["id": 7, "name": "Output gain", "min": -24.0, "max": 24.0, "value": -6.0, "unitLabel": "dB"],
      ["id": 8, "name": "Preamp enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]],
      ["id": 9, "name": "Cabinet enabled", "min": 0.0, "max": 1.0, "value": 1.0, "choices": ["Off", "On"]]]
    let cabinetFrequencies: [Double] = [80, 250, 1000, 4000, 12000]
    for band in 0..<5 {
      let id = 10 + band * 4
      cabinetValues += [
        ["id": id, "name": "Band \(band + 1) frequency", "min": 20.0, "max": 20000.0, "value": cabinetFrequencies[band], "unitLabel": "Hz", "displayScale": "logarithmic"],
        ["id": id + 1, "name": "Band \(band + 1) gain", "min": -18.0, "max": 18.0, "value": 0.0, "unitLabel": "dB"],
        ["id": id + 2, "name": "Band \(band + 1) Q", "min": 0.1, "max": 12.0, "value": 0.70710678, "unitLabel": "Q"],
        ["id": id + 3, "name": "Band \(band + 1) shape", "min": 0.0, "max": 5.0, "value": 0.0, "choices": ["Bell", "Low shelf", "High shelf", "Low-pass", "High-pass", "Notch"]]]
    }
    cabinet.update(model: PatternModel(["nativePlugins": [["name": "Cabinet Simulator", "format": "Built-in"]]]), values: cabinetValues)
    var cabinetEdits = [(Int, Double)]()
    cabinet.onParameter = { _, id, value, _ in cabinetEdits.append((id, value)) }
    let routingRow = cabinet.tableView(cabinet.parameters, viewFor: nil, row: 2) as! PluginParameterRow
    routingRow.slider.doubleValue = 5; routingRow.slider.update()
    try require(cabinetEdits.last?.0 == 2 && cabinetEdits.last?.1 == 5 && routingRow.reading.stringValue == routes[5], "All six cabinet orders use labeled agent-compatible edits")
    try require(routingRow.readingWidth.constant >= (routes[5] as NSString).size(withAttributes: [.font: routingRow.reading.font!]).width + 10, "Routing labels have enough space to avoid truncation")
    cabinet.search.stringValue = "Band 5 shape"; cabinet.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    try require(cabinet.filteredValues.count == 1, "Last cabinet control is reachable by search")
    let lastCabinetRow = cabinet.tableView(cabinet.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    lastCabinetRow.slider.doubleValue = 5; lastCabinetRow.slider.update()
    try require(cabinetEdits.last?.0 == 29 && cabinetEdits.last?.1 == 5, "Last EQ shape retains its stable parameter ID after filtering")
    cabinet.search.stringValue = ""; cabinet.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    let dynamics = PluginEditor(frame: .zero)
    func dynamicParameter(_ id: Int, _ name: String, _ min: Double, _ max: Double, _ value: Double, _ unit: String = "", _ choices: [String] = []) -> [AnyHashable: Any] {
      ["id": id, "name": name, "min": min, "max": max, "value": value, "unitLabel": unit, "choices": choices]
    }
    let dynamicValues = [
      dynamicParameter(0, "Enabled", 0, 1, 1, "", ["Off", "On"]),
      dynamicParameter(1, "Threshold", -96, 0, -36, "dB"),
      dynamicParameter(3, "Attack", 0.1, 200, 1, "ms"),
      dynamicParameter(4, "Release", 5, 5000, 100, "ms"),
      dynamicParameter(5, "Output gain", -24, 24, 0, "dB"),
      dynamicParameter(7, "Detector", 0, 1, 0, "", ["Peak", "RMS"]),
      dynamicParameter(8, "Stereo link", 0, 100, 100, "%"),
      dynamicParameter(9, "Detector source", 0, 1, 1, "", ["Internal", "External sidechain"]),
      dynamicParameter(10, "Detector high-pass", 20, 20000, 20, "Hz"),
      dynamicParameter(11, "Detector low-pass", 20, 20000, 20000, "Hz"),
      dynamicParameter(12, "Detector filters", 0, 1, 0, "", ["Off", "On"]),
      dynamicParameter(13, "Listen to detector", 0, 1, 0, "", ["Off", "On"]),
      dynamicParameter(14, "Dry / wet", 0, 100, 100, "%"),
      dynamicParameter(15, "RMS window", 1, 100, 10, "ms"),
      dynamicParameter(16, "Hold", 0, 2000, 50, "ms"),
      dynamicParameter(17, "Floor", -96, 0, -96, "dB"),
      dynamicParameter(18, "Hysteresis", 0, 24, 3, "dB"),
      dynamicParameter(19, "Mode", 0, 1, 0, "", ["Gate", "Duck"])]
    dynamics.update(model: PatternModel(["nativePlugins": [["name": "Gate", "format": "Built-in"]]]), values: dynamicValues)
    try require(dynamics.dynamicsMeter.isHidden, "Meters wait for the selected device's data")
    let meterData: [AnyHashable: Any] = ["supported": true, "active": true, "reductionDB": [7.25, 5.5], "detectorDB": [-10.0, -12.0]]
    dynamics.showMeters(meterData)
    try require(!dynamics.dynamicsMeter.isHidden && dynamics.dynamicsMeter.active && dynamics.dynamicsMeter.reduction == [7.25, 5.5], "Native dynamics displays both detector and reduction channels")
    try require(dynamics.meterRefreshDue(1) && !dynamics.meterRefreshDue(1.01) && dynamics.meterRefreshDue(1.06), "Meter reads are bounded to twenty updates per second")
    dynamics.showMeters(["supported": false])
    try require(dynamics.dynamicsMeter.isHidden, "Devices without native metering hide the dynamics display")
    dynamics.showMeters(["supported": true, "active": false, "reductionDB": [0.0, 0.0], "detectorDB": [-160.0, -160.0]])
    try require(!dynamics.dynamicsMeter.active && dynamics.dynamicsMeter.reduction == [0, 0], "Stopped meter resets avoid stale attenuation")
    dynamics.showMeters(meterData)
    dynamics.search.stringValue = "Mode"; dynamics.filterParameters()
    var dynamicEdit: (Int, Double)?
    dynamics.onParameter = { _, id, value, _ in dynamicEdit = (id, value) }
    let duckRow = dynamics.tableView(dynamics.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    duckRow.slider.doubleValue = 1; duckRow.slider.update()
    try require(dynamicEdit?.0 == 19 && dynamicEdit?.1 == 1 && duckRow.reading.stringValue == "Duck", "Sparse gate parameter IDs survive search and dispatch")
    dynamics.search.stringValue = ""; dynamics.filterParameters()
    let maximizer = PluginEditor(frame: .zero)
    let limiterValues = [
      dynamicParameter(0, "Enabled", 0, 1, 1, "", ["Off", "On"]),
      dynamicParameter(1, "Boost", 0, 36, 18, "dB"),
      dynamicParameter(2, "Threshold", -36, 0, -9, "dB"),
      dynamicParameter(3, "Peak release", 1, 200, 20, "ms"),
      dynamicParameter(4, "Slow release", 20, 5000, 200, "ms"),
      dynamicParameter(5, "Ceiling", -36, 0, -1, "dB")]
    maximizer.update(model: PatternModel(["nativePlugins": [["name": "Maximizer", "format": "Built-in"]]]), values: limiterValues)
    maximizer.showMeters(["supported": true, "active": true, "reductionDB": [9.5, 9.5], "detectorDB": [4.0, 1.0]])
    maximizer.search.stringValue = "Ceiling"; maximizer.filterParameters()
    var limiterEdit: (Int, Double)?
    maximizer.onParameter = { _, id, value, _ in limiterEdit = (id, value) }
    let ceilingRow = maximizer.tableView(maximizer.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    ceilingRow.slider.doubleValue = -2; ceilingRow.slider.update()
    try require(limiterEdit?.0 == 5 && limiterEdit?.1 == -2, "Limiter ceiling edits retain their API parameter ID")
    maximizer.search.stringValue = ""; maximizer.filterParameters()
    let busCompressor = PluginEditor(frame: .zero)
    let busValues = [
      dynamicParameter(0, "Enabled", 0, 1, 1, "", ["Off", "On"]),
      dynamicParameter(1, "Threshold", -96, 0, -18, "dB"),
      dynamicParameter(2, "Ratio", 1, 40, 4),
      dynamicParameter(3, "Attack", 0.1, 200, 10, "ms"),
      dynamicParameter(4, "Release", 5, 5000, 100, "ms"),
      dynamicParameter(5, "Makeup", -24, 24, 0, "dB"),
      dynamicParameter(6, "Knee", 0, 24, 6, "dB"),
      dynamicParameter(7, "Response", 0, 2, 0, "", ["Adaptive", "Feedback", "Feedforward"]),
      dynamicParameter(8, "Stereo link", 0, 100, 100, "%"),
      dynamicParameter(9, "Detector source", 0, 1, 1, "", ["Internal", "External sidechain"]),
      dynamicParameter(10, "Detector high-pass", 20, 20000, 20, "Hz"),
      dynamicParameter(11, "Detector low-pass", 20, 20000, 20000, "Hz"),
      dynamicParameter(12, "Detector filters", 0, 1, 0, "", ["Off", "On"]),
      dynamicParameter(13, "Listen to detector", 0, 1, 0, "", ["Off", "On"]),
      dynamicParameter(14, "Dry / wet", 0, 100, 100, "%"),
      dynamicParameter(15, "RMS window", 1, 100, 10, "ms")]
    busCompressor.update(model: PatternModel(["nativePlugins": [["name": "Bus Compressor", "format": "Built-in"]]]), values: busValues)
    busCompressor.showMeters(meterData)
    busCompressor.search.stringValue = "Response"; busCompressor.filterParameters()
    var busEdit: (Int, Double)?
    busCompressor.onParameter = { _, id, value, _ in busEdit = (id, value) }
    let responseRow = busCompressor.tableView(busCompressor.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    responseRow.slider.doubleValue = 2; responseRow.slider.update()
    try require(busEdit?.0 == 7 && busEdit?.1 == 2 && responseRow.reading.stringValue == "Feedforward", "Bus response dispatches its stable API ID and choice label")
    busCompressor.search.stringValue = "RMS window"; busCompressor.filterParameters()
    let rmsRow = busCompressor.tableView(busCompressor.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    rmsRow.slider.doubleValue = 60; rmsRow.slider.update()
    try require(busEdit?.0 == 15 && busEdit?.1 == 60, "Last bus-compressor parameter remains reachable in compact layouts")
    busCompressor.search.stringValue = ""; busCompressor.filterParameters()
    let orders = OrderEditor(frame: .zero)
    orders.update(model, selected: 4999)
    let tools = PatternToolsPanel(frame: .zero)
    tools.onContext = { (model, ["startRow": 2, "endRow": 12, "startChannel": 1, "endChannel": 2, "cursorChannel": 2]) }
    var requests = [[String: Any]]()
    tools.onRequest = { params, reply in
      requests.append(params)
      reply(["result": ["revision": "prepared-revision", "data": ["changedCells": 1,
        "changes": [["pattern": 0, "row": 2, "channel": 1,
          "before": ["note": 49, "volumeCommand": 1, "volume": 4],
          "after": ["note": 49, "volumeCommand": 1, "volume": 64]]]]]])
    }
    tools.apply()
    try require(requests.isEmpty, "Apply requires a successful preview")
    tools.preview()
    try require(requests.count == 1 && tools.applyButton.isEnabled && !tools.details.string.isEmpty, "Preview displays changes and enables apply")
    try require(requests[0]["startRow"] as? Int == 2 && requests[0]["rowCount"] as? Int == 11 && requests[0]["channelCount"] as? Int == 2, "Selection bounds become an exact command region")
    tools.apply()
    try require(requests.last?["expectedRevision"] as? String == "prepared-revision" && requests.last?["dryRun"] as? Bool == false, "Apply retains the preview's revision")
    tools.preview()
    tools.amount.stringValue = "3"
    tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    try require(!tools.applyButton.isEnabled, "Changing settings invalidates the preview")
    tools.scope.selectItem(at: 3)
    let songRequest = try tools.parameters()
    for (index, operation) in [(12, "insertRows"), (13, "deleteRows")] {
      tools.operation.selectItem(at: index); tools.scope.selectItem(at: 0); tools.settingsChanged()
      tools.amount.stringValue = "2"; tools.allowLoss.state = .on
      for (field, button) in tools.fieldButtons.enumerated() { button.state = field < 2 ? .on : .off }
      let rows = try tools.parameters()
      try require(rows["operation"] as? String == operation && rows["amount"] as? Double == 2 &&
        rows["rowCount"] as? Int == 11 && rows["fields"] as? [String] == ["note", "instrument"] &&
        rows["allowDataLoss"] as? Bool == true, "Row tools send exact selected bounds, masks, count and explicit loss choice")
    }
    try require(songRequest["pattern"] == nil && songRequest["rowCount"] == nil, "All-pattern scope does not carry stale selection bounds")
    let annotated = PatternModel(["orders": [0, 0, 1],
      "patterns": [["index": 0, "rows": 64, "id": "n1", "name": "Theme", "annotation": "Shared"], ["index": 1, "rows": 64, "id": "n2"]],
      "orderMetadata": [["id": "n3", "name": "Intro"], ["id": "n4", "name": ""], ["id": "n5", "name": "Chorus"]],
      "revisionToken": "metadata-revision"])
    let labels = OrderEditor(frame: .zero)
    labels.update(annotated, selected: 0)
    var annotation: [String: Any] = [:]
    labels.onAnnotate = { annotation = $0 }
    labels.sectionName.stringValue = "Opening"; labels.saveSection()
    try require(annotation["id"] as? String == "n3" && annotation["expectedRevision"] as? String == "metadata-revision", "Section edits target stable identity and displayed revision")
    labels.navigateSection(1)
    try require(labels.selected == 2 && labels.sectionName.stringValue == "Chorus", "Next section navigates to its first order")
    labels.navigateSection(-1)
    try require(labels.selected == 0 && labels.patternNotes.stringValue == "Shared", "Previous section restores shared pattern details")
    let matrix = ArrangementMatrix(frame: .zero)
    matrix.totalChannels = 8
    var matrixRequests = [(String, [String: Any])] ()
    matrix.onRequest = { method, params, reply in
      matrixRequests.append((method, params))
      if method == "arrangement.matrix" {
        reply(["result": ["revision": "matrix-revision", "data": [
          "totalOrders": 2, "totalChannels": 8,
          "tracks": (0..<8).map { ["channel": $0, "name": "Track \($0 + 1)"] as [String: Any] },
          "orders": (0..<2).map { ["order": $0, "pattern": 0, "rows": 64,
            "blocks": (0..<8).map { ["channel": $0, "notes": 4, "bins": [1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0]] as [String: Any] }] as [String: Any] }
        ]]])
      } else { reply(["error": ["message": "Song changed"]]) }
    }
    matrix.load(); matrix.choose(order: 0, channel: 2); matrix.copyBlock()
    matrix.choose(order: 1, channel: 4); matrix.pasteBlock()
    try require(matrixRequests.last?.1["sourceChannel"] as? Int == 2 && matrixRequests.last?.1["targetChannel"] as? Int == 4 && matrixRequests.last?.1["expectedRevision"] as? String == "matrix-revision", "Block copy pins source revision and selected channels")
    try require(matrix.status.stringValue == "Song changed", "Matrix surfaces stale source errors")
    let drag = matrix.dragPayload(order: 0, channel: 2)!
    let beforeDrag = matrixRequests.count
    try require(matrix.drop(drag, order: 1, channel: 4, apply: false) && matrixRequests.count == beforeDrag, "Drag validation is read-only")
    try require(!matrix.drop(drag, order: 0, channel: 2, apply: false), "A block cannot be dropped onto itself")
    try require(!matrix.drop(drag, order: 2, channel: 4, apply: false), "Drag rejects destinations beyond the arrangement")
    try require(!ArrangementMatrix(frame: .zero).drop(drag, order: 1, channel: 4, apply: true), "Drag cannot cross documents")
    try require(matrix.drop(drag, order: 1, channel: 4, apply: true) && matrixRequests.last?.0 == "arrangement.copyBlock", "Drag commits through the shared API")
    matrix.revision = "new-revision"
    try require(!matrix.drop(drag, order: 1, channel: 4, apply: true), "A stale drag cannot replace newer edits")
    let envelopes = PatternAutomationEditor(frame: .zero)
    envelopes.onContext = { PatternModel(["pattern": 0, "nativePlugins": [["name": "Test gain", "instanceID": "gain-instance"]]]) }
    var envelopeRequests = [(String, [String: Any])]()
    envelopes.onRequest = { method, params, reply in
      envelopeRequests.append((method, params))
      var data: Any = [String: Any]()
      if method == "automation.pattern.get" { data = ["rows": 64, "lanes": []] as [String: Any] }
      if method == "plugin.parameters.get" { data = [["id": 7, "name": "Gain", "min": 0.0, "max": 1.0, "value": 0.5]] }
      reply(["result": ["revision": "envelope-revision", "data": data]])
    }
    envelopes.load()
    try require(envelopes.parameterID == 7, "Envelope parameter search and selection loads the first target")
    envelopes.ramp(false)
    try require(envelopes.hasDraft && envelopes.canvas.points.count == 2 && envelopes.canvas.points.last?.position == 16383, "Ramp generator covers the exact pattern endpoint")
    envelopes.canvas.selected = 0
    envelopes.canvas.replaceSelected(position: 16383, value: 0.5, curve: "linear")
    try require(envelopes.canvas.points[0].position == 0, "Canvas refuses duplicate point positions")
    envelopes.canvas.replaceSelected(position: 37, value: 0.25, curve: "smooth")
    envelopes.apply()
    let applied = envelopeRequests.first { $0.0 == "automation.pattern.set" }!.1
    try require(applied["plugin"] as? String == "gain-instance" && applied["parameter"] as? Int == 7 && applied["expectedRevision"] as? String == "envelope-revision", "Envelope Apply targets stable identity and displayed revision")
    try require((applied["points"] as? [[String: Any]])?.first?["position"] as? Int == 37, "Sub-row canvas edits reach the shared API")
    envelopes.search.stringValue = "missing"; envelopes.filter()
    try require(envelopes.table.numberOfRows == 0, "Envelope parameter search filters unavailable names")
    envelopes.search.stringValue = ""; envelopes.filter()
    envelopes.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
    envelopes.curve.selectItem(at: 3); envelopes.changeCurve(); envelopes.ramp(false)
    let mixer = MixerEditor(frame: .zero)
    var mixerRequests = [(String, [String: Any])](), mixerReplies = [([String: Any]) -> Void]()
    mixer.revision = "mixer-revision"
    mixer.update(["active": true, "buses": (0..<192).map { index in
      ["id": "n\(index + 1)", "name": index == 191 ? "Master" : "Track \(index + 1)",
        "kind": index == 191 ? "master" : "track", "output": index == 191 ? "" : "n192", "gainDB": 0.0,
        "width": 1.0, "inserts": [], "sends": []] as [String: Any]
    }, "plugins": [["id": "gain", "name": "Test gain", "instrument": false]], "instruments": []])
    mixer.onRequest = { method, params, reply in mixerRequests.append((method, params)); mixerReplies.append(reply) }
    mixer.control("gainDB", value: -3.0, final: false)
    mixer.control("gainDB", value: -6.0, final: false)
    mixer.finishGesture()
    try require(mixerRequests.count == 1 && mixerRequests[0].1["preview"] as? Bool == true, "Mixer coalesces live dragging while a request is pending")
    mixerReplies.removeFirst()(["result": ["revision": "mixer-revision", "data": [:]]])
    try require(mixerRequests.count == 2 && mixerRequests[1].1["preview"] as? Bool == false && mixerRequests[1].1["gainDB"] as? Double == -6,
      "Mixer sends the last gesture value in one committed API edit")
    mixer.control("pan", value: 0.25, final: true)
    mixerReplies.removeFirst()(["result": ["revision": "mixer-committed", "data": [:]]])
    try require(mixerRequests.count == 3 && mixerRequests[2].1["expectedRevision"] as? String == "mixer-committed",
      "A second gesture survives an outstanding commit and uses its resulting revision")
    mixerReplies.removeFirst()(["result": ["revision": "mixer-second", "data": [:]]])
    try require(mixer.selected?["gainDB"] as? Double == -6 && mixer.selected?["pan"] as? Double == 0.25 && mixer.table.numberOfRows == 192,
      "Mixer keeps all buses accessible and retains both gesture results")
    mixer.showMeters([["bus": "n1", "left": 0.5, "right": 0.25]])
    try require(mixer.meter.left == 0.5 && mixer.peak.stringValue.contains("-6.0"), "Selected bus meter uses its stable bus identity")
    let ports = PluginPortsEditor(frame: .zero)
    var portRequests = [(String, [String: Any])]()
    var portReply: (([String: Any]) -> Void)?
    let portData = (0..<32).map { index in
      ["index": index, "channels": index % 2 == 0 ? 2 : 1, "name": "Output \(index + 1)",
        "direction": "output", "active": index == 0, "supported": true] as [String: Any]
    }
    ports.onRequest = { method, params, reply in portRequests.append((method, params)); portReply = reply }
    ports.open(slot: 2)
    portReply?(["result": ["revision": "ports-revision", "data": ["plugin": "stable-plugin", "buses": portData]]])
    ports.setActive(row: 0, enabled: false)
    try require(portRequests.count == 1, "Main plugin output cannot be disabled")
    ports.setActive(row: 31, enabled: true)
    try require(portRequests.last?.1["outputs"] as? [Int] == [31] && portRequests.last?.1["expectedRevision"] as? String == "ports-revision",
      "Native bus toggles use original port index and revision through shared API")
    var changedPorts = portData; changedPorts[31]["active"] = true
    portReply?(["result": ["revision": "ports-committed", "data": ["buses": changedPorts]]])
    try require(ports.table.numberOfRows == 32 && ports.buses[31]["active"] as? Bool == true, "Auxiliary output activation refreshes native editor")
    ports.load()
    portReply?(["result": ["revision": "moved-revision", "data": ["plugin": "different-plugin", "buses": portData]]])
    try require(ports.buses.isEmpty && ports.status.stringValue.contains("moved"), "A reordered plugin cannot silently retarget the bus editor")
    ports.open(slot: 1)
    portReply?(["result": ["revision": "ports-revision", "data": ["plugin": "stable-plugin", "buses": changedPorts]]])
    let sidechains = SidechainEditor(frame: .zero)
    let sideData: [String: Any] = ["buses": [["id": "n1", "name": "Bass"], ["id": "n2", "name": "Kick"], ["id": "n3", "name": "Master"]],
      "plugins": [["id": "compressor", "slot": 1, "name": "Compressor", "audioBuses": [["direction": "input", "index": 1, "name": "Key input", "active": true]]]],
      "sidechains": []]
    sidechains.revision = "side-revision"; sidechains.update(sideData)
    var sideRequests = [(String, [String: Any])](), sideReplies = [([String: Any]) -> Void]()
    sidechains.onRequest = { method, params, reply in sideRequests.append((method, params)); sideReplies.append(reply) }
    sidechains.source.selectItem(at: 1); sidechains.gain.stringValue = "-6"; sidechains.pre.state = .on; sidechains.apply()
    try require(sideRequests.count == 1 && sideRequests[0].0 == "mixer.sidechains.set", "Sidechain editor uses shared API")
    let sideParams = sideRequests[0].1, sourceValues = sideParams["sources"] as? [[String: Any]] ?? []
    try require(sideParams["plugin"] as? String == "compressor" && sideParams["input"] as? Int == 1 && sideParams["expectedRevision"] as? String == "side-revision",
      "Sidechain editor preserves effect identity, native input index and revision")
    try require(sourceValues.first?["source"] as? String == "n2" && sourceValues.first?["gainDB"] as? Double == -6 && sourceValues.first?["preFader"] as? Bool == true,
      "Native pre-fader sidechain edits retain source and gain")
    sideReplies.removeFirst()(["result": ["revision": "side-applied", "data": [:]]])
    var appliedSide = sideData; appliedSide["sidechains"] = [["source": "n2", "plugin": "compressor", "input": 1, "gainDB": -6.0, "preFader": true, "enabled": true]]
    sideReplies.removeFirst()(["result": ["revision": "side-applied", "data": appliedSide]])
    sidechains.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false); sidechains.remove()
    try require((sideRequests.last?.1["sources"] as? [[String: Any]])?.isEmpty == true, "Removing selected sidechain sends an empty source list")
    sideReplies.removeFirst()(["result": ["revision": "side-removed", "data": [:]]])
    sideReplies.removeFirst()(["result": ["revision": "side-removed", "data": appliedSide]])
    try require(
      orders.table.selectedRow == 4999 && orders.table.numberOfRows == 5000,
      "Every order in a long arrangement remains reachable")
    try require(
      orders.sequencePicker.indexOfSelectedItem == 1,
      "Sequence selection is reflected in the arranger")
    try require(
      effects.undoButton.isEnabled && !effects.redoButton.isEnabled,
      "Effect history buttons reflect available actions")
    let browser = PluginBrowser(); browser.update(browserFixture())
    let mixerRouting = MixerEditor(frame: .zero)
    mixerRouting.update(["active": true, "buses": mixer.buses, "plugins": mixer.plugins, "instruments": mixer.sources])
    mixerRouting.viewMode.selectedSegment = 1; mixerRouting.changeViewMode()
    let timing = songTimingFixture()
    let performance = patternPerformanceFixture()
    for (editor, width, height) in [
      (recovery as NSView, 650.0, 430.0),
      (timing as NSView, 660.0, 560.0),
      (performance as NSView, 700.0, 780.0),
      (preciseNotesFixture() as NSView, 690.0, 720.0),
      (sampleBrowserFixture() as NSView, 980.0, 770.0),
      (multisampleImportFixture() as NSView, 820.0, 720.0),
      (instrumentEnvelopeFixture() as NSView, 680.0, 640.0),
      (pluginProgramsFixture() as NSView, 620.0, 520.0),
      (pluginInstrumentsFixture() as NSView, 620.0, 460.0),
      (browser as NSView, 760.0, 620.0),
      (sample as NSView, 729.0, 1400.0), (zoomedSample as NSView, 729.0, 1400.0), (instrument as NSView, 729, 900),
      (busCompressor as NSView, 729, 1020), (maximizer as NSView, 729, 680), (effects as NSView, 729, 600), (dynamics as NSView, 729, 1020), (cabinet as NSView, 729, 1020), (lofi as NSView, 1040, 940), (distortion as NSView, 1040, 790), (comb as NSView, 1040, 840), (equalizer as NSView, 1040, 790), (builtin as NSView, 729, 600), (orders as NSView, 760, 600),
      (tools as NSView, 720, 690), (matrix as NSView, 780, 450),
      (envelopes as NSView, 960, 620),
      (commands as NSView, 680, 500),
      (NoteTrackEditor(model: noteTrackModel(), channels: [0,1,2], creating: true) as NSView, 580, 370),
      (mixer as NSView, 1040, 650), (mixerRouting as NSView, 1040, 650), (ports as NSView, 600, 400), (sidechains as NSView, 720, 500),
    ] {
      let window = NSWindow(
        contentRect: NSRect(x: 0, y: 0, width: width, height: height), styleMask: [.titled],
        backing: .buffered, defer: false)
      // Pin the offscreen viewport: layout of an unpresented AppKit window can
      // otherwise shrink its content view to a transient fitting size while
      // leaving the window's frame unchanged.
      editor.widthAnchor.constraint(equalToConstant: width).isActive = true
      editor.heightAnchor.constraint(equalToConstant: height).isActive = true
      window.contentView = editor
      window.setContentSize(NSSize(width: width, height: height))
      editor.layoutSubtreeIfNeeded()
      try require(abs(editor.bounds.width - width) < 1 && abs(editor.bounds.height - height) < 1, "Offscreen qualification uses the requested content size")
      func check(_ view: NSView) throws {
        guard !view.isHiddenOrHasHiddenAncestor else { return }
        if let control = view as? NSControl, !(control is NSScroller) {
          let rect = control.convert(control.bounds, to: editor)
          let title = (control as? NSButton)?.title ?? (control as? NSTextField)?.stringValue ?? ""
          try require(
            rect.width > 0 && rect.height > 0 && rect.minX >= -1 && rect.maxX <= width + 1,
            "Control fits compact editor: \(type(of: editor)) / \(type(of: control)) \(title) \(rect)")
          if editor is MultisampleImportView || editor is SampleBrowser || editor is RecoveryBrowser || editor is PreciseNotesEditor || editor is MixerEditor || editor is InstrumentEnvelopeToolsEditor || editor is PluginProgramsEditor {
            try require(rect.minY >= -1 && rect.maxY <= height + 1,
              "Editor control fits vertically: \(type(of: control)) \(title) \(rect)")
          }
        }
        // Table rows and parameter lists have their own scrolling viewport.
        if view is NSScrollView { return }
        for child in view.subviews { try check(child) }
      }
      try check(editor)
      if let mixerView = editor as? MixerEditor, mixerView.viewMode.selectedSegment == 1 {
        func checkInspector(_ view: NSView) throws {
          if let control = view as? NSControl {
            let rect = control.convert(control.bounds, to: mixerView.inspector), bounds = mixerView.inspector.bounds
            try require(rect.width > 0 && rect.height > 0 && rect.minX >= -1 && rect.maxX <= bounds.width + 1 && rect.minY >= -1 && rect.maxY <= bounds.height + 1,
              "Scrolling mixer inspector control fits: \(type(of: control)) \(rect)")
          }
          for child in view.subviews { try checkInspector(child) }
        }
        try checkInspector(mixerView.inspector)
        mixerView.source.scrollToVisible(mixerView.source.bounds)
        try require(mixerView.source.visibleRect.height > 0, "Last routing controls remain reachable in compact mixer")
        mixerView.name.scrollToVisible(mixerView.name.bounds)
      }
      // Render our own test views into images without opening a window or taking
      // control of the desktop. This supplements real input/display qualification.
      if let index = CommandLine.arguments.firstIndex(of: "--snapshots"), index + 1 < CommandLine.arguments.count {
        let directory = URL(fileURLWithPath: CommandLine.arguments[index + 1], isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if let bitmap = editor.bitmapImageRepForCachingDisplay(in: editor.bounds) {
          editor.cacheDisplay(in: editor.bounds, to: bitmap)
          if let png = bitmap.representation(using: .png, properties: [:]) {
            let name = editor === mixerRouting ? "MixerRouting" : editor === zoomedSample ? "SampleDrawing" : editor === busCompressor ? "BusCompressor" : editor === maximizer ? "Maximizer" : editor === dynamics ? "Dynamics" : editor === cabinet ? "CabinetSimulator" : editor === lofi ? "LofiMat" : editor === distortion ? "Distortion" : editor === comb ? "CombFilter" : editor === equalizer ? "Equalizer" : editor === builtin ? "BuiltInEffects" : "\(type(of: editor))"
            try png.write(to: directory.appendingPathComponent("\(name).png"))
          }
        }
      }
      if editor === effects {
        try require(
          effects.parameters.view(atColumn: 0, row: 4095, makeIfNecessary: false) == nil,
          "Offscreen plugin controls are not created eagerly")
      }
    }
  }
  static func main() {
    _ = NSApplication.shared
    NSApp.setActivationPolicy(.prohibited)
    NSApp.appearance = NSAppearance(named: .darkAqua)
    let priorDefaults = UserDefaults.standard.volatileDomain(forName: UserDefaults.argumentDomain)
    defer {
      UserDefaults.standard.setVolatileDomain(priorDefaults, forName: UserDefaults.argumentDomain)
    }
    var defaults: [String: Any] = [
      "returnStartsPlayback": false,
      "noteKeysLow": "zsxdcvgbhnjm", "noteKeysHigh": "q2w3er5t6y7u",
    ]
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    do {
      try automationToolsChecks()
      try automationTargetChecks()
      try mixerStripsChecks()
      try pluginPresetChecks()
      try songTimingChecks()
      try pluginProgramsChecks()
      try preciseNotesChecks()
      try workspaceChecks()
      try signalGraphChecks()
      try sampleBrowserChecks()
      try parameterBoundaryChecks()
      try patternPerformanceChecks()
      try pluginInstrumentsChecks()
      try instrumentEnvelopeChecks()
      try envelopeBankChecks()
      try navigationChecks()
      try noteTrackChecks()
      try pluginBrowserChecks()
      let grid = PatternView()
      var changedModel = grid.model
      let originalNeighbor = changedModel.cell(0, 1)
      changedModel.replaceCell(0, 0, with: [49, 1, 1, 32, 0, 0])
      changedModel.replaceCell(0, changedModel.channels, with: [50, 2, 0, 0, 0, 0])
      try require(changedModel.cell(0, 0) == [49, 1, 1, 32, 0, 0] && changedModel.cell(0, 1) == originalNeighbor && grid.model.cell(0, 0) == [0, 0, 0, 0, 0, 0], "Incremental cell refresh preserves the source snapshot and neighboring cells")
      var transports = 0
      var undo = 0
      var redo = 0
      var messages = [String]()
      var auditions = [(Int, Bool)]()
      grid.onTransport = { transports += 1 }
      grid.onUndo = { undo += 1 }
      grid.onRedo = { redo += 1 }
      grid.onMessage = { messages.append($0) }
      grid.onAudition = { auditions.append(($0, $1)) }
      grid.onEdit = { row, channel, values in
        var next = grid.model
        let start = (row * next.channels + channel) * 6
        next.cells.replaceSubrange(start..<start + 6, with: values)
        grid.model = next
      }
      key(grid, 49, " ")
      key(grid, 49, " ", repeatKey: true)
      try require(transports == 1, "Space toggles transport once, without key-repeat toggling")
      defaults["returnStartsPlayback"] = true
      UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
      key(grid, 36, "\r")
      try require(transports == 2, "Configured Return transport works")
      key(grid, 6, "z")
      try require(
        grid.model.cell(0, 0)[0] == 49 && grid.cursorRow == 1, "C-4 entry and step advance")
      key(grid, 6, "z", repeatKey: true)
      try require(grid.cursorRow == 1, "Held notes do not repeat into the pattern")
      key(grid, 6, "z", up: true)
      try require(
        auditions.count == 2 && auditions[0].0 == 49 && auditions[0].1 && !auditions[1].1,
        "Note-on and matching note-off are paired")
      grid.instrument = 300
      key(grid, 6, "z")
      try require(
        grid.cursorRow == 1 && !messages.isEmpty,
        "Large sample index reports mapping need without trapping")
      grid.instrument = 1
      key(grid, 6, "z", flags: .command)
      key(grid, 6, "z", flags: [.command, .shift])
      try require(undo == 1 && redo == 1, "Undo and redo keyboard routing")
      var pending: ((PatternModel) -> PatternView.Edits)?
      grid.onTransform = { pending = $0 }
      grid.cursorRow = 0
      grid.transpose(12)
      try require(grid.model.cell(0, 0)[0] == 49 && pending != nil, "Bulk operation is deferred")
      let transpose = pending!(grid.model)
      try require(
        transpose.count == 1 && transpose[0].2[0] == 61,
        "Deferred transpose preserves musical interval")
      grid.selectRegion(from: (2, 1), to: (5, 3))
      var rowCommands = [[String: Any]]()
      grid.onRowShift = { rowCommands.append($0) }
      key(grid, 51, "")
      let clear = rowCommands.removeLast()
      try require(
        clear["operation"] as? String == "clear" && clear["rowCount"] as? Int == 4 && clear["channelCount"] as? Int == 3 && (clear["fields"] as? [String])?.contains("effect") == true,
        "Rectangular deletion clears source cells and every FX column in one shared operation")
      grid.model = PatternModel(["rows": 4, "channels": 2, "cells": Data(repeating: 0, count: 48)])
      grid.commandRevision = { "displayed-revision" }
      grid.onRowShift = { rowCommands.append($0) }
      grid.cursorRow = 2; grid.shiftRows(true); grid.shiftRows(false)
      try require(rowCommands.count == 2 && rowCommands[0]["operation"] as? String == "insertRows" &&
        rowCommands[0]["rowCount"] as? Int == 2 && rowCommands[0]["startRow"] as? Int == 2 &&
        rowCommands[0]["channelCount"] as? Int == 2 && rowCommands[0]["expectedRevision"] as? String == "displayed-revision" &&
        rowCommands[0]["allowDataLoss"] as? Bool == false && rowCommands[1]["allowDataLoss"] as? Bool == true,
        "Native row menus use pinned shared commands; insertion protects the tail and explicit deletion removes its row")
      grid.canEdit = { false }; grid.shiftRows(true)
      try require(rowCommands.count == 2, "Busy documents cannot dispatch row commands")
      grid.canEdit = { true }
      grid.selectRegion(from: (0, 0), to: (1000, 1000))
      key(grid, 51, "")
      let clipped = rowCommands.last!
      try require(
        clipped["rowCount"] as? Int == 4 && clipped["channelCount"] as? Int == 2,
        "Selections are bounded by the current pattern")
      let envelope = EnvelopeView(frame: NSRect(x: 0, y: 0, width: 600, height: 180))
      envelope.points = [[0, 64], [8, 32], [16, 0]]
      envelope.selectedNode = 1
      envelope.updateNode(tick: 999, value: 100)
      try require(
        envelope.points[1] == [15, 64], "Envelope edits respect adjacent nodes and value bounds")
      var removed = -1
      envelope.onRemove = { index, _ in removed = index }
      envelope.removeSelectedNode()
      try require(
        removed == 1 && envelope.points == [[0, 64], [16, 0]],
        "Envelope deletion reports the removed index")
      envelope.selectedNode = 0
      envelope.removeSelectedNode()
      try require(
        envelope.points == [[0, 0]], "Deleting the first node anchors the next node at zero")
      envelope.removeSelectedNode()
      try require(
        envelope.points.isEmpty && envelope.selectedNode == nil, "Deleting the final node is safe")
      envelope.points = [[0, 64], [0, 32], [0, 0]]
      envelope.selectedNode = 1
      envelope.updateNode(tick: 20, value: 48)
      try require(
        envelope.points == [[0, 64], [0, 48], [0, 0]],
        "Coincident imported envelope nodes retain their timing")
      try layoutChecks()
      print(
        "PASS editor layouts and native keyboard transport/remapping, note lifecycle, safe sample indices, undo routing, deferred bulk edits, selection bounds and envelope node editing"
      )
    } catch {
      fputs("FAIL: \(error)\n", stderr)
      exit(1)
    }
  }
}
