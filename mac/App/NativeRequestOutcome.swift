import Foundation

/// A busy code can describe a refused request or an admitted operation whose
/// outcome is uncertain. Only the explicit pre-dispatch receipt permits replay.
enum NativeRequestOutcome {
  static func canRetryWrite(_ response: [String: Any]) -> Bool {
    guard let error = response["error"] as? [String: Any], error["code"] as? Int == -32002,
      let data = error["data"] as? [String: Any] else { return false }
    return data["writeOutcome"] as? String == "notCommitted"
  }
}
