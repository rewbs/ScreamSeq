# Windows continuation

Latest arrangement/timing checkpoint (2026-10-07): native Arrange orders
and Tempo and groove tools are connected to the shared guarded worker. Stable
occurrence selection, sequence switching, pattern creation/duplication, explicit
selected-order playback and normalized timing preview are implemented. Drafts,
focus and scroll survive refreshes and rejected requests. See
[ARRANGEMENT_PROGRESS.md](ARRANGEMENT_PROGRESS.md) for exact scope and evidence.
Candidate 6 passes **367/367 application tests** in **898.856 seconds** with
strict outer isolation, **8/8 focused cases**, **42/42 native CTests** and a
separate **11/11 document-operation groups** with six exact offline PCM
comparisons. Fifteen source-matched 192-DPI views pass review. The shared
Duplicate operation now preserves exact pattern timing, engine name/color and
native musical metadata, including resized duplicates and native persistence.
SHA256: `83E06DAB17094A66B79C4D971C536CBC4A7BFE879BAB2D15ACF6302D38C13EB3`.
Checkpoint: `bin/windows-checkpoints/arrangement-20261007/`; its manifest is the
authority for packaging completion. The report retains earlier production,
fixture and foreground-isolation failures without relabeling them as passes.

The next implementation scope is [sections and annotations](ANNOTATION_PLAN.md):
per-occurrence sections, shared pattern details, section navigation, guarded
annotation APIs and native persistence. The arrangement matrix and independent
dock groups remain later UI work, with source-backed plans in
[MATRIX_PLAN.md](MATRIX_PLAN.md) and
[INDEPENDENT_DOCKING_PLAN.md](INDEPENDENT_DOCKING_PLAN.md).
The Windows/Mac parity goal stays open.

Previous recording checkpoint (2026-10-07): timestamped WinMM input, correlated
WASAPI presentation history and worker-owned precise-note takes are connected to
a retained native MIDI/recording window. Shared one-Undo commit, native persistence,
copy-only live recovery and stopped imported-take review are implemented;
Save/Open/close guard unfinished takes.

Qualified ARM64 candidate 5 SHA256:
`DEAA2252608550EBD6B309A8426AF68862093465B7BD1DE05BC1C0F2F1FD98AF`.
The final full application suite passes **359/359** in **875.469 seconds**, no
failures or skips, with strict outer isolation (`bin/windows-recording-final-rerun2-app-tests.log`,
`-isolation.log`, `-exe-sha256.txt`). Focused cases pass **15/15** in 16.046 seconds;
native tests pass **40/40** in 18.41 seconds. Eight source-matched rendering/native
control views pass review at 192 DPI. See [RECORDING_PROGRESS.md](RECORDING_PROGRESS.md)
for exact logs, scoped visual limits and retained failed-run chronology, including
the unreproduced Stop interleaving and unattributed historical desktop resources.

Checkpoint location: `bin/windows-checkpoints/recording-20261007/`; the package
manifest is the authority for packaging completion. Physical MIDI, reciprocal Mac
runtime, foreground/multiple-scale review, accessibility and independent dock
groups remain open; this functional checkpoint does not complete the parity goal.

Its original next-step [arrangement plan](ARRANGEMENT_PLAN.md) remains a
historical source audit. Current implementation and qualification are above.

Previous recovery pass (2026-10-07): `RECOVERY_PROGRESS.md` records ten-second
immutable autosave, ten retained generations per session, a native recovery
browser and revision-guarded recovery APIs. Restore protects current unsaved work,
validates before replacement and opens a pathless dirty document. Manual plugin
state and imported unfinished takes survive snapshots without modifying the live
song. Its historical recording/take gap is addressed by the current slice above.

The final candidate passes **345/345 application tests** in 890.030 seconds,
**14/14 focused recovery cases** and a separate **1/1 vendor-input regression**,
all with successful strict outer isolation. The diagnostic native suite passes
**38/38**; its earlier 37/38 desktop-teardown failure remains unexplained and
preserved, with no production fix claimed. Eight final renderer/native-control
views are reviewed at 192 DPI. See the recovery report for exact executable
identity, logs, historical failures and visual limits. The committed package is
`bin/windows-checkpoints/recovery-20261007/`.
Its original next-step plan is retained in `RECORDING_PLAN.md`; current recording
implementation and remaining qualification are in `RECORDING_PROGRESS.md`.

