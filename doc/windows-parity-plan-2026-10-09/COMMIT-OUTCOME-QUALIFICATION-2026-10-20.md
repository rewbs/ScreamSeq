# Commit-outcome candidate qualification

This is bounded Windows ARM64 evidence, not completion of the parity plan.
The source is frozen at `8c750f7d61966b3dc125ffaf49c34e8678697a25`.
[Draft PR #5](https://github.com/rewbs/ScreamSeq/pull/5) contains that candidate.

## Build and identity

One incremental, two-job build ran from **07:00:17 to 07:01:12 UTC on
20 October 2026**. It built `ScreamSeq`, `ApiTests`,
`document-controller-tests` and `workspace-restore-tests` successfully.
The qualification checkout was clean and matched the frozen source.

- Executable: `bin/windows-parity-p1/CommitOutcome-Release/ScreamSeq.exe`
  in the `parity-ci-layout/ScreamSeq` worktree.
- SHA-256: `9de1eb2ef9ce69065fd413df5a40cb2068eb83e2872acbb20b78f97bc26287c8`.
- Local evidence: `bin/commit-outcome-20261020/` in the primary worktree:
  `build.json`, `BuildInfo.json`, configure/build logs, `checks.json`,
  native JUnit/log, application log and complete conformance reports.
- Earlier executables and evidence were preserved. No next local build before
  **08:01:13 UTC**, retaining an hour after this build's completion.

The check batch ran from **07:01:22 to 07:04:03 UTC**. Native and application
exit codes were zero. Conformance exited one for the three API baseline
differences below; its overall result remains `passed: false`.

## Observed results

| Check | Result and scope |
| --- | --- |
| Eight selected CTest registrations | Passed in 83.63 seconds: workspace asset outcomes and command results; controller scratch/fixture prerequisites, publication, assets and recovery; complete `ApiTests`. |
| API real-pipe groups | Reached and passed after the fresh-request-ID test correction. This includes current-user DACL, framing/bounds, collision/stalled shutdown/restart, serialization outcomes, typed completion boundaries and exact terminal replay. |
| Linked-envelope actual-app regression | Passed in an owned private-desktop process. Dry and real rejected direct edits report `notCommitted`, retain exact saved state, linked bank, PCM and Redo; later valid history still works. The old B3 binary's failure remains retained separately. |
| F01–F05 | All five original-to-first and first-to-reopened saved property trees equal exactly. |
| Codec corpus | All 21 observations completed; safety passed and the existing Windows observation baseline matched. Protected-source refusal no longer aborts the run as an unknown write outcome. This does not mean every damaged/future input is accepted or that codec parity is complete. |
| File-only duration defaults | All three derivatives passed exact save/reopen assertions. |
| Edited opaque metadata | Passed paired clean/opaque title and graph-label edits, rack movement/removal, Undo/Redo and reopen: 18 saved stages. |
| Cross-domain composing history | Passed six stopped edit domains, dry runs, stale rejection, chronological Undo/Redo, Redo retention/forking and exact reopen: 21 saved checkpoints. |
| Input and binary identity | Reported unchanged for inputs, original corpus and executable. All owned app processes had exited after the batch. |

The API corpus retains **zero preservation failures**. Exactly three observations
differ from the older Windows baseline:

| Vector | Existing expected observation | Current observed result |
| --- | --- | --- |
| `pattern-index-real` | `-32602` | Success for an integral real value |
| `plugin-slot-real` | `-32602` | Success for an integral real value |
| `rejected-request-id-reuse` | Success | `-32600` for reuse with a different request |

These follow the shared integer and terminal replay changes. No observation
baseline or assertion was weakened. Review actual Mac results before recording a
new cross-platform baseline. The API inventory also still records unsupported or
platform-specific methods; these targeted vectors do not establish total API
coverage.

## Parallel integration gate

Creating the draft PR started one coordinated CI batch:

- [Windows x64 run 39383770334](https://github.com/rewbs/ScreamSeq/actions/runs/39383770334),
  job `118498660665`.
- [Mac run 39383770337](https://github.com/rewbs/ScreamSeq/actions/runs/39383770337),
  Apple Silicon job `118498660903`, Intel job `118498661208`.

At this report's checkpoint all three jobs were still building. They have no
passing claim here. CI may identify its generated PR merge commit rather than the
local head; inspect its retained manifests and source trees before combining
evidence or exchanging files. Do not rebuild merely to inspect a failed
observation baseline: the workflows retain candidate apps for subsequent checks.

The new edited exchange inputs are retained at
`conformance/edited-metadata/opaque-reopened.screamseq` and
`conformance/composing-history/reopened.screamseq` under this batch's evidence
directory. They have passed Windows checks only. Reciprocal F01–F05 and actual
edited-file legs remain required.

Foreground UI inspection remains stopped following the earlier human Escape.
Renewed permission was requested and has not been received at this checkpoint.
Private-desktop control checks are not live visual, physical keyboard, DPI,
accessibility or audible evidence. No physical audio/device/vendor qualification,
Mac runtime success or full parity completion is claimed.
