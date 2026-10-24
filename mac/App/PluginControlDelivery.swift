import Foundation
import CoreFoundation

/// Main/control-owner input journal. Each event carries its original native
/// timestamp; waiting for the worker never coalesces, retimes or rebinds it.
/// Completion failures retain evidence and never cause automatic resubmission.
final class PluginControlDelivery {
  struct Owner: Equatable {
    let document: String, revision: String, take: String
  }
  struct Input: Equatable {
    let plugin: String, parameter: UInt32, value: Double, timestamp: UInt64
  }
  enum Failure: Error, Equatable { case invalidInput, differentOwner, full, pending }
  enum State: Equatable { case waiting, submitted, refused, unknown }
  typealias Reply = ([String: Any]) -> Void
  typealias Submit = ([String: Any], @escaping Reply) -> Void
  private struct Entry {
    let id = UUID()
    let input: Input
    var state = State.waiting, error = ""
  }
  static let capacity = 1024 // editor/hosted/ControlInputRecovery.hpp
  private(set) var owner: Owner?
  private(set) var accepted: UInt64 = 0, lost: UInt64 = 0
  private(set) var previewError = ""
  private var entries = [Entry]()
  var onChange: (() -> Void)?
  var pending: Bool { entries.contains { $0.state == .waiting || $0.state == .submitted } }
  var submitted: Bool { entries.contains { $0.state == .submitted } }
  var needsReview: Bool { lost > 0 || entries.contains { $0.state == .refused || $0.state == .unknown } }
  var retained: Int { entries.count }
  var reviewMessage: String? {
    if let entry = entries.first(where: { $0.state == .refused || $0.state == .unknown }) { return entry.error }
    return lost > 0 ? "The original input queue overflowed. Review the retained input and loss count." : nil
  }
  private static func valid(_ text: String, maximum: Int) -> Bool {
    !text.isEmpty && text.utf8.count <= maximum && !text.contains("\0")
  }
  private static func diagnostic(_ text: String) -> String {
    // Keep the shared archive's byte bound without splitting a UTF-8 code point.
    var bytes = Array(text.utf8.prefix(2048))
    while !bytes.isEmpty {
      if let valid = String(bytes: bytes, encoding: .utf8) { return valid.replacingOccurrences(of: "\0", with: " ") }
      bytes.removeLast()
    }
    return ""
  }
  func enqueue(_ input: Input, owner: Owner) throws {
    guard Self.valid(owner.document, maximum: 128), Self.valid(owner.revision, maximum: 4096),
      Self.valid(owner.take, maximum: 80), Self.valid(input.plugin, maximum: 128),
      input.timestamp > 0, input.value.isFinite else { throw Failure.invalidInput }
    if let original = self.owner, original != owner { throw Failure.differentOwner }
    self.owner = owner
    guard entries.count < Self.capacity else {
      if lost != .max { lost += 1 }; onChange?(); throw Failure.full
    }
    entries.append(Entry(input: input)); onChange?()
  }
  // The app calls this only when its serial session owner is available. An
  // already submitted event remains owned until that exact callback completes.
  // onChange may schedule another main-loop turn; it must not replay an event.
  func submitNext(_ submit: Submit) {
    guard !submitted, let owner, let index = entries.firstIndex(where: { $0.state == .waiting }) else { return }
    let id = entries[index].id, input = entries[index].input
    entries[index].state = .submitted
    let params: [String: Any] = ["expectedRevision": owner.revision, "take": owner.take,
      "plugin": input.plugin, "parameter": input.parameter, "value": input.value,
      "timestamp": String(input.timestamp)]
    onChange?()
    submit(params) { [weak self] response in self?.finish(id: id, owner: owner, response: response) }
  }
  private func finish(id: UUID, owner: Owner, response: [String: Any]) {
    // A duplicate/late callback cannot retire a different event or count twice.
    guard self.owner == owner, let index = entries.firstIndex(where: { $0.id == id }), entries[index].state == .submitted else { return }
    if let result = response["result"] as? [String: Any], result["revision"] as? String == owner.revision,
      let data = result["data"] as? [String: Any], data["take"] as? String == owner.take,
      let captured = data["accepted"] as? NSNumber, CFGetTypeID(captured) == CFBooleanGetTypeID() {
      if captured.boolValue {
        if let warning = data["intakeError"] as? String { previewError = Self.diagnostic(warning) }
        if accepted != .max { accepted += 1 }
        entries.remove(at: index)
      } else {
        entries[index].state = .refused
        entries[index].error = "The session refused this input; its original value and timestamp are retained."
      }
    } else {
      let error = response["error"] as? [String: Any]
      entries[index].state = NativeRequestOutcome.canRetryWrite(response) ? .refused : .unknown
      entries[index].error = Self.diagnostic(error?["message"] as? String ?? "The input completion could not be verified; original input is retained.")
    }
    onChange?()
  }
  var summary: [String: Any] {
    ["accepted": String(accepted), "lost": String(lost), "retained": entries.count,
     "waiting": entries.filter { $0.state == .waiting }.count,
     "submitted": entries.filter { $0.state == .submitted }.count,
     "refused": entries.filter { $0.state == .refused }.count,
     "unknown": entries.filter { $0.state == .unknown }.count,
     "pending": pending, "needsReview": needsReview, "previewError": previewError]
  }
  // Native review only: bounded pages preserve UUID selection and decimal
  // host timestamps. These rows never become replayable capture requests.
  func reviewPage(offset: Int, limit: Int = 10) -> [String: Any] {
    let count = min(128, max(1, limit)), start = min(entries.count, max(0, offset))
    let rows = entries.dropFirst(start).prefix(count).map { entry -> [String: Any] in
      let state: String
      switch entry.state {
      case .waiting: state = "waiting"
      case .submitted: state = "in flight"
      case .refused: state = "refused"
      case .unknown: state = "uncertain"
      }
      return ["id": entry.id.uuidString, "plugin": entry.input.plugin, "parameter": entry.input.parameter,
        "value": entry.input.value, "hostTime": String(entry.input.timestamp), "state": state, "error": entry.error]
    }
    return ["document": owner?.document ?? "", "take": owner?.take ?? "", "total": entries.count, "offset": start, "events": rows]
  }
  // Accepted/waiting progress cannot invalidate acknowledgement by itself.
  // Every newly refused/unknown original input and overflow must do so.
  var lossCensus: NSDictionary {
    ["lost": String(lost), "events": entries.filter { $0.state == .refused || $0.state == .unknown }.map {
      ["id": $0.id.uuidString, "state": $0.state == .refused ? "refused" : "unknown"]
    }] as NSDictionary
  }
  var recovery: [String: Any]? {
    guard let owner else { return nil }
    return ["version": 1, "reviewOnly": true, "document": owner.document,
      "revision": owner.revision, "take": owner.take, "accepted": String(accepted), "lost": String(lost),
      "previewError": previewError, "events": entries.map { entry -> [String: Any] in
        let state: String
        switch entry.state { case .waiting, .submitted: state = "pending"; case .refused: state = "refused"; case .unknown: state = "unknown" }
        return ["plugin": entry.input.plugin, "parameter": entry.input.parameter, "value": entry.input.value,
          "hostTime": String(entry.input.timestamp), "state": state, "error": entry.error]
      }]
  }
  // Caller must first Keep/Discard the corresponding session take and retain
  // any requested recovery copy. This is never an error-retry or document reset.
  func release(_ expected: Owner) throws {
    guard owner == expected else { throw Failure.differentOwner }
    guard !pending else { throw Failure.pending }
    entries.removeAll(); owner = nil; accepted = 0; lost = 0; previewError = ""; onChange?()
  }
}
