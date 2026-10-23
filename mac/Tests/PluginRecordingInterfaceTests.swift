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
  static func pluginSavedStateRecoveryChecks() throws {
    let h = PluginTakeReplies(), view = h.view
    var state = h.state(capturing: false); state["transportPlaying"] = false
    h.load(state); view.act(.restoreSavedState)
    try require(h.writes.isEmpty && view.restoreSaved.isHidden,
      "Saved-state recovery cannot keep or discard an unkept take")
    state["completed"] = true; state["transportPlaying"] = true
    h.load(state); view.act(.restoreSavedState)
    try require(h.writes.isEmpty && !view.restoreSaved.isHidden && !view.restoreSaved.isEnabled && view.discard.isHidden,
      "Completed recovery is distinct from Discard and requires stopped playback")
    state["transportPlaying"] = false; h.load(state)
    try require(view.restoreSaved.isEnabled && view.detail.stringValue.contains("abandons later unsaved"),
      "Completed recovery must expose its input-abandonment consequence")
    view.act(.restoreSavedState)
    try require(h.writes.count == 1 && h.writes[0].0 == "automation.recording.commit" &&
      h.writes[0].1["restoreSavedState"] as? Bool == true && h.writes[0].1["acknowledgeLoss"] == nil &&
      h.writes[0].1["take"] as? String == "take-a" && h.writes[0].1["expectedRevision"] as? String == h.revision,
      "Explicit recovery preserves the guarded take without acknowledging new musical loss")
    state["restorationPending"] = true; h.answer(state); h.load(state)
    try require(h.writes.count == 1 && view.retained && view.completed,
      "Explicit saved-state recovery must not automatically repeat after a pending reply")
    view.poll(); view.act(.restoreSavedState); state["transportPlaying"] = true; h.answer(state)
    try require(h.writes.count == 1 && !view.pending,
      "Playback restarting during readback must cancel queued saved-state recovery")
    state["transportPlaying"] = false; h.load(state); view.act(.restoreSavedState)
    h.replies.removeFirst()(["error": ["code": -32003, "message": "Unknown recovery completion"]])
    h.load(state)
    try require(h.writes.count == 2 && view.retained,
      "An uncertain saved-state recovery cannot be replayed by polling")
    view.act(.keep)
    try require(h.writes.last?.1["restoreSavedState"] == nil,
      "Ordinary Finish restoration must never inherit explicit saved-state abandonment")
    h.answer(h.state(""))
  }
  static func pluginRestorationRetryChecks() throws {
    for keep in [true, false] {
      let h = PluginTakeReplies(), view = h.view
      h.load(h.state(capturing: false)); view.act(keep ? .keep : .discard)
      h.revision = keep ? "song-a:1:0" : h.revision
      var waiting = h.state(capturing: false, completed: keep)
      waiting["restorationPending"] = true; waiting["releaseRequiresStoppedTransport"] = false
      h.answer(waiting)
      try require(h.writes.count == 1 && !view.pending && view.retained,
        "A definite pending restoration waits for the next poll rather than recursively submitting")
      h.load(waiting)
      try require(h.writes.count == 2 && h.writes.last?.0 == (keep ? "automation.recording.commit" : "automation.recording.discard") &&
        h.writes.last?.1["take"] as? String == "take-a" && h.writes.last?.1["expectedRevision"] as? String == h.revision,
        "Restoration retry preserves its authorized action, take and post-Keep revision")
      h.answer(h.state(""));h.load(h.state(""))
      try require(h.writes.count == 2 && !view.retained, "A successful restoration cannot repeat its action")
    }
    for refusal in ["unknown", "numeric", "stopped", "changed-take", "changed-revision", "changed-loss", "read-error"] {
      let h = PluginTakeReplies(), view = h.view
      h.load(h.state(capturing: false));view.act(.keep)
      var waiting = h.state(capturing: false, completed: true);waiting["restorationPending"] = true
      if refusal == "unknown" { h.replies.removeFirst()(["error": ["code": -32002, "message": "Unknown completion"]]) }
      else {
        if refusal == "numeric" { waiting["restorationPending"] = 1 }
        if refusal == "stopped" { waiting["releaseRequiresStoppedTransport"] = true }
        h.answer(waiting)
      }
      if refusal == "changed-take" { waiting["take"] = "new-take" }
      if refusal == "changed-revision" { h.revision = "song-a:9:0" }
      if refusal == "changed-loss" { waiting["rejected"] = 1 }
      if refusal == "read-error" {
        view.poll();h.replies.removeFirst()(["error": ["code": -32002, "message": "Read unavailable"]])
      }
      h.load(waiting)
      try require(h.writes.count == 1, "Restoration must not auto-replay after \(refusal)")
    }
    let bounded = PluginTakeReplies();bounded.load(bounded.state(capturing: false));bounded.view.act(.keep)
    var waiting = bounded.state(capturing: false, completed: true);waiting["restorationPending"] = true
    bounded.answer(waiting)
    for _ in 0..<8 { bounded.load(waiting);bounded.answer(waiting) }
    bounded.load(waiting)
    try require(bounded.writes.count == 9 && bounded.view.retained && !bounded.view.pending &&
      bounded.view.status.stringValue.contains("still waiting"), "Pending restoration retries must stop at their bounded limit")
  }
  static func pluginRecordingChecks() throws {
    try pluginSavedStateRecoveryChecks()
    try pluginRestorationRetryChecks()
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

    let failedClock = PluginTakeReplies(), failedView = failedClock.view
    var failedState = failedClock.state(); failedState["compatible"] = false
    failedState["transportPlaying"] = true; failedState["discardable"] = false
    failedClock.load(failedState); failedView.act(.discard)
    try require(failedClock.writes.isEmpty && !failedView.discard.isEnabled && !failedView.keep.isEnabled &&
      failedView.stopPlayback.isEnabled && !failedView.stopPlayback.isHidden,
      "Failed live recording exposes explicit playback Stop without enabling premature Discard")
    failedView.poll(); failedView.act(.stopPlayback); failedView.act(.stopPlayback)
    failedClock.answer(failedState)
    try require(failedClock.writes.count == 1 && failedClock.writes[0].0 == "transport.stop" &&
      failedClock.writes[0].1["expectedRevision"] as? String == failedClock.revision && failedClock.writes[0].1["take"] == nil,
      "Playback Stop waits for its read and sends one ordinary revision-guarded transport command")
    failedClock.answer(["playing": false])
    try require(failedView.polling && failedClock.writes.count == 1 && !failedView.discard.isEnabled,
      "Stopping playback reads the take and never automatically discards original input")
    var stoppedFailure = failedState; stoppedFailure["transportPlaying"] = false; stoppedFailure["discardable"] = true
    failedClock.answer(stoppedFailure)
    try require(failedView.capturing && failedView.discard.isEnabled && !failedView.keep.isEnabled &&
      !failedView.stopPlayback.isHidden && !failedView.stopPlayback.isEnabled,
      "The API's stopped failed-clock Discard remains available when the normal recording cut could not finish")
    failedView.act(.discard)
    try require(failedClock.writes.count == 2 && failedClock.writes.last?.0 == "automation.recording.discard" &&
      failedClock.writes.last?.1["take"] as? String == "take-a", "Recovery Discard preserves the original take identity")
    failedClock.answer(failedClock.state(""))
    try require(failedView.stopPlayback.isHidden, "Only verified take release removes its playback recovery action")

    for change in ["take", "transport", "discardable"] {
      let stale = PluginTakeReplies(), v = stale.view
      let discardCase = change == "discardable"
      stale.load(discardCase ? stoppedFailure : failedState); v.poll(); v.act(discardCase ? .discard : .stopPlayback)
      var changed = discardCase ? stoppedFailure : failedState
      if change == "take" { changed["take"] = "another-take" }
      else if change == "transport" { changed["transportPlaying"] = false }
      else { changed["discardable"] = false; changed["transportPlaying"] = true }
      stale.answer(changed)
      try require(stale.writes.isEmpty && !v.pending,
        "A queued recovery action cannot cross changed \(change) eligibility")
    }
    let uncertainStop = PluginTakeReplies(), uncertainStopView = uncertainStop.view
    uncertainStop.load(failedState); uncertainStopView.act(.stopPlayback)
    uncertainStop.replies.removeFirst()(["error": ["code": -32002, "message": "Unknown transport completion"]])
    uncertainStopView.act(.stopPlayback); uncertainStopView.act(.discard); uncertainStopView.poll()
    try require(uncertainStop.writes.count == 1 && uncertainStopView.needsReadback && uncertainStopView.refresh.isEnabled,
      "An uncertain playback Stop cannot be replayed or authorize dependent Discard")

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
    let native = PluginTakeReplies(), nativeView = native.view
    var nativeLoss: NSDictionary = ["lost": "0", "events": [["id": "original-1", "state": "unknown"]]]
    nativeView.onDelivery = { ["accepted": "2", "unknown": 1, "waiting": 0, "submitted": 0, "refused": 0, "lost": "0", "needsReview": true] }
    nativeView.onDeliveryLoss = { nativeLoss }; nativeView.onInputProtected = { true }
    native.load(native.state(capturing: false))
    try require(!nativeView.keep.isEnabled && !nativeView.acknowledge.isHidden && nativeView.recovery.isEnabled &&
      nativeView.inputDetail.stringValue.contains("1 uncertain"),
      "Original input uncertainty is visible, recoverable and included in Keep acknowledgement")
    nativeView.acknowledge.state = .on; nativeView.poll(); nativeView.act(.keep)
    nativeLoss = ["lost": "1", "events": [["id": "original-1", "state": "unknown"]]]
    native.answer(native.state(capturing: false))
    try require(native.writes.isEmpty && nativeView.acknowledge.state == .off,
      "Native queue loss changing during a read invalidates queued Keep independently of shared target counts")
    nativeView.saveRecovery()
    try require(native.writes.last?.0 == "recovery.save" && nativeView.pending,
      "Save recovery copy explicitly archives the stopped take and original input")
    native.answer(["lastCopy": "owned-copy.screamseq"])
    try require(!nativeView.pending && nativeView.status.stringValue.contains("Recovery copy saved"),
      "A confirmed recovery save reports completion without releasing the take")
    native.load(native.state("")); let beforeStart = native.writes.count; nativeView.act(.start)
    try require(!nativeView.record.isEnabled && native.writes.count == beforeStart,
      "An unresolved native journal cannot start a replacement take even if a read reports no shared take")

    let reviewCase = PluginTakeReplies(), reviewed = reviewCase.view
    let originalTime: UInt64 = 9_007_199_254_740_993
    var originalRows: [[String: Any]] = (0..<23).map { index in
      ["id": "input-\(index)", "plugin": "plugin-a", "parameter": 7, "value": Double(index) / 100,
       "hostTime": String(originalTime + UInt64(index)), "state": "refused", "error": "Existing envelope owns this parameter"]
    }
    reviewed.onDeliveryPage = { offset in
      ["document": "song-a", "take": "take-a", "total": originalRows.count,
       "events": Array(originalRows.dropFirst(offset).prefix(10))]
    }
    reviewCase.load(reviewCase.state(capturing: false))
    reviewed.reviewMode.selectItem(at: 1); reviewed.changeReviewMode()
    try require(reviewed.numberOfRows(in: reviewed.table) == 10 && !reviewed.previousInputs.isEnabled && reviewed.nextInputs.isEnabled,
      "Original-input review shows bounded pages without replacing the retained take")
    reviewed.table.selectRowIndexes(IndexSet(integer: 3), byExtendingSelection: false)
    reviewed.tableViewSelectionDidChange(Notification(name: NSTableView.selectionDidChangeNotification))
    let originalDetail = reviewed.inputDetail.stringValue
    reviewCase.load(reviewCase.state(capturing: false))
    try require(reviewed.table.selectedRow == 3 && reviewed.inputDetail.stringValue == originalDetail &&
      originalDetail.contains(String(originalTime + 3)) && originalDetail.contains("Existing envelope") && reviewed.inputDetail.isSelectable,
      "Polling preserves selected original input and copyable exact timestamp/error details")
    originalRows.removeFirst(); reviewCase.load(reviewCase.state(capturing: false))
    try require(reviewed.table.selectedRow == 2 && reviewed.inputDetail.stringValue == originalDetail,
      "Accepted-input retirement cannot retarget the selected original event")
    reviewed.changeInputPage(1); reviewed.changeInputPage(1)
    try require(reviewed.inputOffset == 20 && reviewed.inputRows.count == 2 && !reviewed.nextInputs.isEnabled && reviewCase.writes.isEmpty,
      "Last-page navigation cannot replay input or create musical history")
    reviewed.reviewMode.selectItem(at: 0); reviewed.changeReviewMode()
    try require(reviewed.numberOfRows(in: reviewed.table) == 1 && reviewed.previousInputs.isHidden && reviewed.nextInputs.isHidden,
      "Returning to captured parameters retains the shared target summary")

    let focusCase = PluginTakeReplies(), focused = focusCase.view
    focused.onDelivery = { ["accepted": "2", "unknown": 1, "needsReview": true] }
    focused.onInputProtected = { true }
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
    for control in [focused.scope, focused.keep, focused.stop, focused.recovery, focused.reviewMode, focused.inputDetail, focused.status] as [NSView] {
      try require(focused.bounds.insetBy(dx: -1, dy: -1).contains(focused.convert(control.bounds, from: control)),
        "Compact take review cannot clip a native action, scope or status outside the window")
    }
    var stoppedReview = focusCase.state(capturing: false); stoppedReview["transportPlaying"] = true
    focusCase.load(stoppedReview); focused.layoutSubtreeIfNeeded()
    try require(!focused.stopPlayback.isHidden && focused.stopPlayback.isEnabled &&
      focused.bounds.insetBy(dx: -1, dy: -1).contains(focused.convert(focused.stopPlayback.bounds, from: focused.stopPlayback)),
      "Compact stopped take exposes playback Stop for native restoration without clipping")
    let recoveryBounds = focused.convert(focused.stopPlayback.bounds, from: focused.stopPlayback)
    stoppedReview["transportPlaying"] = false
    focusCase.load(stoppedReview); focused.layoutSubtreeIfNeeded()
    try require(!focused.stopPlayback.isHidden && !focused.stopPlayback.isEnabled &&
      focused.convert(focused.stopPlayback.bounds, from: focused.stopPlayback) == recoveryBounds &&
      window.firstResponder === focused.table && focused.table.selectedRow == 0,
      "Successful playback Stop preserves compact recovery geometry and unrelated table focus until take release")
    focused.onDeliveryPage = reviewed.onDeliveryPage
    focused.reviewMode.selectItem(at: 1); focused.changeReviewMode()
    focused.table.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
    focused.tableViewSelectionDidChange(Notification(name: NSTableView.selectionDidChangeNotification))
    focused.layoutSubtreeIfNeeded(); focused.poll(); focusCase.answer(focusCase.state())
    try require(window.firstResponder === focused.table && focused.table.selectedRow == 0 &&
      focused.inputDetail.toolTip?.contains(String(originalTime + 1)) == true,
      "Original-input polling preserves keyboard focus and full copyable details in the compact window")
    for control in [focused.reviewMode, focused.previousInputs, focused.nextInputs, focused.inputDetail, focused.keep] as [NSView] {
      try require(focused.bounds.insetBy(dx: -1, dy: -1).contains(focused.convert(control.bounds, from: control)),
        "Compact original-input review must retain readable controls and details inside its content")
    }
    var completedReview = focusCase.state(capturing: false, completed: true)
    completedReview["transportPlaying"] = false
    focusCase.load(completedReview); focused.layoutSubtreeIfNeeded()
    try require(focused.restoreSaved.isEnabled && !focused.restoreSaved.isHidden && focused.discard.isHidden &&
      window.firstResponder === focused.table, "Completed recovery controls must retain table focus")
    for control in [focused.restoreSaved, focused.recovery, focused.keep, focused.detail] as [NSView] {
      try require(focused.bounds.insetBy(dx: -1, dy: -1).contains(focused.convert(control.bounds, from: control)),
        "Compact completed-take recovery must keep its explicit action and consequences readable")
    }
    window.close()
  }
}
