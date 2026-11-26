import AppKit

/// Main-thread-only local usage & friction analytics recorder for macOS.
/// Matches the shared JSONL v1 schema in editor/UsageAnalytics.cpp verbatim.
/// Opt-in (off by default) and strictly suppressed during --inspection,
/// --automation-test, and --ui-test runs.
final class UsageAnalytics {
  let suppressed: Bool
  let configURL: URL?
  let logURL: URL?
  private(set) var enabled: Bool = false
  let sessionID: String = UUID().uuidString.lowercased()

  private let startUptime: TimeInterval = ProcessInfo.processInfo.systemUptime
  private(set) var panel: String = "pattern"
  private(set) var layout: String = "Compose"
  private(set) var playing: Bool = false

  private var last10Actions: [String] = []
  private var panelHistory: [(panel: String, ms: UInt64)] = []
  private var lastBounceLogMs: UInt64 = 0
  private var undoTimestamps: [UInt64] = []
  private var lastUndoBurstLogMs: UInt64 = 0
  private var lastStatusFriction: String = ""
  private var lastStatusFrictionMs: UInt64 = 0
  private var lastPaletteZeroQuery: String = ""

  private static let isoFormatter: ISO8601DateFormatter = {
    let f = ISO8601DateFormatter()
    f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    return f
  }()

  init(suppressed: Bool) {
    self.suppressed = suppressed
    if suppressed {
      self.configURL = nil
      self.logURL = nil
      self.enabled = false
      return
    }
    let env = ProcessInfo.processInfo.environment
    let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
      .appendingPathComponent("Resonance", isDirectory: true)
    if let overrideConfig = env["SCREAMSEQ_ANALYTICS_CONFIG"], !overrideConfig.isEmpty {
      self.configURL = URL(fileURLWithPath: overrideConfig)
    } else {
      self.configURL = support?.appendingPathComponent("usage-analytics-config.json")
    }
    if let overrideLog = env["SCREAMSEQ_ANALYTICS_LOG"], !overrideLog.isEmpty {
      self.logURL = URL(fileURLWithPath: overrideLog)
    } else {
      self.logURL = support?.appendingPathComponent("usage-analytics.jsonl")
    }
    loadConfig()
  }

  private func nowMonoMs() -> UInt64 {
    let elapsed = max(0.0, ProcessInfo.processInfo.systemUptime - startUptime)
    return UInt64(elapsed * 1000.0)
  }

