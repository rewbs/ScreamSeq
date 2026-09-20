import AppKit

// An opt-in, application-owned integration workload. CUA inspection complements
// these model/layout checks; this does not synthesize OS input or capture screens.
final class UIQualification {
  unowned let app: AppController
  var timer: Timer?
  var start = 0.0, iteration = 0, edits = 0, saves = 0, undos = 0
  var failures = [String]()
  var finished = false
  var measurementStarted = false
  var activity: NSObjectProtocol?
  var priorWindowLevel: NSWindow.Level?
  private let reporter = DispatchQueue(label: "org.resonance.qualification-report", qos: .utility)
  private var nextProgress = 0.0
  private var visibleSince: Double?
  private var readyDeadline = CFAbsoluteTimeGetCurrent() + 60
  var requestedForeground = false
  var usesVST3 = false
  let duration: Double
  let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
    "resonance-ui-qualification-" + UUID().uuidString)
  init(_ app: AppController) {
    self.app = app
    let args = CommandLine.arguments
    if let index = args.firstIndex(of: "--ui-test-seconds"), index + 1 < args.count {
      duration = min(3600, max(15, Double(args[index + 1]) ?? 60))
    } else {
      duration = 60
    }
  }
  func require(_ condition: Bool, _ message: String) {
    if !condition && !failures.contains(message) { failures.append(message) }
  }
  func startWhenReady() {
    if !requestedForeground {
      requestedForeground = true
      priorWindowLevel = app.window.level
      app.window.level = .floating
      app.window.makeKeyAndOrderFront(nil)
      NSApp.activate(ignoringOtherApps:true)
    }
    guard CFAbsoluteTimeGetCurrent() < readyDeadline else {
      failures.append("No visible active window was available before setup")
      finish()
      return
    }
    guard !app.busy, NSApp.isActive, app.window.occlusionState.contains(.visible) else {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { self.startWhenReady() }
      return
    }
    do {
      try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    } catch {
      fail(error)
      return
    }
    app.window.setContentSize(NSSize(width: 1360, height: 850))
    app.window.center()
    app.window.level = .floating
    activity = ProcessInfo.processInfo.beginActivity(
      options: [.userInitiated, .idleDisplaySleepDisabled],
      reason: "Running visible tracker performance qualification")
    operation({
      // Extend the built-in demo if no long fixture was supplied.
      if self.app.model.orders.count == 1 {
        for _ in 0..<Int(ceil(self.duration / 7)) {
          _ = try self.app.session.addPattern(64, duplicate: true, source: 0)
        }
      }
      if let index = CommandLine.arguments.firstIndex(of: "--ui-test-vst3"),
        index + 1 < CommandLine.arguments.count
      {
        try self.app.session.addPlugin([
          "format": "VST3", "type": 0, "subtype": 0, "manufacturer": 0,
          "name": "Resonance Test Gain", "path": CommandLine.arguments[index + 1],
          "classID": "5245534F4E414E434546464543540001", "isInstrument": false,
        ])
        self.usesVST3 = true
      }
      try self.app.session.addPlugin([
        "type": 0x6175_6678, "subtype": 0x6c70_6173,
        "manufacturer": 0x6170_706c, "name": "Apple: AULowpass",
      ])
    }) {
      self.app.refreshAll()
      self.app.model.pattern = self.app.model.orders.first ?? 0
      self.app.refreshPattern()
      self.app.showEditor(0)
      self.beginMeasurementWhenVisible()
    }
  }
  func beginMeasurementWhenVisible() {
    let now = CFAbsoluteTimeGetCurrent()
    guard now < readyDeadline else {
      failures.append("No stable visible window was available before measurement")
      finish()
      return
    }
    if NSApp.isActive && app.window.occlusionState.contains(.visible) {
      if visibleSince == nil { visibleSince = now }
    } else {
      visibleSince = nil
    }
    guard let visibleSince, now - visibleSince >= 2 else {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { self.beginMeasurementWhenVisible() }
      return
    }
    operation({ try self.app.session.playOrder(0) }) {
      self.app.patternView.resetMetrics()
      self.app.lastFrameCount = 0
      self.app.lastStatusUpdate = CFAbsoluteTimeGetCurrent()
      self.start = CFAbsoluteTimeGetCurrent()
      self.measurementStarted = true
      self.app.patternView.isFollowing = false
      self.timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in self?.tick() }
      RunLoop.main.add(self.timer!, forMode: .common)
    }
  }
  func operation(_ work: @escaping () throws -> Void, done: @escaping () -> Void) {
    app.busy = true
    app.worker.async {
      do {
        try work()
        DispatchQueue.main.async {
          self.app.busy = false
          done()
        }
      } catch {
        DispatchQueue.main.async {
          self.app.busy = false
          self.fail(error)
        }
      }
    }
  }
  func tick() {
    guard !finished, !app.busy else { return }
    let elapsed = CFAbsoluteTimeGetCurrent() - start
    if elapsed >= nextProgress {
      nextProgress = elapsed + 10
      writeProgress(elapsed)
    }
    if elapsed >= duration {
      finish()
      return
    }
    require(app.session.playing, "Playback ended during the measured workload")
    if !app.session.playing {
      finish()
      return
    }
    require(
      app.window.isVisible && app.window.occlusionState.contains(.visible), "Window was not visible"
    )
    if !app.window.occlusionState.contains(.visible) {
      finish()
      return
    }
    require(app.editorMode == 0, "Pattern view was hidden during measurement")
    let grid = app.patternView
    let graphWidth = app.patternGraphHost.lanes.isHidden ? 0 : app.patternGraphHost.lanes.bounds.width
    require(grid.bounds.width > 100 && abs(grid.bounds.width + graphWidth - app.patternGraphHost.bounds.width) < 1,
      "Pattern and command lanes did not fill their workspace pane")
    grid.isFollowing = false
    grid.firstRow = (iteration * 3) % max(1, app.model.rows - 32)
    grid.firstChannel = (iteration / 7) % max(1, app.model.channels - 7)
    grid.cursorRow = grid.firstRow + 4
    grid.cursorChannel = grid.firstChannel
    grid.selectRegion(
      from: (grid.firstRow + 2, grid.firstChannel),
      to: (grid.firstRow + 7, grid.firstChannel + iteration % 3))
    grid.onCursor?()
    iteration += 1
    if usesVST3 && iteration % 5 == 0 {
      do {
        try app.session.pluginParameter(
          0, identifier: 7, value: Double(iteration % 10 + 1) / 20, record: true)
      } catch {
        fail(error)
        return
      }
    }
    if iteration % 4 == 0 {
      let row = min(app.model.rows - 1, grid.cursorRow)
      let ch = grid.cursorChannel
      let before = app.model.cell(row, ch)
      let after: [UInt8] = [UInt8(37 + iteration % 24), 1, 1, 1, 0, 0]
      grid.onEdit?(row, ch, after)
      edits += 1
      require(app.model.cell(row, ch) == after, "Live note edit was not reflected in the document")
      if iteration % 80 == 0 {
        operation({ self.app.session.undo() }) {
          self.app.refreshPattern()
          self.require(
            self.app.model.cell(row, ch) == before, "Undo did not restore the prior cell")
          self.operation({ self.app.session.redo() }) {
            self.app.refreshPattern()
            self.require(
              self.app.model.cell(row, ch) == after, "Redo did not restore the edited cell")
            self.undos += 1
          }
        }
      }
    }
    if iteration % 200 == 0 && !app.busy {
      let path = directory.appendingPathComponent("live-save.resonance").path
      operation({ try self.app.session.savePath(path) }) {
        self.require(self.app.session.playing, "Project saving interrupted playback")
        self.saves += 1
      }
    }
    if iteration % 20 == 0 {
      app.statusLabel.stringValue =
        "Qualification · \(Int(elapsed)) / \(Int(duration)) seconds · \(edits) edits"
    }
  }
  // Copy only constant-size telemetry on the main thread. Serialization and disk
  // I/O stay off it; sorting the growing frame arrays is reserved for the final report.
  func writeProgress(_ elapsed: Double) {
    var progress = app.session.telemetry()
    progress["processID"] = ProcessInfo.processInfo.processIdentifier
    progress["elapsedSeconds"] = elapsed
    progress["requestedSeconds"] = duration
    progress["framesPresented"] = app.patternView.frameCount
    progress["liveEdits"] = edits
    progress["liveSaves"] = saves
    progress["undoRedoCycles"] = undos
    progress["visible"] = app.window.occlusionState.contains(.visible)
    reporter.async {
      if let data = try? JSONSerialization.data(withJSONObject: progress, options: [.sortedKeys]) {
        try? data.write(
          to: URL(fileURLWithPath: "/tmp/resonance-ui-progress.json"), options: .atomic)
      }
    }
  }
  func fail(_ error: Error) {
    failures.append(error.localizedDescription)
    finish()
  }
  func finish() {
    guard !finished else { return }
    finished = true
    timer?.invalidate()
    if let priorWindowLevel { app.window.level = priorWindowLevel }
    if let activity {
      ProcessInfo.processInfo.endActivity(activity)
      self.activity = nil
    }
    var report = app.diagnostics()
    if measurementStarted {
      let presented = (report["presentationSamples"] as? Int ?? 0)
      let missed = (report["missedPresentations"] as? Int ?? 0)
      require(presented > Int(max(0, duration - 4) * 55), "Insufficient presented frames")
      require(
        missed == 0 || Double(missed) / Double(max(1, presented + missed)) < 0.001,
        "Missed presentation deadlines exceed 0.1%")
      require(
        (report["maxPresentMS"] as? Double ?? 100) < 34,
        "Presentation stall exceeded two 60 Hz periods")
      require(
        (report["p99CPUFrameMS"] as? Double ?? 100) < 6, "CPU frame preparation p99 exceeded 6 ms")
      require((report["p99GPUFrameMS"] as? Double ?? 100) < 4, "GPU execution p99 exceeded 4 ms")
      require(
        (report["maxMainThreadDrawMS"] as? Double ?? 100) < 34,
        "Main-thread drawing or drawable acquisition stalled for more than two periods")
      require((report["overruns"] as? Int ?? 1) == 0, "Audio callback exceeded its deadline")
      require(
        (report["p999Micros"] as? Double ?? .infinity) < Double(app.session.bufferSize)
          / app.session.sampleRate * 500000,
        "Audio callback p99.9 exceeded half the buffer period")
      require((report["fault"] as? Bool ?? true) == false, "Audio engine capacity fault")
      require((report["pluginFailure"] as? Bool ?? true) == false, "Audio Unit failure")
      require(edits > 0 && undos > 0 && saves > 0, "Edit/undo/save workload did not execute")
    }
    report["usesVST3"] = usesVST3
    report["requestedVST3"] = CommandLine.arguments.contains("--ui-test-vst3")
    report["automationPoints"] = app.session.snapshot(app.model.pattern)["automationPoints"]
    report["measurementStarted"] = measurementStarted
    report["durationSeconds"] = measurementStarted ? CFAbsoluteTimeGetCurrent() - start : 0
    report["requestedSeconds"] = duration
    report["liveEdits"] = edits
    report["undoRedoCycles"] = undos
    report["liveSaves"] = saves
    report["failures"] = failures
    report["passed"] = failures.isEmpty
    report["artifacts"] = directory.path
    if let data = try? JSONSerialization.data(
      withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
    {
      try? data.write(to: URL(fileURLWithPath: "/tmp/resonance-ui-test.json"), options: .atomic)
    }
    app.session.stop()
    app.dirty = false
    app.window.isDocumentEdited = false
    app.statusLabel.stringValue =
      failures.isEmpty ? "Qualification passed" : "Qualification: \(failures.count) failed checks"
  }
}
