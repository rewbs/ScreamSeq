import AppKit

private final class UnavailableCommandTarget: NSObject, NSMenuItemValidation {
  var allowed = false, runs = 0
  @objc func runCommand(_ sender: Any?) { runs += 1 }
  func validateMenuItem(_ item: NSMenuItem) -> Bool { allowed }
}
extension InterfaceTests {
  static func patternGridChecks() throws {
    let prior = UserDefaults.standard.volatileDomain(forName: UserDefaults.argumentDomain)
    defer { UserDefaults.standard.setVolatileDomain(prior, forName: UserDefaults.argumentDomain) }
    var defaults = prior
    defaults["returnStartsPlayback"] = false
    defaults["noteKeysLow"] = "zsxdcvgbhnjm"; defaults["noteKeysHigh"] = "q2w3er5t6y7u"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    func freshModel(rows: Int = 8, channels: Int = 2, pattern: Int = 0, revision: String = "doc-a:1") -> PatternModel {
      PatternModel(["rows": rows, "channels": channels, "pattern": pattern, "revisionToken": revision,
        "cells": Data(repeating: 0, count: max(0, rows * channels * 6))])
    }
    let grid = PatternView()
    grid.frame = NSRect(x: 0, y: 0, width: 800, height: 400)
    grid.model = freshModel()
    var edits = 0, auditions = [(Int, Bool)](), busy = false
    grid.onEdit = { row, channel, values in
      edits += 1
      var next = grid.model; next.replaceCell(row, channel, with: values); grid.model = next
    }
    grid.onAudition = { note, _, _, on in auditions.append((note, on)) }
    grid.canEdit = { !busy }

    // 1. Control/Option chords are never data entry in any field.
    try require(KeyboardSettings.isDataTyping([.shift]) && !KeyboardSettings.isDataTyping([.control]) &&
      !KeyboardSettings.isDataTyping([.option]) && !KeyboardSettings.isDataTyping([.command]), "One predicate defines musical typing")
    key(grid, 8, "c", flags: .control); key(grid, 9, "v", flags: .option)
    try require(edits == 0 && grid.cursorRow == 0 && auditions.isEmpty, "Control and Option chords do not enter notes")
    for column in 1...4 {
      grid.column = column
      key(grid, 8, "c", flags: .control); key(grid, 18, "1", flags: .option)
    }
    var effects = 0
    grid.onTrackerEffect = { _, _, _, _, _ in effects += 1 }
    try require(edits == 0 && effects == 0, "Control and Option chords do not enter instrument, volume or FX data")
    grid.column = 0
    key(grid, 125, "", flags: .control)
    try require(grid.cursorRow == 1, "Modified cursor keys keep navigating")
    grid.cursorRow = 0

    // 2 and 3. Typing while busy is queued in order, replayed when busy clears,
    // and a key already released is not auditioned on replay.
    busy = true
    key(grid, 6, "z"); key(grid, 6, "z", up: true)
    key(grid, 125, "")
    key(grid, 7, "x")
    try require(edits == 0 && grid.deferredKeyCount == 3 && grid.cursorRow == 0, "Busy typing waits instead of being dropped")
    key(grid, 49, " ")
    try require(grid.deferredKeyCount == 3, "Transport is never queued")
    busy = false; grid.replayDeferredKeys()
    try require(edits == 2 && grid.model.cell(0, 0)[0] == 49 && grid.model.cell(2, 0)[0] == 51 && grid.cursorRow == 3 && grid.deferredKeyCount == 0,
      "Queued notes and navigation replay in order")
    try require(auditions.count == 1 && auditions[0].0 == 51 && auditions[0].1, "A replayed key whose key-up already happened starts no audition")
    key(grid, 7, "x", up: true)
    try require(auditions.count == 2 && auditions[1].0 == 51 && !auditions[1].1, "A replayed key that is still held is released by its key-up")
    busy = true
    for _ in 0..<(PatternView.deferredKeyLimit + 5) { key(grid, 6, "z"); key(grid, 6, "z", up: true) }
    try require(grid.deferredKeyCount == PatternView.deferredKeyLimit, "The key queue is bounded")
    key(grid, 53, "\u{1b}")
    try require(grid.deferredKeyCount == 0, "Escape clears queued typing")
    key(grid, 6, "z"); grid.navigate(EditorNavigation(pattern: 0, row: 1, channel: 0, column: 0, following: false), clearSelection: true)
    try require(grid.deferredKeyCount == 0, "Explicit navigation clears queued typing")
    key(grid, 6, "z"); _ = grid.resignFirstResponder()
    try require(grid.deferredKeyCount == 0, "Leaving the grid clears queued typing")
    key(grid, 6, "z"); grid.model = freshModel(pattern: 1)
    try require(grid.deferredKeyCount == 0, "Changing pattern clears queued typing")
    key(grid, 6, "z"); grid.model = freshModel(pattern: 1, revision: "doc-b:1")
    try require(grid.deferredKeyCount == 0, "Changing document clears queued typing")
    var locked = freshModel(); locked.editable = false; grid.model = locked
    key(grid, 6, "z")
    try require(grid.deferredKeyCount == 0, "A document that is not editable queues nothing")
    busy = false; grid.model = freshModel(); edits = 0; auditions.removeAll(); grid.cursorRow = 0

    // 4. Auto-repeat of a key mapped to a note is consumed, including "1".
    defaults["noteKeysLow"] = "1sxdcvgbhnjm"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    key(grid, 18, "1")
    try require(edits == 1 && grid.model.cell(0, 0)[0] == 49 && grid.cursorRow == 1, "A remapped note key enters its note")
    key(grid, 18, "1", repeatKey: true)
    try require(edits == 1 && grid.model.cell(1, 0)[0] == 0 && grid.cursorRow == 1, "Repeat of a remapped note key writes no note-off")
    key(grid, 18, "1", up: true)
    defaults["noteKeysLow"] = "zsxdcvgbhnjm"
    UserDefaults.standard.setVolatileDomain(defaults, forName: UserDefaults.argumentDomain)
    key(grid, 18, "1")
    try require(grid.model.cell(1, 0)[0] == 255, "The default note-off key still works")

    // 5. Stored positions and clicks are clamped to the current pattern.
    grid.model = freshModel(rows: 4, channels: 2)
    grid.navigate(EditorNavigation(pattern: 0, row: 99, channel: 9, column: 40, following: false), clearSelection: true)
    try require(grid.cursorRow == 3 && grid.cursorChannel == 1 && grid.column == grid.model.lastField(1), "Navigation clamps to a pattern that shrank")
    grid.navigate(EditorNavigation(pattern: 0, row: -5, channel: -2, column: -1, following: false), clearSelection: true)
    try require(grid.cursorRow == 0 && grid.cursorChannel == 0 && grid.column == 0, "Navigation clamps negative positions")
    grid.model = freshModel(rows: 0, channels: 2)
    let click = NSEvent.mouseEvent(with: .leftMouseDown, location: NSPoint(x: 120, y: 200), modifierFlags: [], timestamp: 0,
      windowNumber: 0, context: nil, eventNumber: 0, clickCount: 1, pressure: 1)!
    grid.mouseDown(with: click)
    grid.navigate(EditorNavigation(pattern: 0, row: 3, channel: 1, column: 0, following: false), clearSelection: true)
    key(grid, 0, "a", flags: .command)
    try require(grid.cursorRow == 0 && grid.cursorChannel >= 0 && grid.automationSelection["endRow"] == 0, "An empty pattern never produces a negative cursor or selection")

    // 10. Negative counts from the bridge cannot trap.
    let negative = PatternModel(["rows": -3, "channels": -2])
    grid.model = negative
    try require(negative.rows == 0 && negative.channels == 0 && grid.positionLabels.isEmpty, "Negative bridge dimensions are clamped")
    grid.model = freshModel(rows: 2, channels: 1)
    grid.cursorRow = 1
    var fallback: ((PatternModel) -> PatternView.Edits)?
    grid.onTransform = { fallback = $0 }
    grid.pasteText("Resonance Pattern 1\n31,01,00,00,00,00\n32,01,00,00,00,00")
    if let fallback {
      let shrunk = fallback(freshModel(rows: 0, channels: 0))
      try require(shrunk.isEmpty, "Legacy paste fallback tolerates a smaller target")
    } else { throw InterfaceFailure(message: "Legacy paste fallback was not dispatched") }

    // 9. The current clipboard format is parsed off the main thread and applied
    // only to the unchanged target.
    grid.model = freshModel(rows: 8, channels: 2, revision: "doc-a:7")
    grid.commandRevision = nil; grid.cursorRow = 2; grid.cursorChannel = 1
    var pastes = [[String: Any]](), messages = [String]()
    grid.onPaste = { pastes.append($0) }
    grid.onMessage = { messages.append($0) }
    func settle(_ done: () -> Bool) {
      let end = Date().addingTimeInterval(2)
      while !done() && Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
    }
    let clip = "ScreamSeq Pattern 2\n{\"rows\":1,\"channels\":1,\"cells\":[[49,1,0,0,0,0]],\"effects\":[],\"bindings\":[]}"
    grid.pasteText(clip, mode: "mix")
    try require(pastes.isEmpty, "Clipboard JSON is not parsed synchronously on the main thread")
    settle { !pastes.isEmpty }
    try require(pastes.count == 1 && pastes[0]["startRow"] as? Int == 2 && pastes[0]["startChannel"] as? Int == 1 &&
      pastes[0]["mode"] as? String == "mix" && pastes[0]["expectedRevision"] as? String == "doc-a:7" && pastes[0]["rows"] as? Int == 1,
      "Parsed clipboard pastes at the captured cursor with the captured revision")
    grid.pasteText(clip)
    grid.model = freshModel(rows: 8, channels: 2, revision: "doc-a:8")
    settle { messages.contains { $0.hasPrefix("Paste cancelled") } }
    try require(pastes.count == 1 && messages.contains { $0.hasPrefix("Paste cancelled") }, "A paste cannot land after its target changed")
    grid.pasteText("ScreamSeq Pattern 2\n{\"rows\":1,\"surprise\":true}")
    settle { messages.contains("Invalid pattern clipboard.") }
    try require(pastes.count == 1 && messages.contains("Invalid pattern clipboard."), "Malformed clipboard JSON is rejected")

    // 6. Shortcuts on the plus key round-trip through the stored format.
    for text in ["cmd+shift++", "ctrl++", "+", "cmd+g", "ctrl+opt+space"] {
      try require(WorkspaceStroke(text)?.encoded == text, "Shortcut \(text) round-trips")
    }
    try require(WorkspaceStroke("cmd+shift++")?.key == "+" && WorkspaceStroke("cmd+shift++")?.modifiers == [.command, .shift], "A trailing plus is the key")
    for text in ["cmd+", "cmd+shift+", "", "cmd++g", "cmd+cmd++", "bogus++"] {
      try require(WorkspaceStroke(text) == nil, "Malformed shortcut \(text) is rejected")
    }
    let palette = WorkspaceCommandPalette()
    let target = UnavailableCommandTarget()
    let item = NSMenuItem(title: "Guarded", action: #selector(UnavailableCommandTarget.runCommand(_:)), keyEquivalent: "")
    item.target = target
    palette.entries = [.init(item: item, path: "Test / Guarded")]
    let id = palette.entries[0].id
    try require(palette.setShortcut(id, keys: ["cmd+shift++"], persist: false) == nil && item.keyEquivalent == "+" &&
      item.keyEquivalentModifierMask == [.command, .shift], "The plus key can be bound")
    try require(palette.setShortcut(id, keys: ["?"], persist: false) != nil, "Unmodified keys stay reserved for note entry when binding")
    try require(palette.setShortcut(id, keys: ["?"], persist: false, allowUnmodified: true) == nil && item.keyEquivalent == "?" &&
      item.keyEquivalentModifierMask.intersection(WorkspaceStroke.mask).isEmpty, "An unmodified default can be restored")

    // 7. Palette and sequences honour menu validation.
    try require(!palette.isAvailable(item, from: nil), "A command its target rejects is unavailable")
    palette.sequences.bindings = [id: [WorkspaceStroke("ctrl+g")!, WorkspaceStroke("m")!]]
    palette.sequences.onRun = nil
    palette.entries = [.init(item: item, path: "Test / Guarded")]
    target.allowed = true
    try require(palette.isAvailable(item, from: nil), "A validated command is available")
    let orphan = NSMenuItem(title: "Orphan", action: #selector(UnavailableCommandTarget.runCommand(_:)), keyEquivalent: "")
    try require(!palette.isAvailable(orphan, from: nil), "A command nothing responds to is unavailable")

    // 8. An Apply result is always reported; newer draft text is preserved.
    let tools = PatternToolsPanel(frame: .zero)
    tools.onContext = { (freshModel(), ["startRow": 0, "endRow": 1, "startChannel": 0, "endChannel": 0, "cursorChannel": 0]) }
    var replies = [PatternToolsPanel.Reply]()
    tools.onRequest = { _, reply in replies.append(reply) }
    let success: [String: Any] = ["result": ["revision": "r1", "data": ["changedCells": 3, "changes": [[String: Any]]()]]]
    tools.preview(); replies.removeFirst()(success)
    tools.apply()
    tools.amount.stringValue = "7"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(["error": ["message": "The song changed; preview again."]])
    try require(tools.summary.stringValue.contains("The song changed") && tools.amount.stringValue == "7" && !tools.applyButton.isEnabled && tools.previewButton.isEnabled,
      "An Apply error is shown even when settings were edited in flight")
    tools.preview(); replies.removeFirst()(success)
    tools.apply()
    tools.amount.stringValue = "9"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(success)
    try require(tools.summary.stringValue.contains("Applied 3 cell changes") && tools.amount.stringValue == "9" && !tools.applyButton.isEnabled,
      "An Apply success is shown and the newer draft needs its own preview")
    tools.preview()
    tools.amount.stringValue = "5"; tools.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification))
    replies.removeFirst()(success)
    try require(!tools.applyButton.isEnabled && tools.summary.stringValue == "Preview a change before applying it.", "A stale preview still cannot be applied")
    print("PASS pattern grid: modifier-safe typing, busy key queue, audition release, repeat handling, clamped navigation, plus-key shortcuts, palette validation, apply reporting, background paste")
  }
}
