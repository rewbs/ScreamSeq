import AppKit
extension AppController {
  @objc func newNoteTrack() { showNoteTrackEditor(creating: true) }
  @objc func groupNoteColumns() { showNoteTrackEditor(creating: false) }
  func showNoteTrackEditor(creating: Bool) {
    guard !busy, model.editable else { return }
    let region = patternView.automationSelection
    let first = region["startChannel"] ?? patternView.cursorChannel, last = region["endChannel"] ?? patternView.cursorChannel
    let editor = NoteTrackEditor(model: model, channels: Array(first...last), creating: creating)
    editor.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    noteTrackWindow?.close()
    let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 580, height: 370), styleMask: [.titled, .closable], backing: .buffered, defer: false)
    win.title = creating ? "New note track" : "Group note columns"; win.isReleasedWhenClosed = false
    win.contentView = editor; noteTrackWindow = win; win.center(); win.makeKeyAndOrderFront(nil)
  }
  @objc func ungroupNoteTrack() {
    guard !busy, model.editable, let track = model.noteTrackByChannel[patternView.cursorChannel] else { return }
    handleAutomation("track.ungroup", params: ["expectedRevision": model.revisionToken, "track": track.id]) { [weak self] response in
      if let error = response["error"] as? [String: Any] { self?.statusLabel.stringValue = error["message"] as? String ?? "Ungroup failed" }
      else { self?.statusLabel.stringValue = "Columns ungrouped. Mixer routing and effects are preserved." }
    }
  }
}