  private func loadConfig() {
    guard !suppressed, let configURL,
          let data = try? Data(contentsOf: configURL),
          let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
      enabled = false
      return
    }
    enabled = (obj["enabled"] as? Bool) == true
  }

  @discardableResult
  func setEnabled(_ next: Bool) -> Bool {
    guard !suppressed, let configURL else { return false }
    let previous = enabled
    enabled = next
    do {
      try FileManager.default.createDirectory(at: configURL.deletingLastPathComponent(), withIntermediateDirectories: true)
      let payload = try JSONSerialization.data(withJSONObject: ["v": 1, "enabled": enabled], options: [.sortedKeys])
      try payload.write(to: configURL, options: .atomic)
    } catch {
      return false
    }
    if enabled && !previous {
      writeEvent(category: "usage", event: "analytics_enabled", detail: ["configPath": configURL.path], includeActions: false)
    } else if !enabled && previous {
      enabled = true
      writeEvent(category: "usage", event: "analytics_disabled", detail: [:], includeActions: false)
      enabled = false
    }
    return true
  }

  @discardableResult
  func toggleEnabled() -> Bool { setEnabled(!enabled) }

  func setContext(panel: String? = nil, layout: String? = nil, playing: Bool? = nil) {
    if let panel, !panel.isEmpty { self.panel = panel }
    if let layout, !layout.isEmpty { self.layout = layout }
    if let playing { self.playing = playing }
  }

  private func pushAction(_ summary: String) {
    guard !summary.isEmpty else { return }
    let clipped = String(summary.prefix(120))
    last10Actions.append(clipped)
    if last10Actions.count > 10 {
      last10Actions.removeFirst(last10Actions.count - 10)
    }
  }

  private func writeEvent(category: String, event: String, detail: [String: Any], includeActions: Bool) {
    guard !suppressed, enabled, let logURL else { return }
    var record: [String: Any] = [
      "v": 1,
      "ts": Self.isoFormatter.string(from: Date()),
      "monoMs": nowMonoMs(),
      "session": sessionID,
      "platform": "macos",
      "category": category,
      "event": event,
      "panel": panel,
      "layout": layout,
      "playing": playing,
      "detail": detail,
    ]
    if includeActions {
      record["last10Actions"] = last10Actions
    }
    guard var data = try? JSONSerialization.data(withJSONObject: record, options: [.sortedKeys]) else { return }
    data.append(0x0A)
    do {
      try FileManager.default.createDirectory(at: logURL.deletingLastPathComponent(), withIntermediateDirectories: true)
      if FileManager.default.fileExists(atPath: logURL.path) {
        let handle = try FileHandle(forWritingTo: logURL)
        defer { try? handle.close() }
        try handle.seekToEnd()
        try handle.write(contentsOf: data)
      } else {
        try data.write(to: logURL, options: .atomic)
      }
    } catch {
      // Never disturb musical editing if disk is full or unavailable.
    }
  }

  func recordAppStart(document: String = "") {
    guard !suppressed else { return }
    panelHistory = [(panel, nowMonoMs())]
    guard enabled else { return }
    writeEvent(category: "usage", event: "app_start", detail: ["document": String(document.prefix(120))], includeActions: false)
  }

  func recordAppStop() {
    guard !suppressed, enabled else { return }
    writeEvent(category: "usage", event: "app_stop", detail: ["durationMs": nowMonoMs()], includeActions: false)
  }

  func recordCommand(id: String, label: String, source: String = "menu", isUndo: Bool = false, deepMenuPath: String? = nil) {
    guard !suppressed else { return }
    let clean = String((label.isEmpty ? id : label).prefix(100))
    pushAction("cmd:\(clean)")
    guard enabled else { return }
    writeEvent(category: "usage", event: "command", detail: ["id": id, "label": clean, "source": source], includeActions: false)
    if let deepMenuPath, !deepMenuPath.isEmpty {
      recordDeepMenu(menuPath: deepMenuPath, command: clean)
    } else if source == "deep_menu" {
      recordDeepMenu(menuPath: clean, command: clean)
    }
    if isUndo {
      let now = nowMonoMs()
      undoTimestamps.append(now)
      undoTimestamps.removeAll { now >= $0 && now - $0 > 5000 }
      if undoTimestamps.count >= 3 && (lastUndoBurstLogMs == 0 || now >= lastUndoBurstLogMs + 5000) {
        let windowMs = now >= (undoTimestamps.first ?? now) ? (now - (undoTimestamps.first ?? now)) : 0
        lastUndoBurstLogMs = now
        writeEvent(category: "friction", event: "undo_burst", detail: ["count": undoTimestamps.count, "windowMs": windowMs], includeActions: true)
      }
    }
  }

  func recordPanelFocus(_ nextPanel: String) {
    guard !suppressed, !nextPanel.isEmpty, nextPanel != panel else { return }
    let previous = panel
    panel = nextPanel
    pushAction("panel:\(previous)->\(nextPanel)")
    let now = nowMonoMs()
    if panelHistory.isEmpty { panelHistory.append((previous, now)) }
    panelHistory.append((nextPanel, now))
    if panelHistory.count > 6 { panelHistory.removeFirst(panelHistory.count - 6) }
    guard enabled else { return }
    writeEvent(category: "usage", event: "panel_focus", detail: ["from": previous, "to": nextPanel], includeActions: false)
    if panelHistory.count >= 3 {
      let a1 = panelHistory[panelHistory.count - 3]
      let b = panelHistory[panelHistory.count - 2]
      let a2 = panelHistory[panelHistory.count - 1]
      if a1.panel == a2.panel && a1.panel != b.panel && a2.ms >= a1.ms && (a2.ms - a1.ms) <= 5000 {
        if lastBounceLogMs == 0 || now >= lastBounceLogMs + 1500 {
          lastBounceLogMs = now
          writeEvent(category: "friction", event: "panel_bounce", detail: ["panelA": a1.panel, "panelB": b.panel, "elapsedMs": a2.ms - a1.ms], includeActions: true)
        }
      }
    }
  }

  func recordLayoutChange(_ nextLayout: String) {
    guard !suppressed, !nextLayout.isEmpty, nextLayout != layout else { return }
    let previous = layout
    layout = nextLayout
    pushAction("layout:\(nextLayout)")
    guard enabled else { return }
    writeEvent(category: "usage", event: "layout_change", detail: ["from": previous, "to": nextLayout], includeActions: false)
  }

  func recordTransport(action: String, nowPlaying: Bool) {
    guard !suppressed else { return }
    playing = nowPlaying
    pushAction("transport:\(action)")
    guard enabled else { return }
    writeEvent(category: "usage", event: "transport", detail: ["action": action], includeActions: false)
  }

  func recordDocumentAction(_ action: String, detail: String = "") {
    guard !suppressed else { return }
    pushAction("doc:\(action)")
    guard enabled else { return }
    var d: [String: Any] = ["action": action]
    if !detail.isEmpty { d["detail"] = String(detail.prefix(120)) }
    writeEvent(category: "usage", event: "document_action", detail: d, includeActions: false)
  }

  func recordPaletteOpen() {
    guard !suppressed else { return }
    lastPaletteZeroQuery = ""
    pushAction("palette:open")
    guard enabled else { return }
    writeEvent(category: "usage", event: "palette_open", detail: [:], includeActions: false)
  }

  func recordPaletteQuery(_ query: String, matchCount: Int) {
    guard !suppressed else { return }
    let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
    if trimmed.isEmpty {
      lastPaletteZeroQuery = ""
      return
    }
    if matchCount == 0 {
      lastPaletteZeroQuery = String(trimmed.prefix(100))
    }
  }

  func recordPaletteExecute(query: String, command: String, matchCount: Int) {
    guard !suppressed else { return }
    let trimmed = String(query.trimmingCharacters(in: .whitespacesAndNewlines).prefix(100))
    pushAction("palette:run:\(String(command.prefix(80)))")
    guard enabled else { lastPaletteZeroQuery = ""; return }
    if !lastPaletteZeroQuery.isEmpty {
      writeEvent(category: "friction", event: "palette_zero_result", detail: ["query": lastPaletteZeroQuery, "recovered": true], includeActions: true)
      lastPaletteZeroQuery = ""
    }
    writeEvent(category: "usage", event: "palette_execute", detail: ["query": trimmed, "command": String(command.prefix(100)), "resultCount": matchCount], includeActions: false)
  }

  func recordPaletteClose(finalQuery: String, finalMatchCount: Int, executed: Bool) {
    guard !suppressed else { return }
    let trimmed = String(finalQuery.trimmingCharacters(in: .whitespacesAndNewlines).prefix(100))
    if !executed && !trimmed.isEmpty { pushAction("palette:abandoned:\(trimmed)") }
    guard enabled else { lastPaletteZeroQuery = ""; return }
    if !executed {
      if finalMatchCount == 0 && !trimmed.isEmpty {
        writeEvent(category: "friction", event: "palette_zero_result", detail: ["query": trimmed, "recovered": false], includeActions: true)
      } else if !lastPaletteZeroQuery.isEmpty {
        writeEvent(category: "friction", event: "palette_zero_result", detail: ["query": lastPaletteZeroQuery, "recovered": false], includeActions: true)
      }
      if !trimmed.isEmpty {
        writeEvent(category: "friction", event: "palette_abandoned", detail: ["query": trimmed, "matchCount": finalMatchCount], includeActions: true)
      }
    }
    lastPaletteZeroQuery = ""
  }

  static func classifyFrictionStatus(_ message: String) -> (isFriction: Bool, draftHeld: Bool) {
    let lower = message.lowercased()
    guard !lower.isEmpty else { return (false, false) }
    if lower.contains("draft held") || lower.contains("fields kept") || lower.contains("edit kept") ||
       lower.contains("retained") || lower.contains("reload saved state") {
      return (true, true)
    }
    let tokens = [
      "finish the current", "finish or reload", "unavailable", "failed",
      "cannot ", "can't ", "rejected", "invalid ", "document is busy",
      "wait for the", "outside its allowed range", "must be ", "not available"
    ]
    if tokens.contains(where: { lower.contains($0) }) { return (true, false) }
    return (false, false)
  }

  func recordStatusMessage(_ message: String, context: String? = nil) {
    guard !suppressed, !message.isEmpty else { return }
    let kind = Self.classifyFrictionStatus(message)
    guard kind.isFriction else { return }
    recordRejectedEdit(reason: message, context: context, draftHeld: kind.draftHeld)
  }

  func recordRejectedEdit(reason: String, context: String? = nil, draftHeld: Bool = false) {
    guard !suppressed, !reason.isEmpty else { return }
    let now = nowMonoMs()
    let clipped = String(reason.trimmingCharacters(in: .whitespacesAndNewlines).prefix(160))
    if clipped == lastStatusFriction && lastStatusFrictionMs > 0 && now >= lastStatusFrictionMs && (now - lastStatusFrictionMs) < 1500 {
      return
    }
    lastStatusFriction = clipped
    lastStatusFrictionMs = now
    pushAction("\(draftHeld ? "draft_held" : "rejected"):\(String(clipped.prefix(60)))")
    guard enabled else { return }
    writeEvent(category: "friction", event: draftHeld ? "draft_held" : "rejected_edit",
               detail: ["reason": clipped, "context": context ?? panel], includeActions: true)
  }

  func recordDeepMenu(menuPath: String, command: String) {
    guard !suppressed, enabled else { return }
    writeEvent(category: "friction", event: "deep_menu",
               detail: ["menuPath": String(menuPath.prefix(120)), "command": String(command.prefix(100))], includeActions: true)
  }

  @discardableResult
  func recordBookmark(trigger: String = "shortcut", note: String = "") -> String {
    guard !suppressed else { return "" }
    let id = UUID().uuidString.lowercased()
    let cleanNote = String(note.trimmingCharacters(in: .whitespacesAndNewlines).prefix(200))
    if enabled {
      writeEvent(category: "friction", event: "bookmark",
                 detail: ["bookmarkId": id, "trigger": trigger, "note": cleanNote], includeActions: true)
    }
    pushAction("bookmark:\(cleanNote.isEmpty ? String(id.prefix(8)) : String(cleanNote.prefix(40)))")
    return id
  }

  func recordBookmarkNote(bookmarkID: String, note: String) {
    guard !suppressed, !bookmarkID.isEmpty else { return }
    let cleanNote = String(note.trimmingCharacters(in: .whitespacesAndNewlines).prefix(200))
    guard !cleanNote.isEmpty else { return }
    pushAction("bookmark_note:\(String(cleanNote.prefix(40)))")
    guard enabled else { return }
    writeEvent(category: "friction", event: "bookmark_note",
               detail: ["bookmarkId": bookmarkID, "note": cleanNote], includeActions: true)
  }
}

