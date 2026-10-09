# Planning review refresh and handoff

**Status superseded:** [current review](current-review.md) pins main to `16cab100b` and checkout to `07060e1c4`, records successful P0a CI and remaining P0b work. Source/CI status below retains its original checkpoint; use the current review and main plan for the next batch.

Historical review checkpoint at `61a28b489`. For the current inspected `e7f165eb4` checkout and later existing candidate evidence, use [current source review](current-source-review.md). The source status below is retained for provenance, not presented as current branch state.

This is a documentation-only review of the two supplied peer files and the existing integrated plan. The comprehensive proposal remains [README.md](README.md), with its [117-row parity matrix](parity-matrix.csv), [419-entry command inventory](command-inventory.csv), [232-method API inventory](api-inventory.csv), [source map](source-map.md) and [84-row peer reconciliation](peer-matrix-review.csv). No implementation is requested by this document.

## Observed source state

- Remote `refs/heads/main` still resolves to `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. There is no post-reference main delta to extrapolate.
- The current branch is `codex/windows-parity-p0` at `61a28b4892b89e6c1e0445dd3b16f975fd222c9e`. It was clean when this review began. Relative to main, `f74ef145d` adds the planning package, `605c7c688` adds the initial candidate, and `61a28b489` adds the narrow follow-up below and existing progress documentation. These commits already existed when this review began.
- The candidate changes eight product/test files. Other product code matches main, so the main-pinned functional inventory remains applicable outside that narrow delta. This pass read existing local logs and one failed candidate CI log; their scope and limitations are recorded in [handoff details](handoff-details.md). No new test or build was executed, and the candidate is not fully qualified.
- The supplied ZIP and both peer files still match the full SHA-256 hashes in [evidence.json](evidence.json). The second peer file is named `SCREAMSEQ_WINDOWS_PARITY_MATRIX_2026-10-09.csv`, rather than PLAN.

| Existing candidate source | Observed change | Future acceptance still required |
|---|---|---|
| `windows/App/RecordingIntegration.inc`, `SampleCaptureIntegration.inc` | Separates MIDI and sample take checks; aggregate checks MIDI, sample, then MIDI again after worker-boundary message pumping | Clean, MIDI-only, sample-only, both, worker failure, stale take and reentrant take creation; no save/open/close loses either take |
| `windows/App/Main.cpp` | Close failure fallback includes current MIDI take state | Failure must not expose a destructive close fallback when a take remains; ordinary save/cancel behavior stays correct |
| `mac/Audio/CaptureEndMonitor.hpp`, `SampleRecorder.mm` | Uses explicit atomic stop request and joined `std::thread`; resets stop on start and joins before AudioUnit disposal | Actual supported Apple compiler/libc++/SDK, restart, autonomous limit/disconnect, partial-start failure, callback/control ownership and teardown |
| `mac/Tests/SampleRecorderTests.mm`, `windows/Tests/Workspace/WorkspaceRestoreTests.cpp` | Adds monitor lifecycle and aggregate retained-take cases | Inspect what each fake/hook exercises; run affected native targets during future implementation, plus real app/API and required host/device cases where the doubles cannot prove behavior |
| `mac/App/WorkspaceIntegration.swift`, workspace test follow-up | Simplifies typed menu accumulation after a CI Swift type-check failure; fixes percent-display expectation and adds exact raw-value assertions | Qualify the actual final candidate with both native toolchains; the earlier PR-merge CI result is not a result for local HEAD |

The recommendation remains **complete P0a on main first**, then P0b/P0c, then Mixer as the first substantial feature. An implementer should review/reuse this candidate, reconcile any subsequent main changes, and obtain the phase's required evidence. Do not create a second implementation just because the plan describes the same repair. Do not treat the candidate as a merged milestone or start builds, tests or merges under this planning-only request.

## Peer ideas retained and constrained

The base remains the integrated plan because it already retains individual reference provenance, full inventory coverage and concrete source/test pointers. The peer's detailed safety, conformance, UI and architecture ideas are incorporated into the implementation sections rather than left as optional commentary.

| Peer contribution | Integrated destination | Boundary retained |
|---|---|---|
| Green baseline and recording safety | P0a / F19–F20 | Both checks and native recorder lifecycle; actual compilation, not a regex-only guard |
| Dirty drafts, stage roles and modulation-field preservation | P0b / F21–F23 | Invalid/stale drafts are reviewed, never silently applied; existing saved cables are not rewritten to new defaults |
| Typed golden fixtures and API conformance | P0c / F25–F27, A01/A15 | Exact bool/int/real and opaque bytes; known drift is tracked for repair, not excused by normalization |
| Shared validators, codec, history and operations | Architecture table; P1/P2/P3/P5/P6 | Reuse proven portable code family by family; both adapters change together; native services stay native |
| Menus, command availability, DPI, themes and UIA | P3a and each new surface | Windows conventions; no compulsory Mac colors, fonts, window styles or identical layout presets |
| Gesture-end commits and retained workspace | Common transaction contract; P3/P4/P5 | One Undo per completed gesture; explicit Apply for compound previews; preserve raw text and full captured dependency set |
| Manual automation recording and richer parameter UI | P4a/P4b | Existing recorded-point editing is not live capture; any new musical edit receives API/history/persistence coverage |
| Graph reachability and direct manipulation | P5a/P5b | Inspect current forms and commands first; do not duplicate an operation merely because its canvas gesture is absent |
| CI coverage and reciprocal artifact exchange | P0c/P6/P8 | Required tests are not quarantined to obtain green status; both native builds converge on one main |

Additional corrections from this review:

- `plugin.state.set` reaches `PluginOperations::commit` and `DocumentController::prepareRackPublication`; a blanket claim that Windows always stops is incorrect. Test compatible and rejected states rather than implementing a second replacement path.
- Native controls can expose accessibility without a custom application UIA provider. Custom canvas semantics and actual Narrator behavior still require their own implementation/qualification.
- Different history capacity, a different commit affordance or a different canvas curve constant is not by itself a musical parity failure. Compare recoverability, result, Undo meaning and interaction cost.
- The final gate covers **all** B01–B14. The peer's explicit exit list omits several necessary recording, recovery, plugin and audio cases; those omissions are not adopted. Report skipped/unavailable cases separately.
- Neither source code nor screenshots establish foreground presentation, sustained rendering, actual input/device behavior or reciprocal saves. Existing logs can be reused only with unchanged relevant transitive inputs and their original scope.
- The integrated plan's earlier stopped-only claim for recorded-point edits was wrong. Both adapters already prepare/publish live recorded timelines, with a Windows live publication test in source. A12 and the matrices now preserve this behavior; manual gesture capture remains a separate gap. Stopped Activity target enumeration is not equivalent to stopped saved-lane access.
- The peer's proposed acceptance text remains traceable, but is subordinate to the integrated corrections: no unsafe stale rebase, blanket no-edit allocator exemption, or compulsory Mac-like control layout.

## Visual review and provenance

The package's main update, README, fixture guide, screenshot index/gallery records and checklist were consulted. All nine contact sheets and the original Mixer capture 36 were inspected; the retained Windows modulation and recorded-activity captures were also inspected. The Mac Mixer shows aligned repeated strips, visible units and immediate per-bus controls, but its clipped right edge is not a usability target. Windows already has a coherent native surface; prominent UUID strings and large unused form regions reduce useful density. These observations support names-first targeting, compact measured grids, contextual commands and native sliders; they do not justify copying macOS title bars or controls.

The Mac image is a carried-forward live capture; the Windows image is historical private-desktop/native-render evidence. Neither was recaptured here. Supplemental component renders, including blank native captions and synthetic meter values, remain geometry references only. Fixture session selection, focus, pin and draft state must be recreated using the gallery instructions; song reopening alone does not reproduce a screen.

## Decisions and handoff

No product decision is needed to finish reviewing this plan. Recommended defaults are stated for commit behavior, command migration, validation, shared extraction and native presentation. The later qualification scope needs the user's must-support physical audio/MIDI devices and commercial plugins, if any beyond redistributable fixtures. The plan assumes Windows x64/ARM64 and Mac Apple Silicon/Intel; dropping an architecture requires explicit scope agreement. Optional exclusive-mode/ASIO expansion and automatic recovery deletion are not prerequisites.

The next action after delivery is the user's review. This pass performs no product edits, application launch, build/test execution, workflow dispatch, branch switch, merge or process control.