Previous sample UI pass (2026-10-07): `SAMPLE_WORKFLOWS_PROGRESS.md` records retained
Drawing, Process, Loops, Paste and Snap pages, independent normal/sustain drafts
with joint preview/Apply, reviewed paste with rate/gain options, and selection
or loop snapping with grid origin and automatic selection. Shared musical
operations, Undo and native storage are reused. The minimum client is 900×720
DIPs; pending loop bounds appear as dashed waveform markers.

Candidate 3 passes the complete **330/330 application tests** with no failures
or skips in 835.843 seconds and a successful strict outer isolation check, plus
**32/32 affected UI tests** and **36/36 native CTests**. Its **19
renderer/native-control views** are reviewed with exact
source/binary identity. The full-suite log is
`bin/windows-sample-workflows-final-app-tests.log`.
An additional isolated probe reproduced native-focus loss after loop/paste
Preview/Apply. Candidate 4 contains that correction and page-specific menu
hints; all **33/33 individual affected UI tests pass** in 194.613 seconds, but
**the outer run fails strict foreground isolation with exit 1**. The foreground
change is unattributed, so final-build isolation qualification remains unresolved.
Its **19 refreshed rendering views** are reviewed and pass their own isolation
checks; those do not qualify the failed application run. The final UI log is
`bin/windows-sample-workflows-candidate4-ui-final.log`. The full-suite and native
CTest results remain tied to the retained `bin/ScreamSeq-sample-candidate3.exe`
baseline; the report records both exact executable hashes. It also retains the
first candidate's 31/32 failure and correction, and the final candidate's initial
32/33 result caused by a mistaken test expectation about native button focus.
Both candidate-4 outer isolation failures are preserved:
`bin/windows-sample-workflows-candidate4-isolation-final.log` and
`bin/windows-sample-workflows-candidate4-outer-isolation-failure.log`.
No reactivation or further equality-chasing test run was attempted.
The later recovery checkpoint above supersedes this checkpoint's next-step plan;
its historical isolation failure remains preserved.

## Previous checkpoints

The command/menu pass (2026-10-07): `WORKSPACE_COMMANDS_PROGRESS.md` records native
context menus for pattern, sample, graph, instrument and detailed sample editors,
plus a 99-command palette with configurable shortcuts/sequences, atomic saved
preferences, Clear/Reset and Reload. Native text and local editor input retain
priority; captured menu targets reject stale actions. Full Mac parity remains
active. The following sample workflow checkpoint connects its existing backend
capabilities to the retained UI described above.

The command checkpoint's full baseline passes **324/324 application tests** with
no failures or skips. Final palette-only conflict feedback passes **9/9 focused
app tests** and **35/35 native CTests**. The report distinguishes the full-run
and final-build hashes and records the earlier regressions and their fixes.
The preserved ARM64 package is
`bin/windows-checkpoints/workspace-commands-20261007/`.

The preceding UI pass (2026-10-07): `WORKSPACE_DOCKING_PROGRESS.md` records retained
automation/instrument docking, responsive workspace tabs and compact editor
pages. `WORKSPACE_LAYOUTS_PROGRESS.md` covers the persistent lower editor tabs,
named layout manager, command-palette search/focus improvements and visual
corrections. The separate build is `bin/windows-ui-parity/`. Current verification
and remaining limitations are in those reports. Full Mac parity remains active.

Qualification: the docking baseline passed all 298 application tests and 34
native CTests. The final build, including focus corrections and visual copy,
passed all 49 affected UI tests with no skips. Its 24 renderer/native-control
compositions are reviewed; foreground and sustained presentation gates remain
open. The report distinguishes the full-run and final-build fingerprints.
The preserved ARM64 package is
`bin/windows-checkpoints/workspace-docking-20261007/`.

