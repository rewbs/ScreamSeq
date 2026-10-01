# Windows reusable graph editor — 2026-09-21

This checkpoint adds a native reusable graph canvas and independent graph-plugin
recipe editing. It is progress toward full Mac parity, not a parity release.
Upstream remains `bcfe0f8a7`; a fresh fetch found no newer default-branch commits.

## Implementation

- The Graph dock creates, clones and deletes reusable definitions; adds real
  rack effects and modulation sources; edits node properties; and assigns a
  graph to an actual mixer bus with retained amount/wet fields.
- Cached node, socket and sampled-wire geometry drives both D2D drawing and hit
  testing. Dragging an output socket adds an audio/modulation wire, selecting an
  existing wire exposes its properties, and edits preserve neighboring wires.
  Nodes support drag, arrow movement, Escape cancellation, fit, zoom and pan.
  Painting does not query Document, the worker or plugin instances.
- A captured definition draft survives navigation and external song edits.
  Apply uses the original revision and one document Undo transaction. Invalid
  cycles, stale revisions and rejected assignments retain the unfinished draft.
  Field target changes are guarded; asynchronous catalog replies cannot replace
  newly typed fields or parameter controls for a different node.
- `graph.plugin.get/set` follows the Mac parameter/bus metadata interface.
  Whole parameter batches, supported auxiliary ports and the resulting graph
  are validated before committing. Disposable instances leave rack baselines
  and plugin history untouched; graph recipe changes use document history.
- `graph.plugin.editor.open/commit/close` owns an independent native VST3 draft.
  Commit checks the token, graph/node and unchanged captured recipe. Closing the
  native window updates editor presence but retains its draft for explicit
  commit. Document replacement disposes it. Dry runs and no-ops preserve history.

The lower dock currently switches among editors. The contextual workspace API
still exposes only its supported notes/samples panels; `graphEditor` is separate
retained state. `unavailable` now distinguishes song overview and graph curves
from the implemented reusable canvas.

## Build and evidence

ARM64 Release executable: `bin/windows-parity/Release/ScreamSeq.exe`.
SHA-256:
`CD3E2111D7192CEEBFA43B30DCD29B07694B8EB99936231A5E9F3F57EB4B01F3`.
The packaged checkpoint is `bin/windows-checkpoints/graph-editor-20260921/`;
its manifest records the exact source commit, tree and packaged file hashes.

- All **99 app tests passed** in one final run, 125.036 seconds, with installed
  effect/instrument caches, provider fixtures and opt-in live audio enabled.
  There were no skipped cases. The private desktops preserve the user's
  foreground window and clipboard sequence and terminate only owned processes.
- Seven graph tests cover socket dragging, wire edits, drag cancellation,
  stale drafts, parameter/port validation, independent rack state, Undo/Redo,
  save/reopen, actual mixer assignments and 900×620 native-control bounds.
  OrbitCab's real editor initialization, native window close, draft commit and
  conflicting recipe rejection are exercised through the application.
- All **29 CTests passed**, 15.80 seconds. These include 26 portable cases and
  three hosted/project cases. Their existing allocation checks are not a full
  host realtime allocation/free/lock audit.
- The worker renders an assigned graph Gainer at 44.1/48/96 kHz with block sizes
  17/128/4096/8193. A −12 dB recipe edit matches the independently scaled baseline
  with maximum PCM delta **7.43264e-9** (limit 1e-6). The same test verifies
  independent rack state, exact Undo PCM and saved-project reopen.

An earlier 99-case run passed 98 cases but failed the existing native rack Add
test; it passed immediately in isolation. A sent timer can return inside another
inspector refresh's nested message pump. The test's timer helper now follows it
with an API read, whose dispatch waits for that refresh to finish, before sending
the next button click. This is synchronization, not a retry of a musical edit.
The final clean run includes that correction and the assignment-draft fixes.

Primary logs:

- `bin/windows-graph-final-build.log`
- `bin/windows-graph-final-app-tests.log`
- `bin/windows-graph-ctests.log`
- `bin/windows-graph-final-pcm.log`
- `bin/windows-graph-qualified-app-tests.log` (retained earlier failure)
- `bin/windows-graph-plugin-evidence/live-parameters.json`

Contourtonist, OrbitCab and Surge XT remain privately installed, with provenance
and earlier lifecycle evidence in `UPSTREAM_PLUGIN_QUALIFICATION.md`,
`LIVE_PLUGIN_PARAMETERS.md` and `TRIGGER_INSTRUMENT_PROGRESS.md`. This app suite
exercises all three. Its live parameter dwell is one second per stage, so it
does not establish long-session capacity or reliability.

## Remaining parity work

Song routing overview, automation curves/formulas and graph command lanes,
graph plugin browsing/presets/native auxiliary-port controls, instrument graph
assignment UI, envelope/bank editors, MIDI/recording/recovery, device selection,
simultaneous/floating/persisted docks and accessibility remain incomplete.
Foreground visual comparison and sustained 60 Hz qualification remain open;
private-desktop geometry tests are not that evidence.

Live structural/opaque-state replacement, OrbitCab's existing partition
discrepancy, Surge's first-open opaque zoom change, fresh reciprocal Mac
format-17 reopen, x64 and long loaded realtime qualification also remain open.
