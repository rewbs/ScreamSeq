# Recorded take and graph-retention batch

## CI checkpoint at 14:03 UTC

The following results qualify `7fb99d0b2` / its identical merge tree only,
not the subsequent unbuilt native vendor-ownership source.

- Windows run **39430710486**, job **118654733984**, is terminal **failure**.
  Build passed (finished 13:45:28); all **157 worker/model** and **48 native**
  registrations passed. The application selection ran 86 tests: **74 passed,
  10 failed, 2 skipped**. The 13 failure reports include three additional
  teardown failures for already-exited audition processes. The ten affected
  cases fail at audio startup/playback-clock acquisition, matching the earlier
  endpoint-dependent failure set; there is no new graph fixture failure.
  This is not a green application gate. Actual audio availability and the
  current binary still require separate evidence before closing those cases.
- Mac Apple Silicon run **39430710525**, job **118654733396**, is terminal
  **success** (13:54:38). All **134 native tests** and **35/35 interface groups**
  passed, including the protected-source history correction and real native
  plugin capture. Actual socket editing contracts and all five original native
  fixture save/reopen cases passed. This is not a foreground visual review.
- Mac Intel job **118654733679** remains live in its original build. Do not
  restart it or dispatch another native build.

Complete terminal job logs are retained locally as
`bin/recorded-take-20261020/ci-118654733984.log` and
`ci-118654733396.log`. The prior reciprocal exchange remains attached to its
own candidate lineage.

The earlier gate below is superseded: use **14:54:39 UTC at the earliest**
(7:54:39 a.m. Pacific), and extend it to one hour after the later Intel
compilation/harness completion. The conservative ARM bound includes its Swift
interface-harness compilation, not just the application build.

## Frozen candidate and scope

Candidate `7fb99d0b2b50f3827e16bcf59e295d4a9b4f85b5` is pushed to
`codex/plugin-edit-parity`, in draft PR 5 against shared main
`07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`. Main was fetched before freezing;
there is no intervening main change.

This batch includes the shared take owner and intake/publication tests,
promotion of fatal identity failures after reviewable input loss, resolution
of delayed timestamps once per provider poll, real Windows fixture callback
and Mac NSSlider capture checks, the Windows graph busy-selector repair, and
the Mac protected-source history fixture correction. Details and remaining
native integration are in [take ownership](RECORDED-TAKE-OWNER-2026-10-20.md)
and the [preceding CI review](GESTURE-CI-REVIEW-2026-10-20.md).

Source review and `git diff --check` completed. Neither is an execution pass.
The native callback checks use controlled presentation intervals; they do not
prove device latency, recovery, manual-state exclusion or final Record UI.

## One coordinated build

The hourly gate was **13:27:48 UTC on 20 October**. Local Windows ARM64
compilation began at **13:28:14.609 UTC**, with two jobs and five requested
targets: ScreamSeq, workspace-restore-tests, document-controller-tests,
recorded-automation-tests and plugin-edit-notification-tests. The existing
qualification checkout was deliberately advanced to the frozen candidate.
The earlier `GestureBatch-Release` binaries and evidence were preserved;
the new output is `bin/windows-parity-p1/RecordedTake-Release`.

Local receipts and logs are in `bin/recorded-take-20261020/` in the primary
checkout. Build script and checks reject changed source, repeated receipts
and an existing output binary.

The focused CTest selection is:

- `recorded-automation-tests`: shared clock/capture/take, limits, late input,
  identity failures, atomic publication and existing rendered/audit checks.
- `plugin-edit-notification`: native VST3 timestamps through take publication,
  bounded queue loss and existing PCM/audit checks.
- `document-controller-live-recorded-publication`: existing Windows native
  timeline publication plus complete take commit, busy refusal, exact saved
  baselines, one Undo/Redo, no-op and save/reopen.
- `workspace-restore-advanced-tests`: retained native owners and the new
  busy/pending/drag refusal checks for all seven graph selectors.
- `workspace-recipe-edit-tests`: adjacent retained recipe parameter editing.

The push triggered the same coordinated CI batch:

| Platform | Run | Native job | Latest observed state |
| --- | --- | --- | --- |
| Windows x64 | 39430710486 | 118654733984 | Building; started 13:28:43 UTC |
| Mac Apple Silicon | 39430710525 | 118654733396 | Building; started 13:28:49 UTC |
| Mac Intel | 39430710525 | 118654733679 | Building; started 13:28:45 UTC |

These are live handles, not passes. Inspect them for terminal results without
dispatching another build. The next build gate is at least one hour after the
latest build completion in this batch, including CI.

The prior candidate's complete twenty-return reciprocal fixture gate remains
valid for its exact binary/input lineage. It does not qualify the new take
owner or repaired graph handlers. Reuse only unchanged evidence; retain
current CI's native/API/conformance results separately.

## Local execution result

**Windows ARM64 build passed**, finishing **13:31:37.992 UTC** (203.38 s).
Executable SHA-256:
`586aade4af899b29f72d6ca6126db25cf3f30d1c23cfdb5d8e382511506f5001`.
The existing enum/floating arithmetic warning at
`PluginParametersWindow.hpp:233` remains; compilation had no errors.

**All six CTest registrations passed in 44.40 seconds**, comprising the five
selected groups and their required document-controller scratch setup:

| Group | Seconds | Result |
| --- | ---: | --- |
| Workspace advanced restore | 37.86 | Passed, including all seven busy graph selectors |
| Workspace recipe edits | 4.81 | Passed |
| Native VST3 notifications | 0.54 | Passed, including real callbacks through the take |
| Shared recorded automation | 0.27 | Passed, including identity/loss and clock-ring eviction regressions |
| Document-controller scratch setup | 0.09 | Passed |
| Live recorded publication | 0.77 | Passed, including take ownership, exact persistence and history |

The affected actual-app case
`test_graph_editor.GraphEditorTests.test_native_parameter_fields_and_assignment_to_real_bus`
also passed on the same binary in **2.348 seconds**, without skips or errors.
It used the existing PID-scoped pipe/native-message fixture on a private,
never-switched desktop. This is not foreground visual/DPI qualification.

`checks.json`, `native.log`, `native.xml`, `app-checks.json` and `app.log` retain
the exact inputs and results. The source checkout and executable hash remained
unchanged through both test invocations. No build/test retry occurred.

GitHub's PR merge source `3bb3996e4e38804b602e35b4a02560988e89e222` was fetched:
its complete Git tree and the local candidate's both equal
`8671ad0bc86561b7248056163c196b0cb4fd346c`. At the subsequent observation,
all three CI native jobs above were still building. They are not qualified by
these ARM64 results. No new build before **14:31:38 UTC**, extended to one hour
after any later build completion in this batch.
