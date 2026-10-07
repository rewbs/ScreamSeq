# Windows / Mac parity plan

Current sections/annotations checkpoint (2026-10-07): Arrange now has named
order-occurrence sections, Previous/Next section navigation, and retained Section
and Pattern details pages. Independent drafts preserve raw Unicode names/notes
through navigation, hiding and stale revisions. Guarded annotation APIs, native
persistence and playback-preserving annotation Undo/Redo are connected.
The final candidate passes **373/373 application tests** with strict isolation,
**14/14 focused cases**, **42/42 native tests**, a separate **15/15
document-operation groups** and six exact offline PCM comparisons. Fifteen
source-matched 192-DPI views pass review. See
[annotation evidence](ANNOTATION_PROGRESS.md) for executable identity and retained
earlier failures. Checkpoint destination: `bin/windows-checkpoints/annotations-20261007/`;
its manifest confirms packaging.

Next is a compact-header and native-selection appearance pass, followed by the
[arrangement matrix](MATRIX_PLAN.md) and
[independent dock groups](INDEPENDENT_DOCKING_PLAN.md).
Full Windows/Mac parity remains the goal.

Previous arrangement/timing checkpoint (2026-10-07): retained native order
and tempo/groove tools expose guarded arrangement edits, stable order-occurrence
selection, sequence selection, new/duplicate patterns and timing preview/Apply.
Candidate 6 passes **367/367 application tests** with strict outer isolation,
**8/8 focused cases**, **42/42 native tests**, a separate **11/11
document-operation groups** and six exact offline PCM comparisons. Fifteen
source-matched 192-DPI views pass review. Shared duplication also preserves exact
pattern-specific timing and metadata. See [arrangement evidence](ARRANGEMENT_PROGRESS.md)
for executable identity, logs and earlier failures. Checkpoint destination:
`bin/windows-checkpoints/arrangement-20261007/`; its manifest confirms packaging.
Its original next-step [sections and annotations plan](ANNOTATION_PLAN.md) is
retained as a scope audit; current annotation implementation is above.

Previous recording checkpoint (2026-10-07): timestamped WinMM input, correlated
WASAPI presentation history, worker-owned precise-note takes and a retained
native MIDI/recording window are implemented. The six guarded `recording.*`
methods share commit preparation with Mac; one Undo, native persistence,
copy-only live recovery and stopped imported-take review are connected.

Candidate 5 passes **359/359 application tests** in 875.469 seconds, with no
failures or skips and strict outer isolation; **15/15 focused cases** in 16.046
seconds; and **40/40 native tests** in 18.41 seconds. Eight source-matched views
pass native bounds and original-resolution review at 192 DPI. Final full-run
evidence is `bin/windows-recording-final-rerun2-app-tests.log`, `-isolation.log`
and `-exe-sha256.txt`. See [recording evidence](RECORDING_PROGRESS.md) for the exact
application hash, retained failed runs and diagnostic distinctions. The test-only
GUI-worker boundary removes post-GUI desktop migration; historical resource
causes remain unattributed. This qualifies the functional and scoped visual
checkpoint, not full Windows/Mac parity or every release gate.

Checkpoint location: `bin/windows-checkpoints/recording-20261007/`; the package
manifest is the authority for packaging completion.

Its [arrangement and timing plan](ARRANGEMENT_PLAN.md) is retained as a historical
audit; current implementation and remaining qualification are above.

Previous recovery checkpoint (2026-10-07): ten-second immutable autosave, ten
generations per session, a retained native browser and guarded recovery APIs are
implemented. Restore protects current work before replacement and opens a pathless
dirty document; snapshots preserve manual plugin state and imported unfinished
takes. See [recovery evidence](RECOVERY_PROGRESS.md) for final build identity,
qualification, retained failures and visual limits. Its then-next recording
slice is implemented above; the original [recording plan](RECORDING_PLAN.md)
is retained as a historical audit and qualification checklist.
The final recovery build passes 345 application tests, 14 focused recovery cases
and a supplemental vendor-input case with strict isolation. The diagnostic native
suite passes 38/38; the original unexplained 37/38 desktop-teardown failure is
retained. Eight rendered views at 192 DPI pass review; foreground, other scales
and sustained presentation remain separate gates.

