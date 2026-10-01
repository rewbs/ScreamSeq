# Mac-parity continuation — paused by user

**PAUSED: read `PAUSED_HANDOFF.md` for the stopping checkpoint and unprocessed
completion results.** The entries below record work history, not permission to
continue or a claim that described workers remain running.

This file records unfinished integration work, not a completed checkpoint.
The immutable `bin/windows-checkpoints/workspace-preview` remains the prior
read-only demo. Continue in `codex/windows-native`; do not discard or commit the
uncommitted tree without authorization.

## Current work

- Native binary-plist codec (`Project/BinaryPlist.*`) compiled and tested on ARM64
  with bounded parsing, exact supported value types and opaque date/UID support.
  There is no Python runtime dependency in the app.
- Native metadata versions 1–14 (`Project/NativeMetadata*`) restore the existing
  shared `Tracker::NativeSong`, using shared graph/mixer/envelope validation.
  The canonical writer emits metadata 14; the original typed tree is retained
  outside the model to preserve unknown properties.
- `Project/NativeProject.*` now loads the supplied **actual Mac file** into the
  actual shared Document, restores its native metadata against its snapshot, and
  saves native projects through the C++ codec and atomic Windows publication.
  Independent review found that editing historical metadata could promote its
  version without materializing all required fields, producing an unreadable
  save; it also found stale recovery-take compatibility after edits. Both are
  blocking and under focused repair, including validation of the actual merged
  persisted tree and recovery provenance that survives saves.
  Tests check exact sample PCM, all known native data, plugin/opaque state,
  unknown fields, a real pattern edit, save/reopen, Undo, malformed references
  and failed-save preservation. They do not prove Mac app reopening/audio parity.
- `Project/ProjectPreservation.hpp` performs a three-way merge using stable IDs.
  Unknown-bearing arrays without a stable identity reject ambiguous edits rather
  than silently dropping or retargeting unknown data. Integer/float types and
  signed zero are compared deliberately rather than loose numeric equality.
- `Project/ProjectIO.hpp` uses Unicode paths, bounded reads, same-directory
  exclusive staging, flush and atomic publication. Concurrent no-overwrite
  writers are tested: only one can publish a complete destination.
- The separate `bin/windows-editor/Release/ScreamSeq.exe` accepts
  `--project <file> --inspection --automation`. The actual app test verifies the
  supplied file's four precise notes, two graphs, three bank templates, seven
  instruments and rack record—not the ordinary generated demo. Native Open/Save,
  ordinary tracker edits/history and the core/Timeline pipe methods now run on
  the document worker. Parent reran all 14 actual-app editor tests, then used the
  live native GUI to enter a note and click Undo, verifying both through the
  exact-PID pipe. API Redo/save/disk reopen retained the change in a disposable
  copy; the original reference hash stayed unchanged. Independent app review
  found a P1 non-atomic failed-open/cache-publication path plus P2 legacy-charset
  and chooser-focus defects. Fixes and regressions are active; approval is
  withheld. The parent also confirmed sequence names were published as arrays
  instead of strings. Cache budgeting/reuse and byte-bounded request deduplication
  are being corrected with the same round of work. This is not lossless-editing
  or full-parity approval. See
  `App/INTEGRATION.md` and `bin/windows-editor/qa-live-edit-readback.json`.
  One early delegated clipboard restoration attempt was unverified (user told);
  subsequent inspection tests use a private clipboard. Parent QA did not touch
  the desktop clipboard. A transient UIA recapture timeout did not block pipe
  responses; accessibility remains unqualified.
- `TrackerHosted` is now extracted into `editor/hosted/`, sharing the actual
  rack, mixer, graph, precise-note and automation orchestration with Mac. Parent
  reran its seven offline tests and rebuilt/reran them in
  `bin/windows-hosted-current` against the corrected shared snapshot libraries.
  Built-in effect/graph DSP is real. Independent extraction review passed without
  concrete regressions; native Mac compilation/vendor fixtures remain open.
  The parent strengthened endpoint tests to inspect exact double values at
  offsets 0 and 4095, rather than infer precision from float PCM tolerance; all
  seven hosted tests pass after that change. Deterministic backend fixtures do
  not prove vendor loading. App project playback remains guarded until the preparation and
  render path are integrated. The Windows VST3 provider, native HWND editor host
  and isolated cached scanner now pass five standalone CTests against real
  compiled fixture DLLs; parent reran them. Independent review nevertheless found
  seven reproduced defects: lost modal-loop dispatcher work, optional controller
  state handling, failed-initialization cleanup, Created-phase call order,
  parameter-enumeration errors, editor sizing and over-limit cache publication.
  Backend/UI and cache fixes now have separate owners and regression gates;
  VST3 approval is withheld and it is not integrated into the application.
  Commercial plugins, x64/ARM64EC bridging, runtime crash isolation and MIDI-output
  routing are not qualified/provided. AU remains unavailable and retained. Mac
  compilation, device testing and realtime auditing remain unqualified.
