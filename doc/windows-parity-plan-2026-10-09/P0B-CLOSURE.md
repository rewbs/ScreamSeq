# P0b closure checkpoint

This is the finite remaining-work checklist for the implementation authorized after the original planning-only review. It supplements, rather than replaces, the five exit requirements in [final-planning-review.md](final-planning-review.md). Product checkpoint: `daddecd6c9c9a590d80533158a84b26dbfce93d1`; inspected branch HEAD: `eaa3863b8`. Remote main was rechecked at `16cab100b898f66726cfab6c2386887a9bcf5fd4`. P0a is merged. P0b is unmerged and incomplete; P0c–P8 remain required.

**Execution decision:** prepare one grouped closure candidate, then qualify that frozen candidate. Do not build after each owner/control change. Aim for at most one consolidated build cycle per hour. The preceding ARM64 build finished at 19:06:01 UTC on 9 October; the next normal build checkpoint is no earlier than 20:06 UTC. Source work and fixture preparation continue in between. Failed necessary checks still require correction and targeted rerun; the cadence cannot turn a failure into a pass.

This checkpoint used source and retained evidence only. No build, test, application launch or device operation was run. [closure-inspection.json](closure-inspection.json) pins the inspected files and evidence comparisons before the subsequent, unbuilt C4/C5 preparation recorded in IMPLEMENTATION.md. An implementation found in source is not a current runtime qualification. The owner census below establishes coverage of ownership registration, not proof that every field is represented.

## Exit requirements and remaining work

| Original requirement | Present implementation | Remaining closure work |
|---|---|---|
| 1. Retain uncertain operations and reconcile each side-effect domain without replay | `NativeWriteCompletion.hpp`, per-call `NativeCallReceipt`, prepared receipts for bulk imports/render/Keep; dedicated plugin-path, browser, preset, sample and instrument readback | C1–C4 below: remaining bank/catalogue and Main command boundaries; genuinely unknown render/import/Keep; prepared single-asset receipts. Reuse the present machinery |
| 2. Census and no-throw post-adoption retirement | `DocumentDraftRegistry.hpp`, `NativeToolWindow.hpp`, eight Main callbacks, automatic child registration | Bank capture-name omission; explicitly handle Main command outcomes that have no draft owner. Preserve global settings. No wholesale owner rewrite |
| 3. Application final admission and exact consent | `DocumentDepartureIntegration.inc`, Application implements `DocumentReplacementAdmission`; strict snapshots, same-owner Review, structured API refusal | Qualify the final combined candidate; include newly covered owners in exact consent/departure fault cases |
| 4. Lease through adoption/retirement/refresh, take guards, safe recovery stack | `NativeInputGate.hpp`, native synchronous/queued input interception, retryable completion and Close/session-end rollback; both take guards | Preserve these contracts while adding result owners. Test recording result loss and save completion without relying on a song revision for external effects |
| 5. Coherent platform and fixture integration gate | Extensive scoped ARM64 receipts and native/private-desktop tests already exist | C5/C6: non-overlapping test scheduling; frozen ARM64/x64/Mac checks, actual departure paths, supplied F04/Parity WAVs and controlled capture. Current ARM64 evidence alone does not close this requirement |

## Owner census

There are **29** classes deriving directly from `NativeToolWindow` in `windows/App`, **24** with `documentDraft()` overrides, and **eight** Main registrations in `DocumentDrafts.inc`. The machine-readable inventory records exact source paths. These counts correct the preliminary commentary count; they are not completion percentages.