Latest user-directed refinement: `FLAT_CONTROLS_PROGRESS.md` removes unrelated
control redraws during cursor movement and gives native selectors a flat style.
The Mac and Windows frontends coexist; Mac build/runtime qualification remains
outstanding. Full parity remains active.
All 268 isolated application tests pass with no failures or skips, alongside
31 native CTests. The package is `bin/windows-checkpoints/flat-controls-20260922/`;
the musician's previous process was left running.

Previous continuation: `AUDIO_SETTINGS_PROGRESS.md` adds native output-device and
buffer selection plus independent settings APIs. The six output tests and eleven
library tests pass together, as do 30 primary CTests and silent device tests. All
**264 application tests pass**, no failures or skips, in 671.289 seconds, with
strict desktop isolation. The package is
`bin/windows-checkpoints/audio-settings-20260922/`. Full parity remains active.

Previous continuation: `SAMPLE_LIBRARY_PROGRESS.md` adds the native library/browser,
independent preview and guarded family import. All 257 application tests pass
with no failures or skips, as do 30 primary native CTests, two index/worker tests
and two adapter tests. The preserved package is
`bin/windows-checkpoints/sample-library-20260922/`. Full parity remains active.

Previous continuation: `DEFERRED_VIEWS_PROGRESS.md` fixes lost native view requests
during worker activity, preserves captured targets and isolates legacy app tests
on a private desktop. All 247 application tests and 29 native CTests pass.
Sample library/browser/preview work follows; full parity remains active.

Previous continuation: `SURGE_RESTART_PROGRESS.md` identifies the post-Undo Surge
audition failure as rejection of a parameter-title notification and adds safe
control-thread catalog refresh plus first-failure diagnostics. Full qualification
is recorded there; full parity remains active.

Previous continuation: `INSTRUMENT_IMPORT_PROGRESS.md` records native guarded
instrument import, selected imported sounds and a keymap list alongside the
envelope. Range fields retain their captured draft through selection and Close.

Previous continuation: `SAMPLE_SETTINGS_PROGRESS.md` records native sample settings,
batch imports, captured replacement and sample-instrument creation, plus a shared
fix preserving omitted tuning, volume, panning and loop settings. Full parity remains active.

Previous continuation: `MUSICAL_TYPING_PROGRESS.md` records selected-sound pattern
entry, sample/instrument musical typing, main-workspace Live keys and voice
ownership through focus changes and worker waits. The final build passes all
29 native tests and 17 focused audition/typing tests. Its full application run
has 227 passes and one routing-window visibility failure out of 228 cases;
the routing module passes separately, with the intermittent failure retained
as unresolved. The preserved package is
`bin/windows-checkpoints/musical-typing-20260921/`. Full parity remains active.

Previous continuation: `AUDITION_PROGRESS.md` records the native sample/instrument
piano, shared audition API, preview cut/retrigger corrections, detailed sample
voice markers and idle presentation behavior. All 29 native CTests pass. The
full app run covers 221 cases (220 passes, one formula-test worker-busy error);
the test's readiness wait is corrected and all nine formula cases pass on rerun.
The exact results are retained in the report and preserved development package
`bin/windows-checkpoints/audition-20260921/`. Full parity remains active.

Previous editor continuation: `SAMPLE_DETAIL_PROGRESS.md` records the native
detailed waveform, drawing, processing, crossfade, snapping and clipboard
controls. All 211 application tests pass with no failures or skips; the preserved
build is `bin/windows-checkpoints/sample-detail-20260921/`.
`ABSOLUTE_AUTOMATION_PROGRESS.md` records the
Mac-compatible absolute lane API and native song automation editor, with
plugin history and bounded dense-lane drawing. All 199 actual-app tests pass
with zero failures or skips; the preserved build is
`bin/windows-checkpoints/absolute-automation-20260921/`.
`INSTRUMENT_ENVELOPE_PROGRESS.md` records native
instrument curves, settings/keymaps, transforms, bank integration and playback
markers, with 187 actual-app tests passing.
`PARAMETER_AUTOMATION_PROGRESS.md` records native
pattern parameter curves, the shared transform API and connected envelope bank
and formula editing. `GRAPH_COMMANDS_PROGRESS.md` records the preceding native
row-aligned graph lanes and installed-plugin command rendering. Native song routing,
plugin library/presets/location repair, graph curves and precise-note milestones
are linked from `PAUSED_HANDOFF.md`. The broader work and installed-plugin
limitations remain in `PARITY_PLAN.md`.