- `Session/DocumentOperations.*` has passed its standalone actual-document tests
  for core edits, document history, structure and sample reads. Its diagnostic
  exposed shared snapshot loss for long titles and absent pattern slots. The
  shared `RSCORE1` sample-archive correction now fixes those losses and also
  preserves pattern settings/orders. Parent reruns passed 24 portable CTests,
  the three snapshot/editing/probe CTests and the actual Mac fixture case; the
  strict probe retains the 100-byte title and an unallocated slot. Independent
  review found two additional P2 gaps: semantic cell equality omitted dormant
  volume/parameter bytes, and module export did not check pattern-name charset
  interpretation. Both now have exact shared comparisons and isolated red/green
  regressions across the applicable formats. Parent reran the three snapshot,
  editing and probe CTests with the actual Mac fixture; all passed. Independent
  re-review is active, so approval remains withheld until its verdict. Older Mac binaries reject this
  extension when emitted; a Mac rebuild/reopen remains unverified. Do not qualify
  full app editing or old-binary interchange from wrapper tests alone.
- `Session/TimelineOperations.*` now passes actual-model tests for precise-note
  replacement/legacy clearing and per-hit effects, fractional timing and groove,
  shared formula preview/reference, no-op/dry-run/history and atomic invalid
  requests. Review found that unchanged noncanonical precise-event ordering
  could create history and stop playback; a shared helper and both Windows/Mac
  bindings are being corrected, with actual no-op/Redo regressions. See
  `Tests/Timeline/README.md`. It is not a curve editor or hosted playback
  implementation.
- `Session/HostedProject.*` now prepares the actual Mac project's plugin records,
  native model and renderer through TrackerHosted. Its offline test produced real
  finite/nonzero PCM from the supplied project, and built-in saved-state renders
  passed callback-partition checks at 44.1/48/96 kHz. Independent bridge review
  passed. Parent added and ran audible half-second absolute-automation,
  nonzero-cursor and late-failure/detached-owner lifetime regressions; all passed.
  The bridge is not yet connected to WASAPI/application playback. No full-song tail or Mac
  audio-equivalence claim follows from the tested one-second window.
- `Session/GraphOperations.*` now passes its eight actual-document CTest groups;
  `Session/AssetOperations.*` passes its CTest with 17 scenario groups. Parent
  reran both suites. These are document/API bindings, not a graph UI, plugin
  runtime or application save/reopen qualification. App dispatch and real graph
  host hooks are being integrated separately.
- All four shared sample/instrument import paths now pass ASCII and Unicode
  probes, independently rerun by the parent. The asset layer qualifies sample
  import, batch sample import and multisample import using staged shared decoding.
  The missing/external-SFZ crash and partial-import gate now have shared decoder
  fixes: bounded Vorbis/MO3 progress and editor-only strict SFZ rejection.
  `instrument.import` is enabled in the operation layer. Parent reran all six
  asset/decoder CTests plus all 24 portable core tests successfully. Independent
  decoder and asset-operation reviews are active; arbitrary SFZ fidelity,
  sanitizer coverage and app registration remain unqualified. No assertion
  suppression or alternate Windows importer is used.
- `Session/EnvelopeOperations.*` now passes 22 model/file CTests, independently
  rerun by the parent. The Mac-schema JSON catalogue gap is implemented: new
  catalogues default to UTF-8 JSON; existing JSON/plist retain their encoding,
  with typed preservation, bounded preflight and read-back-valid publication.
  Linked masters, independent copies, fitting/baking, Undo, stale/no-op guards and
  real writer contention remain covered. Fixtures are generated schema oracles,
  not a Mac-produced catalogue or Mac application reopen. Independent envelope
  and graph reviews are active; host hooks and app/UI registration remain pending.
- Parallel owners are fixing app publication/charset/focus and cache limits,
  dedupe byte limits, shared snapshot conservation, legacy metadata/recovery,
  precise-note no-ops, external-instrument decoding and Windows VST3 lifecycle/cache
  defects. Do not edit their files
  until their results are delivered and reviewed. Persistence and timeline code
  have also been submitted for independent review. No review approval or complete
  parity is implied by an in-flight task.

## Entry points

```powershell
.\windows\build.ps1 -BuildDirectory "$PWD\bin\windows-editor" -Test
$env:SCREAMSEQ_TEST_EXE = "$PWD\bin\windows-editor\Release\ScreamSeq.exe"
$env:SCREAMSEQ_REFERENCE_PROJECT = 'C:\path\to\reference.screamseq'
python -B windows/Tests/test_native_project_app.py -v
```

`native-project-tests.exe` takes the actual reference path and an **isolated
scratch directory**. `project-preservation-tests.exe` is self-contained.
Standalone codec, metadata and I/O test harnesses are under
`windows/Tests/{ProjectNative,Metadata,ProjectIO}`. Reused portable core tests
still pass in the separate editor build. Tests are functional evidence, not
ASan, full realtime auditing or sustained frame-pacing qualification.

The full feature-parity task remains active: app editing/history/save, complete
sample/instrument/automation/graph workflow, VST3, MIDI, docking/accessibility,
recovery and all final qualification gates are not yet complete.
