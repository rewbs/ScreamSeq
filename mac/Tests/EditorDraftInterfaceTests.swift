import AppKit

private final class HistoryTextView: NSTextView {
  let history = UndoManager()
  override var undoManager: UndoManager? { history }
}

// Regressions for popups mapped by index, reloads that overwrote drafts, and
// related editor faults found in review.
extension InterfaceTests {
  private static func keyEvent(_ code: UInt16, flags: NSEvent.ModifierFlags = [], up: Bool = false, repeating: Bool = false) -> NSEvent {
    NSEvent.keyEvent(with: up ? .keyUp : .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0,
      context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: repeating, keyCode: code)!
  }
  private static func press(_ root: NSView, _ title: String) throws {
    func find(_ view: NSView) -> ActionButton? {
      if let button = view as? ActionButton, button.title == title { return button }
      for child in view.subviews { if let found = find(child) { return found } }
      return nil
    }
    guard let button = find(root) else { throw InterfaceFailure(message: "Missing button \(title)") }
    button.handler?()
  }
  private static func wait(_ seconds: Double) { RunLoop.current.run(until: Date().addingTimeInterval(seconds)) }

  static func editorDraftChecks() throws {
    try textHistoryChecks()
    try popupIdentityChecks()
    try mixerDraftChecks()
    try graphDraftChecks()
    try songGraphEnvelopeChecks()
    try automationDraftChecks()
    try pluginDraftChecks()
    try envelopeBankDraftChecks()
    print("PASS editor drafts: popups resolve stable IDs, reloads keep drafts, busy edits retry, Tab leaves canvases")
  }

  static func textHistoryChecks() throws {
    let text = HistoryTextView(frame:NSRect(x:0,y:0,width:300,height:80))
    text.isEditable=true;text.allowsUndo=true;text.isFieldEditor=true;text.string="0"
    try require(!EditorHistory.performTextHistory(in:text,redo:false) && !EditorHistory.performTextHistory(in:text,redo:true),
      "An untouched automatically focused field lets Undo and Redo reach the document")
    text.insertText("2",replacementRange:NSRange(location:0,length:1));text.breakUndoCoalescing()
    try require(text.string=="2" && EditorHistory.performTextHistory(in:text,redo:false) && text.string=="0",
      "A real field edit consumes Undo locally and restores its text")
    try require(EditorHistory.performTextHistory(in:text,redo:true) && text.string=="2",
      "Redo restores a real text edit rather than changing song history")
    text.history.removeAllActions()
    text.setMarkedText("x",selectedRange:NSRange(location:1,length:0),replacementRange:NSRange(location:0,length:1))
    try require(text.hasMarkedText() && EditorHistory.performTextHistory(in:text,redo:false) && text.hasMarkedText(),
      "An active input-method composition cannot fall through to song Undo")
    text.unmarkText();text.isEditable=false
    try require(!EditorHistory.performTextHistory(in:text,redo:false) && !EditorHistory.performTextHistory(in:nil,redo:false),
      "Read-only text and ordinary graph focus use document history")
  }

  static func songGraphEnvelopeChecks() throws {
    let editor=GraphEnvelopeEditor(frame:NSRect(x:0,y:0,width:850,height:260))
    var calls=[(String,[String:Any])](),pending:[([String:Any])->Void]=[]
    editor.onRequest={method,params,reply in
      calls.append((method,params))
      if method=="graph.automation.get" {reply(["result":["revision":"song:1","data":["rows":64,"points":[["position":0,"value":0.5,"curve":"linear"]]]]])}
      else if method=="graph.automation.set" {pending.append(reply)}
    }
    editor.context(graph:nil,node:["id":"n20","kind":"automation","name":"Song curve"],patterns:[["index":0]],revision:"song:1")
    try require(!editor.isHidden && editor.node=="n20" && calls.last?.1["graph"] is NSNull,"Song source opens the shared curve editor with an explicit null graph")
    editor.canvas.selected=0;editor.canvas.replaceSelected(position:128,value:0.3,curve:"smooth");editor.apply()
    try require(calls.last?.0=="graph.automation.set" && calls.last?.1["graph"] is NSNull && calls.last?.1["node"] as? String=="n20","Song curve gesture retains the stable root source and null graph")
    pending.removeFirst()(["result":["revision":"song:2","data":[:]]])
    editor.showBank()
    try require(calls.contains{call in call.0=="envelope.bank.list" && (call.1["target"] as? [String:Any])?["graph"] is NSNull && (call.1["target"] as? [String:Any])?["node"] as? String=="n20"},"Root curves can load and link song-bank templates")
    editor.bankWindow?.close();editor.resetDocument()
    try require(editor.isHidden && editor.node==nil && !editor.hasDraft,"Reset closes root source context without retaining a draft")
    editor.onRequest=nil
  }

