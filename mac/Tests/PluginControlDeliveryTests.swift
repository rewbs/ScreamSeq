import AppKit

extension InterfaceTests {
  static func pluginControlDeliveryChecks() throws {
    try pluginOriginalControlChecks()
    try pluginRecordingInputChecks()
    typealias Delivery = PluginControlDelivery
    let owner = Delivery.Owner(document: "document-a", revision: "document-a:4:2", take: "take-a")
    let another = Delivery.Owner(document: "document-b", revision: "document-b:0:0", take: "take-b")
    let first = Delivery.Input(plugin: "stable-plugin-a", parameter: 7, value: 0.25, timestamp: 9_007_199_254_740_993)
    let next = Delivery.Input(plugin: "stable-plugin-b", parameter: 90, value: -6, timestamp: first.timestamp + 1)
    func rejected(_ failure: Delivery.Failure, _ work: () throws -> Void) throws {
      do { try work(); throw InterfaceFailure(message: "Expected native input refusal: \(failure)") }
      catch let actual as Delivery.Failure { try require(actual == failure, "Native input refusal must have the expected cause") }
    }
    func result(_ owner: Delivery.Owner, accepted: Bool = true, warning: String? = nil) -> [String: Any] {
      var data: [String: Any] = ["take": owner.take, "accepted": accepted]
      if let warning { data["intakeError"] = warning }
      return ["result": ["revision": owner.revision, "data": data]]
    }
    func events(_ delivery: Delivery) -> [[String: Any]] { delivery.recovery?["events"] as? [[String: Any]] ?? [] }
    let delivery = Delivery()
    var calls = [[String: Any]](), replies = [Delivery.Reply]()
    let submit: Delivery.Submit = { params, reply in calls.append(params); replies.append(reply) }
    try require(delivery.recovery == nil && !delivery.pending && !delivery.needsReview, "An idle input journal has no invented owner or recovery evidence")
    try delivery.enqueue(first, owner: owner); try delivery.enqueue(next, owner: owner)
    try require(calls.isEmpty && delivery.pending && delivery.retained == 2,
      "Input waits without submitting, coalescing or requiring an available document worker")
    let waiting = events(delivery)
    try require(waiting.map { $0["hostTime"] as? String } == ["9007199254740993", "9007199254740994"] &&
      waiting.allSatisfy { $0["state"] as? String == "pending" }, "Queued recovery retains exact host times beyond Double precision")
    try rejected(.differentOwner) { try delivery.enqueue(first, owner: another) }
    try rejected(.pending) { try delivery.release(owner) }
    delivery.submitNext(submit); delivery.submitNext(submit)
    try require(calls.count == 1 && calls[0]["plugin"] as? String == first.plugin && calls[0]["parameter"] as? UInt32 == first.parameter &&
      calls[0]["value"] as? Double == first.value && calls[0]["timestamp"] as? String == String(first.timestamp) &&
      calls[0]["expectedRevision"] as? String == owner.revision && calls[0]["take"] as? String == owner.take,
      "Submission retains original stable target, revision, value and timestamp and permits one in-flight request")
    let originalReply = replies.removeFirst(); originalReply(result(owner, warning: "Preview unavailable")); originalReply(result(owner))
    try require(delivery.accepted == 1 && delivery.retained == 1 && delivery.previewError == "Preview unavailable" && !delivery.submitted,
      "A duplicate callback cannot count twice; accepted input keeps any separate preview warning")
    delivery.submitNext(submit)
    try require(calls.count == 2 && calls[1]["timestamp"] as? String == String(next.timestamp),
      "The next original event is submitted only after the preceding completion")
    replies.removeFirst()(result(owner, accepted: false))
    try require(delivery.needsReview && !delivery.pending && delivery.retained == 1 && events(delivery)[0]["state"] as? String == "refused",
      "Refused input stays reviewable and recoverable with its original payload")
    delivery.submitNext(submit)
    try require(calls.count == 2, "Refused input is not retried as a fresh request")
    try rejected(.differentOwner) { try delivery.release(another) }
    try delivery.release(owner)
    try delivery.enqueue(first, owner: another); originalReply(result(owner))
    try require(delivery.owner == another && delivery.accepted == 0 && delivery.retained == 1,
      "An old callback after explicit release cannot mutate another take")

    for kind in ["notCommitted", "unknown", "committed", "noChange", "unclassified", "mismatched-take", "mismatched-revision", "missing-accepted", "numeric-accepted"] {
      let current = Delivery(); try current.enqueue(first, owner: owner)
      var sends = 0, reply: Delivery.Reply?
      current.submitNext { _, done in sends += 1; reply = done }
      let response: [String: Any]
      switch kind {
      case "mismatched-take": response = result(Delivery.Owner(document: owner.document, revision: owner.revision, take: "other"))
      case "mismatched-revision": response = result(Delivery.Owner(document: owner.document, revision: "changed", take: owner.take))
      case "missing-accepted": response = ["result": ["revision": owner.revision, "data": ["take": owner.take]]]
      case "numeric-accepted": response = ["result": ["revision": owner.revision, "data": ["take": owner.take, "accepted": 1]]]
      default:
        var error: [String: Any] = ["code": -32002, "message": "Delivery needs review"]
        if kind != "unclassified" { error["data"] = ["writeOutcome": kind] }
        response = ["error": error]
      }
      reply?(response); current.submitNext { _, _ in sends += 1 }
      let evidence = events(current)
      try require(sends == 1 && current.accepted == 0 && current.needsReview && !current.pending &&
        evidence.count == 1 && evidence[0]["hostTime"] as? String == String(first.timestamp) &&
        evidence[0]["state"] as? String == (kind == "notCommitted" ? "refused" : "unknown"),
        "A \(kind) completion retains exact input evidence and is never silently replayed")
    }
    let bounded = Delivery()
    for i in 0..<Delivery.capacity {
      try bounded.enqueue(Delivery.Input(plugin: first.plugin, parameter: first.parameter,
        value: Double(i) / Double(Delivery.capacity), timestamp: first.timestamp + UInt64(i)), owner: owner)
    }
    try rejected(.full) { try bounded.enqueue(next, owner: owner) }
    try require(bounded.retained == 1024 && bounded.lost == 1 && bounded.needsReview && bounded.owner == owner &&
      events(bounded).first?["hostTime"] as? String == String(first.timestamp) &&
      events(bounded).last?["hostTime"] as? String == String(first.timestamp + 1023),
      "Capacity exhaustion reports loss without dropping, replacing or coalescing retained original input")
    let archive = bounded.recovery!
    let encoded = try PropertyListSerialization.data(fromPropertyList: archive, format: .binary, options: 0)
    let decoded = try PropertyListSerialization.propertyList(from: encoded, options: [], format: nil) as! [String: Any]
    try require((decoded as NSDictionary).isEqual(archive) && decoded["reviewOnly"] as? Bool == true && decoded["lost"] as? String == "1",
      "Pending-input evidence round-trips as a plist-safe detached archive with exact decimal counters")
    let invalid = Delivery()
    try rejected(.invalidInput) { try invalid.enqueue(Delivery.Input(plugin: first.plugin, parameter: 7, value: .nan, timestamp: 1), owner: owner) }
    try rejected(.invalidInput) { try invalid.enqueue(Delivery.Input(plugin: first.plugin, parameter: 7, value: 1, timestamp: 0), owner: owner) }
    try rejected(.invalidInput) { try invalid.enqueue(first, owner: Delivery.Owner(document: "bad\0identity", revision: owner.revision, take: owner.take)) }
    try require(invalid.owner == nil && invalid.recovery == nil, "Invalid input cannot create a partially bound journal")
    try invalid.enqueue(first, owner: owner)
    invalid.submitNext { _, done in done(["error": ["code": -32003, "message": "Bad\0input " + String(repeating: "🎛", count: 900)]]) }
    let diagnostic = events(invalid)[0]["error"] as? String ?? ""
    try require(!diagnostic.isEmpty && diagnostic.utf8.count <= 2048 && !diagnostic.contains("�") && !diagnostic.contains("\0"),
      "Bounded diagnostics preserve UTF-8 boundaries and the shared archive's string contract")
  }
  static func pluginOriginalControlChecks() throws {
    let editor = PluginEditor(frame: .zero)
    let model = PatternModel(["nativePlugins": [["instanceID": "stable-plugin-a", "name": "Native input", "format": "Built-in"]]])
    var clock: UInt64 = 9_007_199_254_740_993, original = [PluginControlDelivery.Input](), manual = [Double]()
    editor.parameterInputClock = { clock }
    editor.canEdit = { false }
    editor.onParameter = { _, _, value, _ in manual.append(value) }
    editor.onOriginalParameter = { plugin, parameter, value, timestamp in
      original.append(.init(plugin: plugin, parameter: parameter, value: value, timestamp: timestamp)); return true
    }
    editor.update(model: model, values: [["id": 7, "name": "Gain", "min": 0.0, "max": 1.0, "value": 0.0, "step": 0.1]])
    let row = editor.tableView(editor.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    row.slider.changed?(0.26); clock += 1; row.slider.changed?(0.47); clock += 1; row.reading.commit?("0.8")
    try require(original.count == 3 && original.map(\.timestamp) == [9_007_199_254_740_993, 9_007_199_254_740_994, 9_007_199_254_740_995] &&
      original.allSatisfy { $0.plugin == "stable-plugin-a" && $0.parameter == 7 } &&
      abs(original[0].value - 0.3) < 1e-12 && abs(original[1].value - 0.5) < 1e-12 && abs(original[2].value - 0.8) < 1e-12 && manual.isEmpty,
      "A busy native take receives every original snapped slider/text value and timestamp without manual fallback or coalescing")
    // A detached old row must not emit input into the new selected processor.
    editor.update(model: PatternModel(["nativePlugins": [["instanceID": "replacement", "name": "Other plugin", "format": "Built-in"]]]),
      values: [["id": 7, "name": "Gain", "min": 0.0, "max": 1.0, "value": 0.0]])
    row.slider.changed?(0.9)
    try require(original.count == 3, "An obsolete parameter row cannot redirect original input to another stable plugin")
    editor.canEdit = { true }; editor.onOriginalParameter = { _, _, _, _ in false }
    let ordinary = editor.tableView(editor.parameters, viewFor: nil, row: 0) as! PluginParameterRow
    ordinary.slider.changed?(0.4)
    try require(manual == [0.4], "Without a retained input owner, the ordinary manual control path remains available")
    editor.onOriginalParameter = { _, _, _, _ in throw PluginRecordingInput.Refusal.unavailable("Take stopped; review input") }
    ordinary.slider.changed?(0.9)
    try require(manual == [0.4] && editor.note.stringValue == "Take stopped; review input" &&
      abs((Double(ordinary.reading.stringValue) ?? -1) - 0.4) < 1e-12,
      "A retained-owner refusal restores the displayed value and never falls back to an ordinary musical edit")
  }
}
