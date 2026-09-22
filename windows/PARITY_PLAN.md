# Windows / Mac parity plan — 2026-09-21 upstream integration

Baseline: upstream `bcfe0f8a7`, following `c486a0642`. The remote has no `main`;
its default branch is `codex/screamseq`. Local Windows work was preserved in
`40d0f074c` before merging. `mptrack/` remains upstream OpenMPT UI.

Full parity is the goal, not the status of this checkpoint. Musical behavior,
format, history and API semantics belong to the shared model. Native window,
device and plugin hosting belong to each platform. See
[qualification evidence](UPSTREAM_PLUGIN_QUALIFICATION.md) and
[current app interfaces](App/INTEGRATION.md).

Latest continuation: `MUSICAL_TYPING_PROGRESS.md` adds selected-sound pattern
entry, sample/instrument editor typing and main-workspace Live keys. A fresh
fetch still finds no newer upstream commits. The installed Contourtonist,
OrbitCab and Surge XT binary hashes were rechecked before current app testing.
Next implementation priorities are native sample properties and batch/replacement import,
instrument import/keymap presentation, then MIDI/device/recording and workspace
parity. Existing plugin and cross-platform release gates remain in force.
The Mac source confirms sample settings and batch import. Sample export is a
separate enhancement; no existing Mac sample-export UI/API was found in this
review, so it is not treated as an established parity gap.

## What changed upstream and how it changes the work

| Landed capability | Windows status after integration | Remaining work |
|---|---|---|
| Unified 1–8 FX columns, ordinary tracker commands plus PS/PL/BS/BL/NC | Shared engine/API, variable native grid, two-character entry, searchable FX inspector, stable bindings, row transforms and Pattern 2 clipboard with binding remap integrated. FX 1→FX 8/4 PCM is identical at three rates and four block sizes. See `PATTERN_FX_PROGRESS.md` and `PATTERN_NOTES_PROGRESS.md`. | Broader row-tool controls, first-letter completion and visible layout/accessibility qualification. |
| Precise note cut and plugin trigger instruments | Shared semantics/persistence, current precise-note API, native row-draft editor and timing/velocity canvas, NC inspector, empty trigger API, native creation/assignment and independent history integrated. Installed Surge XT trigger PCM/WASAPI checks pass with documented fixture constraints. | Broader instrument/routing qualification, foreground visual/accessibility verification and live native-edit publication. |
| Project container 6 / metadata 17; RSONGS2 snapshots | Current-only native open/save, strict rejection of historical native wrappers, opaque-state retention, sample-exact save/reopen tests. MOD/XM/IT/S3M import remains available. | Obtain a newly exported Mac format-17 fixture and perform Windows→Mac→Windows reopen. The supplied format-14 reference is historical and is not silently migrated. |
| Explicit disconnected mixer/plugin destinations | Actual-app mixer/graph API, native bus controls, reusable graph canvas, pattern curves, envelope bank and song routing overview integrated. Native graph command lanes connect pattern rows to row/persistent graph stages. Pattern parameter automation now has the shared API and a native curve editor with transforms, bank and formula workbench connections. See `GRAPH_COMMANDS_PROGRESS.md` and `PARAMETER_AUTOMATION_PROGRESS.md` for these latest additions. | Native instrument import, inline completion and broader routing/long-session qualification. The audition piano and shared API are implemented in `AUDITION_PROGRESS.md`. |
| Voice positions for sample and envelope playback | Shared bounded atomic telemetry, Windows transport fields, sample waveform markers and native instrument-envelope markers integrated. See `INSTRUMENT_ENVELOPE_PROGRESS.md`. | Audition piano, detailed waveform markers and preview releases are implemented in `AUDITION_PROGRESS.md`; broader vendor release-tail qualification remains. |
| Dynamic plugin latency and safer editor shutdown | Shared chain maintenance ported through the extracted backend; Windows pauses/joins WASAPI before reactivation and compensation updates, retains transport position, respects Stop. Fixture latency/lifetime tests pass. | Exercise interactive commercial instruments, changing graph latency during long sessions, full host allocation/free/lock evidence. |
| Plugin aliases, routing and editor interactions | Native rack, discovery, assign/remove/bypass, program/port controls, modeless instrument aliases/MIDI channels, sound presets, library organization, explicit VST3 location repair and native graph/mixer connections integrated; live parameters reach the prepared renderer. Two ARM64 effects and Surge XT instrument tested through the app. See `PLUGIN_ALIASES_PROGRESS.md`, `PLUGIN_PRESETS_PROGRESS.md`, `PLUGIN_LIBRARY_PROGRESS.md`, `PLUGIN_PATH_PROGRESS.md` and `SONG_ROUTING_PROGRESS.md`. | Live opaque-state replacement (including Surge's first-open zoom state) and broader missing-plugin recovery. |
| Mac context menus, docking, focus, recovery and visual refinements | Reviewed; Windows retains its native implementation. | Implement the equivalent interactions and visual hierarchy in Windows, then compare actual windows at multiple scales. |

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
   instrument import and visual keymaps, inline formula
   completion and the remaining bank/workbench entry points, with retained
   drawing, meaningful context menus and keyboard use.
4. **Recording and workspace.** Native sample zoom/drawing/crossfade, processing,
   snapping and clipboard controls are implemented in `SAMPLE_DETAIL_PROGRESS.md`;
   their current qualification is recorded there. Native sample/instrument audition
   and detailed voice markers are implemented in `AUDITION_PROGRESS.md`. Complete
   sample property/import/export controls, device selection, MIDI input
   and mapping, precise recording/recovery, floating
   and persisted docks, accessibility, configurable keys and command palette
   parity. Keep cursor, selection, focus, pins and playback independent.
   Pattern/dock typing and main-workspace Live keys are implemented in
   `MUSICAL_TYPING_PROGRESS.md`; broader floating-tool keyboard behavior remains.
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