  static func popupIdentityChecks() throws {
    let model = PatternModel(["channels": 4, "revisionToken": "r", "trackLayout": ["maximumColumns": 12, "noteTracks": [],
      "destinations": [["id": "a", "name": "Bus"], ["id": "b", "name": "Bus"], ["id": "c", "name": "Out"]]]])
    let track = NoteTrackEditor(model: model, channels: [0, 1], creating: false)
    var sent = [String: Any]()
    track.onRequest = { _, params, _ in sent = params }
    try require(track.destination.numberOfItems == 4, "Destinations sharing a name all stay in the track output popup")
    track.destination.selectItem(at: 3); track.apply()
    try require(sent["output"] as? String == "c", "Track output after a duplicate name resolves its own bus")
    let second = NoteTrackEditor(model: model, channels: [0, 1], creating: false)
    second.onRequest = { _, params, _ in sent = params }
    second.destination.selectItem(at: 2); second.apply()
    try require(sent["output"] as? String == "b", "The second of two equally named buses is addressable")

    let effects = PatternPerformanceEditor(frame: .zero)
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    effects.onContext = { (performanceModel(), 0, 0, 6) }
    effects.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    effects.capture()
    replies.removeFirst()(["result": ["revision": "song:1", "data": performanceData]])
    replies.removeFirst()(["result": ["revision": "song:1", "data": [["id": 7, "name": "Gain", "canSlide": true], ["id": 9, "name": "Gain", "canSlide": true]]]])
    try require(effects.parameter.numberOfItems == 2 && effects.parameter.selectedItem?.representedObject as? Int == 7, "Equally named parameters both remain selectable and the bound one is selected")
    effects.parameter.selectItem(at: 1); effects.selectParameter(); effects.apply(dryRun: true)
    try require((calls.last?.1["bindings"] as? [[String: Any]])?.first?["parameter"] as? Int == 9, "A duplicate-named parameter binds its own stable ID")

    let sequences = PatternModel(["orders": [0, 1], "patterns": [["index": 0, "rows": 64, "id": "p0", "name": "A"], ["index": 1, "rows": 64, "id": "p1"]],
      "orderMetadata": [["id": "o0", "name": "Intro"], ["id": "o1", "name": ""]],
      "sequences": [["name": "Main"], ["name": "Main"], ["name": "Alt"]], "sequence": 0, "revisionToken": "r"])
    let orders = OrderEditor(frame: .zero)
    orders.update(sequences, selected: 0)
    var sequence = -1
    orders.onSequence = { sequence = $0 }
    if sequences.sequences.count == 3 {
      try require(orders.sequencePicker.numberOfItems == 3, "Sequences sharing a name all stay in the picker")
      orders.sequencePicker.selectItem(at: 2)
      _ = orders.sequencePicker.sendAction(orders.sequencePicker.action, to: orders.sequencePicker.target)
      try require(sequence == 2, "Choosing a sequence after a duplicate name selects that sequence")
    }
    orders.sectionName.stringValue = "Verse"; orders.patternNotes.stringValue = "Typed notes"
    orders.update(sequences, selected: 0)
    try require(orders.sectionName.stringValue == "Verse" && orders.patternNotes.stringValue == "Typed notes" && orders.hasUncommittedDetails, "A refresh keeps uncommitted section and pattern text for the same order")
    orders.update(sequences, selected: 1)
    try require(orders.sectionName.stringValue == "" && orders.patternNotes.stringValue == "", "Another order shows its own details")

    let notes = PreciseNotesEditor(frame: .zero)
    notes.onContext = { (PatternModel(["rows": 64, "channels": 4]), 0, 0, 2) }
    notes.onRequest = { _, _, reply in
      var data = preciseData
      data["effects"] = [["command": 1, "label": "1xx", "name": "Slide", "description": "up"], ["command": 2, "label": "1xx", "name": "Slide", "description": "down"]]
      reply(["result": ["revision": "notes:1", "data": data]])
    }
    notes.capture()
    try require(notes.effect.numberOfItems == 3, "Effects with equal titles all stay in the precise-note popup")
    notes.effect.selectItem(at: 2); notes.changeEffect()
    try require(notes.effectHint.stringValue == "down", "The selected duplicate-titled effect is the one that is used")

    let picker = EnvelopeBankWindow.cataloguePicker([["id": "c1", "name": "Pluck"], ["id": "c2", "name": "Pluck"], ["id": "c3", "name": "Pad"]])
    picker.selectItem(at: 2)
    try require(picker.numberOfItems == 3 && picker.selectedItem?.representedObject as? String == "c3", "Replace catalogue entry addresses the chosen entry, not a shifted index")
  }

