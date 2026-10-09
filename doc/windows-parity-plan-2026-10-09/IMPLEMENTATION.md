# Parity implementation progress

Implementation resumed after the documentation-only review under the active user goal, “Go ahead with the implementation as per the latest plan.” The complete scope is the [reviewed parity plan](README.md); [latest planning review](current-review.md) retains the planning checkpoint. **P0a is merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding.** Earlier receipts below retain their original scope and dates.

## P0b.1 outcome boundaries — partial implementation

This continuation implements and qualifies the existing partial outcome candidate; it does **not** close P0b.1 or retained-work protection. The planning readback's hashes and “not executed” labels remain the earlier checkpoint, not the status of the code below.

- `editor/WriteOutcome.hpp` defines a portable value-only outcome. `ApiError` optionally carries it without changing public error numbers. Known worker postcommit publication failures report committed identity/revision; controller tests now assert typed data instead of searching message text.
- `SessionAdapter` preserves an outcome thrown by the write itself. Failures after the host returns (snapshot, envelope serialization or wire-size admission) become `unknown`, with no pre-write revision. A later callback's own typed refusal or foreign identity cannot be attributed to the original write. The native Application uses the same distinction and repairs presentation without replacing the original worker exception.
- The existing FIFO cache retains classified committed/unknown error responses as well as successes. Same-ID/same-content replay returns the retained response; changed content rejects. Bounds remain 64 entries/8 MiB, best effort and non-durable. Deliverable oversize-cache successes still return normally. Undeliverable write replies now become a small classified error before the pipe serialization boundary. Shared `ProtocolLimits.hpp` keeps adapter/transport byte limits aligned.
- [API guide](../../windows/Api/README.md#classified-write-outcomes) and [optional error-data schema](../../windows/Api/write-outcome.schema.json) document coverage and limits. The Python client already preserves error data and performs no send retries. No project format, history policy, audio/device path or Mac UI changed.

Qualification uses the dedicated Windows ARM64 Release app/controller/workspace directory and a separate small `bin/windows-parity-api` C++17 target. Actual named-pipe fault cases cover equal error codes/messages with different commit outcomes, authoritative worker identity, snapshot failures (including a nested typed refusal), invalid UTF-8, the 32 MiB reply bound, exact replay and side effects that leave the song revision unchanged. Domain readback must show exactly one effect. Native Application fault injection runs after the real worker edit and checks the retained new cell, readback, one Undo, nested-callback attribution and ordinary stale refusal.

Final qualification results and exact source/binary/log identities are recorded in `bin/parity-evidence/p0b-outcome-receipt.json`: 1,834 source/dependency hashes, both CMake caches, five executables and retained logs. All frozen inputs matched after the final checks.

| Final evidence | Result |
|---|---|
| `p0b-outcome-api-final-build.log`, `p0b-outcome-boundary-build.log` | Standalone API, controller, workspace and app built successfully |
| `p0b-outcome-api-final-tests.log` | 2/2 API targets, 4.67 s; detail log retains individual protocol/cache assertions |
| `p0b-outcome-controller-final-tests.log` | Full 26-entry controller inventory, 14.78 s, including scratch/fixture setup |
| `p0b-outcome-workspace-final-tests.log` | Full workspace restore and draft census, 2/2, 89.38 s; detail log includes actual worker/UI completion and one-Undo assertions |
| `p0b-outcome-app-final-tests.log` | Five actual-app/private-pipe scenarios, 2.559 s, no skips: context, Activity, recorded automation/history/save, sample import/history/persistence and independently revisioned library replay |

Intermediate evidence is retained: the first workspace compile found a test-local `Json` alias missing; the correction changed only test compilation. The first workspace suite and five app cases passed; later review tightened callback attribution in both adapters, requiring the final rebuild/recheck above. No assertions, private-child timeout or error checks were weakened. Qualification used owned private desktops and disposable documents; no system audio defaults or musician sessions were changed. These checks do not establish physical audio or foreground presentation.

**Next required work:** native owners must retain submitted generation/target/result and expose domain-specific reconciliation before uncertain imports, Keep, repair or render can be retried. Cached receipts alone do not prevent replay after eviction/nonretention/restart, and an unchanged song token cannot establish filesystem/catalogue/take outcome. Complete direct-render registration and every owner census; wire final replacement/Close/recovery/session-end admission. Mac and Windows x64 gates, reciprocal fixtures, foreground/accessibility and device qualification remain open. Do not merge this partial checkpoint as completed P0b.1 or mark the full parity goal achieved.

## P0b plugin repair, configured import and recorder setup

This continuation completes the three owner omissions identified by the latest review. It is branch work; it does not close F21 or qualify the final departure path.

- `PluginPathWindow` registers raw manual-path/candidate intent and pending reads, verification, scans, chooser and reconnect requests. Late fields and selections retain their captured revision; accepted-path baselines distinguish returning to an old path from accepting the submitted reconnect. Manual paths not submitted by Reconnect remain dirty. Explicit Discard releases local intent. `Application::openPluginPath` preserves hidden dirty/pending owners and distinguishes the document when reusing a clean owner.
- `MultisampleImportWindow::open` treats a configured family as retained intent immediately. Hiding is presentation, not a new draft generation. Check/Import keep pending ownership through completion callbacks; late raw text invalidates reviewed roots and survives successful import. Reopen raises the existing family; explicit Use current song plus Check is required for stale import. Discard rechecks generation after stopping preview.
- `SampleRecordingWindow` registers document-scoped name/output intent separately from the session's retained take. Idle endpoint/channel preferences do not create song drafts. Pending Start/Keep/Discard/read requests are protected, and attempted endpoint retargeting during a request restores the captured selection. Keep clears only its submitted generation; later name/output changes survive. Discard setup resets local fields without discarding a take; Discard take does not consume unrelated setup. Old-song setup cannot silently start/keep into a replacement song.

New real-HWND cases in `Workspace/DraftImportRetentionTests.inc` exercise pending census/refused discard, raw text and selector callbacks, Check versus commit, rejected writes, stale retry, late callback input, hidden/reopened owners and explicit discard. `DocumentDraftCensusTests.inc` additionally exercises the actual Application's hidden path-owner reuse and recorder registration. The small target links native `comdlg32` for the existing file chooser; production libraries and Mac/shared musical code are unchanged.

Qualification used the dedicated ARM64 Release `bin/windows-parity-p0` build and owned private desktops. No musician app was running at the preflight or final process check; no audio defaults changed. Logs under `bin/parity-evidence/`:

| Evidence | Result and boundary |
|---|---|
| `p0b-import-before-build.log`, `p0b-import-before-test.log` | New check reproduced omitted configured family intent before the repair |
| `p0b-owner-completion-tests.log` | Retained first post-edit failure: late plugin choice returning to the old baseline was misclassified clean; fixed without weakening the assertion |
| `p0b-owner-completion-final-build.log`, `p0b-owner-completion-final-tests.log` | Native tool target passed, 1.11 s |
| `p0b-owner-app-build.log` | App and workspace harness built successfully once the native owner changes were ready |
| `p0b-owner-integration-tests.log` | 4/4 CTests: full workspace restore 83.60 s, expanded draft census 4.01 s, native tools 1.05 s, sample-recording lifecycle 0.06 s; private child bound unchanged |
| `p0b-owner-app-tests.log` | Five sample-library scenarios and native rack reconnect passed. The graph-path scenario exposed an outdated foreign-path fixture setup; its failure remains retained |
| `p0b-owner-graph-path-tests.log` | File-based foreign-path setup passed backend state/history/persistence checks; native entry then exposed a stale button/deferred-load test sequence |
| `p0b-owner-path-final-tests.log` | Graph and rack native reconnect cases both passed, 7.531 s, after fixture/input readiness corrections; no product validation was relaxed |

The corrected graph-path case now asserts `graph.update` rejects a foreign Windows module path without mutation, then creates the imported project through a saved property tree. It still asserts recipe/rack/opaque-state preservation, Undo/Redo, save/reopen and the actual native graph target. Native commands wait for deferred view work, select the stable fixture node and verify selection before opening repair. The sample-library addition verifies a hidden family survives an external document edit before any field has been typed. Seven distinct application scenarios passed across the retained runs, with no skipped cases. Existing third-party/native provider fixtures were reused from the explicit test caches, not installed or modified globally.

`bin/parity-evidence/p0b-owner-completion-receipt.json` records source/dependency, executable, fixture/cache and log hashes. This is Windows ARM64 functional/native ownership evidence; it does not establish foreground accessibility, physical capture, x64, Mac P0b or reciprocal saves. No shared code changed in this slice, so the unchanged Mac baseline is reused only within its original P0a scope.

**Next:** introduce typed commit outcomes at the actual controller commit boundary and reconcile uncertain native writes; finish the remaining owner audit, including direct `SampleCaptureIntegration.inc::renderPatternSample` pending intent (not currently registered in `DocumentDrafts.inc`), Main FX identity and unavailable nudge review. Then wire Application final replacement/close/recovery admission with exact consent and the input/API lease. Existing numeric `-32003` errors cannot alone distinguish rejection from committed-but-unreported work. Do not treat these selected owner tests as complete departure protection.

## P0a integrated on shared main

[PR 3](https://github.com/rewbs/ScreamSeq/pull/3) merged as `16cab100b898f66726cfab6c2386887a9bcf5fd4` after all three candidate jobs passed. The actual tested PR merge was `6433dc0da9c1b779b1390de7a495af2c2353fd9e`; a local tree comparison found no content difference from the final main merge. Head was guarded at `6c3eea908fd3167ada681b2c44be551cb7eb94a3` during merge.

- Windows x64 [run 37923921386](https://github.com/rewbs/ScreamSeq/actions/runs/37923921386), job 113798216568: app build, 46 portable entries, 62 worker entries, three scratch app cases, three retained-editor CTests and five merged app scenarios passed.
- Mac [run 37923921268](https://github.com/rewbs/ScreamSeq/actions/runs/37923921268): Apple Silicon job 113798215876 and Intel job 113798216235 passed native builds, 120 CTests each and Swift recovery/picker checks. Their actual checkout lines identify `6433dc0`; CTest elapsed times were 118.25 and 228.86 seconds respectively.

These gates qualify the P0a integration, not P0b, reciprocal fixture compatibility, physical devices or full foreground/accessibility behavior. No duplicate workflow was dispatched. Source main now differs from the planning reference baseline; the plan's main-pinned findings remain historical until individually superseded by implementation evidence here.

## P0b native draft census and completion retention

The native registry now follows retained HWND/Main owner lifetimes, including hidden/reparented windows. Raw draft summaries include captured document, revision, target and generation; composite arrangement details retain independent identity/generation tuples. Formula children inherit their original parent identity; reference-only Formula search is excluded. Main owners register separately from `NativeToolWindow`. Song Timing includes the captured sequence; nudge Review reveals the captured cell without reloading or applying its raw strings.

Graph Trim and Pattern Selection Render requests expose pending state and retain newer input on completion. A new real-HWND regression reproduced loss of a trim link choice while Apply pumped messages. The repair increments generation for later selector edits and restores the displayed owner/port selection when an in-flight request prevents retargeting. Trim text, read refusal, stale retries and pending discard are covered; render Check/refusal/commit cases distinguish submitted fields from newer raw input. Those bounded callbacks qualify native completion ownership, not audio rendering.

Focused qualification uses `bin/windows-parity-p0` (ARM64 Release):

- `p0b-request-retention-before.log` retains the reproduced selector-loss failure; `p0b-request-retention-after.log` passes after the fix.
- Final app/workspace build: `p0b-census-final-build.log`.
- `p0b-census-final-tests.log`: workspace draft census, native-tool-window and pure departure targets pass, 2.49 seconds total.
- `p0b-census-final-app.log`: all 17 song-tools/graph-curve-host/precise-note-host app cases pass, 28.388 seconds, with the explicit arrangement fixture and silent-output prerequisites.
- `p0b-census-final-restore.log`: final retained workspace regression passed in 82.82 seconds, within the unchanged private-child bound.

`bin/parity-evidence/p0b-census-receipt.json` records the 1,830 source/dependency hashes, build configuration, executables and logs. Earlier census attempts remain retained; a missing arrangement-fixture setup failure and explicit silent-output skip are not relabelled as passes. The two new census cases use a separate invocation of the existing workspace executable, avoiding a second native-app translation unit and retaining the private child's 90-second bound.

**This is a foundation slice, not F21 closure.** `Application` still omits `DocumentReplacementAdmission`, and `protectUnsaved`/`canClose` do not authorize against the registry. Remaining P0b work includes complete owner classification (plugin path repair, recorder configuration, configured import intent, uncertain write outcomes), stable or sufficiently guarded Main FX identity, unavailable nudge-target review, and the final native input/API/adoption/retirement lease. Native Open/Close/API replacement/recovery and shutdown must then pass actual application checks. Current P0a Mac evidence does not qualify the changed departure header.

## P0b departure protocol foundation

The latest review was committed separately as `75ca12e30`. Implementation then added the shared value-token rules in `editor/DocumentDeparture.hpp`, UI-thread owner registration in `windows/App/DocumentDraftRegistry.hpp`, and a final replacement-admission observer in `DocumentController`. **The application does not yet pass an observer or register its native editors. This foundation is not complete raw-draft protection and does not close F21.** No product version, musical data, API schema, DSP or native device behavior changed.

- A token captures document/revision, owner incarnation, stable target, captured revision, raw-draft generation and dirty/pending/uncertain state. Presentation labels, clean navigation and owner iteration order are excluded. Pending or uncertain writes cannot be authorized by Discard. New work, owner recreation, failed owner reads, cancellation and a newer review invalidate old consent.
- The registry retains hidden owners, supports nested owners and raises an existing owner for Review. Registration handles follow native lifetimes; summary callbacks are UI-thread-only, read-only and cannot pump messages. A failed summary fails admission closed. The registry deliberately does not inspect arbitrary HWND text or classify every window as dirty.
- `DocumentController::installCandidate` now accepts an optional native observer shared by Open and recovery. Candidate parsing, cache construction and the recovery fingerprint check precede admission. Admission and Stop run in the same UI callback; the short lease spans worker adoption and view publication. Stop refusal calls `finish(false)` without changing the original view. Successful publication calls `finish(true)`. Initial construction has no departing document and does not invoke the observer. Existing callers remain compatible.

Local Release ARM64 evidence, built into the existing dedicated `bin/windows-parity-p0` directory with no running musician process found:

| Check | Result / log under `bin/parity-evidence/` |
|---|---|
| Token/registry target | Passed; `p0b-departure-tests.log` |
| New real-worker admission plus publication/recovery/recording regressions | 7 CTest entries passed including scratch/fixture setup, 1.55 s; `p0b-departure-controller-tests.log` |
| Full controller regression after the worker change | 26 CTest entries passed including setup, 14.71 s; `p0b-departure-controller-regression.log` |
| Final cancellation/failed-summary consent tightening | Both departure targets rebuilt; 3 focused entries passed including scratch setup, 0.46 s; `p0b-departure-final-build.log`, `p0b-departure-final-tests.log`. Worker implementation is unchanged from the broader regression; the new registry guard and cancellation assertions have this final focused result |

`p0b-departure-receipt.json` records 1,827 source/dependency hashes, CMake cache and log hashes. Final executables: `document-departure-tests.exe` SHA-256 `efcae9f727b3f8d73a0d3f53297b1c354bab8bbcf6a1adef5c31cdc6b5c326f6`; `document-controller-tests.exe` SHA-256 `06c6c32ddb7e33cb6dee23067d4fd2f416ec90e064f8f25148b0bc67dab2dca7`. No app rebuild or foreground/device test was needed for this foundation. The portable departure target is registered in both build definitions; Mac execution of this new source is still required at the cohesive P0b gate.

**Next implementation work:** register every owner family in the handoff census with exact raw-generation summaries, including nested formula/bank drafts and hidden Main-bottom fields. Add the native Review/Discard/Cancel departure flow and structured API refusal. Pass the observer from Application; keep document-scoped input deferred during admission while continuing worker service. On successful adoption, retain that lease until UI refresh/old-owner retirement completes; on failure or cancelled chooser, release without erasing drafts. Apply the same token policy to Close, bounded session end and recovery. Recheck both take types at final admission. Then build the app/workspace target once for the cohesive native integration and run the full owner census plus actual app Open/Close/recovery race checks. Tests of a fake registry owner do not substitute for those native registrations or raw-text retention checks.

P0a CI remains independent at `8afd4175a`: Apple Silicon job `113780223733` in run `37918435661` was observed successful; Windows job `113780223549` and Intel job `113780224030` were still live at the latest poll. No duplicate runs were started. Their eventual results qualify that isolated P0a source, not this P0b foundation. The previous planning turn made review progress; this resumed goal turn adds implementation and targeted execution evidence.

## P0a retained-editor qualification repairs

The existing x64 job at PR merge `29149338` passed build, portable/worker and scratch checks, then failed workspace and graph-curve native checks. Local diagnostics established two distinct issues:

- At 96 DPI, the Graph Curve shape combo occupied `[8,52,154,80]`, while the axes began at y=79. The real one-pixel overlap is repaired by measuring closed combo bounds before placing the axes. Requested dropdown heights cannot stand in for closed HWND geometry. Existing 100–120-DIP short canvas and exact 272-DIP floating canvas assertions remain intact. The native suite now exercises 96 DPI as well as the developer display's 192 DPI through a fixture-local thread context.
- `SetWindowPos` obeyed monitor-derived maximum tracking bounds. Even the local requested 1440×900-DIP client was actually 2880×1759 pixels at 192 DPI, rather than 2880×1800. The private restore fixture now temporarily establishes its exact requested frame maximum and asserts the resulting client size. It additionally reproduces a capped-small-desktop case and requires compact reflow. Production maximum/minimum sizing, focus, raw-field and independent-region assertions are unchanged.

The five previously unreached Python app cases exposed two fixture issues. A wheel message carried screen `(0,0)` and was correctly ignored outside the tracker; the test now verifies that rejection, then sends real tracker screen coordinates and retains both scroll-direction assertions. A graph read raced the curve's coalesced preview; the test now uses the existing bounded quiet/readiness observation before the read, without retrying any edit or weakening draft/target assertions.

Source commits on the safety branch: `13a5be2ab` (layout/native fixtures), `758cd1559` (app fixtures). Corresponding isolated P0a commits: `25a97241c`, `8afd4175a`, based on `4a5c8ed88`; later P0b cable/role changes are not included in that branch.

Local ARM64 Release evidence, no physical device or foreground claim:

| Check | Result / retained log under `bin/parity-evidence/` |
|---|---|
| 96-DPI graph failure before repair | Reproduced; `p0-layout-graph-before.log` includes exact rectangles |
| Original workspace geometry | Pass at larger local work area, with clipped client height recorded; `p0-layout-workspace-before.log` |
| Focused native/app rebuild | Passed; `p0-layout-build.log` |
| Graph Curve native suite, display DPI plus 96 DPI | Passed, 2.72 s; `p0-layout-graph-after.log` |
| Full workspace restore and Precise Notes native suites | Both passed, 83.33 s + 5.60 s; `p0-layout-retained-after.log` |
| Five app integration cases | Three passed initially; two diagnosed failures retained in `p0-layout-integration.log`. Both corrected cases passed, 4.799 s, in `p0-layout-integration-corrected.log`; unchanged successful cases were not rerun |

`p0-layout-receipt.json` records safety-branch source `758cd1559bb3e9be13093e1263476708e49d6081`, 1,823 tracked source/dependency input hashes, CMake cache and log hashes. Executable SHA-256: app `2e43cc3305c1239cb79d9a54e2afb3da509402c3987a84531ca48a8916ac18f2`; workspace `1b9f3dc5621e7b6c7989022472b3cad35761ffd96d9517990e8b67abd161ac10`; graph curve `381f04441e54dca99e673194b823d505d1701af97a39b2914aa0560b2caf4122`; precise notes `ecbad7f684264af8d31a79b7ca0b07762a9b1ec890ddc9389e198444296a9da0`.

Both jobs in existing Mac run `37913656837` completed successfully before the new push: app build, 120 CTests, Swift recovery and picker checks. Intel's native tests took 240.22 seconds. Their checkout remains PR merge `29149338` of `4a5c8ed88` into `ffe81aa4b`. The isolated P0a head `8afd4175a82eba3dc0331e6951b55e3ea891ae72` was then pushed to the existing draft PR 3, avoiding cancellation of that live evidence. New exact-candidate CI is still required; local safety-branch execution is not substituted for it. In particular, actual-app tests requesting large windows may expose the same runner geometry limit separately from the now-fixed in-process fixture. Do not skip their assertions if that occurs. No source migration, broad UI redesign or aggregate raw-draft registry is included in this correction.

New runs confirmed live for `8afd4175a`: [Windows 37918435648](https://github.com/rewbs/ScreamSeq/actions/runs/37918435648), [Mac 37918435661](https://github.com/rewbs/ScreamSeq/actions/runs/37918435661). Inspect those handles rather than starting duplicate runs. The managed `parity-ci-layout` worktree remains at the isolated PR head for any demonstrated follow-up; the main task checkout keeps the broader safety candidate and complete reviewed plan.

## Resumed P0a: exposed Windows worker failures

The original CI runs are terminal failures: both Mac jobs reached the already-repaired Swift expression failure, while Windows x64 compiled and passed 60 of 62 worker checks. The two failures were reproduced in the isolated local ARM64 build before editing:

- `document-controller-live-graph-publication`: a full prepared graph-control queue escaped as `std::runtime_error`, yielding the generic engine error instead of guarded `-32002`. The native preparation boundary now translates host preparation exceptions consistently with rack preparation. It does not alter queue budgets, publication ordering, audio, history or accepted model state. Existing full-queue, bypass, Undo/Redo and persistence assertions remain intact.
- `document-controller-live-native`: the test still expected a valid processor addition to be unsupported. The current shared implementation supports prepared live recipe topology. The replacement case explicitly refuses publication and asserts unchanged exact graph/view/history/transport, then accepts the same operation and verifies rendered transition, one Undo and one Redo with exact graph/stable-identity restoration. It does not remove rejection coverage.

After the repair, all four focused publication/native cases passed (plus their automatic scratch-directory setup), then all 25 `document-controller-*` CTest entries passed in 14.61 seconds. This covers worker/model behavior with the real prepared render chain, not a physical device or foreground UI. Only `document-controller-tests` was rebuilt; no musician process was present and no audio defaults were changed. One intermediate test compilation exposed a mixed-type `auto` declaration; it was corrected before the successful rebuild.

Evidence: `bin/parity-evidence/p0-exposed-worker-before.log`, `p0-exposed-worker-build.log`, `p0-exposed-worker-after.log`, `p0-controller-regression.log`. Current ARM64 Release `document-controller-tests.exe` SHA-256: `9efb29082d42cccc9566bf66bed1c1a5bd2d9fa178f112efe1f0bc142b42cb59`. The older hash below remains the earlier receipt. Both-platform CI must still qualify the final candidate before P0a integration.

## P0a candidate

Base: main `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. Initial implementation commit: `605c7c688e8fb5e3876b1b086ea6b4f1a1e17309`. [Draft PR 3](https://github.com/rewbs/ScreamSeq/pull/3) holds this batch.

- Windows has separate MIDI/sample retained-take checks and one aggregate guard. A second MIDI check covers message pumping during the sample read; close-error fallback also protects current MIDI state. UI guards do not silently rewrite the server's separate save/open contracts.
- The Mac recorder uses an explicitly stopped/joined monitor compatible with the supported libc++ instead of unavailable `jthread`/`stop_token`. Restart resets the stop request; disposal follows join; capture-limit/disconnection still stops the AudioUnit without polling from the UI.
- Native regression cases cover both retained take types, failure and reentrant MIDI creation, plus monitor restart, explicit/autonomous stop, join and destruction.

### Failures exposed after restoring compilation

1. **Workspace regression expectation:** `workspace-restore-tests` passed its new take guards, then expected raw `.375/.75` in a typed percent field. `NativePatternCommands.cpp` declares strength display units as percent, and production `PatternFields::text` presents `37.5/75`. The assertion now checks that display and additionally requires exact raw strength, offset and duration after reading the fields. No tolerance or behavior assertion was removed. Full workspace suite passed after this correction.
2. **Mac Swift compiler limit:** [Apple Silicon job 113750668968](https://github.com/rewbs/ScreamSeq/actions/runs/37909410789/job/113750668968) passed the recorder C++ compilation but failed at `WorkspaceIntegration.swift:110`, reporting “the compiler is unable to type-check this expression in reasonable time”. The additional-menu collection is now explicit typed accumulation in the same order, with the same visibility/content checks. This repair requires a new native CI build; it has no local Mac execution claim.

### Local Windows evidence

Dedicated directory: `bin/windows-parity-p0`, Release ARM64, Visual Studio 2022 / MSVC `19.44.35229.0`. The app, workspace, document-controller and sample-recording targets built successfully. Only the workspace target was rebuilt after its assertion correction. No system audio defaults or musician process were changed. Private-desktop lifecycle checks explicitly verified foreground and clipboard preservation.

| Check | Result | Retained local log |
|---|---|---|
| App/API recording and native MIDI review | 6 passed; initial 4 audio-dependent cases skipped | `bin/parity-evidence/p0-recording-app.log` |
| Those four cases with explicit silent output | 4 passed, no skips: timestamped chord/commit/history/reopen, stop draining, queue-loss retention, autosave/recovery | `bin/parity-evidence/p0-recording-silent.log` |
| Full native workspace suite after percent expectation correction | Passed in 80.75 seconds, including all new take-protection cases and existing retained-owner checks | `bin/parity-evidence/p0-workspace-native-pass.log` |
| Worker recovery, worker recording, sample recording | 3 tests passed, plus automatic scratch-directory fixture setup | `bin/parity-evidence/p0-worker-recording-pass.log` |
| Initial target build / workspace rebuild | Exit 0 / exit 0 | `bin/parity-evidence/p0-build.log`, `p0-workspace-rebuild.log` |

The initial CTest invocation used a relative output-log destination that was not retained where intended. The successful workspace `LastTest.log` was copied before another CTest run could overwrite it; the three cheap worker checks were repeated once with an absolute log destination to retain their actual output. The first failing workspace child log remains in the private fixture's reported temporary directory. These are qualified local results, not a whole-suite or hardware pass.

Executable SHA-256 identities:

| Binary | SHA-256 |
|---|---|
| `ScreamSeq.exe` | `78408717255f0c62f4077b9223c5a6a29af73f6f5da70d4074f2e203865ee225` |
| `workspace-restore-tests.exe` after assertion correction | `9769ea210547c959098f89a24da7f49864954ff6b0b9af9ffa5eb85823e2a838` |
| `document-controller-tests.exe` | `3f53c85fe446fab56530ef0ca0266e62dcba4ec196ad1af2dffdd99f51406fbc` |
| `sample-recording-tests.exe` | `43393dae07affa28847ee91e6ea00aaf5d52db0804ad37787d94607f35916e6e` |

### Remaining P0a gates

Windows x64 and both Mac CI workflows must complete successfully at the final candidate, including tests previously masked by compile failures. Any further failures need diagnosis and repair. Controlled recording tests do not establish physical device/disconnection behavior. The monitor's actual Mac lifecycle tests still need their native execution result. P0a is not merged or complete on the evidence above.

## Next batches

P0b covers retained draft protection and role/field-safe graph edits. P0c establishes typed fixture/API conformance, resolves the Mac interface harness failures and records accurate scheduled test coverage. The shared/native architecture, acceptance recipes, effort estimates and P1–P8 scope remain exactly those in the plan. New evidence updates this receipt; it must not silently convert an unexecuted matrix row to Pass.

### P0b cable safety slice in progress

Temporary branch `codex/windows-parity-safety` starts at P0a candidate `4a5c8ed881a3168b7804ed6ddad8a62ed771c757`. P0a CI runs `37913656837` (Mac) and `37913656606` (Windows) were confirmed live after the push; the safety branch does not invalidate those inputs. It must converge on main after the prerequisite qualifies.

`GraphCableEdits.hpp` now creates zero-depth modulation wires with an enabled peer's base/mode, or a catalogue-derived normalized manual base with explicit discrete mode. An all-disabled peer set does not impose a stale base on a new enabled cable. Same-target rewiring preserves a disabled cable's own settings. `GraphEditor.inc` copies both drag endpoints before reading the worker catalogue and rechecks captured draft generation, graph and document revision afterward. The new-wire form defaults to zero depth and a blank automatic Base; a typed base remains explicit. Selected-wire edits copy the original JSON before replacing displayed fields, preserving quantized/enabled and untouched data. Normal and staged first-open defaults agree. No project version, API schema, shared DSP or Mac source changed in this slice.

The graph fan-connection model test passes, including new baseline/discrete/disabled-peer cases and existing dry-run, no-op, Undo/Redo, metadata and unrelated-edge cases. Four actual-app private-desktop tests pass in 6.761 seconds: socket creation and neighbor preservation, new-wire form defaults, disabled quantized field edits with history/save/reopen, and audio fan-out/rewiring. These are native HWND/pipe checks, not foreground visual or hardware audio evidence. All 27 graph/workspace CTest entries then passed in 81.53 seconds (26 graph cases plus the full workspace suite). Four additional native graph-editor cases passed in 5.375 seconds, covering parameter transaction/history, cancelled/stale drafts and keyboard history, assignments, compact bounds and invalid-cycle refusal. Draft-registry and stage-role work remain outstanding; P0b is not complete.

Logs are `bin/parity-evidence/p0b-cables-build.log`, `p0b-cables-model.log`, `p0b-cables-ui.log`, `p0b-cables-neighbor-ui.log` and `p0b-cables-regression.log`. ARM64 Release hashes for this slice: app `953ad5f8bcd0de49a53997c322d55f3d1b194df83f9e26d7106bf5be5dbea9ff`; graph-document tests `7affa4d37119bf1515570841cd92de7cd8c200bb54239195d6798c2993b7ad6e`; workspace-restore tests `ea045d9be717f62477dac231928ca907861266fd6bd4fc87d9a5485191d856f0`. Native catalogue-read generation checks still need a deliberately pumped replacement/draft regression in the upcoming owner-safety batch.

### P0b command-stage protection

`SongRoutingCanvas::Node` now carries an explicit Row/Persistent/Ordinary/Instrument role and assignment/insert capabilities through projection and the workspace snapshot. Row/Persistent inspection displays the selected stage's recipe, with no misleading Ordinary amount/wet values. Both HWND enablement and the assignment/insert handlers reject attempts to change the Ordinary assignment or regular insert chain from those cards. The existing bus, Ordinary and sample-instrument assignment paths remain available. Persisted display/layout IDs are unchanged. Aggregate auxiliary routing and individual-copy observation remain separate existing contracts; this slice does not add exact-copy audio routes.

Four actual-app private-desktop cases passed in 19.691 seconds: role-specific inspection, all six assignment/insert mutation commands dispatched past disabled HWNDs without document/history changes, shared-recipe navigation, Ordinary clear/Undo, stage overview/filtering, instrument assignment/expansion, and insert-order/layout history/save/reopen. The full workspace regression also passed in 83.26 seconds. Its added catalogue case deliberately pumps a newer raw field edit during the real worker read and proves the stale completion refuses while retaining exact text, graph and song/history. This closes the raw-draft race check noted above; document replacement during the read still belongs to the remaining aggregate-owner admission work.

Build/test logs: `bin/parity-evidence/p0b-stage-build.log`, `p0b-stage-ui.log`, `p0b-stage-workspace.log`; detailed child assertions are retained in `p0b-stage-workspace-detail.log`. ARM64 Release app SHA-256 `6f449e3ab5f30360d2399108c8886f8f1ea74f7130088d26d14060e63970cf55`; workspace test SHA-256 `b51ad1974e22c8cd88dc826173fadf7acce573fce690946bed8a6bc41578538e`. Shared model/audio and Mac sources are unchanged; these results do not establish foreground rendering, physical device qualification, supplied-fixture reciprocal parity or completion of P0b. The P0a CI jobs were re-polled and remained live; they were not restarted. Next: aggregate raw-draft protection across native Close/Open, API replacement and recovery, with bounded shutdown admission and deferred discard.
