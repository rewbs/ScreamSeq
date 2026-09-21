import AppKit

extension AppController {
  @objc func toggleAutomation() {
    if let server = automationServer {
      server.stop()
      automationServer = nil
      automationMenuItem?.state = .off
      statusLabel.stringValue = "Local automation disabled"
    } else {
      do {
        let testDirectory =
          automationTest
          ? ProcessInfo.processInfo.environment["RESONANCE_AUTOMATION_TEST_DIRECTORY"].map {
            URL(fileURLWithPath: $0, isDirectory: true)
          } : nil
        automationServer = try AutomationServer(discoveryDirectory: testDirectory) {
          [weak self] method, params, reply in
          self?.handleAutomation(method, params: params, reply: reply)
        }
        automationMenuItem?.state = .on
        statusLabel.stringValue = "Local automation enabled for applications in your account"
      } catch { show(error) }
    }
  }

  func handleAutomation(
    _ method: String, params: [String: Any], reply: @escaping AutomationServer.Reply
  ) {
    guard !shuttingDown else {
      reply(AutomationServer.error(-32002, "The application is shutting down"))
      return
    }
    if handleWorkspaceAutomation(method, params: params, reply: reply) { return }
    if handleSampleLibraryAutomation(method, params: params, reply: reply) { return }
    let ownMultisampleReview = method == "instrument.importMultisample" && NSApp.modalWindow?.contentView is MultisampleImportView
    guard !busy, window.attachedSheet == nil, NSApp.modalWindow == nil || ownMultisampleReview else {
      reply(AutomationServer.error(-32002, "The document is busy; retry shortly"))
      return
    }
    var error: NSError?
    let edits = session.collectPluginEdits(pluginEditor.record.state == .on, error: &error)
    if edits > 0 {
      dirty = true
      window.isDocumentEdited = true
    }
    if let error {
      reply(AutomationServer.error(-32003, error.localizedDescription))
      return
    }
    if handleRecoveryAutomation(method, params: params, reply: reply) { return }
    var navigationChanged = false
    if method == "context.set" {
      do {
        let current = patternView.navigation
        let prepared = try current.prepared(params, revision: session.automationRevision,
          contextToken: patternView.contextToken, patterns: model.patterns, channels: model.channels, effectColumns: model.effectColumns)
        navigationChanged = prepared != current
        let moved = prepared.pattern != current.pattern || prepared.row != current.row || prepared.channel != current.channel || prepared.column != current.column
        if prepared.pattern != model.pattern { model.pattern = prepared.pattern; refreshPattern() }
        patternView.navigate(prepared, clearSelection: moved)
      } catch let error as EditorNavigation.Failure {
        reply(AutomationServer.error(error.code, error.message)); return
      } catch { reply(AutomationServer.error(-32602, error.localizedDescription)); return }
    }
    let sampleFrames=model.samples.first { $0["index"] as? Int == sampleEditor.index }?["frames"] as? Int ?? 0
    let context: [String: Any] = [
      "pattern": model.pattern, "row": patternView.cursorRow,
      "channel": patternView.cursorChannel, "column": patternView.column,
      "instrument": patternView.instrument,
      "octave": patternView.octave, "step": patternView.step, "order": selectedOrder,
      "selection": patternView.automationSelection,
      "following": patternView.isFollowing, "contextRevision": patternView.contextToken,
      "playback": ["playing": session.playing, "pattern": patternView.playPattern, "row": patternView.playRow],
      "sampleSelection": sampleEditor.automationContext(frames: sampleFrames),
      "sampleViewport": sampleEditor.viewportContext(frames:sampleFrames,revision:session.automationRevision),
      "editor": ["patterns", "samples", "instruments", "plugins"][editorMode],
      "dirty": dirty, "file": documentURL?.path as Any? ?? NSNull(), "autosave": recoveryStatus,
    ]
    if method == "context.get" || method == "context.set" {
      guard method == "context.set" || params.isEmpty else {
        reply(AutomationServer.error(-32602, "context.get accepts no parameters"))
        return
      }
      reply(["result": ["revision": session.automationRevision, "data": context, "changed": false,
        "contextChanged": navigationChanged, "playbackStopped": false]])
      return
    }
    busy = true
    let before = session.automationRevision
    worker.async {
      var error: NSError?
      let result = self.session.automationMethod(method, params: params, error: &error)
      let changed = before != self.session.automationRevision
      let revision = self.session.automationRevision
      DispatchQueue.main.async {
        self.busy = false
        if changed {
          self.dirty = true
          self.refreshAll()
        }
        if let result {
          if method == "document.save", let saved = result["data"] as? [String: Any],
            saved["written"] as? Bool == true, let path = saved["path"] as? String {
            self.documentURL = URL(fileURLWithPath: path)
            self.dirty = false
            self.window.isDocumentEdited = false
            self.clearRecovery()
            self.statusLabel.stringValue = "Saved \(self.documentURL!.lastPathComponent)"
          }
          if method == "api.describe", var described = result as? [String: Any], var data = described["data"] as? [String: Any] {
            data["reads"] = (data["reads"] as? [String] ?? []) + ["workspace.commands.get", "workspace.get", "recovery.status", "recovery.list", "sample.library.get", "sample.library.search", "sample.library.inspect", "sample.library.multisample.get"]
            data["writes"] = (data["writes"] as? [String] ?? []) + ["workspace.ruler", "workspace.shortcut.set", "workspace.panel", "workspace.layout", "recovery.save", "recovery.restore", "sample.library.roots.set", "sample.library.rescan", "sample.library.preview", "sample.library.preview.stop"]
            data["sampleLibrary"] = "Application only: cached local search of filenames and inherited folder tags. Library methods use libraryRevision, not the song revision. sample.importMany works in both hosts and is one atomic document Undo."
            data["recovery"] = "Application only: automatic recovery copies every 10 seconds; restore protects current unsaved edits and opens an unsaved song. Recordings reopen as stopped takes."
            described["data"] = data; reply(["result": described])
          } else { reply(["result": result]) }
        } else {
          reply(
            AutomationServer.error(
              error?.code ?? -32003, error?.localizedDescription ?? "Operation failed",
              data: ["revision": revision]))
        }
        self.drainNotes()
      }
    }
  }
}
