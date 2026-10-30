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

## Keyboard, native editor API and retained Song Settings — 2026-10-30

This source batch adds `workspace.keyboard.set`, the optional upper-octave C,
shared command-palette Play/Stop bindings (including disabled and multi-key
bindings), local text/menu/formula ownership, and guarded retained preference
fields. Held-note release retains the pitch, instrument and channel captured at
key-down after a mapping change. Preference edits leave song history unchanged.

`plugin.editor.open/close` now use the existing rack host. Strict admission,
revision guards, stable identity replies, repeated opening/closing and parameter
preview preservation are covered. Closing a stopped native editor drains its
accepted processor-input prefix before a later Undo; otherwise an older pending
VST3 editor value could overwrite that Undo. State capture is limited to a joined,
stopped processor and its returned effective state is not stored as the musical
baseline. A retained recording take bypasses this drain and keeps original input.

Song Settings is now a retained modeless window. Invalid/refused fields, hidden
window drafts, stale targets and text entered during pending reads or writes are
preserved. Requests contain only changed fields. Save/export/history wait for
pending and valid newer drafts; failures settle their waiting actions. An
uncertain write cannot replay until current state is read and explicitly accepted.

Validation before the next permitted full build:

- All 41 AppKit interface groups pass, including held-note release across mapping
  changes, invalid/stale settings, delayed completion with newer input, explicit
  uncertain-result review, failed initial reads and hidden-window retention.
- Complete Swift application type-check and six schema/inventory checks pass.
- The complete windowless socket suite passes against a freshly linked test host,
  including strict/stale rack-editor requests, preview retention, built-in editor
  refusal, no-op close and request replay. The app-only keyboard path awaits the
  next integrated application build.
- The native session and plugin CTest targets pass. Native fixture UI tests cover
  repeated windows, strict admission, unchanged previews, manual editing followed
  by close and one Undo restoring exact saved bytes.
- The native UI/BlackHole lifecycle suite passes with 512-frame buffers, including
  close during retained capture without changing original events, rejection of
  opening during the take, recovery, live Keep/Undo/Redo, stopped verification and
  save/reopen. The test verifies unchanged system output and buffer size. This is
  fixture evidence, not physical-device or commercial-plugin qualification.

Logs are under `bin/mac-parity-current/evidence/`: `interface-song-settings-final.log`,
`typecheck-song-settings-final.log`, `test-plugin-editor-final.log`,
`test-plugin-editor-flush.log`, `socket-plugin-editors-windowless-final.log`
and `api-inventory-keyboard-plugin.log`.
These additions are newer than full build 5. Application socket and computer-use
qualification of the integrated binary remain pending; the next full build cannot
start before 07:21:16 UTC under the one-build-per-hour limit.

### Follow-up read completion and recorded source navigation

The next Greptile review found that typing during the initial Song Settings read
left all other fields empty. Read completion now fills each untouched field while
preserving that field's newer generation or active composition. The regression
queues Save after an early title edit, then verifies that only the title is written
and the untouched fractional tempo, ticks and channel count are loaded unchanged.
A deliberately cleared numeric field stays invalid instead of being filled in.

Selecting a recorded point in Parameter Activity now establishes its stable
plugin/parameter source link, and clearing selection retires that link. This fixes
the previously nonfunctional Open source action for recorded table rows. A native
interface regression exercises selection, opening and deselection. Desktop
confirmation remains part of the next full-build qualification.

The subsequent review identified a source-ownership regression: a recorded-table
refresh could replace a newer frozen-trace selection. Refresh now updates a
source link only when the table owns it. Targeted native interface regressions
pass for no-row refreshes, trace selection during an outstanding read, removed
rows, recorded-origin trace points and table-owned time edits/reordering. The
full application build and desktop confirmation follow this source checkpoint.

### Full build 6 and integrated follow-up

Full build 6 started at 07:25:00 UTC on `b87371d8b`, more than an hour after
build 5. All 3,527 tracked files remained unchanged through the build; bundle
signature verification passed. The fingerprint is
`4d26ec8340a9597c4cb790604c0bdc9004e8e7ce79a135e6bbe219220fb86f9b`.
The complete application socket suite passes, including the rack editor API.

