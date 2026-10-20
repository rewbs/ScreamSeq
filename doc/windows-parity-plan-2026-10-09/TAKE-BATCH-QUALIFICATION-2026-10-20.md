# Recorded take and graph-retention batch

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
