# Mac parity checkpoint from current main

This PR is a tested partial parity checkpoint, not a claim that every Windows
change has been ported. At the user's request, the work is being brought to a
reviewable stopping point. The dynamic left menu is owned by another session and
is outside this PR.

## Base and scope

The task restarted from freshly fetched main `4885bb9997393bcf072639358da3a900a20efb01`.
The former attempt based on `ffe81aa4b` remains preserved in `0adfbb0db`; its test
results do not qualify this branch. Before raising this PR, main was fetched again
and the branch rebased onto `d1b52ade532d2d8fd9308a374ad61dd91ec7968c`, which includes
PR #8's shared advanced gate implementation and Mac inspector. Both the Gate
inspector and independent Graph Curve remain registered and tested.

The initial inventory covered 590 commits after the former baseline, including
384 touching `windows/`. Those counts describe review scope, not completion.
The primary checkout and other sessions' worktrees were not reset or edited.
Native AppKit, Core Audio and CoreMIDI behavior remains platform-specific; musical
editing continues through the existing shared model, APIs, Undo and persistence.

## Implemented in this checkpoint

- Named workspace layouts, retained layout management, Connected and Graph editing
  presets, categorized command search and keyboard traversal.
- Independent pinned/dockable Graph Curve with staged target capture and draft
  retention; arrangement event counts, page navigation and captured annotations.
- Guarded MIDI settings API, retained MIDI capture review, immediate Arm using
  accepted settings, source/loss handling and retained busy intent.
- Prepared live precise-note publication and Undo/Redo without restarting transport;
  bounded refusal, improved hit selection and ordered Save/history completion.
- Retained song-time parameter automation editor using the shared 48 kHz timeline,
  exact native values, automatic editing, Undo/Redo, search, zoom and source links.
- Parameter Activity search that preserves the watched target, frozen trace
  keyboard navigation, retained recorded-point identity and automatic editing.
  Navigation, paging and explicit refresh finish pending point writes first.
- Ordered composing completion across precise notes, pattern/graph curves,
  instruments and recorded automation before native Save/export/history.
- Captured instrument edit ownership, newer-text/keymap retention, Carry and
  Release Node controls, and unchanged instrument edits preserving Redo.
- Retained Core Audio settings with separate revision guards, stable device UIDs,
  rescan retention and negotiated rate/buffer readback. A candidate output is
  prepared before the previous output is stopped. No system default is changed.

## Qualification

All evidence below is local under `bin/mac-parity-current/evidence/`; build products,
user songs, screenshots and private runtime files are excluded from the PR.

| Checkpoint | Evidence |
| --- | --- |
| Full build 1, 2026-10-28 07:17:23 UTC | Build passed; 135/135 CTest tests and 38/38 interface groups passed; complete app socket and parity socket suites passed. |
| Full build 1 computer use | MIDI raw text retained through Arm; named layouts and independent Graph Curve; precise note edit and Undo during looped playback with zero reported overruns. |
| Full build 2, 2026-10-28 12:37:21 UTC | Build passed with unchanged frozen source hashes; 135/135 CTest tests, 38/38 interface groups, app socket and parity socket suites passed. Live precise-note publication/history/refusal test passed on explicitly selected BlackHole 2ch. |
| Full build 2 computer use | Audio refresh retained the selected endpoint and switch reported 48 kHz/128 frames; song automation invalid-input retention, Undo/Redo and Command-S; instrument Carry/Release Node and bank handoff; Activity point relocation, saved-field completion, search, freeze, trace keyboard navigation and source opening. |
| Full build 3, 2026-10-29 03:56:26 UTC, rebased on current main | Build and signature verification passed with 2,159 frozen source/test files unchanged; 138/138 CTest tests, 39/39 interface groups, 6/6 API inventory checks, app socket and Mac parity socket suites passed. The live precise-note publication/history/refusal test passed on BlackHole 2ch. |
| Full build 3 computer use | Song automation numeric edit and immediate Command-S updated both the timeline accessibility value and saved project. Recorded-point raw text remained visible after invalid input and refresh; the corrected value was confirmed by API readback. Command search opened the Gate inspector alongside Graph Curve and Parameter Activity. |

Saved-project and API readback confirmed the UI edits, including a recorded point
at frame 12000 with native value -7. The first build-2 QA process ended during a
pause in testing, without a crash report; history/persistence tests were repeated
in a newly launched process with verified inspection flags. All test apps used
separate bundle identities and disposable songs. No musician process was replaced.
The GUI observations do not establish sustained 60 fps or physical-device latency.

Final app source fingerprint: `dceb7bea65d6eb4d7a70ca1a1a3d8c6758570a2fb4aee6e1f9388f79ee775cf6`.
Executable SHA-256 before assigning the QA bundle identity: `ab20eb6426ff18101328ae4fdd78e28318e837c7ffb0a17dfc7708e40dbf8d40`.
The complete advertised read/write inventory matched the updated reviewed baseline.
A computer-use connection timed out during command-palette dismissal; the app
continued responding to its socket, and reconnecting the tool restored UI access.

Full builds are at least one hour apart, with computer-use validation after each.
Swift type checks and the interface harness are separate from full app builds.

## Remaining parity work

These items are deliberately outside this checkpoint and must be resolved before
claiming complete Windows parity:

- Explicit rack/graph plugin path scan and reconnect with module-hash guards and
  Undo. Mac's trusted automatic class resolution is not an equivalent workflow.
- Rack plugin-editor open/close API and guarded musical-keyboard settings API,
  including optional upper-octave C and custom Play/Stop binding integration.
- Retained Song Settings form after invalid/refused edits; additional composing
  departure, panel-close and newer-input guards.
- Mixer raw-field retention and completion, sample-setting completion and retained
  refused waveform strokes.
- Remaining recorder/review focus, root keyboard traversal, paging and endpoint
  duplicate-name/disconnection details.
- Further source-by-source review of plugin/routing controls, instrument-envelope
  navigation, recorded-point-row source links, accessibility and precise-note
  navigation/cursor behavior.
- Final integrated startup, workspace, recovery, long-export, reopening and
  sustained presentation qualification across the remaining workflows.

Sample recording already offers the same 60- and 300-second duration choices as
Windows. The Mac API already separates request input timeout from a 120-second
operation wait; the bundled client defaults to 120 seconds. Those source findings
avoid unnecessary ports but do not replace runtime checks of long exports.
