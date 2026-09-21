# Windows / Mac parity plan — 2026-09-21 upstream integration

Baseline: upstream `bcfe0f8a7`, following `c486a0642`. The remote has no `main`;
its default branch is `codex/screamseq`. Local Windows work was preserved in
`40d0f074c` before merging. `mptrack/` remains upstream OpenMPT UI.

Full parity is the goal, not the status of this checkpoint. Musical behavior,
format, history and API semantics belong to the shared model. Native window,
device and plugin hosting belong to each platform. See
[qualification evidence](UPSTREAM_PLUGIN_QUALIFICATION.md) and
[current app interfaces](App/INTEGRATION.md).

## What changed upstream and how it changes the work

| Landed capability | Windows status after integration | Remaining work |
|---|---|---|
| Unified 1–8 FX columns, ordinary tracker commands plus PS/PL/BS/BL/NC | Shared engine integrated; moved-effect PCM/timeline/cursor-start tests pass across MPTM/IT/XM/S3M/MOD. Metadata reads/writes every new command field. | Integrate the complete pattern-performance API and grid/editor, two-character entry, FX discovery, clipboard binding remap, and row transformations. Ordinary FX 1 editing is not full parity. |
| Precise note cut and plugin trigger instruments | Shared semantics and persistence integrated. | Trigger creation/assignment, precise-note editor and Windows API transactions, history and saved-project tests. |
| Project container 6 / metadata 17; RSONGS2 snapshots | Current-only native open/save, strict rejection of historical native wrappers, opaque-state retention, sample-exact save/reopen tests. MOD/XM/IT/S3M import remains available. | Obtain a newly exported Mac format-17 fixture and perform Windows→Mac→Windows reopen. The supplied format-14 reference is historical and is not silently migrated. |
| Explicit disconnected mixer/plugin destinations | Shared mixer behavior and Windows metadata roundtrip accept output/target 0. | Connect live mixer/graph API and controls; exercise disconnect with sends, history and reopen through the UI. |
| Voice positions for sample and envelope playback | Shared bounded atomic telemetry, Windows transport fields and sample waveform markers integrated. | Envelope editor markers and audition UI, including overlapping voices and release tails. |
| Dynamic plugin latency and safer editor shutdown | Shared chain maintenance ported through the extracted backend; Windows pauses/joins WASAPI before reactivation and compensation updates, retains transport position, respects Stop. Fixture latency/lifetime tests pass. | Exercise interactive commercial instruments, changing graph latency during long sessions, full host allocation/free/lock evidence. |
| Plugin aliases, routing and editor interactions | Native rack, discovery, assign/remove/bypass, guarded program/state APIs, independent history and editor ownership integrated; installed ARM64 effects tested through the app. | Uninterrupted live parameter edits, program/bus controls, presets/library, explicit path resolution, missing-plugin recovery and a real instrument. |
| Mac context menus, docking, focus, recovery and visual refinements | Reviewed; Windows retains its native implementation. | Implement the equivalent interactions and visual hierarchy in Windows, then compare actual windows at multiple scales. |

## Execution order and completion gates

The native rack is now integrated; see `PLUGIN_RACK_PROGRESS.md`. Discovery,
add/remove/move/bypass, parameters, aliases, native editor ownership, saved state
and independent plugin history have application coverage. Program and bus APIs
are connected but still need native controls and broader provider fixtures.
Immediate remaining plugin work is uninterrupted live parameter propagation,
presets/library organization, missing-plugin resolution, a real instrument and
the OrbitCab partition discrepancy. The following gates remain in force.

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
3. **Graph, mixer, automation and envelope editors.** Supply real rack activity,
   parameter, baseline and conflict hooks to the existing operation layers.
   Passing isolated operations does not mean the app exposes them. Complete
   graph routing, disconnected outputs, curve/formula editing and both envelope
   bank levels, with retained drawing, meaningful context menus and keyboard use.
4. **Recording and workspace.** Device selection, MIDI input and mapping,
   precise recording/recovery, sample zoom/drawing/crossfade/audition, floating
   and persisted docks, accessibility, configurable keys and command palette
   parity. Keep cursor, selection, focus, pins and playback independent.
5. **Release qualification.** Fresh Mac and Windows builds against the same
   source, reciprocal project reopen and offline comparisons, commercial-plugin
   matrix, endpoint switching, long loaded playback, loopback, foreground
   presentation at supported scales, memory/latency bounds and full realtime
   audits. Current short runs do not establish sustained 60 Hz or top-of-class
   audio capacity. Native ARM64 is exercised here; x64 and bridging remain open.

## Evidence rules

- Keep actual UI captures and executable/source hashes with each checkpoint.
- Preserve old reports as historical evidence, never as proof of current source.
- Use separate task-owned processes and disposable songs; never replace a
  musician's running application or change system audio defaults.
- Do not infer successful plugin integration from discovery alone, or audible
  correctness from finite PCM alone. Record phase-specific failures.
