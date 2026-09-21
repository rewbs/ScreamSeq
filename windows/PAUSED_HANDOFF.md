# Paused by user — resume only on explicit request

> Resumed explicitly on 2026-09-21. See `RESUME_PROGRESS.md` for the newer
> continuation checkpoint, current test evidence and remaining work; the historical record below
> is preserved.
> Latest upstream integration: `PARITY_PLAN.md` and
> `UPSTREAM_PLUGIN_QUALIFICATION.md`. Prior source is preserved in a local Git
> checkpoint; the statements about uncommitted work below are historical.
> Current live plugin editing: `LIVE_PLUGIN_PARAMETERS.md`; native rack and
> visible UI evidence: `PLUGIN_RACK_PROGRESS.md`. Full parity is still active.
> Empty trigger creation and the installed Surge XT instrument checkpoint:
> `TRIGGER_INSTRUMENT_PROGRESS.md`, including the first-editor-open playback
> stop and the qualified deterministic instrument fixture.
> Native factory-program and audio-port controls, including corrected program
> dry-run semantics: `PLUGIN_PROGRAM_PORTS_PROGRESS.md`.
> Unified pattern FX, precise-note API, shared row tools and native FX inspector:
> `PATTERN_FX_PROGRESS.md`. Clipboard and precise-note UI parity remain active.

The user requested a natural stopping point and **stop**. No further development,
review-fix dispatches or integration should run automatically in response to late
process/delegation notifications. All work remains uncommitted.

## Location and preserved artifact

- Worktree: `C:/Users/P14/code/ScreamSeq-windows`, branch `codex/windows-native`.
- Do not reset, clean, stash, recreate or overwrite this tree. `windows/` and
  `editor/hosted/` contain untracked implementation work, not disposable files.
- Latest parent-tested editing executable:
  `bin/windows-editor-review-fix/Release/ScreamSeq.exe`.
- Expected executable SHA-256:
  `937fdb371571cbb01d34430d399031f7cc306c42e424bf887a9d108ca4dd21c5`.
- A separate pause copy is recorded in `bin/windows-editor-pause.json`, including
  its exact path and verified executable hash. It is a development artifact,
  **not approved for users' songs or full Mac parity**.
- The older `bin/windows-checkpoints/workspace-preview` and `native-bootstrap`
  checkpoints are preserved independently.
- The executable is not a claim that all current working-tree source was built.
  Several concurrent fixes/reviews completed after its build. Its original build
  evidence is `editor-review-fix-evidence/results.json` under Hermes scratch,
  copied with the pause artifact.

## Last verification completed before stopping

Parent reruns at the stopping point:

- **20 actual-app editor tests passed** against the executable above, including
  candidate-publication failure, postcommit recovery, legacy text/sequence names,
  native chooser focus, large-cache bounds/reuse, persistence and worker behavior.
- API replay-cache CTests: **2/2 Release and 2/2 Debug passed** in
  `bin/windows-api-cache-fix`.
- Earlier parent runs: 22 envelope/catalogue CTests; six asset/decoder CTests and
  24 portable core CTests; three snapshot/editing/probe CTests; seven hosted tests;
  hosted-project PCM/automation/lifetime tests; five initial VST3 CTests.
- The five initial VST3 tests did **not** establish approval: independent review
  found seven defects and subsequent fixes require their own final review.
- A disposable Mac-project copy was edited through native GUI note entry and Undo,
  checked via the exact-PID pipe, then redone/saved/reopened via API. The original
  reference hash remained unchanged. All parent-owned QA processes were closed.

The original supplied reference SHA-256 is
`96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7`.
No Mac executable reopen, commercial-plugin compatibility, sustained 60fps,
comprehensive allocation/free/lock audit, sanitizer coverage or new hosted WASAPI
qualification is claimed.

## App boundary and pending integration

The current editing app owns Document on a serial worker and publishes immutable
views. It supports native/module Open, atomic native Save/Save As, ordinary tracker
editing/history, guarded core/Timeline API calls and retained navigation/inspectors.
See `App/INTEGRATION.md` for the exact current interface and limits.

Still not integrated into the app:

- GraphOperations, AssetOperations and EnvelopeOperations dispatch/UI.
- Reviewed `HostedProjectPlayback` plus TrackerHosted into normal WASAPI playback.
  Hosted projects still reject rather than substitute dry playback.
