import Foundation
import CoreFoundation

extension InterfaceTests {
  static func pluginRecordingInputChecks() throws {
    typealias Adapter = PluginRecordingInput
    let original = PluginControlDelivery.Input(plugin: "plugin-a", parameter: 7, value: 0.4, timestamp: 9_007_199_254_740_993)
    let next = PluginControlDelivery.Input(plugin: "plugin-a", parameter: 7, value: 0.8, timestamp: original.timestamp + 1)
    let params: [String: Any] = ["expectedRevision": "song-a:4:2", "take": "take-a"]
    func state(capturing: Bool = true, retained: Bool = true, token: String = "take-a", revision: String = "song-a:4:2", accepted: Bool? = nil) -> [String: Any] {
      var data: [String: Any] = ["take": token, "retained": retained, "capturing": capturing,
        "baseRevision": "song-a:4:2", "compatible": true, "completed": false, "failure": 0,
        "sources": [["plugin": "plugin-a", "lost": 0]]]
      if let accepted { data["accepted"] = accepted }
      return ["result": ["revision": revision, "data": data]]
    }
    func refused(_ adapter: Adapter, _ input: PluginControlDelivery.Input) throws {
      do { _ = try adapter.capture(input); throw InterfaceFailure(message: "A protected native control unexpectedly accepted input") }
      catch is Adapter.Refusal {}
    }
    let adapter = Adapter()
    let manual = try adapter.capture(original)
    try require(!manual && !adapter.protectsDocument, "An idle adapter preserves ordinary manual parameter editing")
    adapter.observe("automation.recording.get", response: state())
    _ = try adapter.capture(original); _ = try adapter.capture(next)
    var calls = [(String, [String: Any])](), replies = [Adapter.Reply](), stopped: [String: Any]?
    let send: Adapter.Send = { method, parameters, reply in calls.append((method, parameters)); replies.append(reply) }
    adapter.request("automation.recording.stop", params: params) { stopped = $0 }
    try refused(adapter, next)
    adapter.pump(send); adapter.pump(send)
    try require(calls.count == 1 && calls[0].0 == "automation.recording.capture" &&
      calls[0].1["timestamp"] as? String == "9007199254740993" && stopped == nil,
      "Stop waits behind the exact first original input and does not submit a second in-flight request")
    replies.removeFirst()(state(accepted: true)); adapter.pump(send)
    try require(calls.count == 2 && calls[1].1["timestamp"] as? String == "9007199254740994",
      "Busy delivery preserves every original timestamp instead of coalescing or retiming controls")
    replies.removeFirst()(state(accepted: true)); adapter.pump(send)
    try require(calls.count == 3 && calls[2].0 == "automation.recording.stop" && stopped == nil,
      "Stop is submitted only after both original input completions")
    replies.removeFirst()(state(capturing: false))
    try require(stopped != nil && !adapter.changingState && !adapter.capturing && adapter.protectsDocument,
      "A stopped take retains its original input owner until explicit Keep or Discard")
    try refused(adapter, next)
    adapter.request("automation.recording.commit", params: params) { _ in }
    adapter.pump(send); replies.removeFirst()(state(capturing: false, retained: false, revision: "song-a:5:2"))
    try require(!adapter.protectsDocument && adapter.delivery.recovery == nil,
      "Only the verified matching lifecycle release retires original input evidence")

    let uncertain = Adapter(); uncertain.observe("automation.recording.get", response: state())
    _ = try uncertain.capture(original)
    uncertain.pump { _, _, done in done(["error": ["code": -32003, "message": "Unknown write outcome"]]) }
    var sends = 0
    uncertain.pump { _, _, _ in sends += 1 }
    uncertain.observe("automation.recording.get", response: state(capturing: false, retained: false, token: ""))
    try require(uncertain.uncertain && uncertain.protectsDocument && sends == 0 && uncertain.delivery.needsReview &&
      (uncertain.delivery.recovery?["events"] as? [[String: Any]])?.first?["hostTime"] as? String == String(original.timestamp),
      "A read reporting no take cannot erase or replay an uncertain original input")
    try refused(uncertain, next)
    uncertain.observe("automation.recording.get", response: state(capturing: false))
    var denied: [String: Any]?
    uncertain.request("automation.recording.commit", params: params) { denied = $0 }
    try require(denied?["error"] != nil && !uncertain.changingState, "Native delivery loss requires explicit acknowledgement before Keep")
    uncertain.request("automation.recording.discard", params: params) { _ in }
    uncertain.pump { _, _, done in done(state(capturing: false, retained: false, token: "different-take")) }
    try require(uncertain.uncertain && uncertain.delivery.retained == 1, "A mismatched release receipt cannot retire retained original input")

    // A Keep acknowledgement captured while input is draining cannot silently
    // cover a newly refused input. The original accepted music stays untouched.
    let changedLoss = Adapter(); changedLoss.observe("automation.recording.get", response: state())
    _ = try changedLoss.capture(original)
    var keepParams = params; keepParams["acknowledgeLoss"] = true
    var keepReply: [String: Any]?, methods = [String]()
    changedLoss.request("automation.recording.commit", params: keepParams) { keepReply = $0 }
    changedLoss.pump { method, _, done in methods.append(method); done(state(capturing: false, accepted: false)) }
    changedLoss.pump { method, _, _ in methods.append(method) }
    try require(methods == ["automation.recording.capture"] && keepReply?["error"] != nil && changedLoss.delivery.retained == 1,
      "New loss during drain invalidates the captured Keep acknowledgement without sending Commit")
    changedLoss.request("automation.recording.commit", params: keepParams) { keepReply = $0 }
    changedLoss.pump { method, _, done in methods.append(method); done(state(capturing: false, retained: false)) }
    try require(methods.last == "automation.recording.commit" && !changedLoss.protectsDocument,
      "A newly reviewed acknowledgement can finish the exact retained take")

    let start = Adapter(); var startReply: [String: Any]?
    // Only the literal boolean recovery intent may pass retained refused input
    // to the session's completed/stopped validation. It never replays the input.
    for flag in [false, 1, "true", true] as [Any] {
      let repair = Adapter(); repair.observe("automation.recording.get", response: state())
      _ = try repair.capture(original)
      repair.pump { _, _, done in done(state(capturing: false, accepted: false)) }
      var recoveryParams = params; recoveryParams["restoreSavedState"] = flag
      var recoveryReply: [String: Any]?, recoveryCalls = [String]()
      repair.request("automation.recording.commit", params: recoveryParams) { recoveryReply = $0 }
      repair.pump { method, _, done in
        recoveryCalls.append(method)
        // The native session remains authoritative for completed/stopped checks.
        done(["error": ["code": -32002, "message": "Already-kept take required"]])
      }
      let explicit = CFGetTypeID(flag as CFTypeRef) == CFBooleanGetTypeID() && (flag as? Bool == true)
      try require(recoveryCalls == (explicit ? ["automation.recording.commit"] : []) &&
        recoveryReply?["error"] != nil && repair.delivery.retained == 1,
        "Recovery intent must be strict, retain refused input and defer lifecycle validation to the session")
    }
    start.request("automation.recording.start", params: ["expectedRevision": "song-a:4:2", "plugins": ["plugin-a"]]) { startReply = $0 }
    try refused(start, original)
    start.pump { _, _, done in done(["error": ["code": -32003, "message": "Start completion unavailable"]]) }
    try require(start.uncertain && start.protectsDocument && startReply?["error"] != nil,
      "A failed lifecycle response requires explicit readback before manual controls resume")
    start.observe("automation.recording.get", response: state(capturing: false, retained: false, token: ""))
    try require(!start.protectsDocument, "Verified no-take readback permits manual editing after an uncertain start without original input")
    for method in ["automation.recording.start", "automation.recording.stop", "automation.recording.commit", "automation.recording.discard"] {
      var delivered = false
      start.request(method, params: params) { _ in }
      start.pump { actual, _, done in delivered = actual == method; done(["error": ["code": -32001, "message": "Stale or absent take"]]) }
      try require(delivered && !start.uncertain && !start.protectsDocument,
        "Without retained input, shared validation remains authoritative and a known conflict does not invent a protected take")
    }
    var invalid = state(), result = invalid["result"] as! [String: Any], data = result["data"] as! [String: Any]
    data["retained"] = 1; result["data"] = data; invalid["result"] = result
    start.observe("automation.recording.start", response: invalid)
    try require(start.uncertain && !start.capturing, "Numeric booleans cannot authorize recording ownership")
  }
}
