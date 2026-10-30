import AppKit

extension AppController: TrackerDocumentDepartureDelegate {
  // This census is read on the main thread at adoption, after file preparation.
  // Include hidden retained panels; visibility is not permission to lose work.
  // It does not apply, rebase, end editing, or clear any owner's state.
  func retainedNativeEditorWork() -> [String] {
    var work = [String]()
    func retain(_ name: String, _ retained: Bool) { if retained { work.append(name) } }
    retain("Song settings", songSettingsWindow?.editor.needsCompletion == true)
    retain("Mixer", mixerEditor.needsCompletion)
    retain("Samples", sampleEditor.hasDraft)
    retain("Instruments", instrumentEditor.hasDraft)
    retain("Precise notes", workspaceNotes.hasDraft || workspaceNotes.pending)
    retain("Pattern automation", workspaceAutomation.hasDraft || workspaceAutomation.loading)
    retain("Gate inspector", gateInspector.needsCompletion)
    retain("In-grid command", patternView.nudgeEditor?.needsCompletion == true)
    retain("Queued pattern input", patternView.deferredKeyCount > 0)
    retain("Graph", signalGraphEditor.hasDraft || signalGraphEditor.loading || !signalGraphEditor.pendingMoves.isEmpty)
    let curve = signalGraphEditor.envelopeEditor
    retain("Graph curve", curve.hasDraft || curve.loading)
    retain("Graph commands", graphCommandsEditor.hasDraft || graphCommandsEditor.pending)
    retain("Recorded parameter points", parameterActivity.needsCompletion)
    retain("Song automation", songAutomationWindow?.editor.needsCompletion == true)
    retain("Plugin automation take", pluginRecordingInput.protectsDocument)
    retain("Plugin parameter gesture", pluginParameterGestureActive || pluginManualGestureApplied || pluginEditor.hasDeferredParameterEdits)
    retain("Note recording", recordingTakeID != nil || recordingFinishing)
    if let recorder = sampleRecordingWindow?.contentView as? SampleRecordingView {
      retain("Sample recording", recorder.needsDocumentCompletion)
    }
    retain("Selection render options", patternSampleRender?.needsDocumentCompletion == true)
    retain("Selection render result", directSampleRender?.needsDocumentCompletion == true)
    if let scratch = scratchGestureWindow?.contentView as? ScratchGestureEditor {
      retain("Scratch phrase", scratch.hasPendingEdits || scratch.pending)
    }
    for location in pluginLocationWindows.values {
      retain("Plugin location", location.editor.pending || location.editor.uncertain)
    }
    // Expanded formula and template windows have their own drafts, independent
    // of their parent curve's accepted state. Closing them is their discard UI.
    for nativeWindow in NSApp.windows where nativeWindow.isVisible {
      if let formula = nativeWindow.windowController as? FormulaWorkbench {
        retain("Expanded formula", formula.hasDraft)
      }
      if let bank = nativeWindow.windowController as? EnvelopeBankWindow {
        retain("Envelope template", bank.dirty || bank.pending)
      }
      retain("Text composition", (nativeWindow.firstResponder as? NSTextView)?.hasMarkedText() == true)
    }
    return work
  }
  func documentReplacementWillBegin() { documentDepartureInput.begin() }
  func documentReplacementDidFinish(_ adopted: Bool) { documentDepartureInput.finish(adopted: adopted) }
}
