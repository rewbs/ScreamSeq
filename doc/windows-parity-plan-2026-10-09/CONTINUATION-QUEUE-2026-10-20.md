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

The next Windows candidate is frozen at
`b1581c9c8bc7d776d637d8c6ac094e8c3db7780a` in the qualification worktree.
It combines recipe preview ownership, sample release/loop gestures, the corrected
sidebar fixture and zero-duration file defaults. Seven native build targets,
the selected native/application checks, and exact fixture/API/codec/duration
checks are prepared under `bin/recipe-sample-20261020/`. Its build gate is
**20 October 04:50 UTC**. A build or test pass must be recorded before upgrading
any of those source-only claims.

The context-panel binary at `e79133155` has separate bounded evidence: seven
native groups passed, one native context fixture failed, one actual-app context
case passed, and five subsequent keyboard/browser cases passed. See
[context evidence](CONTEXT-PANEL-2026-10-19.md). This is not qualification of
the newer source or a visual review.

## Dependency-ordered remaining work

| Priority / effort | Confirmed work or gap | Concrete next action and gate |
| --- | --- | --- |
| 1 / M | Frozen batch remains uncompiled; shared preview paths also change Mac bridge/audio adapters. | Run the single prepared ARM64 build and its targeted checks. Resolve actual failures without changing assertions to manufacture a pass. Obtain Mac/x64 execution at the next permitted build opportunity; retain both apps on the same integration line. |
| 2 / S–M | Mac `TrackerSessionAPI.inc` excludes `dryRun` from `plugin.bypass` and `plugin.parameters.set`; Windows `PluginOperations.cpp` accepts it. The API corpus explicitly retains `API-PLUGIN-DRY-RUN`. | Add validated, side-effect-free Mac dry runs using existing shared parameter preparation and accepted manual values. Preserve active preview ownership, queues, Redo, revision, opaque bytes and transport. Check changed and no-op batches, full-batch rejection, unavailable/read-only targets, stale guards and subsequent real edits. Do not count a method-name match as parity. |
| 3 / M | API numeric-type and rejected-request-ID reuse policies still differ. `editor/Tests/Conformance/api-vectors.json` retains `API-INTEGER-TYPES` and the rejected reuse discrepancy. | Define one API-number/replay contract before changing adapters. Inspect `mac/Bridge/AutomationValidation.hpp`, `mac/App/AutomationServer.swift`, Windows API validators and `windows/Api/PipeServer.cpp`. Keep boolean rejection, finite/integral/range checks, stable string identities, uncertain-result protection and legitimate busy retries. File-decoder numeric rules are a separate compatibility contract. Update vectors only with both hosts' new execution evidence. |
| 4 / L | Unknown root/native/entity extensions are retained by Windows and dropped by canonical Mac save with warnings/source protection. This is observed in `doc/api/codec-observations.json` and still present in source. | Converge lossless preservation, then reciprocal edited-file qualification. See the ownership plan below. Do not copy Mac's loss into Windows to make the baseline equal. |
| 5 / M–L | The main integration contains mixer, tracks, graph gestures, plugin/sample layering, automatic fields and recorder-duration work. Dated P1–P5 absence claims are superseded; their complete acceptance gates are not. | Reconcile each A02–A14/A17 journey with exact candidate evidence. Reuse unchanged targeted results, but execute changed input paths and the final complete composing journeys. Direct recipe preview, sample gestures and sidebar changes specifically invalidate their earlier interaction claims. |
| 6 / M | Native control/event checks do not establish visual ergonomics, DPI, UIA or physical input quality. Computer use remains stopped after the user's Escape. | Retain A16/P8 as open. Resume foreground inspection only with renewed authorization. Check 100/150/200% DPI, compact/normal layouts, focus visibility, hidden tab stops, high contrast and Narrator on the usable candidate. Avoid further speculative UI redesign based on screenshots alone. |
| 7 / L, equipment-dependent | Real vendor/device lifecycle and physical audio/MIDI remain separately unqualified. Optional unavailable provider tests are skips, not passes. | Use explicit fixture providers/devices; preserve musician sessions and audio defaults. Qualify architecture mismatch, editor lifecycle, latency changes, reconnect, capture/overflow and physical measurements with their existing strict limits. Keep silent private-desktop observations separate from audible evidence. |
| 8 / M | Shared-main final gate and reciprocal edited files remain open. | Run both native platforms from the integrated candidate, reciprocal F01–F05 and edited outputs, sustained display/audio checks, full composing journeys and compatibility/error paths. Confirm source/binary fingerprints and clean main integration before claiming parity. |

Effort is relative implementation/qualification scope, not elapsed-time estimates
or percent complete. Priorities 2–4 can be cohesive subsequent batches; they must
not delay investigation of a failure in the frozen batch.

## Unknown-metadata ownership plan

Confirmed source boundaries:

- Windows `windows/Project/ProjectPreservation.hpp` contains exact stored-value
  comparison, known-schema identity matching, canonical promotion and three-way
  merge. It refuses ambiguous edits to arrays with opaque extensions.
- `NativeMetadataRecovery.inc` preserves understood canonical fields and warns
  about uninterpreted extensions; `NativeProject.cpp::nativeProjectTree`
  merges against the captured pre-edit baseline and validates the actual saved
  tree before staging atomic replacement.
- Mac `TrackerSession.mm::decodeProject` warns about unknown root fields,
  `ProjectLoadRecovery.inc` reports unknown native fields as lossy, and
  `projectDataForRecovery:` constructs a new root with encoded native metadata.
  Merely changing a warning would not preserve the dropped contents.

Proposed boundary: share preservation policy, stable-identity matching and merge
decisions in `editor/`, while retaining native property-list I/O, atomic file
publication and session lifetime in each platform adapter. Mac must retain its
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
