# Graph completion — active work log

This is an implementation log, not a release qualification or a paused report.
The active scope is items 2–6 in
[the paused checkpoint](../2026-10-01-graph-resume/STOPPAGE_REPORT.md).
The user explicitly deferred the FPS gate. Other correctness, realtime, native UI
and audio checks remain required. Starting integration commit: `b4680f32f` on
`main`, after the independent-repository cleanup.

## Confirmed decisions

- A channel's new note/MIDI cable forwards **plugin-instrument notes only**.
  Sample-instrument notes remain sample playback.
- A destination connected during playback **waits for the next note-on**.
  It does not receive synthetic held notes. Removing a route releases its owners.
- Space keeps playback; Tab keeps navigation. Edits to reusable definitions update
  all uses. These prior approved decisions continue to apply.
- Windows desktop/device targets were offline at preflight. The user explicitly
  chose **native CI for this pass; desktop checks later**. No desktop result is
  inferred from the CI or portable proxy tests.

## Implemented checkpoints (not all integrated/qualified yet)

| Area | Current source / evidence |
|---|---|
| Note routes | Shared stable-ID model, implicit-assignment suppression, channel remap, bounded held-generation ledger and publication hooks. Mac/Windows codecs and guarded editing APIs are added. Native event wrappers preserve instrument/channel/NNA context. Exact adopted event counters and typed Notes UI are integrated. Native AU/VST3 render, telemetry, API/history/persistence and copied AppKit checks pass; final real UI walks remain. |
| Note ownership | Portable test passes fanout, no replay on add/Undo, overlapping route ownership, removed/silent generation FIFO, NNA moves, pedal ownership and atomic capacity rejection. Native plugin, alias and precise-pattern regression suites pass with the new wrappers. |
| Newly watched recipe note envelopes | Targeted held/released/NNA/sample/inspector checks pass across 17/512/4096-frame buffers; host realtime audit reports no allocations, frees or locks. |
| Source mute | True contribution suppression preserves running clocks and source state. Targeted portable/native and history tests pass. |
| Structural live edits | Retained per-copy endpoints and latency-aligned dry transitions pass AU/VST3 topology-add/remove/reorder/Undo checks, including a simultaneous new held-note source. Arbitrary-port/latency and new source/assignment support remain in progress. |
| Graph actions | Source mute, exact-use independence, selection clipboard/duplicate, preset actions and explicit branch healing are being integrated with shared APIs and native UI tests. Full final UI rewalk remains outstanding. |
| Recorded automation | Bounded replaceable absolute timeline plus native transaction adapters are implemented. Shared timing, partition, realtime and manual-edit ordering-fence regressions pass. |
| Windows publication | Graph/routing publication, history, recorded editing, codecs and API adapters are implemented. Native worker tests are staged; full native execution of these changes remains outstanding. |

## Golden-engine checkpoint

The editor-only source-context bridge for MIDI macros passed all 30 stock PCM
comparisons at 44.1/48/96 kHz, with **exact equality** and zero intercepted callback
allocations, frees or mutex calls. This covers the core extension; it is not a
qualification of the still-changing host topology code.

- [Core fixture checks](logs/note-routing-core-tests.log)
- [Stock PCM comparisons](logs/note-routing-stock-pcm.log)
- Oracle: pinned original OpenMPT `f83cedb0cd5446e4dfaa83ac97e3087107e26767`.

## Integrated note/action checkpoint

The ten targeted suites passed in 11.12 seconds: portable note ownership, recorded
automation, native AU/VST3 note routing, sample-accurate routed pitch, native mixer
and graph topology, graph API/history, graph editing, and the Mac-hosted Windows
graph/metadata adapters. See [the log](logs/note-telemetry-shared-tests.log) and
[binary hashes](logs/note-telemetry-evidence.json). Windows-native compilation is
still separate. Copied AppKit Notes/action tests pass, including exact cable
identity, mixed audio/note cuts and pending/retired event readings.

The opt-in `commercial-graph-qualification` tool now builds. Replika VST3 passed
a 10-second offline render with rack removal/restoration, failed-preparation
retention and bypass at 48 kHz / 512 frames. This is not device timing evidence.
The first Serum probe exhausted the small test-worker stack: the harness had
placed the large host on its stack instead of matching the application's heap
ownership. The harness is corrected and ledger event staging is now preallocated off
the callback stack. The three affected note/pitch suites pass, including a
64 KiB worker-stack regression. Serum 2 VST3 then passed its 10-second musical
render with live note-route suppression/restoration and channel mapping. No musician
process or system audio route was involved.

## Qualification work still required

- Finish arbitrary live topology, source/assignment, latency, port-layout and
  N-channel cases, including preparation-failure rollback and owned endpoint retirement.
- Complete exact per-copy audio/control/event observation, group boundaries/bypass,
  and the remaining context/catalog/branch actions.
- Rebuild a coherent final source snapshot and walk the real Mac UI, recording
  screenshots and journey actions/switches/confirms.
- Run the final shared/native/API/AppKit suites, stock PCM comparisons after the
  editor-only macro source-context extension, and focused sanitizer cases.
- Run current-runtime BlackHole loopback and commercial plugin transitions; retain
  source/executable hashes, rate/buffer/fixture details and bounded claims.
- Run native Windows CI. Desktop/device checks are explicitly deferred by the user.
- Publish a consolidated report separating implemented, tested and pending items.

BlackHole 2ch was present at preflight (48 kHz). The system defaults pointed at
Bluetooth audio; no system default was changed. No musician ScreamSeq process was
running at that preflight. Tests must continue to use a distinct QA bundle/process.