| Native owner (`windows/App/<name>.hpp`) | Captured ownership / draft boundary | Disposition |
|---|---|---|
| AbsoluteAutomationWindow | Document/revision, plugin/parameter, raw generation, curve/drag/pending | Keep current registration |
| ArrangementMatrixWindow | Document/revision/sequence and pending request generation | Copied block/navigation is not itself an unsaved song edit; preserve pending guard |
| ArrangementWindow | Document/revision/sequence; section/pattern subdraft targets and generations | Keep current registrations and nested summaries |
| EnvelopeBankWindow | Captured document/revision, source target, selected bank entry, point/timing/master draft | **C1:** captureName currently ignored; bank/catalogue completion not retained |
| FormulaWorkbenchWindow | Parent document/revision/target/point and raw source generation | Reference-only window has no song draft; mutable child retains parent identity |
| GraphCommandsWindow | Document/revision, pattern ID, bus, column, row and dirty/pending | Keep current registration |
| GraphCurveWindow | Document/revision, graph/node/pattern ID, curve/drag, child opening | Keep; bank child participates in C1 |
| GraphTrimsWindow | Document/revision, graph/node/port, dirty/pending | Keep current registration |
| GraphWorkflowWindow | Captured document/revision, graph description, raw dirty/pending | Keep current registration |
| InstrumentEnvelopeWindow | Envelope/point/tool drafts plus creation target and receipt | Keep; C4 strengthens worker receipts; bank child participates in C1 |
| MultisampleImportWindow | Document/revision, family, raw generation, pending/receipt | **C2:** truly unknown result currently cannot be resolved by Review |
| ParameterActivityWindow | Document/revision, target/plugin/parameter/original frame, raw fields/pending | Keep current registration |
| ParameterAutomationWindow | Document/revision, stable pattern/plugin/parameter; point/tool/drag/pending | Keep; bank child participates in C1 |
| PatternSampleRenderWindow | Document/revision and captured range; raw options/pending/receipt | **C2:** unknown Review only retries finishResult and throws |
| PluginInstrumentsWindow | Document/revision/plugin and dirty/pending | Keep current registration |
| PluginLibraryWindow | plugin.add pending/unknown document result | Global preferences additionally protected by protectsClose; retain that distinction |
| PluginPathWindow | Stable plugin target, captured document/revision, raw baseline/generation, completion/scan intent | Known receipt and unknown location/candidate readback exist; reuse |
| PreciseNoteWindow | Document/revision, stable pattern/track/row, point/tool/drag/pending | Keep current registration |
| SampleDetailWindow | Stable sample ID, original mutation target, ranges/strokes/loops/raw generation and completion | Staged reads and unknown-domain review exist; reuse. Deleted-target readback still needs explicit bounded disposition |
| SampleLibraryWindow | Chooser/import completion captured in original document | Independent roots/rescan/preferences have separate protection; reuse |
| SampleRecordingWindow | Setup/lifecycle/Keep context, take/device/input/generation | Start/Stop/Discard observation exists; **C2:** truly unknown Keep still throws indefinitely |
| ScratchGestureWindow | Document/revision, gesture ID, dirty/drag/saveNeeded/pending | Keep current registration |
| SongRoutingWindow | Document/revision, selected node/wire, dirty/layout/drag/pending | Keep current registration; command-stage assignment guards already exist |
| SongTimingWindow | Captured document/revision/sequence and retained raw fields/pending | Keep current registration |
| AudioSettingsWindow | Global device settings | No song draft expected; native device/settings ownership remains separate |
| WorkspaceLayoutWindow | Global workspace preference draft | No song draft expected; layout preference retention is intentional |
| AuditionWindow | Live note ownership | No retained musical edit; release notes at departure |
| MidiRecordingWindow | Global setup plus session-owned take | Take guard exists; **C3:** inspect/retain actual lifecycle/completion at Application boundary, not by inventing a song draft for global preferences |
| RecoveryWindow | Restore action stack, reads and deferred Close | Existing recovery lifetime/admission protocol; preserve and qualify |

