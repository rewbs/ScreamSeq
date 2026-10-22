import Foundation
import CoreFoundation

/// Main-owner adapter for native controls and the shared recording API. The
/// caller pumps only while its serial session worker is idle. No musical state
/// or event timing is implemented here; original inputs use the existing API.
final class PluginRecordingInput {
  typealias Reply = ([String: Any]) -> Void
  typealias Send = (String, [String: Any], @escaping Reply) -> Void
  enum Refusal: LocalizedError {
    case unavailable(String)
    var errorDescription: String? { if case .unavailable(let text) = self { return text }; return nil }
  }
  private struct Boundary {
    let id = UUID(), method: String, params: [String: Any], reply: Reply
    let owner: PluginControlDelivery.Owner?
    let loss: NSDictionary
    var sent = false
  }
  let delivery = PluginControlDelivery()
  private(set) var owner: PluginControlDelivery.Owner?
  private(set) var revision = "", capturing = false, compatible = false, uncertain = false
  private(set) var sources = Set<String>()
  private var boundary: Boundary?
  var onReady: (() -> Void)?
  var changingState: Bool { boundary != nil }
  var protectsDocument: Bool { owner != nil || delivery.owner != nil || changingState || uncertain }
  var pending: Bool { delivery.pending || changingState }
  init() { delivery.onChange = { [weak self] in self?.onReady?() } }
  static func isBoundary(_ method: String) -> Bool {
    ["automation.recording.start", "automation.recording.stop", "automation.recording.commit", "automation.recording.discard"].contains(method)
  }
  private static func boolean(_ value: Any?) -> Bool? {
    guard let value = value as? NSNumber, CFGetTypeID(value) == CFBooleanGetTypeID() else { return nil }
    return value.boolValue
  }
  static func refused(_ message: String) -> [String: Any] {
    ["error": ["code": -32002, "message": message, "data": ["writeOutcome": "notCommitted"]]]
  }
  /// Called with the worker's response before the app refreshes/pumps events.
  /// A failed lifecycle operation closes input until explicit readback; it does
  /// not turn an uncertain write into another write or erase its journal.
  func observe(_ method: String, response: [String: Any]) {
    guard method.hasPrefix("automation.recording.") else { return }
    guard let result = response["result"] as? [String: Any], let revision = result["revision"] as? String,
      let data = result["data"] as? [String: Any], let retained = Self.boolean(data["retained"]),
      let capturing = Self.boolean(data["capturing"]), let token = data["take"] as? String else {
      if Self.isBoundary(method) {
        let code = (response["error"] as? [String: Any])?["code"] as? Int
        // Validation/conflict and explicit pre-dispatch rejection are known
        // refusals. Preserve the current owner without inventing uncertainty.
        if code != -32602 && code != -32001 && !NativeRequestOutcome.canRetryWrite(response) {
          uncertain = true; self.capturing = false
        }
      }
      return
    }
    if retained {
      guard !token.isEmpty, let base = data["baseRevision"] as? String,
        let document = base.split(separator: ":", omittingEmptySubsequences: false).first, !document.isEmpty,
        let compatible = Self.boolean(data["compatible"]), let entries = data["sources"] as? [[String: Any]],
        entries.allSatisfy({ ($0["plugin"] as? String)?.isEmpty == false }) else {
        uncertain = true; self.capturing = false; return
      }
      let next = PluginControlDelivery.Owner(document: String(document), revision: base, take: token)
      if let original = delivery.owner, original != next { uncertain = true; self.capturing = false; return }
      owner = next; self.revision = revision; self.capturing = capturing
      self.compatible = compatible && ((data["failure"] as? NSNumber)?.uint64Value ?? 0) == 0
      sources = Set(entries.compactMap { $0["plugin"] as? String }); uncertain = false
    } else {
      guard !capturing else { uncertain = true; self.capturing = false; return }
      if let original = delivery.owner {
        // Only this exact completed lifecycle call proves permission to retire
        // original input. A read reporting no take is not such a receipt.
        guard (method == "automation.recording.commit" || method == "automation.recording.discard"),
          token == original.take, revision.split(separator: ":").first.map(String.init) == original.document,
          !delivery.pending else { uncertain = true; self.capturing = false; return }
        do { try delivery.release(original) } catch { uncertain = true; self.capturing = false; return }
      }
      owner = nil; self.revision = revision; self.capturing = false; compatible = false; sources.removeAll(); uncertain = false
    }
  }
  /// false preserves the ordinary manual path. Rejection throws so the native
  /// row can restore its previous displayed value instead of faking acceptance.
  func capture(_ input: PluginControlDelivery.Input) throws -> Bool {
    guard protectsDocument else { return false }
    guard !changingState, !uncertain, let owner, capturing, compatible, revision == owner.revision,
      sources.contains(input.plugin), !delivery.needsReview else {
      throw Refusal.unavailable("Review or finish the retained plugin take before editing this control.")
    }
    do { try delivery.enqueue(input, owner: owner) }
    catch { throw Refusal.unavailable("Original plugin input could not be queued. Review the retained input and loss count.") }
    onReady?(); return true
  }
  /// Capture once. Stop waits for every previously queued original input; new
  /// native input is refused from this point, so draining cannot postpone Stop
  /// indefinitely. Keep/Discard also cannot overtake a capture completion.
  func request(_ method: String, params: [String: Any], reply: @escaping Reply) {
    guard Self.isBoundary(method), boundary == nil else { reply(Self.refused("A plugin recording state change is already pending.")); return }
    guard !uncertain else { reply(Self.refused("Check the retained take before changing its state.")); return }
    if method == "automation.recording.start" {
      guard owner == nil && delivery.owner == nil else { reply(Self.refused("Finish the retained plugin take before starting another.")); return }
    } else {
      if let owner, let token = params["take"] as? String, let expected = params["expectedRevision"] as? String,
        token != owner.take || expected != revision {
        reply(["error": ["code": -32001, "message": "The plugin take changed. Check it before choosing another action.",
          "data": ["writeOutcome": "notCommitted"]]]); return
      }
      if method == "automation.recording.commit", delivery.needsReview, Self.boolean(params["acknowledgeLoss"]) != true {
        reply(Self.refused("Review original refused or uncertain input and acknowledge loss before keeping the accepted points.")); return
      }
    }
    boundary = Boundary(method: method, params: params, reply: reply, owner: owner, loss: delivery.lossCensus)
    onReady?()
  }
  func pump(_ send: Send) {
    if delivery.pending {
      delivery.submitNext { params, reply in
        send("automation.recording.capture", params) { [weak self] response in
          self?.observe("automation.recording.capture", response: response); reply(response)
        }
      }
      return
    }
    guard var request = boundary, !request.sent else { return }
    // A completion/read during the wait may have changed the retained owner or
    // loss census. Explicit acknowledgement never authorizes a different take.
    guard owner == request.owner, !uncertain,
      request.owner == nil || request.params["expectedRevision"] as? String == revision else {
      boundary = nil; request.reply(Self.refused("The plugin take changed while input was draining. Review it again.")); return
    }
    if request.method == "automation.recording.commit",
      !request.loss.isEqual(delivery.lossCensus) || (delivery.needsReview && Self.boolean(request.params["acknowledgeLoss"]) != true) {
      boundary = nil; request.reply(Self.refused("Original input needs review before keeping this take.")); return
    }
    request.sent = true; boundary = request
    send(request.method, request.params) { [weak self] response in
      guard let self, self.boundary?.id == request.id else { return }
      self.observe(request.method, response: response); self.boundary = nil
      request.reply(response); self.onReady?()
    }
  }
}
