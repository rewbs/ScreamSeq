# Parity implementation progress

Implementation resumed after the documentation-only review under the active user goal, “Go ahead with the implementation as per the latest plan.” The complete scope is the [reviewed parity plan](README.md); [current source review](current-source-review.md) retains the planning checkpoint. P0a is not yet fully qualified or merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding. Earlier receipts below retain their original scope and dates.

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
