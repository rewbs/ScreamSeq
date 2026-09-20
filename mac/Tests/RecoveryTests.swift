import Foundation

@main struct RecoveryTests {
  static func main() throws {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      "resonance-recovery-" + UUID().uuidString, isDirectory: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let store = RecoveryStore(directory: directory)
    let a = "FFFFFFFF-FFFF-4FFF-8FFF-FFFFFFFFFFFF"
    let b = "00000000-0000-4000-8000-000000000000"
    for i in 0..<12 {
      try store.save(id: a, format: "mptm") {
        try Data("generation \(i)".utf8).write(to: $0, options: .atomic)
      }
      Thread.sleep(forTimeInterval: 0.003)
    }
    let generations = try store.candidates()
    precondition(generations.count == RecoveryStore.generations, "keep exactly ten generations")
    let newest = try store.save(id: b, format: "resonance", title: "Recovered lead", source: "/Music/song.resonance", hasRecording: true) {
      try Data("other document".utf8).write(to: $0, options: .atomic)
    }
    let sorted = try store.candidates()
    precondition(
      sorted.first?.resolvingSymlinksInPath() == newest.resolvingSymlinksInPath(),
      "sort by timestamp, not random document UUID")
    do {
      try store.save(id: a, format: "mptm") { file in
        try Data("incomplete".utf8).write(to: file)
        throw CocoaError(.fileWriteOutOfSpace)
      }
      preconditionFailure("expected failed save")
    } catch {}
    let retained = try store.candidates()
    precondition(retained.count == RecoveryStore.generations + 1, "failed save retains all prior recovery generations")
    let entry = try store.entries().first!
    precondition(entry.title == "Recovered lead" && entry.source == "/Music/song.resonance" && entry.hasRecording,
      "Recovery metadata identifies the song and unfinished take")
    let partial = directory.appendingPathComponent(".pending-crashed.resonance")
    try Data("partial".utf8).write(to: partial)
    let link = directory.appendingPathComponent("linked.resonance")
    try FileManager.default.createSymbolicLink(at: link, withDestinationURL: newest)
    let discoverable = try store.candidates()
    precondition(discoverable.count == RecoveryStore.generations + 1, "Partial and symbolic-link files are not recoverable")
    try store.clear(id: a)
    let remaining = try store.candidates()
    precondition(
      remaining.map { $0.resolvingSymlinksInPath() } == [newest.resolvingSymlinksInPath()],
      "clear only the saved document")
    precondition(
      RecoveryStore.documentID(for: newest) == b, "recovered document retains recovery identity")
    print(
      "PASS recovery generations, newest selection across documents, failed-write retention, document-scoped cleanup"
    )
  }
}
