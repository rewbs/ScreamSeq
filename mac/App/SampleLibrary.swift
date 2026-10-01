import Foundation
import Darwin

struct SampleLibraryEntry: Codable, Equatable {
  let path: String, root: String, name: String, folders: [String]
  let bytes: Int64, modified: Double
  var dictionary: [String: Any] { ["path": path, "root": root, "name": name, "folders": folders, "bytes": bytes, "modified": modified] }
}
struct SampleLibrarySnapshot: Codable {
  let version: Int
  let roots: [String], entries: [SampleLibraryEntry], warnings: [String]
  let indexedAt: Date
}
struct SampleLibraryQuery {
  var text = "", tags = [String](), root: String?, tagText = "", offset = 0, limit = 500
}
struct SampleLibraryResults {
  let items: [SampleLibraryEntry], total: Int, tags: [(String, Int)], offset: Int
  var dictionary: [String: Any] { ["items": items.map(\.dictionary), "total": total, "offset": offset,
    "tags": tags.map { ["name": $0.0, "count": $0.1] as [String: Any] }] }
}
final class SampleLibraryIndex {
  // Foundation sometimes rewrites /private/var to /var while its enumerator
  // returns /private/var paths. Use one real filesystem spelling throughout.
  static func canonicalPath(_ path: String) -> String {
    guard let resolved = path.withCString({ realpath($0, nil) }) else { return path }
    defer { free(resolved) }; return String(cString: resolved)
  }
  static let extensions: Set<String> = ["wav", "wave", "aif", "aiff", "aifc", "flac", "mp3", "ogg", "caf", "w64", "au", "snd", "its", "s3i", "iff", "8svx", "brr"]
  static func fold(_ text: String) -> String { text.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "en_US_POSIX")) }
  let snapshot: SampleLibrarySnapshot
  private let multisamples: [String: MultisampleGroup]
  private let searchable: [(entry: SampleLibraryEntry, text: String, tags: Set<String>, folderNames: [(String, String)])]
  init(_ snapshot: SampleLibrarySnapshot) {
    self.snapshot = snapshot
    var groups = [String: MultisampleGroup]()
    for group in MultisampleGroup.detect(snapshot.entries) { for member in group.members { groups[member.entry.path] = group } }
    multisamples = groups
    searchable = snapshot.entries.map { entry in
      var names = [String: String]()
      for folder in entry.folders { names[Self.fold(folder)] = folder }
      return (entry, Self.fold((entry.folders + [entry.name]).joined(separator: " ")), Set(names.keys), names.map { ($0.key, $0.value) })
    }
  }
  func multisample(for path: String) -> MultisampleGroup? { multisamples[Self.canonicalPath(path)] }
  static func scan(roots: [String], cancelled: () -> Bool = { false }) throws -> SampleLibraryIndex {
    let roots = roots.map(canonicalPath)
    let manager = FileManager.default
    let keys: [URLResourceKey] = [.isRegularFileKey, .isDirectoryKey, .isSymbolicLinkKey, .fileSizeKey, .contentModificationDateKey]
    var entries = [SampleLibraryEntry](), seen = Set<String>(), warnings = [String]()
    for root in roots {
      if cancelled() { throw CocoaError(.userCancelled) }
      var isDirectory: ObjCBool = false
      guard manager.fileExists(atPath: root, isDirectory: &isDirectory), isDirectory.boolValue else { warnings.append("Folder is unavailable: \(root)"); continue }
      let url = URL(fileURLWithPath: root, isDirectory: true)
      guard let iterator = manager.enumerator(at: url, includingPropertiesForKeys: keys, options: [.skipsHiddenFiles, .skipsPackageDescendants], errorHandler: { file, error in
        if warnings.count < 20 { warnings.append("\(file.lastPathComponent): \(error.localizedDescription)") }; return true
      }) else { warnings.append("Cannot read \(root)"); continue }
      for case let file as URL in iterator {
        if cancelled() { throw CocoaError(.userCancelled) }
        let values = try? file.resourceValues(forKeys: Set(keys))
        if file.lastPathComponent == "__MACOSX" || values?.isSymbolicLink == true { iterator.skipDescendants(); continue }
        guard values?.isRegularFile == true, Self.extensions.contains(file.pathExtension.lowercased()), seen.insert(file.path).inserted else { continue }
        let relative = String(file.path.dropFirst(root.count + (root.hasSuffix("/") ? 0 : 1)))
        let folders = [url.lastPathComponent] + relative.split(separator: "/").dropLast().map(String.init)
        entries.append(SampleLibraryEntry(path: file.path, root: root, name: file.lastPathComponent, folders: folders,
          bytes: Int64(values?.fileSize ?? 0), modified: values?.contentModificationDate?.timeIntervalSince1970 ?? 0))
        if entries.count >= 250_000 { throw NSError(domain: "SampleLibrary", code: 1, userInfo: [NSLocalizedDescriptionKey: "Library exceeds 250,000 files. Choose smaller sample folders."]) }
      }
    }
    entries.sort { $0.path.localizedStandardCompare($1.path) == .orderedAscending }
    return SampleLibraryIndex(SampleLibrarySnapshot(version: 2, roots: roots, entries: entries, warnings: warnings, indexedAt: Date()))
  }
  // Quoted phrases and -excluded words complement all-word path matching.
  static func terms(_ text: String) -> [(text: String, excluded: Bool)] {
    var tokens = [String](), current = "", quoted = false
    for character in text {
      if character == "\"" { quoted.toggle() }
      else if character.isWhitespace && !quoted { if !current.isEmpty { tokens.append(current); current = "" } }
      else { current.append(character) }
    }
    if !current.isEmpty { tokens.append(current) }
    return tokens.compactMap { token in
      let excluded = token.hasPrefix("-") && token.count > 1
      let term = fold(excluded ? String(token.dropFirst()) : token)
      return term.isEmpty ? nil : (term, excluded)
    }
  }
  func search(_ query: SampleLibraryQuery) -> SampleLibraryResults {
    let terms = Self.terms(query.text), tags = Set(query.tags.map(Self.fold)), tagText = Self.fold(query.tagText)
    let root = query.root.map(Self.canonicalPath).map { $0.hasSuffix("/") ? $0 : $0 + "/" }
    var items = [SampleLibraryEntry](), total = 0, counts = [String: (String, Int)]()
    let offset = max(0, query.offset), limit = max(1, min(1000, query.limit))
    for record in searchable {
      guard root == nil || record.entry.path.hasPrefix(root!),
        tags.isSubset(of: record.tags), terms.allSatisfy({ record.text.contains($0.text) != $0.excluded }) else { continue }
      if total >= offset && items.count < limit { items.append(record.entry) }; total += 1
      // Each ancestor is an inherited tag, counted once per file even where
      // sample packs repeat the same directory name at several depths.
      for (key, folder) in record.folderNames {
        guard tagText.isEmpty || key.contains(tagText) else { continue }
        let old = counts[key] ?? (folder, 0); counts[key] = (old.0, old.1 + 1)
      }
    }
    let facets = counts.values.sorted { $0.1 == $1.1 ? $0.0.localizedStandardCompare($1.0) == .orderedAscending : $0.1 > $1.1 }
    return SampleLibraryResults(items: items, total: total, tags: Array(facets.prefix(1000)), offset: offset)
  }
}