Computer use in the separately identified QA6 app verified populated Song
Settings, invalid-field refusal, hidden/reopened draft retention, Command-S
completion after correction and Undo restoring the original title/tempo.
Keyboard settings retain invalid mappings and accept the default optional top C.
The native-editor API opened Apple's AULowpass window visibly and closed it
without changing document data. An accessibility lookup timed out after hiding
Song Settings; focusing a workspace panel restored computer-use access while
the app continued responding through its API.

The integrated Mac parity socket test uncovered a separate Return-binding
collision with the graph's local Open plugin shortcut. The follow-up source fix
allows that local binding to coexist with Return transport, which already only
handles Return in PatternView; true global collisions still reject. Focused
keyboard interface regressions and full application Swift type-checking pass.
This fix is newer than build 6 and still needs integrated app qualification;
no second full build is permitted before 08:25:00 UTC. The failed integrated
receipt is retained as `socket-build6-mac-parity.log`.

Greptile's review of `b87371d8b` completed at 07:29:04 UTC: 73 files, zero new
comments, confidence 5/5, all eight threads resolved. The later Return-binding
fix requires a fresh review of its own commit.

## Explicit VST3 location repair follow-up — 2026-10-30

Added the six shared rack/graph `plugin.path.get/scan/set` operations, a private
verified module cache, and retained native Plugin location windows. Explicit
isolated scanning ties the matching class/role to the current executable SHA-256;
reconnect validates saved state before stopping playback and changing only the
selected stored path. Chronological Undo also preserves the old Windows hint
when automatic resolution had already found the same Mac module. Reads/scans/dry
runs do not drain unrelated native input in the application wrapper. AU identity
remains registered component identity, without format conversion.

The editor captures document/processor identity and a revision, retains newer
path input and candidate selection through asynchronous replies, and requires
explicit readback/acknowledgement after an unknown outcome. Closing/reopening
retains the draft. Scanning never silently chooses the first cached candidate.

Targeted validation before the next full build:

- Seven CTest groups passed: session, unified-history, signal-graph-session,
  plugin-trust, plugin-inventory, plugin-locations and plugin-paths.
- 42/42 native interface groups passed, including delayed plugin-location reads,
  stale writes, explicit uncertain-result review and changed-document ownership.
- Full Swift application type-checking and six API inventory checks passed.
- The complete existing windowless socket suite passed; the new focused socket
  path suite then passed real scanning, hash/refusal receipts, retained preview,
  dry run, exact rack/graph state preservation, no-op and chronological Undo/Redo.
- Direct native fixture tests also cover original hints, equal runtime paths,
  unknown-field preservation and native save/reopen. Cache tests cover changed
  executables/metadata, failed scans, canonical aliases and private cache files.

Evidence is under `bin/mac-parity-current/evidence/` (`*plugin-path*`,
`interface-plugin-path-all.log`, `test-plugin-path-validation.log`). Integrated
bundle, desktop and latest-commit Greptile qualification remain pending at this
source checkpoint; the preceding build 6 is not evidence for these changes.

## Build 7 desktop and review checkpoint — 2026-10-30

Full build 7 started at 08:29:21 UTC and completed at 08:33:05 UTC on
`6c981f6f3`, more than an hour after build 6. Its 3,534 tracked source files
remained unchanged; bundle signature verification passed. Source fingerprint:
`71534bcd3b24ae2faf622677af1af70a9f7854eaa77c23a6dd1091a51c330edf`.
Executable SHA-256:
`a317e61d0a6f2f61e19a508ca26158ee6948c2e99250bb5788eba0d76991f6e6`.

Computer use in unique QA bundle `org.resonance.tracker.qa.parity.20261030b7`
verified missing Windows rack and graph paths, refused scan draft retention,
explicit scan/candidate choice, dry check, reconnect, retained text after
close/reopen, and main-window Command-Z/Command-Shift-Z restoring the old/new
rack paths. Both repairs survived saving a separate song and reopening with the
native Open dialog. The app showed no missing-plugin error after reopening.

