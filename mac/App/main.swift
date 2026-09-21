import AppKit
import UniformTypeIdentifiers

final class AppController: NSObject, NSApplicationDelegate, NSWindowDelegate {
  let session = TrackerSession()
  var window: NSWindow!
  let patternView = PatternView()
  lazy var patternGraphHost=PatternGraphHost(patternView)
  let sampleEditor = SampleEditor(frame: .zero), instrumentEditor = InstrumentEditor(frame: .zero)
  let keyboardSettings = KeyboardSettings()
  let pluginEditor = PluginEditor(frame: .zero)
  let pluginPortsEditor = PluginPortsEditor(frame: .zero)
  var pluginPortsWindow: NSWindow?
  let sidechainEditor = SidechainEditor(frame: .zero)
  var sidechainWindow: NSWindow?
  let editorHost = Panel(Theme.bg)
  var workspace: DockWorkspace?
  let workspaceNotes = PreciseNotesEditor(frame:.zero), workspaceAutomation = PatternAutomationEditor(frame:.zero)
  var workspaceAutomationModel: PatternModel?
  var workspaceReturnPoints = [String:EditorNavigation](), workspaceContextTokens = [String:String]()
  let signalGraphEditor=SignalGraphEditor(frame:.zero), graphPluginBrowser=PluginBrowser(), graphCommandsEditor=GraphCommandsEditor(frame:.zero)
  let commandPalette = WorkspaceCommandPalette()
  var workspaceContextMonitor: Any?
  var workspaceInputMonitor: Any?, workspaceHeldKeys = [UInt16:Int]()
  var liveKeyboard = false, liveKeyboardItem: NSMenuItem?, liveKeyboardButton: ActionButton?
  var lastWorkspaceRefresh = 0.0
  var editorMode = 0
  var model = PatternModel([:])
  let titleLabel = Theme.label("Midnight Circuit", size: 20, weight: .semibold)
  var noteTrackWindow: NSWindow?
  var pluginBrowserWindow: NSWindow?
  var sampleBrowserWindow: NSWindow?
  lazy var sampleLibrary = SampleLibrary(directory: sampleLibraryDirectory)
  let sampleAudition = SampleAudition()
  let sampleInspectionQueue = DispatchQueue(label: "org.resonance.sample-inspection", qos: .userInitiated)
  var sampleLibraryConnected = false
  let subtitleLabel = Theme.label("MPTM  /  8 channels", size: 11, color: Theme.muted)
  let positionLabel = Theme.label("000 : 00", size: 20, color: Theme.accent, mono: true)
  var songTimingWindow: NSWindow?
  var pluginProgramsWindow: NSWindow?
  var pluginInstrumentsWindow: NSWindow?
  var instrumentPluginWindow: NSWindow?
  var instrumentEnvelopeToolsWindow: NSWindow?
  var instrumentEnvelopeBank:EnvelopeBankWindow?
  let instrumentEnvelopeClipboard=InstrumentEnvelopeClipboard()
  let tempoLabel = Theme.label("124", size: 20, mono: true)
  let infoLabel = Theme.label("Pattern 00", size: 13, weight: .semibold)
  let cursorLabel = Theme.label("ROW 000   CH 01", size: 11, color: Theme.muted, mono: true)
  let commandHelpLabel = Theme.label("", size: 10, color: Theme.muted)
  let statusLabel = Theme.label("Ready", size: 11, color: Theme.muted)
  let audioLabel = Theme.label(
    "CORE AUDIO   48 kHz / 128", size: 10, color: Theme.muted, mono: true)
  let meter = LevelMeter()
  let sidebar = NSStackView()
  let orderStack = NSStackView()
  var playButton: ActionButton!
  var followButton: ActionButton!
  var loopButton: ActionButton!
  var playbackLoop = false
  var inspectorHeldKeys = [UInt16:(note:Int, sample:Int, instrument:Int)]()
  var inspectorPendingNotes = [(note:Int, sample:Int, instrument:Int, on:Bool)]()
  var tickTimer: Timer?
  var positionTimelineKey="", positionTimelinePending=false
  var rulerPlaybackOrder:Int?
  var selectedOrder = 0
  var documentURL: URL?
  var dirty = false
  var lastStatusUpdate = 0.0
  var lastFrameCount: UInt64 = 0
  var lastFPS = 0.0
  var settingsWindow: NSWindow?
  let worker = DispatchQueue(label: "org.resonance.document", qos: .userInitiated)
  let recoveryWriter = DispatchQueue(label: "org.resonance.recovery", qos: .utility)
  var busy = false
  var shuttingDown = false
  var currentPlaying = false
  let presetWorkflow = PluginPresetWorkflow()
  var uiReady = false
  var pendingOpenURL: URL?
  var recoveryTimer: Timer?
  var recoveryID = UUID().uuidString {
    didSet {
      recoveryLastRevision = nil; recoveryLastTake = nil; recoveryLastDate = nil; recoveryLastCopy = nil; recoveryError = nil
      updateRecoveryStatus()
    }
  }
  var recoveryLastRevision: String?, recoveryLastCopy: String?, recoveryError: String?
  var recoveryLastTake: String?
  var recoveryLastDate: Date?
  var recoverySaving = false
  var recoveryEpoch = 0
  var recoveryWindow: NSWindow?
  var recoveryStatusButton: ActionButton?
  var assetToken = 0
  var workspaceAssetTokens = [String:Int]()
  var pendingNotes = [(Int, Int, Int, Bool)]()
  var midiArmed = false
  var midiNotes = [Int: Int]()
  var midiWindow: NSWindow?
  var orderWindow: NSWindow?
  let matrixEditor = ArrangementMatrix(frame: .zero)
  var matrixWindow: NSWindow?
  var patternToolsWindow: NSWindow?
  var commandPickerWindow: NSWindow?
  var patternPerformanceWindow: NSWindow?
  var preciseNotesWindow:NSWindow?
  var recordingTakeID:String?,recordingFinishing=false
  var midiQuantization=0,midiLatencyMS=0.0,midiRecordColumns=1
  var patternAutomationWindow: NSWindow?
  let mixerEditor = MixerEditor(frame: .zero)
  var mixerWindow: NSWindow?
  let orderEditor = OrderEditor(frame: .zero)
  var qualification: UIQualification?
  var automationServer: AutomationServer?
  var automationMenuItem: NSMenuItem?
  let automationTest = CommandLine.arguments.contains("--automation-test")
  let inspectionTest = CommandLine.arguments.contains("--inspection")

