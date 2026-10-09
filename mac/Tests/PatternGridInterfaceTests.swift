import AppKit

private final class UnavailableCommandTarget: NSObject, NSMenuItemValidation {
  var allowed = false, runs = 0
  @objc func runCommand(_ sender: Any?) { runs += 1 }
  func validateMenuItem(_ item: NSMenuItem) -> Bool { allowed }
}
extension InterfaceTests {
  static func presentationGateChecks() throws {
    let start=100.0,end=160.0,windowStart=start+2
    let ideal=(0..<3480).map{windowStart+0.005+Double($0)/60}
    func metrics(_ times:[Double])->PatternPresentationMetrics {
      PatternPresentationMetrics(timestamps:times,measurementStart:start,measurementEnd:end)
    }
    let exact=metrics(ideal)
    try require(exact.start==102 && exact.duration==58 && exact.expectedFrames==3480 &&
      exact.missedFrames==0 && exact.passesCadence,
      "A fixed two-second warmup leaves an explicit 58-second, phase-independent 60 Hz measurement")
    let reordered=metrics(Array(ideal.reversed())+Array(ideal.prefix(40))+[0,.nan,.infinity,-1,101,161])
    try require(reordered.timestamps==exact.timestamps && reordered.missedFrames==exact.missedFrames &&
      reordered.maxGapMS==exact.maxGapMS && reordered.passesCadence,
      "Callback order, duplicate delivery and invalid or out-of-window times cannot alter actual cadence")
    let slow=metrics((0..<3132).map{windowStart+0.005+Double($0)/54})
    try require(slow.gapMisses==0 && slow.rateDeficit==348 && slow.missedFrames==348 &&
      abs(slow.missedFraction-0.1)<0.000001 && !slow.passesCadence,
      "Steady 54 Hz fails the unchanged 0.1 percent gate even without a long individual interval")
    let tail=metrics(ideal.filter{$0<157}),head=metrics(ideal.filter{$0>=105})
    try require(tail.trailingGapMS>3000 && !tail.passesCadence && head.leadingGapMS>=3000 && !head.passesCadence,
      "Unpresented leading and terminal time remains part of the measured window")
    let allMissing=metrics([])
    try require(allMissing.missedFrames==3480 && allMissing.maxGapMS==58000 && !allMissing.passesCadence,
      "A completely stalled renderer cannot pass through an empty interval array")
    let boundaryTimes=(0..<3000).map{102.005+Double($0)/60}
    let below=PatternPresentationMetrics(timestamps:boundaryTimes.enumerated().filter{![100,1000].contains($0.offset)}.map(\.element),measurementStart:100,measurementEnd:152)
    let at=PatternPresentationMetrics(timestamps:boundaryTimes.enumerated().filter{![100,1000,2000].contains($0.offset)}.map(\.element),measurementStart:100,measurementEnd:152)
    try require(below.missedFrames==2 && below.passesCadence && at.missedFrames==3 &&
      at.missedFraction==0.001 && !at.passesCadence && at.maxGapMS<34,
      "The strict 0.1 percent boundary remains unchanged and independent of the 34 ms stall gate")
    let trace=PatternFrameTrace(capacity:4000)
    let ids=ideal.map{trace.begin(generation:7,callback:$0-0.05,deadline:$0-0.03,presentationTarget:$0,geometryPrepared:$0-0.06)}
    let mainAtFinish=Array(ideal.dropLast(120))
    // GPU handlers report physical times after finish; their delivery timestamp
    // must not replace the earlier physical presentation timestamp.
    for index in ids.indices {trace.update(ids[index]){$0.presented=ideal[index];$0.presentationCallback=160.2}}
    let drained=metrics(mainAtFinish+trace.snapshot(generation:7).map(\.presented))
    try require(!metrics(mainAtFinish).passesCadence && drained.timestamps==ideal && drained.passesCadence,
      "A fixed reporting drain recovers late callback delivery without inventing a physical terminal stall")
    let realStall=metrics(mainAtFinish+[160.01,160.1,160.2])
    try require(realStall.timestamps==mainAtFinish && !realStall.passesCadence,
      "Frames physically displayed after the frozen end cannot repair an actual terminal stall")
    let invalid=PatternPresentationMetrics(timestamps:ideal,measurementStart:100,measurementEnd:101)
    try require(!invalid.valid && !invalid.passesCadence,"A measurement shorter than its fixed warmup cannot qualify")
  }
  static func patternGridChecks() throws {
    try presentationGateChecks()
    var recovery=PatternSnapshotRecovery(interval:1.0/60)
    try require(recovery.enqueue(now:10.010,preparedAt:10,external:true,active:true)==nil,
      "A fresh host snapshot does not add another UI tick to normal display cadence")
    let delayed=recovery.enqueue(now:10.020,preparedAt:10,external:true,active:true)!
    try require(recovery.enqueue(now:10.040,preparedAt:10,external:true,active:true)==nil,
      "Several display callbacks coalesce into one delayed host refresh")
    try require(!recovery.shouldRefresh(delayed,now:10.041,preparedAt:10.040,external:true,active:true),
      "A normal tick which refreshes while recovery is queued prevents duplicate host work")
    recovery.finish(delayed)
    let stale=recovery.enqueue(now:10.065,preparedAt:10.040,external:true,active:true)!
    try require(recovery.shouldRefresh(stale,now:10.072,preparedAt:10.040,external:true,active:true),
      "Recovery remains eligible when the latest playhead snapshot is still stale on main")
    try require(!recovery.shouldRefresh(stale,now:10.072,preparedAt:10.040,external:true,active:false),
      "Paused and stopped presenters never refresh from a queued callback")
    recovery.cancel()
    let resumed=recovery.enqueue(now:10.080,preparedAt:10.040,external:true,active:true)!
    recovery.finish(stale)
    try require(resumed != stale && recovery.pending==resumed &&
      !recovery.shouldRefresh(stale,now:10.081,preparedAt:10.040,external:true,active:true),
      "A retired recovery cannot run or clear a newer request after pause and resume")
    recovery.finish(resumed)
    let standalone=recovery.enqueue(now:10.090,preparedAt:10.089,external:false,active:true)!
    try require(recovery.shouldRefresh(standalone,now:10.091,preparedAt:10.089,external:false,active:true),
      "Standalone views retain display-driven snapshot requests without a host tick")
    recovery.finish(standalone)
    try require(recovery.enqueue(now:10.100,preparedAt:nil,external:true,active:false)==nil &&
      recovery.enqueue(now:10.100,preparedAt:nil,external:true,active:true) != nil,
      "An active presenter recovers a missing snapshot, while an inactive presenter stays idle")
    let workTrace=UIWorkTrace(capacity:3)
    for index in 0..<5 {workTrace.record(.graphDraw,start:Double(index),end:Double(index)+0.25)}
    try require(workTrace.snapshot().map(\.sequence)==[2,3,4] && workTrace.overwritten==2,
      "Main-thread work evidence retains the newest bounded records in chronological order")
    try require(workTrace.snapshot().allSatisfy{$0.phase == .graphDraw && $0.durationMS==250},
      "Work trace timestamps preserve the duration and phase without clock conversion")
    workTrace.reset()
    try require(workTrace.snapshot().isEmpty && workTrace.overwritten==0,"Measurement reset does not leak setup work into the timeline")
    workTrace.record(.tick,start:10,end:11)
    try require(workTrace.snapshot().count==1 && workTrace.snapshot()[0].sequence==0,"A new measurement starts a fresh trace generation")
    // GPU and compositor completions may race and outlive the bounded history.
    // Their evidence must stay attached to the original drawable, never a newer
    // entry which happens to occupy the same ring slot.
    let trace=PatternFrameTrace(capacity:8)
    let retired=trace.begin(generation:0,callback:0,deadline:1,presentationTarget:2,geometryPrepared:0)
    var frameIDs=[Int]()
    for index in 1...8 {frameIDs.append(trace.begin(generation:1,callback:Double(index),deadline:Double(index)+1,presentationTarget:Double(index)+2,geometryPrepared:Double(index)-0.01))}
    trace.update(retired){$0.presented = -100}
    DispatchQueue.concurrentPerform(iterations:frameIDs.count*2){index in
      let frame=frameIDs[index/2]
      if index%2==0{trace.update(frame){$0.gpuEnd=Double(frame)+0.5}}
      else{trace.update(frame){$0.presented=Double(frame)+2}}
    }
    let captured=trace.snapshot(generation:1)
    try require(captured.count==8 && captured.map(\.sequence)==frameIDs && trace.snapshot(generation:0).isEmpty,
      "The frame timeline stays bounded and rejects an overwritten generation")
    try require(captured.allSatisfy{$0.gpuEnd==Double($0.sequence)+0.5 && $0.presented==$0.presentationTarget},
      "Racing GPU and presentation completions remain correlated to their own drawable")
    let prior = UserDefaults.standard.volatileDomain(forName: UserDefaults.argumentDomain)
    defer { UserDefaults.standard.setVolatileDomain(prior, forName: UserDefaults.argumentDomain) }
    var defaults = prior
    defaults["returnStartsPlayback"] = false
    defaults["noteKeysLow"] = "zsxdcvgbhnjm"; defaults["noteKeysHigh"] = "q2w3er5t6y7u"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    func freshModel(rows: Int = 8, channels: Int = 2, pattern: Int = 0, revision: String = "doc-a:1") -> PatternModel {
      PatternModel(["rows": rows, "channels": channels, "pattern": pattern, "revisionToken": revision,
        "cells": Data(repeating: 0, count: max(0, rows * channels * 6))])
    }
    let grid = PatternView()
    grid.frame = NSRect(x: 0, y: 0, width: 800, height: 400)
    grid.model = freshModel()
    var hostRefreshes=0
    grid.onNeedsRefresh={hostRefreshes+=1;grid.playRow=3;grid.playPattern=0}
    grid.refreshForPresentation()
    try require(hostRefreshes==1 && grid.playRow==3 && grid.playPattern==0,
      "Presentation recovery asks the host to advance playback state instead of restamping cached geometry")
    grid.onNeedsRefresh=nil;grid.playRow = -1;grid.playPattern = -1
    var edits = 0, auditions = [(Int, Bool)](), busy = false
    grid.onEdit = { row, channel, values in
      edits += 1
      var next = grid.model; next.replaceCell(row, channel, with: values); grid.model = next
    }
    grid.onAudition = { note, _, _, on in auditions.append((note, on)) }
    grid.canEdit = { !busy }

    // 1. Control/Option chords are never data entry in any field.
    try require(KeyboardSettings.isDataTyping([.shift]) && !KeyboardSettings.isDataTyping([.control]) &&
      !KeyboardSettings.isDataTyping([.option]) && !KeyboardSettings.isDataTyping([.command]), "One predicate defines musical typing")
    key(grid, 8, "c", flags: .control); key(grid, 9, "v", flags: .option)
    try require(edits == 0 && grid.cursorRow == 0 && auditions.isEmpty, "Control and Option chords do not enter notes")
    for column in 1...4 {
      grid.column = column
      key(grid, 8, "c", flags: .control); key(grid, 18, "1", flags: .option)
    }
    var effects = 0
    grid.onTrackerEffect = { _, _, _, _, _ in effects += 1 }
    try require(edits == 0 && effects == 0, "Control and Option chords do not enter instrument, volume or FX data")
    grid.column = 0
    key(grid, 125, "", flags: .control)
    try require(grid.cursorRow == 1, "Modified cursor keys keep navigating")
    grid.cursorRow = 0

    // 2 and 3. Typing while busy is queued in order, replayed when busy clears,
    // and a key already released is not auditioned on replay.
    busy = true
    key(grid, 6, "z"); key(grid, 6, "z", up: true)
    key(grid, 125, "")
    key(grid, 7, "x")
    try require(edits == 0 && grid.deferredKeyCount == 3 && grid.cursorRow == 0, "Busy typing waits instead of being dropped")
    key(grid, 49, " ")
    try require(grid.deferredKeyCount == 3, "Transport is never queued")
    busy = false; grid.replayDeferredKeys()
    try require(edits == 2 && grid.model.cell(0, 0)[0] == 49 && grid.model.cell(2, 0)[0] == 51 && grid.cursorRow == 3 && grid.deferredKeyCount == 0,
      "Queued notes and navigation replay in order")
    try require(auditions.count == 1 && auditions[0].0 == 51 && auditions[0].1, "A replayed key whose key-up already happened starts no audition")
    key(grid, 7, "x", up: true)
    try require(auditions.count == 2 && auditions[1].0 == 51 && !auditions[1].1, "A replayed key that is still held is released by its key-up")
    busy = true
    for _ in 0..<(PatternView.deferredKeyLimit + 5) { key(grid, 6, "z"); key(grid, 6, "z", up: true) }
    try require(grid.deferredKeyCount == PatternView.deferredKeyLimit, "The key queue is bounded")
    key(grid, 53, "\u{1b}")
    try require(grid.deferredKeyCount == 0, "Escape clears queued typing")
    key(grid, 6, "z"); grid.navigate(EditorNavigation(pattern: 0, row: 1, channel: 0, column: 0, following: false), clearSelection: true)
    try require(grid.deferredKeyCount == 0, "Explicit navigation clears queued typing")
    key(grid, 6, "z"); _ = grid.resignFirstResponder()
    try require(grid.deferredKeyCount == 0, "Leaving the grid clears queued typing")
    key(grid, 6, "z"); grid.model = freshModel(pattern: 1)
    try require(grid.deferredKeyCount == 0, "Changing pattern clears queued typing")
    key(grid, 6, "z"); grid.model = freshModel(pattern: 1, revision: "doc-b:1")
    try require(grid.deferredKeyCount == 0, "Changing document clears queued typing")
    var locked = freshModel(); locked.editable = false; grid.model = locked
    key(grid, 6, "z")
    try require(grid.deferredKeyCount == 0, "A document that is not editable queues nothing")
    busy = false; grid.model = freshModel(); edits = 0; auditions.removeAll(); grid.cursorRow = 0

    // 4. Auto-repeat of a key mapped to a note is consumed, including "1".
    defaults["noteKeysLow"] = "1sxdcvgbhnjm"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    key(grid, 18, "1")
    try require(edits == 1 && grid.model.cell(0, 0)[0] == 49 && grid.cursorRow == 1, "A remapped note key enters its note")
    key(grid, 18, "1", repeatKey: true)
    try require(edits == 1 && grid.model.cell(1, 0)[0] == 0 && grid.cursorRow == 1, "Repeat of a remapped note key writes no note-off")
    key(grid, 18, "1", up: true)
    defaults["noteKeysLow"] = "zsxdcvgbhnjm"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    key(grid, 18, "1")
    try require(grid.model.cell(1, 0)[0] == 255, "The default note-off key still works")

    // 5. Stored positions and clicks are clamped to the current pattern.
    grid.model = freshModel(rows: 4, channels: 2)
    grid.navigate(EditorNavigation(pattern: 0, row: 99, channel: 9, column: 40, following: false), clearSelection: true)
    try require(grid.cursorRow == 3 && grid.cursorChannel == 1 && grid.column == grid.model.lastField(1), "Navigation clamps to a pattern that shrank")
    grid.navigate(EditorNavigation(pattern: 0, row: -5, channel: -2, column: -1, following: false), clearSelection: true)
    try require(grid.cursorRow == 0 && grid.cursorChannel == 0 && grid.column == 0, "Navigation clamps negative positions")
    grid.model = freshModel(rows: 0, channels: 2)
    let click = NSEvent.mouseEvent(with: .leftMouseDown, location: NSPoint(x: 120, y: 200), modifierFlags: [], timestamp: 0,
      windowNumber: 0, context: nil, eventNumber: 0, clickCount: 1, pressure: 1)!
    grid.mouseDown(with: click)
    grid.navigate(EditorNavigation(pattern: 0, row: 3, channel: 1, column: 0, following: false), clearSelection: true)
    key(grid, 0, "a", flags: .command)
    try require(grid.cursorRow == 0 && grid.cursorChannel >= 0 && grid.automationSelection["endRow"] == 0, "An empty pattern never produces a negative cursor or selection")

    // 10. Negative counts from the bridge cannot trap.
    let negative = PatternModel(["rows": -3, "channels": -2])
    grid.model = negative
    try require(negative.rows == 0 && negative.channels == 0 && grid.positionLabels.isEmpty, "Negative bridge dimensions are clamped")
    grid.model = freshModel(rows: 2, channels: 1)
    grid.cursorRow = 1
    var fallback: ((PatternModel) -> PatternView.Edits)?
    grid.onTransform = { fallback = $0 }
    grid.pasteText("Resonance Pattern 1\n31,01,00,00,00,00\n32,01,00,00,00,00")
    if let fallback {
      let shrunk = fallback(freshModel(rows: 0, channels: 0))
      try require(shrunk.isEmpty, "Legacy paste fallback tolerates a smaller target")
    } else { throw InterfaceFailure(message: "Legacy paste fallback was not dispatched") }

    // 9. The current clipboard format is parsed off the main thread and applied
    // only to the unchanged target.
    grid.model = freshModel(rows: 8, channels: 2, revision: "doc-a:7")
    grid.commandRevision = nil; grid.cursorRow = 2; grid.cursorChannel = 1
    var pastes = [[String: Any]](), messages = [String]()
    grid.onPaste = { pastes.append($0) }
    grid.onMessage = { messages.append($0) }
    func settle(_ done: () -> Bool) {
      let end = Date().addingTimeInterval(2)
      while !done() && Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
    }
    let clip = "ScreamSeq Pattern 2\n{\"rows\":1,\"channels\":1,\"cells\":[[49,1,0,0,0,0]],\"effects\":[],\"bindings\":[]}"
    grid.pasteText(clip, mode: "mix")
    try require(pastes.isEmpty, "Clipboard JSON is not parsed synchronously on the main thread")
    settle { !pastes.isEmpty }
    try require(pastes.count == 1 && pastes[0]["startRow"] as? Int == 2 && pastes[0]["startChannel"] as? Int == 1 &&
      pastes[0]["mode"] as? String == "mix" && pastes[0]["expectedRevision"] as? String == "doc-a:7" && pastes[0]["rows"] as? Int == 1,
      "Parsed clipboard pastes at the captured cursor with the captured revision")
    grid.pasteText(clip)
    grid.model = freshModel(rows: 8, channels: 2, revision: "doc-a:8")
    settle { messages.contains { $0.hasPrefix("Paste cancelled") } }
    try require(pastes.count == 1 && messages.contains { $0.hasPrefix("Paste cancelled") }, "A paste cannot land after its target changed")
    grid.pasteText("ScreamSeq Pattern 2\n{\"rows\":1,\"surprise\":true}")
    settle { messages.contains("Invalid pattern clipboard.") }
    try require(pastes.count == 1 && messages.contains("Invalid pattern clipboard."), "Malformed clipboard JSON is rejected")

    // 6. Shortcuts on the plus key round-trip through the stored format.
    for text in ["cmd+shift++", "ctrl++", "+", "cmd+g", "ctrl+opt+space"] {
      try require(WorkspaceStroke(text)?.encoded == text, "Shortcut \(text) round-trips")
    }
    try require(WorkspaceStroke("cmd+shift++")?.key == "+" && WorkspaceStroke("cmd+shift++")?.modifiers == [.command, .shift], "A trailing plus is the key")
    for text in ["cmd+", "cmd+shift+", "", "cmd++g", "cmd+cmd++", "bogus++"] {
      try require(WorkspaceStroke(text) == nil, "Malformed shortcut \(text) is rejected")
    }
    let palette = WorkspaceCommandPalette()
    let target = UnavailableCommandTarget()
    let item = NSMenuItem(title: "Guarded", action: #selector(UnavailableCommandTarget.runCommand(_:)), keyEquivalent: "")
    item.target = target
    palette.entries = [.init(item: item, path: "Test / Guarded")]
    let id = palette.entries[0].id
    try require(palette.setShortcut(id, keys: ["cmd+shift++"], persist: false) == nil && item.keyEquivalent == "+" &&
      item.keyEquivalentModifierMask == [.command, .shift], "The plus key can be bound")
    try require(palette.setShortcut(id, keys: ["?"], persist: false) != nil, "Unmodified keys stay reserved for note entry when binding")
    try require(palette.setShortcut(id, keys: ["?"], persist: false, allowUnmodified: true) == nil && item.keyEquivalent == "?" &&
      item.keyEquivalentModifierMask.intersection(WorkspaceStroke.mask).isEmpty, "An unmodified default can be restored")

    // 7. Palette and sequences honour menu validation.
    try require(!palette.isAvailable(item, from: nil), "A command its target rejects is unavailable")
    palette.sequences.bindings = [id: [WorkspaceStroke("ctrl+g")!, WorkspaceStroke("m")!]]
    palette.sequences.onRun = nil
    palette.entries = [.init(item: item, path: "Test / Guarded")]
    target.allowed = true
    try require(palette.isAvailable(item, from: nil), "A validated command is available")
    let orphan = NSMenuItem(title: "Orphan", action: #selector(UnavailableCommandTarget.runCommand(_:)), keyEquivalent: "")
    try require(!palette.isAvailable(orphan, from: nil), "A command nothing responds to is unavailable")

    // 8. An Apply result is always reported; newer draft text is preserved.
    let tools = PatternToolsPanel(frame: .zero)
    tools.onContext = { (freshModel(), ["startRow": 0, "endRow": 1, "startChannel": 0, "endChannel": 0, "cursorChannel": 0]) }
    var replies = [PatternToolsPanel.Reply]()
    tools.onRequest = { _, reply in replies.append(reply) }
    let success: [String: Any] = ["result": ["revision": "r1", "data": ["changedCells": 3, "changes": [[String: Any]]()]]]
    tools.preview(); replies.removeFirst()(success)
    tools.apply()
    tools.amount.stringValue = "7"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(["error": ["message": "The song changed; preview again."]])
    try require(tools.summary.stringValue.contains("The song changed") && tools.amount.stringValue == "7" && !tools.applyButton.isEnabled && tools.previewButton.isEnabled,
      "An Apply error is shown even when settings were edited in flight")
    tools.preview(); replies.removeFirst()(success)
    tools.apply()
    tools.amount.stringValue = "9"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(success)
    try require(tools.summary.stringValue.contains("Applied 3 cell changes") && tools.amount.stringValue == "9" && !tools.applyButton.isEnabled,
      "An Apply success is shown and the newer draft needs its own preview")
    tools.preview()
    tools.amount.stringValue = "5"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(success)
    try require(!tools.applyButton.isEnabled && tools.summary.stringValue == "Preview a change before applying it.", "A stale preview still cannot be applied")
    try patternCursorEditingChecks()
    print("PASS pattern grid: modifier-safe typing, busy key queue, audition release, repeat handling, clamped navigation, plus-key shortcuts, palette validation, apply reporting, background paste, cursor-only field clear and channel row deletion")
  }
  static func patternCursorEditingChecks() throws {
    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:800,height:400)
    let commands:[[String:Any]]=[
      ["channel":1,"column":1,"position":2*65536,"kind":"tracker","effect":1,"parameter":37],
      ["channel":1,"column":2,"position":2*65536+8192,"kind":"nudge-reverse","value":0.75,"durationBeats":0.1875],
      ["channel":1,"column":3,"position":2*65536+4096,"kind":"parameter-slide","binding":7,"value":0.4,"duration":131072],
      ["channel":1,"column":4,"position":2*65536+2048,"kind":"pitch-slide","value":-3.0,"duration":65536,"pitchRange":12],
      ["channel":1,"column":5,"position":2*65536+16384,"kind":"note-cut"]]
    grid.model=PatternModel(["pattern":4,"revisionToken":"field:1","rows":8,"channels":3,"effectColumns":[1,8,1],"performanceCommands":commands,
      "cells":Data(repeating:0,count:8*3*6)])
    grid.model.replaceCell(2,1,with:[49,3,1,32,1,37])
    grid.cursorRow=2;grid.cursorChannel=1;key(grid,0,"a",flags:.command)
    grid.commandRevision={"field:latest"}
    var transforms=[[String:Any]](),effects=[[String:Any]](),unexpected=0
    grid.onRowShift={transforms.append($0)}
    grid.onEdit={_,_,_ in unexpected += 1};grid.onClearPreciseNotes={_,_ in unexpected += 1}
    grid.onTypedNativeEffect={_ in unexpected += 1};grid.onTrackerEffect={_,_,_,_,_ in unexpected += 1}
    grid.onAudition={_,_,_,_ in unexpected += 1}
    grid.onNudgeRequest={params,reply in effects.append(params);reply(["result":[:]])}
    for (column,field) in ["note","instrument","volume"].enumerated() {
      grid.column=column
      grid.parameterIndex=grid.parameterFields().firstIndex{$0.storage=="value" || $0.storage=="offset"} ?? 0
      key(grid,47,".")
      let edit=transforms.last!
      try require(edit["operation"] as? String=="clear" && edit["fields"] as? [String]==[field] &&
        edit["startRow"] as? Int==2 && edit["rowCount"] as? Int==1 && edit["startChannel"] as? Int==1 && edit["channelCount"] as? Int==1,
        "Dot clears only the cursor \(field), ignoring a larger selection")
    }
    for (column,kind) in [(4,"tracker"),(6,"tracker"),(8,"nudge-reverse"),(10,"parameter-slide"),(12,"pitch-slide"),(14,"note-cut")] {
      grid.column=column
      grid.parameterIndex=grid.parameterFields().firstIndex{$0.storage=="value" || $0.storage=="offset"} ?? 0
      key(grid,47,".")
      let edit=effects.last!,command=edit["command"] as! [String:Any]
      try require(edit["column"] as? Int==(column-3)/2 && command["kind"] as? String==kind,
        "Dot targets the exact FX value column, including extra FX")
      if kind=="tracker" {try require(command["effect"] as? Int==1 && command["parameter"] as? Int==0,"Tracker value clear keeps its command")}
      if kind=="nudge-reverse" {try require(command["value"] as? Double==0 && command["durationBeats"] as? Double==0.1875 && command["duration"]==nil && command["offset"] as? Int==8192,"Nudge strength clear retains duration and fractional onset")}
      if kind=="parameter-slide" {try require(command["value"] as? Double==0 && command["binding"] as? Int==7 && command["duration"] as? Int==131072 && command["offset"] as? Int==4096,"Parameter value clear retains binding, duration and onset")}
      if kind=="pitch-slide" {try require(command["value"] as? Double==0 && command["pitchRange"] as? Int==12 && command["duration"] as? Int==65536,"Pitch value clear retains range and duration")}
      if kind=="note-cut" {try require(command["offset"] as? Int==0 && command["value"]==nil,"Note-cut displayed offset resets to the row start")}
    }
    for column in [3,5,7,9,11,13] {
      grid.column=column
      grid.parameterIndex=grid.parameterFields().firstIndex{$0.storage=="value" || $0.storage=="offset"} ?? 0
      key(grid,47,".")
      try require(effects.last?["command"] is NSNull && effects.last?["column"] as? Int==(column-3)/2,"Dot on an FX code removes only that logical command")
    }
    grid.model.commands=PatternCommandCatalog(["effect":[["command":20,"parameterMask":240,"parameterValue":208,"label":"SDx"]]])
    grid.model.replaceCell(2,1,with:[49,3,1,32,20,211]);grid.column=4;key(grid,47,".")
    let extended=effects.last?["command"] as? [String:Any]
    try require(extended?["effect"] as? Int==20 && extended?["parameter"] as? Int==208,"Clearing SD3's value keeps the SD command and resets only its value to SD0")
    grid.column=3;key(grid,45,"n")
    try require(grid.effectPrefix=="N","Fixture begins a two-character FX command")
    key(grid,47,".")
    try require(grid.effectPrefix.isEmpty && unexpected==0,"Dot cancels an unfinished prefix without inserting or auditioning another command")
    for code:UInt16 in [51,117] {
      grid.column=3;key(grid,45,"n");key(grid,code,"",flags:.shift)
      let edit=transforms.last!
      try require(edit["operation"] as? String=="deleteRows" && edit["startRow"] as? Int==2 && edit["rowCount"] as? Int==6 &&
        edit["startChannel"] as? Int==1 && edit["channelCount"] as? Int==1 && edit["amount"] as? Int==1 && edit["allowDataLoss"] as? Bool==true && edit["fields"]==nil,
        "Shift+Delete moves every field of the current channel through the tail, regardless of selection")
    }
    try require(transforms.allSatisfy{$0["expectedRevision"] as? String=="field:latest"} && effects.allSatisfy{$0["expectedRevision"] as? String=="field:latest"} &&
      grid.cursorRow==2 && grid.cursorChannel==1 && grid.automationSelection==["startRow":0,"endRow":7,"startChannel":0,"endChannel":2],"Cursor edits retain location/selection and use the current revision")
    grid.column=0;key(grid,51,"")
    try require(transforms.last?["operation"] as? String=="clear" && transforms.last?["channelCount"] as? Int==3 && transforms.last?["rowCount"] as? Int==8,
      "Plain Delete still clears the rectangular selection")
    let count=transforms.count
    key(grid,47,".",flags:.control);key(grid,47,">",flags:.shift)
    try require(transforms.count==count,"Modified non-dot characters do not trigger field clearing")
    var busy=true;grid.canEdit = {!busy};key(grid,47,".");key(grid,51,"",flags:.shift)
    try require(grid.deferredKeyCount==2 && transforms.count==count,"Both cursor edits wait behind a pending document edit")
    busy=false;grid.replayDeferredKeys()
    try require(transforms.count==count+2 && grid.deferredKeyCount==0,"Queued cursor edits replay without being lost")
  }
}