## Latest: upstream integration and installed plugins

The default upstream branch is `codex/screamseq` (there is no `main`). Changes
through `bcfe0f8a7` are integrated after preserving prior Windows work in local
checkpoint `40d0f074c`. The updated execution order and completion gates are in
[PARITY_PLAN.md](PARITY_PLAN.md); current phase-specific qualification, plugin
versions/hashes and the retained OrbitCab offline failure are in
[UPSTREAM_PLUGIN_QUALIFICATION.md](UPSTREAM_PLUGIN_QUALIFICATION.md).

New behavior includes project 6 / metadata 17, shared unified FX playback before
cursor seeking, dynamic VST3 latency maintenance and actual sample voice markers.
Contourtonist and OrbitCab ARM64 bundles are installed in a dedicated per-user
QA folder. Both pass lifecycle/editor and short silent WASAPI checks; only
Contourtonist passes the application's offline partition gate. Full native
plugin workflow, other editor families and final cross-platform/performance
qualification remain open.

The original Mac reference is now a historical format and is rejected by the
current reader. Do not use the instructions/results below as current-format
proof; the new qualification report explicitly documents its synthetic fixture.
The remainder of this file records the earlier continuation checkpoint.

## Historical continuation before this upstream merge

The user explicitly resumed the port and enabled Full access, which is active. Preserve all prior uncommitted/untracked work and the historical PAUSED_HANDOFF.md artifacts. Windows/Mac parity remains unfinished; this is a development checkpoint.

## Implemented in this continuation

- Worker-owned AssetOperations, revision guards, persistent private sample clipboard, atomic document replacement, native persistence and Undo/Redo. Imports validate actual preserved plugin aliases, adapter capacity and cache growth. Integer-valued JSON slots cannot bypass ownership checks.
- Explicit waveform invalidation for PCM changes/imports/history. Unrelated patterns/waveforms share immutable buffers; loop-only edits retain their PCM overview. Snapshot retirement stays on the document worker.
- Native sample editor: waveform selection, exact frame fields, Reverse, Normalize, Fade, Trim, normal/sustain loops, forward/ping-pong/reverse directions, private copy/cut/paste and import. All commands are in the palette, including loop controls when the inspector is short.
- Selections use stable sample IDs; drafts capture target/revision. External changes cannot silently retarget Apply. Escape cancels drags/drafts. New document identities clear presentation state.
- Dark native title/list/combo presentation, short-sample waveform drawing, initialized pattern/order choosers and keyboard-focus handling.
- Retained DirectWrite layouts (4096 maximum) and event-driven idle rendering. Read-only operations with an unchanged view no longer trigger layout.
- 32 MiB framed requests, matching response/Mac byte bounds. Newline detection scans chunks once. Exact-limit and one-byte-over tests use a real pipe.
- Shared Windows/Mac graph-envelope replacement preserves unrelated ordering, history, Redo and playback for identical get/set.
- Envelope preflight counts the complete candidate before excluding pending instrument links for materialization validation. Exact 16 MiB and one-byte-over cases are tested.
- Shared hosted playback integrated with the app and WASAPI. The worker prepares/owns/retires it; UI/audio readers drop pointers after device join and before replacement. Callback requests above 4096 frames are split; processor faults silence output. AU/unresolved recipes reject without a dry substitute.
- VST3 scanner and notices build beside the app. Plugin discovery/editor/state-editing frontend is still pending.
- build.ps1 -Fresh and poisoned-empty-flag detection. Fresh Release builds use /EHsc and /O2 /Ob2 /DNDEBUG; earlier failed-configure artifacts are not used for qualification.