  func applicationDidFinishLaunching(_ notification: Notification) {
    NSApp.setActivationPolicy(automationTest ? .prohibited : .regular)
    NSApp.appearance = NSAppearance(named: .darkAqua)
    makeMenu()
    NSWorkspace.shared.notificationCenter.addObserver(
      self, selector: #selector(systemSleep), name: NSWorkspace.willSleepNotification, object: nil)
    NSWorkspace.shared.notificationCenter.addObserver(
      self, selector: #selector(systemWake), name: NSWorkspace.didWakeNotification, object: nil)
    window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 1360, height: 880),
      styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false
    )
    window.title = "ScreamSeq"
    window.titlebarAppearsTransparent = true
    window.backgroundColor = Theme.panel
    window.minSize = NSSize(width: 940, height: 580)
    window.delegate = self
    if !automationTest && !inspectionTest {
      if let screen = NSScreen.screens.max(by: { $0.visibleFrame.width*$0.visibleFrame.height < $1.visibleFrame.width*$1.visibleFrame.height }) {
        window.setFrame(screen.visibleFrame.insetBy(dx:8,dy:8),display:false)
      }
      window.setFrameAutosaveName("ResonanceConnectedWorkspace")
    }
    createContent()
    patternView.onEdit = { [weak self] row, channel, values in
      guard let self, !self.busy else { return }
      do {
        try self.session.editPattern(
          self.model.pattern, row: row, channel: channel, values: values.map { NSNumber(value: $0) }
        )
        self.dirty = true
        // A validated single-cell edit cannot change song shape or metadata.
        // Keep typing independent of total pattern size and asset count.
        self.model.replaceCell(row, channel, with: values)
        self.model.revisionToken = self.session.automationRevision
        self.patternView.model = self.model
        self.updateCommandHelp()
        self.window.isDocumentEdited = true
      } catch { self.show(error) }
    }
    patternView.onPreciseNotes = {[weak self] in self?.showPreciseNotes()}
    patternView.onClearPreciseNotes = {[weak self] row,channel in self?.clearPreciseNotes(row:row,channel:channel)}
    patternView.onEffectPicker = {[weak self] in self?.showPatternCommands()}
    patternView.onEffectColumns = {[weak self] channel,count in self?.setEffectColumns(channel:channel,count:count)}
    patternView.onContextMenu = {[weak self] event in self?.showPatternContextMenu(event)}
    patternView.onPositionMode = {[weak self] in
      guard let self else{return};self.updatePositionTimeline()
      self.statusLabel.stringValue="Position: \(self.patternView.positionMode.title) · click the header to cycle · time is the first visit to each row"
      if !self.inspectionTest && !self.automationTest {UserDefaults.standard.set(self.patternView.positionMode.rawValue,forKey:"patternPositionMode")}
    }
    if let saved=UserDefaults.standard.string(forKey:"patternPositionMode"),let mode=PatternPositionMode(rawValue:saved),!inspectionTest,!automationTest{patternView.positionMode=mode}
    patternView.onNativeEffect = {[weak self] in self?.showPatternPerformance()}
    patternView.onTrackerEffect = {[weak self] row,channel,column,effect,parameter in self?.setTrackerEffect(row:row,channel:channel,column:column,effect:effect,parameter:parameter)}
    patternView.onTypedNativeEffect = {[weak self] kind in self?.openPatternPerformance(kind:kind)}
    patternView.onClearNativeEffect = {[weak self] row,channel,column in self?.clearNativeEffect(row:row,channel:channel,column:column)}
    patternView.onTransport = { [weak self] in self?.togglePlayback() }
    patternView.onUndo = { [weak self] in self?.undo() }
    patternView.onRedo = { [weak self] in self?.redo() }
    patternView.onCursor = { [weak self] in
      guard let self else { return }
      self.updateCommandHelp()
    }
    patternView.onMute = { [weak self] ch in
      guard let self, !self.busy, self.model.editable, ch < self.model.tracks.count,
            let id = self.model.tracks[ch]["id"] as? String else { return }
      self.handleAutomation("track.column.set", params: ["expectedRevision": self.model.revisionToken, "column": id,
        "mute": !self.patternView.muted.contains(ch)]) { [weak self] response in
          if let error = response["error"] as? [String: Any] { self?.statusLabel.stringValue = error["message"] as? String ?? "Column mute failed" }
      }
    }
    connectEditors()
    refreshAll()
    if automationTest || inspectionTest { window.center() }
    if !automationTest {
      window.makeKeyAndOrderFront(nil)
      window.makeFirstResponder(patternView)
      NSApp.activate(ignoringOtherApps: true)
    }
    recoveryTimer = Timer.scheduledTimer(withTimeInterval: 10, repeats: true) { [weak self] _ in
      self?.autosave()
    }
    tickTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in
      self?.tick()
    }
    uiReady = true
    let argumentURL = AppLaunchArguments.documentPath(CommandLine.arguments,exists:{FileManager.default.fileExists(atPath:$0)}).map{URL(fileURLWithPath:$0)}
    if let file = pendingOpenURL ?? argumentURL {
      pendingOpenURL = nil
      load(file)
    }
    if CommandLine.arguments.contains("--ui-test") {
      recoveryTimer?.invalidate()
      qualification = UIQualification(self)
      qualification?.startWhenReady()
    }
    if inspectionTest || (automationTest && !CommandLine.arguments.contains("--recovery-test")) { recoveryTimer?.invalidate() }
    if !automationTest && !inspectionTest && !CommandLine.arguments.contains("--ui-test") { listRecovery() }
    if CommandLine.arguments.contains("--automation") || automationTest { toggleAutomation() }
  }
  func createContent() {
    let root = Panel(Theme.bg)
    window.contentView = root
    let vertical = NSStackView()
    vertical.orientation = .vertical
    vertical.distribution = .fill
    vertical.spacing = 0
    vertical.alignment = .leading
    vertical.fill(root)
    let toolbar = Panel()
    toolbar.fixed(height: 58)
    let brand = stack(.vertical, [titleLabel, subtitleLabel], spacing: 5)
    let transport = stack(.horizontal, [], spacing: 6)
    playButton = ActionButton("Play", symbol: "play.fill") { [weak self] in self?.togglePlayback() }
    playButton.contentTintColor = Theme.accent
    playButton.fixed(width: 90, height: 34)
    let stop = ActionButton("", symbol: "stop.fill") { [weak self] in self?.stopPlayback() }
    stop.setAccessibilityLabel("Stop playback")
    stop.fixed(width: 36, height: 34)
    let restart = ActionButton("", symbol: "backward.end.fill") { [weak self] in
      self?.selectedOrder = 0
      self?.stopPlayback()
      self?.togglePlayback()
    }
    restart.setAccessibilityLabel("Play from beginning")
    restart.fixed(width: 36, height: 34)
    transport.addArrangedSubview(restart)
    transport.addArrangedSubview(playButton)
    transport.addArrangedSubview(stop)
    let position = stack(
      .vertical,
      [positionLabel, Theme.label("PLAY · ORDER : ROW", size: 9, color: Theme.muted, weight: .medium)],
      spacing: 3)
    let tempo = stack(
      .vertical, [tempoLabel, Theme.label("BPM", size: 9, color: Theme.muted, weight: .medium)],
      spacing: 3)
    let spacer = NSView()
    spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
    liveKeyboardButton = ActionButton("Live keys") { [weak self] in self?.toggleLiveKeyboard() }
    liveKeyboardButton?.toolTip = "Keep musical keyboard input active while using other panels (⌘⌥L)"
    let paletteButton = ActionButton("Commands ⌘K",symbol:"magnifyingglass") { [weak self] in self?.showCommandPalette() }
    let header = stack(.horizontal, [brand, spacer, paletteButton, liveKeyboardButton!, transport, position, tempo], spacing: 16)
    header.fill(toolbar, inset: 10)
    brand.widthAnchor.constraint(lessThanOrEqualToConstant: 350).isActive = true
    titleLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    position.fixed(width: 116)
    tempo.fixed(width: 108)
    vertical.addArrangedSubview(toolbar)
    let divider = Panel(Theme.border)
    divider.fixed(height: 1)
    vertical.addArrangedSubview(divider)
    let workspace = NSStackView()
    workspace.orientation = .horizontal
    workspace.distribution = .fill
    workspace.spacing = 0
    workspace.alignment = .top
    let side = Panel()
    side.fixed(width: 165)
    sidebar.orientation = .vertical
    sidebar.distribution = .fill
    sidebar.alignment = .leading
    sidebar.spacing = 7
    let sideScroll = verticalScrollView()
    sideScroll.documentView = sidebar
    sideScroll.fill(side, inset: 10)
    sidebar.translatesAutoresizingMaskIntoConstraints = false
    NSLayoutConstraint.activate([
      sidebar.topAnchor.constraint(equalTo: sideScroll.contentView.topAnchor, constant: 6),
      sidebar.leadingAnchor.constraint(equalTo: sideScroll.contentView.leadingAnchor),
      sidebar.widthAnchor.constraint(equalTo: sideScroll.contentView.widthAnchor),
    ])
    workspace.addArrangedSubview(side)
    let sideRule = Panel(Theme.border)
    sideRule.fixed(width: 1)
    workspace.addArrangedSubview(sideRule)
    let editor = NSStackView()
    editor.orientation = .vertical
    editor.distribution = .fill
    editor.spacing = 0
    editor.alignment = .leading
    infoLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    let tools = Panel(Theme.bg)
    tools.fixed(height: 39)
    let flexible = NSView()
    followButton = ActionButton("Follow", symbol: "scope") { [weak self] in
      guard let self else { return }
      self.patternView.isFollowing.toggle()
    }
    followButton.fixed(width: 105)
    followButton.toolTip = "Follow playback through patterns. Turn off to browse and edit independently."
    patternView.onFollowChanged = { [weak self] enabled in
      self?.followButton.title = enabled ? "Follow on" : "Follow off"
      self?.followButton.contentTintColor = enabled ? Theme.accent : Theme.muted
      self?.followButton.setAccessibilityValue(enabled ? "On" : "Off")
    }
    loopButton = ActionButton("Loop off", symbol: "repeat") { [weak self] in self?.togglePlaybackLoop() }
    loopButton.fixed(width: 100)
    loopButton.toolTip = "Repeat the playing selection, pattern or song. Changes take effect at the next boundary."
    loopButton.setAccessibilityLabel("Loop playback")
    loopButton.setAccessibilityValue("Off")
    followButton.title = "Follow on"
    followButton.setAccessibilityValue("On")
    followButton.contentTintColor = Theme.accent
    let octave = NSPopUpButton()
    for i in 0...8 { octave.addItem(withTitle: "Octave \(i)") }
    octave.selectItem(at: 4)
    octave.target = self
    octave.action = #selector(changeOctave(_:))
    octave.fixed(width: 112)
    let editStep = NSPopUpButton()
    for i in [0, 1, 2, 4, 8] {
      editStep.addItem(withTitle: "Step \(i)")
      editStep.lastItem?.tag = i
    }
    editStep.selectItem(at: 1)
    editStep.target = self
    editStep.action = #selector(changeStep(_:))
    editStep.fixed(width: 94)
    stack(
      .horizontal, [infoLabel, flexible, octave, editStep, followButton, loopButton], spacing: 12
    ).fill(tools, inset: 12)
    editor.addArrangedSubview(tools)
    let sequence = Panel(Theme.panel)
    sequence.fixed(height: 47)
    orderStack.orientation = .horizontal
    orderStack.spacing = 6
    orderStack.alignment = .centerY
    let orderScroll = NSScrollView()
    orderScroll.drawsBackground = false
    orderScroll.hasHorizontalScroller = true
    orderScroll.documentView = orderStack
    orderStack.translatesAutoresizingMaskIntoConstraints = false
    orderScroll.fill(sequence, inset: 5)
    NSLayoutConstraint.activate([
      orderStack.topAnchor.constraint(equalTo: orderScroll.contentView.topAnchor),
      orderStack.leadingAnchor.constraint(equalTo: orderScroll.contentView.leadingAnchor),
      orderStack.heightAnchor.constraint(equalToConstant: 38),
    ])
    editor.addArrangedSubview(sequence)
    let gridRule = Panel(Theme.border)
    gridRule.fixed(height: 1)
    editor.addArrangedSubview(gridRule)
    editor.addArrangedSubview(editorHost)
    installWorkspace()
    let gridFooter = Panel(Theme.panel)
    gridFooter.fixed(height: 27)
    commandHelpLabel.lineBreakMode = .byTruncatingTail
    commandHelpLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    stack(
      .horizontal,
      [
        cursorLabel, commandHelpLabel,
      ], spacing: 12
    ).fill(gridFooter, inset: 10)
    editor.addArrangedSubview(gridFooter)
    workspace.addArrangedSubview(editor)
    vertical.addArrangedSubview(workspace)
    let bottomRule = Panel(Theme.border)
    bottomRule.fixed(height: 1)
    vertical.addArrangedSubview(bottomRule)
    let bottom = Panel()
    bottom.fixed(height: 34)
    meter.fixed(width: 170, height: 20)
    recoveryStatusButton = ActionButton("Autosave on", symbol: "clock.arrow.circlepath") { [weak self] in self?.recoverDocument() }
    recoveryStatusButton?.setAccessibilityLabel("Autosave status and recovery copies")
    recoveryStatusButton?.fixed(width: 160)
    statusLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    updateRecoveryStatus()
    stack(
      .horizontal,
      [
        statusLabel, NSView(), recoveryStatusButton!, audioLabel, Theme.label("MASTER", size: 9, color: Theme.muted),
        meter,
      ], spacing: 16
    ).fill(bottom, inset: 6)
    vertical.addArrangedSubview(bottom)
    editor.stretchAcrossAxis()
    workspace.stretchAcrossAxis()
    vertical.stretchAcrossAxis()
  }
  func refreshPattern() {
    guard !busy else { return }
    let previousDocument=model.revisionToken.split(separator:":").first
    model = PatternModel(session.snapshot(model.pattern))
    if previousDocument != model.revisionToken.split(separator:":").first {
      sampleEditor.resetDocumentContext();instrumentEditor.settingsDraft.reset()
      workspaceContextTokens.removeAll();workspaceReturnPoints.removeAll()
    }
    patternView.model = model
    updateCommandHelp()
    window.isDocumentEdited = dirty
    infoLabel.stringValue =
      String(format: "Pattern %02d", model.pattern) + "   ·   \(model.rows) rows"
  }
  func updatePositionTimeline() {
    guard patternView.positionMode == .songTime || patternView.positionMode == .patternTime else{return}
    let candidate=patternView.isFollowing && currentPlaying ? rulerPlaybackOrder ?? selectedOrder : selectedOrder
    let order=model.orders.indices.contains(candidate) && model.orders[candidate]==model.pattern ? candidate : model.orders.firstIndex(of:model.pattern)
    let capturedRevision=model.revisionToken
    let key="\(capturedRevision):\(model.pattern):\(order ?? -1)"
    guard key != positionTimelineKey,!positionTimelinePending,!busy else{return}
    patternView.positionTimes=[];positionTimelinePending=true
    var params:[String:Any]=["pattern":model.pattern];if let order{params["order"]=order}
    handleAutomation("pattern.timeline.get",params:params){[weak self] response in
      guard let self else{return};self.positionTimelinePending=false
      if let result=response["result"] as? [String:Any],self.model.revisionToken==capturedRevision,
        let data=result["data"] as? [String:Any],data["pattern"] as? Int==self.model.pattern {
        self.positionTimelineKey=key;self.patternView.positionTimes=data["positions"] as? [[String:Any]] ?? []
      }
    }
  }
  func refreshAll() {
    guard !busy else { return }
    refreshPattern()
    selectedOrder = max(0, min(selectedOrder, model.orders.count - 1))
    titleLabel.stringValue = model.title.isEmpty ? "Untitled" : model.title
    subtitleLabel.stringValue =
      "\(model.format)   /   \(model.channels) channels   /   \(model.samples.count) samples"
    tempoLabel.stringValue = model.tempoText
    refreshSidebar()
    refreshOrders()
    refreshAssets()
    window.title = "\(model.title) — ScreamSeq"
    updateRecoveryStatus()
  }
  func refreshSidebar() {
    sidebar.arrangedSubviews.forEach {
      sidebar.removeArrangedSubview($0)
      $0.removeFromSuperview()
    }
    sidebar.addArrangedSubview(
      Theme.label("SCREAMSEQ", size: 10, color: Theme.accent, weight: .bold))
    let gap = NSView()
    gap.fixed(height: 12)
    sidebar.addArrangedSubview(gap)
    sidebar.addArrangedSubview(
      Theme.label("PROJECT", size: 10, color: Theme.muted, weight: .semibold))
    sidebar.addArrangedSubview(
      ActionButton("Open module…", symbol: "folder") { [weak self] in self?.openFile() })
    sidebar.addArrangedSubview(
      ActionButton("Save", symbol: "square.and.arrow.down") { [weak self] in self?.saveFile() })
    sidebar.addArrangedSubview(
      ActionButton("Export audio…", symbol: "waveform") { [weak self] in self?.exportAudio() })
    let space = NSView()
    space.fixed(height: 16)
    sidebar.addArrangedSubview(space)
    sidebar.addArrangedSubview(
      Theme.label(
        "SAMPLES   \(model.samples.count)", size: 10, color: Theme.muted, weight: .semibold))
    for s in model.samples.prefix(16) {
      let index = s["index"] as? Int ?? 1
      let name = s["name"] as? String ?? "Sample"
      let b = ActionButton(
        String(format: "%02d  ", index) + (name.isEmpty ? "Sample \(index)" : name)
      ) { [weak self] in
        self?.patternView.instrument = index
        self?.sampleEditor.index = index
        self?.refreshAssets()
        self?.statusLabel.stringValue = "Selected sample \(index): \(name)"
      }
      b.bezelStyle = .inline
      b.alignment = .left
      b.contentTintColor = index == patternView.instrument ? Theme.accent : Theme.text
      b.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
      b.fixed(width: 174, height: 27)
      sidebar.addArrangedSubview(b)
    }
    if model.samples.isEmpty {
      sidebar.addArrangedSubview(Theme.label("No samples yet", size: 11, color: Theme.muted))
    }
    let gap2 = NSView()
    gap2.fixed(height: 20)
    sidebar.addArrangedSubview(gap2)
    sidebar.addArrangedSubview(
      ActionButton("Import sample…", symbol: "plus") { [weak self] in self?.importSample() })
    sidebar.addArrangedSubview(
      ActionButton("Song settings…") { [weak self] in self?.songSettings() })
    sidebar.addArrangedSubview(
      Theme.label("WORKSPACE", size: 10, color: Theme.muted, weight: .semibold))
    sidebar.addArrangedSubview(
      ActionButton("Audio settings…", symbol: "slider.horizontal.3") { [weak self] in
        self?.audioSettings()
      })
    sidebar.addArrangedSubview(
      ActionButton("MIDI input…", symbol: "pianokeys") { [weak self] in self?.midiSettings() })
    sidebar.addArrangedSubview(
      ActionButton("Keyboard settings…") { [weak self] in self?.keyboardPreferences() })
    sidebar.addArrangedSubview(
      ActionButton("Import report…") { [weak self] in self?.importReport() })
    sidebar.addArrangedSubview(Theme.label("OpenMPT audio engine", size: 10, color: Theme.muted))
  }
  func refreshOrders() {
    orderEditor.update(model, selected: selectedOrder)
    orderStack.arrangedSubviews.forEach {
      orderStack.removeArrangedSubview($0)
      $0.removeFromSuperview()
    }
    orderStack.addArrangedSubview(ActionButton("Arrange…") { [weak self] in self?.arrangeOrders() })
    let first = max(0, min(selectedOrder - 32, model.orders.count - 128))
    for i in first..<min(model.orders.count, first + 128) {
      let pat = model.orders[i]
      let b = ActionButton(String(format: "%02d   %02d", i, pat)) { [weak self] in
        guard let self else { return }
        self.selectedOrder = i
        self.model.pattern = pat
        self.patternView.firstRow = 0
        self.refreshPattern()
        self.refreshOrders()
      }
      b.font = .monospacedSystemFont(ofSize: 12, weight: .medium)
      b.contentTintColor = i == selectedOrder ? Theme.accent : Theme.muted
      b.fixed(width: 80, height: 32)
      orderStack.addArrangedSubview(b)
    }
    orderStack.addArrangedSubview(
      ActionButton("+ Pattern") { [weak self] in self?.addPattern(false) })
    orderStack.addArrangedSubview(
      ActionButton("Duplicate") { [weak self] in self?.addPattern(true) })
    orderStack.addArrangedSubview(
      ActionButton("Remove") { [weak self] in
        guard let self else { return }
        let index = self.selectedOrder
        self.perform(
          "Removing order…", markDirty: true, { try self.session.removeOrder(index) },
          completion: {
            self.selectedOrder = max(0, index - 1)
            self.refreshAll()
          })
      })
  }
  @objc func changeOctave(_ sender: NSPopUpButton) {
    patternView.octave = sender.indexOfSelectedItem
  }
  @objc func arrangeOrders() {
    if let orderWindow {
      orderEditor.update(model, selected: selectedOrder)
      orderWindow.makeKeyAndOrderFront(nil)
      return
    }
    let win = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 860, height: 650),
      styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "Arrange orders"
    win.minSize = NSSize(width: 760, height: 600)
    win.isReleasedWhenClosed = false
    win.delegate = self
    orderWindow = win
    orderEditor.fill(win.contentView!)
    orderEditor.onSelect = { [weak self] order in
      guard let self, !self.busy, order < self.model.orders.count else { return }
      self.selectedOrder = order
      self.model.pattern = self.model.orders[order]
      self.patternView.isFollowing = false
      self.refreshPattern()
    }
    orderEditor.onSequence = { [weak self] sequence in
      guard let self else { return }
      self.perform(
        "Selecting sequence…", { try self.session.selectSequence(sequence) },
        completion: {
          self.selectedOrder = 0
          self.model.pattern = self.model.orders.first ?? 0
          self.refreshPattern()
          self.refreshOrders()
        })
    }
    orderEditor.onMatrix = { [weak self] in self?.showArrangementMatrix() }
    orderEditor.onAnnotate = { [weak self] params in
      self?.runPatternCommand("song.annotate", params: params) { [weak self] response in
        if let error = response["error"] as? [String: Any] {
          self?.show(NSError(domain: "Arrangement", code: 1,
            userInfo: [NSLocalizedDescriptionKey: error["message"] as? String ?? "Could not update annotations"]))
        }
      }
    }
    orderEditor.onChange = { [weak self] order, pattern, operation in
      guard let self else { return }
      var nextOrder = order
      if operation == "after" || operation == "down" { nextOrder += 1 }
      if operation == "up" { nextOrder -= 1 }
      self.perform(
        "Updating order list…", markDirty: true,
        {
          if operation == "remove" {
            try self.session.removeOrder(order)
          } else {
            try self.session.editOrder(order, pattern: pattern, operation: operation)
          }
        },
        completion: {
          self.selectedOrder = max(0, min(nextOrder, self.model.orders.count - 1))
          self.model.pattern = self.model.orders[self.selectedOrder]
          self.refreshPattern()
          self.refreshOrders()
        })
    }
    orderEditor.update(model, selected: selectedOrder)
    win.center()
    win.makeKeyAndOrderFront(nil)
  }
  @objc func changeStep(_ sender: NSPopUpButton) { patternView.step = sender.selectedTag() }
  @objc func playOrStop() { togglePlayback() }
  func togglePlayback() {
    if session.playing { stopPlayback() } else { startPlayback(fromCursor: false, bounded: false) }
  }
  @objc func playFromCursor() { startPlayback(fromCursor: true, bounded: false) }
  @objc func playSelection() { startPlayback(fromCursor: false, bounded: true) }
  @objc func playSelectionFromCursor() { startPlayback(fromCursor: true, bounded: true) }
  @objc func togglePlaybackLoop() {
    guard !busy else { return }
    playbackLoop.toggle(); session.setPlaybackLoop(playbackLoop)
    loopButton.title = playbackLoop ? "Loop on" : "Loop off"
    loopButton.contentTintColor = playbackLoop ? Theme.accent : Theme.muted
    loopButton.setAccessibilityValue(playbackLoop ? "On" : "Off")
  }
  func playbackSettings(fromCursor: Bool, bounded: Bool) -> [String:Any] {
    let currentOrder = model.orders.indices.contains(selectedOrder) && model.orders[selectedOrder] == model.pattern
      ? selectedOrder : (model.orders.firstIndex(of: model.pattern) ?? selectedOrder)
    var settings: [String:Any] = ["order": currentOrder, "loop": playbackLoop]
    if bounded {
      let range = patternView.playbackSelection ?? (0..<model.rows)
      let start = range.lowerBound, end = range.upperBound
      settings["pattern"] = model.pattern; settings["startRow"] = start; settings["endRow"] = end
      settings["cursorRow"] = fromCursor && range.contains(patternView.cursorRow) ? patternView.cursorRow : start
    } else { settings["cursorRow"] = fromCursor ? patternView.cursorRow : 0 }
    return settings
  }
  func startPlayback(fromCursor: Bool, bounded: Bool) {
    guard !busy, model.rows > 0 else { return }
    let settings = playbackSettings(fromCursor: fromCursor, bounded: bounded)
    perform("Preparing playback…", refresh: false, { try self.session.playRegion(settings) },
      completion: { self.patternView.isFollowing = true; self.startLiveRecordingIfArmed() })
  }
  func stopPlayback() {
    guard !busy else { return }
    pendingNotes.removeAll()
    session.panic()
    session.stop()
    currentPlaying = false
    statusLabel.stringValue = "Stopped"
    finishLiveRecording()
  }
  func tick() {
    guard !busy else { return }
    if session.pluginLatencyChanged() {
      perform("Updating plugin delay compensation…", refresh: false, {
        try self.session.refreshPluginLatencies()
      })
      return
    }
    if session.deviceChanged() {
      perform(
        "Reconfiguring audio device…", refresh: false, { try self.session.refreshDevice() },
        completion: { self.statusLabel.stringValue = "Audio device changed · playback stopped" })
      return
    }
    do {
      var pluginError: NSError?
      let edits = session.collectPluginEdits(pluginEditor.record.state == .on, error: &pluginError)
      if let pluginError { throw pluginError }
      if edits > 0 {
        dirty = true
        window.isDocumentEdited = true
      }
    } catch { statusLabel.stringValue = error.localizedDescription }
    drainNotes()
    guard !busy else { return }
    updatePositionTimeline()
    guard !busy else {return}
    let t = session.telemetry()
    let positions=t["voicePositions"] as? [[String:Any]] ?? []
    sampleEditor.waveform.playbackFrames=positions.filter{$0["sample"] as? Int==sampleEditor.index}.compactMap{($0["sampleFrame"] as? NSNumber)?.doubleValue}
    let envelopeKind=max(0,min(2,instrumentEditor.envelopeType.indexOfSelectedItem))
    instrumentEditor.envelope.playbackTicks=positions.filter{$0["instrument"] as? Int==instrumentEditor.index}.compactMap{($0["envelopeTicks"] as? [NSNumber]).flatMap{$0.indices.contains(envelopeKind) ? $0[envelopeKind].doubleValue : nil}}
    if let loop = t["loop"] as? Bool, loop != playbackLoop {
      playbackLoop = loop; loopButton.title = loop ? "Loop on" : "Loop off"
      loopButton.contentTintColor = loop ? Theme.accent : Theme.muted; loopButton.setAccessibilityValue(loop ? "On" : "Off")
    }
    let playing = session.playing
    currentPlaying = playing
    rulerPlaybackOrder=playing ? t["order"] as? Int : nil
    if t["fault"] as? Bool == true {
      statusLabel.stringValue = "Playback stopped: engine capacity limit reached"
    }
    if t["pluginFailure"] as? Bool == true {
      statusLabel.stringValue = "Playback stopped: a plugin returned invalid output"
    }
    handleMIDI(session.midiEvents(), telemetry: t)
    if !playing && recordingTakeID != nil && !recordingFinishing {finishLiveRecording()}
    let transportTitle = playing ? "Stop" : "Play"
    if playButton.title != transportTitle {
      playButton.title = transportTitle
      playButton.image = NSImage(
        systemSymbolName: playing ? "stop.fill" : "play.fill",
        accessibilityDescription: transportTitle)
    }
    if playing {
      let order = t["order"] as? Int ?? 0
      let row = t["row"] as? Int ?? 0
      let pat = t["pattern"] as? Int ?? 0
      let position = String(format: "%03d : %02d", order, row)
      if positionLabel.stringValue != position { positionLabel.stringValue = position }
      patternView.playRow = row
      patternView.playPattern = pat
      if patternView.isFollowing {
        if pat != model.pattern {
          model.pattern = pat
          refreshPattern()
        }
        let visible = max(1, Int(patternView.bounds.height / CGFloat(patternView.rowHeight)) - 3)
        if row < patternView.firstRow || row >= patternView.firstRow + visible {
          patternView.firstRow = min(max(0, model.rows - visible), max(0, row - 4))
        }
      }
    } else {
      patternView.playRow = -1
    }
    meter.left = Float(truncating: t["left"] as? NSNumber ?? 0)
    meter.right = Float(truncating: t["right"] as? NSNumber ?? 0)
    if !playing {
      meter.left = 0
      meter.right = 0
    }
    meter.needsDisplay = true
    if mixerWindow?.isVisible == true || workspace?.visibleIDs.contains("mixer") == true {
      mixerEditor.showMeters(session.mixerMeters())
      mixerEditor.synchronize(session.automationRevision)
    }
    patternGraphHost.refresh()
    let now = CFAbsoluteTimeGetCurrent()
    if now - lastWorkspaceRefresh > 0.12 { lastWorkspaceRefresh = now; updateWorkspaceContext();if workspace?.visibleIDs.contains("graph") == true{signalGraphEditor.showActivity(t["graphActivity"] as? [[String:Any]] ?? [],playing:playing)} }
    if workspace?.visibleIDs.contains("plugins") == true && pluginEditor.meterRefreshDue(now) {
      pluginEditor.showMeters(session.pluginMeters(pluginEditor.selected))
    }
    if now - lastStatusUpdate > 1 {
      lastFPS = Double(patternView.frameCount - lastFrameCount) / max(1, now - lastStatusUpdate)
      lastFrameCount = patternView.frameCount
      lastStatusUpdate = now
      audioLabel.stringValue = String(
        format: "%@   CORE AUDIO  %.1f kHz / %d",
        String(format: "%.0f fps", lastFPS),
        session.sampleRate / 1000,
        session.bufferSize)
      if playing {
        statusLabel.stringValue =
          "Playing · \(t["voices"] ?? 0) voices · \(t["overruns"] ?? 0) audio overruns"
      } else if statusLabel.stringValue.hasPrefix("Playing") {
        statusLabel.stringValue = "Stopped"
      }
    }
  }
  @objc func undo() {
    if let text = window.firstResponder as? NSTextView, text.isEditable {
      text.undoManager?.undo()
      return
    }
    perform("Undo…", markDirty: true, { self.session.undo() })
  }
  @objc func redo() {
    if let text = window.firstResponder as? NSTextView, text.isEditable {
      text.undoManager?.redo()
      return
    }
    perform("Redo…", markDirty: true, { self.session.redo() })
  }
  @objc func openFile() {
    let panel = NSOpenPanel()
    // OpenMPT detects formats by content, including legacy extensions that have
    // no reliable Launch Services type declaration on a fresh Mac.
    panel.canChooseFiles = true
    panel.canChooseDirectories = false
    panel.allowsMultipleSelection = false
    panel.message = "Choose a tracker module or ScreamSeq project."
    panel.beginSheetModal(for: window) { [weak self] response in
      if response == .OK, let url = panel.url { self?.load(url) }
    }
  }
  func load(_ url: URL) {
    guard !busy, discardChanges() else { return }
    perform(
      "Opening \(url.lastPathComponent)…", { try self.session.openPath(url.path) },
      completion: {
        self.documentURL = url.deletingLastPathComponent().standardizedFileURL == self.recoveryDirectory.standardizedFileURL ? nil : url
        self.recordingTakeID = self.session.recordingTakeID
        self.recordingFinishing = self.recordingTakeID != nil
        self.recoveryID = UUID().uuidString
        self.selectedOrder = 0
        self.model.pattern = 0
        self.dirty = self.documentURL == nil || self.recordingTakeID != nil
        self.patternView.firstRow = 0
        self.patternView.muted.removeAll()
        self.refreshAll()
        self.model.pattern = self.model.orders.first ?? 0
        self.refreshPattern()
        self.statusLabel.stringValue =
          "Opened \(url.lastPathComponent)"
          + (self.model.issues.isEmpty ? "" : " · \(self.model.issues.count) import notes")
      })
  }
  @objc func saveFile() {
    if !["screamseq", "resonance"].contains(documentURL?.pathExtension.lowercased() ?? "") {
      saveAs()
      return
    }
    if let url = documentURL {
      save(to: url)
      return
    }
    saveAs()
  }
  @objc func saveAs() {
    guard !busy else { return }
    let ext = "screamseq"
    let panel = NSSavePanel()
    panel.nameFieldStringValue = (model.title.isEmpty ? "Untitled" : model.title) + "." + ext
    panel.allowedContentTypes = [UTType(filenameExtension: ext) ?? .data]
    panel.beginSheetModal(for: window) { [weak self] response in
      if response == .OK, let url = panel.url { self?.save(to: url) }
    }
  }
  func save(to url: URL) {
    perform(
      "Saving…", refresh: false, { try self.session.savePath(url.path) },
      completion: {
        self.documentURL = url
        self.dirty = false
        self.window.isDocumentEdited = false
        self.statusLabel.stringValue = "Saved \(url.lastPathComponent)"
        self.clearRecovery()
      })
  }
  @objc func newFile() {
    guard !busy, discardChanges() else { return }
    perform(
      "Creating song…", { self.session.newSong(false) },
      completion: {
        self.documentURL = nil
        self.recoveryID = UUID().uuidString
        self.dirty = false
        self.selectedOrder = 0
        self.model.pattern = 0
        self.patternView.muted.removeAll()
        self.refreshAll()
      })
  }
  @objc func demoFile() {
    guard !busy, discardChanges() else { return }
    perform(
      "Opening demo…", { self.session.newSong(true) },
      completion: {
        self.documentURL = nil
        self.recoveryID = UUID().uuidString
        self.dirty = false
        self.selectedOrder = 0
        self.model.pattern = 0
        self.patternView.muted.removeAll()
        self.refreshAll()
      })
  }
  @objc func exportModule() {
    guard !busy else { return }
    let ext = model.format.lowercased()
    guard ["mod", "xm", "s3m", "it", "mptm"].contains(ext) else { return }
    let panel = NSSavePanel()
    panel.title = "Export Module"
    panel.nameFieldStringValue = (model.title.isEmpty ? "Untitled" : model.title) + "." + ext
    panel.allowedContentTypes = [UTType(filenameExtension: ext) ?? .data]
    panel.beginSheetModal(for: window) { [weak self] response in
      guard response == .OK, let self, let url = panel.url else { return }
      self.perform("Exporting module…", refresh: false, { try self.session.savePath(url.path) }, completion: {
        self.statusLabel.stringValue = "Exported \(url.lastPathComponent)"
      })
    }
  }
  @objc func exportAudio() {
    guard !busy else { return }
    let panel = NSSavePanel()
    panel.allowedContentTypes = [.wav]
    panel.nameFieldStringValue = model.title + ".wav"
    panel.beginSheetModal(for: window) { [weak self] response in
      guard response == .OK, let self, let url = panel.url else { return }
      self.perform(
        "Rendering audio…", refresh: false,
        {
          let data = self.session.serializedData()
          guard !data.isEmpty else {
            throw NSError(
              domain: "ScreamSeq", code: 1,
              userInfo: [NSLocalizedDescriptionKey: "This module could not be prepared for export."]
            )
          }
          try TrackerSession.export(data, path: url.path)
        }, completion: { self.statusLabel.stringValue = "Exported \(url.lastPathComponent)" })
    }
  }
  @objc func audioSettings() {
    guard !busy else { return }
    if let settingsWindow {
      settingsWindow.makeKeyAndOrderFront(nil)
      return
    }
    let win = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 460, height: 250), styleMask: [.titled, .closable],
      backing: .buffered, defer: false)
    win.title = "Audio settings"
    win.isReleasedWhenClosed = false
    win.delegate = self
    settingsWindow = win
    let device = NSPopUpButton()
    device.addItem(withTitle: "System default")
    device.lastItem?.tag = 0
    for item in session.devices() {
      device.addItem(withTitle: item["name"] as? String ?? "Device")
      device.lastItem?.tag = item["id"] as? Int ?? 0
    }
    let buffer = NSPopUpButton()
    for n in [64, 128, 256, 512] {
      buffer.addItem(withTitle: "\(n) frames")
      buffer.lastItem?.tag = n
    }
    buffer.selectItem(at: 1)
    let apply = ActionButton("Apply") { [weak self] in
      guard let self else { return }
      do {
        try self.session.configureDevice(
          UInt(device.selectedTag()), buffer: UInt(buffer.selectedTag()))
        self.statusLabel.stringValue = "Audio device configured"
        win.close()
      } catch { self.show(error) }
    }
    let content = stack(
      .vertical,
      [
        Theme.label("Output device", size: 12, weight: .medium), device,
        Theme.label("Buffer size", size: 12, weight: .medium), buffer,
        Theme.label(
          "Device sample rate is negotiated automatically.", size: 11, color: Theme.muted), apply,
      ], spacing: 10)
    content.fill(win.contentView!, inset: 24)
    win.center()
    win.makeKeyAndOrderFront(nil)
  }
  func connectEditors() {
    sampleEditor.onSelect = { [weak self] index in
      self?.patternView.instrument = index
      self?.refreshAssets()
    }
    sampleEditor.onImport = { [weak self] in self?.importSample() }
    sampleEditor.onReplace = { [weak self] in
      guard let self else { return }
      self.importSample(replacing: self.sampleEditor.index)
    }
    sampleEditor.onPreview = { [weak self] note in
      guard let self else { return }
      let sample = self.sampleEditor.index
      self.perform(
        "Preparing sample…", refresh: false, { try self.session.previewSample(sample, note: note) })
    }
    sampleEditor.onRequest = { [weak self] method, params, reply in
      guard let self else { return }
      if method.hasSuffix(".get") { self.handleAutomation(method, params: params, reply: reply) }
      else { self.runPatternCommand(method, params: params, reply: reply) }
    }
    sampleEditor.canBeginDrawing = { [weak self] in guard let self else { return false }; return !self.busy && self.model.editable }
    sampleEditor.onSettings = { [weak self] values in
      guard let self else { return }
      let sample = self.sampleEditor.index
      self.perform(
        "Updating sample…", refresh: false, markDirty: true,
        { try self.session.sampleSettings(sample, values: values) }, completion: {
          if self.sampleEditor.index==sample {self.sampleEditor.settingsDraft.accept(values.keys)}
          self.refreshAll()
        })
    }
    sampleEditor.onInstrument = { [weak self] in self?.addInstrument() }
    instrumentEditor.onPluginAssignment = {[weak self] in self?.showInstrumentPluginAssignment()}
    instrumentEditor.onNewPluginInstrument = {[weak self] in self?.showNewPluginInstrument()}
    pluginEditor.onNewInstrument = {[weak self] slot in self?.showNewPluginInstrument(slot:slot)}
    instrumentEditor.onCreate = { [weak self] in self?.addInstrument() }
    instrumentEditor.onImport = { [weak self] in self?.importInstrument() }
    instrumentEditor.onEnvelopeBank = { [weak self] kind in self?.showInstrumentEnvelopeBank(kind) }
    instrumentEditor.onEnvelopeTools = { [weak self] kind in self?.showInstrumentEnvelopeTools(kind) }
    instrumentEditor.envelope.canEdit = { [weak self] in
      self.map { !$0.busy && $0.model.editable } ?? false
    }
    instrumentEditor.onSelect = { [weak self] index in
      self?.patternView.instrument = index
      self?.refreshAssets()
    }
    instrumentEditor.onApply = { [weak self] values in
      guard let self else { return }
      let instrument = self.instrumentEditor.index
      self.perform(
        "Updating instrument…", refresh: false, markDirty: true,
        { try self.session.instrumentSettings(instrument, values: values) }, completion: {
          if self.instrumentEditor.index==instrument {self.instrumentEditor.settingsDraft.accept(values.keys)}
          self.refreshAll()
        })
    }
    pluginEditor.onPrograms = { [weak self] slot in self?.showPluginPrograms(slot) }
    pluginEditor.onInstruments = { [weak self] slot in self?.showPluginInstruments(slot) }
    pluginEditor.onPorts = { [weak self] slot in self?.showPluginPorts(slot) }
    pluginEditor.onSavePreset = { [weak self] slot in self?.showPluginPreset(slot: slot, saving: true) }
    pluginEditor.onLoadPreset = { [weak self] slot in self?.showPluginPreset(slot: slot, saving: false) }
    pluginEditor.onSelect = { [weak self] _ in self?.refreshPlugins() }
    pluginEditor.onOpen = { [weak self] slot in
      guard let self, !self.busy else { return }
      do { try self.session.showPluginEditor(slot) } catch { self.show(error) }
    }
    pluginEditor.onAssign = { [weak self] slot, instrument in
      self?.perform(
        "Assigning instrument…", markDirty: true,
        { try self?.session.assignPlugin(slot, instrument: instrument) })
    }
    pluginEditor.onAdd = { [weak self] in self?.addPlugin() }
    pluginEditor.onAddBuiltIn = { [weak self] in self?.addPlugin(builtInOnly: true) }
    pluginEditor.onRemove = { [weak self] slot in
      self?.perform("Removing effect…", markDirty: true, { try self?.session.removePlugin(slot) })
    }
    pluginEditor.onMove = { [weak self] slot, direction in
      self?.perform(
        "Reordering effects…", markDirty: true,
        { try self?.session.movePlugin(slot, direction: direction) })
    }
    pluginEditor.onBypass = { [weak self] slot, bypass in
      self?.perform(
        "Updating effect…", markDirty: true,
        { try self?.session.bypassPlugin(slot, bypass: bypass) })
    }
    pluginEditor.onPatternAutomation = { [weak self] in self?.showPatternAutomation() }
    pluginEditor.onClearAutomation = { [weak self] in
      self?.perform(
        "Clearing automation…", markDirty: true, { try self?.session.clearAutomation() })
    }
    pluginEditor.onUndo = { [weak self] in
      self?.perform(
        "Undoing effect change…", markDirty: true, { try self?.session.undoEffectChange() })
    }
    pluginEditor.onRedo = { [weak self] in
      self?.perform(
        "Redoing effect change…", markDirty: true, { try self?.session.redoEffectChange() })
    }
    pluginEditor.onParameter = { [weak self] slot, id, value, record in
      guard let self, !self.busy else { return }
      do {
        try self.session.pluginParameter(slot, identifier: id, value: value, record: record)
        self.dirty = true
        self.window.isDocumentEdited = true
      } catch { self.show(error) }
    }
    patternView.onAudition = { [weak self] note, on in self?.audition(note: note, on: on) }
    patternView.canEdit = { [weak self] in self.map { !$0.busy && $0.model.editable } ?? false }
    patternView.onMessage = { [weak self] message in self?.statusLabel.stringValue = message }
    patternView.commandRevision = { [weak self] in self?.session.automationRevision ?? "" }
    patternView.onPaste = { [weak self] params in
      self?.runPatternCommand("pattern.paste", params: params) { [weak self] reply in
        if let error = reply["error"] as? [String: Any] {
          self?.statusLabel.stringValue = error["message"] as? String ?? "Paste failed"
        } else { self?.statusLabel.stringValue = "Pattern pasted" }
      }
    }
    patternView.onTransform = { [weak self] transform in
      guard let self, !self.busy else { return }
      let snapshot = self.model
      self.perform(
        "Editing pattern…", markDirty: true,
        {
          let cells = transform(snapshot)
          try self.session.editCells(
            cells.map {
              ["pattern": snapshot.pattern, "row": $0.0, "channel": $0.1, "values": $0.2]
            })
        })
    }
    patternView.onRowShift = { [weak self] params in
      self?.runPatternCommand("pattern.transform", params: params) { [weak self] reply in
        if let error = reply["error"] as? [String: Any] {
          self?.statusLabel.stringValue = error["message"] as? String ?? "Row edit failed"
        } else { self?.statusLabel.stringValue = params["operation"] as? String == "insertRows" ? "Row inserted" : "Row deleted" }
      }
    }
  }
  func showEditor(_ mode: Int) {
    editorMode = max(0,min(3,mode))
    patternView.renderingPaused = false
    if editorMode == 0 { focusPattern() }
    else {
      let id = ["", "samples", "instruments", "plugins"][editorMode]
      // Explicit asset selection must not be replaced by automatic cursor following.
      workspaceContextTokens[id] = "asset:\(model.pattern):\(patternView.cursorRow):\(patternView.cursorChannel)"
      workspace?.show(id)
      refreshAssets()
    }
  }
  func refreshAssets() {
    guard !busy,editorMode != 0 else{return}
    if editorMode==3 {refreshPlugins()} else {refreshWorkspaceAsset(editorMode==1 ? "samples" : "instruments")}
  }
  func refreshWorkspaceAsset(_ id:String) {
    guard !busy else{return}
    let token=(workspaceAssetTokens[id] ?? 0)+1;workspaceAssetTokens[id]=token
    let sample=id=="samples",index=sample ? sampleEditor.index : instrumentEditor.index
    worker.async {
      let data=sample ? self.session.sampleInfo(index) : self.session.instrumentInfo(index)
      let revision=self.session.automationRevision
      DispatchQueue.main.async {
        guard self.workspaceAssetTokens[id]==token,(sample ? self.sampleEditor.index : self.instrumentEditor.index)==index else{return}
        if sample {self.sampleEditor.update(data,samples:self.model.samples,revision:revision)} else {self.instrumentEditor.update(data,model:self.model)}
      }
    }
  }
  func importSample(replacing slot: Int = 0) {
    guard !busy else { return }
    if slot == 0 { showSampleBrowser(); return }
    let panel = NSOpenPanel()
    panel.title = slot > 0 ? "Replace sample \(slot)" : "Import sample"
    panel.prompt = slot > 0 ? "Replace" : "Import"
    panel.allowedContentTypes = [.wav, .aiff, .mp3]
    panel.allowsOtherFileTypes = true
    panel.beginSheetModal(for: window) { [weak self] result in
      guard result == .OK, let self, let url = panel.url else { return }
      var sample = 1
      self.perform(
        "Importing sample…", markDirty: true,
        { sample = try self.session.importSample(url.path, slot: slot) },
        completion: {
          self.sampleEditor.index = sample
          self.patternView.instrument = sample
          self.refreshAll()
          self.showEditor(1)
        })
    }
  }
  func importInstrument() {
    guard !busy else { return }
    let panel = NSOpenPanel()
    panel.title = "Import instrument"
    panel.allowedContentTypes = ["iti", "xi", "pat", "sfz"].compactMap {
      UTType(filenameExtension: $0)
    }
    panel.allowsOtherFileTypes = true
    panel.beginSheetModal(for: window) { [weak self] result in
      guard result == .OK, let self, let url = panel.url else { return }
      var index = 1
      self.perform(
        "Importing instrument…", markDirty: true,
        { index = try self.session.importInstrument(url.path, slot: 0) },
        completion: {
          self.instrumentEditor.index = index
          self.patternView.instrument = index
          self.refreshAll()
          self.showEditor(2)
        })
    }
  }
  func addInstrument() {
    let sample = sampleEditor.index
    var index = 1
    perform(
      "Creating instrument…", markDirty: true, { index = try self.session.addInstrument(sample) },
      completion: {
        self.instrumentEditor.index = index
        self.refreshAll()
        self.showEditor(2)
      })
  }
  func addPattern(_ duplicate: Bool) {
    let rows = model.rows
    let source = model.pattern
    var pattern = 0
    perform(
      "Adding pattern…", markDirty: true,
      { pattern = try self.session.addPattern(rows, duplicate: duplicate, source: source) },
      completion: {
        self.model.pattern = pattern
        self.refreshAll()
        self.selectedOrder = self.model.orders.count - 1
        self.refreshOrders()
        self.patternView.firstRow = 0
        self.showEditor(0)
      })
  }
  @objc func songSettings() {
    guard !busy else { return }
    let alert = NSAlert()
    alert.messageText = "Song settings"
    let settingsRevision = model.revisionToken
    let title = NSTextField(string: model.title)
    let tempo = NSTextField(string: model.tempoText)
    tempo.fixed(width: 96); tempo.setAccessibilityLabel("Tempo")
    let speed = numberField(model.speed, label: "Ticks per row")
    let channels = numberField(model.channels, label: "Channel count")
    title.fixed(width: 300)
    let content = stack(
      .vertical,
      [
        labeled("TITLE", title),
        stack(
          .horizontal,
          [labeled("BPM", tempo), labeled("TICKS / ROW", speed), labeled("CHANNELS", channels)],
          spacing: 24),
      ], spacing: 16)
    content.frame = NSRect(x: 0, y: 0, width: 320, height: 110)
    alert.accessoryView = content
    alert.addButton(withTitle: "Apply")
    alert.addButton(withTitle: "Cancel")
    if alert.runModal() == .alertFirstButtonReturn {
      let text = title.stringValue
      guard let bpm = Double(tempo.stringValue), bpm.isFinite, let ticks = Int(speed.stringValue), let count = Int(channels.stringValue) else {
        statusLabel.stringValue = "Enter a finite tempo and whole-number ticks/channel counts."; return
      }
      handleAutomation("document.patch", params: ["expectedRevision": settingsRevision, "title": text, "tempo": bpm, "speed": ticks, "channels": count]) { [weak self] response in
        if let error = response["error"] as? [String: Any] { self?.statusLabel.stringValue = error["message"] as? String ?? "Song settings failed" }
      }
    }
  }
  @objc func keyboardPreferences() {
    keyboardSettings.show { self.patternView.rowHeight = KeyboardSettings.rowHeight }
    keyboardSettings.window?.delegate = self
  }
  func importReport() {
    let alert = NSAlert()
    alert.messageText = "Import report"
    alert.informativeText =
      model.issues.isEmpty
      ? "No missing samples or inactive tracker plug-ins were detected. Native editing supports MOD, XM, S3M, IT, and MPTM. Other formats are preview-only."
      : model.issues.joined(separator: "\n\n")
    alert.beginSheetModal(for: window)
  }
  func refreshPlugins() {
    guard !busy else { return }
    assetToken += 1
    let token = assetToken
    let index = pluginEditor.selected
    worker.async {
      let parameters = self.session.pluginParameters(index)
      DispatchQueue.main.async {
        guard token == self.assetToken, self.editorMode == 3 else { return }
        self.pluginEditor.update(model: self.model, values: parameters)
      }
    }
  }
  func addPlugin(rescan: Bool = false, builtInOnly: Bool = false) {
    guard !busy else { return }
    let browser = PluginBrowser(builtInOnly: builtInOnly)
    browser.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    browser.onChoose = { [weak self] descriptor in
      guard let self, !self.busy else { return }
      self.pluginBrowserWindow?.close()
      self.perform("Validating plugin…", markDirty: true, { try self.session.addPlugin(descriptor) }, completion: {
        self.pluginEditor.selected = max(0, self.model.nativePlugins.count - 1); self.showEditor(3)
      })
    }
    pluginBrowserWindow?.close()
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 840, height: 680), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = builtInOnly ? "Built-in effects" : "Plugin browser"; win.minSize = NSSize(width: 760, height: 620)
    win.isReleasedWhenClosed = false; win.contentView = browser; pluginBrowserWindow = win
    win.center(); win.makeKeyAndOrderFront(nil); browser.load(rescan: rescan)
  }
  @objc func systemSleep() {
    guard !shuttingDown else { return }
    pendingNotes.removeAll()
    if busy { worker.async { self.session.stop() } } else { session.stop() }
  }
  @objc func systemWake() {
    guard !busy else { return }
    perform("Restoring audio device…", refresh: false, { try self.session.refreshDevice() })
  }
  func audition(note: Int, velocity: Int = 100, on: Bool) {
    let instrument = patternView.instrument
    if busy {
      pendingNotes.append((note, instrument, velocity, on))
      return
    }
    if !session.note(note, instrument: instrument, velocity: velocity, on: on) {
      pendingNotes.append((note, instrument, velocity, on))
      perform("Preparing audition…", refresh: false, { try self.session.prepareAudition() })
    }
  }
  func drainNotes() {
    drainInspectorNotes()
    guard !busy, !pendingNotes.isEmpty else { return }
    let pending = pendingNotes
    pendingNotes.removeAll()
    for note in pending {
      if !session.note(note.0, instrument: note.1, velocity: note.2, on: note.3) {
        pendingNotes.append(note)
      }
    }
    if !pendingNotes.isEmpty {
      perform("Preparing audition…", refresh: false, { try self.session.prepareAudition() })
    }
  }
  func handleMIDI(_ events: [[AnyHashable: Any]], telemetry: [AnyHashable: Any]) {
    guard !events.isEmpty else { return }
    var auditions = [(Int, Int, Bool)]()
    var edits = [[String: Any]]()
    for event in events {
      let status = (event["status"] as? Int ?? 0) & 0xf0
      let note = min(120, (event["note"] as? Int ?? 0) + 1)
      let velocity = event["velocity"] as? Int ?? 0
      if status == 0xb0 {
        auditions.removeAll()
        pendingNotes.removeAll()
        midiNotes.removeAll()
        session.panic()
        continue
      }
      let on = status == 0x90 && velocity > 0
      auditions.append((note, velocity, on))
      guard midiArmed else { continue }
      // Live input was timestamped and added to the take by the session. Poll
      // timing is used only for stopped, explicit step entry.
      if currentPlaying || recordingTakeID != nil {continue}
      let row =
        currentPlaying ? telemetry["row"] as? Int ?? patternView.cursorRow : patternView.cursorRow
      let pattern = currentPlaying ? telemetry["pattern"] as? Int ?? model.pattern : model.pattern
      let channel = on ? patternView.cursorChannel : midiNotes[note] ?? patternView.cursorChannel
      if on { midiNotes[note] = channel } else { midiNotes.removeValue(forKey: note) }
      // Live recording uses the separate timestamped take; this is stopped step entry.
      if on || currentPlaying {
        var values=model.cell(row,channel).map(Int.init)
        values[0]=on ? note : 255;values[1]=patternView.instrument;values[2]=on ? 1 : 0;values[3]=on ? velocity*64/127 : 0
        edits.append(["pattern":pattern,"row":row,"channel":channel,"values":values])
      }
      if !currentPlaying && on {
        patternView.cursorRow = min(model.rows - 1, row + patternView.step)
      }
    }
    if !edits.isEmpty {
      do {
        try session.editCells(edits)
        dirty = true
        refreshPattern()
      } catch { show(error) }
    }
    for event in auditions { audition(note: event.0, velocity: event.1, on: event.2) }
  }
  @objc func midiSettings() {
    guard !busy else { return }
    if let midiWindow {
      midiWindow.makeKeyAndOrderFront(nil)
      return
    }
    let win = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 520, height: 440), styleMask: [.titled, .closable],
      backing: .buffered, defer: false)
    win.title = "MIDI input"
    win.isReleasedWhenClosed = false
    win.delegate = self
    midiWindow = win
    let source = NSPopUpButton()
    source.addItem(withTitle: "Virtual input only")
    source.lastItem?.tag = 0
    for item in session.midiSources() {
      source.addItem(withTitle: item["name"] as? String ?? "MIDI source")
      source.lastItem?.tag = item["id"] as? Int ?? 0
    }
    let armed = NSButton(
      checkboxWithTitle: "Record notes into the current pattern", target: nil, action: nil)
    armed.state = midiArmed ? .on : .off
    let grid=NSPopUpButton(),columns=NSPopUpButton(),latency=NSTextField(string:String(midiLatencyMS))
    for (title,units) in [("Keep exact timing",0),("1/16 row",4096),("1/4 row",16384),("Whole row",65536)] {grid.addItem(withTitle:title);grid.lastItem?.tag=units}
    grid.selectItem(withTag:midiQuantization)
    for count in 1...max(1,min(16,model.channels-patternView.cursorChannel)) {columns.addItem(withTitle:"\(count) note columns");columns.lastItem?.tag=count}
    columns.selectItem(withTag:midiRecordColumns)
    for (view,title) in [(grid as NSControl,"Recording grid"),(columns as NSControl,"Recording note columns"),(latency as NSControl,"Input adjustment milliseconds")] {view.setAccessibilityLabel(title)}
    let apply = ActionButton("Apply") { [weak self] in
      guard let self, !self.busy else { return }
      do {
        guard let adjustment=Double(latency.stringValue),adjustment.isFinite,(-500...500).contains(adjustment) else {self.statusLabel.stringValue="Input adjustment must be −500 to +500 ms";return}
        try self.session.connectMIDI(UInt(source.selectedTag()))
        self.midiQuantization=grid.selectedTag();self.midiLatencyMS=adjustment;self.midiRecordColumns=columns.selectedTag()
        self.midiArmed = armed.state == .on
        if self.currentPlaying {if self.midiArmed {self.startLiveRecordingIfArmed()} else {self.finishLiveRecording()}}
        win.close()
      } catch { self.show(error) }
    }
    let content = stack(
      .vertical,
      [
        Theme.label("MIDI source", size: 14, weight: .semibold), source, armed,
        stack(.horizontal,[Theme.label("Timing grid",size:12),grid]),
        stack(.horizontal,[Theme.label("Recording columns",size:12),columns]),
        stack(.horizontal,[Theme.label("Input adjustment (ms)",size:12),latency]),
        Theme.label(
          "Live recording starts at the selected channel and uses adjacent columns for chords. Positive adjustment places input earlier. A stopped take is one Undo step. Step entry uses the cursor row.",
          size: 11, color: Theme.muted), apply,
      ], spacing: 18)
    content.fill(win.contentView!, inset: 24)
    win.center()
    win.makeKeyAndOrderFront(nil)
  }
  func perform(
    _ message: String, refresh: Bool = true, markDirty: Bool = false,
    _ operation: @escaping () throws -> Void, completion: @escaping () -> Void = {}
  ) {
    guard !busy else {
      NSSound.beep()
      return
    }
    var pluginError: NSError?
    let pluginEdits = session.collectPluginEdits(
      pluginEditor.record.state == .on, error: &pluginError)
    if pluginEdits > 0 {
      dirty = true
      window.isDocumentEdited = true
    }
    if let pluginError {
      show(pluginError)
      return
    }
    busy = true
    statusLabel.stringValue = message
    worker.async {
      do {
        try operation()
        DispatchQueue.main.async {
          self.busy = false
          if markDirty { self.dirty = true }
          if refresh { self.refreshAll() }
          self.statusLabel.stringValue = "Ready"
          completion()
          self.drainNotes()
        }
      } catch {
        DispatchQueue.main.async {
          self.busy = false
          self.pendingNotes.removeAll()
          self.inspectorPendingNotes.removeAll()
          self.show(error)
        }
      }
    }
  }
  func show(_ error: Error) {
    statusLabel.stringValue = error.localizedDescription
    NSSound.beep()
  }
  func discardChanges() -> Bool {
    if session.recordingTakeID != nil {statusLabel.stringValue="Finish or discard the recording take from the Pattern menu before closing or replacing the song.";return false}
    if !dirty { return true }
    let alert = NSAlert()
    alert.messageText = "Discard unsaved changes?"
    alert.informativeText = "Save your module before replacing this document."
    alert.addButton(withTitle: "Cancel")
    alert.addButton(withTitle: "Discard")
    return alert.runModal() == .alertSecondButtonReturn
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    sender !== window || (!busy && discardChanges())
  }
  func windowWillClose(_ notification: Notification) {
    guard let closing = notification.object as? NSWindow, closing !== window else { return }
    if closing === sampleBrowserWindow { (closing.contentView as? SampleBrowser)?.stop() }
    if automationTest || inspectionTest { return }
    DispatchQueue.main.async { [weak self] in
      self?.window.makeKeyAndOrderFront(nil)
    }
  }
  func windowDidChangeOcclusionState(_ notification: Notification) {
    guard let changed = notification.object as? NSWindow, changed === window else { return }
    patternView.renderingPaused = !window.occlusionState.contains(.visible)
    patternView.resetPresentationClock()
  }
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
  func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
    if shuttingDown { return .terminateLater }
    if busy { return .terminateCancel }
    guard discardChanges() else { return .terminateCancel }
    shuttingDown = true
    busy = true
    workspaceHeldKeys.removeAll()
    inspectorHeldKeys.removeAll()
    pendingNotes.removeAll()
    inspectorPendingNotes.removeAll()
    tickTimer?.invalidate()
    recoveryTimer?.invalidate()
    if let workspaceInputMonitor { NSEvent.removeMonitor(workspaceInputMonitor) }
    if let workspaceContextMonitor { NSEvent.removeMonitor(workspaceContextMonitor) }
    sampleAudition.stop()
    automationServer?.stop()
    // Reads can still be queued after busy clears. Drain them and any recovery
    // write without blocking the main thread: plugin calls may need that thread.
    worker.async {
      self.recoveryWriter.async {
        DispatchQueue.main.async { sender.reply(toApplicationShouldTerminate: true) }
      }
    }
    return .terminateLater
  }
  func applicationWillTerminate(_ notification: Notification) {
    if !automationTest && !inspectionTest,let state=workspace?.state { UserDefaults.standard.set(state,forKey:"workspaceLastLayout") }
    session.shutdown()
  }
  func application(_ sender: NSApplication, openFile filename: String) -> Bool {
    // AppKit may deliver positional option values as open-file events as well.
    if AppLaunchArguments.isOptionValue(filename,arguments:CommandLine.arguments){return true}
    let url = URL(fileURLWithPath: filename)
    if uiReady { load(url) } else { pendingOpenURL = url }
    return true
  }
  func diagnostics() -> [AnyHashable: Any] {
    var data = session.telemetry()
    data["framesPresented"] = patternView.frameCount
    data["presentationCallbacks"] = patternView.presentationCallbacks
    data["unpresentedDrawables"] = patternView.unpresentedDrawables
    data["outOfOrderPresentations"] = patternView.outOfOrderPresentations
    let times = Array(Set(patternView.presentationTimestamps)).sorted()
    let sortedIntervals = zip(times.dropFirst(), times).dropFirst(120).map { ($0.0 - $0.1) * 1000 }
    data["sortedPresentationSamples"] = sortedIntervals.count
    data["sortedP99PresentMS"] = patternView.percentile(sortedIntervals, 0.99)
    data["sortedMaxPresentMS"] = sortedIntervals.max() ?? 0
    data["lastFPS"] = lastFPS
    data["p99PresentMS"] = patternView.p99PresentMS
    data["p99CPUFrameMS"] = patternView.percentile(patternView.cpuTimes, 0.99)
    data["p99GPUFrameMS"] = patternView.percentile(patternView.gpuTimes, 0.99)
    data["p99MainThreadDrawMS"] = patternView.percentile(patternView.mainThreadTimes, 0.99)
    data["p99DrawableWaitMS"] = patternView.percentile(patternView.drawableWaitTimes, 0.99)
    data["maxMainThreadDrawMS"] = patternView.mainThreadTimes.max() ?? 0
    data["maxPresentMS"] = patternView.presentIntervals.max() ?? 0
    data["presentationSamples"] = patternView.presentIntervals.count
    data["missedPresentations"] = patternView.presentIntervals.reduce(0) {
      $0 + max(0, Int(($1 / (1000.0 / 60)).rounded()) - 1)
    }
    data["meanPresentedFPS"] =
      patternView.presentIntervals.isEmpty
      ? 0
      : Double(patternView.presentIntervals.count) * 1000
        / patternView.presentIntervals.reduce(0, +)
    data["submittedFrames"] = patternView.submittedFrames
    data["maxCPUFrameMS"] = patternView.maxFrameMS
    data["maxGPUFrameMS"] = patternView.maxGPUMS
    data["sampleRate"] = session.sampleRate
    data["bufferSize"] = session.bufferSize
    data["playing"] = session.playing
    data["windowVisible"] = window.isVisible
    data["windowOccluded"] = !window.occlusionState.contains(.visible)
    data["windowMiniaturized"] = window.isMiniaturized
    data["applicationActive"] = NSApp.isActive
    data["applicationHidden"] = NSApp.isHidden
    data["rendererPaused"] = patternView.renderingPaused
    data["windowLevel"] = window.level.rawValue
    data["processID"] = ProcessInfo.processInfo.processIdentifier
    data["executable"] = Bundle.main.executablePath ?? ""
    if let url = Bundle.main.url(forResource: "BuildInfo", withExtension: "json"),
      let bytes = try? Data(contentsOf: url),
      let build = try? JSONSerialization.jsonObject(with: bytes) as? [String: Any]
    {
      data["sourceFingerprint"] = build["sourceFingerprint"]
      data["baseRevision"] = build["baseRevision"]
    }
    data["timestamp"] = ISO8601DateFormatter().string(from: Date())
    return data
  }
  func writeDiagnostics() {
    let data = diagnostics()
    if let json = try? JSONSerialization.data(
      withJSONObject: data, options: [.prettyPrinted, .sortedKeys])
    {
      try? json.write(to: URL(fileURLWithPath: "/tmp/resonance-ui-test.json"))
    }
  }
  func makeMenu() {
    let menu = NSMenu()
    NSApp.mainMenu = menu
    func submenu(_ title: String) -> NSMenu {
      let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
      menu.addItem(item)
      let sub = NSMenu(title: title)
      item.submenu = sub
      return sub
    }
    func item(
      _ menu: NSMenu, _ title: String, _ action: Selector, _ key: String = "",
      _ modifiers: NSEvent.ModifierFlags = .command
    ) {
      let i = NSMenuItem(title: title, action: action, keyEquivalent: key)
      i.target = self
      i.keyEquivalentModifierMask = modifiers
      menu.addItem(i)
    }
    let app = submenu("ScreamSeq")
    item(app, "About ScreamSeq", #selector(about))
    app.addItem(.separator())
    item(app, "Keyboard and Display…", #selector(keyboardPreferences), ",")
    item(app, "Audio Settings…", #selector(audioSettings))
    app.addItem(.separator())
    app.addItem(
      withTitle: "Quit ScreamSeq", action: #selector(NSApplication.terminate(_:)),
      keyEquivalent: "q")
    let file = submenu("File")
    item(file, "New", #selector(newFile), "n")
    item(file, "Open…", #selector(openFile), "o")
    item(file, "Open Demo", #selector(demoFile))
    item(file, "Browse Samples…", #selector(showSampleBrowser), "l", [.command, .shift])
    item(file, "Recover a Song…", #selector(recoverDocument))
    file.addItem(.separator())
    item(file, "Save", #selector(saveFile), "s")
    item(file, "Save As…", #selector(saveAs), "s", [.command, .shift])
    item(file, "Export Module…", #selector(exportModule))
    item(file, "Export Audio…", #selector(exportAudio), "e")
    file.addItem(.separator())
    file.addItem(
      withTitle: "Close Window", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
    let edit = submenu("Edit")
    item(edit, "Undo", #selector(undo), "z")
    item(edit, "Redo", #selector(redo), "z", [.command, .shift])
    edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
    edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
    edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
    edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
    let automation = submenu("Automation")
    item(automation, "Pattern Automation…", #selector(showPatternAutomation))
    automation.addItem(.separator())
    item(automation, "Enable Local API", #selector(toggleAutomation))
    automationMenuItem = automation.items.last
    let playback = submenu("Playback")
    // Dispatch shortcuts after local responders; labels remain discoverable in the menu/palette.
    item(playback, "Play / Stop", #selector(playOrStop))
    item(playback, "Play from Cursor ⇧Space", #selector(playFromCursor))
    item(playback, "Play Selection or Pattern ⌃Space", #selector(playSelection))
    item(playback, "Play Selection or Pattern from Cursor ⌃⇧Space", #selector(playSelectionFromCursor))
    item(playback, "Toggle Playback Loop", #selector(togglePlaybackLoop))
    let pattern = submenu("Pattern")
    item(pattern, "Tempo and Groove…", #selector(showSongTiming))
    item(pattern, "Precise Notes…", #selector(showPreciseNotes), "n", [.command, .shift])
    item(pattern, "Finish Recording Take", #selector(retryRecordingFinish), "", [])
    item(pattern, "Discard Recording Take", #selector(discardLiveRecording), "", [])
    item(pattern, "Extra Effects…", #selector(showPatternPerformance), "e", [.command, .shift])
    item(pattern, "Pattern Tools…", #selector(showPatternTools), "t", [.command, .shift])
    item(pattern, "Command Picker…", #selector(showPatternCommands), "k", [.command, .shift])
    item(pattern, "New Note Track…", #selector(newNoteTrack))
    item(pattern, "Group Selected Columns…", #selector(groupNoteColumns))
    item(pattern, "Ungroup Current Track", #selector(ungroupNoteTrack))
    item(pattern, "Mix Paste (Fill Empty Fields)", #selector(mixPaste))
    item(pattern, "Merge Paste (Skip Empty Source Fields)", #selector(mergePaste))
    pattern.addItem(.separator())
    item(pattern, "Arrange Orders…", #selector(arrangeOrders))
    item(pattern, "Arrangement Matrix…", #selector(showArrangementMatrix))
    item(pattern, "New Pattern", #selector(newPattern))
    item(pattern, "Duplicate Pattern", #selector(duplicatePattern))
    pattern.addItem(.separator())
    item(pattern, "Transpose Up", #selector(transposeUp))
    item(pattern, "Transpose Down", #selector(transposeDown))
    item(pattern, "Insert Row", #selector(insertRow))
    item(pattern, "Delete Row", #selector(deleteRow))
    let workspaceMenu = submenu("Workspace")
    for tab in InspectorTabs.items {
      let entry=NSMenuItem(title:"Show "+tab.label,action:#selector(focusInspector(_:)),keyEquivalent:tab.key)
      entry.keyEquivalentModifierMask=[.control,.option];entry.target=self;entry.representedObject=tab.id
      workspaceMenu.addItem(entry)
    }
    workspaceMenu.addItem(.separator())
    item(workspaceMenu, "Audio & Modulation Graph", #selector(showSignalGraph), "g", [.command,.option])
    item(workspaceMenu, "Pattern Graph Commands", #selector(showGraphCommands), "g", [.command,.shift])
    item(workspaceMenu, "Command Palette…", #selector(showCommandPalette), "k")
    item(workspaceMenu, "Focus Pattern", #selector(focusPattern), "1", [.command,.option])
    item(workspaceMenu, "Next Panel", #selector(focusNextPanel), "\t", [.control])
    item(workspaceMenu, "Pin Focused Panel", #selector(pinFocusedPanel), "p", [.command,.option])
    item(workspaceMenu, "Follow Cursor in Panel", #selector(followFocusedPanel), "f", [.command,.option])
    item(workspaceMenu, "Return to Panel Opening Row", #selector(returnFocusedPanel), "r", [.command,.option])
    item(workspaceMenu, "Float Focused Panel", #selector(floatFocusedPanel), "d", [.command,.option])
    item(workspaceMenu, "Dock Focused Panel", #selector(dockFocusedPanel), "d", [.command,.option,.shift])
    item(workspaceMenu, "Toggle Playback Follow", #selector(toggleFollow), "f", [.control,.option])
    item(workspaceMenu, "Live Musical Keyboard", #selector(toggleLiveKeyboard), "l", [.command,.option])
    liveKeyboardItem=workspaceMenu.items.last
    workspaceMenu.addItem(.separator())
    item(workspaceMenu, "Compose Layout", #selector(composeLayout), "2", [.command,.option])
    item(workspaceMenu, "Sound Design Layout", #selector(soundLayout), "3", [.command,.option])
    item(workspaceMenu, "Pattern Focus Layout", #selector(patternLayout), "4", [.command,.option])
    item(workspaceMenu, "Save Custom Layout", #selector(saveWorkspaceLayout))
    item(workspaceMenu, "Restore Custom Layout", #selector(restoreWorkspaceLayout))
    let win = submenu("Window")
    item(win, "Mixer…", #selector(showMixer), "m", [.command, .shift])
    win.addItem(
      withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
    NSApp.windowsMenu = win
  }
  @objc func newPattern() { addPattern(false) }
  @objc func duplicatePattern() { addPattern(true) }
  @objc func transposeUp() { patternView.transpose(1) }
  @objc func transposeDown() { patternView.transpose(-1) }
  @objc func insertRow() { patternView.shiftRows(true) }
  @objc func deleteRow() { patternView.shiftRows(false) }
  @objc func about() {
    let alert = NSAlert()
    alert.messageText = "ScreamSeq"
    alert.informativeText =
      "A native macOS tracker powered by OpenMPT.\n\nOpenMPT © 1997–2026 its contributors. BSD-3-Clause.\nNative application in active development."
    alert.runModal()
  }
}

final class LevelMeter: NSView {
  var left: Float = 0, right: Float = 0
  override func draw(_ rect: NSRect) {
    Theme.bg.setFill()
    bounds.fill()
    let segment: CGFloat = 5
    for (row, level) in [left, right].enumerated() {
      let amplitude = max(0, min(1, (20 * log10(max(0.0001, level)) + 48) / 48))
      for i in 0..<Int(bounds.width / segment) {
        let active = Float(i) / Float(bounds.width / segment) < amplitude
        (active
          ? (i > Int(bounds.width / segment) * 9 / 10 ? Theme.gold : Theme.accent) : Theme.border)
          .setFill()
        NSRect(x: CGFloat(i) * segment, y: CGFloat(row) * 11, width: 3, height: 8).fill()
      }
    }
  }
}

let app = NSApplication.shared
let controller = AppController()
app.delegate = controller
#if SCREAMSEQ_SHUTDOWN_TEST
AppShutdownTest.start(controller)
#endif
app.run()