Previous sample UI checkpoint (2026-10-07): the detailed editor now has retained
Drawing, Process, Loops, Paste and Snap pages. Independent normal/sustain rows
support joint preview/Apply and dashed pending boundaries; paste has a reviewed
clipboard/revision, rate conversion and gain controls; selection and loop
snapping include grid origin and automatic selection. The existing shared APIs,
Undo and native storage remain authoritative. See
[sample workflow evidence](SAMPLE_WORKFLOWS_PROGRESS.md).

The baseline passes all 330 application tests with strict outer isolation, plus
32 affected UI tests and 36 native CTests. The final focus-restoration/menu-hint
candidate's 33 individual UI tests pass, **but its outer run fails strict
foreground isolation with exit 1**. The foreground change is unattributed;
final-build isolation qualification remains unresolved. Its 19 reviewed
renderer/native-control views at large, compact and minimum client sizes pass
their own isolation checks, which do not qualify the failed test run. The report
records the distinct candidate hashes and both isolation-failure traces. Those
historical failures remain preserved. Independent dock groups,
remaining editor menus and the cross-platform qualification gates below remain open.

Current UI work (2026-10-07): the workspace has direct lower-editor tabs,
collapse/reopen, named saved arrangements and a retained layout manager. Command
search now ranks relevant titles, supports unordered words and restores field
focus. Toolbar collisions and compact inspector clipping are corrected. See
[workspace evidence](WORKSPACE_LAYOUTS_PROGRESS.md) for exact qualification.

The current docking checkpoint adds retained automation/instrument docking, compact editor
pages, floating/redocking and local keyboard routing. Wide windows can show the
graph beside the selected editor; smaller windows retain each editor behind tabs.
See [docking evidence](WORKSPACE_DOCKING_PROGRESS.md) for qualification and limits.
Arbitrary independent dock groups and the cross-platform
qualification gates below remain open.

Native context menus now connect pattern, sample, graph, instrument and detailed
sample workflows to the existing guarded actions. The command palette supports
custom keys, sequences, Clear, Reset and saved-preference reload. Main-window and
retained-editor dispatch share the binding model, preserving native text input,
local editor commands and note release. See
[command and menu evidence](WORKSPACE_COMMANDS_PROGRESS.md) for exact scope,
candidate-specific tests and visual limits.

The sample UI retains independent raw loop drafts and paste preview signatures
bound to document, sample identity, revision and clipboard ID. Unrelated edits
cannot silently rebase those drafts. Completing these interactions does not
replace accessibility or cross-platform qualification.

## Upstream integration checkpoint — 2026-09-21

Baseline: upstream `bcfe0f8a7`, following `c486a0642`. The remote has no `main`;
its default branch is `codex/screamseq`. Local Windows work was preserved in
`40d0f074c` before merging. `mptrack/` remains upstream OpenMPT UI.

Full parity is the goal, not the status of this checkpoint. Musical behavior,
format, history and API semantics belong to the shared model. Native window,
device and plugin hosting belong to each platform. See
[qualification evidence](UPSTREAM_PLUGIN_QUALIFICATION.md) and
[current app interfaces](App/INTEGRATION.md).

Latest user-directed refinement: `FLAT_CONTROLS_PROGRESS.md` removes cursor-driven
redraws of unchanged native controls and gives workspace/modeless selectors a
shared flat appearance. Both platform frontends remain in this source tree;
Mac runtime and reciprocal project qualification remain outstanding.
All 268 application tests and 31 native CTests pass. The separate ARM64 package
is `bin/windows-checkpoints/flat-controls-20260922/`.

Previous continuation: `AUDIO_SETTINGS_PROGRESS.md` adds native output selection,
supported buffer-period negotiation and independent guarded settings APIs. Six
focused output cases plus eleven library cases and all 30 primary CTests pass.
The full isolated suite passes **264/264**, with no failures or skips. Timestamped MIDI/recording and recovery remain
next, with device-change notification and multichannel output still unimplemented.

