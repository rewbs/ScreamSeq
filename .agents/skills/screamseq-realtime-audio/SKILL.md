---
name: screamseq-realtime-audio
description: Change or debug ScreamSeq audio rendering, precise notes, plugin hosting, modulation, routing and latency compensation with realtime-safe implementation and rendered-audio evidence.
---

Find the checkout, then inspect `editor/TrackerDocument.hpp`, `editor/SignalGraph.hpp`, `editor/SignalRuntime.hpp`, and the relevant platform host. On macOS, `mac/Audio/NativeInstrument.cpp` connects the shared renderer to native plugins, automation and mixer adapters; `NativeSignalGraph.cpp` owns graph processor copies.

The audio callback cannot allocate, free, lock, log, touch UI, parse scripts, discover plugins, serialize state or construct processors. Prepare bounded buffers, schedules, formulas and graph plans on the control thread. Publish safely and retire old objects off the callback. Use the existing realtime allocation/free/lock audit; a passing functional test alone does not establish realtime safety. Vendor-private allocations are outside the host's audit guarantees.

Keep playback compatible when native features are absent. Use stock OpenMPT PCM comparisons across rates and callback sizes after changes to `soundlib/`. Native extensions are guarded by `OPENMPT_EDITOR_CORE`. Exact timing belongs to the engine clock: precise notes/graph commands use 65536 units per row; automation curves currently use 256. Convert musical coordinates with the actual pattern beat signature. MIDI recording maps incoming timestamps through `RecordingClock`, not UI delivery time.

Trigger a precise note only on its target voice/channel; do not fake whole-engine ticks. Onset effects apply at the onset; continuing tracker effects retain the ordinary tick clock. Account for note-off/cut, instrument NNA, repeated identical-note generations, loop entry and sample offset boundaries. Per-hit effects are validated against both the safe note-local whitelist and module format.

VST3 supports parameter queues with sample offsets inside a processing block. Send ramp endpoints through bounded queues; don't process 1-sample VST3 buffers to simulate slides. Preserve endpoint precision for partition-independent results. Test AU, VST3, built-ins, samples and instrument events separately because their automation/event contracts differ. Stable parameter IDs and native-versus-normalized values must not be confused.

Graph invariants: independent processors per target and row/persistent/ordinary role; row graphs precede persistent graphs, then the ordinary graph and regular inserts. A then B means A→B; repeating Start updates the existing entry. Inactive graphs process silence continuously, including sidechain inputs, while transport advances. Stop cuts graph output by default; optional tails remain at the retained stage. Downstream processors retain already-received audio. Preserve bypass compensation and validate latency-bearing reorder transitions explicitly. Reject implicit zero-delay cycles.

Sample documents retain exact native sample payloads; module export may not represent them. Do not infer stored precision from float WAV export. Exercise import/save/reopen/Undo and audible sample onset/loop behavior when changing sample storage or rendering.

Use rendered fixtures to measure onset placement, independent channels, pan/volume, discontinuities, tails, sidechain isolation, latency and callback partition invariance. Add targeted sanitizer cases for lifetime/index changes. Report rate, buffer size, duration, fixture and limitations with performance results; do not extrapolate a gain-fixture benchmark to commercial plugin capacity.