## Build and verification

Native ARM64 application: bin/windows-resume/Release/ScreamSeq.exe. Build with windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-resume -Target ScreamSeq,document-controller-tests -Jobs 3.

Use the bundled real Python executable, not the WindowsApps alias. Set TMPDIR/TEMP/TMP to disposable scratch, SCREAMSEQ_TEST_EXE to the explicit build, and SCREAMSEQ_REFERENCE_PROJECT to the supplied Mac reference.

| Suite | Fresh result |
|---|---|
| Portable shared core | 24 CTests passed after rebuilding current source |
| Assets/imports/decoders | 6 CTests passed |
| API transport/cache | 2 CTests passed, including exact request boundary |
| Graph operations | 9 CTests passed, including unsorted envelopes and pending Redo |
| Envelope operations/catalogue | 23 CTests passed, including complete metadata budget |
| VST3 native provider | 17 CTests passed, including pending editor/SDK review regressions |
| Registry review | 11 CTests passed, including capacity, identities, cleanup and concurrent writers |
| Hosted project | PCM, automation, ownership, repeated failure, large callbacks and fault silence passed |
| Scoped C++ allocation probe | Positive controls detected; zero new/delete in prepared renders; direct malloc/free and locks are outside coverage |
| Actual app suites | All 51 tests passed in bin/windows-resume-application-qualified.log |

Actual-app coverage includes worker publication failures, bounded/reused caches, Unicode, stale requests, native focus, save/reopen, private clipboard, large PCM requests, plugin-owned import rejection, sample/loop controls, cancelled drafts/drags, idle redraw and hosted partitioning. A loop-control regression caught disabled reverse/ping-pong flags; disabling now clears them as the shared model requires.

The actual Mac project rendered through the application's worker at 44100/48000/96000 Hz with partitions 17/128/4096/8193. The original reference is preserved. Shared hosted fixtures retain the 1e-6 PCM bound; measured absolute-automation partition error was at most 3.241e-7. This is not Mac-native audio equivalence.

## Measured performance and limits

bin/windows-resume-wasapi-reference.json: 10.06-second silent hardware run of a disposable reference copy at 48000 Hz, 480-frame period. 1007 callbacks / 483360 frames, maximum callback 5026.9 microseconds; zero deadline overruns, starvation, processor faults, device errors and MMCSS errors. DSP ran normally; output was muted afterward. No route or system volume changed. This is bounded callback evidence, not acoustic/loopback or long-session proof.

bin/windows-resume-idle-comparison.json: sequential ten-second stopped demo runs with builds finished. Preserved review build: 1.421875 CPU-seconds / 130 frames; resumed build: 0.25 CPU-seconds / one frame, including startup/shutdown. The measured resumed executable is preserved at bin/windows-resume-evidence/performance/ScreamSeq.exe, SHA-256 B3481423FAE7307F6C12751B2B6946155C2BF2B0841B74C218A7CD68BD109E0F. Later loop controls are not part of that measured artifact. Do not extrapolate this workload to total DAW capacity or sustained 60 Hz presentation.

## Remaining work

- Graph/curve/precise-note/instrument UI and app dispatch with real rack, parameter and conflict hooks. GraphOperations/EnvelopeOperations passing tests does not mean those endpoints are advertised.
- VST3 discovery/editor/state-history workflow and path resolution; device selection; MIDI/recording/recovery; floating/saved layouts; accessibility and configurable keyboard parity.
- Sample zoom, direct drawing/crossfade controls and audition. Their operation layers exist, but UI coverage is incomplete.
- Sustained foreground presentation, long loaded audio/loopback, full malloc/free/lock audit, sanitizers, commercial plugins and cross-platform reopen.
- Native ARM64 is tested; x64/ARM64EC bridging is not implemented/qualified.

No commits, pushes, resets, stashes, user-song writes or subagent dispatches were performed. Close only task-owned QA PIDs. Earlier handoff/reference artifacts remain preserved; see the evidence manifest for hashes.

