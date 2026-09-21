import AppKit

extension AppController {
  var recoveryDirectory: URL {
    if automationTest || inspectionTest, let root = ProcessInfo.processInfo.environment["RESONANCE_AUTOMATION_TEST_DIRECTORY"] {
      return URL(fileURLWithPath: root, isDirectory: true).appendingPathComponent("Recovery", isDirectory: true)
    }
    return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("Resonance/Recovery", isDirectory: true)
  }
  var recoveryStatus: [String: Any] {
    ["enabled": model.editable, "intervalSeconds": 10, "generations": RecoveryStore.generations,
      "lastSavedAt": recoveryLastDate.map { ISO8601DateFormatter().string(from: $0) } as Any? ?? NSNull(),
      "lastCopy": recoveryLastCopy as Any? ?? NSNull(), "error": recoveryError as Any? ?? NSNull(),
      "saving": recoverySaving]
  }
  func updateRecoveryStatus() {
    if !model.editable {
      recoveryStatusButton?.title = "Recovery copies"
      recoveryStatusButton?.contentTintColor = Theme.muted
      recoveryStatusButton?.toolTip = "This imported format is read-only. Click to browse recovery copies."
      return
    }
    if let error = recoveryError {
      recoveryStatusButton?.title = "Autosave failed"
      recoveryStatusButton?.contentTintColor = Theme.gold
      recoveryStatusButton?.toolTip = error + " · Click to browse previous copies."
    } else {
      recoveryStatusButton?.title = recoveryLastDate.map {
        "Autosaved " + DateFormatter.localizedString(from: $0, dateStyle: .none, timeStyle: .short)
      } ?? "Autosave on"
      recoveryStatusButton?.contentTintColor = Theme.accent
      recoveryStatusButton?.toolTip = "Recovery copies every 10 seconds after changes. Click to recover a song."
    }
  }
  func autosave(force: Bool = false, reply: AutomationServer.Reply? = nil) {
    guard !busy, !recoverySaving else { reply?(AutomationServer.error(-32002, "The document is busy; retry shortly")); return }
    if let error = collectRecoveryEdits() {
      recoveryError = error.localizedDescription; updateRecoveryStatus()
      reply?(AutomationServer.error(-32003, error.localizedDescription)); return
    }
    let take = session.recordingTakeID
    guard model.editable else { reply?(AutomationServer.error(-32003, "This document cannot be autosaved")); return }
    let revision = session.automationRevision
    if !force && ((!dirty && take == nil && recoveryLastTake == nil) || (take == nil && recoveryLastTake == nil && recoveryLastRevision == revision)) { return }
    let store = RecoveryStore(directory: recoveryDirectory), id = recoveryID
    let title = model.title, source = documentURL?.path
    let epoch = recoveryEpoch
    busy = true; recoverySaving = true
    func finish(_ file: URL?, _ error: Error?) {
      self.recoverySaving = false
      if self.recoveryID == id && self.recoveryEpoch == epoch {
        if let file {
          self.recoveryLastRevision = revision; self.recoveryLastTake = take; self.recoveryLastDate = Date()
          self.recoveryLastCopy = file.lastPathComponent
        }
        self.recoveryError = error?.localizedDescription; self.updateRecoveryStatus()
      }
      if let error { reply?(AutomationServer.error(-32003, error.localizedDescription)) }
      else { reply?(["result": ["revision": revision, "data": self.recoveryStatus, "changed": false, "playbackStopped": false]]) }
    }
    worker.async {
      do {
        let snapshot = try self.session.recoveryData()
        DispatchQueue.main.async {
          // Capture is serialized with edits; filesystem work uses immutable data
          // on a separate queue so a slow disk does not hold the editor busy.
          self.busy = false
          self.recoveryWriter.async {
            do {
              let file = try store.save(id: id, format: "screamseq", title: title, source: source, hasRecording: take != nil) {
                try snapshot.write(to: $0, options: .atomic)
              }
              DispatchQueue.main.async { finish(file, nil) }
            } catch { DispatchQueue.main.async { finish(nil, error) } }
          }
          self.drainNotes()
        }
      } catch {
        DispatchQueue.main.async {
          self.busy = false; finish(nil, error); self.drainNotes()
        }
      }
    }
  }
  private func collectRecoveryEdits() -> NSError? {
    var error: NSError?
    if session.collectPluginEdits(pluginEditor.record.state == .on, error: &error) > 0 {
      dirty = true; window.isDocumentEdited = true
    }
    return error
  }
  func clearRecovery() {
    let store = RecoveryStore(directory: recoveryDirectory), id = recoveryID
    recoveryEpoch += 1
    recoveryLastRevision = nil; recoveryLastTake = nil; recoveryLastDate = nil; recoveryLastCopy = nil; recoveryError = nil
    updateRecoveryStatus()
    recoveryWriter.async { try? store.clear(id: id) }
  }
  @objc func recoverDocument() { listRecovery(showWhenEmpty: true) }
  func listRecovery(showWhenEmpty: Bool = false) {
    let store = RecoveryStore(directory: recoveryDirectory)
    recoveryWriter.async {
      do {
        let entries = try store.entries()
        DispatchQueue.main.async {
          guard showWhenEmpty || !entries.isEmpty else { return }
          let browser: RecoveryBrowser
          if let existing = self.recoveryWindow?.contentView as? RecoveryBrowser { browser = existing }
          else {
            browser = RecoveryBrowser(frame: .zero)
            let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 460), styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
            win.title = "Recover a song"; win.minSize = NSSize(width: 650, height: 430)
            win.isReleasedWhenClosed = false; win.contentView = browser
            self.recoveryWindow = win; win.center()
            browser.onRestore = { [weak self, weak browser] id in
              guard let self else { return }
              browser?.restore.isEnabled = false
              self.restoreRecovery(id: id) { response in
                browser?.restore.isEnabled = true
                if let error = response["error"] as? [String: Any] { browser?.message.stringValue = error["message"] as? String ?? "Recovery failed" }
                else { self.recoveryWindow?.close() }
              }
            }
          }
          browser.update(entries)
          if !self.automationTest { self.recoveryWindow?.makeKeyAndOrderFront(nil) }
        }
      } catch {
        DispatchQueue.main.async { self.recoveryError = error.localizedDescription; self.updateRecoveryStatus() }
      }
    }
  }
  func restoreRecovery(id: String, reply: @escaping AutomationServer.Reply) {
    guard !busy, !recoverySaving, window.attachedSheet == nil, NSApp.modalWindow == nil else {
      reply(AutomationServer.error(-32002, "The document is busy; retry shortly")); return
    }
    if let error = collectRecoveryEdits() { reply(AutomationServer.error(-32003, error.localizedDescription)); return }
    guard session.recordingTakeID == nil else {
      reply(AutomationServer.error(-32003, "Finish or discard the current recording take before recovering another song")); return
    }
    // Use a separate identity for the protected current song so retention can
    // never prune the very copy the user has selected for restoration.
    let store = RecoveryStore(directory: recoveryDirectory), currentID = UUID().uuidString
    let preserve = dirty, title = model.title, source = documentURL?.path
    busy = true
    worker.async {
      do {
        guard let file = try store.candidates().first(where: { $0.lastPathComponent == id }) else {
          throw CocoaError(.fileReadNoSuchFile)
        }
        if preserve {
          try store.save(id: currentID, format: "screamseq", title: title, source: source) { try self.session.saveRecoveryPath($0.path) }
        }
        let wasPlaying = self.session.playing
        try self.session.openPath(file.path)
        let revision = self.session.automationRevision
        DispatchQueue.main.async {
          self.busy = false; self.documentURL = nil; self.recoveryID = UUID().uuidString
          self.recoveryLastRevision = nil; self.recoveryLastDate = nil; self.recoveryLastCopy = nil; self.recoveryError = nil
          self.dirty = true; self.selectedOrder = 0; self.model.pattern = 0
          self.recordingTakeID = self.session.recordingTakeID; self.recordingFinishing = self.recordingTakeID != nil
          self.patternView.firstRow = 0; self.patternView.muted.removeAll(); self.refreshAll(); self.updateRecoveryStatus()
          self.statusLabel.stringValue = self.recordingTakeID == nil ? "Recovered song · Save As to keep this version" : "Recovered song and recording take · use Pattern → Finish Recording Take"
          reply(["result": ["revision": revision, "data": ["id": id, "hasRecording": self.recordingTakeID != nil], "changed": true, "playbackStopped": wasPlaying]])
          self.drainNotes()
        }
      } catch {
        DispatchQueue.main.async {
          self.busy = false; reply(AutomationServer.error(-32003, error.localizedDescription)); self.drainNotes()
        }
      }
    }
  }
  func handleRecoveryAutomation(_ method: String, params: [String: Any], reply: @escaping AutomationServer.Reply) -> Bool {
    guard ["recovery.status", "recovery.list", "recovery.save", "recovery.restore"].contains(method) else { return false }
    let allowed: Set<String> = method == "recovery.restore" ? ["id", "expectedRevision"] : method == "recovery.save" ? ["expectedRevision"] : []
    guard Set(params.keys).isSubset(of: allowed) else { reply(AutomationServer.error(-32602, "Unexpected recovery parameter")); return true }
    if method == "recovery.save" || method == "recovery.restore" {
      guard let expected = params["expectedRevision"] as? String else { reply(AutomationServer.error(-32602, "expectedRevision is required")); return true }
      guard expected == session.automationRevision else { reply(AutomationServer.error(-32001, "Song changed; read its current revision")); return true }
    }
    switch method {
    case "recovery.status":
      reply(["result": ["revision": session.automationRevision, "data": recoveryStatus, "changed": false, "playbackStopped": false]])
    case "recovery.save": autosave(force: true, reply: reply)
    case "recovery.restore":
      guard let id = params["id"] as? String, !id.isEmpty, id.count <= 200 else { reply(AutomationServer.error(-32602, "id from recovery.list is required")); return true }
      restoreRecovery(id: id, reply: reply)
    default:
      let store = RecoveryStore(directory: recoveryDirectory), revision = session.automationRevision
      recoveryWriter.async {
        do {
          let entries = try store.entries().map(\.dictionary)
          DispatchQueue.main.async { reply(["result": ["revision": revision, "data": ["copies": entries], "changed": false, "playbackStopped": false]]) }
        } catch { DispatchQueue.main.async { reply(AutomationServer.error(-32003, error.localizedDescription)) } }
      }
    }
    return true
  }
}
