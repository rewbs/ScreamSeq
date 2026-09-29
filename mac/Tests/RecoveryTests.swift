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
    try pruningChecks()
    print(
      "PASS recovery generations, newest selection across documents, failed-write retention, document-scoped cleanup, conservative cross-session pruning"
    )
  }
  static func pruningChecks() throws {
    let fm = FileManager.default
    let directory = fm.temporaryDirectory.appendingPathComponent("resonance-recovery-prune-" + UUID().uuidString, isDirectory: true)
    defer { try? fm.removeItem(at: directory) }
    let store = RecoveryStore(directory: directory)
    precondition(store.prune(protecting: []).isEmpty, "Pruning a missing recovery directory does nothing")
    let now = Date(), day: TimeInterval = 24 * 60 * 60
    func age(_ file: URL, _ seconds: TimeInterval) throws {
      try fm.setAttributes([.modificationDate: now.addingTimeInterval(-seconds)], ofItemAtPath: file.path)
    }
    func copy(_ id: String, _ text: String, age seconds: TimeInterval) throws -> URL {
      let file = try store.save(id: id, format: "screamseq", title: text) { try Data(text.utf8).write(to: $0, options: .atomic) }
      try age(file, seconds); try age(file.appendingPathExtension("json"), seconds)
      return file
    }
    func exists(_ file: URL) -> Bool { fm.fileExists(atPath: file.path) }
    func metadata(_ file: URL) -> Bool { fm.fileExists(atPath: file.path + ".json") }
    let current = UUID().uuidString, crashed = UUID().uuidString, ancient = UUID().uuidString, busy = UUID().uuidString
    let currentOld = try copy(current, "current session", age: 90 * day)
    let crashedOld = try copy(crashed, "crashed older generation", age: 29 * day)
    let crashedNew = try copy(crashed, "crashed newest generation", age: 2 * day)
    let ancientOld = try copy(ancient, "abandoned older generation", age: 45 * day)
    let ancientNew = try copy(ancient, "abandoned newest generation", age: 31 * day)
    let stalePending = directory.appendingPathComponent(".pending-" + UUID().uuidString + ".screamseq")
    let freshPending = directory.appendingPathComponent(".pending-" + UUID().uuidString + ".screamseq")
    try Data("stale".utf8).write(to: stalePending); try age(stalePending, 2 * 60 * 60)
    try Data("in flight".utf8).write(to: freshPending); try age(freshPending, 10 * 60)
    let orphan = directory.appendingPathComponent("\(UUID().uuidString)-1-\(UUID().uuidString).screamseq.json")
    try Data("{}".utf8).write(to: orphan); try age(orphan, 2 * 60 * 60)
    let foreign = directory.appendingPathComponent("my-own-backup.screamseq")
    try Data("user file".utf8).write(to: foreign); try age(foreign, 400 * day)
    let hiddenNote = directory.appendingPathComponent(".note")
    try Data("unrelated".utf8).write(to: hiddenNote); try age(hiddenNote, 400 * day)

    let removed = Set(store.prune(protecting: [current], now: now).map(\.lastPathComponent))
    precondition(removed == Set([ancientOld, stalePending, orphan].map(\.lastPathComponent)),
      "Default pruning removes only expired older generations, stale staging files and orphaned metadata")
    precondition(exists(currentOld) && metadata(currentOld), "The current session's copies survive whatever their age")
    precondition(exists(crashedOld) && exists(crashedNew) && metadata(crashedNew), "Copies inside the age limit survive")
    precondition(!exists(ancientOld) && !metadata(ancientOld), "Older generations past the age limit are removed with their metadata")
    precondition(exists(ancientNew) && metadata(ancientNew), "Age alone never removes the newest copy of a song: it may be the only one")
    precondition(!exists(stalePending) && exists(freshPending), "Only stale staging files are removed")
    precondition(!exists(orphan), "Old metadata without a copy is removed")
    precondition(exists(foreign) && exists(hiddenNote), "Files the store did not name are never pruned")

    // Only the total cap can remove the last copy of an old song, oldest first.
    // It never removes anything inside the age limit, however small the cap is.
    let forgotten = try copy(UUID().uuidString, "only copy of a forgotten song", age: 60 * day)
    var generations = [URL]()
    for i in 0..<4 { generations.append(try copy(busy, "generation \(i)", age: TimeInterval(10 - i) * day)) }
    let present = try store.candidates().count
    precondition(present == 10, "Ten copies are present before the cap applies")
    let capped = store.prune(protecting: [current], now: now, maximumCopies: 9).map(\.lastPathComponent)
    precondition(capped == [forgotten.lastPathComponent], "The cap removes the oldest last copy first")
    precondition(exists(ancientNew) && !metadata(forgotten), "A newer last copy survives while the total fits")
    let minimal = store.prune(protecting: [current], now: now, maximumCopies: 0).map(\.lastPathComponent)
    precondition(minimal == [ancientNew.lastPathComponent], "A zero cap removes only last copies past the age limit")
    precondition(generations.allSatisfy(exists) && exists(crashedOld) && exists(crashedNew) && exists(currentOld) && exists(foreign) && exists(freshPending),
      "Everything inside the age limit, the current session and foreign files survive a zero cap")
    let survivor = try store.entries().first { $0.url.lastPathComponent == crashedNew.lastPathComponent }
    precondition(survivor?.title == "crashed newest generation",
      "Surviving copies keep their metadata")
    precondition(store.prune(protecting: [current], now: now).isEmpty, "Pruning again removes nothing more")
  }
}