- Windows VST3 scanner/provider/editor frontend and stopped-state/state-history
  orchestration; MIDI/recording/recovery workflow; full graph/curve/sample editing
  UI, docking/floating/accessibility and broader keyboard parity.
- Remaining Mac API families and final cross-platform/device/performance gates.

Before asset integration, fix the explicit waveform-cache assumption in
DocumentController: the currently registered operations do not alter PCM.
Asset edits/imports and their Undo/Redo need explicit waveform invalidation.
Supply real plugin assignment/capacity hooks for imports, actual rack/activity/
baseline hooks for graphs, and parameter/conflict hooks for envelopes.
Do not use empty fixture hooks to represent a nonempty project.

Replay cache now retains at most 64 entries and 8 MiB of serialized request plus
response content (not a heap/RSS limit). Oversized successful writes remain
successful without retention. Transport still accepts **1 MiB requests**, versus
Mac's 32 MiB; this is too small for some implemented PCM/clipboard maxima. Raise
and qualify transport bounds before claiming full operation parity. Write retries
remain disabled; postcommit/serialization failures require fresh state readback.

## Review/fix state — do not infer approval from a passing smoke suite

Verified review approvals already received:

- Shared hosted orchestration extraction (Mac compilation still pending).
- Project-to-host preparation bridge (offline only). Parent subsequently added
  exact endpoint, audible absolute-automation, cursor-start and lifetime checks.

Other important work needs completion-result reconciliation or re-review:

- App fixes from `deleg_1a2c8d9d` passed parent tests but have not received final
  independent re-review. They add atomic candidate view publication, UTF-8 text,
  native focus preservation and a 64 MiB per-view budget/reuse/worker retirement.
  Replay-cache fixes in that batch also need final re-review.
- `deleg_49788238`: legacy metadata promotion plus recovery-take provenance, and
  shared Windows/Mac precise-note no-op/history correction. Their later completion
  summaries have not yet been processed by the parent. Inspect actual source and
  evidence before deciding whether any original blocker remains.
- `deleg_3604fd46`: final re-review of exact six-field snapshot conservation and
  pattern-name charset export refusal. Parent tests passed; final verdict not yet
  processed here.
- `deleg_634e5e3f`: independent GraphOperations and EnvelopeOperations/catalogue
  reviews. Completion findings not yet processed by the parent.
- `deleg_6e8978f8`: independent asset/import and shared Vorbis/SFZ/MO3 reviews.
  Parent functional tests passed; completion findings not yet processed here.
- `deleg_b1a51987`: VST3 backend/UI and registry fixes. Registry worker had finished
  by pause but its completion was not processed. The only still-running worker,
  `sa-0-55d8d62c` (backend/UI fixes), was explicitly interrupted at user request.
  Preserve its partial changes; **do not assume it finished or passed**.

Delegation logs and late results remain under
`C:/Users/P14/AppData/Local/hermes/cache/delegation/live/<delegation-id>/` and in
session `20260920_200735_49766b`. Read those on an explicitly authorized resume;
do not poll or restart work while paused.

## Source and safety notes

- Shared changes now include snapshot/import/decoder fixes, explicit RNG namespace
  lookup, shared hosted extraction and precise-note helper/binding work. Mac files
  changed **inside the Windows worktree** need reconciliation with the other Mac
  agent; do not overwrite the original checkout or its platform tree.
- RSCORE1 is an optional final sample-archive correction, not an outer/metadata
  version bump. Older Mac binaries reject it when emitted. Native Mac rebuild and
  reopen remain required before claiming interchange with those changes.
- An early delegated OLE clipboard restoration attempt was unverified; the user
  was informed. Later inspection QA uses a process-private clipboard. Never read
  or replace the desktop clipboard for automated QA.
- Use explicit Hermes scratch paths and verify TMPDIR/TEMP/TMP before probes;
  tool/compiler environments can reset them. All test fixtures must be disposable.
- Latest process inventory found no ScreamSeq/scanner process and no running
  parent background task. Cancellation was requested for the remaining child;
  its partial completion is evidence to record, not authorization to resume.

## Resume priority

Only after the user requests continuation: reconcile outstanding results and
partial VST3 work, finish independent re-reviews, then integrate reviewed operation
layers/hosting with explicit invalidation and real hooks. Rebuild from a coherent
source snapshot, rerun tests and inspect a separate real UI process. Keep all
incomplete features visibly unsupported. Do not commit or push without permission.
