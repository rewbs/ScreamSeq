import Foundation

struct RecoveryEntry {
  let url: URL
  let date: Date
  let title: String
  let source: String?
  let hasRecording: Bool
  var dictionary: [String: Any] {
    ["id": url.lastPathComponent, "document": RecoveryStore.documentID(for: url),
     "savedAt": ISO8601DateFormatter().string(from: date), "title": title,
     "source": source as Any? ?? NSNull(), "hasRecording": hasRecording]
  }
}

struct RecoveryStore {
  let directory: URL
  static let generations = 10
  private func metadataURL(_ file: URL) -> URL { file.appendingPathExtension("json") }
  func candidates() throws -> [URL] {
    guard FileManager.default.fileExists(atPath: directory.path) else { return [] }
    return try FileManager.default.contentsOfDirectory(
      at: directory, includingPropertiesForKeys: [.contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey],
      options: .skipsHiddenFiles
    ).filter {
      let values = try $0.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
      return values.isRegularFile == true && values.isSymbolicLink != true &&
        ["mod", "xm", "s3m", "it", "mptm", "resonance", "screamseq"].contains($0.pathExtension.lowercased())
    }.sorted { a, b in
      let x = (try? a.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
      let y = (try? b.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
      return x == y ? a.lastPathComponent > b.lastPathComponent : x > y
    }
  }
  func entries() throws -> [RecoveryEntry] {
    try candidates().map { file in
      let meta = metadataURL(file)
      var details = [String: Any]()
      if let size = try? meta.resourceValues(forKeys: [.fileSizeKey]).fileSize, size <= 65536,
        let data = try? Data(contentsOf: meta), let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
        details = json
      }
      return RecoveryEntry(url: file,
        date: (try? file.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast,
        title: details["title"] as? String ?? "Recovered song",
        source: details["source"] as? String, hasRecording: details["hasRecording"] as? Bool ?? false)
    }
  }
  @discardableResult func save(id: String, format: String, title: String = "Untitled", source: String? = nil,
    hasRecording: Bool = false, write: (URL) throws -> Void) throws -> URL {
    guard UUID(uuidString: id) != nil,
      ["mod", "xm", "s3m", "it", "mptm", "resonance", "screamseq"].contains(format)
    else { throw CocoaError(.fileWriteInvalidFileName) }
    let fm = FileManager.default
    try fm.createDirectory(at: directory, withIntermediateDirectories: true)
    let timestamp = UInt64(Date().timeIntervalSince1970 * 1_000_000)
    let unique = UUID().uuidString
    let destination = directory.appendingPathComponent("\(id)-\(timestamp)-\(unique).\(format)")
    let staging = directory.appendingPathComponent(".pending-\(unique).\(format)")
    defer { try? fm.removeItem(at: staging) }
    do {
      try write(staging)
      // Only complete, synchronized files become discoverable recovery copies.
      let handle = try FileHandle(forWritingTo: staging)
      defer { try? handle.close() }
      try handle.synchronize()
      let metadata: [String: Any] = ["title": title, "source": source as Any? ?? NSNull(), "hasRecording": hasRecording]
      try JSONSerialization.data(withJSONObject: metadata).write(to: metadataURL(destination), options: .atomic)
      try fm.moveItem(at: staging, to: destination)
    } catch {
      try? fm.removeItem(at: metadataURL(destination))
      throw error
    }
    // A cleanup failure must never invalidate a successfully written snapshot.
    if let own = try? candidates().filter({ $0.lastPathComponent.hasPrefix(id + "-") }) {
      for file in own.dropFirst(Self.generations) {
        try? fm.removeItem(at: file)
        if !fm.fileExists(atPath: file.path) { try? fm.removeItem(at: metadataURL(file)) }
      }
    }
    return destination
  }
  // Launch-time housekeeping across sessions. Recovery copies are the last line
  // of defence, so this only removes what is clearly obsolete: abandoned staging
  // files and older generations past the age limit. The newest generation of a
  // song may be its only copy, so age alone never removes it: only the generous
  // total cap can, oldest first. Nothing inside the age limit and nothing of a
  // protected session is ever removed.
  static let pendingAge: TimeInterval = 60 * 60
  static let maximumAge: TimeInterval = 30 * 24 * 60 * 60
  static let maximumCopies = 200
  @discardableResult func prune(protecting: Set<String>, now: Date = Date(), pendingAge: TimeInterval = RecoveryStore.pendingAge,
    maximumAge: TimeInterval = RecoveryStore.maximumAge, maximumCopies: Int = RecoveryStore.maximumCopies) -> [URL] {
    let fm = FileManager.default
    var removed = [URL]()
    func modified(_ file: URL) -> Date? { try? file.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate }
    func regular(_ file: URL) -> Bool {
      let values = try? file.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
      return values?.isRegularFile == true && values?.isSymbolicLink != true
    }
    func remove(_ file: URL, metadata: Bool) {
      guard (try? fm.removeItem(at: file)) != nil else { return }
      removed.append(file)
      if metadata { try? fm.removeItem(at: metadataURL(file)) }
    }
    guard fm.fileExists(atPath: directory.path),
      let all = try? fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.contentModificationDateKey, .isRegularFileKey, .isSymbolicLinkKey])
    else { return [] }
    // A file with an unreadable date is kept: unknown is never treated as old.
    for file in all where regular(file) {
      guard let date = modified(file), now.timeIntervalSince(date) > pendingAge else { continue }
      let name = file.lastPathComponent
      if name.hasPrefix(".pending-") { remove(file, metadata: false) }
      // Metadata left behind when a write stopped before its copy was published.
      else if !name.hasPrefix("."), file.pathExtension == "json", UUID(uuidString: String(name.prefix(36))) != nil,
        !fm.fileExists(atPath: file.deletingPathExtension().path) { remove(file, metadata: false) }
    }
    guard let copies = try? candidates() else { return removed }  // newest first
    var newest = Set<String>(), kept = [(file: URL, fixed: Bool)]()
    for file in copies {
      let name = file.lastPathComponent, id = String(name.prefix(36))
      // Only files this store named are ever pruned.
      guard UUID(uuidString: id) != nil, name.dropFirst(36).hasPrefix("-"), let date = modified(file) else { kept.append((file, true)); continue }
      let first = newest.insert(id).inserted, recent = now.timeIntervalSince(date) <= maximumAge
      if protecting.contains(id) || recent { kept.append((file, true)) }
      else if first { kept.append((file, false)) }
      else { remove(file, metadata: true); if fm.fileExists(atPath: file.path) { kept.append((file, true)) } }
    }
    var excess = kept.count - max(0, maximumCopies)
    for entry in kept.reversed() where excess > 0 && !entry.fixed {
      remove(entry.file, metadata: true)
      if !fm.fileExists(atPath: entry.file.path) { excess -= 1 }
    }
    return removed
  }
  func clear(id: String) throws {
    guard UUID(uuidString: id) != nil else { return }
    for file in try candidates() where file.lastPathComponent.hasPrefix(id + "-") {
      try FileManager.default.removeItem(at: file)
      try? FileManager.default.removeItem(at: metadataURL(file))
    }
  }
  static func documentID(for file: URL) -> String {
    let id = String(file.lastPathComponent.prefix(36))
    return UUID(uuidString: id) != nil ? id : UUID().uuidString
  }
}
