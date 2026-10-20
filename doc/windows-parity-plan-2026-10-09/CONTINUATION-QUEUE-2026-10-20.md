# Current continuation queue

This is a dependency queue, **not a claim that the 117-row reference inventory
has been fully qualified**. The original A01–A18 acceptance requirements and
fixture provenance remain in [README](README.md) and `parity-matrix.csv`.
Historical “not implemented” or “paused” statements in dated phase reports
must not override current source or the active goal.

## Current integration boundary

**Codec review completed:** [13 Mac expectation changes](CODEC-OBSERVATION-REVIEW-2026-10-20.md)
are proven extension/archive preservation improvements on both architectures.
Inputs, admission, protection, Windows expectations and historical reports stay
unchanged. Additional retained Windows probes confirmed loss of plugin records
and recorded points for integral-real project fields; the
[file-integer repair](FILE-INTEGER-RECOVERY-2026-10-20.md) now covers those known
fields too, with strict regression checks prepared but unbuilt.

**New source, unbuilt:** [original-time plugin notifications](PLUGIN-EDIT-TIMESTAMPS-2026-10-20.md)
now preserve VST3 callback/AU listener timestamps through the shared host
interface and expose backend loss counters. Native/worker checks are prepared.
Existing consumers retain their old behavior: Windows capture and the Mac
poll-time ingestion replacement remain outstanding. Include this slice in the
next coordinated build, no earlier than **10:03 UTC**; do not build it separately.

**Reciprocal gate now passed for frozen `54fa2babc` / merge `d79321c7c`:**
[retained fixture exchange](RETAINED-EXCHANGE-2026-10-20.md). No-build run
39399717354 completed successfully on both Mac architectures. Windows return
legs passed F01–F05 plus rendered/imported derivatives. All seven Mac saved
inputs are byte-identical across architectures, with lineage/hash validation;
the one Windows return run covers both identical input sets. No native build
was started. This does not qualify newer source repairs or complete parity.

**Newest evidence:** all three CI jobs are terminal. Intel matched Apple
Silicon's 134 native/35 interface passes and conformance failures. Retained x64
then passed all seven editing contracts, edited metadata, composing history,
envelope-bank checks, five fixture legs and 21 codec safety cases without a
build. All three binaries have identical outcomes on the 28 API probes; only
five proven-obsolete expected outcomes were reconciled. Original reports remain
unchanged. See [paired API and retained results](PAIRED-API-REVIEW-2026-10-20.md).

That codec comparison identified a real Windows gap: integral-real identity
fields unnecessarily deactivate native music during file import. The scoped
[file-integer recovery fix](FILE-INTEGER-RECOVERY-2026-10-20.md) and rich-model
checks are source-only. Strict canonical/API validation remains unchanged.
Keep the next coordinated build no earlier than **10:03 UTC**. Reciprocal
fixture workflows reuse retained binaries and do not count as a new build.
Historical dispatch boundary: no-build exchange run **39399717354** was dispatched once from unchanged remote
head `54fa2babc`, using Mac app run 39393737331 and Windows fixture run
39393737358. At 09:20:28 UTC, Apple Silicon job **118551112695** passed;
Intel job **118551112822** was live in its owned native checks. Full-build and
interface-build jobs are explicitly skipped. Fetch these artifacts, verify their
retained identities/lineage, then perform the Windows return leg; do not dispatch
the same input leg again. The newer reciprocal result above supersedes this
in-progress checkpoint. Local dispatch receipt is
`bin/export-capture-20261020/retained-exchange-dispatch.json`.

**Newer native CI evidence (09:02:42 UTC):**
[cross-platform export results and repairs](EXPORT-CROSS-PLATFORM-2026-10-20.md).
Apple Silicon passed its build, 134 native registrations and 35 interface groups,
but conformance found a stable-ID state-read gap and Foundation rejection of a
large opaque UID. Both have local source repairs, unbuilt. Windows x64 passed its
build, 155/156 portable registrations and all 48 native editor registrations;
the snapshot fixture failure is already corrected locally, device-dependent
application failures remain, and the exact one-method export inventory update is
reviewed. Intel's build/native steps passed and its interface job remains live.
Conservative next coordinated build bound: **10:03 UTC**, pending exact Intel
build completion timestamp. Do not push/build during cooldown.

`6fe231884` adds the shared absolute recording-frame clock, native callback
adapters and controlled tests, all unbuilt. Event ingestion, capture ownership,
API/UI and full A13 acceptance still remain. Continue from
[frame-clock ownership and gates](RECORDING-FRAME-CLOCK-2026-10-20.md).
Keep native CI repair qualification ahead of claiming new capture functionality.