Main owners: **Pattern FX**, **In-grid nudge**, **Sample range**, **Mixer**, **Plugin parameters and programs**, **Plugin preset result**, **Graph recipe**, **Render selection result**. Each has an explicit retirement callback. Numeric Pattern FX coordinates are bound to the captured document/revision; their use alone is not evidence of wrong-target editing. Nudge uses stable pattern/track IDs. Preset/direct-render owners prevent aggregate discard while unresolved. Main sample edits, MIDI operations and document Save currently use generic `documentOperation` rather than a persistent result owner; see C3.

NativeToolWindow captures parent ownership before child reads and registers no-throw disable/hide/destroy retirement. Registry admission stages callbacks before obtaining the lease; it skips children already unregistered by parent destruction and does not replay successful cleanup. Application clears C++ owners only after adoption at the safe completion boundary. These implementations should be retained, not replaced with a second departure protocol.

## Fixed closure candidate

### C1 — Bank drafts and bank/catalogue outcomes

**Confirmed source findings:** `EnvelopeBankWindow::action` returns immediately for captureName, without a generation or draft change. Both `retainedDraft()` and `documentDraft()` omit that raw name. `call` resets pending after an exception but stores no result/intent. Save master clears dirty before its fallible refresh. `EnvelopeOperations::invoke(envelope.catalogue.publish)` changes the catalogue file and only then constructs its return value; a song revision cannot establish this file effect.

Implement together in `EnvelopeBankWindow.hpp`, its children/call sites (`GraphCurveOwnerChildren.inc`, `ParameterAutomationWindow.hpp`, `InstrumentEnvelopeWindow.hpp`) and `EnvelopeOperations.cpp` as needed. Track the captured name separately from the editable master so typing a name does not disable Save captured curve. Include it in generation/retention/consent even in catalogue scope. Stage fallible bank/catalogue reads before replacing fields. Use the existing completion ticket and retain method, source/entry/catalogue IDs, document/revision, catalogue revision and submitted raw generation. Review reads the bank and catalogue domains; observation alone must be labelled unverified and never claim which request created an entry or Undo. A newer draft remains at its original revision until explicit refresh/discard. Do not re-publish, re-import, re-apply or re-save during Review.

Acceptance: hidden child with only captureName edited blocks API replacement and appears in native review; canceled/stale consent retains it. Lost response after each allocation/publish path cannot create a duplicate on Review. Failed/malformed refresh and later typing retain raw master/name/selection. Catalogue revision changes at equal song revision require catalogue readback. Parent destruction cannot leave callbacks into a deleted child. Add these to existing census/nested and actual Application receipt fixtures, not a separate per-control build.

### C2 — Finish unknown render/import/Keep recovery

**Confirmed source findings:** unknown-without-receipt paths in `PatternSampleRenderWindow::finishResult`, `MultisampleImportWindow::finishResult`, `SampleCaptureIntegration::finishDirectSampleRender` and `SampleRecordingWindow::finishCommit` currently throw repeatedly. Known receipts already avoid replay. Four prepared worker receipts reduce the occurrence of unknown outcomes but do not eliminate this required fallback.

Reuse the existing sample/instrument creation observation and explicit acknowledgement pattern. Read and validate original-document sample/instrument catalogues and, for Keep, `sample.recording.get` with the original take identity. Do not infer authorship from names, matching PCM, a vacated take or equal revision. Record an unverified observation separately from an exact receipt. Release uncertainty only through an explicit state-checked action; preserve newer raw work and require a fresh target/dry-run before a new operation. A deleted stable target is a valid absence observation when a validated catalogue proves absence, not a reason to retarget a reused slot or trap the owner forever. Failed/malformed reads and changed context retain uncertainty.

Acceptance: known/unknown outcomes, missing/reused slots, intervening edits, new take, malformed readback, newer fields and stale acknowledgement across the four entry points. Review and acknowledgement perform zero writes. Known receipt preserves exact original result and one Undo; unverified observation makes no history claim. Exercise F04 and supplied Parity WAVs at C6, sharing the frozen binary.

### C3 — Main command, MIDI and Save completion boundaries

