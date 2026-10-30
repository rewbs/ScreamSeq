# Mac parity checkpoint from current main

This PR is a tested partial parity checkpoint, not a claim that every Windows
change has been ported. At the user's request, the work is being brought to a
reviewable stopping point. This checkpoint now includes the dynamic left context
pane; the separate left-pane session had not produced changes to integrate.

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

- A retained context pane following the focused editor, with editor-specific
  actions, pattern/order/input selectors and the complete sample catalogue. Toolbar,
  menu and Command–Option–B toggles reclaim width; named/last-used layouts retain
  visibility. Tracked menus retain captured identities, and malformed visibility
  rejects before changing panel placements.
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

Build 3 app source fingerprint: `dceb7bea65d6eb4d7a70ca1a1a3d8c6758570a2fb4aee6e1f9388f79ee775cf6`.
Build 3 executable SHA-256 before assigning the QA bundle identity: `ab20eb6426ff18101328ae4fdd78e28318e837c7ffb0a17dfc7708e40dbf8d40`.
The complete advertised read/write inventory matched the updated reviewed baseline.
A computer-use connection timed out during command-palette dismissal; the app
continued responding to its socket, and reconnecting the tool restored UI access.

Full builds are at least one hour apart, with computer-use validation after each.
Swift type checks and the interface harness are separate from full app builds.

## Context-pane checkpoint, 2026-10-30

Main was fetched again before this checkpoint; it remains `d1b52ade532d2d8fd9308a374ad61dd91ec7968c`.
The full build started at 05:19:04 UTC, more than 25 hours after build 3, and
completed successfully at 05:21:56 UTC. Bundle signature verification passed and
2,127 frozen source/test files were unchanged through that build.

- All 39 native interface groups passed, including new context-menu identity,
  retained-control, disabled-action, 80-sample catalogue and atomic layout tests.
- All six API inventory checks and the complete app socket suite passed.
  The focused Mac parity socket suite passed,
  including the new context snapshot and saved-layout visibility checks.
- CTest passed 137/138 tests in its first run. The real CoreMIDI loopback test
  reported `Receive all five MIDI events` while the GUI QA process was running.
  After closing that process, the isolated MIDI rerun passed. The initial failure
  remains recorded; concurrency is not established as its cause.
- Computer use in the separate `qa4` inspection bundle confirmed step changes in
  both selectors; hide/show reclaiming width; retained sample-name text, caret and
  focus across toggles; restored hidden-layout visibility; retained text after
  reopening a sample inspector hidden by layout restore; context Add group followed
  by Undo restoring the bus count; and Mixer-to-song-graph navigation updating the
  pane and focusing the graph. Readback confirmed presentation changes did not
  change song data.

Build 4 source fingerprint: `7858155009cd228810c9092f6dcbc5efc03cc765c75ccf0c193f8875f2af61d8`.
Executable SHA-256 before QA identity signing: `16cf6d00570f07fd7abf7debfc06f5af90acbfef109851380cbd8a42eefa972e`.
After those desktop checks, one additional line makes the toggle's accessibility
label follow its current Show/Hide action instead of the image's initial label.
The complete Swift application type-check passed after that change. It was not
followed by another full build inside the hour; the build fingerprint above
identifies the binary actually exercised. Reports and API documentation are also
updated separately from that binary. This remains bounded functional evidence,
not sustained presentation or physical-device qualification.

## Greptile review fixes and build 5 — 2026-10-30

The five initial findings are fixed in `b56687e22`: song-automation reads and
precise-note checks settle waiting Save/close/history actions; pattern and graph
curve fields retain ownership of their original point; Core Audio schema methods
are application-only; and song point edits preserve untouched native values and
exact frames. Regressions cover delayed success/failure, newer and invalid input,
mouse/keyboard selection, deletion, native range boundaries and precision.

Full build 5 began at 06:21:16 UTC, more than one hour after build 4, and completed
at 06:23:56 UTC from exactly `b56687e22`. All 3,521 tracked files were unchanged
through the build and the copied QA bundle's signature verified. Computer use in
`qa5` confirmed a time-only edit preserved the exact native value and second
point; invalid pattern fields survived a canvas click without changing selected
point ownership; correcting the value updated only point 0; and Command-S
completed on the disposable `QA5-Review.screamseq` project.

The next review reported a Tab-focus trap while curve fields were invalid. It was
reproduced with computer use in build 5 and then corrected: Tab/Shift-Tab leave
the canvas while point selection remains guarded. All 39 interface groups and a
complete application type-check pass after this correction. The native tests use
actual NSWindow key-view traversal in both curve editors. This last focus change
is not in build 5; no second full build was run inside the hour.

Build 5 source fingerprint: `2481d804877cebd20afac1d45a2f4c1a161ef7401557c75407b093266cb4d2b1`.
Executable SHA-256 before QA identity signing: `e3ff43777fa3763a792b074c1d099fafb4bcd026b92dfe1b0e2bbb0060cb42eb`.
The unpublished keyboard API work was set aside for this build and is not part
of this review checkpoint. Broader parity limits below remain unchanged.

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
