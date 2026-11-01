import AppKit

extension AppController {
  var midiRevision: String { "midi:\(midiSettingsIdentity):\(midiSettingsGeneration)" }
  func midiSettingsData() -> [String: Any] {
    let sources = session.midiSources(), selected = sources.first { $0["id"] as? Int == midiSourceID }
    let connected = midiSourceID != 0 && selected != nil && session.connectedMIDISource == UInt(midiSourceID)
    return ["revision": midiRevision, "source": midiSourceID == 0 ? "" : "coremidi:\(midiSourceID)",
      "armed": midiArmed, "channelsCount": midiRecordColumns, "quantization": midiQuantization, "latencyMS": midiLatencyMS,
      "connected": connected, "connectedSource": connected ? "coremidi:\(midiSourceID)" : "",
      "name": selected?["name"] as? String ?? "Virtual input only", "hostClock": "CoreMIDI host ticks", "inputPrecision": "CoreMIDI timestamps"]
  }
  func applyMidiSettings(_ params: [String: Any]) throws -> [String: Any] {
    func reject(_ message: String, _ code: Int = -32602) -> NSError { NSError(domain: "MIDI", code: code, userInfo: [NSLocalizedDescriptionKey: message]) }
    guard Set(params.keys).isSubset(of: ["expectedMidiRevision", "source", "armed", "channelsCount", "quantization", "latencyMS", "dryRun"]),
      let expected = params["expectedMidiRevision"] as? String, !expected.isEmpty, expected.utf8.count <= 200 else { throw reject("Supply complete MIDI settings and expectedMidiRevision") }
    guard expected == midiRevision else { throw reject("MIDI settings changed; reload settings before applying", -32001) }
    func integer(_ name: String, _ range: ClosedRange<Int>) throws -> Int {
      guard let n = params[name] as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID(), n.doubleValue.isFinite,
        n.doubleValue.rounded(.towardZero) == n.doubleValue, Double(range.lowerBound)...Double(range.upperBound) ~= n.doubleValue else { throw reject("Invalid \(name)") }
      return n.intValue
    }
    guard let source = params["source"] as? String, source.utf8.count <= 4096,
      let armed = params["armed"] as? NSNumber, CFGetTypeID(armed) == CFBooleanGetTypeID(),
      let latency = params["latencyMS"] as? NSNumber, CFGetTypeID(latency) != CFBooleanGetTypeID(), latency.doubleValue.isFinite, (-500...500).contains(latency.doubleValue) else { throw reject("Invalid MIDI source, arm or input adjustment") }
    let columns = try integer("channelsCount", 1...127), quantum = try integer("quantization", 0...65536)
    let sourceID: Int
    if source.isEmpty { sourceID = 0 }
    else if source.hasPrefix("coremidi:"), let value = UInt32(source.dropFirst(9)), value != 0 { sourceID = Int(value) }
    else { throw reject("Choose a source from midi.devices.get") }
    let dry: Bool
    if let value = params["dryRun"] { guard let n = value as? NSNumber, CFGetTypeID(n) == CFBooleanGetTypeID() else { throw reject("dryRun must be boolean") }; dry = n.boolValue } else { dry = false }
    let same = sourceID == midiSourceID && armed.boolValue == midiArmed && columns == midiRecordColumns && quantum == midiQuantization && latency.doubleValue == midiLatencyMS
    let reconnect = sourceID != 0 && session.connectedMIDISource != UInt(sourceID)
    if same && !dry && !reconnect { return midiSettingsData() }
    guard sourceID == 0 || session.midiSources().contains(where: { $0["id"] as? Int == sourceID }) else { throw reject("Selected MIDI input is unavailable; rescan inputs") }
    if dry { var data = midiSettingsData(); var proposed = params; proposed.removeValue(forKey: "expectedMidiRevision"); proposed.removeValue(forKey: "dryRun"); data["proposed"] = proposed; return data }
    // The captured take keeps its original instrument, columns and timing.
    // Stop capture before a source transition; the musician can still review it.
    if session.recordingActive && (sourceID != midiSourceID || !armed.boolValue), let take = session.recordingTakeID {
      var error: NSError?
      guard session.automationMethod("recording.stop", params: ["take": take, "expectedRevision": session.automationRevision], error: &error) != nil else { throw error ?? reject("Could not retain the current recording take") }
      recordingTakeID = take; recordingFinishing = true
    }
    if sourceID != midiSourceID || reconnect { try session.connectMIDI(UInt(sourceID)); releaseMIDIAuditions() }
    midiSourceID = sourceID; midiArmed = armed.boolValue; midiRecordColumns = columns; midiQuantization = quantum; midiLatencyMS = latency.doubleValue; midiSettingsGeneration += 1
    if midiArmed && currentPlaying && session.recordingTakeID == nil { startLiveRecordingIfArmed() }
    return midiSettingsData()
  }
  func handleMIDIAutomation(_ method: String, params: [String: Any], reply: @escaping AutomationServer.Reply) -> Bool {
    guard method.hasPrefix("midi.") else { return false }
    guard !busy, !sessionReading else { reply(AutomationServer.error(-32002, "The document is busy; retry shortly")); return true }
    do {
      let data: [String: Any]
      switch method {
      case "midi.settings.get": guard params.isEmpty else { throw NSError(domain: "MIDI", code: -32602, userInfo: [NSLocalizedDescriptionKey: "midi.settings.get accepts no parameters"]) }; data = midiSettingsData()
      case "midi.devices.get":
        guard params.isEmpty else { throw NSError(domain: "MIDI", code: -32602, userInfo: [NSLocalizedDescriptionKey: "midi.devices.get accepts no parameters"]) }
        data = ["devices": session.midiSources().map { ["id": "coremidi:\($0["id"] as? Int ?? 0)", "name": $0["name"] as? String ?? "MIDI source"] }]
      case "midi.settings.set": data = try applyMidiSettings(params)
      default: reply(AutomationServer.error(-32601, "Unknown MIDI method")); return true
      }
      reply(["result": ["revision": midiRevision, "data": data, "changed": false, "playbackStopped": false]])
    } catch { let error = error as NSError; reply(AutomationServer.error(error.code, error.localizedDescription)) }
    return true
  }
  @objc func midiSettings() {
    guard !busy else { return }
    if let midiWindow { midiWindow.makeKeyAndOrderFront(nil); midiEditor.startReview(); return }
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 760, height: 600), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
    win.title = "MIDI & recording"; win.minSize = NSSize(width: 720, height: 520); win.isReleasedWhenClosed = false; win.delegate = self
    midiWindow = win; win.contentView = midiEditor
    midiEditor.onSources = { [weak self] in self?.session.midiSources().compactMap { $0 as? [String: Any] } ?? [] }
    midiEditor.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    midiEditor.onAcceptedArm = {[weak self] in self?.midiArmed ?? false}
    midiEditor.onSound = {[weak self] in guard let self else{return ""};return "Sound \(self.patternView.instrument) · from channel \(self.patternView.cursorChannel+1)"}
    midiEditor.onChooseSound = {[weak self] in self?.chooseMIDISound()}
    var settingsRevision = midiRevision
    midiEditor.onSettings = { [weak self] source, armed, quantum, columns, latency, completion in
      guard let self else {completion("MIDI settings closed");return}
      let params:[String:Any]=["source": source == 0 ? "" : "coremidi:\(source)", "armed": armed, "quantization": quantum, "channelsCount": columns, "latencyMS": latency, "expectedMidiRevision": settingsRevision]
      self.submitMIDISettings(params){error in if error==nil{settingsRevision=self.midiRevision};completion(error)}
    }
    midiEditor.onArm = {[weak self] armed,completion in
      guard let self else{completion("MIDI settings closed");return}
      let before=self.midiRevision
      let params:[String:Any]=["source":self.midiSourceID==0 ? "":"coremidi:\(self.midiSourceID)","armed":armed,"quantization":self.midiQuantization,"channelsCount":self.midiRecordColumns,"latencyMS":self.midiLatencyMS,"expectedMidiRevision":before]
      self.submitMIDISettings(params){error in
        if error==nil,settingsRevision==before{settingsRevision=self.midiRevision}
        completion(error)
      }
    }
    midiEditor.onReloadSettings = { [weak self] in
      guard let self, !self.busy, !self.sessionReading else { return }
      settingsRevision = self.midiRevision
      self.midiEditor.configure(source: self.midiSourceID, armed: self.midiArmed, quantum: self.midiQuantization, columns: self.midiRecordColumns, latency: self.midiLatencyMS, reload: true)
    }
    midiEditor.onFinish = { [weak self] take in
      guard let self, !self.busy, self.session.recordingTakeID == take else { return }
      self.recordingTakeID = take; self.retryRecordingFinish()
    }
    midiEditor.onDiscard = { [weak self] take in
      guard let self, !self.busy, self.session.recordingTakeID == take else { return }; self.discardLiveRecording()
    }
    midiEditor.onReturn = { [weak self] in self?.window.makeKeyAndOrderFront(nil); self?.focusPattern() }
    midiEditor.onNavigate = { [weak self] event, revision in
      guard let self, !self.busy, revision.split(separator: ":").first == self.model.revisionToken.split(separator: ":").first,
        let pattern = self.model.patterns.first(where: { $0["id"] as? String == event["patternID"] as? String })?["index"] as? Int,
        let channel = self.model.tracks.first(where: { $0["id"] as? String == event["track"] as? String })?["index"] as? Int else { self?.midiEditor.status.stringValue = "The recorded pattern or track is no longer available."; return }
      self.model.pattern = pattern; self.refreshPattern()
      self.patternView.cursorRow = min(self.model.rows - 1, (event["position"] as? Int ?? 0) / 65536); self.patternView.cursorChannel = channel; self.patternView.column = 0; self.patternView.isFollowing = false
      self.showPreciseNotes(); self.window.makeKeyAndOrderFront(nil)
    }
    midiEditor.configure(source: midiSourceID, armed: midiArmed, quantum: midiQuantization, columns: midiRecordColumns, latency: midiLatencyMS)
    win.center(); win.makeKeyAndOrderFront(nil); midiEditor.startReview()
  }
  func submitMIDISettings(_ params:[String:Any],completion:@escaping(String?)->Void){
    let document=recoveryID,stamp=midiEditor.settingsStamp
    let perform = {[weak self] in
      guard let self,self.recoveryID==document,self.midiEditor.settingsStamp==stamp,self.midiWindow?.isVisible==true else{completion("MIDI settings action canceled because its draft, window or document changed.");return}
      do{_ = try self.applyMidiSettings(params);completion(nil)}catch{completion(error.localizedDescription)}
    }
    if busy || sessionReading {deferUntilIdle(perform,cancel:{completion("MIDI settings action canceled")})}
    else{perform()}
  }
  func chooseMIDISound(){
    guard !busy,!sessionReading else{return}
    let instruments = !model.instruments.isEmpty,items=instruments ? model.instruments:model.samples,document=recoveryID
    let menu=NSMenu(title:"Recording sound");menu.autoenablesItems=false
    for item in items {
      guard let index=item["index"] as? Int,(1...255).contains(index) else{continue}
      let identity=item["id"] as? String
      let action=ContextAction("\(index) · \(item["name"] as? String ?? "Sound")"){[weak self] in
        guard let self,self.recoveryID==document else{return}
        let current=instruments ? self.model.instruments:self.model.samples
        guard let chosen=current.first(where:{identity == nil ? $0["index"] as? Int==index:$0["id"] as? String==identity}),let slot=chosen["index"] as? Int else{return}
        self.patternView.instrument=slot;self.midiEditor.refresh()
      }
      action.state=index==patternView.instrument ? .on:.off;menu.addItem(action)
    }
    if menu.items.isEmpty{midiEditor.status.stringValue="Import a sample or create an instrument first.";return}
    menu.popUp(positioning:nil,at:NSPoint(x:20,y:midiEditor.bounds.height-90),in:midiEditor)
  }
}