/// Non-modal inline note bar for UsageAnalytics bookmarks.
/// Never blocks audio or modal dialogs and auto-hides if left unfocused.
final class UsageBookmarkBar: NSView, NSTextFieldDelegate {
  let promptLabel = Theme.label("Bookmark saved · optional note:", size: 11, color: Theme.gold, weight: .semibold)
  let noteField = NSTextField(string: "")
  let hintLabel = Theme.label("Return saves note · Esc hides · ignore to keep editing", size: 10, color: Theme.muted)
  var onSaveNote: ((String, String) -> Void)?
  var onDismiss: (() -> Void)?
  private var activeBookmarkID: String = ""
  private var autoHideWork: DispatchWorkItem?

  override init(frame: NSRect) {
    super.init(frame: frame)
    wantsLayer = true
    layer?.backgroundColor = Theme.panel.cgColor
    isHidden = true
    noteField.placeholderString = "Optional 5-word note (e.g. 'lost cursor after paste')…"
    noteField.delegate = self
    noteField.target = self
    noteField.action = #selector(submitNote)
    noteField.setAccessibilityLabel("Optional friction bookmark note")
    let closeButton = ActionButton("Hide") { [weak self] in self?.dismiss(restoreFocus: true) }
    let row = stack(.horizontal, [promptLabel, noteField, hintLabel, closeButton], spacing: 8)
    row.fill(self, inset: 6)
    fixed(height: 34)
  }
  required init?(coder: NSCoder) { fatalError() }

