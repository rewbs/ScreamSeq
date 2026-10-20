# Main integration — 19 October 2026

## Scope and provenance

The user requested inspection of local branches/worktrees and integration of every outstanding change into main. Remote main was refreshed at `16cab100b`; the local main checkout was at `d36a6129f` with uncommitted menu/keyboard edits. All 12 registered worktrees and all 14 original local branches were inspected. No product session or audio defaults were changed. Generated binaries, caches and ignored private evidence remain outside Git.

## Inputs and disposition

| Input | Disposition |
| --- | --- |
| `codex/windows-parity-integration` / `codex/windows-parity-workspace`, `b95c55d57` | Integrated the complete 166-commit continuation of remote main, including mixer, tracks, native controls, direct graph edits, instrument/plugin layering and automatic inspector edits. |
| `codex/windows-parity-safety`, `9051ef586` | Integrated nine commits absent from the integration branch. Kept Mac graph/provenance corrections, API/codec observations and qualification reports. Combined the plugin no-op fix with the newer shared validation and atomic queue-refusal implementation. |
| `codex/windows-parity-mixer`, `codex/windows-parity-tracks`, `codex/windows-parity-p0` | Already ancestors of the integration branch. |
| `codex/screamseq`, `codex/windows-main-parity-merge`, original `codex/windows-native` | Already integrated through main/integration history. |
| `codex/independent-dock-regions`, `codex/graph-curve-host`, `codex/precise-note-host`, `codex/precise-note-parity` | Every unique commit was patch-equivalent to a commit already integrated (`git cherry` returned only minus entries). Recorded ancestry without replaying obsolete file versions. |
| Detached arrangement worktree, `ca9f6c475` | Matrix continuation commits were patch-equivalent; the earlier annotation/report commits were applied in a different order. Checked their source differences: annotation changes and native report handling are both retained. Recorded ancestry without undoing newer arrangement edits. |
| Stash `matrix core draft before annotation integration 20261007` | Preserved unchanged. Compared its tracked and untracked test contents with the later matrix checkpoint: the checkpoint contains the draft plus physical-span no-op protection and corrected/expanded tests. No outstanding feature was found in this earlier draft. |
| Uncommitted main worktree changes | First preserved as `4ddc97a73` on `codex/local-main-recovery-20261019`, then reconciled with the newer implementation. The source commit remains in main ancestry. |
| Untracked `CROSS_PLATFORM_UI_ASSESSMENT.md` | Preserved as `f2b0a00ea` and merged. This historical assessment is a proposal, not approval to introduce Qt/JUCE or replace native UI/audio. |
| Untracked `NEXT_SESSION_PROMPT.md` | Preserved as `doc/HISTORICAL_WINDOWS_BOOTSTRAP_HANDOFF.md` with an explicit historical banner. Its old paths, status and instructions are not current guidance. |

## Conflict decisions

- Plugin parameter writes still validate the entire batch through shared `prepareParameterEdits`. Only after validation are unchanged manual values removed. An empty effective edit preserves original opaque plugin bytes, including an empty imported state, without consuming history or touching playback. Full live queues still reject atomically; the older stop-and-commit fallback was not restored. Tests retain both cold-state no-op/Redo checks and queue-refusal assertions.
- Integration testing exposed a cold-state Undo admission defect: empty saved default plugin bytes compared unequal to the restored canonical default state. Admission now compares canonical defaults when the target bytes are empty, while history preserves the original empty bytes. This keeps live queue refusal atomic without stopping playback. The targeted controller and native application fixtures pass.
- The earlier standalone menu implementation, direct New-song replacement and old command IDs were superseded by the current retained menu catalogue, document-departure lifecycle, shared constructors and schemas. They were not layered on top as a competing menu path.
- Distinct local improvements were retained: selector open/focus/cancel notifications do not masquerade as edits; Select row/channel/pattern preserves the independent cursor; keyboard help and About are discoverable under Tools → Help; Alt+arrows select input instrument/octave, keypad divide/multiply adjusts octave, and Tab preserves the field across channels. Return to opening position defaults to Ctrl+Alt+Backspace to avoid the Alt+Left conflict. Saved configurable bindings remain subject to the existing conflict checks.
- Current empty instrument slot navigation and the full 0..9 octave range remain intact. The old draft's occupied-slot-only navigation and 0..8 clamp were superseded. Current Backspace/clear semantics, precise effects, retained drafts and queued paste behavior remain intact. Decimal entry in nudge fields is preserved.
- New command IDs occupy 630..634, outside native effect controls starting at 700. The surviving selector and selection tests are ported onto current APIs; superseded draft tests remain recoverable from their source commit while current menu/New/history/clear tests continue to cover those behaviors.

## Validation

The Windows application and registered test targets built successfully. One full build was followed by targeted rebuilds for integration corrections and changed fixtures; unchanged targets and passing evidence were reused. The final application was built from product source commit `e8e97cdb0e76a82de43c3b2839906c44d55670ff`; later changes affect tests, scheduling and this audit. Its SHA-256 is `f585a5d565f8c80114739621425711708ee96e4c0da4b755444d8a1089ef3d95`.

The initial full CTest run passed 185 of 192 registrations. Seven failures were investigated and resolved: the cold-state plugin Undo admission defect described below; stale automatic-save/preview, numeric formatting, plugin-plus-sample routing, annotation accounting and advertised API expectations; and an oversized workspace test process. Assertions for atomic rejection, history, raw drafts and persistence were retained. The workspace cases were split into three registrations with the existing 90-second child and 120-second CTest limits unchanged; their final runs took approximately 48, 35 and 5 seconds.

All **194 current CTest registrations have passing evidence**, combining the full run with targeted reruns against relevant final inputs. This is not a claim of a fresh single all-pass 194-test run. Python checks recorded **57 passes and two explicit skips**, including native keyboard/menu checks and a real named-pipe application fixture for cold plugin no-op, Undo/Redo and saved-state behavior. The API client fixture used the current built test host instead of its obsolete default Debug path.

The scheduling audit found two existing API tests omitted from the CI label filter. The filter now includes the API label. The final audit reports **194 registered tests, zero unscheduled and zero repeated**, across 18 separate CMake projects. Recovered native menu/selection regressions are included in the existing Windows CI batch.

The [machine-readable evidence record](MAIN_INTEGRATION_2026-10-19.json) records per-test results, source revisions, receipt hashes, explicit skips and the executable hash. Detailed local logs remain in the qualification checkout under `bin/main-integration-20261019/`. The usable application is `bin/windows-parity-p1/MainIntegration-Release/ScreamSeq.exe` in `C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq`.

This integration does not claim full Windows/Mac parity, foreground accessibility, physical-device qualification or a fresh Mac build. Earlier reports retain their original evidence limits. The normal main push invokes the repository's cross-platform CI; its results must be evaluated separately.
