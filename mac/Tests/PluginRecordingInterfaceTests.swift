import AppKit

private final class PluginTakeReplies {
  let view = PluginRecordingView(frame: NSRect(x: 0, y: 0, width: 760, height: 540))
  var calls = [(String, [String: Any])](), replies = [PluginRecordingView.Reply]()
  var document = "song-a", revision = "song-a:0:0"
  init() {
    view.onDocument = { [weak self] in self?.document }
    view.onPlugins = { [("plugin-a", "Lead"), ("plugin-b", "Delay")] }
    view.onRequest = { [weak self] method, params, reply in self?.calls.append((method, params)); self?.replies.append(reply) }
    view.loadScopes()
  }
  func state(_ token: String = "take-a", capturing: Bool = true, completed: Bool = false, loss: Int = 0) -> [String: Any] {
    if token.isEmpty { return ["take": "", "retained": false, "capturing": false] }
    return ["take": token, "retained": true, "capturing": capturing, "completed": completed, "compatible": true,
      "eventCount": 3, "rejected": 0, "failure": 0,
      "targets": [["plugin": "plugin-a", "parameter": 7, "eventCount": 3, "unmapped": loss, "overflow": 0]],
      "sources": [["plugin": "plugin-a", "lost": 0]]]
  }
  func answer(_ data: [String: Any]) { replies.removeFirst()(["result": ["revision": revision, "data": data]]) }
  func load(_ data: [String: Any]) { view.poll(explicit: true); answer(data) }
  var writes: [(String, [String: Any])] { calls.filter { $0.0 != "automation.recording.get" } }
}

