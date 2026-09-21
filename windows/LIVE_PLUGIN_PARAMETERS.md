# Windows live plugin parameters — 2026-09-21

This continues the native rack checkpoint. Full Mac parity remains in progress;
the completion gates in `PARITY_PLAN.md` still apply.

## Implemented

Validated API/native-rack parameter edits update the saved baseline and plugin
history while publishing one complete parameter batch to the prepared renderer.
The single-producer/single-consumer ring has 4,096 preallocated entries. Its
release publication includes batch boundaries; consumption never splits a batch
at the ordinary 128-change servicing limit. Queue saturation rejects the entire
batch and the app stops playback before retaining the complete saved edit.

Instrument, mixer and effect stages share one parameter boundary per render
block. Updates arriving after that boundary wait for the next block. The common
chain and both platform callback entry points use this rule. The Mac entry point
was updated in source but cannot be built or qualified on this Windows host.

The document worker validates IDs/ranges and constructs disposable baseline
instances before publication. Invalid, dry-run and no-op writes neither stop nor
publish parameters. Plugin Undo/Redo remains independent of document history;
save/reopen retains the edited baseline. Automation acts on the separate playback
instance and is not captured into saved state.

Native editor notifications are polled on a 16 ms UI timer while editors are
open. Polling drains/coalesces gestures without copying the rack or serializing
state. Full state capture waits for 400 ms without parameter activity, while
save/read/close/history forces immediate capture. Idle opaque-state checks are
bounded to the same 400 ms interval. Unchanged polls avoid native control layout.
These are scheduling intervals, not a measured end-to-end latency guarantee.

Before committing an editor gesture without stopping, a disposable instance
replays its parameters from the saved baseline. Its complete resulting state
must match the editor state. This prevents an IR/preset load that also emits
parameter notifications from being mistaken for a parameter-only edit. Opaque
changes retain their complete saved state and currently stop playback.

## Qualification

The ARM64 app, worker tests and hosted tests build in `bin/windows-parity`.
The checkpoint manifest records source identity, executable SHA-256 and evidence
hashes; all app evidence must be interpreted against that executable.

- The Windows Python suite discovered 64 cases: 63 passed, with the opt-in
  hardware case run separately. The suite includes both installed ARM64 effects.
- Worker integration tests passed live UI-thread publication, prepublication
  failure, invalid/dry/no-op batches, overflow fallback, independent Undo/Redo,
  and save/reopen against a real prepared renderer.
- `live-parameter-tests` passed whole-batch rejection, 4,096-entry transactions,
  ring wrap, mid-block deferral and 2,048 concurrent batches of 130 changes.
  Rendered manual edits at 44.1/48/96 kHz and 17/128/4,096/8,193-frame request
  sizes remained below the existing 1e-6 PCM comparison threshold. Later song
  automation still took effect at its own boundary. The saved project and
  automation remained unchanged by playback.
- The full hosted-project fixture passed alongside the new tests, including
  automation timing, prepared-owner lifetime, repeated late preparation failure,
  processor-fault silence and unavailable-AU preservation. The optional reference
  is the explicitly synthetic format-17 wrapper described in the upstream report,
  not a newly exported Mac fixture.
- Scoped host C++ `new`/`delete` probes passed normal/aligned positive controls
  and recorded no callback allocations or frees. These do not cover direct
  malloc/free, locks, drivers or vendor-private allocations.
- A fresh `bin/windows-live-vst3` build passed all 18 provider fixture CTests,
  covering scanner failures, native editor ownership, separate controllers,
  state/phase/catalog lifecycles and changing latency.

Actual-app silent WASAPI qualification covers Gainer, Contourtonist 0.2.2 and
OrbitCab 2.5.0. It applies parameter batches during playback, checks invalid and
dry-run nonmutation, saves while playing, quits cleanly, and reopens exact saved
state in an inspection instance. OrbitCab's own editor-generated EQ notification
also exercises gesture propagation and baseline capture. A separate owned
process is used for each playback run; system audio defaults are unchanged.
Per-plugin durations, callback counters/timings and transport samples are in
`bin/windows-plugin-qualification/live-parameters/live-parameters.json`.
The final run used 48 kHz / 480-frame WASAPI buffers: Gainer ran 21.64 seconds
(2,165 callbacks, maximum 535.7 microseconds), Contourtonist 21.73 seconds
(2,173 callbacks, maximum 884.6 microseconds), and OrbitCab 22.25 seconds
(2,228 callbacks, maximum 1,110 microseconds). Each recorded zero deadline
overruns, starvation indicators and device/MMCSS errors. The VST3 editors stayed
open during the 20-second observation portion. These bounded silent-output runs
establish callback continuity and state behavior, not physical sound quality.

Logs: `bin/windows-live-parameter-{build,regressions,worker-tests,hosted-tests,
app-tests}.log`. The rendered queue test also has a detailed PCM log at
`bin/windows-live-parameter-tests.log`.

## Remaining limits

- Opaque state/program/IR changes, structural rack edits and plugin Undo/Redo
  still stop playback. API parameter commits close baseline editor windows;
  preserving those windows across API edits needs further work.
- The installed plugins exercised here are effects. Real Windows instrument
  discovery, trigger creation, MIDI notes, bus routing and full lifecycle
  qualification remain required, alongside presets/library and missing-plugin UI.
- OrbitCab's previously measured offline partition discrepancy remains open.
  Passing live continuity/state tests does not resolve or waive that finding.
- These runs do not establish speaker sound, physical latency, long-session
  capacity, 60 Hz presentation, x64 behavior, a full realtime audit or reciprocal
  Windows/Mac project reopen.
