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