  static func mixerDraftChecks() throws {
    let mixer = MixerEditor(frame: .zero)
    mixer.revision = "r0"; mixer.busyRetryDelay = 0.01; mixer.busyRetryLimit = 2
    let data: [String: Any] = ["active": true, "buses": [
      ["id": "t1", "name": "Lead", "kind": "track", "output": "g1", "gainDB": 0.0, "pan": 0.0, "width": 1.0, "inserts": [], "sends": []],
      ["id": "g1", "name": "Bus", "kind": "group", "output": "m", "inserts": [], "sends": []],
      ["id": "g2", "name": "Bus", "kind": "group", "output": "m", "inserts": [], "sends": []],
      ["id": "m", "name": "Master", "kind": "master", "output": "", "inserts": [], "sends": []]],
      "plugins": [["id": "fx1", "name": "Gain", "instrument": false], ["id": "fx2", "name": "Gain", "instrument": false]]]
    mixer.update(data)
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    mixer.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    func ok(_ revision: String, _ data: [String: Any] = [:]) { replies.removeFirst()(["result": ["revision": revision, "data": data]]) }
    func busy() { replies.removeFirst()(["error": ["code": -32002, "message": "The document is busy; retry shortly"]]) }
    try require(mixer.output.numberOfItems == 4 && mixer.output.selectedItem?.representedObject as? String == "g1", "Equally named destinations stay distinct and the saved output is selected")
    mixer.output.selectItem(at: 2); mixer.name.stringValue = "Typed name"; mixer.effect.selectItem(at: 1)
    mixer.update(data)
    try require(mixer.output.indexOfSelectedItem == 2 && mixer.name.stringValue == "Typed name" && mixer.effect.indexOfSelectedItem == 1, "A refresh keeps an unapplied output choice, typed name and effect choice")
    _ = mixer.output.sendAction(mixer.output.action,to:mixer.output.target)
    try require(calls.last?.0 == "mixer.bus.set" && calls.last?.1["output"] as? String == "g2", "Route sends the chosen bus, not the one sharing its name")
    replies.removeFirst()(["error": ["code": -32000, "message": "No"]])
    try require(replies.isEmpty, "A rejected routing edit has nothing to resynchronize")
    let existing=mixer.insertMenu(nil).items.compactMap(\.submenu).flatMap(\.items).filter{$0.title=="Gain"}
    try require(existing.count==2,"Duplicate-named effects remain distinct menu entries")
    _ = NSApp.sendAction(existing[1].action!,to:existing[1].target,from:existing[1])
    try require(calls.last?.1["plugins"] as? [String] == ["fx2"], "Add effect uses the chosen plugin's stable ID")
    replies.removeFirst()(["error": ["code": -32000, "message": "No"]])

    mixer.control("gainDB", value: -3.27, final: true)
    try require(calls.last?.1["preview"] as? Bool == false && calls.last?.1["gainDB"] as? Double == -3.27, "Release commits the exact fader value")
    let sentBefore = calls.count
    func waitForRetry(_ condition:()->Bool) {
      let deadline=Date().addingTimeInterval(1)
      while !condition(),Date()<deadline {RunLoop.current.run(until:min(deadline,Date().addingTimeInterval(0.01)))}
    }
    busy();waitForRetry{calls.count>=sentBefore+1}
    try require(calls.count == sentBefore + 1 && calls.last?.1["gainDB"] as? Double == -3.27 && mixer.hasPendingControls, "A busy reply retries the same commit and keeps it pending")
    busy();waitForRetry{calls.count>=sentBefore+2}
    try require(calls.count==sentBefore+2 && replies.count==1,"The second busy retry sends exactly one pending commit")
    busy();waitForRetry{mixer.status.stringValue.contains("kept")}
    try require(replies.isEmpty && mixer.hasPendingControls && mixer.status.stringValue.contains("kept"), "Exhausted busy retries keep the fader commit instead of discarding it")
    mixer.synchronize("r0")
    try require(replies.isEmpty, "The deferred retry waits before asking again")
    wait(1.05); mixer.synchronize("r0")
    try require(calls.last?.1["gainDB"] as? Double == -3.27 && calls.last?.1["preview"] as? Bool == false, "The kept commit is sent once the wait has passed")
    ok("r1")
    try require(!mixer.hasPendingControls && mixer.selected?["gainDB"] as? Double == -3.27, "The retried commit reaches the document")

    mixer.control("pan", value: 0.5, final: true)
    replies.removeFirst()(["error": ["code": -32000, "message": "Song changed; reload"]])
    try require(calls.last?.1["preview"] as? Bool == true && calls.last?.1["pan"] as? Double == 0 && mixer.status.stringValue.contains("Song changed"), "A rejected edit previews the saved value so the engine matches the document")
    ok("r1")
    try require(!mixer.hasPendingControls && !mixer.loading, "Resynchronizing leaves no pending edit")

    let fader = mixer.controls[2], before = calls.count
    try require(fader.value.stringValue == "-3.27", "The inspector shows the saved fader value")
    fader.value.stringValue = "-3.27"; fader.entered()
    try require(calls.count == before, "Ending an edit without typing commits nothing")
    fader.value.stringValue = "-5"; fader.entered()
    try require(calls.count == before + 1 && calls.last?.1["gainDB"] as? Double == -5, "Typed text that differs is committed")
    ok("r2")
    mixer.showMeters([["bus": "t1", "left": 0.5, "right": 0.25]])
    mixer.peak.stringValue = "marker"; mixer.showMeters([["bus": "t1", "left": 0.5, "right": 0.25]])
    try require(mixer.peak.stringValue == "marker", "Unchanged meter levels do not rewrite the peak label")
    mixer.showMeters([["bus": "t1", "left": 1.0, "right": 0.25]])
    try require(mixer.peak.stringValue == "0.0 dB", "Changed meter levels update the peak label")
  }

