import Foundation

// UI-owner state only. The renderer has one held preview slot per channel/key;
// multiple physical input owners share it until the final owner releases it.
struct AuditionOwnership {
  struct Voice: Equatable { let note: Int, instrument: Int, channel: Int }
  struct Event { let voice: Voice; let on: Bool }
  private var holds = [String: Voice]()
  mutating func clear() { holds.removeAll() }
  mutating func release(_ key: String) -> [Event] {
    guard let voice = holds.removeValue(forKey: key), !holds.values.contains(voice) else { return [] }
    return [Event(voice: voice, on: false)]
  }
  mutating func release(prefix: String) -> [Event] {
    var result = [Event]()
    for key in holds.keys.filter({ $0.hasPrefix(prefix) }) { result += release(key) }
    return result
  }
  mutating func press(_ key: String, voice: Voice) -> [Event] {
    guard holds[key] != voice else { return [] }
    var result = release(key)
    // A different sound on the same renderer slot supersedes its older owners.
    // Their later key-up must not cut the newly prepared voice.
    holds = holds.filter { $0.value.note != voice.note || $0.value.channel != voice.channel || $0.value.instrument == voice.instrument }
    let shared = holds.values.contains(voice)
    holds[key] = voice
    if !shared { result.append(Event(voice: voice, on: true)) }
    return result
  }
}
