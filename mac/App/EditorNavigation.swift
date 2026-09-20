import Foundation
import CoreFoundation

struct EditorNavigation: Equatable {
  var pattern = 0, row = 0, channel = 0, column = 0
  var following = true
  var token: String { "\(pattern):\(row):\(channel):\(column):\(following ? 1 : 0)" }
  var dictionary: [String: Any] { ["pattern":pattern,"row":row,"channel":channel,"column":column,"following":following] }
  struct Failure: Error { let code: Int; let message: String }
  func prepared(_ params: [String: Any], revision: String, contextToken: String,
                patterns: [[String: Any]], channels: Int, extraColumns: [Int] = []) throws -> EditorNavigation {
    func invalid(_ message: String) throws -> Never { throw Failure(code: -32602, message: message) }
    let names: Set<String> = ["expectedRevision","expectedContext","pattern","row","channel","column","following"]
    guard Set(params.keys).isSubset(of: names), params.count > 2 else { try invalid("Supply at least one navigation field and both revision tokens.") }
    guard let expected = params["expectedRevision"] as? String, let expectedContext = params["expectedContext"] as? String else { try invalid("Read context.get and supply expectedRevision and expectedContext.") }
    guard expected == revision, expectedContext == contextToken else {
      throw Failure(code: -32001, message: "The song or edit cursor changed; read context.get and prepare navigation again.")
    }
    func integer(_ key: String, fallback: Int, maximum: Int) throws -> Int {
      guard let raw = params[key] else { return fallback }
      guard let number = raw as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { try invalid("\(key) must be an integer.") }
      let value = number.doubleValue
      guard value.isFinite, value.rounded() == value, value >= 0, value <= Double(maximum) else { try invalid("\(key) is outside the available editor range.") }
      return Int(value)
    }
    var result = self
    result.pattern = try integer("pattern", fallback: pattern, maximum: 65535)
    guard let chosen = patterns.first(where: { $0["index"] as? Int == result.pattern }),
      let rows = chosen["rows"] as? Int, rows > 0, channels > 0 else { try invalid("Choose an allocated, nonempty pattern.") }
    result.row = try integer("row", fallback: min(row, rows - 1), maximum: rows - 1)
    result.channel = try integer("channel", fallback: min(channel, channels - 1), maximum: channels - 1)
    let lastColumn=4 + (extraColumns.indices.contains(result.channel) ? extraColumns[result.channel] : 0)
    result.column = try integer("column", fallback: min(column,lastColumn), maximum: lastColumn)
    if let raw = params["following"] {
      guard let number = raw as? NSNumber, CFGetTypeID(number) == CFBooleanGetTypeID() else { try invalid("following must be true or false.") }
      result.following = number.boolValue
    } else if result.pattern != pattern { result.following = false }
    return result
  }
}
