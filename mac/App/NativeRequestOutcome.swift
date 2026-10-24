import Foundation

/// A busy code can describe a refused request or an admitted operation whose
/// outcome is uncertain. Only the explicit pre-dispatch receipt permits a
/// write replay; read-only callers can retry busy without duplicating a write.
enum NativeRequestOutcome {
  static func canRetryRead(_ response: [String: Any]) -> Bool {
    (response["error"] as? [String: Any])?["code"] as? Int == -32002
  }
  static func canRetryWrite(_ response: [String: Any]) -> Bool {
    guard let error = response["error"] as? [String: Any], error["code"] as? Int == -32002,
      let data = error["data"] as? [String: Any] else { return false }
    return data["writeOutcome"] as? String == "notCommitted"
  }
}
