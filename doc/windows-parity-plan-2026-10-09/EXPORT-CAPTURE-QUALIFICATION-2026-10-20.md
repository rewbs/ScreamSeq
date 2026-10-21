# Export and capture-admission qualification

## Frozen candidate and scope

Candidate head: `54fa2babc96e63d0b10ebd8dedb8c438036f3b3f`.
The primary `codex/plugin-edit-parity` branch includes the temporary export and
capture-admission branches. Main remains `07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`.
Draft [PR #5](https://github.com/rewbs/ScreamSeq/pull/5) has been updated and pushed
after the **08:28:51 UTC** coordinated hourly gate.

This batch includes native module export/shared loss admission, terminal replay
and failure recovery checks, normal-build snapshot scheduling, the prior Mac
preview-initializer correction, native menu-fixture repairs, and shared
same-frame recorded-point preparation used by Mac capture. It does not implement
Windows gesture recording or original-event timestamp mapping. See the
[export source scope](EXPORT-FOLLOWUP-2026-10-20.md) and
[capture scope](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md).

## Current execution

Windows ARM64 build started at **08:29:14 UTC** in the existing isolated
qualification checkout, with two workers and new `ExportCapture-Release`
outputs. The prior `CommitOutcome-Release` and retained x64 app are preserved.
Eight selected targets share one build invocation:
ScreamSeq, ApiTests, document-controller-tests, workspace-restore-tests,
snapshot-tests, recorded-automation-tests, windows-save-tests and
native-metadata-tests.

Receipts/scripts/logs are retained under `bin/export-capture-20261020/` in the
primary checkout. `build.json` is authoritative for completion and executable
hash; null completion is not success. The scripts reject a second invocation
using the same receipt and reject a changed candidate or existing app output.

Live CI handles, verified after dispatch:

| Platform | Run | Job |
| --- | --- | --- |
| Mac Apple Silicon | [39393737331](https://github.com/rewbs/ScreamSeq/actions/runs/39393737331) | 118531234979 |
| Mac Intel | same run | 118531235347 |
| Windows x64 | [39393737358](https://github.com/rewbs/ScreamSeq/actions/runs/39393737358) | 118531235136 |

These three native jobs were building at the initial checkpoint. The optional
retained-app/interface-only jobs are skipped by workflow selection; they are not
runtime passes. CI uses PR merge commit
`d79321c7ccf59e7fdf0a4b2918d6906b41097ef4`, whereas the local build uses the exact
head above. Confirm retained artifact BuildInfo before attributing results.

## Selected gates and evidence limits

The prepared local checks cover the normal-build 48-case snapshot suite,
recorded-automation preparation/rendering under callback audit, Windows save and
native metadata, the real export worker, API dispatch, native command recovery,
the repaired departure/song-properties menus, and the actual named-pipe export
contract with native reopen. CTest scratch fixtures provide owned directories.
The configured schedule audit is separate from runtime execution.

Both CI workflows also run the broader native/model and fixture journeys.
Mac's fresh-process editing-contract runner includes module export; Windows's
actual-app suite includes the same contract. Reciprocal original/edited files,
API observation reconciliation and full parity acceptance remain open.

No foreground computer use or audible claim is made. Native-message fixtures
run on owned private desktops; visual/DPI/accessibility checks remain outstanding.
Do not rebuild immediately after a failure. Retain its full log, inspect source
and batch corrections for the next permitted window. Its earliest start must
account for the completed times of this coordinated batch, not the superseded
08:28:51 gate.

## ARM64 result

The one build invocation passed, **08:29:14–08:33:39 UTC**, including all eight
targets. App SHA-256:
`178840388b8a5a18770bd3925da3dc6f4da01f53d425391b02a567543b1d155a`.
No app process was running at the preceding check; the test processes used owned
private desktops and exited. The checks receipt confirms unchanged candidate
source and executable through completion.

Targeted CTest: **11/12 registrations passed**, including three scratch setups,
in **107.05 seconds**. The failed registration is `snapshot-tests`: **47/48
internal cases passed**. Full native API, both repaired menu groups, command
recovery including export's known/unknown completion paths, recorded-capture/
timeline callback audit, native metadata, native saves, and five-format export
worker including filesystem refusal/source aliases passed.

Actual-app named pipe: `test_module_export_contract_and_reopen` **passed** in
**1.189 seconds**. This includes dry/admission guards, exclusive and replacement
publication, successful/error request replay after external file changes, ID
conflicts, retained native save state, pending Redo and exact native Undo/reopen.
It does not exercise the rendered OS save picker.

Schedule audit: 204 registered, zero unscheduled, zero repeated; 18 separate
CMake projects remain inventoried. This is not execution of all 204.
`native-project-tests` still requires its separate configured external fixture
and was not registered or executed by this build. The bundled F04 snapshot case
did run and pass; it is not a substitute claim about that other target.

### Snapshot failure and prepared correction

`titlesAndLegacy` failed at `legacy[6] == '1'`. All of its preceding five-format
Unicode/native-save and legacy-container recovery/source-protection checks ran
before this assertion. Current `SampleArchive.cpp::packSongSnapshot` always
writes RSONGS2, including a zero timing length. The test still assumed the
historical writer's minimal RSONGS1 framing, verified directly against retained
commit `db496a399`.

The local source correction reconstructs that actual historical 16-byte header
and payloads independently, requires the strict current splitter to reject it,
then requires file-open recovery to produce byte-identical canonical RSONGS2
with source protection and no loss. It retains full musical-state comparison,
timing-override checks and idempotent canonical recovery. No format writer,
reader, assertion baseline or product behavior changes.

**The correction is unbuilt/unexecuted.** The original failure remains in
`native.log`/`native.xml`; no retry or additional build has run. The current
snapshot target remains failed. Rebuild that correction with the next cohesive
batch and rerun the affected target. Do not repeat the already valid export/UI
groups unless their relevant inputs change.

Both Mac jobs and x64 were still actively building at the subsequent status
check. The local-only next-build lower bound is **09:33:40 UTC**; later CI
completion may move the conservative coordinated boundary later.