  func present(bookmarkID: String, analyticsEnabled: Bool) {
    activeBookmarkID = bookmarkID
    promptLabel.stringValue = analyticsEnabled
      ? "Bookmark saved · optional note:"
      : "Analytics is off (enable in Workspace → Local Usage Analytics)"
    noteField.stringValue = ""
    noteField.isEnabled = analyticsEnabled
    isHidden = false
    scheduleAutoHide(after: 10.0)
    if analyticsEnabled {
      window?.makeFirstResponder(noteField)
    }
  }

  private func scheduleAutoHide(after seconds: TimeInterval) {
    autoHideWork?.cancel()
    let work = DispatchWorkItem { [weak self] in
      guard let self, !self.isHidden else { return }
      let focused = (self.window?.firstResponder as? NSTextView)?.delegate as? NSTextField === self.noteField
      if !focused || self.noteField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
        self.dismiss(restoreFocus: false)
      } else {
        self.scheduleAutoHide(after: 10.0)
      }
    }
    autoHideWork = work
    DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: work)
  }

  @objc private func submitNote() {
    let text = noteField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
    if !text.isEmpty && !activeBookmarkID.isEmpty {
      onSaveNote?(activeBookmarkID, text)
    }
    dismiss(restoreFocus: true)
  }

  func dismiss(restoreFocus: Bool) {
    autoHideWork?.cancel()
    autoHideWork = nil
    isHidden = true
    if restoreFocus { onDismiss?() }
  }

  func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
    if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
      dismiss(restoreFocus: true)
      return true
    }
    return false
  }

  func controlTextDidChange(_ obj: Notification) {
    scheduleAutoHide(after: 12.0)
  }
}