**Observed:** Main sample paste already checks stable selection ID, captured revision and range after the clipboard read (`SampleEditor.inc:sampleCommand`). Do not recreate this guard or claim it is absent. The final write still calls `edit`, which unwraps `documentOperationWithOutcome(...).result`; the command has no persistent result owner. Main's generic completion wrapper correctly preserves a returned CompletedCall on ApiError, but top-level status handling does not by itself retain a Review owner. MIDI finish can report “Take retained” after an exception even when recording.commit returned before native completion failed. Save can likewise throw after file promotion or recovery cleanup. These are result-boundary gaps, not proof that music/PCM was lost.

Provide a small reusable native command-result owner for these explicit entry points, using NativeWriteCompletion rather than separate ad hoc state machines per button. Persist the submitted request/captured target and the exact receipt when present. Use domain readback: sample metadata/catalogue/clipboard for sample commands; original take and document state for MIDI; original destination plus validated persisted project state and recovery metadata for Save. Do not alter API wire semantics or globally re-route every musical edit through a new controller as a prerequisite. API-originated requests retain their structured returned outcomes; avoid opening native modal prompts for API calls.

Add the owner to final admission and review/retirement without deleting an object on its active stack. Retained MIDI uncertainty must not auto-start or consume another take. Stop/note release and required worker callbacks remain available. Save Review does not overwrite the file or delete recovery data again; distinguish an exact completed save from a file merely matching current state. Preserve the existing chooser revision and take guards. Audit the bounded list of direct commands here once while wiring the common helper; unrelated ordinary edit usability moves to its existing later phase.

Acceptance: inject a fault after a real command returns and before/after native refresh. Preserve exact result/target and allow read-only Review with no extra history, import, clipboard replacement, capture or disk write. Test unknown paths, external changes, unavailable readback, later typing, departure/canceled shutdown, and Save failure during recovery cleanup. Existing sample paste stale-selection guard must still reject. Test MIDI lost Stop/commit/discard and Start before another automatic recording opportunity; compare actual retained take rather than status text.

### C4 — Complete prepared asset result publication

**Confirmed source findings:** `AssetOperations.cpp` prepares receipts for importMany and importMultisample, but single sample.import/instrument.import construct `{index}` after commit; both instrument.create paths also construct their response after mutation. `DocumentController::prepareAssetCompletion` already supplies the allocation-before-commit/no-throw-publication boundary.

Extend that boundary to these single-asset operations. Prepare candidate, validate, construct exact result and receipt, Stop, commit, then publish receipt without allocation. For instrument.create, preserve existing first-instrument/sample mapping, stable identity allocation, plugin-slot exclusions and one transaction/Undo. Dry-run/no-op must not publish a fictitious committed receipt. Only extend NotCommitted classification where the method's entire side-effect contract proves it; unchanged song revision is insufficient for catalogue, recorder or filesystem operations.

Acceptance: before-Stop, failed Stop, failed candidate/commit and post-commit publication/native callback faults. Exact method/document/revision/result survives; precommit refusal has no receipt; no-op/dry-run and initial instrument mapping stay equivalent. Use existing prepared-asset, API/worker and instrument-creation tests. This is grouped with C1–C3, not its own build milestone.

### C5 — Make validation groups non-overlapping and select them explicitly

**Confirmed source findings:** default `WorkspaceRestoreTests.cpp` runs sample readback, sample mutation, instrument creation, preset and native completion cases that also run in named groups. The new readback group alone took 67.86 seconds at its qualified checkpoint. All default work still shares the private child deadline. The Windows workflow selects workspace-restore, precise-note and graph-curve only; build.ps1 selects portable and worker labels, which do not substitute for all new workspace groups. This is a scheduling/coverage defect; an actual current-CI timeout is not claimed.

