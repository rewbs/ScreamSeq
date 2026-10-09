# Parity implementation progress

Historical receipt of implementation already present before the current planning-only review. Its earlier implementation authorization does not authorize execution under the current request. The complete proposed scope is the [reviewed parity plan](README.md); [current source review](current-source-review.md) distinguishes main and candidates. The results below retain their original scope and dates. P0a is not recorded here as fully qualified or merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding.

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
