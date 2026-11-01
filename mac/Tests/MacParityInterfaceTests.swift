import AppKit

extension InterfaceTests {
  static func macParityChecks() throws {
    try songAutomationChecks()
    let returnPoint=EditorReturnPoint(navigation:EditorNavigation(pattern:0,row:8,channel:0),revision:"document:1",patterns:[["id":"p","index":0]],tracks:[["id":"t","index":0]])!
    let returned=returnPoint.resolve(revision:"document:2",patterns:[["id":"p","index":2]],tracks:[["id":"t","index":3]])
    try require(returned?.pattern==2 && returned?.channel==3 && returned?.row==8 && returned?.following==false,"Panel Return resolves original pattern and track identities after reordering")
    try require(returnPoint.resolve(revision:"document:2",patterns:[["id":"p","index":0]],tracks:[["id":"replacement","index":0]])==nil,"Panel Return cannot jump to a replacement track at the old index")
    let directory=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer {try? FileManager.default.removeItem(at:directory)}
    let file=directory.appendingPathComponent("layouts.json"),first=WorkspaceLayouts(file:directory.appendingPathComponent("layouts.json")),second=WorkspaceLayouts(file:directory.appendingPathComponent("layouts.json"))
    try first.save("Mix",state:["pins":["graph":true],"locations":["graph":"float"]])
    try require(first.layouts["Mix"]?["pins"] == nil,"Named layouts store presentation without replacing editor pins")
    do {try second.save("Stale",state:[:]);throw InterfaceFailure(message:"A stale layout writer must reject")} catch is WorkspaceLayouts.Failure {}
    try second.reload();try second.save("Graph",state:["bottom":"graph"])
    try require(WorkspaceLayouts(file:file).names==["Graph","Mix"],"Refreshing saved layouts preserves another session's named arrangements")
    try second.delete("Mix")
    try require(!WorkspaceLayouts.validName(" leading") && !WorkspaceLayouts.validName("line\nbreak") && WorkspaceLayouts.validName("Été"),"Layout names preserve Unicode and reject invisible ambiguity")
    let palette=WorkspaceCommandPalette(),exact=NSMenuItem(title:"Graph editing",action:#selector(NSObject.description),keyEquivalent:"g"),other=NSMenuItem(title:"Edit pattern",action:#selector(NSObject.description),keyEquivalent:"e")
    palette.table.dataSource=palette;palette.table.addTableColumn(NSTableColumn(identifier:.init("command")))
    palette.entries=[.init(item:other,path:"Graph / Edit pattern"),.init(item:exact,path:"Workspace / Graph editing")]
    palette.search.stringValue="graph edit";palette.filter()
    try require(palette.filtered.first?.item===exact,"Command titles rank ahead of scattered category words")
    palette.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false);palette.filter()
    try require(palette.filtered.indices.contains(palette.table.selectedRow) && palette.filtered[palette.table.selectedRow].item===other,"Refreshing command results preserves a deliberate selection")
    palette.search.stringValue="⌘g";palette.filter()
    try require(palette.filtered.count==1 && palette.filtered.first?.item===exact,"Command search includes displayed shortcuts")
    palette.search.stringValue="no such command";palette.filter()
    try require(palette.table.selectedRow == -1 && palette.status.stringValue=="No matching commands","Empty command searches clear selection and explain the result")
    var ownership = AuditionOwnership()
    let voice = AuditionOwnership.Voice(note: 61, instrument: 2, channel: 0)
    try require(ownership.press("keyboard:61", voice: voice).count == 1, "The first held source starts one preview voice")
    try require(ownership.press("midi:0:61", voice: voice).isEmpty && ownership.release("keyboard:61").isEmpty, "MIDI and keyboard share a held voice without retriggering or cutting one another")
    try require(ownership.release("midi:0:61").first?.on == false, "Only the final input owner releases the shared voice")
    _ = ownership.press("midi:0:61", voice: voice)
    _ = ownership.press("midi:1:61", voice: voice)
    try require(ownership.release(prefix: "midi:0:").isEmpty, "All-notes-off on one MIDI channel preserves another channel's hold")
    let replacement = AuditionOwnership.Voice(note: 61, instrument: 3, channel: 0)
    _ = ownership.press("keyboard:61", voice: replacement)
    try require(ownership.release("midi:1:61").isEmpty && ownership.release("keyboard:61").first?.voice == replacement, "An old MIDI key-up cannot cut a replacement sound on the same renderer slot")
    let order = OrderEditor(frame: .zero)
    var model = PatternModel(["orders": [0, 1], "revisionToken": "song:1:0:0",
      "orderMetadata": [["id": "n1", "name": "Intro"], ["id": "n2", "name": "Verse"]],
      "patterns": [["id": "n3", "index": 0, "name": "A"], ["id": "n4", "index": 1, "name": "B"]]])
    order.update(model, selected: 0); order.sectionName.stringValue = "Unfinished intro"; order.patternNotes.stringValue = "Keep this draft"
    order.update(model, selected: 1); order.sectionName.stringValue = "Unfinished verse"
    model.revisionToken = "song:2:0:0"; order.update(model, selected: 0)
    try require(order.sectionName.stringValue == "Unfinished intro" && order.patternNotes.stringValue == "Keep this draft", "Annotation drafts survive selection changes and unrelated song revisions")
    var submitted = [String: Any](), acknowledge: (([String: Any]) -> Void)?
    order.onAnnotate = { params, reply in submitted = params; acknowledge = reply }
    order.saveSection()
    try require(submitted["id"] as? String == "n1" && submitted["expectedRevision"] as? String == "song:1:0:0", "A retained section draft never silently adopts another revision or order")
    acknowledge?(["error": ["message": "Song changed"]]); order.reloadDetails()
    order.sectionName.stringValue = "Submitted intro"; order.saveSection(); order.sectionName.stringValue = "Newer intro"
    acknowledge?(["result": ["revision": "song:3:0:0"]])
    order.saveSection()
    try require(submitted["name"] as? String == "Newer intro" && submitted["expectedRevision"] as? String == "song:3:0:0", "Annotation completion acknowledges only the submitted text and keeps newer typing")
    acknowledge?(["error": ["message": "Retained"]]); order.update(model, selected: 1)
    try require(order.sectionName.stringValue == "Unfinished verse", "Another occurrence keeps its own independent annotation draft")

    let matrix = ArrangementMatrix(frame: .zero)
    var revision = "song:1:0:0", writes = 0, navigated = -1
    matrix.totalChannels = 24; matrix.currentRevision = { revision }
    matrix.onNavigate = { order, _ in navigated = order }
    matrix.onRequest = { method, params, reply in
      if method == "arrangement.matrix" {
        let start = params["startOrder"] as! Int, channel = params["startChannel"] as! Int
        reply(["result": ["revision": revision, "data": ["totalOrders": 66, "totalChannels": 24,
          "tracks": (channel..<min(channel + 12, 24)).map { ["channel": $0] },
          "orders": (start..<min(start + 64, 66)).map { ["order": $0, "pattern": $0 == 65 ? 65535 : 0, "rows": $0 == 65 ? 0 : 64,
            "blocks": (channel..<min(channel + 12, 24)).map { ["channel": $0, "notes": 0, "events": 3, "preciseEvents": 1, "nativeFxEvents": 2, "bins": [3]] }] }
        ]]])
      } else { writes += 1; reply(["error": ["message": "Rejected copy"]]) }
    }
    func key(_ code: UInt16, _ text: String = "", _ flags: NSEvent.ModifierFlags = []) {
      matrix.table.keyDown(with: NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0, context: nil, characters: text, charactersIgnoringModifiers: text, isARepeat: false, keyCode: code)!)
    }
    matrix.load(); matrix.choose(order: 63, channel: 11); key(124); key(125)
    try require(matrix.selectedOrder == 64 && matrix.selectedChannel == 12 && matrix.startOrder == 64 && matrix.startChannel == 12, "Arrow navigation crosses order and track pages while retaining the selected block")
    key(36); try require(navigated == 64, "Return opens the selected occurrence")
    key(8, "c", .command); key(125); key(36); key(9, "v", .command)
    try require(navigated == 64 && writes == 0 && matrix.copied?.order == 64, "End slots cannot open or receive a block and do not discard the copied source")
    key(126); revision = "song:1:0:1"; key(9, "v", .command)
    try require(writes == 0 && matrix.copied != nil, "Changing sequence rejects stale copied blocks before dispatch")
    key(96); key(8, "c", .command); key(9, "v", .command)
    try require(writes == 1 && matrix.copied != nil, "Explicit refresh and recopy use the new revision; rejected pastes retain their source")
    let block = matrix.tableView(matrix.table, viewFor: matrix.table.tableColumns[1], row: 0) as! MatrixBlock
    try require(block.notes == 0 && block.events == 3 && block.toolTip?.contains("2 native FX") == true, "Precise release and native-FX-only blocks expose their complete event counts")

