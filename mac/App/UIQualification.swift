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
  private var priorGraphWindowLevel:NSWindow.Level?
  private let reporter = DispatchQueue(label: "org.resonance.qualification-report", qos: .utility)
  private var nextProgress = 0.0
  private var visibleSince: Double?
  private var readyDeadline = CFAbsoluteTimeGetCurrent() + 60
  private var requestedDisplayID:UInt32?
  var requestedForeground = false
  var usesVST3 = false
  let existingGraph = CommandLine.arguments.contains("--ui-test-existing-graph")
  let floatingGraph = CommandLine.arguments.contains("--ui-test-float-graph")
  var soundingChannels = 0
  private var minimumGraphCards=Int.max
  private var windowStateObservers=[NSObjectProtocol]()
  private var windowStates=[(timestamp:Double,flags:UInt8)]()
  private var windowStateDropped=0,windowStateSamples=0,inactiveSamples=0,noKeyWindowSamples=0,missingActivitySamples=0
  private var measurementClockStart=0.0,measurementClockEnd=0.0
  private var windowInventoryBefore=[[String:Any]]()
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
      // Directly launched QA bundles may otherwise remain on an inactive Space,
      // even after activation. Expose only this disposable test window across
      // Spaces; the visible-frame gates below still require real presentation.
      app.window.collectionBehavior.remove(.moveToActiveSpace)
      app.window.collectionBehavior.formUnion([.canJoinAllSpaces,.fullScreenAuxiliary])
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
    let targetDisplay:NSScreen?
    do {
      requestedDisplayID=try QualificationDisplay.requestedID(CommandLine.arguments)
      targetDisplay=try QualificationDisplay.select(requestedDisplayID,screens:NSScreen.screens,fallback:app.window.screen)
    } catch {fail(error);return}
    // Use the available display for the dense workload. A fixed small window
    // plus the restored secondary dock can leave only six tiny card fragments
    // visible; that is not a valid graph performance/legibility workload.
    let available=app.window.screen?.visibleFrame.size ?? NSSize(width:1360,height:900)
    let chrome=app.window.frame.height-app.window.contentLayoutRect.height
    app.window.setContentSize(NSSize(width:min(existingGraph ? 1600:1360,available.width),
      height:min(existingGraph ? 1000:850,max(500,available.height-chrome))))
    if let requestedDisplayID,let targetDisplay {
      // Keep the existing workload size; this option changes placement only.
      do {app.window.setFrame(try QualificationDisplay.centeredFrame(size:app.window.frame.size,visibleFrame:targetDisplay.visibleFrame),display:true)}
      catch {fail(error);return}
      guard QualificationDisplay.id(app.window.screen)==requestedDisplayID else {
        fail(QualificationDisplay.Failure(message:"Qualification window did not reach the requested display"));return
      }
    } else {app.window.center()}
    app.window.level = .floating
    activity = ProcessInfo.processInfo.beginActivity(
      options: [.userInitiated, .idleDisplaySleepDisabled],
      reason: "Running visible tracker performance qualification")
    operation({
      if let index=CommandLine.arguments.firstIndex(of:"--ui-test-device"),index+1<CommandLine.arguments.count {
        let name=CommandLine.arguments[index+1]
        guard name=="BlackHole 2ch",let device=self.app.session.devices().first(where:{$0["name"] as? String==name}),let id=device["id"] as? UInt else{throw NSError(domain:"Qualification",code:1,userInfo:[NSLocalizedDescriptionKey:"Requested BlackHole test device is unavailable"])}
        try self.app.session.configureDevice(id,buffer:512)
      }
      // Extend the built-in demo if no long fixture was supplied.
      if !self.existingGraph && self.app.model.orders.count == 1 {
        for _ in 0..<Int(ceil(self.duration / 7)) {
          _ = try self.app.session.addPattern(64, duplicate: true, source: 0)
        }
      }
      if !self.existingGraph, let index = CommandLine.arguments.firstIndex(of: "--ui-test-vst3"),
        index + 1 < CommandLine.arguments.count
      {
        try self.app.session.addPlugin([
          "format": "VST3", "type": 0, "subtype": 0, "manufacturer": 0,
          "name": "Resonance Test Gain", "path": CommandLine.arguments[index + 1],
          "classID": "5245534F4E414E434546464543540001", "isInstrument": false,
        ])
        self.usesVST3 = true
      }
      if !self.existingGraph {try self.app.session.addPlugin([
        "type": 0x6175_6678, "subtype": 0x6c70_6173,
        "manufacturer": 0x6170_706c, "name": "Apple: AULowpass",
      ])}
      if self.existingGraph {
        guard self.app.model.channels>=16 else{throw NSError(domain:"Qualification",code:3,userInfo:[NSLocalizedDescriptionKey:"Existing-graph qualification requires a supplied 16+ channel song"])}
        self.app.session.setPlaybackLoop(true)
      } else if CommandLine.arguments.contains("--ui-test-graph"){try self.prepareGraph()}
    }) {
      self.app.refreshAll()
      self.app.model.pattern = self.app.model.orders.first ?? 0
      self.app.refreshPattern()
      self.app.showEditor(0)
      if self.existingGraph {
        self.app.workspace?.place("graph",at:self.floatingGraph ? "float":"bottom",select:true)
        self.app.workspace?.right.isHidden=true
        self.app.workspace?.secondary.isHidden=true
        if self.floatingGraph {self.app.workspace?.lower.isHidden=true}
        self.app.workspace?.layoutSubtreeIfNeeded()
        if self.floatingGraph {self.positionFloatingGraph()}
        else if let workspace=self.app.workspace {workspace.vertical.setPosition(max(200,workspace.vertical.bounds.height*0.3),ofDividerAt:0)}
      }
      if CommandLine.arguments.contains("--ui-test-parameter-activity"),self.usesVST3,
        let plugin=self.app.model.nativePlugins.first?["instanceID"] as? String {
        self.app.workspace?.place("parameterActivity",at:"right",select:true)
        self.app.showParameterActivity(plugin:plugin,parameter:7)
      }
      self.beginMeasurementWhenVisible()
    }
  }
  func prepareGraph() throws {
    func call(_ method:String,_ params:[String:Any]=[:],write:Bool=true)throws->[String:Any]{var p=params;if write{p["expectedRevision"]=app.session.automationRevision};var error:NSError?;guard let result=app.session.automationMethod(method,params:p,error:&error),let data=result["data"] as? [String:Any]else{throw error ?? NSError(domain:"Qualification",code:2)};return data}
    for sample in 1...4{_ = try app.session.addInstrument(sample)}
    _ = try call("mixer.enable")
    let graph=try call("graph.create",["name":"Qualification motion"])["graph"] as! String
    var data=try call("graph.get",write:false)
    let definition=(data["library"] as! [[String:Any]]).first{$0["id"] as? String==graph}!,input=(definition["nodes"] as! [[String:Any]]).first{$0["kind"] as? String=="input"}!["id"] as! String
    let plugin=try call("graph.node.add",["graph":graph,"kind":"plugin","plugin":["format":"Built-in","classID":"resonance.gainer.v1"],"insertAfter":input])["node"] as! String
    let source=try call("graph.node.add",["graph":graph,"kind":"automation","name":"Pattern motion"])["node"] as! String
    data=try call("graph.get",write:false)
    for pattern in data["patterns"] as! [[String:Any]]{_ = try call("graph.automation.set",["graph":graph,"node":source,"pattern":pattern["index"]!,"points":[["position":0,"value":0.2,"curve":"smooth"],["position":8192,"value":0.8,"curve":"linear"]]])}
    var changed=(try call("graph.get",write:false)["library"] as! [[String:Any]]).first{$0["id"] as? String==graph}!
    changed["modulation"]=[["source":source,"target":plugin,"parameter":1,"minimum":0.7,"maximum":0.8]]
    _ = try call("graph.update",["definition":changed])
    _ = try call("graph.instrument.assign",["instrument":1,"graph":graph])
    let target=(data["mixer"] as! [String:Any])["buses"] as! [[String:Any]]
    _ = try call("graph.assign",["target":target[0]["id"]!,"graph":graph])
  }
  func beginMeasurementWhenVisible() {
    let now = CFAbsoluteTimeGetCurrent()
    guard now < readyDeadline else {
      failures.append("No stable visible window was available before measurement")
      finish()
      return
    }
    if NSApp.isActive && app.window.occlusionState.contains(.visible) && (!floatingGraph || app.signalGraphEditor.window?.occlusionState.contains(.visible)==true) {
      if visibleSince == nil { visibleSince = now }
    } else {
      visibleSince = nil
    }
    guard let visibleSince, now - visibleSince >= 2 else {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { self.beginMeasurementWhenVisible() }
      return
    }
    if let requestedDisplayID,QualificationDisplay.id(app.window.screen) != requestedDisplayID {
      fail(QualificationDisplay.Failure(message:"Requested qualification display changed before measurement"));return
    }
    if existingGraph {
      let graph=app.signalGraphEditor
      guard !graph.loading,!graph.canvas.nodes.isEmpty else {
        DispatchQueue.main.asyncAfter(deadline:.now()+0.1){self.beginMeasurementWhenVisible()};return
      }
      app.workspace?.layoutSubtreeIfNeeded()
      prepareDenseViewport()
      require(graph.scroll.contentSize.height>=180,"Dense graph viewport is too short to inspect")
      require(denseGraphCards>=8,
        "Dense graph must display at least eight actual cards")
      require(visiblePatternHeight>=180,"Dense graph workload must keep at least 180 points of the pattern visible")
      // Do not spend a minute measuring a workload known to be invalid. Keep
      // the screenshot-able setup and report its actual scale/card count.
      guard denseGraphCards>=8,graph.scroll.contentSize.height>=180,visiblePatternHeight>=180 else{finish();return}
    }
    if QualificationWindowEncodeTrace.enabled {windowInventoryBefore=QualificationWindowEncodeTrace.inventory()}
    operation({ try self.app.session.playOrder(0) }) {
      self.app.patternView.resetMetrics()
      self.app.signalGraphEditor.canvas.resetDrawStatistics()
      UIWorkTrace.active?.reset()
      self.app.lastFrameCount = 0
      self.app.lastStatusUpdate = CFAbsoluteTimeGetCurrent()
      self.start = CFAbsoluteTimeGetCurrent()
      self.measurementStarted = true
      self.startWindowStateTrace()
      self.app.patternView.isFollowing = false
      self.timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in self?.tick() }
      RunLoop.main.add(self.timer!, forMode: .common)
    }
  }
  private func startWindowStateTrace() {
    measurementClockStart=CACurrentMediaTime();windowStates.reserveCapacity(4096)
    for name in [NSApplication.didBecomeActiveNotification,NSApplication.didResignActiveNotification,
      NSWindow.didBecomeKeyNotification,NSWindow.didResignKeyNotification,NSWindow.didChangeOcclusionStateNotification] {
      windowStateObservers.append(NotificationCenter.default.addObserver(forName:name,object:nil,queue:.main){[weak self] _ in self?.sampleWindowState()})
    }
    sampleWindowState()
  }
  private func sampleWindowState() {
    guard measurementStarted,!finished else{return}
    let graph=app.signalGraphEditor.window
    // Record transitions as well as every workload tick. Visibility alone does
    // not establish foreground/key status or that the App Nap activity is held.
    let flags:UInt8=(NSApp.isActive ? 1:0) | (app.window.isKeyWindow ? 2:0) |
      (graph?.isKeyWindow==true ? 4:0) |
      (app.window.isVisible && app.window.occlusionState.contains(.visible) ? 8:0) |
      (graph?.isVisible==true && graph?.occlusionState.contains(.visible)==true ? 16:0) |
      (activity != nil ? 32:0) | (NSApp.keyWindow != nil ? 64:0)
    windowStateSamples+=1
    if flags & 1 == 0 {inactiveSamples+=1}
    if flags & 64 == 0 {noKeyWindowSamples+=1}
    if flags & 32 == 0 {missingActivitySamples+=1}
    if windowStates.last?.flags != flags {
      if windowStates.count<4096 {windowStates.append((CACurrentMediaTime(),flags))}
      else {windowStateDropped+=1}
    }
  }
  private var denseGraphCards:Int {
    let graph=app.signalGraphEditor,visible=graph.scroll.documentVisibleRect.insetBy(dx:-0.5,dy:-0.5)
    return graph.canvas.nodes.filter{visible.contains($0.rect)}.count
  }
  private var visiblePatternHeight:CGFloat {
    let pattern=app.patternView
    guard let window=pattern.window else{return 0}
    let rect=window.convertToScreen(pattern.convert(pattern.bounds,to:nil))
    guard floatingGraph,let graphWindow=app.signalGraphEditor.window,graphWindow !== window,
      graphWindow.frame.intersects(rect) else{return rect.height}
    // The single-screen floating fixture leaves an unobscured top strip of
    // the real tracker. A second-screen graph never intersects this rectangle.
    return max(0,rect.maxY-max(rect.minY,graphWindow.frame.maxY))
  }
  private func positionFloatingGraph() {
    guard let window=app.signalGraphEditor.window,window !== app.window,let mainScreen=app.window.screen else{return}
    priorGraphWindowLevel=window.level
    window.collectionBehavior.remove(.moveToActiveSpace)
    window.collectionBehavior.formUnion([.canJoinAllSpaces,.fullScreenAuxiliary]);window.level = .floating
    // A reported secondary display can belong to another Space or be a
    // headless/virtual surface. Qualify an independent window on the display
    // whose main window has already passed the actual visibility check.
    let available=mainScreen.visibleFrame
    app.window.contentView?.layoutSubtreeIfNeeded()
    let pattern=app.patternView
    let rect=app.window.convertToScreen(pattern.convert(pattern.bounds,to:nil))
    let top=min(available.maxY,rect.maxY-220)
    window.setFrame(NSRect(x:available.minX,y:available.minY,width:min(1600,available.width),height:max(400,top-available.minY)),display:true)
    window.contentView?.layoutSubtreeIfNeeded();window.makeKeyAndOrderFront(nil)
  }
  private func prepareDenseViewport() {
    let graph=app.signalGraphEditor
    app.window.contentView?.layoutSubtreeIfNeeded();graph.window?.contentView?.layoutSubtreeIfNeeded()
    let rects=graph.canvas.nodes.map(\.rect)
    // Use the musician's real zoom/scroll path, with a legibility floor. Do not
    // fit the entire sparse song into tiny cards, rearrange it, or hide nodes.
    // Native screenshots must additionally qualify text/ports at this scale.
    for scale in [1.0,0.9,0.8] {
      graph.scroll.magnification=scale
      graph.layoutSubtreeIfNeeded();graph.scroll.layoutSubtreeIfNeeded()
      let size=graph.scroll.documentVisibleRect.size
      let xs=Set(rects.flatMap{[max(0,$0.minX-12),max(0,$0.maxX-size.width+12)]})
      let ys=Set(rects.flatMap{[max(0,$0.minY-12),max(0,$0.maxY-size.height+12)]})
      var best=NSPoint.zero,bestCount = -1
      for y in ys.sorted(){for x in xs.sorted(){let visible=NSRect(origin:NSPoint(x:x,y:y),size:size)
        let count=rects.filter{visible.contains($0)}.count
        if count>bestCount{bestCount=count;best=visible.origin}
      }}
      graph.canvas.frame.size=NSSize(width:max(graph.canvas.frame.width,best.x+size.width),height:max(graph.canvas.frame.height,best.y+size.height))
      graph.scroll.contentView.scroll(to:best);graph.scroll.reflectScrolledClipView(graph.scroll.contentView)
      if denseGraphCards>=8{break}
    }
    minimumGraphCards=denseGraphCards
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
    let trace=UIWorkTrace.active,started=UIWorkTrace.active == nil ? 0:CACurrentMediaTime()
    defer{trace?.finish(.qualificationTick,start:started)}
    guard !finished else { return }
    sampleWindowState()
    guard !app.busy else { return }
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
    if existingGraph {
      require(app.workspace?.visibleIDs.contains("graph")==true,"Graph panel was hidden during the dense workload")
      require(app.signalGraphEditor.window?.occlusionState.contains(.visible)==true,"Graph window was not visible during the dense workload")
      require(visiblePatternHeight>=180,"Floating graph obscured the pattern during measurement")
      minimumGraphCards=min(minimumGraphCards,denseGraphCards)
      require(denseGraphCards>=8,"Dense graph lost its eight readable cards during measurement")
    }
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
    let trace=UIWorkTrace.active,started=UIWorkTrace.active == nil ? 0:CACurrentMediaTime()
    defer{trace?.finish(.qualificationProgress,start:started)}
    var progress = app.session.telemetry()
    if existingGraph {
      var error:NSError?
      if let result=app.session.automationMethod("mixer.get",params:[:],error:&error),let data=result["data"] as? [String:Any] {
        let tracks=Set((data["buses"] as? [[String:Any]] ?? []).filter{$0["kind"] as? String=="track"}.compactMap{$0["id"] as? String})
        let sounding=(data["meters"] as? [[String:Any]] ?? []).filter{tracks.contains($0["bus"] as? String ?? "") && max($0["left"] as? Double ?? 0,$0["right"] as? Double ?? 0)>1e-7}.count
        soundingChannels=max(soundingChannels,sounding);progress["soundingTrackMeters"]=sounding
      }
    }
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
    sampleWindowState();measurementClockEnd=CACurrentMediaTime()
    // Freeze the base/audio measurement before draining late presentation
    // handlers. The drain adds reporting time, never workload or measured time.
    var report = app.diagnostics()
    let frozenPresentationTimes=app.patternView.presentationTimestamps
    finished = true
    for observer in windowStateObservers {NotificationCenter.default.removeObserver(observer)}
    windowStateObservers.removeAll()
    timer?.invalidate()
    if let priorWindowLevel { app.window.level = priorWindowLevel }
    if let priorGraphWindowLevel {app.signalGraphEditor.window?.level=priorGraphWindowLevel}
    if let activity {
      ProcessInfo.processInfo.endActivity(activity)
      self.activity = nil
    }
    if measurementStarted {
      require(
        (report["p99CPUFrameMS"] as? Double ?? 100) < 6, "CPU frame preparation p99 exceeded 6 ms")
      require((report["p99GPUFrameMS"] as? Double ?? 100) < 4, "GPU execution p99 exceeded 4 ms")
      require((report["maxGeometrySnapshotAgeMS"] as? Double ?? 100) < 34,
        "Pattern geometry snapshot was stale at the render callback for more than two periods")
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
    if existingGraph && measurementStarted {require(soundingChannels>=16,"Fewer than 16 sounding track meters were observed")}
    report["usesGraph"] = existingGraph || CommandLine.arguments.contains("--ui-test-graph")
    report["usesExistingGraph"] = existingGraph
    report["graphWindowMode"] = floatingGraph ? "floating":"docked"
    report["maximumSoundingTrackMeters"] = soundingChannels
    report["graphVisible"] = app.workspace?.visibleIDs.contains("graph")==true
    report["graphViewportHeight"] = app.signalGraphEditor.scroll.contentSize.height
    report["graphVisibleNodes"] = app.signalGraphEditor.canvas.nodes.filter{$0.rect.intersects(app.signalGraphEditor.scroll.documentVisibleRect)}.count
    report["graphFullyVisibleNodes"] = denseGraphCards
    report["graphMinimumFullyVisibleNodes"] = minimumGraphCards==Int.max ? 0:minimumGraphCards
    report["graphMagnification"] = app.signalGraphEditor.scroll.magnification
    report["graphDrawStatistics"] = app.signalGraphEditor.canvas.drawStatistics
    report["graphViewportWidth"] = app.signalGraphEditor.scroll.contentSize.width
    report["visiblePatternHeight"] = visiblePatternHeight
    report["graphEditorHeight"] = app.signalGraphEditor.bounds.height
    report["workspaceHeight"] = app.workspace?.bounds.height
    report["workspaceUpperHeight"] = app.workspace?.upper.bounds.height
    report["workspaceLowerHeight"] = app.workspace?.lower.bounds.height
    report["graphWindowVisible"] = app.signalGraphEditor.window?.occlusionState.contains(.visible)==true
    func rectangle(_ value:NSRect)->[String:Double]{["x":value.minX,"y":value.minY,"width":value.width,"height":value.height]}
    func display(_ screen:NSScreen?)->[String:Any]{guard let screen else{return [:]};return ["id":screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] ?? NSNull(),"frame":rectangle(screen.frame),"visibleFrame":rectangle(screen.visibleFrame),"backingScale":screen.backingScaleFactor]}
    report["mainWindowFrame"]=rectangle(app.window.frame)
    report["graphWindowFrame"]=app.signalGraphEditor.window.map{rectangle($0.frame)} ?? [:]
    report["mainDisplay"]=display(app.window.screen)
    report["graphDisplay"]=display(app.signalGraphEditor.window?.screen)
    report["usesVST3"] = usesVST3
    report["requestedVST3"] = CommandLine.arguments.contains("--ui-test-vst3")
    report["automationPoints"] = app.session.snapshot(app.model.pattern)["automationPoints"]
    report["measurementStarted"] = measurementStarted
    report["durationSeconds"] = measurementStarted ? measurementClockEnd-measurementClockStart : 0
    report["requestedSeconds"] = duration
    report["liveEdits"] = edits
    report["undoRedoCycles"] = undos
    report["liveSaves"] = saves
    report["failures"] = failures
    report["passed"] = failures.isEmpty
    report["artifacts"] = directory.path
    report["windowStateSamples"]=windowStateSamples
    report["inactiveWindowStateSamples"]=inactiveSamples
    report["noKeyWindowStateSamples"]=noKeyWindowSamples
    report["missingActivityWindowStateSamples"]=missingActivitySamples
    report["windowStateTransitionsDropped"]=windowStateDropped
    report["measurementClockStart"]=measurementClockStart
    report["measurementClockEnd"]=measurementStarted ? measurementClockEnd:0
    report["activityOptions"]=["userInitiated","idleDisplaySleepDisabled"]
    if measurementStarted {
      let values:[[String:Any]]=windowStates.map { state in
        ["timestamp":state.timestamp,"appActive":state.flags & 1 != 0,"mainWindowKey":state.flags & 2 != 0,
         "graphWindowKey":state.flags & 4 != 0,"mainWindowVisible":state.flags & 8 != 0,
         "graphWindowVisible":state.flags & 16 != 0,"activityHeld":state.flags & 32 != 0,
         "applicationHasKeyWindow":state.flags & 64 != 0]
      }
      let path=directory.appendingPathComponent("window-state-timeline.json")
      if let data=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? data.write(to:path,options:.atomic)) != nil {report["windowStateTimeline"]=path.path}
    }
    if let trace=UIWorkTrace.active,measurementStarted {
      let entries=trace.snapshot()
      let values:[[String:Any]]=entries.map{["sequence":$0.sequence,"phase":$0.phase.rawValue,"start":$0.start,"end":$0.end]}
      let path=directory.appendingPathComponent("ui-work-timeline.json")
      if let data=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? data.write(to:path,options:.atomic)) != nil {
        report["uiWorkTimeline"]=path.path
      }
      report["uiWorkTimelineSamples"]=entries.count;report["uiWorkTimelineOverwritten"]=trace.overwritten
      var phases=[String:Any]()
      for phase in UIWorkTrace.Phase.allCases {
        let durations=entries.filter{$0.phase==phase}.map(\.durationMS)
        if !durations.isEmpty {phases[phase.rawValue]=["samples":durations.count,"maxMS":durations.max() ?? 0,"p99MS":app.patternView.percentile(durations,0.99)]}
      }
      report["uiWorkPhases"]=phases
    }
    if QualificationRunLoopTrace.enabled {
      var values=[[String:Any]](),dropped=[String:Int]()
      for (name,trace) in [("main",UIWorkTrace.active?.runLoopTrace),("render",app.patternView.qualificationRunLoopTrace)] {
        guard let trace else{continue};let snapshot=trace.snapshot();dropped[name]=snapshot.overwritten
        values += snapshot.entries.map{["loop":name,"sequence":$0.sequence,"timestamp":$0.timestamp,"activity":$0.activity,"depth":$0.depth]}
      }
      let path=directory.appendingPathComponent("runloop-timeline.json")
      if let bytes=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? bytes.write(to:path,options:.atomic)) != nil {report["runLoopTimeline"]=path.path}
      report["runLoopTimelineOverwritten"]=dropped
      report["measurementClass"]="instrumented-runloop-diagnostic"
      report["cleanQualificationEligible"]=false
    }
    if let trace=UIWorkTrace.active?.buttonDrawTrace {
      let entries=trace.snapshot(),path=directory.appendingPathComponent("button-draw-timeline.json")
      let values:[[String:Any]]=entries.map{entry in
        ["sequence":entry.sequence,"button":String(entry.identity),"title":entry.title,"context":entry.context,
         "start":entry.start,"end":entry.end,"enabled":entry.enabled,"highlighted":entry.highlighted,"state":entry.state,
         "frame":rectangle(entry.frame)]
      }
      if let bytes=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? bytes.write(to:path,options:.atomic)) != nil {report["buttonDrawTimeline"]=path.path}
      report["buttonDrawTimelineSamples"]=entries.count;report["buttonDrawTimelineOverwritten"]=trace.overwritten
      report["measurementClass"]=QualificationRunLoopTrace.enabled ? "instrumented-runloop-and-button-diagnostic":"instrumented-button-diagnostic"
      report["cleanQualificationEligible"]=false
    }
    if let trace=UIWorkTrace.active?.windowEncodeTrace {
      let entries=trace.snapshot(),path=directory.appendingPathComponent("window-encode-timeline.json")
      let values:[[String:Any]]=entries.map{entry in
        ["sequence":entry.sequence,"windowNumber":entry.windowNumber,"title":entry.title,"class":entry.className,"kind":entry.kind,
         "isRestorable":entry.restorable,"visible":entry.visible,"start":entry.start,"end":entry.end]
      }
      if let bytes=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? bytes.write(to:path,options:.atomic)) != nil {report["windowEncodeTimeline"]=path.path}
      report["windowEncodeTimelineSamples"]=entries.count;report["windowEncodeTimelineOverwritten"]=trace.overwritten
      report["windowInventoryBefore"]=windowInventoryBefore;report["windowInventoryAfter"]=QualificationWindowEncodeTrace.inventory()
      report["windowInventoryLimit"]=128;report["windowInventoryAfterTotal"]=NSApp.windows.count
      report["measurementClass"]="instrumented-window-state-diagnostic";report["cleanQualificationEligible"]=false
    }
    report["applicationClass"]=String(reflecting:type(of:NSApp!))
    report["applicationStateEncodingTraceRequested"]=QualificationApplicationEncodeTrace.enabled
    report["applicationStateEncodingTraceInstalled"]=NSApp is UIQualificationApplication
    if let trace=UIWorkTrace.active?.applicationEncodeTrace {
      let entries=trace.snapshot(),path=directory.appendingPathComponent("application-encode-timeline.json")
      let values:[[String:Any]]=entries.map{entry in
        ["sequence":entry.sequence,"kind":entry.kind.rawValue,"start":entry.start,"end":entry.end]
      }
      if let bytes=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? bytes.write(to:path,options:.atomic)) != nil {report["applicationEncodeTimeline"]=path.path}
      report["applicationEncodeTimelineSamples"]=entries.count
      report["applicationEncodeTimelineDropped"]=trace.overwritten
      report["applicationEncodeMeasurement"]="outer synchronous AppKit call; background queue completion is not measured"
      report["measurementClass"]="instrumented-window-and-application-state-diagnostic"
      report["cleanQualificationEligible"]=false
    }
    report["presentationQualificationSource"]="canonical-actual-presentation-window"
    report["presentationWarmupSeconds"]=PatternPresentationMetrics.warmupSeconds
    report["presentationReportingDrainSeconds"]=0.25
    report["geometrySnapshotAgeMeasurement"]="render-callback-minus-geometry-prepared"
    let frozenReport=report
    DispatchQueue.main.asyncAfter(deadline:.now()+0.25) { [self] in
      completePresentationReport(frozenReport,mainTimestamps:frozenPresentationTimes)
    }
  }
  private func completePresentationReport(_ frozenReport:[AnyHashable:Any],mainTimestamps:[Double]) {
    var report=frozenReport
    // snapshot() already selects the current measurement generation. A fixed
    // reporting drain permits late handlers; actual times after the frozen end
    // never repair a real terminal stall or increase the measured frame count.
    let frames=app.patternView.qualificationFrameTrace.filter{
      $0.callback>=measurementClockStart && $0.callback<=measurementClockEnd
    }
    let traceTimes=frames.map(\.presented).filter{$0.isFinite && $0>0}
    let cadence=PatternPresentationMetrics(timestamps:mainTimestamps+traceTimes,
      measurementStart:measurementClockStart,measurementEnd:measurementClockEnd)
    report["canonicalPresentationWindowStart"]=cadence.start
    report["canonicalPresentationWindowEnd"]=cadence.end
    report["canonicalPresentationWindowSeconds"]=cadence.duration
    report["canonicalPresentationSamples"]=cadence.timestamps.count
    report["canonicalExpectedPresentationFrames"]=cadence.expectedFrames
    report["canonicalPresentationRateDeficit"]=cadence.rateDeficit
    report["canonicalPresentationGapMisses"]=cadence.gapMisses
    report["canonicalMissedPresentations"]=cadence.missedFrames
    report["canonicalMissedPresentationFraction"]=cadence.missedFraction
    report["canonicalPresentedFPS"]=cadence.framesPerSecond
    report["canonicalLeadingPresentationGapMS"]=cadence.leadingGapMS
    report["canonicalTrailingPresentationGapMS"]=cadence.trailingGapMS
    report["canonicalMaxPresentMS"]=cadence.maxGapMS
    report["canonicalPresentationPassed"]=measurementStarted && cadence.passesCadence
    report["presentationMainSourceSamples"]=mainTimestamps.count
    report["presentationTraceSourceSamples"]=traceTimes.count
    report["presentationMainSourceFirst"]=mainTimestamps.filter{$0.isFinite && $0>0}.min() ?? 0
    report["presentationMainSourceLast"]=mainTimestamps.filter{$0.isFinite && $0>0}.max() ?? 0
    report["presentationTraceSourceFirst"]=traceTimes.min() ?? 0
    report["presentationTraceSourceLast"]=traceTimes.max() ?? 0
    report["presentationReportClock"]=CACurrentMediaTime()
    report["presentationActualReportingDelaySeconds"]=CACurrentMediaTime()-measurementClockEnd
    if measurementStarted {
      require(cadence.valid && !cadence.timestamps.isEmpty,"Insufficient presented frames in the measured window")
      require(cadence.missedFraction<0.001,"Missed presentation deadlines exceed 0.1%")
      require(cadence.maxGapMS<34,"Presentation stall exceeded two 60 Hz periods")
    }
    let presentedFrames=frames.filter{$0.presented.isFinite && $0.presented>=cadence.start && $0.presented<=cadence.end}
    let contentAges=presentedFrames.filter{$0.geometryPrepared>0 && $0.geometryPrepared<=$0.presented}.map{($0.presented-$0.geometryPrepared)*1000}
    report["presentedContentAgeMeasurement"]="actual-presentation-minus-geometry-prepared; includes intentional queued presentation latency; diagnostic only"
    report["presentedContentAgeSamples"]=contentAges.count
    report["p99PresentedContentAgeMS"]=app.patternView.percentile(contentAges,0.99)
    report["maxPresentedContentAgeMS"]=contentAges.max() ?? 0
    report["presentedContentAgeSourceFirst"]=presentedFrames.map(\.presented).min() ?? 0
    report["presentedContentAgeSourceLast"]=presentedFrames.map(\.presented).max() ?? 0
    report["presentedContentAgeTraceCoversWindowStart"]=(frames.first?.callback ?? .infinity)<=cadence.start
    if !frames.isEmpty {
      let values=frames.map{entry in ["sequence":Double(entry.sequence),"callback":entry.callback,"deadline":entry.deadline,
        "presentationTarget":entry.presentationTarget,"geometryPrepared":entry.geometryPrepared,"committed":entry.committed,
        "scheduled":entry.scheduled,"gpuStart":entry.gpuStart,"gpuEnd":entry.gpuEnd,"completed":entry.completed,
        "presented":entry.presented,"presentationCallback":entry.presentationCallback]}
      let path=directory.appendingPathComponent("frame-timeline.json")
      if let data=try? JSONSerialization.data(withJSONObject:values,options:[.sortedKeys]),(try? data.write(to:path,options:.atomic)) != nil {
        report["frameTimeline"]=path.path
      }
      report["frameTimelineSamples"]=frames.count
      report["framesCommittedAfterDeadline"]=frames.filter{$0.committed>$0.deadline}.count
      report["framesGPUEndedAfterPresentationTarget"]=frames.filter{$0.gpuEnd>$0.presentationTarget}.count
      report["p99PresentationTargetErrorMS"]=app.patternView.percentile(frames.filter{$0.presented>0}.map{($0.presented-$0.presentationTarget)*1000},0.99)
    }
    report["failures"]=failures
    report["passed"]=failures.isEmpty
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
