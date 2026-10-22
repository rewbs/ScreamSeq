# MIDI sound selection with a hidden context panel

## Observed failure

Computer Use inspected the owned `--inspection --automation` process PID 10228
of the qualified navigation package at source
`86538cb4226d74c257eefc86c88ceb1ee82cc1b9`. Executable SHA-256:
`318bb274d082efc31d83371d580454d5eac44372f73b44f67fb2d39a133d4098`.
The inspection opened Windows > MIDI recording, hid the context panel, reopened
MIDI recording and clicked Choose beside Sound. No usable sound selector appeared.
The process exited normally after the audit. No musician process or audio route
was changed. This was inspection mode, not an audible or timing qualification.

The relevant source was unchanged between that package and `e33349d30`:
`RecordingIntegration.inc` implemented Choose by focusing Main's `soundChooser`.
That control is hidden when the context panel is hidden or follows another editor.
The old route also moved editing away from the recording window unnecessarily.

The new private-desktop regression was executed once against that retained
executable. It failed at the exact missing-menu assertion after opening the window,
hiding the context panel and retaining an incomplete latency field. Receipts:
`bin/midi-sound-choice-20261022/baseline.json` and `baseline.log`.
Computer Use exposes floating tools as related UI and may reactivate Main on input;
the source inspection and separate native-menu regression corroborate this finding.
No claim of floating text-entry continuity is inferred from that tool behavior.

## Source correction, not yet qualified

Choose now opens a standard Windows menu anchored to its own button. Existing
samples or instruments use the same musical-input catalogue and stable identities
as Main's selector. The menu checks the current sound, supports normal native
keyboard navigation and escapes ampersands in user names. Empty songs show a
disabled explanation. It does not reveal the context panel or replace a workspace
view. This implements the connected-navigation/keyboard obligations in A01/A16
and reference rows 40, B05 and B10; it does not close MIDI hardware parity.

`MusicalTyping.inc` captures document, revision, sample/instrument mode and input
sound before the menu pumps messages. Choosing resolves the stable identity only
if that captured context still matches. Cursor/playback movement alone is allowed;
changed songs, revisions, input overrides or selections are rejected. Rechoosing
the current sound is a no-op. Main's close gate retains the owner during the native
menu. A valid choice updates future input and the recording label, leaving the
current recorded take, document history, saved project and raw MIDI settings alone.
No audio callback, recording clock, project format or musical editing API changes.
The existing `workspace.input` API remains the guarded nonvisual input selector.

## Bounded acceptance

- Existing `workspace-context-panel-tests` now checks hidden-sidebar selection,
  retained raw latency text/caret/focus, unchanged layout/music/take/cursor,
  stable-ID resolution, no-op selection, and busy/stale/missing-sound rejection.
- `test_midi_recording_ui.MidiRecordingUITests.test_sound_menu_with_hidden_context_keeps_layout_draft_and_history`
  uses the actual application's native popup, arrow/Enter selection, live input
  snapshot and song history. It also attempts Main close while the menu is active
  and changes the input through the guarded API during another popup, requiring
  rejection of the earlier selection. It reuses the existing private-desktop menu
  helper with a button-opening entry point. Existing Windows CI runs the class.
- The next hourly local checkpoint includes this case with the existing recording
  batch. No extra build is justified. The correction is Windows presentation/input
  context only; unchanged shared/Mac source can reuse the corresponding CI evidence.
- Foreground Computer Use must verify Choose from a hidden context panel and a
  different active editor, arrow/Enter and Escape, label update, retained MIDI
  settings, and unchanged workspace. This remains pending on the new executable.

Apply settings remains explicit here: changing MIDI source or disarming can finish
an active recorded take. The Mac reference also uses an explicit Apply for this
boundary (`mac/App/main.swift::midiSettings`). This correction does not silently turn
a connection/recording boundary into an automatic action on each typed character.
