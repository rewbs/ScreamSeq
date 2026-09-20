import CryptoKit
import Darwin
import Foundation

/// A user-only Unix socket. Parsing and blocking I/O never run on the UI/audio threads.
/// The handler and reply cache are confined to the main queue; the app serializes
/// document work on its existing worker queue.
final class AutomationServer {
  typealias Reply = ([String: Any]) -> Void
  typealias Handler = (String, [String: Any], @escaping Reply) -> Void
  static let maxBytes = 32 * 1024 * 1024
  let directory: URL
  let socketPath: String
  let endpointURL: URL
  private var listener: DispatchSourceRead?
  private var active = false
  private let clients = DispatchSemaphore(value: 4)
  private let io = DispatchQueue(label: "org.resonance.automation.io", attributes: .concurrent)
  private let handler: Handler
  private var cache = [String: (String, [String: Any], Int)]()
  private var cacheOrder = [String]()
  private var cacheBytes = 0
  private var pending = Set<String>()

  static func error(_ code: Int, _ message: String, data: [String: Any] = [:]) -> [String: Any] {
    ["error": ["code": code, "message": message, "data": data]]
  }

  init(discoveryDirectory: URL? = nil, handler: @escaping Handler) throws {
    self.handler = handler
    var template = Array("/tmp/resonance-\(geteuid())-XXXXXX".utf8CString)
    guard let temporary = mkdtemp(&template) else {
      throw Self.posix("Create private API directory")
    }
    directory = URL(fileURLWithPath: String(cString: temporary), isDirectory: true)
    socketPath = directory.appendingPathComponent("api.sock").path
    let discovery =
      discoveryDirectory
      ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("Resonance/Automation", isDirectory: true)
    endpointURL = discovery.appendingPathComponent("\(getpid()).json")
    do {
      try FileManager.default.createDirectory(
        at: discovery, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
      var info = stat()
      guard lstat(discovery.path, &info) == 0, info.st_uid == geteuid(),
        (info.st_mode & S_IFMT) == S_IFDIR, (info.st_mode & 0o077) == 0
      else {
        throw NSError(
          domain: "ResonanceAPI", code: 1,
          userInfo: [
            NSLocalizedDescriptionKey:
              "API discovery directory must be a private directory owned by you."
          ])
      }
      let fd = socket(AF_UNIX, SOCK_STREAM, 0)
      guard fd >= 0 else { throw Self.posix("Create API socket") }
      var installed = false
      defer { if !installed { close(fd) } }
      _ = fcntl(fd, F_SETFD, FD_CLOEXEC)
      _ = fcntl(fd, F_SETFL, O_NONBLOCK)
      var address = sockaddr_un()
      address.sun_family = sa_family_t(AF_UNIX)
      address.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
      let bytes = Array(socketPath.utf8CString)
      withUnsafeMutableBytes(of: &address.sun_path) { target in
        target.copyBytes(from: bytes.map { UInt8(bitPattern: $0) })
      }
      let bound = withUnsafePointer(to: &address) { pointer in
        pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
          Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
      }
      guard bound == 0, chmod(socketPath, 0o600) == 0, listen(fd, 4) == 0 else {
        throw Self.posix("Listen on API socket")
      }
      let metadata: [String: Any] = [
        "version": 1, "pid": getpid(), "socket": socketPath,
        "startedAt": ISO8601DateFormatter().string(from: Date()),
        "application": Bundle.main.executablePath ?? "ScreamSeq",
      ]
      let data = try JSONSerialization.data(
        withJSONObject: metadata, options: [.sortedKeys, .prettyPrinted])
      try data.write(to: endpointURL, options: .atomic)
      _ = chmod(endpointURL.path, 0o600)
      let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: .main)
      source.setEventHandler { [weak self] in self?.acceptClients(fd) }
      source.setCancelHandler { close(fd) }
      active = true
      listener = source
      installed = true
      source.resume()
    } catch {
      try? FileManager.default.removeItem(at: directory)
      throw error
    }
  }

  func stop() {
    active = false
    listener?.cancel()
    listener = nil
    try? FileManager.default.removeItem(at: endpointURL)
    try? FileManager.default.removeItem(at: directory)
  }

  private static func posix(_ action: String) -> NSError {
    NSError(
      domain: NSPOSIXErrorDomain, code: Int(errno),
      userInfo: [NSLocalizedDescriptionKey: "\(action): \(String(cString: strerror(errno)))"])
  }

