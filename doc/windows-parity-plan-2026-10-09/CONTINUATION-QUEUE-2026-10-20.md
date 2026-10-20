# Current continuation queue

This is a dependency queue, **not a claim that the 117-row reference inventory
has been fully qualified**. The original A01–A18 acceptance requirements and
fixture provenance remain in [README](README.md) and `parity-matrix.csv`.
Historical “not implemented” or “paused” statements in dated phase reports
must not override current source or the active goal.

## Current integration boundary

Remote main was checked on 20 October and remains
`07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`. Its existing CI runs were queried
again, without triggering a build:

| Platform | Run / job | Current authoritative result |
| --- | --- | --- |
| Windows x64 | 39349699781 / 118390718402 | Completed, failed. Build, portable/worker and native editor groups succeeded; application and conformance steps failed. |
| Mac Intel | 39349699645 / 118390718018 | Completed, failed. Build, interface, library and conformance steps succeeded; model/host step failed. |
| Mac Apple Silicon | 39349699645 / 118390718187 | Completed, failed. Same step-level disposition as Intel. |

The logs and diagnoses already recorded in
[plugin continuation](PLUGIN-EDIT-BATCH-2026-10-19.md) remain relevant; this fresh
status query does not independently rerun or diagnose those tests. Local fixes
and passing tests on retained binaries do not turn those CI runs green.
The returned job summaries are retained in
`bin/parity-queue-20261020-ci.json`; original runs:
[Windows](https://github.com/rewbs/ScreamSeq/actions/runs/39349699781) and
[Mac](https://github.com/rewbs/ScreamSeq/actions/runs/39349699645).

The Windows candidate is frozen at
`b1581c9c8bc7d776d637d8c6ac094e8c3db7780a` in the qualification worktree.
It combines recipe preview ownership, sample release/loop gestures, the corrected
sidebar fixture and zero-duration file defaults. Seven native build targets,
the selected native/application checks, and exact fixture/API/codec/duration
checks are retained under `bin/recipe-sample-20261020/`.
[Qualification](RECIPE-SAMPLE-QUALIFICATION-2026-10-20.md) records a successful
04:50–04:53 UTC build, 16/17 native groups passed, 21 application cases passed,
two provider skips and successful fixture/API/codec/duration checks against
their pinned baselines. The native metadata test failed on an NF/NR field
assumption; a test-only correction is prepared but unbuilt. The next build gate
is **20 October 05:53:34 UTC**.

The context-panel binary at `e79133155` has separate bounded evidence: seven
native groups passed, one native context fixture failed, one actual-app context
case passed, and five subsequent keyboard/browser cases passed. See
[context evidence](CONTEXT-PANEL-2026-10-19.md). This is not qualification of
the newer source or a visual review.

## Dependency-ordered remaining work

| Priority / effort | Confirmed work or gap | Concrete next action and gate |
| --- | --- | --- |
| 1 / M | Frozen ARM64 batch has bounded passing evidence and one failed native metadata test. Shared preview paths also change Mac bridge/audio adapters. | Rebuild and rerun the corrected metadata test at the next permitted opportunity; preserve full-model and unit assertions. Obtain Mac/x64 execution; reuse unchanged passing evidence and retain both apps on the same integration line. |
| 2 / S–M | At the frozen candidate, Mac `TrackerSessionAPI.inc` excludes rack `dryRun`; Windows accepts it. The later [Mac correction](RACK-DRY-RUN-2026-10-20.md) is source only, with a shared contract passed on the existing Windows app. The API corpus still retains `API-PLUGIN-DRY-RUN`. | Qualify Mac dry runs using existing shared parameter preparation and accepted manual values. Preserve active preview ownership, queues, Redo, revision, opaque bytes and transport. Check changed and no-op batches, full-batch rejection, unavailable/read-only targets, stale guards and subsequent real edits. Do not count a method-name match as parity. |
| 3 / M | [Shared API integer validation](INTEGER-API-BATCH-2026-10-20.md) and [terminal request replay](REQUEST-REPLAY-BATCH-2026-10-20.md) are prepared but unbuilt. The retained Windows app reproduces both differences. | Qualify numeric bounds/adapters, terminal-error retention, busy retries, uncertain side effects, exact cache accounting and both shared app contracts in the next batch. API vectors retain the old baseline until both hosts execute. Generic host failures now require reconciliation without advertising the pre-write revision; prove that behavior with the native real-pipe cases. |
| 4 / L | Unknown root/native/entity extensions are retained by Windows and dropped by canonical Mac save with warnings/source protection. The [shared typed foundation](PROPERTY-TREE-BATCH-2026-10-20.md) is now prepared but unbuilt; Mac session integration remains open. | Connect staged Mac load/save/recovery ownership to the shared codec/merge and native data bridge. Preserve XML imports, accepted recovery trees, budgets and opaque plugin state; then execute reciprocal edited-file checks. Do not copy Mac's loss into Windows to make the baseline equal. |
| 5 / M–L | The main integration contains mixer, tracks, graph gestures, plugin/sample layering, automatic fields and recorder-duration work. Dated P1–P5 absence claims are superseded; their complete acceptance gates are not. | Reconcile each A02–A14/A17 journey with exact candidate evidence. Reuse unchanged targeted results, but execute changed input paths and the final complete composing journeys. Direct recipe preview, sample gestures and sidebar changes specifically invalidate their earlier interaction claims. |
| 6 / M | Native control/event checks do not establish visual ergonomics, DPI, UIA or physical input quality. Computer use remains stopped after the user's Escape. | Retain A16/P8 as open. Resume foreground inspection only with renewed authorization. Check 100/150/200% DPI, compact/normal layouts, focus visibility, hidden tab stops, high contrast and Narrator on the usable candidate. Avoid further speculative UI redesign based on screenshots alone. |
| 7 / L, equipment-dependent | Real vendor/device lifecycle and physical audio/MIDI remain separately unqualified. Optional unavailable provider tests are skips, not passes. | Use explicit fixture providers/devices; preserve musician sessions and audio defaults. Qualify architecture mismatch, editor lifecycle, latency changes, reconnect, capture/overflow and physical measurements with their existing strict limits. Keep silent private-desktop observations separate from audible evidence. |
| 8 / M | Shared-main final gate and reciprocal edited files remain open. | Run both native platforms from the integrated candidate, reciprocal F01–F05 and edited outputs, sustained display/audio checks, full composing journeys and compatibility/error paths. Confirm source/binary fingerprints and clean main integration before claiming parity. |

Effort is relative implementation/qualification scope, not elapsed-time estimates
or percent complete. Priorities 2–4 can be cohesive subsequent batches; they must
not delay investigation of a failure in the frozen batch.

## Unknown-metadata ownership plan

Confirmed source boundaries:

- Shared `editor/project/ProjectPreservation.hpp` contains exact stored-value
  comparison, known-schema identity matching, canonical promotion and three-way
  merge. The Windows path forwards to it. It refuses ambiguous edits to arrays
  with opaque extensions; the newly prepared plugin-instance identity support
  and Mac data bridge await compilation.
- `NativeMetadataRecovery.inc` preserves understood canonical fields and warns
  about uninterpreted extensions; `NativeProject.cpp::nativeProjectTree`
  merges against the captured pre-edit baseline and validates the actual saved
  tree before staging atomic replacement.
- Mac `TrackerSession.mm::decodeProject` warns about unknown root fields,
  `ProjectLoadRecovery.inc` reports unknown native fields as lossy, and
  `projectDataForRecovery:` constructs a new root with encoded native metadata.
  Merely changing a warning would not preserve the dropped contents.

The typed codec and preservation policy have moved into `editor/project/`;
the [foundation batch](PROPERTY-TREE-BATCH-2026-10-20.md) records the remaining
native integration. Retain atomic file publication and session lifetime in each
platform adapter. Mac must retain its
original property tree and a canonical baseline for the loaded document; reset
or replace them only with successful document replacement/save. Failed loads,
failed saves, Undo/Redo, recovery snapshots and edited identities need explicit
ownership tests. Keep Foundation types out of the shared model and avoid textual
JSON conversion of opaque values.

Before moving the Windows helper, inventory its typed leaf representations and
Foundation equivalents: binary bytes, signed/integral/real values, signed zero,
Unicode, dates and unsupported plist objects. Either a typed shared value adapter
or a narrow tree-policy interface is reasonable; neither may silently coerce
unknown values. Preserve unknown fields without interpreting them as routing or
DSP instructions, retain appropriate warnings/source protection, and refuse
unsafe identity merges instead of guessing by array position.

Acceptance must include unknown root/native/track/graph-node fields from the
existing codec corpus; reorder/delete/insert by stable identity; unrelated edits;
opaque unavailable AU/VST3 state; failure before atomic save promotion; and
Mac→Windows→edit→Mac plus Windows→Mac→edit→Windows. The existing exact typed
comparator and malformed-file protections remain mandatory. A successful
canonical reopen alone cannot establish lossless preservation.

No additional product-design decision is required for the current frozen batch.
Foreground inspection authorization and specific physical equipment availability
are later qualification inputs, not reasons to stop current code work.
