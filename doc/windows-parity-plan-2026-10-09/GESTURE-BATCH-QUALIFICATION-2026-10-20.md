# Recorded capture, clear and exact-history qualification

**Latest:** all three native CI jobs are terminal. See the
[CI review and retained follow-ups](GESTURE-CI-REVIEW-2026-10-20.md) for the
confirmed failures, prepared repairs and extended exchange. The live-job
statements below describe the earlier checkpoint. Next native build is no
earlier than **13:27:48 UTC** on 20 October.

## Frozen candidate and executed local checks

Candidate `db42b142196471f0a34fe67554c55dfc9de68439` built on Windows ARM64
from **11:48:11.923 through 11:52:09.027 UTC on 20 October**. Configure and
the single two-worker, fourteen-target build both exited zero. Its executable
SHA-256 is
`2b70b5462832d5bc0cd53b2fe2f233f68ff93833d278d8153a08c3a5c017573c`.
The independent qualification checkout remained at this candidate.

Receipts and logs are retained under `bin/gesture-batch-20261020/`:
`build.json`, `checks.json`, `native.xml`, `native.log`, `app-checks.json`
and `app.log`. The executable is in the qualification checkout's
`bin/windows-parity-p1/GestureBatch-Release/`. These ignored artifacts are
local evidence, not files shipped with the application.

**All 24 selected CTest registrations passed**, with no failures or skips,
in 34.10 seconds of reported test time. These comprise 21 executable checks
and three scratch setups; they are not 24 independent composing journeys.
The selected checks cover:

- Shared recorded automation, original-time capture, multi-target preparation,
  incarnation/loss guards, rendered PCM and the existing host callback audit.
- Native VST3 edit notification/source identity and audio-bus checks.
- Windows recorded-history publication, real queue refusal, atomic two-target
  adoption, exact typed untouched rows, no-op Redo, Undo/Redo and save/reopen.
- API, snapshot, native metadata, plist/preservation, save and hosted projects.
- Parameter preview/live values and the selected module-export, recovery,
  rack/recipe/graph/recorded publication and unified-history worker checks.

The snapshot assertion repair now executes successfully. Do not retain the
earlier snapshot failure as the current candidate's result.

Two actual-application checks also passed, with no failures, errors or skips:

1. `test_plugin_edit_parity.PluginEditParityTests.test_recorded_clear_contract_and_reopen`
2. `test_absolute_automation_ui.AbsoluteAutomationUITests.test_clear_all_recordings_preserves_stale_and_field_drafts_and_one_undo`

They took 5.693 seconds and used disposable documents, PID-scoped APIs/native
messages and never-switched private desktops. They did not use audio hardware
or foreground computer control. Receipts verify unchanged source and executable.
These checks establish clear-all behavior; they do not establish gesture
recording from a physical slider or vendor editor.

## Cross-platform gate in progress

One coordinated PR qualification batch uses this same source:

- [Windows x64 run 39418201506](https://github.com/rewbs/ScreamSeq/actions/runs/39418201506),
  native job `118612292760`.
- [macOS run 39418201597](https://github.com/rewbs/ScreamSeq/actions/runs/39418201597),
  Apple Silicon job `118612294240` and Intel job `118612293867`.

All three native jobs were confirmed in their build steps during the
12:00–12:10 UTC review. No success is claimed for them here. Inspect these
existing handles; do not restart them because an observation times out.
The earlier `28d757f65` CI jobs are terminal and are different evidence.

The subsequent poll confirmed Apple Silicon's build succeeded at **12:11:19 UTC**.
Its native model/host step failed at **12:13:44 UTC**; interface/conformance
steps continue independently. The job log was not yet available (GitHub
returned BlobNotFound), so the failing test/cause is not known at this checkpoint.
Windows x64 and Intel were still building. Preserve and inspect this job's
eventual logs; do not restart the build on a log retrieval failure.
Do not start another coordinated build before **13:11:20 UTC**, extended to
one hour after the latest native build completion in the current CI batch.
Reuse the unchanged executable for additional targeted runtime checks.

The [extended reciprocal checker](EXTENDED-EXCHANGE-2026-10-20.md) now includes
the newer opaque-metadata, mixed-history and envelope-bank final saves.
Its tooling checks pass; actual current-candidate exchange remains pending.

## What remains incomplete

`PluginOperations::commitRecordedGestures` is an internal completion boundary,
not an exposed recording feature. Windows still needs native arming, original
input intake before coalescing, release/Stop drain barriers, manual baseline
protection, retained failures/recovery and the public/UI workflow. Mac's existing
record controls still need the original-time consumer integration.

The Mac exact opaque/manual-cache Undo repair is included in this source but
awaits the current Mac run. Earlier successful cross-platform codec and fixture
results remain scoped to their recorded candidates. The new candidate still
needs reciprocal edited-file exchange, composing qualification and integration
onto shared main. Foreground ergonomics, accessibility and physical audio/MIDI
evidence remain separate open gates.

## Corrections to historical inventory readings

The original matrix remains the reference acceptance inventory. These observed
source mappings supersede blanket absence claims, not its unexecuted checks:

| Reference / acceptance | Current source and evidence | Remaining gate |
| --- | --- | --- |
| 01 / A16 / B03, workspace | `WorkspaceContextPanel.inc`, `WorkspaceState.hpp` and `WorkspaceLayouts.inc` provide contextual actions, Ctrl+B, reclaimed width and saved visibility. The relevant files and context tests are unchanged from the successful scoped recipe/sample checkpoint. | Foreground layout, keyboard feel, DPI and accessibility; no claim that private-desktop messages establish these. |
| 58 / A15 / B01, module export | `ModuleExportDialog.hpp` and the existing worker export path are in this candidate. The targeted worker export check passed. | Complete module export journey/compatibility checks. Do not confuse this with offline audio export. |
| B04, timing/follow | Shared main already contains `TimelineOperations.cpp` and `WorkspaceTransport.inc`; `pattern.timeline.get` and loop control are not absent features. The earlier retained x64 two-case loop/device evidence is separately recorded. | Full current composing/follow behavior and physical timing checks; older device evidence does not automatically qualify changed inputs. |
| 28 / A01 / A12, recorded lane | Point editing pre-existed. Both-host clear API and the Windows clear action now have this local API/native-control evidence. | Mac execution, current cross-platform persistence and gesture capture as a separate feature. |
| B05 / B13, capture/failure | Shared capture preparation and Windows atomic completion are implemented and tested as above. | User-facing recording, recovery and both native intake adapters are still missing. |
| B02 / B09, history/preservation | Shared typed preservation and native ownership exist; paired corpus execution is recorded in the preceding CI review. | Current Mac exact Undo repair and final reciprocal edited files. |

Do not close unreviewed rows or reimplement features solely because their
9 October planning cells say they were missing.
