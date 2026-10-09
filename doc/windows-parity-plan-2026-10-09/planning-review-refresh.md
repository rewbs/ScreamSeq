# ScreamSeq parity planning review

This review consolidates the supplied Downloads plan and matrix into [the implementation plan](README.md). It is planning only. Only planning documents were written; no product code, tests, builds, app launches, CI dispatches, commits, pushes or merges were performed. Existing implementation and execution receipts are treated as evidence at their recorded inputs.

## Current source and evidence

Read-only `git ls-remote origin refs/heads/main` returns `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`, identical to the reference baseline. There are **zero post-reference main commits**. The local checkout is `codex/windows-parity-safety` at `d5b7a09eb50a89b775841da14d8d81f49362c122`, clean at the start of this review. Its 21 product/test paths plus Windows API guide differ from main; shared `editor/` source does not. Source findings pinned to main remain valid unless the candidate column says otherwise.

| Input | Identity |
|---|---|
| Mac archive | `030bfcf7471228cbce516426f756c2e5a3d4ce6ff760a92f7d31b0cbe54ade81` |
| Peer Markdown, 166,644 bytes | `c1b50ca8424da02bd2e0dced7e7129e928d7e4de5e25d7cd31aae0378a8d73ce` |
| Peer CSV, 37,671 bytes | `ec2bbc2c01769072f6cc3edc3e9d331122e2656f53d6cb793fcdd4a444aa91f5` |

The second supplied file is named `SCREAMSEQ_WINDOWS_PARITY_MATRIX_2026-10-09.csv`. Its 84 rows are retained in the [peer row review](peer-matrix-review.csv). The consolidated matrix covers those 84 checks plus 33 components, and the command/API inventories preserve all 419 reference commands and 232 Mac methods; the handoff additionally covers 16 Windows extensions. These are inventory counts, not completion percentages.

The Mac gallery was reviewed across all nine contact sheets. Live captures have readable native controls; component sheets have blank control captions in several renders, reinforcing their limited use as geometry evidence. Mac Mixer 36 and Sound design 51 establish a useful simultaneous overview; they do not prove meter accuracy, audible gestures or performance. Existing Windows modulation and activity composites show native controls and clear value fields, but long stable-ID strings dominate target labels. The activity composite visibly contains minus/Fit/plus controls. Neither historical Windows composite is a current foreground or Narrator pass.

## Candidate changes since the earlier review

These changes already existed when this planning pass started. They must be reused or reviewed, rather than scheduled as entirely new implementation.

| Source delta | Observed implementation | Existing evidence read and remaining limit |
|---|---|---|
| P0a guard/recorder/worker repairs through `4a5c8ed88` | Separate retained MIDI and microphone guards with recheck; compatible Mac monitor lifecycle; Swift menu expression split; guarded worker preparation errors | Earlier P0a merge `29149338` passed both Mac jobs, including 120 CTests and Swift recovery/picker checks. Windows built and passed portable/worker/scratch checks before two retained-editor failures. Neither result qualifies every interface, device or later commit |
| `13a5be2ab` | `GraphCurveOwnerLayout.inc` measures closed combo bounds in client DIPs before placing axes. Graph test adds a fixture-local 96-DPI context. Workspace fixture establishes and asserts actual client size while retaining a compact capped-window case | Retained log reproduces 96-DPI overlap, then passes the graph suite (2.72 s). Workspace and precise-note suites pass (83.33 s and 5.60 s). A fixture geometry repair does not establish real desktop usability at all scales |
| `758cd1559` | Pattern wheel fixture tests ignored outside coordinates then sends tracker screen coordinates. Graph host fixture waits for existing bounded preview readiness before reading | Five app cases initially had three passes and two failures; the corrected two pass in 4.799 s. This is scoped native HWND/pipe evidence, not a full app-suite pass |
| P0b `d59c24c8c` / `e7f165eb4` | Fresh modulation depth/base and field preservation; explicit stage-role capability checks in both controls and handlers | Existing targeted logs are recorded in the earlier source review. Recipe Enabled/Quantize controls, exact-copy observation and aggregate raw-draft departure protection remain outstanding |

The local layout receipt `bin/parity-evidence/p0-layout-receipt.json` pins source `758cd1559`, 1,823 source/dependency hashes, CMake cache, logs and executables. Local logs read in this review include `p0-layout-graph-before.log`, `p0-layout-graph-after.log`, `p0-layout-retained-after.log`, `p0-layout-integration.log` and `p0-layout-integration-corrected.log`. Failures and corrected attempts remain distinct. A later documentation commit does not invalidate a binary, but reuse still requires matching transitive inputs, toolchain, architecture, fixture and scenario.

