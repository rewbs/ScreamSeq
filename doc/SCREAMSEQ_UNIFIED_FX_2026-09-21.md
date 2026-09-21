# Unified FX columns — 21 September 2026

Every channel now has one to eight equal FX columns. Each accepts every tracker
effect in the current song format's catalog and ScreamSeq's precise PS/PL parameter,
BS/BL pitch and NC cut commands. There is no separate Native FX column type.
This checkpoint also includes the preceding, uncommitted interaction-polish and
plugin-trigger/precise-cut work.

## Using it

- Click **+FX** in a channel header, or right-click the pattern and choose
  **FX columns**. Populated columns cannot be removed accidentally.
- Arrow keys move between each command and value. Type a two-character command
  such as **SC**, **PL** or **NC**, or press **?** for the searchable finder.
- Tracker commands retain their original parameter range and tick behavior.
  Precise commands retain double-precision targets and 1/65,536-row timing.
  Return opens the appropriate editor. The wide grid value is an overview;
  it does not quantize precise values.
- Commands run left to right with shared channel effect memory. Independent
  effects combine; repeated sets leave the rightmost value. Notes trigger once.
  Format-specific pattern-delay and flow rules remain in force.
- Copy/paste includes all FX and their stable parameter bindings, remapping
  conflicting binding numbers by plugin/parameter identity. Paste expands the
  destination's FX count when necessary. Structural row operations include all
  FX, and Undo restores the entire operation.

## Implementation and agent interface

The shared model represents all eight columns identically. For original module
interchange, tracker FX 1 still uses the source pattern cell internally; other
tracker effects and all precise commands use typed metadata. The UI and agent
interface combine these into one set of cells. Extra tracker effects are prepared
on the control thread and executed by the guarded OpenMPT engine, including
sample playback, note timing, macros, speed/tempo, flow and cursor-start seeking.
Packed imported PC/PCS data retains its own interpretation in FX 1.

- `pattern.effects.get` reads every FX cell, total counts and bindings.
- `pattern.effects.set` atomically replaces a pattern's full FX collection;
  omitted collections are preserved. Counts are total columns, **1–8**.
- `pattern.effect.set` edits one zero-based column/cell, with `command:null`
  to clear. It has revision guards, dry run, no-op handling and one-step Undo.
- `kind:"tracker"` uses the catalog's numeric `effect` and `parameter` bytes.
  Precise command kinds keep their existing timing, binding and value fields.
- `pattern.performance.get/set` remain API aliases with these unified semantics.
- Clipboard `ScreamSeq Pattern 2` and `pattern.paste` include effects and bindings.
  Numeric source-byte Pattern Tools still target the source effect byte; the
  all-column API supports arbitrary precise edits.

The API description, JSON schema, musician guide and shared architecture document
are updated for the Windows implementation too.

## Native project format

The only supported native format is **project container 6 / metadata 17**, with
the current exact song snapshot. Historical native wrapper, metadata and RSONGS1
loading paths were removed. Older `.screamseq` and `.resonance` projects are
intentionally rejected without replacing the open document. Original MOD, XM,
S3M, IT, MPTM and other OpenMPT imports remain available. Save native features as
`.screamseq`; original module formats cannot represent all of them.

## Verification

- 75/75 core tests passed. The new unified-effects test additionally covers
  clipboard binding collisions, exact values, structural row edits, one-cell
  no-ops, invalid edit rejection, Undo, save/reopen and old-format rejection.
- **205 catalog entries** across MPTM, IT, XM, S3M and MOD produce sample-identical
  audio when moved from FX 1 to FX 8, both from the beginning and from a later
  cursor row. Their engine timelines match. Combined effects test execution
  order, one note onset, later-column note delay and callback partition invariance
  at 44.1/48/96 kHz with 17/128/512/4096-frame blocks.
- 30 stock OpenMPT PCM comparisons passed exactly, with zero intercepted host
  allocations, frees or locks. The unified-effects renderer passes the same audit.
- AppKit interface tests passed, including SC and PL in every column, deferred
  rapid value entry, navigation/hit geometry, bounds and rectangular deletion.
- The full application socket suite passed, including new unified methods,
  revision/dry-run/no-op behavior, count-shrink protection, schema and Undo.
- Live isolated UI checks confirmed actual SC3 entry in FX 4, opening PL in FX 1,
  applying NC at 0.375 rows in FX 1, and copying a later-column tracker effect to
  another channel with automatic expansion to four FX columns. API readbacks
  verified the stored values. A screenshot and generated demo accompany the build.

## Performance qualification

Two attempts on the final mixed-FX demo could not start the 60-second workload:
macOS reported the window as occluded despite it being visible to accessibility,
and the harness recorded **zero submitted/presented frames and zero audio
callbacks**. The final attempt used the final source fingerprint below. These
are setup failures, not FPS or audio-performance measurements. The prior sustained
60fps presentation limitation remains open; this update does not claim to fix it.

The intended combined workload uses BlackHole at 48 kHz / 512 frames with AU,
VST3, graph routing, native voices and mixed FX columns. Since setup did not run,
those settings were not applied in these attempts. Offline rendered-audio and
realtime-allocation audit results above are the audio evidence for this update.

## Checkpoint

Application: `bin/mac-checkpoints/2026-09-21-unified-fx/ScreamSeq.app`.
The adjacent `BUILD.json` records the exact source fingerprint, base revision,
executable hash and source-file hashes. Logs, API readbacks, the UI screenshot
and generated test song are retained beside the application.

The musician's existing process and song were preserved. Save and quit that app
before opening this checkpoint. Changes remain uncommitted.

- Source fingerprint: `6413980597709f8dfa37583b187b161e6493fe8b3878ff458b139e955b9e2c2b`.
- Packaged executable SHA-256: `621a47622057abfe3d16c2c68a315c6d24822e58f1e14cba7539689d6b65c5c0`.
- All 452 recorded source hashes match the final build; strict deep signature
  verification passed.