Keep ordinary workspace scenarios in the default group and run each recovery scenario once under an explicit group. Register any new closure group(s) in windows/CMakeLists.txt and list all required groups in the Windows workflow. Keep the existing private-process and CTest time bounds; do not weaken assertions or merely increase deadlines. Maintain a source-level mapping of scenarios to groups so moving a scenario cannot silently drop it. Split long groups by dependency/fixture cost only when needed; one test executable can serve several bounded processes without recompilation.

### C6 — One final P0b integration gate

1. Freeze C1–C5 product/test/fixture inputs together. Inspect diffs and scenario registration first. Record source/dependency/toolchain/provider/fixture hashes and unique logs. No per-owner build.
2. In one scheduled cycle build the required Windows ARM64 native/worker targets and new/changed targeted fixtures. Run portable consent/completion, affected asset/envelope/controller cases, all required workspace groups and actual application command/departure cases. Use private desktops and disposable files. Preserve first failures.
3. Common lifetime/dispatch changes require workspace restore, shortcut, musical typing and recovery coverage. Broader audio/codec suites are triggered by changed shared music/audio/serialization inputs or unexplained output differences, not by a changed label alone. Reuse earlier test evidence only when all relevant transitive inputs, compiler, fixtures/providers and scenario match; equal executable bytes alone are insufficient.
4. Qualify Windows x64 and affected Mac Apple Silicon/Intel shared-header/session/shutdown builds/checks against the same candidate. Update native callers together when a shared contract changes. Avoid building the identical commit again after a docs-only edit. CI execution is part of the consolidated gate, not one dispatch per small fix.
5. Use disposable F04: dry run, exactly one committed selection render, induced completion loss, Review with no rerender, one Undo, exact PCM save/reopen. Use all three supplied Parity WAVs for retained import. Use generated controlled capture for Start/Stop/Discard/Keep and mixed MIDI/sample take guards before hardware qualification. Run native/API Open, recovery, Close and canceled/completed session-end with hidden/docked/floating owners, stale/newer input, deletion, failed summary, nested destruction and retryable cleanup.
6. Check the gate's touched fixture/project fields reciprocally on both platforms, preserving stable IDs, opaque plugin state, sample bytes and retained attribution. Full F01–F05 typed no-edit and edited reciprocal exchange remains mandatory in P0c/P6/P8 and cannot be claimed from one F04 check. Integrate on shared main only after the required candidate gates; no permanent platform branch.

## Existing evidence and deliberate limits

The latest `p0b-sample-readback-receipt.json` records 1,861 source/dependency inputs, two executables and four logs: four native groups passed, including 31 staged-read scenarios, plus nine actual-application cases with no skips. Its scope is Windows ARM64/private desktops. Earlier departure, retirement, input-gate, plugin path/library/preset, recording lifecycle, prepared asset and browser receipts remain supporting evidence for their exact checkpoints. Do not silently relabel them as the final candidate.

Graph command-stage identity and modulation cable defaults are already implemented in `SongRoutingCanvas.hpp` / `SongRoutingWindow::assignGraph` and `GraphCableEdits.hpp` / `GraphEditor.inc`. Row/Persistent stages are distinguished from ordinary assignable stages; new modulation bounds begin at zero, target-wide base/quantization are preserved, and replacements retain original edge fields. Qualify these required F22/F23 behaviors at the gate; do not budget another port of already-present code.

Relative remaining effort: C1–C4 together **M–L**, high correctness risk; C5 **S**, low code risk but essential coverage; C6 **M**, dominated by platform/fixture execution and any actual failures. These are relative estimates, not a percentage or a promised completion time. No additional product design decision is needed for this closure scope. Physical device/vendor requirements remain later release qualification inputs.

Mixer, track/group editing, transform workflow, running-loop/playback scope, keyboard/display preferences, manual automation recording, entry/export workflows, graph ergonomics and physical accessibility/device qualification keep their existing P1–P8 placement. New findings must identify which of the original five P0b exit requirements they violate before extending this closure batch. Desirable unrelated refinements do not restart P0b discovery.