Previous continuation: `SAMPLE_LIBRARY_PROGRESS.md` adds all eight library APIs,
background indexing/search, independent sample preview, the native browser and
guarded family import review. All 257 isolated application tests, 30 primary
native CTests, two index/worker tests and two adapter tests pass. A subsequent
live-gain/preview-feedback refinement passes 11 focused app tests and its native
callback/device tests. A fresh fetch on 2026-09-22 still finds no newer upstream commits.
`DEFERRED_VIEWS_PROGRESS.md` retains native view-opening
requests during background reads without replaying edits or stale targets. The
full isolated suite passes 247/247 application tests and 29/29 native CTests.
`SURGE_RESTART_PROGRESS.md` diagnoses and fixes the
post-Undo Surge audition fault: the host rejected a parameter-title notification.
Title/unit refresh now publishes a separate control snapshot while the audio
contract stays immutable; first-failure diagnostics preserve future fault causes.
`INSTRUMENT_IMPORT_PROGRESS.md` adds guarded native
instrument import, imported-sound selection and a visible keymap beside the
envelope, including retained range drafts. `SAMPLE_SETTINGS_PROGRESS.md` adds native sample settings,
batch sample/mapped-instrument import, captured replacement and instrument
creation, and fixes implicit panning and relative-tuning changes on partial edits.
`MUSICAL_TYPING_PROGRESS.md` adds selected-sound pattern
entry, sample/instrument editor typing and main-workspace Live keys. A fresh
fetch still finds no newer upstream commits. The installed Contourtonist,
OrbitCab and Surge XT binary hashes were rechecked before current app testing.
The installed Surge repetition gate passes 20/20. The preceding run's lost
routing command is fixed and covered by real-worker regressions. The entire
suite now inherits a private desktop; strict foreground/clipboard checks pass.
The older foreground failure remains unattributed and preserved in its report.
Following implementation priorities are MIDI/device/recording/recovery and
workspace parity. Existing plugin and cross-platform release gates remain in force.
The Mac source confirms sample settings and batch import. Sample export is a
separate enhancement; no existing Mac sample-export UI/API was found in this
review, so it is not treated as an established parity gap.

The instrument step matches `mac/App/main.swift::importInstrument`
(ITI/XI/PAT/SFZ, new slot, select the imported sound) and the octave/sample
mapping summary in `mac/App/AssetEditors.swift`. Windows now stages numeric
key ranges alongside a native map list in `InstrumentEnvelopeWindow.hpp` and
guards imports without discarding that parent draft. The sample library now
follows `SampleLibraryIntegration.swift`: separate library revisions,
bounded background indexing/search, folder tags, preview, and the existing
atomic `sample.importMany` / `instrument.importMultisample` transactions.

## Current: timestamped MIDI and recording qualification

Windows now supplies correlated presentation timestamps to the shared renderer,
advances origins through callbacks larger than 4,096 frames, and retains clock
history independently of renderer lifetime. WinMM source discovery/connection
runs off-thread, using opaque device-interface IDs and a bounded generation-aware
queue. Driver millisecond precision and anchor uncertainty are reported honestly;
invalid/startup clocks do not substitute the edit cursor.

The six `recording.*` methods and native retained window support stable captured
targets, atomic batches, explicit loss review, one-Undo commit and exact native
persistence. API Stop stops capture without commit; native Stop attempts Finish
once and leaves losses or stale takes for review. Autosave closes notes only in a
copy, while explicit Restore drains/stops capture before protective replacement.
Imported takes hydrate stopped with fresh IDs and preserved compatibility.
The shared commit helper is used by both frontends. Container 6 / metadata 17 is
unchanged; the optional loss-reason recovery field is supported by current source
on both platforms and requires reciprocal Mac qualification.

