import AppKit

extension AppController {
  var sampleLibraryDirectory: URL {
    if automationTest, let root = ProcessInfo.processInfo.environment["RESONANCE_AUTOMATION_TEST_DIRECTORY"] {
      return URL(fileURLWithPath: root, isDirectory: true).appendingPathComponent("SampleLibrary", isDirectory: true)
    }
    return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("Resonance/SampleLibrary", isDirectory: true)
  }
  func prepareSampleLibrary() {
    guard !sampleLibraryConnected else { return }; sampleLibraryConnected = true
    sampleLibrary.onChange = { [weak self] in
      guard let self else { return }; (self.sampleBrowserWindow?.contentView as? SampleBrowser)?.updateLibrary(self.sampleLibrary.status)
    }
    let standard = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("samples", isDirectory: true).path
    sampleLibrary.load(defaultRoots: !automationTest && FileManager.default.fileExists(atPath: standard) ? [standard] : [])
  }
  @objc func showSampleBrowser() {
    guard !busy else { return }; prepareSampleLibrary()
    if let sampleBrowserWindow { if !automationTest && !inspectionTest { sampleBrowserWindow.makeKeyAndOrderFront(nil) }; return }
    let browser = SampleBrowser(frame: .zero)
    browser.createInstruments.state = model.instruments.isEmpty ? .off : .on
    browser.onSearch = { [weak self] query, reply in self?.sampleLibrary.search(query) { result, _ in reply(result) } }
    browser.onFindMultisample = { [weak self] path, reply in self?.sampleLibrary.multisample(for: path) { group, _ in reply(group) } }
    browser.onImportMultisample = { [weak self] group in self?.showMultisampleImport(group) }
    browser.onStop = { [weak self] in self?.sampleAudition.stop() }
    browser.onVolume = { [weak self] db in self?.sampleAudition.volume = Float(pow(10, db / 20)) }
    browser.onInspect = { [weak self] path, audible, reply in
      guard let self else { return }
      self.sampleAudition.prepare(path: path, audible: audible && !self.automationTest && !self.inspectionTest,
        decoder: { try TrackerSession.inspectSampleFile($0) as! [String: Any] }, completion: reply)
    }
    browser.onImport = { [weak self] paths, instruments, reply in self?.loadSamplePaths(paths, instruments: instruments, reply: reply) }
    browser.onAddFolder = { [weak self, weak browser] in
      guard let self else { return }; let panel = NSOpenPanel()
      panel.title = "Add sample folders"; panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.allowsMultipleSelection = true
      panel.beginSheetModal(for: self.sampleBrowserWindow!) { response in
        guard response == .OK else { return }
        do { try self.sampleLibrary.setRoots(self.sampleLibrary.roots + panel.urls.map(\.path)) }
        catch { browser?.status.stringValue = error.localizedDescription }
      }
    }
    browser.onRemoveFolder = { [weak self, weak browser] path in
      guard let self else { return }
      do { try self.sampleLibrary.setRoots(self.sampleLibrary.roots.filter { $0 != path }) }
      catch { browser?.status.stringValue = error.localizedDescription }
    }
    browser.onRescan = { [weak self] in self?.sampleLibrary.rescan() }
    browser.onChooseFiles = { [weak self, weak browser] in
      guard let self else { return }; let panel = NSOpenPanel()
      panel.title = "Load samples"; panel.allowsMultipleSelection = true; panel.allowsOtherFileTypes = true
      panel.allowedContentTypes = [.wav, .aiff, .mp3]
      panel.beginSheetModal(for: self.sampleBrowserWindow!) { response in if response == .OK { browser?.loadPaths(panel.urls.map(\.path)) } }
    }
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1140, height: 820), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Sample library"; win.contentMinSize = NSSize(width: 980, height: 770); win.isReleasedWhenClosed = false; win.delegate = self; win.contentView = browser
    sampleBrowserWindow = win; browser.updateLibrary(sampleLibrary.status)
    if !automationTest && !inspectionTest { win.center(); win.makeKeyAndOrderFront(nil); win.makeFirstResponder(browser.search) }
  }
  func showMultisampleImport(_ group: MultisampleGroup) {
    guard !busy, let parent = sampleBrowserWindow, parent.attachedSheet == nil else { return }
    let view = MultisampleImportView(group: group, revision: session.automationRevision)
    let sheet = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 820, height: 720), styleMask: [.titled], backing: .buffered, defer: false)
    sheet.title = "Import multi-sample instrument"; sheet.contentView = view; sheet.isReleasedWhenClosed = false
    view.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    view.onClose = { [weak self, weak parent, weak sheet] in self?.sampleAudition.stop(); if let sheet { parent?.endSheet(sheet) } }
    view.onStop = { [weak self] in self?.sampleAudition.stop() }
    view.onPreview = { [weak self, weak view] path in
      guard let self else { return }
      self.sampleAudition.prepare(path: path, audible: !self.automationTest && !self.inspectionTest,
        decoder: { try TrackerSession.inspectSampleFile($0) as! [String: Any] }) { result in
          if case .failure(let error) = result {
            let failure = error as NSError
            if failure.domain != NSCocoaErrorDomain || failure.code != NSUserCancelledError { view?.status.stringValue = error.localizedDescription }
          }
        }
    }
    view.onApplied = { [weak self, weak view] instrument in
      guard let self else { return }; view?.onClose?(); self.instrumentEditor.index = instrument
      self.patternView.instrument = instrument; self.showEditor(2); self.refreshAssets()
      (parent.contentView as? SampleBrowser)?.status.stringValue = "Loaded \(group.name) as instrument \(instrument) · \(group.members.count) samples · one Undo step"
    }
    if !automationTest && !inspectionTest { parent.beginSheet(sheet) }
  }
  func loadSamplePaths(_ paths: [String], instruments: Bool, reply: @escaping (Result<Int, Error>) -> Void) {
    guard !busy else { reply(.failure(sampleLibraryError(-32002, "The song is busy; try loading again shortly"))); return }
    handleAutomation("sample.importMany", params: ["paths": paths, "createInstruments": instruments, "expectedRevision": session.automationRevision]) { [weak self] response in
      guard let self else { return }
      if let error = response["error"] as? [String: Any] { reply(.failure(self.sampleLibraryError(error["code"] as? Int ?? -32003, error["message"] as? String ?? "Import failed"))); return }
      guard let data = (response["result"] as? [String: Any])?["data"] as? [String: Any], let samples = data["samples"] as? [[String: Any]], let first = samples.first, let sample = first["sample"] as? Int else { reply(.failure(self.sampleLibraryError(-32003, "No imported samples returned"))); return }
      self.sampleEditor.index = sample
      if let instrument = first["instrument"] as? Int, instrument > 0 { self.patternView.instrument = instrument }
      else if self.model.instruments.isEmpty { self.patternView.instrument = sample }
      self.showEditor(1); self.refreshAssets(); reply(.success(samples.count))
    }
  }
  private func sampleLibraryError(_ code: Int, _ message: String) -> NSError { NSError(domain: "SampleLibrary", code: code, userInfo: [NSLocalizedDescriptionKey: message]) }
  func handleSampleLibraryAutomation(_ method: String, params p: [String: Any], reply: @escaping AutomationServer.Reply) -> Bool {
    guard method.hasPrefix("sample.library.") else { return false }; prepareSampleLibrary()
    func finish(_ data: [String: Any]) { reply(["result": ["revision": "library:" + sampleLibrary.revision, "data": data, "changed": false, "playbackStopped": false]]) }
    func keys(_ allowed: [String]) throws { guard Set(p.keys).isSubset(of: Set(allowed)) else { throw sampleLibraryError(-32602, "Unknown sample library field") } }
    func string(_ key: String, fallback: String? = nil) throws -> String {
      if p[key] == nil, let fallback { return fallback }
      guard let value = p[key] as? String, value.count <= 4096, !value.contains("\0") else { throw sampleLibraryError(-32602, "\(key) must be a bounded string") }; return value
    }
    func strings(_ key: String, maximum: Int) throws -> [String] {
      guard let values = p[key] as? [String], values.count <= maximum, values.allSatisfy({ $0.count <= 4096 && !$0.contains("\0") }) else { throw sampleLibraryError(-32602, "Invalid \(key) list") }; return values
    }
    func integer(_ key: String, fallback: Int, maximum: Int) throws -> Int {
      guard let raw = p[key] else { return fallback }
      guard let value = raw as? NSNumber, CFGetTypeID(value) != CFBooleanGetTypeID(), value.doubleValue.isFinite,
        value.doubleValue >= 0, value.doubleValue <= Double(maximum), let integer = Int(exactly: value.doubleValue) else { throw sampleLibraryError(-32602, "Invalid \(key)") }; return integer
    }
    func version() throws { guard try string("expectedLibraryRevision") == sampleLibrary.revision else { throw sampleLibraryError(-32001, "Sample library changed; read sample.library.get again") } }
    do {
      switch method {
      case "sample.library.get": try keys([]); finish(sampleLibrary.status)
      case "sample.library.multisample.get":
        try keys(["path", "expectedLibraryRevision"])
        if p["expectedLibraryRevision"] != nil { try version() }
        let path = try string("path"); guard (path as NSString).isAbsolutePath else { throw sampleLibraryError(-32602, "Use an absolute sample path") }
        sampleLibrary.multisample(for: path) { group, revision in
          reply(["result": ["revision": "library:" + revision, "data": ["group": group?.dictionary as Any? ?? NSNull(), "libraryRevision": revision], "changed": false, "playbackStopped": false]])
        }
      case "sample.library.search":
        try keys(["query", "tags", "root", "tagQuery", "offset", "limit", "expectedLibraryRevision"])
        if p["expectedLibraryRevision"] != nil { try version() }
        var query = SampleLibraryQuery(text: try string("query", fallback: ""), tags: p["tags"] == nil ? [] : try strings("tags", maximum: 32),
          tagText: try string("tagQuery", fallback: ""), offset: try integer("offset", fallback: 0, maximum: 250_000), limit: try integer("limit", fallback: 100, maximum: 1000))
        guard query.limit > 0 else { throw sampleLibraryError(-32602, "limit must be positive") }
        if p["root"] != nil { query.root = SampleLibraryIndex.canonicalPath(try string("root")); guard sampleLibrary.roots.contains(query.root!) else { throw sampleLibraryError(-32602, "Root is not in this library") } }
        sampleLibrary.search(query) { result, revision in
          var data = result.dictionary; data["libraryRevision"] = revision; data["indexing"] = self.sampleLibrary.indexing
          reply(["result": ["revision": "library:" + revision, "data": data, "changed": false, "playbackStopped": false]])
        }
      case "sample.library.roots.set":
        try keys(["roots", "expectedLibraryRevision"]); try version()
        try sampleLibrary.setRoots(strings("roots", maximum: 32)); finish(sampleLibrary.status)
      case "sample.library.rescan":
        try keys(["expectedLibraryRevision"]); try version()
        guard !sampleLibrary.indexing else { throw sampleLibraryError(-32002, "Sample library is already indexing") }
        sampleLibrary.rescan(); finish(sampleLibrary.status)
      case "sample.library.inspect":
        try keys(["path"]); let path = try string("path")
        guard (path as NSString).isAbsolutePath else { throw sampleLibraryError(-32602, "Use an absolute sample path") }
        sampleInspectionQueue.async {
          do { let data = try SampleAuditionData(TrackerSession.inspectSampleFile(path) as! [String: Any]); DispatchQueue.main.async { finish(data.dictionary) } }
          catch { DispatchQueue.main.async { reply(AutomationServer.error(-32003, error.localizedDescription)) } }
        }
      case "sample.library.preview":
        try keys(["path", "gainDB"]); let path = try string("path")
        guard (path as NSString).isAbsolutePath else { throw sampleLibraryError(-32602, "Use an absolute sample path") }
        if let raw = p["gainDB"] {
          guard let number = raw as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(), number.doubleValue.isFinite, (-60...0).contains(number.doubleValue) else { throw sampleLibraryError(-32602, "Preview gain must be −60 to 0 dB") }
          sampleAudition.volume = Float(pow(10, number.doubleValue / 20))
        }
        let audible = !automationTest && !inspectionTest
        sampleAudition.prepare(path: path, audible: audible, decoder: { try TrackerSession.inspectSampleFile($0) as! [String: Any] }) { result in
          switch result { case .success(let sample): var data = sample.dictionary; data["audible"] = audible; finish(data)
          case .failure(let error): reply(AutomationServer.error(-32003, error.localizedDescription)) }
        }
      case "sample.library.preview.stop": try keys([]); sampleAudition.stop(); finish(["playing": false])
      default: throw sampleLibraryError(-32601, "Unknown sample library method")
      }
    } catch { reply(AutomationServer.error((error as NSError).code, error.localizedDescription)) }
    return true
  }
}