  private func acceptClients(_ fd: Int32) {
    // Bound work per UI-queue turn even if clients continuously connect.
    for _ in 0..<16 where active {
      let client = accept(fd, nil, nil)
      if client < 0 { return }
      var uid: uid_t = 0
      var gid: gid_t = 0
      guard getpeereid(client, &uid, &gid) == 0, uid == geteuid(),
        clients.wait(timeout: .now()) == .success
      else {
        close(client)
        continue
      }
      _ = fcntl(client, F_SETFD, FD_CLOEXEC)
      _ = fcntl(client, F_SETFL, 0)
      var timeout = timeval(tv_sec: 5, tv_usec: 0)
      var yes: Int32 = 1
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
      setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
      setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, socklen_t(MemoryLayout<Int32>.size))
      io.async { [self] in
        defer {
          close(client)
          clients.signal()
        }
        var input = Data()
        var buffer = [UInt8](repeating: 0, count: 16384)
        let deadline = Date().addingTimeInterval(10)
        while input.count <= Self.maxBytes && Date() < deadline {
          let count = read(client, &buffer, buffer.count)
          if count <= 0 { return }
          input.append(contentsOf: buffer.prefix(count))
          if buffer.prefix(count).contains(10) { break }
        }
        var response = Self.error(-32600, "Expected one bounded JSON request followed by a newline")
        response["jsonrpc"] = "2.0"
        response["id"] = NSNull()
        if input.count <= Self.maxBytes, input.last == 10,
          let request = try? JSONSerialization.jsonObject(with: input) as? [String: Any]
        {
          let ready = DispatchSemaphore(value: 0)
          let canonical =
            (try? JSONSerialization.data(withJSONObject: request, options: [.sortedKeys])) ?? Data()
          let digest = SHA256.hash(data: canonical).map { String(format: "%02x", $0) }.joined()
          // Reply only transfers immutable JSON values back to this I/O thread.
          DispatchQueue.main.async { [self] in
            route(request, digest: digest, requestBytes: canonical.count) { value in
              response = value
              ready.signal()
            }
          }
          guard ready.wait(timeout: .now() + 120) == .success else { return }
        }
        guard
          var output = try? JSONSerialization.data(
            withJSONObject: response, options: [.sortedKeys]), output.count < Self.maxBytes
        else { return }
        output.append(10)
        output.withUnsafeBytes { raw in
          var offset = 0
          while offset < raw.count {
            let count = write(client, raw.baseAddress!.advanced(by: offset), raw.count - offset)
            if count <= 0 { break }
            offset += count
          }
        }
      }
    }
  }

  private func route(
    _ request: [String: Any], digest: String, requestBytes: Int, completion: @escaping Reply
  ) {
    guard active else {
      completion([
        "jsonrpc": "2.0", "id": request["id"] ?? NSNull(),
        "error": ["code": -32002, "message": "API is disabled"],
      ])
      return
    }
    guard request["jsonrpc"] as? String == "2.0", let id = request["id"] as? String, !id.isEmpty,
      id.utf8.count <= 80,
      let method = request["method"] as? String, method.utf8.count <= 80,
      let params = request["params"] as? [String: Any],
      Set(request.keys).isSubset(of: ["jsonrpc", "id", "method", "params"])
    else {
      completion([
        "jsonrpc": "2.0", "id": NSNull(),
        "error": [
          "code": -32600,
          "message": "Use jsonrpc 2.0, a unique string id, method and object params",
        ],
      ])
      return
    }
    func reply(_ body: [String: Any]) -> [String: Any] {
      var value = body
      value["jsonrpc"] = "2.0"
      value["id"] = id
      return value
    }
    if let cached = cache[id] {
      completion(
        cached.0 == digest
          ? cached.1
          : reply(Self.error(-32600, "Request id was already used for a different request")))
      return
    }
    guard pending.insert(id).inserted else {
      completion(reply(Self.error(-32002, "This request is still running; retry the same id")))
      return
    }
    handler(method, params) { [weak self] body in
      guard let self else { return }
      self.pending.remove(id)
      let response = reply(body)
      // A conservative bound for cached mutation replies, including before/after
      // pattern patches. Large payloads and reads are never cached. In particular,
      // no base64 or JSON encoding of plugin/sample data happens on the main thread.
      let count = requestBytes * 20 + 4096
      // Busy requests are retryable. Large results are not retained in memory.
      if !method.hasSuffix(".get"), !["api.describe", "plugin.discover", "plugin.preset.inspect", "pattern.commands", "automation.pattern.copy", "arrangement.matrix", "plugin.meters", "mixer.meters", "sample.library.search", "sample.library.inspect"].contains(method),
        (body["error"] as? [String: Any])?["code"] as? Int != -32002, count <= 4 * 1024 * 1024
      {
        self.cache[id] = (digest, response, count)
        self.cacheOrder.append(id)
        self.cacheBytes += count
        while self.cacheOrder.count > 64 || self.cacheBytes > 8 * 1024 * 1024 {
          let oldest = self.cacheOrder.removeFirst()
          self.cacheBytes -= self.cache.removeValue(forKey: oldest)?.2 ?? 0
        }
      }
      completion(response)
    }
  }
}
