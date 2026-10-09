# Parity implementation progress

Implementation has resumed under the active user goal, “Go ahead with the implementation as per the latest plan.” The preceding [planning review](review-refresh.md) was documentation-only; its provenance and observations remain historical. The complete scope is the [reviewed parity plan](README.md). P0a is not fully qualified or merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding.

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