extension InterfaceTests {
  static func pluginRecordingChecks() throws {
    let h = PluginTakeReplies(), view = h.view
    h.load(h.state(""))
    try require(view.record.isEnabled && !view.stop.isEnabled && !view.keep.isEnabled && h.writes.isEmpty,
      "Opening retained take review must not start recording or modify the song")
    view.scope.selectItem(at: 2); view.poll(); view.act(.start); view.act(.start)
    try require(view.pending && h.writes.isEmpty, "Record waits for its current read and duplicate clicks cannot duplicate intent")
    view.scope.selectItem(at: 1) // Even programmatic selection cannot redirect the captured scope.
    h.answer(h.state(""))
    try require(h.writes.count == 1 && h.writes[0].1["plugins"] as? [String] == ["plugin-b"] &&
      h.writes[0].1["expectedRevision"] as? String == "song-a:0:0", "Queued Record preserves original stable plugin scope and revision")
    h.answer(h.state())
    try require(view.stop.isEnabled && !view.record.isEnabled && !view.keep.isEnabled, "An active take exposes Stop and blocks another Record")
    view.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
    view.poll()
    try require(view.stop.isEnabled && !view.pending && view.table.selectedRow == 0,
      "Background reads preserve the selected target and do not disable Stop")
    view.act(.stop); view.act(.stop); h.answer(h.state())
    try require(h.writes.count == 2 && h.writes.last?.0 == "automation.recording.stop" &&
      h.writes.last?.1["take"] as? String == "take-a", "Stop during a read submits once against its captured take")
    h.answer(h.state(capturing: false))
    try require(view.keep.isEnabled && view.discard.isEnabled && !view.stop.isEnabled, "Stopped input stays available for deliberate Keep or Discard")
    view.act(.keep); h.revision = "song-a:1:0"
    var kept = h.state(capturing: false, completed: true); kept["restorationError"] = "Stop playback to finish restoration"
    h.answer(kept)
    try require(view.completed && view.keep.title == "Finish restoration" && !view.discard.isEnabled &&
      view.status.stringValue.contains("restoration"), "Committed music with retained cleanup must not be offered as a new Keep or Discard")
    let writeCount = h.writes.count; view.act(.discard)
    try require(h.writes.count == writeCount, "Even direct action dispatch cannot discard already-kept music")
    view.act(.keep)
    try require(h.writes.last?.1["expectedRevision"] as? String == h.revision && h.writes.last?.1["take"] as? String == "take-a",
      "Restoration uses the retained completed token at the post-commit revision")
    h.answer(h.state(""))
    try require(view.record.isEnabled && view.take.isEmpty && view.targets.isEmpty && !view.completed,
      "Verified release retires only the finished take and its displayed targets")

    for change in ["take", "revision", "document", "completed", "read-error"] {
      let guardCase = PluginTakeReplies(), v = guardCase.view
      guardCase.load(guardCase.state(capturing: false)); v.poll(); v.act(.discard)
      switch change {
      case "take": guardCase.answer(guardCase.state("replacement", capturing: false))
      case "revision": guardCase.revision = "song-a:9:0"; guardCase.answer(guardCase.state(capturing: false))
      case "document": guardCase.document = "song-b"; guardCase.answer(guardCase.state(capturing: false))
      case "completed": guardCase.answer(guardCase.state(capturing: false, completed: true))
      default: guardCase.replies.removeFirst()(["error": ["code": -32002, "message": "Read failed"]])
      }
      try require(guardCase.writes.isEmpty && !v.pending && !v.status.stringValue.isEmpty,
        "Queued Discard must refuse a changed \(change), never silently retarget or authorize after a failed read")
    }
    let lossCase = PluginTakeReplies(), lossView = lossCase.view
    lossCase.load(lossCase.state(capturing: false, loss: 1))
    try require(!lossView.keep.isEnabled && !lossView.acknowledge.isHidden, "Reported loss requires visible explicit acknowledgement")
    lossView.act(.keep); try require(lossCase.writes.isEmpty, "An unacknowledged Keep cannot bypass disabled controls")
    lossView.acknowledge.state = .on; lossView.poll(); lossView.act(.keep)
    lossCase.answer(lossCase.state(capturing: false, loss: 2))
    try require(lossCase.writes.isEmpty && lossView.acknowledge.state == .off && !lossView.keep.isEnabled,
      "New loss invalidates the original acknowledgement before queued Keep")
    lossView.acknowledge.state = .on; lossView.act(.keep)
    try require(lossCase.writes.count == 1 && lossCase.writes[0].1["acknowledgeLoss"] as? Bool == true,
      "An explicit acknowledgement travels with exactly the reviewed Keep request")
    lossCase.answer(lossCase.state(""))

    var incompatible = lossCase.state(capturing: false); incompatible["compatible"] = false
    lossCase.load(incompatible); lossView.acknowledge.state = .on; lossView.act(.keep)
    try require(!lossView.keep.isEnabled && lossCase.writes.count == 1 && lossView.discard.isEnabled,
      "Acknowledging loss cannot authorize Keep in an incompatible context")
    var providerLoss = lossCase.state(capturing: false)
    providerLoss["targets"] = []; providerLoss["eventCount"] = 0
    providerLoss["sources"] = [["plugin": "plugin-a", "lost": 5]]
    lossCase.load(providerLoss)
    try require(lossView.detail.stringValue.contains("5 source lost") && !lossView.acknowledge.isHidden && !lossView.keep.isEnabled,
      "Provider loss remains visible and requires acknowledgement even when no parameter input survived")

    for outcome in ["unknown", "committed", "notCommitted", "noChange", "unclassified"] {
      let uncertain = PluginTakeReplies(), v = uncertain.view
      uncertain.load(uncertain.state(capturing: false)); v.act(.keep)
      var error: [String: Any] = ["code": -32002, "message": "Completion needs inspection"]
      if outcome != "unclassified" { error["data"] = ["writeOutcome": outcome] }
      uncertain.replies.removeFirst()(["error": error])
      let count = uncertain.calls.count; v.poll(); v.act(.keep); v.act(.discard)
      try require(v.needsReadback && uncertain.calls.count == count && !v.keep.isEnabled && v.refresh.isEnabled,
        "A \(outcome) result never auto-replays input or permits a dependent write")
      v.poll(explicit: true); uncertain.answer(["retained": false])
      try require(v.needsReadback && !v.record.isEnabled, "A malformed readback cannot authorize a new take")
      v.poll(explicit: true); uncertain.answer(uncertain.state(capturing: false, completed: true))
      try require(!v.needsReadback && v.keep.title == "Finish restoration" && uncertain.writes.count == 1,
        "Explicit readback reconciles already-kept music without creating another transaction")
    }
    let focusCase = PluginTakeReplies(), focused = focusCase.view
    focusCase.load(focusCase.state())
    let window = NSWindow(contentRect: focused.frame, styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = focused
    window.makeFirstResponder(focused.table); focused.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
    focused.poll(); focusCase.answer(focusCase.state())
    try require(window.firstResponder === focused.table && focused.table.selectedRow == 0,
      "Unchanged take polling preserves native table focus and stable target selection")
    window.setContentSize(NSSize(width: 700, height: 500)); focused.layoutSubtreeIfNeeded()
    try require(focused.scope.frame.width > 0 && focused.keep.frame.width > 0 && focused.status.frame.width <= focused.bounds.width,
      "Compact retained take review keeps scope, actions and wrapped status inside its content")
    for control in [focused.scope, focused.keep, focused.stop, focused.status] as [NSView] {
      try require(focused.bounds.insetBy(dx: -1, dy: -1).contains(focused.convert(control.bounds, from: control)),
        "Compact take review cannot clip a native action, scope or status outside the window")
    }
    window.close()
  }
}