**Newest result:** pushed candidate `54fa2babc` passed its one Windows ARM64
build at 08:33:39 UTC. Targeted checks passed 11/12 CTest registrations and the
actual-app export replay/reopen contract. The snapshot target passed 47/48
internal cases; one historical RSONGS1 writer assertion is corrected locally
but remains unbuilt. The same-frame recorded-capture admission/render checks
passed; this does not implement Windows capture or qualify the Mac adapter.
[Export/capture qualification](EXPORT-CAPTURE-QUALIFICATION-2026-10-20.md)
records exact hashes, scope, original failure and live CI handles.
Mac Apple Silicon/Intel and Windows x64 are building in runs 39393737331 and
39393737358. Poll those handles rather than restarting them. Do not push another
build-triggering commit during the cooldown; the local lower bound is
09:33:40 UTC and later CI completion may extend the coordinated boundary.
The older checkpoint chronology below is supporting history.

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

**Subsequent result:** Mac Apple Silicon failed compilation at 07:08:49 UTC on
a malformed rack-preview array initializer. The correction is prepared locally;
the same report retains its exact log and pending native gate. Intel and x64
were still running at diagnosis. Collect their results and batch corrections
before the next hourly build; do not interpret the Windows passes as Mac proof.
The [export follow-up audit](EXPORT-FOLLOWUP-2026-10-20.md) also confirms the
missing Windows module-export endpoint/command and records concrete shared/native
boundaries for A15, to follow the current integration repair.

During the cooldown the retained Windows candidate also passed the new shared
`--envelope-bank` journey at **07:16:44–07:16:59 UTC**: 25 exact saved checkpoints
cover two linked uses, an independent copy, unlink, master edits, dry/no-op and
invalid requests, chronological Undo/Redo and reopen. All instrument fields and
sample PCM were checked. Evidence is under
`bin/commit-outcome-20261020/envelope-bank-journey/`; the final exchange input is
`envelope-bank/reopened.screamseq`. Original inputs, outputs and executable
remained unchanged and owned processes exited. This is a bounded A08 bank check,
not family/catalogue/UI/audio completion. The next native workflow batch
schedules it on both platforms; no new product build ran.

The [manual automation audit](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md)
confirms a remaining A13/P4b implementation gap: Windows supports live editing
of recorded points, but its native/vendor parameter gestures do not capture
them. The audit maps existing Mac behavior, Windows queues, shared/native
ownership, timing and history gates. Do not count existing recorded-point
editing or note capture as completion, and do not add only a cosmetic Record
toggle. This larger batch follows current integration repairs.

Intel CI subsequently failed at 07:21:08 UTC on the same Mac initializer and
downstream parse diagnostics. Both Mac failures are retained in the
commit-outcome report; neither reached runtime qualification. Windows x64
remained live. Keep the prepared correction and tooling changes local until
the coordinated Mac gate, **08:21:09 UTC**, and incorporate any further confirmed
x64 failures into that batch.

At **07:28:50 UTC**, Windows x64 job `118498660665` had completed its build and
was running portable/worker tests. Its runtime gate is not yet complete. Retain
the final log's exact build-completion time before scheduling another Windows
build; **08:28:51 UTC** is a conservative earliest coordinated boundary from
this observation.

The x64 run has since completed failed. Its build and all 153 portable/worker
checks passed; two native UI fixtures targeted intentionally removed sidebar
buttons. Their replacements now exercise the real menus and await compilation.
The retained artifact and audio probe establish that hosted CI had no WASAPI
endpoints. Reusing its exact x64 executable locally yielded ten passing
live-output cases, followed by a separately passing corrected loop-menu case,
without a rebuild. Exact scope, hashes, original failures and persistence
results are in [x64 qualification](X64-CI-QUALIFICATION-2026-10-20.md).
Keep the next build boundary at **08:28:51 UTC**.

Independent module-export implementation has advanced in the existing
`C:/Users/P14/.codex/worktrees/windows-parity-workspace/ScreamSeq` checkout,
temporary branch `codex/module-export-followup`, commit `b8646d785`.
It now includes shared loss admission, validated bytes, the Windows API/native
Export command and retained-result review, Mac recovery/gesture preservation,
and prepared worker/native/Mac regressions. It is unbuilt. The feature branch's
export-follow-up document records newly prepared actual pipe/socket replay,
post-publication native fault coverage, real filesystem refusal and protected
source-alias checks. The full snapshot suite is now scheduled against matching
libraries in normal Windows CMake, with current migration checks and bundled
F04. These additions have static checks only; native compilation and actual
runtime execution remain open.
Neither the PR head
nor the qualification checkout includes this feature work. Continue it and
reconcile back into the corrected integration branch before its own gate;
do not leave a long-lived platform branch.

**Local reconciliation at 08:13 UTC:** the complete export batch through
`2be5dbaf6` was fast-forwarded into `codex/plugin-edit-parity` in the primary
checkout. It includes the existing Mac compiler correction and Windows native
menu-fixture repairs. The temporary export checkout remains at the same commit.
Remote PR #5 is still frozen at `8c750f7d6`; nothing was pushed and no build was
started during this cooldown. Continue from the primary checkout for the next
coordinated batch, preserving the **08:28:51 UTC** earliest build boundary.

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
