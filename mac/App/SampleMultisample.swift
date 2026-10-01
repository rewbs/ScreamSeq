import Foundation
import CryptoKit

struct SampleFilenameNote {
  let family: String, label: String, semitone: Int, number: Int?
  // A note must be a whole filename token. Multiple note tokens are ambiguous.
  private static let note = try! NSRegularExpression(pattern: #"(?<![\p{L}\p{N}])([A-Ga-g])([#♯b♭]?)(-?\d{1,2})(?![\p{L}\p{N}])"#)
  private static let prefix = try! NSRegularExpression(pattern: #"^\s*(\d{1,3})[\s._-]+"#)
  private static let separators = try! NSRegularExpression(pattern: #"[\s_]+"#)
  static func parse(_ filename: String) -> SampleFilenameNote? {
    let stem = (filename as NSString).deletingPathExtension, ns = stem as NSString
    let matches = note.matches(in: stem, range: NSRange(location: 0, length: ns.length))
    guard matches.count == 1, let match = matches.first, let octave = Int(ns.substring(with: match.range(at: 3))), (-4...10).contains(octave) else { return nil }
    let letter = ns.substring(with: match.range(at: 1)).uppercased(), accidental = ns.substring(with: match.range(at: 2))
    let pitch = ["C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11][letter]!
    let semitone = octave * 12 + pitch + (["#","♯"].contains(accidental) ? 1 : ["b","♭"].contains(accidental) ? -1 : 0)
    var family = ns.replacingCharacters(in: match.range, with: " "), number: Int?
    if let start = prefix.firstMatch(in: family, range: NSRange(location: 0, length: (family as NSString).length)) {
      number = Int((family as NSString).substring(with: start.range(at: 1)))
      family = (family as NSString).replacingCharacters(in: start.range, with: "")
    }
    family = separators.stringByReplacingMatches(in: family, range: NSRange(location: 0, length: (family as NSString).length), withTemplate: " ")
      .trimmingCharacters(in: CharacterSet(charactersIn: " ._-()[]{}"))
    return SampleFilenameNote(family: family, label: ns.substring(with: match.range), semitone: semitone, number: number)
  }
}

struct MultisampleMember {
  let entry: SampleLibraryEntry, note: SampleFilenameNote
  var dictionary: [String: Any] { ["path": entry.path, "filename": entry.name, "sourceNote": note.label, "semitone": note.semitone] }
}
struct MultisampleGroup {
  let id: String, name: String, folder: String, members: [MultisampleMember], suggestedOctaveShift: Int, explanation: String
  var dictionary: [String: Any] { ["id": id, "name": name, "folder": folder, "count": members.count,
    "samples": members.map(\.dictionary), "suggestedOctaveShift": suggestedOctaveShift, "explanation": explanation] }
  static func detect(_ entries: [SampleLibraryEntry]) -> [MultisampleGroup] {
    var families = [String: [MultisampleMember]]()
    for entry in entries {
      guard let parsed = SampleFilenameNote.parse(entry.name) else { continue }
      let folder = (entry.path as NSString).deletingLastPathComponent
      let note = SampleFilenameNote(family: parsed.family.isEmpty ? (folder as NSString).lastPathComponent : parsed.family,
        label: parsed.label, semitone: parsed.semitone, number: parsed.number)
      let key = folder + "\0" + SampleLibraryIndex.fold(note.family) + "\0" + (entry.name as NSString).pathExtension.lowercased()
      families[key, default: []].append(MultisampleMember(entry: entry, note: note))
    }
    return families.compactMap { key, values in
      guard values.count >= 2, Set(values.map { $0.note.semitone }).count >= 2 else { return nil }
      let members = values.sorted { $0.note.semitone == $1.note.semitone ? $0.entry.path < $1.entry.path : $0.note.semitone < $1.note.semitone }
      let offsets = members.compactMap { member in member.note.number.map { $0 - member.note.semitone } }
      let numbered = members.count >= 3 && offsets.count == members.count && Set(offsets).count == 1 && offsets[0] % 12 == 0 && (-48...48).contains(offsets[0]) && members.allSatisfy { (0..<120).contains($0.note.number!) }
      let shift = numbered ? offsets[0] / 12 : 0
      let explanation = numbered ? "Filename numbers agree across all notes; suggested octave offset \(shift >= 0 ? "+" : "")\(shift). Check the tracker roots below." : "Roots follow filename octaves. Adjust the octave offset if this pack uses a different convention."
      return MultisampleGroup(id: SHA256.hash(data: Data(key.utf8)).map { String(format: "%02x", $0) }.joined(),
        name: members[0].note.family, folder: (members[0].entry.path as NSString).deletingLastPathComponent,
        members: members, suggestedOctaveShift: shift, explanation: explanation)
    }.sorted { $0.id < $1.id }
  }
  func sources(octaveShift: Int) throws -> [[String: Any]] {
    guard (-4...4).contains(octaveShift), (2...128).contains(members.count) else { throw Self.error("Select a family with 2 to 128 samples and an octave offset from −4 to +4.") }
    let notes = members.map { $0.note.semitone + octaveShift * 12 + 1 }
    guard notes.allSatisfy({ (1...120).contains($0) }) else { throw Self.error("Some roots fall outside C-0…B-9. Adjust the octave offset.") }
    guard Set(notes).count == notes.count else { throw Self.error("More than one sample has the same note. Separate duplicate, velocity or round-robin variants before importing.") }
    return zip(members, notes).map { ["path": $0.entry.path, "rootNote": $1] }
  }
  static func noteName(_ note: Int) -> String {
    guard (1...120).contains(note) else { return "Out of range" }
    // Omit the tracker's padding dash here so C1 cannot be confused with
    // a filename's negative octave C-1 in the adjacent column.
    return ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][(note-1)%12] + String((note-1)/12)
  }
  static func error(_ message: String) -> NSError { NSError(domain: "Multisample", code: -32602, userInfo: [NSLocalizedDescriptionKey: message]) }
}