The same five layout/test file changes are isolated on P0a head `8afd4175a82eba3dc0331e6951b55e3ea891ae72`, based on `4a5c8ed88`; it excludes the later P0b cable/role changes. Existing [Windows run 37918435648](https://github.com/rewbs/ScreamSeq/actions/runs/37918435648) and [Mac run 37918435661](https://github.com/rewbs/ScreamSeq/actions/runs/37918435661) were **in progress** at the read-only snapshot. Jobs: Windows `113780223549`, Mac Apple Silicon `113780223733`, Intel `113780224030`. Read eventual results and actual checkout SHA before qualifying that candidate. This planning task does not wait for or manage implementation CI.

## Corrections and incorporation of the peer review

[Peer reconciliation](peer-review-reconciliation.md) records the full disposition. The adopted ideas are integrated into the main phases: build safety first; typed fixture/API conformance; shared validators/history/persistence; explicit command availability; measured native layout and semantic accessibility; gesture-level Undo; cohesive change batches; reciprocal fixture exchange.

Additional source checks make the following distinctions explicit:

| Peer assertion or proposal | Reviewed disposition |
|---|---|
| Activity lacks zoom and is unusable while stopped | `ParameterActivityWindow.hpp::action` already handles `fit`, `zoomIn`, `zoomOut`. `AbsoluteAutomationWindow` accesses saved rack lanes without a live-copy catalogue; active point edits have prepared publication. Prepared-copy discovery and unavailable-state UX still need qualification. Do not port zoom or disable live editing anew |
| Graph has no Enabled/Quantize controls | `GraphWorkflowWindow` has both for **song** modulation; recipe-wire form lacks them. Preserve this scope in P5. Existing candidate preserves recipe fields during edits, which does not add controls to edit them |
| Graph annotations/groups/reroutes are absent | `GraphWorkflowActions.inc::execute` already provides numeric forms for groups, frames/comments, collapse and reroutes. P5 adds contextual/direct manipulation and command reachability; it must not create a second data model |
| All local graph/form actions count as missing because not in the palette | Concrete handler mappings are now in [local action audit](local-action-audit.md). A native button is an existing entry point, but still needs command registration and a tested availability predicate |
| Mixer then tracks must wait for wholesale SessionCore relocation and global restyling | Use the small shared Mixer control candidate first, then typed track operations. UI foundations may move earlier independently. Large refactors remain possible, with conformance and both native callers in each merge |
| Bare `midiGuard && sampleGuard` suffices | The candidate correctly checks MIDI again after the sample worker read. UI message pumping can create new work. P0b needs final admission plus a short departure lease, not merely another early prompt |
| File/codec equality can normalize allocator changes on any save | No-edit legs preserve identity and allocator exactly. Only the documented allocation/Undo case permits `nextID` 41→43. Typed trees distinguish bool/int/real; binary payloads remain exact |

## State and failure coverage that governs every phase

For each owner named in the handoff, cover clean, valid dirty, invalid raw, stale target/revision, pending submission, newer generation during completion, uncertain outcome, deleted target and hidden/nested states. Exercise show/focus/retarget separately: reopening a visible owner raises it without discarding work. Selection, playhead and keyboard focus remain independent. Local text Undo, document Undo and audition ownership must dispatch by actual focus.

P0b additionally covers native Open/New/Demo/Close, API replacement, recovery restore and bounded shutdown. `EditingView.inc::protectUnsaved` currently checks takes, plugin flush and persisted dirty state. `DocumentController::installCandidate` stages a candidate then invokes UI stop before ownership swap; `RecoveryOperations.inc` rechecks its fingerprint but does not inspect every raw native draft. A departure token must be revalidated at final admission, with a short lease through adoption/rollback. Reject or defer new document-scoped writes during that lease without blocking an event loop needed by the worker. Deferred discard is applied only after successful replacement; chooser cancellation, load failure and stale admission retain exact raw text and targets. API refusal returns structured owner summaries without opening modal dialogs. Reference-only formula windows, telemetry, searches and global preferences are not dirty song owners.

All musical writes inherit A01: complete validation before commit, stable identity, unchanged unrelated data, dry-run/no-op without history, exact Undo/Redo, persistence, queue/admission failure without partial publication, and outcome reconciliation after uncertain replies. Realtime jobs add prepare/adopt/retire ownership and no callback allocation/locks. Source inspection identifies the branches; future native execution establishes whether they satisfy the contract.

## Recommended first implementation batch

**P0a qualification and integration of the existing isolated repairs**, relative effort **S–M**, remains first. Read the existing CI results at `8afd4175a`; correct only demonstrated blockers, preserve assertions and qualify the actual merge candidate on both native platforms. Do not repeat the guard/monitor/layout repairs or move unrelated shared code in this batch. Local safety-branch results cannot substitute for the isolated candidate's x64 CI.

If exact-candidate CI is green, the next code batch is **P0b aggregate draft departure protection**, relative effort **M**, high data-safety risk. Reuse the completed cable/role slices. Its cheapest checks are owner summaries and token/lease transitions, followed by one Windows app/workspace build for the cohesive implementation and targeted Open/API/recovery/Close/recording tests. Common admission changes trigger broader workspace/recovery checks; shared-token changes also require Mac session/editor-draft checks. Follow P0c typed conformance with P1 Mixer, P2 tracks and P3–P8 from the main plan.

No user decision is needed to specify these batches. The only later product input required is the must-support physical audio/MIDI devices and commercial plugins, and explicit agreement if Windows x64/ARM64 or Mac Apple Silicon/Intel support is reduced. Exclusive audio modes, automatic recovery deletion and repository settings changes are optional separate scope decisions, not implicit parity work.