/// Main-thread owner; scans/cache IO and queries run on separate utility queues.
/// Publishing a new immutable index never blocks searches of the old one.
final class SampleLibrary {
  let directory: URL
  private let scanQueue = DispatchQueue(label: "org.resonance.sample-library.scan", qos: .utility)
  private let searchQueue = DispatchQueue(label: "org.resonance.sample-library.search", qos: .userInitiated)
  private var scanWork: DispatchWorkItem?
  private var loaded = false, generation = 0
  private(set) var index: SampleLibraryIndex?
  private(set) var roots = [String](), indexing = false, revision = UUID().uuidString, error: String?
  var onChange: (() -> Void)?
  init(directory: URL) { self.directory = directory }
  var status: [String: Any] { ["roots": roots, "count": index?.snapshot.entries.count ?? 0, "indexing": indexing,
    "ready": index != nil, "libraryRevision": revision, "error": error as Any? ?? NSNull(),
    "indexedAt": index.map { ISO8601DateFormatter().string(from: $0.snapshot.indexedAt) } as Any? ?? NSNull(),
    "warnings": index?.snapshot.warnings ?? [], "extensions": SampleLibraryIndex.extensions.sorted()] }
  func load(defaultRoots: [String]) {
    guard !loaded else { return }; loaded = true; indexing = true; onChange?()
    generation += 1; let generation = generation
    let directory = directory
    scanQueue.async {
      let configured = ((try? Data(contentsOf: directory.appendingPathComponent("roots.json"))).flatMap { try? JSONDecoder().decode([String].self, from: $0) } ?? defaultRoots).map(SampleLibraryIndex.canonicalPath)
      let url = directory.appendingPathComponent("index.json")
      var cached: SampleLibraryIndex?
      if let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize, size < 256 * 1024 * 1024,
        let data = try? Data(contentsOf: url), let snapshot = try? JSONDecoder().decode(SampleLibrarySnapshot.self, from: data),
        snapshot.version == 2, snapshot.roots == configured, snapshot.entries.count <= 250_000 {
        cached = SampleLibraryIndex(snapshot)
      }
      DispatchQueue.main.async {
        guard self.generation == generation else { return }
        self.roots = configured; self.index = cached; self.indexing = false; self.revision = UUID().uuidString; self.onChange?()
        if cached == nil { self.rescan() }
      }
    }
  }
  func setRoots(_ paths: [String]) throws {
    guard paths.count <= 32 else { throw failure("Use at most 32 library folders") }
    let manager = FileManager.default
    var normalized = [String]()
    for path in paths {
      guard (path as NSString).isAbsolutePath, !path.contains("\0"), path.count <= 4096 else { throw failure("Choose an absolute folder path") }
      let root = SampleLibraryIndex.canonicalPath(path)
      var directory: ObjCBool = false
      guard manager.fileExists(atPath: root, isDirectory: &directory), directory.boolValue else { throw failure("Folder is unavailable: \(path)") }
      if !normalized.contains(root) { normalized.append(root) }
    }
    // Persist the small root list before publishing it. The file is never
    // inside a sample pack and removing a root never removes its files.
    try manager.createDirectory(at: directory, withIntermediateDirectories: true)
    try JSONEncoder().encode(normalized).write(to: directory.appendingPathComponent("roots.json"), options: .atomic)
    loaded = true; roots = normalized; index = nil; revision = UUID().uuidString; rescan()
  }
  func rescan() {
    generation += 1; let generation = generation, roots = roots, directory = directory
    scanWork?.cancel(); indexing = true; error = nil; onChange?()
    let work = DispatchWorkItem { [weak self] in
      guard let self else { return }
      do {
        // Old requests may finish, but generation checks prevent publication
        // after changing roots. All disk operations remain serial.
        let next = try SampleLibraryIndex.scan(roots: roots)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try JSONEncoder().encode(next.snapshot).write(to: directory.appendingPathComponent("index.json"), options: .atomic)
        DispatchQueue.main.async {
          guard self.generation == generation else { return }
          self.index = next; self.indexing = false; self.error = nil; self.revision = UUID().uuidString; self.onChange?()
        }
      } catch {
        DispatchQueue.main.async { guard self.generation == generation else { return }; self.indexing = false; self.error = error.localizedDescription; self.onChange?() }
      }
    }
    scanWork = work; scanQueue.async(execute: work)
  }
  func search(_ query: SampleLibraryQuery, completion: @escaping (SampleLibraryResults, String) -> Void) {
    let index = index, revision = revision
    searchQueue.async {
      let result = index?.search(query) ?? SampleLibraryResults(items: [], total: 0, tags: [], offset: query.offset)
      DispatchQueue.main.async { completion(result, revision) }
    }
  }
  func multisample(for path: String, completion: @escaping (MultisampleGroup?, String) -> Void) {
    let index = index, revision = revision
    searchQueue.async { let group = index?.multisample(for: path); DispatchQueue.main.async { completion(group, revision) } }
  }
  private func failure(_ text: String) -> NSError { NSError(domain: "SampleLibrary", code: -32602, userInfo: [NSLocalizedDescriptionKey: text]) }
}
