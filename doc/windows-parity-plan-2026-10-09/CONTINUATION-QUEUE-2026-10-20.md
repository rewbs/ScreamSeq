# Current continuation queue

This is a dependency queue, **not a claim that the 117-row reference inventory
has been fully qualified**. The original A01–A18 acceptance requirements and
fixture provenance remain in [README](README.md) and `parity-matrix.csv`.
Historical “not implemented” or “paused” statements in dated phase reports
must not override current source or the active goal.

## Current integration boundary

**Latest checkpoint:** candidate `8c750f7d61966b3dc125ffaf49c34e8678697a25`
built successfully on Windows ARM64 at 07:00–07:01 UTC.
[Commit-outcome qualification](COMMIT-OUTCOME-QUALIFICATION-2026-10-20.md)
records all eight selected native checks, the actual-app linked-envelope
regression, F01–F05, complete codec safety/baseline, duration, edited metadata
and composing history as passed. The aggregate conformance result remains
failed for three unchanged API baseline differences; no baseline was relaxed.
[Draft PR #5](https://github.com/rewbs/ScreamSeq/pull/5) started the coordinated
Mac/x64 batch, still building at this checkpoint. Next local build is no earlier
than **08:01:13 UTC**. Earlier pending-build statements below are historical;
the next action is to inspect those live CI runs and their retained results,
then review actual cross-platform observations and exchange saved files.

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

The earlier Windows candidate is
`b1581c9c8bc7d776d637d8c6ac094e8c3db7780a`.
It combines recipe preview ownership, sample release/loop gestures, the corrected
sidebar fixture and zero-duration file defaults. Seven native build targets,
the selected native/application checks, and exact fixture/API/codec/duration
checks are retained under `bin/recipe-sample-20261020/`.
[Qualification](RECIPE-SAMPLE-QUALIFICATION-2026-10-20.md) records a successful
04:50–04:53 UTC build, 16/17 native groups passed, 21 application cases passed,
two provider skips and successful fixture/API/codec/duration checks against
their pinned baselines. The native metadata test failed on an NF/NR field
assumption; its test-only correction now passes on the later candidate below.

The retained shared-contract executable was built at
`b3fe23e45a14763c1f34fe602f8340a49385e93a`. Its
[shared contract qualification](SHARED-CONTRACT-QUALIFICATION-2026-10-20.md)
records a 05:53–05:57 UTC ARM64 build, 21/22 native registrations and all ten
actual-app contracts passed. F01–F05 and separate duration checks passed.
The API corpus has three intentional unmatched observations; the codec corpus
stopped on an unclassified protected-source Save refusal. Fresh-ID test and
explicit file-boundary corrections are prepared after B3 but unbuilt.
The next build gate is **20 October 06:57:04 UTC**.

During the cooldown, the retained B3 app passed the new shared
`--edited-metadata` check: 18 paired clean/opaque stages covering edits,
stable plugin movement/deletion, Undo/Redo and reopen. Its final opaque output
is retained under
`bin/shared-contracts-20261020/edited-metadata-final-2/edited-metadata/opaque-reopened.screamseq`
for exchange. Both native workflows request the check. No new build ran; Mac
execution and the reciprocal return journey remain pending.

The subsequent [rack dry-run follow-up](RACK-DRY-RUN-2026-10-20.md)
also prepares Mac `plugin.move` validation parity. The expanded shared contract
passes on retained Windows B3; Mac native/socket assertions and schema are
updated but still need Mac execution. This did not require a product build.

The stopped F04 cross-domain composing journey subsequently passed 21 exact
saved checkpoints on retained B3 (`composing-history-final/` in its evidence
directory). The shared `--composing-history` check is scheduled on both hosts.
It also exposed an unclassified linked-envelope refusal during private-clone
validation. That pure-preflight outcome correction is prepared with controller
and private-PID regressions; the old executable demonstrably fails the new
outcome assertion. Include these checks in the next bounded build batch.

Temporary integration branch `codex/plugin-edit-parity` was pushed through
`b0d34b57af665f0d7e694e97929e034b64b635c1` without starting CI: no PR existed
and both native workflows' push triggers are restricted to main. Later prepared
source still needs the same-candidate local and cross-platform gate before main.

The next product candidate is `73d5d277c3f9cdfbed628b75fee8c022b16bbd6c`,
pushed and fast-forwarded into the qualification checkout. A subsequent
tooling-only change adds an explicit Mac CI socket stage for the six existing
shared editing contracts; its Windows runner path passes on retained B3.
The next frozen build will include that stage. Prepared scripts under
`bin/commit-outcome-20261020/` retain the hourly gate and preserve earlier outputs;
they have been parsed but not executed.

The context-panel binary at `e79133155` has separate bounded evidence: seven
native groups passed, one native context fixture failed, one actual-app context
case passed, and five subsequent keyboard/browser cases passed. See
[context evidence](CONTEXT-PANEL-2026-10-19.md). This is not qualification of
the newer source or a visual review.

## Dependency-ordered remaining work

| Priority / effort | Confirmed work or gap | Concrete next action and gate |
| --- | --- | --- |
| 1 / M | B3 built; corrected metadata passes. Its API executable stops on reused fixture IDs, and codec qualification stops at an unclassified protected-source refusal. Follow-up source corrections are prepared. | At the next permitted build, qualify explicit pre-commit file outcomes, fresh-ID API cases and previously unreached real-pipe tests. Rerun the actual codec corpus without weakening uncertainty/source checks. Obtain Mac/x64 execution and reuse unchanged evidence. |
| 2 / S–M | At the frozen candidate, Mac `TrackerSessionAPI.inc` excludes rack `dryRun`; Windows accepts it. The later [Mac correction](RACK-DRY-RUN-2026-10-20.md) is source only, with a shared contract passed on the existing Windows app. The API corpus still retains `API-PLUGIN-DRY-RUN`. | Qualify Mac dry runs using existing shared parameter preparation and accepted manual values. Preserve active preview ownership, queues, Redo, revision, opaque bytes and transport. Check changed and no-op batches, full-batch rejection, unavailable/read-only targets, stale guards and subsequent real edits. Do not count a method-name match as parity. |
| 3 / M | [Shared API integer validation](INTEGER-API-BATCH-2026-10-20.md), cache accounting and [terminal request replay](REQUEST-REPLAY-BATCH-2026-10-20.md) now pass B3's shared/actual-app checks. Mac and remaining real-pipe paths are open. | Preserve the old observation baseline until both hosts execute and changes are reviewed. Qualify remaining completion failures without advertising a pre-write revision or converting uncertain side effects into proven rejection. |
| 4 / L | The executed corpus still records Mac's unknown-extension loss. B3's shared codec/preservation/owner tests pass on Windows; [Mac session ownership](MAC-PRESERVATION-BATCH-2026-10-20.md) remains uncompiled/unexecuted on Mac. | Qualify staged Mac load/save/recovery ownership, XML admission, inactive archives, owned plugin/take state and actual merged-tree validation. Execute reciprocal edited-file checks before updating observations. Large-file peak memory and unsupported-encoding archive limits remain explicit risks. |
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
- The executed Mac corpus predates preservation ownership. New source stages
  `ProjectMetadata` beside the document and shares `ProjectMetadata.inc` between
  Open and Save. It merges current known fields with retained typed extensions,
  validates the merged musical tree, and archives damaged original sections
  inactive. Unknown-field warning changes now accompany this source behavior,
  but remain unqualified.

The typed codec and preservation policy have moved into `editor/project/`;
the [foundation batch](PROPERTY-TREE-BATCH-2026-10-20.md) and
[native ownership batch](MAC-PRESERVATION-BATCH-2026-10-20.md) record the changes
and remaining gates. Retain atomic file publication and session lifetime in each
platform adapter. Mac must retain its
original property tree and a canonical baseline for the loaded document; reset
or replace them only with successful document replacement; saving does not
replace the immutable import baseline. Failed loads,
failed saves, Undo/Redo, recovery snapshots and edited identities need explicit
ownership tests. Keep Foundation types out of the shared model and avoid textual
JSON conversion of opaque values.

The shared codec now supplies typed binary bytes, signed/integral/real values,
signed zero, Unicode, dates, UIDs, wide integers and float32 NaNs. Original binary
metadata stays outside Foundation canonicalization. Validate those representations
through the actual session, not only the adapter. Preserve unknown fields without interpreting them as routing or
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