The final full application suite passes 359/359, with the 15/15 focused suite and
40/40 native suite also passing. Retain the three historical native migration
failures alongside the replacement-harness pass. See
[recording evidence](RECORDING_PROGRESS.md) for exact candidate-specific results;
[the original recording plan](RECORDING_PLAN.md) remains a historical checklist.
Hardware MIDI timing/hotplug and real sample/plugin performance capture,
reciprocal Mac reopen, independent dock groups, accessibility and the broader
release gates below remain open. The existing short silent WASAPI and injected
callback tests do not establish those results.

## What changed upstream and how it changes the work

| Landed capability | Windows status after integration | Remaining work |
|---|---|---|
| Unified 1–8 FX columns, ordinary tracker commands plus PS/PL/BS/BL/NC | Shared engine/API, variable native grid, two-character entry, searchable FX inspector, stable bindings, row transforms and Pattern 2 clipboard with binding remap integrated. FX 1→FX 8/4 PCM is identical at three rates and four block sizes. See `PATTERN_FX_PROGRESS.md` and `PATTERN_NOTES_PROGRESS.md`. | Broader row-tool controls, first-letter completion and visible layout/accessibility qualification. |
| Precise note cut and plugin trigger instruments | Shared semantics/persistence, current precise-note API, native row-draft editor and timing/velocity canvas, NC inspector, empty trigger API, native creation/assignment and independent history integrated. Installed Surge XT trigger PCM/WASAPI checks pass with documented fixture constraints. | Broader instrument/routing qualification, foreground visual/accessibility verification and live native-edit publication. |
| Project container 6 / metadata 17; RSONGS2 snapshots | Current-only native open/save, strict rejection of historical native wrappers, opaque-state retention, sample-exact save/reopen tests. MOD/XM/IT/S3M import remains available. | Obtain a newly exported Mac format-17 fixture and perform Windows→Mac→Windows reopen. The supplied format-14 reference is historical and is not silently migrated. |
| Explicit disconnected mixer/plugin destinations | Actual-app mixer/graph API, native bus controls, reusable graph canvas, pattern curves, envelope bank and song routing overview integrated. Native graph command lanes connect pattern rows to row/persistent graph stages. Pattern parameter automation now has the shared API and a native curve editor with transforms, bank and formula workbench connections. See `GRAPH_COMMANDS_PROGRESS.md` and `PARAMETER_AUTOMATION_PROGRESS.md` for these latest additions. | Inline completion and broader routing/long-session qualification. The audition piano and shared API are implemented in `AUDITION_PROGRESS.md`. |
| Voice positions for sample and envelope playback | Shared bounded atomic telemetry, Windows transport fields, sample waveform markers and native instrument-envelope markers integrated. See `INSTRUMENT_ENVELOPE_PROGRESS.md`. | Audition piano, detailed waveform markers and preview releases are implemented in `AUDITION_PROGRESS.md`; broader vendor release-tail qualification remains. |
| Dynamic plugin latency and safer editor shutdown | Shared chain maintenance ported through the extracted backend; Windows pauses/joins WASAPI before reactivation and compensation updates, retains transport position, respects Stop. Fixture latency/lifetime tests pass. | Exercise interactive commercial instruments, changing graph latency during long sessions, full host allocation/free/lock evidence. |
| Plugin aliases, routing and editor interactions | Native rack, discovery, assign/remove/bypass, program/port controls, modeless instrument aliases/MIDI channels, sound presets, library organization, explicit VST3 location repair and native graph/mixer connections integrated; live parameters reach the prepared renderer. Two ARM64 effects and Surge XT instrument tested through the app. See `PLUGIN_ALIASES_PROGRESS.md`, `PLUGIN_PRESETS_PROGRESS.md`, `PLUGIN_LIBRARY_PROGRESS.md`, `PLUGIN_PATH_PROGRESS.md` and `SONG_ROUTING_PROGRESS.md`. | Live opaque-state replacement (including Surge's first-open zoom state) and broader missing-plugin recovery. |
| Mac context menus, docking, focus, recovery and visual refinements | Retained automation/instrument docking, compact pages, named layouts, five native context-menu surfaces, configurable shortcuts, native autosave/recovery and timestamped MIDI/take review are implemented; see the workspace, recovery and recording reports above. | Hardware MIDI and reciprocal Mac qualification, other editor menu surfaces, independent dock groups, accessibility and actual foreground comparison at multiple scales remain open. |

## Execution order and completion gates

The native rack is now integrated; see `PLUGIN_RACK_PROGRESS.md`. Discovery,
add/remove/move/bypass, parameters, aliases, native editor ownership, saved state
and independent plugin history have application coverage. Program/port native
controls and the provider fixture are covered in `PLUGIN_PROGRAM_PORTS_PROGRESS.md`;
broader vendor and routing qualification remain.
Live parameter propagation now has bounded atomic publication, worker-owned
baseline/history, and installed-plugin WASAPI coverage; see
`LIVE_PLUGIN_PARAMETERS.md`. Empty trigger creation and installed instrument
evidence are in `TRIGGER_INSTRUMENT_PROGRESS.md`. Immediate remaining plugin work
is live opaque-state replacement,
broader missing-plugin recovery and the OrbitCab partition discrepancy. The following gates remain
in force. A fresh fetch during this continuation found no newer upstream commits.

Mac-compatible preset inspect/save/load and native guarded file dialogs are
implemented; see `PLUGIN_PRESETS_PROGRESS.md`. Loads pin plugin/document identity
across selection and inspect, recheck `expectedPresetRevision`, preserve aliases,
ports/routing and use one plugin Undo. Dry-run load validates file identity
without invoking a vendor state decoder. Both branded and legacy file extensions
are accepted; new native saves use `.screamseq-preset` with compatible wire magic.
Native library search, categories, favorites and hidden entries are now
implemented in a retained browser and independent API; see
`PLUGIN_LIBRARY_PROGRESS.md`. Explicit VST3 location repair for rack instances
and graph recipes is implemented in `PLUGIN_PATH_PROGRESS.md`. It accepts only
the same plugin class and role from a freshly verified native Windows module,
changes only the saved path and retains exact opaque state, aliases and routing.
AU recipes remain preserved and unavailable on Windows. Structural and opaque
changes still use the stop-before-publication path.
Library preferences use their own `expectedLibraryRevision`, outside document
Undo, and damaged preferences must not disable discovery or plugin insertion.
Library IDs must follow the reviewed Mac contract: format plus normalized path
and uppercase class ID for VST3, format/effect ID for built-ins, and component
codes for AU. Unlike sound-preset matching, per-installation library identity
includes the VST3 path. Display names are excluded. Preference writes must
preserve concurrent edit guards.

1. **Plugin workflow in the application.** Connect the existing scanner/provider
   to a native browser and worker-owned rack. Every musical change needs guarded
   API, dry run, Undo/Redo and native persistence. Editor gestures must commit
   state without losing automation/binding identities. Reuse the private-STA
   ownership and exact class/path/hash validation. Include two real effects and
   at least one real instrument, removal with open editors, repeated reopen,
   malformed/missing state and unavailable-plugin preservation. Investigate the
   OrbitCab partition failure; do not relax the shared 1e-6 comparison threshold.
2. **Unified pattern and instrument workflows.** Integrate all current Mac FX,
   precise notes/cuts, trigger instruments, searchable effects and clipboard
   semantics before polishing their Windows controls. Support the same API
   inputs and rejection rules; verify history and persistence with the shared
   model. Complete independent ruler/timeline display and focus behavior.
3. **Graph, mixer, automation and envelope editors.** Real rack activity,
   parameter, baseline and conflict hooks are integrated into the actual app.
   The native mixer bus dock and guarded operations have app, worker, PCM and
   live WASAPI coverage; see `MIXER_GRAPH_PROGRESS.md`. The reusable graph canvas
   and independent graph-plugin editing now have native UI, persistence and PCM
   coverage (`GRAPH_EDITOR_PROGRESS.md`). Native graph pattern curves, scripted
   previews and stable modulation sampling are now covered in
   `GRAPH_CURVES_PROGRESS.md`. Both envelope bank levels now have modeless native
   controls, launched from graph curves, with guarded drafts, linked updates,
   explicit catalogue copies and native app coverage (`ENVELOPE_BANK_PROGRESS.md`).
   The modeless formula workbench now supplies multiline editing, completion,
   searchable reference and guarded preview/Use from graph curves and song bank
   masters (`FORMULA_WORKBENCH_PROGRESS.md`). Preview validation covers the full
   bank range on both platforms without expanding pattern-edit limits.
   The native song routing overview now connects these stages and their editors;
   see `SONG_ROUTING_PROGRESS.md`. It includes explicit/default inserts, sends,
   graph and plugin ports, sample-instrument/bus graph assignments, retained
   layout drafts and disconnected routes. Routing inventory reads and graph
   capacity validation avoid copying vendor state while retaining its validation.
   Graph command lanes and their native captured editor are now implemented;
   `GRAPH_COMMANDS_PROGRESS.md` records control/history/persistence tests and
   installed Contourtonist command rendering. The standalone
   `automation.pattern.get/set/remove/copy/transform` methods and native parameter
   curve editor are now implemented; `PARAMETER_AUTOMATION_PROGRESS.md` records
   their API, native control and rendered-audio qualification. Native instrument
   volume/pan/pitch envelopes, point and marker controls,
   shared transforms, bank entry, settings/keymap and bounded voice markers are
   implemented in `INSTRUMENT_ENVELOPE_PROGRESS.md`. Absolute song automation
   now has the existing Mac read/replacement API and a retained native editor
   with bounded drawing, exact frame/native value fields, plugin history and
   cross-view navigation (`ABSOLUTE_AUTOMATION_PROGRESS.md`). Complete native
   inline formula completion and the remaining bank/workbench entry points, with retained
   drawing, meaningful context menus and keyboard use.
4. **Recording and workspace.** Native sample zoom/drawing/crossfade, processing,
   snapping and clipboard controls are implemented in `SAMPLE_DETAIL_PROGRESS.md`;
   their current qualification is recorded there. Native sample/instrument audition
   and detailed voice markers are implemented in `AUDITION_PROGRESS.md`. Device
   selection, timestamped MIDI/takes and recovery, retained floating/docked editors,
   named layouts and configurable command keys are now integrated. Complete their
   remaining qualification, independent dock groups, accessibility and remaining
   editor-menu parity. Keep cursor, selection, focus, pins and playback independent.
   Pattern/dock typing and main-workspace Live keys are implemented in
   `MUSICAL_TYPING_PROGRESS.md`; broader floating-tool keyboard behavior remains.
   Native sample settings, batch import, captured replacement and creation of a
   sample instrument are implemented in `SAMPLE_SETTINGS_PROGRESS.md`.
   Native library search/tags/preview and guarded family review/import are now
   implemented in `SAMPLE_LIBRARY_PROGRESS.md`; foreground and large-pack
   performance qualification remain open.
5. **Release qualification.** Fresh Mac and Windows builds against the same
   source, reciprocal project reopen and offline comparisons, commercial-plugin
   matrix, endpoint switching, long loaded playback, loopback, foreground
   presentation at supported scales, memory/latency bounds and full realtime
   audits. Current short runs do not establish sustained 60 Hz or top-of-class
   audio capacity. Native ARM64 is exercised here; x64 and bridging remain open.

Absolute song automation now matches the Mac `automation.get` /
`automation.replaceLane` contract: fixed 48 kHz frame timestamps, native
parameter values, preserved unrelated lanes and plugin-history Undo. Windows
uses the existing project wrapper and renderer, with live-catalogue validation
and pattern-curve/command conflicts. Native controls, 100,000-point storage and
bounded drawing, history, reopen, silent live behavior and enabled/absent audio
comparisons are covered in `ABSOLUTE_AUTOMATION_PROGRESS.md`. Long loaded audio
and reciprocal Mac qualification remain release gates.

## Evidence rules

- Keep actual UI captures and executable/source hashes with each checkpoint.
- Preserve old reports as historical evidence, never as proof of current source.
- Use separate task-owned processes and disposable songs; never replace a
  musician's running application or change system audio defaults.
- Do not infer successful plugin integration from discovery alone, or audible
  correctness from finite PCM alone. Record phase-specific failures.