  static func graphDraftChecks() throws {
    let editor = SignalGraphEditor(frame: .zero)
    let data: [String: Any] = ["library": [["id": "n100", "number": 1, "name": "Motion", "nodes": [
      ["id": "n101", "kind": "input", "name": "Input", "x": 30.0, "y": 60.0],
      ["id": "n102", "kind": "plugin", "name": "Filter", "x": 300.0, "y": 60.0],
      ["id": "n103", "kind": "output", "name": "Output", "x": 600.0, "y": 60.0],
      ["id": "n104", "kind": "lfo", "name": "Slow sweep", "x": 30.0, "y": 200.0]],
      "audio": [["source": "n101", "target": "n102", "input": 0, "output": 0], ["source": "n102", "target": "n103", "input": 0, "output": 0]],
      "modulation": [["source": "n104", "target": "n102", "parameter": 7]]]],
      "mixer": ["buses": [["id": "n1", "name": "Drums", "kind": "track", "output": "n2"], ["id": "n3", "name": "Bass", "kind": "track", "output": "n2"], ["id": "n2", "name": "Master", "kind": "master", "output": ""]]],
      "assignments": [], "commands": [], "lanes": []]
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    editor.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    editor.load(); replies.removeFirst()(["result": ["revision": "g:0", "data": data]]); calls = []
    editor.graphID = "n100"; editor.update(data)
    editor.disconnect()
    try require(calls.isEmpty && editor.status.stringValue.contains("Select a wire"), "Remove connection needs a wire the musician selected")
    editor.selectConnection(2); editor.update(data)
    try require(editor.canvas.selectedEdge == 2 && editor.connection.indexOfSelectedItem == 2, "A wire selection survives a rebuild by identity")
    var reordered = data, definition = (data["library"] as! [[String: Any]])[0]
    definition["audio"] = [["source": "n102", "target": "n103", "input": 0, "output": 0]]
    reordered["library"] = [definition]; editor.update(reordered)
    try require(editor.canvas.selectedEdge == 1 && editor.canvas.edges[1].modulation, "The selected wire is found again after the list shifts")
    definition["modulation"] = []; reordered["library"] = [definition]; editor.update(reordered)
    editor.disconnect()
    try require(editor.canvas.selectedEdge == nil && calls.isEmpty, "A wire that no longer exists is deselected, never replaced by its neighbour")

    editor.update(data); editor.selectedID = "n104"; editor.canvas.selected = "n104"; editor.inspect()
    editor.canvas.nudgeDelay = 5
    for index in 0..<3 { editor.canvas.keyDown(with: keyEvent(124, repeating: index > 0)) }
    try require(calls.isEmpty && editor.canvas.nodes.first { $0.id == "n104" }?.x == 42, "Repeated arrow nudges move the node without writing the definition each time")
    editor.canvas.keyUp(with: keyEvent(124, up: true))
    let moved = ((calls.last?.1["definition"] as? [String: Any])?["nodes"] as? [[String: Any]])?.first { $0["id"] as? String == "n104" }
    try require(calls.count == 1 && moved?["x"] as? Double == 42, "Releasing the key writes the move once")
    replies.removeFirst()(["error": ["message": "captured"]]); calls = []

    editor.update(data); editor.selectedID = "n104"; editor.inspect()
    editor.name.stringValue = "Typed"; editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: editor.name))
    editor.load()
    editor.moveNodes([("n101",99,77)])
    try require(calls.count == 1, "A move made while loading is not dropped into a second request")
    replies.removeFirst()(["result": ["revision": "g:1", "data": data]])
    try require(editor.name.stringValue == "Typed" && editor.hasDraft, "A reload keeps a typed node name")
    let queued = ((calls.last?.1["definition"] as? [String: Any])?["nodes"] as? [[String: Any]])?.first { $0["id"] as? String == "n101" }
    try require(calls.count == 2 && queued?["x"] as? Double == 99, "The queued move is sent after the load and the node stays where it was dropped")
    replies.removeFirst()(["error": ["message": "captured"]])
    editor.reload(); replies.removeFirst()(["result": ["revision": "g:1", "data": data]])
    try require(editor.name.stringValue == "Slow sweep" && !editor.hasDraft, "Reload discards typed inspector text")

    let last = editor.canvas.nodes.last!.id
    editor.canvas.selected = last; editor.canvas.keyDown(with: keyEvent(48))
    try require(editor.canvas.selected == last, "Tab past the last node leaves the canvas instead of wrapping")
    editor.canvas.selected = editor.canvas.nodes.first!.id; editor.canvas.keyDown(with: keyEvent(48))
    try require(editor.canvas.selected == editor.canvas.nodes[1].id, "Tab still steps through nodes")

    calls = []
    editor.graphID = nil; editor.filterID = nil; editor.update(data)
    let output = editor.songConnections.firstIndex { $0["kind"] as? String == "output" && $0["source"] as? String == "n1" }!
    editor.selectConnection(output)
    editor.picker(editor.source, editor.canvas.nodes.map { ($0.title, $0.id) }, select: "n3")
    editor.updateConnection()
    try require(calls.isEmpty && editor.status.stringValue.contains("source bus"), "Update wire cannot reroute a different bus through the Source popup")
    editor.selectConnection(output); editor.updateConnection()
    try require(calls.last?.0 == "mixer.bus.set" && calls.last?.1["bus"] as? String == "n1", "Update wire still edits the selected wire's own bus")
    replies.removeAll()

    let envelope = GraphEnvelopeEditor(frame: NSRect(x: 0, y: 0, width: 850, height: 260))
    envelope.onRequest = { _, _, reply in reply(["result": ["revision": "v1", "data": ["rows": 64, "rowsPerBeat": 4, "points": [["position": 0, "value": 0.5, "curve": "linear"]]]]]) }
    envelope.context(graph: "n100", node: ["id": "n105", "kind": "automation", "name": "Motion"], patterns: [["index": 0, "rows": 64]], revision: "v1")
    envelope.canvas.selected=0;envelope.showPoint()
    envelope.row.stringValue = "12"; envelope.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: envelope.row))
    envelope.value.stringValue = "80"; envelope.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: envelope.value))
    try require(envelope.hasDraft, "Typing a selected point retains its draft until the direct edit commits")
    envelope.onRequest = nil

    let model = PatternModel(["rows": 64, "channels": 4, "patterns": [["index": 0, "id": "n9"]]])
    let commands = GraphCommandsEditor(frame: .zero); commands.onContext = { (model, 0, 0) }; commands.preferredTarget = "n1"
    var respond: (([String: Any]) -> Void)?
    var graph = data; graph["lanes"] = [["target": "n1", "count": 3]]
    commands.onRequest = { _, _, reply in respond = reply }
    commands.capture(); respond?(["result": ["revision": "graph:1", "data": graph]])
    commands.amount.stringValue = "40"; commands.apply()
    commands.amount.stringValue = "55"
    respond?(["result": ["revision": "graph:2", "data": [:]]])
    respond?(["result": ["revision": "graph:2", "data": graph]])
    try require(commands.amount.stringValue == "55" && commands.hasDraft && commands.revision == "graph:2", "Text typed while a graph command was being saved stays as a draft")

    let lanes = PatternGraphHost(PatternView()).lanes
    try require(lanes.isAccessibilityElement(), "Graph lanes are exposed as an accessibility element")
  }

  static func automationDraftChecks() throws {
    let editor = PatternAutomationEditor(frame: .zero)
    editor.onContext = { PatternModel(["pattern": 0, "nativePlugins": [["name": "Test gain", "instanceID": "gain-instance"]]]) }
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    let lanes = [[String: Any]]()
    func answer() {
      let method = calls[calls.count - replies.count].0, reply = replies.removeFirst()
      var data: Any = [String: Any]()
      if method == "automation.pattern.get" { data = ["rows": 64, "lanes": lanes] as [String: Any] }
      if method == "plugin.parameters.get" { data = [["id": 7, "name": "Gain", "min": 0.0, "max": 1.0, "value": 0.5]] }
      if method == "automation.pattern.set" { data = ["lane": "lane1"] }
      reply(["result": ["revision": "rev", "data": data]])
    }
    editor.onRequest = { method, params, reply in calls.append((method, params)); replies.append(reply) }
    editor.load(); answer(); answer()
    try require(editor.parameterID == 7 && !editor.hasDraft, "Automation loads its first parameter")
    editor.ramp(false);editor.apply()
    try require(calls.last?.0=="automation.pattern.set" && editor.loading,"A direct gesture starts one save")
    editor.canvas.selected=0;editor.canvas.replaceSelected(position:512,value:0.25,curve:"linear")
    answer()
    try require(editor.hasDraft && editor.parameterID==7 && editor.laneID=="lane1" && editor.canvas.points.first?.position==512,
      "An in-flight save keeps the newer gesture and its target")
    editor.autoSaveWork?.cancel();editor.autoSaveWork=nil;editor.apply()
    try require(calls.last?.0=="automation.pattern.set" && (calls.last?.1["points"] as? [[String:Any]])?.first?["position"] as? Int==512,
      "The next save delivers the newer gesture")
    answer()
    try require(!editor.hasDraft && editor.canvas.points.first?.position==512,"An undisturbed save keeps the confirmed canvas")

    let canvas = AutomationCanvas(frame: NSRect(x: 0, y: 0, width: 400, height: 200))
    canvas.points = [EnvelopePoint(position: 0, value: 0, curve: "linear"), EnvelopePoint(position: 512, value: 1, curve: "linear")]
    canvas.keyDown(with: keyEvent(48)); canvas.keyDown(with: keyEvent(48))
    try require(canvas.selected == 1, "Tab steps through automation points")
    canvas.keyDown(with: keyEvent(48))
    try require(canvas.selected == 1, "Tab past the last point leaves the canvas instead of wrapping")
    canvas.keyDown(with: keyEvent(48, flags: .option))
    try require(canvas.selected == 0, "Option-Tab cycles within the curve")
    canvas.points = []; canvas.selected = nil; canvas.keyDown(with: keyEvent(48))
    try require(canvas.selected == nil, "Tab on an empty curve is not consumed")

    let envelope = EnvelopeView(frame: NSRect(x: 0, y: 0, width: 400, height: 200))
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 400, height: 200), styleMask: [.borderless], backing: .buffered, defer: false)
    window.contentView = envelope
    envelope.points = [[0, 32], [20, 32], [40, 32]]
    func mouse(_ type: NSEvent.EventType, _ point: NSPoint) -> NSEvent {
      NSEvent.mouseEvent(with: type, location: envelope.convert(point, to: nil), modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber,
        context: nil, eventNumber: 0, clickCount: 1, pressure: 1)!
    }
    var changes = 0
    envelope.onChange = { _ in changes += 1 }
    envelope.mouseDown(with: mouse(.leftMouseDown, envelope.location([40, 32])))
    try require(envelope.selectedNode == 2, "The envelope fixture picks up its last node")
    envelope.points = [[0, 32]]
    envelope.mouseDragged(with: mouse(.leftMouseDragged, NSPoint(x: 200, y: 50)))
    envelope.mouseUp(with: mouse(.leftMouseUp, NSPoint(x: 200, y: 50)))
    try require(envelope.points == [[0, 32]] && changes == 0, "Points replaced during a drag end the drag instead of indexing out of range")
  }

  static func pluginDraftChecks() throws {
    let editor = PluginEditor(frame: .zero)
    func model(_ ids: [String]) -> PatternModel { PatternModel(["nativePlugins": ids.map { ["name": "Plugin \($0)", "instanceID": $0, "format": "AU"] }]) }
    let values: [[AnyHashable: Any]] = [["id": 1, "name": "Gain", "min": 0.0, "max": 1.0, "value": 0.5]]
    editor.update(model: model(["a", "b", "c"]), values: values)
    editor.picker.selectItem(at: 2); editor.selectPlugin()
    editor.update(model: model(["a", "c", "b"]), values: values)
    try require(editor.selected == 1 && editor.selectedPlugin == "c" && editor.picker.indexOfSelectedItem == 1, "The rack selection follows a moved plugin")
    editor.selected = 0; editor.update(model: model(["b", "a", "c"]), values: values)
    try require(editor.selected == 0 && editor.selectedPlugin == "b", "A slot chosen from outside is resolved against the refreshed rack")
    editor.update(model: model(["a"]), values: values)
    try require(editor.selected == 0 && editor.selectedPlugin == "a", "A removed plugin falls back to a valid slot")

    var edits = [Double](), busy = true
    editor.onParameter = { _, _, value, _ in if !busy { edits.append(value) } }
    editor.canEdit = { !busy }; editor.parameterRetryDelay = 0.01
    let row = editor.tableView(editor.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    row.slider.doubleValue = 0.8; row.slider.update()
    try require(edits.isEmpty, "A busy document does not take the parameter edit")
    busy = false; wait(0.08)
    try require(edits == [0.8], "The edit is sent once the document is free instead of being lost")
    busy = true; editor.parameterRetryLimit = 2
    row.slider.doubleValue = 0.2; row.slider.update(); wait(0.12)
    try require(edits == [0.8] && row.slider.doubleValue == 0.8 && editor.note.stringValue.contains("busy"), "An edit that never gets through restores the real value and says so")
    busy = false
    row.slider.doubleValue = 0.2; row.slider.update()
    try require(edits == [0.8, 0.2], "The restored value is still the baseline for the next edit")

    row.reading.prepareEditing()
    try require(row.reading.stringValue == "0.2", "Focusing a value shows its full precision")
    row.reading.submit()
    try require(edits == [0.8, 0.2] && row.reading.stringValue == "0.2", "Leaving a focused value untouched is not an edit")

    let ports = PluginPortsEditor(frame: .zero)
    var requests = [[String: Any]](), reply: (([String: Any]) -> Void)?
    ports.onRequest = { _, params, respond in requests.append(params); reply = respond }
    ports.open(slot: 2); ports.open(slot: 4, name: "Older"); ports.open(slot: 5, name: "Synth")
    try require(requests.count == 1 && ports.queued?.slot == 5 && ports.status.stringValue.contains("Synth"), "A plugin asked for while loading is queued and announced")
    reply?(["result": ["revision": "p1", "data": ["plugin": "first", "buses": []]]])
    try require(requests.count == 2 && requests.last?["slot"] as? Int == 5 && ports.buses.isEmpty && ports.identity == nil, "The newest request opens next and the older reply is not shown as its buses")
    reply?(["result": ["revision": "p2", "data": ["plugin": "synth-id", "buses": [["index": 0, "direction": "output", "channels": 2, "active": true, "supported": true]]]]])
    try require(ports.identity == "synth-id" && ports.target.stringValue.contains("Synth") && ports.target.stringValue.contains("synth-id") && ports.buses.count == 1, "The window names the plugin it is editing")
  }

  static func envelopeBankDraftChecks() throws {
    let point: [String: Any] = ["position": 0, "value": 0.5, "curve": "linear"]
    let shape: [String: Any] = ["span": 16384, "points": [point]]
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    let target: [String: Any] = ["kind": "volume", "instrument": "n1"]
    let bank = EnvelopeBankWindow(title: "Draft test", target: target, shape: shape, revision: "r1", request: { method, params, reply in
      if method != "automation.formula.preview" { calls.append((method, params)); replies.append(reply) }
    })
    defer { bank.close() }
    let list: [String: Any] = ["entries": [["id": "n2", "name": "Master", "shape": shape]], "linkedTemplate": ""]
    replies.removeFirst()(["result": ["revision": "r1", "data": list]])
    var refusal = ""
    try require(EnvelopeBankWindow.reuse(bank, for: target, refused: { refusal = $0 }) && refusal.isEmpty, "Reopening the bank for the same envelope raises it")
    try require(!EnvelopeBankWindow.reuse(bank, for: ["kind": "pan", "instrument": "n1"], refused: { refusal = $0 }), "A clean bank for another envelope is replaced, not reused")
    bank.canvas.selected = 0; bank.canvas.replaceSelected(position: 256, value: 0.75, curve: "linear")
    try require(bank.dirty, "Editing the master marks the bank draft")
    try require(EnvelopeBankWindow.reuse(bank, for: ["kind": "pan", "instrument": "n1"], refused: { refusal = $0 }) && refusal.contains("draft"), "A bank holding a draft is not retargeted to another envelope")
    bank.reload(discard: true)
    replies.removeFirst()(["error": ["message": "Unavailable"]])
    try require(bank.dirty && bank.canvas.points.first?.position == 256, "A failed reload leaves the edited draft marked unsaved")
    bank.reload()
    replies.removeFirst()(["result": ["revision": "r1", "data": list]])
    try require(bank.dirty && bank.canvas.points.first?.position == 256 && bank.selected?["id"] as? String == "n2", "A background refresh keeps the draft and its template")
    bank.reload(discard: true)
    replies.removeFirst()(["result": ["revision": "r1", "data": list]])
    try require(!bank.dirty && bank.canvas.points.first?.position == 0, "Reload / discard restores the saved master")
  }
}
