import AppKit

extension AppController {
  func focusedView(_ event: NSEvent) -> NSView? {
    (event.window ?? NSApp.keyWindow)?.firstResponder as? NSView
  }
  func handlePlaybackKey(_ event: NSEvent) -> Bool {
    guard NSApp.isActive, event.type == .keyDown,
      event.keyCode == 49 || event.keyCode == KeyboardSettings.transportKey,
      commandPalette.window?.isKeyWindow != true else { return false }
    let modifiers = event.modifierFlags.intersection([.command, .control, .option, .shift])
    guard !modifiers.contains(.command), !modifiers.contains(.option) else { return false }
    let focus = focusedView(event)
    if focus is FormulaCodeView, modifiers == [.control], event.keyCode == 49 { return false }
    // These responders have an actual local binding: text insertion, sample preview,
    // control activation, or Return's default action. Modified Space remains global.
    if !modifiers.contains(.control) {
      if let text = focus as? NSTextView, text.isEditable { return false }
      if focus is NSTextField { return false }
      if modifiers.isEmpty && (focus is SampleBrowserTable || focus is NSButton || focus is NSPopUpButton) { return false }
      if event.keyCode == 36 && !(focus is PatternView) { return false }
    }
    guard !event.isARepeat else { return true }
    if modifiers.isEmpty { togglePlayback() }
    else { startPlayback(fromCursor: modifiers.contains(.shift), bounded: modifiers.contains(.control)) }
    return true
  }
  func handleInspectorNote(_ event: NSEvent) -> Bool {
    // A key release belongs to the original asset, even after focus/selection changes.
    if event.type == .keyUp, let held = inspectorHeldKeys.removeValue(forKey: event.keyCode) {
      sendInspectorNote(note: held.note, sample: held.sample, instrument: held.instrument, on: false)
      return true
    }
    guard NSApp.isActive, event.type == .keyDown,
      event.modifierFlags.intersection([.command,.control,.option]).isEmpty,
      let key = event.charactersIgnoringModifiers?.lowercased(), let offset = KeyboardSettings.note(for:key),
      let focus = focusedView(event) else { return false }
    if let text = focus as? NSTextView, text.isEditable { return false }
    if focus is NSTextField || focus is NSPopUpButton { return false }
    let sample = focus === sampleEditor || focus.isDescendant(of:sampleEditor)
    let instrument = focus === instrumentEditor || focus.isDescendant(of:instrumentEditor)
    guard sample || instrument else { return false }
    if !event.isARepeat && inspectorHeldKeys[event.keyCode] == nil {
      let held = (note: min(120, patternView.octave * 12 + offset + 1), sample: sample ? sampleEditor.index : 0, instrument: instrument ? instrumentEditor.index : 0)
      inspectorHeldKeys[event.keyCode] = held
      sendInspectorNote(note:held.note, sample:held.sample, instrument:held.instrument, on:true)
    }
    return true
  }
  func sendInspectorNote(note:Int, sample:Int, instrument:Int, on:Bool) {
    inspectorPendingNotes.append((note,sample,instrument,on)); drainInspectorNotes()
  }
  func drainInspectorNotes() {
    guard !busy, !inspectorPendingNotes.isEmpty else { return }
    let pending = inspectorPendingNotes; inspectorPendingNotes.removeAll()
    for event in pending {
      let accepted = event.sample > 0
        ? session.sampleNote(event.note, sample:event.sample, velocity:100, on:event.on)
        : session.note(event.note, instrument:event.instrument, velocity:100, on:event.on)
      if !accepted { inspectorPendingNotes.append(event) }
    }
    if !inspectorPendingNotes.isEmpty {
      perform("Preparing inspector audition…", refresh:false, { try self.session.prepareAudition() }, completion: {
        // Invalid/deleted assets must not cause an endless prepare loop.
        let waiting = self.inspectorPendingNotes; self.inspectorPendingNotes.removeAll()
        for e in waiting {
          if e.sample > 0 { _ = self.session.sampleNote(e.note,sample:e.sample,velocity:100,on:e.on) }
          else { _ = self.session.note(e.note,instrument:e.instrument,velocity:100,on:e.on) }
        }
      })
    }
  }
  func releaseInspectorKeys() {
    let held = inspectorHeldKeys; inspectorHeldKeys.removeAll()
    for (_,e) in held { sendInspectorNote(note:e.note,sample:e.sample,instrument:e.instrument,on:false) }
  }
}