The integrated Mac parity socket checks now pass unchanged, including the
Return binding alongside the graph's local binding. Real keyboard Return started
and stopped the QA song on explicitly selected BlackHole 2ch at 48 kHz / 128
frames. QA keyboard, loop and output settings were restored, and the disposable
app was closed. System audio defaults were unchanged. No sustained performance
or commercial-plugin qualification is claimed. After workspace preset checks,
the main window was compressed; native Zoom restored its visible graph layout,
and Pattern focus presented the grid. Broader workspace geometry remains open.

The application socket suite exposed a preflight receipt lost by the final
reply path. Greptile independently reported the same issue. `c1d609f51` forwards
the already-prepared response, preserving `writeOutcome: notCommitted`, and the
review thread is resolved. Complete Swift type-checking passes. This fix is
**newer than build 7**, so the failed receipt assertion remains recorded in
`socket-build7-app.log`; it is not reported as a passing full app suite. The
existing suite passed through request-replay checks before that assertion.
Direct-session and windowless path tests cover the correct receipt and remaining
path contracts. Another full build cannot start before 09:29:21 UTC.

Long candidate paths also now compress/truncate within the location inspector.
A native interface regression with a 1,200-character path passes without forcing
the initial window wider, alongside the retained-draft tests. Complete Swift
application type-checking passes; this presentation follow-up is also newer than
the packaged binary.

Receipts: `build-7-identity.json`, `qa7-desktop-validation.json`,
`socket-build7-mac-parity.log`, `typecheck-path-receipt.log`,
`interface-plugin-location-width.log`, and `typecheck-plugin-path-width.log` in
`bin/mac-parity-current/evidence/`.

## Retained Mixer fields and location revisions — 2026-10-30

The Windows `NativeCommandApplicationTests`, `AutomaticEditsApplicationTests`
and `DraftImportRetentionTests` exposed additional Mac ownership gaps. Mixer
Details now retains invalid name/color/timing and numeric text, output choices,
and mute/solo intent; valid changed fields submit as one guarded transaction after
an idle delay or Return. Unchanged rounded fields are omitted so they cannot alter
native precision. Invalid fields block bus changes and structural commands.

Strip gain text belongs to its stable bus even when scrolled out of view. It is
not recycled or restored to an old number on validation failure. Explicit Reload
reads accepted state before releasing retained work and cancels audition only for
supported numeric/boolean controls. Newer text, input-method composition and
old-document completions cannot be erased or authorized by delayed responses.

Mixer completion now participates in Save, Save As, export, document history,
document departure and Quit. Pending previews finish as one durable gesture;
failed reads/writes settle waiting actions without proceeding. Send forms retain
their explicit Set-send behavior and block departure while unfinished. Adding a
bus opens its stable identity in Details and focuses its name.

Plugin location read, scan and reconnect completions also retain the older
revision when newer text or candidate choices arrive. Interrupted scans require
readback; a reviewed result cannot authorize fields changed after that read.
These checks apply to both rack and graph location windows.

Validation: all 42 native interface groups passed, followed by the focused Mixer
checks after the two-stage Reload revision fix. Whole-application Swift type
checking passed. The complete windowless socket suite passed, including a new
name/color batch check with exact untouched gain, one-step Undo/Redo and saved
native metadata. Native tests cover failed reads/writes, newer inputs during
write/read/preview release, retained strip recycling, composition and completion
of waiting Save actions. Logs: `interface-mixer-location-all.log`,
`interface-mixer-raw.log`, `typecheck-mixer-location.log` and
`socket-mixer-raw-windowless.log` under the evidence directory.

This source follow-up is newer than build 7. It does not claim packaged desktop
qualification until the next hourly build and computer-use pass. The shared
compact graph level control retains its existing transaction behavior; its
broader refusal/retention audit remains open.

## Remaining parity work

These items are deliberately outside this checkpoint and must be resolved before
claiming complete Windows parity:

- Packaged-app confirmation of the plugin-location refusal receipt and long-path
  layout follow-ups newer than build 7.
- Integrated application/socket/computer-use qualification of the newly added
  keyboard settings, rack editor APIs and retained Song Settings.
- Additional composing departure, panel-close and newer-input guards in other
  editors.
- Packaged qualification of the retained Mixer fields and location-revision
  changes above; sample-setting completion and retained refused waveform strokes.
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