    let notes = PreciseNotesEditor(frame: .zero)
    var row = 0, sound = 3, answer: (([String: Any]) -> Void)?
    notes.onContext = { (PatternModel(["revisionToken": "notes:1:0:0", "channels": 1]), row, 0, sound) }
    notes.onRequest = { _, _, reply in answer = reply }
    notes.capture(); sound = 7
    answer?(["result": ["revision": "notes:1:0:0", "data": ["pattern": 0, "events": []]]])
    try require(notes.instrument.stringValue == "3", "An empty precise-note row keeps the sound captured before the read")
    notes.offset.stringValue = "1/8"; notes.put(replacing: false)
    let oldDraft = notes.draft; row = 1; notes.capture()
    answer?(["error": ["message": "Read failed"]])
    try require(notes.capturedRow == 0 && notes.revision == "notes:1:0:0" && NSArray(array: notes.draft).isEqual(to: oldDraft), "Failed recapture preserves the original note target, revision and unsaved notes")

    let midi = MIDIRecordingEditor(frame: .zero)
    var sources: [[String: Any]] = [["id": 1, "name": "Keys"], ["id": 2, "name": "Keys"]]
    midi.onSources = { sources }; midi.configure(source: 2, armed: true, quantum: 0, columns: 3, latency: 1.5)
    midi.latency.stringValue = "unfinished"; sources.removeLast(); midi.rescan()
    try require(midi.source.selectedTag() == 2 && midi.source.titleOfSelectedItem?.contains("Disconnected") == true && midi.latency.stringValue == "unfinished", "MIDI rescan preserves duplicate source identities, disconnected selection and unfinished fields")
    var settings = 0; midi.onSettings = { _, _, _, _, _, reply in settings += 1; reply(nil) }; midi.applySettings()
    try require(settings == 0, "Invalid MIDI adjustment remains local")
    var acceptedArm=true,armReply:((String?)->Void)?
    midi.onAcceptedArm={acceptedArm};midi.onArm={value,reply in acceptedArm=value;armReply=reply}
    midi.toggleArm()
    try require(midi.settingsPending && !acceptedArm && midi.latency.stringValue=="unfinished","Immediate MIDI Arm uses accepted settings without parsing or replacing unfinished timing")
    armReply?(nil)
    try require(!midi.settingsPending && midi.armed.title=="Arm notes" && midi.latency.stringValue=="unfinished","MIDI Arm completion preserves raw timing and reflects acknowledged state")
    var takeReply: (([String: Any]) -> Void)?
    midi.onRequest = { _, _, reply in takeReply = reply }; midi.refresh()
    let event: [String: Any] = ["patternID": "n3", "track": "n5", "position": 32768, "note": 61, "instrument": 2, "velocity": 91]
    let take: [String: Any] = ["take": "take-a", "capturing": false, "events": [event], "eventCount": 1, "baseRevision": "song:1", "missingTime": 2, "inputError": "Input disconnected"]
    takeReply?(["result": ["revision": "song:1", "data": take]])
    midi.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false); midi.refresh(); takeReply?(["result": ["revision": "song:1", "data": take]])
    try require(midi.table.selectedRow == 0 && midi.latency.stringValue == "unfinished" && midi.takeStatus.stringValue.contains("Input disconnected"), "Take polling keeps event selection and settings text while showing loss reasons")
    try require(MIDIRecordingEditor.lossSummary([:]) == nil && MIDIRecordingEditor.lossSummary(["overflow": 1]) != nil, "Automatic finish distinguishes complete takes from each retained input-loss condition")
    let graphCurve = GraphEnvelopeEditor(frame: .zero)
    graphCurve.independent = true
    var curveReply: (([String: Any]) -> Void)?, curveRequests = 0, follows = true
    graphCurve.followsSelection = { follows }
    graphCurve.onRequest = { method, _, reply in
      if method == "graph.automation.get" { curveRequests += 1; curveReply = reply }
      else { reply(["error": ["message": "No preview needed"]]) }
    }
    let patterns: [[String: Any]] = [["index": 0], ["index": 1]]
    let points: [[String: Any]] = [["position": 0, "value": 0.2], ["position": 256, "value": 0.8]]
    let curveData: [String: Any] = ["result": ["revision": "curve:1", "data": ["rows": 64, "points": points]]]
    graphCurve.context(graph: "n10", node: ["id": "n11", "kind": "automation", "name": "Motion"], patterns: patterns, revision: "curve:1")
    curveReply?(curveData); graphCurve.canvas.selected = 1
    follows = false
    graphCurve.context(graph: "n20", node: ["id": "n21", "kind": "automation"], patterns: patterns, revision: "curve:1")
    try require(curveRequests == 1 && graphCurve.node == "n11", "A pinned graph curve keeps its independent source while routing selection changes")
    follows = true
    graphCurve.context(graph: "n20", node: ["id": "n21", "kind": "automation"], patterns: patterns, revision: "curve:1")
    curveReply?(["error": ["message": "Could not read new source"]])
    try require(graphCurve.graph == "n10" && graphCurve.node == "n11" && graphCurve.canvas.points.count == 2, "Failed graph-curve reads cannot attach old points to a new source")
    graphCurve.context(graph: nil, node: nil, patterns: [], revision: "curve:2")
    try require(!graphCurve.isHidden && graphCurve.node == "n11", "An independent curve remains visible when the graph selects a cable or non-envelope node")
    graphCurve.load(); curveReply?(curveData)
    try require(graphCurve.canvas.selected == 1, "Reloading the same graph curve preserves its selected point")
    graphCurve.markDraft()
    graphCurve.context(graph:"n20",node:["id":"n21","kind":"automation"],patterns:patterns,revision:"curve:2",force:true)
    try require(graphCurve.node=="n11" && graphCurve.hasDraft,"Explicit panel Follow retains an unfinished curve until it is saved or explicitly reloaded")
    let boundary=preciseNotesFixture();var noteReply:(([String:Any])->Void)?,noteWrites=0,boundaryResults=[Bool]()
    boundary.onRequest={_,_,reply in noteWrites+=1;noteReply=reply}
    boundary.velocity.stringValue="104"
    boundary.finishAutomaticEdit{boundaryResults.append($0)}
    try require(noteWrites==1 && boundaryResults.isEmpty,"Native Save waits for the typed precise note instead of racing its debounce")
    noteReply?(["result":["revision":"notes:2","data":[:]]])
    try require(boundaryResults==[true] && !boundary.hasDraft,"Native Save continues only after accepting the captured note edit")
    boundary.velocity.stringValue="unfinished";boundary.finishAutomaticEdit{boundaryResults.append($0)}
    try require(noteWrites==1 && boundaryResults.last==false && boundary.velocity.stringValue=="unfinished","Incomplete precise input blocks the composing boundary without replay or loss")
    boundary.velocity.stringValue="105";boundary.finishAutomaticEdit{boundaryResults.append($0)}
    noteReply?(["error":["message":"Stale notes"]]);boundary.finishAutomaticEdit{boundaryResults.append($0)}
    try require(noteWrites==2 && boundaryResults.last==false && boundary.hasDraft,"A refused precise write is retained and never replayed by another Save")
    boundary.showPlayback(document:"notes",pattern:0,position:128)
    try require(boundary.timeline.playbackOffset==0.5,"Precise playback cursor uses 256-unit telemetry independently of note selection")
    boundary.showPlayback(document:"replacement",pattern:0,position:128)
    try require(boundary.timeline.playbackOffset==nil,"Playback from a replacement document cannot illuminate an old captured row")
    print("PASS Mac parity: retained annotations, matrix keyboard/pages/events, atomic note recapture and MIDI take review")
  }
}
