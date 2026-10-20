# Rack parameter preview batch — source checkpoint

Status: **implementation in progress; native compilation and execution pending**.
This follows the built `1d6c2a21106411c3885169cfd5689dd369e1ceab`
plugin-edit candidate and the uncompiled recovery correction `69110f4ed`.
Those executables do not contain this preview implementation.

## User-visible behavior and boundary

The retained Windows parameter inspector already presents up to ten editable
parameters per page. This batch makes a rack slider audition during tracking,
then save its final value on release with one Undo step. Escape, Reload and Close
restore the accepted value. The inspector remains attached to its stable plugin
while the musician selects other mixing-graph nodes. Typed values, switches and
keyboard steps continue through the existing ordinary edit path.

The shared audio primitive already exists in `editor/hosted/PluginChain.cpp`:
`previewParameters`, `hasParameterPreview`, `cancelParameterPreviews`. It separates
effective audible values from accepted manual values and opaque state. This batch
adds session ownership and UI integration, without changing the audio callback.
Recipe controls use prepared copies with different publication semantics; their
live preview remains open. They retain automatic saving on completed gestures.

## Source ownership

| File | Change / responsibility |
| --- | --- |
| `editor/ParameterPreviewOwner.hpp` | One transient token + stable plugin/parameter + document revision + host generation. Prepare before audio publication; refusal retains the previous owner; cancellation clears only after restoration succeeds. |
| `editor/hosted/ParameterEdits.hpp` | Existing shared catalog validation; both adapters use it before publication. No new value-unit or persistence format. |
| `windows/Session/ParameterPreviewOperations.inc` | Worker-owned preview API, UI-producer audio publication, stale host/document checks, normal durable parameter commit and view publication. Quiescent cancellation drains queued controls before restoring. |
| `windows/App/PluginParametersWindow.hpp` | Trackbar tracking/release/Escape and retained ownership recovery. No full inspector relayout while sending a tracking update. |
| `mac/Bridge/ParameterPreviewAPI.inc` | Equivalent API ownership around native Core Audio publication and ordinary chronological plugin history. |
| `mac/Audio/AudioDevice.hpp`, `.mm` | Native chain generation and publication/cancellation accessors. |
| `mac/Bridge/TrackerSession.mm` | Native save/history/parameter/rack paths cancel previews; snapshot/telemetry refresh retires stale ownership without interrupting a current gesture. Failed restoration retains ownership for retry. |
| `mac/Tools/resonance-api.schema.json`, `mac/AUTOMATION.md` | Four explicit `plugin.parameters.preview.*` operations and cancellation/commit contract. |

`doc/api/platform-differences.json` records **expected**, source-reviewed inventory
additions on both platforms. Its older binary receipts do not qualify them. Existing
unresolved API/codec differences and assertions remain unchanged.

## Required acceptance and bounded validation

1. Shared `parameter-preview` test: existing exact PCM checks at 44.1/48/96 kHz,
   blocks 17/128/511; transient/manual/opaque-state separation; restoration to a
   newer accepted value; saturation; native callback audit. Added owner checks
   cover refused/throwing publication, wrong token/revision/host, and cancellation
   refusal without losing ownership. Windows audit covers C++ allocation/free;
   Mac covers allocation/free/lock. Neither claims vendor-private instrumentation.
2. `document-controller-live-parameters`: existing parameter edit regression plus
   `LiveParameterPreviewTests.inc`. Real worker/UI producer/prepared renderer:
   inactive and active preview, invalid inputs and competing owners, one-step
   Undo/Redo, no-op release, full queue set/cancel/commit refusal, intervening edit,
   host generation retirement, stopped cancellation, and native save/reopen.
3. `workspace-command-result-tests`: actual retained inspector handlers establish
   preview ownership while tracking, clear it on release/Escape, preserve the
   captured plugin across graph selection, and retain existing keyboard, paging,
   stale draft and completion-loss checks. The prior `69110f4ed` graph-update
   rejection/Reload correction is included in this same build and native group.
   Added release-completion failure preserves newer text, retires the committed
   preview, and reviews without another commit or Undo entry.
   A refused/uncertain tracking request blocks release until Reload instead of
   falling back to a durable edit. Competing-editor and dropped ownership-read
   cases retain the other owner; Reload reconciles before cancelling. Escape and
   Close preserve newer text and the retained result after completion loss.
   Pending worker requests may pump native input: release/capture-loss timers
   remain deferred until the request finishes, the final trackbar position is
   read before committing, and a deferred Escape takes precedence over release.
   Native cases pump those three event sequences during the preview request and
   require one commit/Undo or an unchanged revision on cancellation.
4. `test_plugin_edit_parity`: new shared `editor/Tests/parameter_preview_contract.py`
   runs over a disposable Windows PID pipe and checks silent ownership, type/key
   validation, history, stale release and exact accepted save trees. Windows adds
   actual reopen. `mac/Tests/test_automation.py` invokes the same contract over its
   owned socket. Mac edited-fixture reopen remains part of the reciprocal gate.
   Both transports must replay exact retained request IDs without reactivating a
   cancelled preview or duplicating the final commit.
   Mac `parameter-activity` also exercises native/API gesture interleaving,
   stale native document edits, recovery without committing/cancelling the drag,
   and explicit native save/reopen with the stable plugin identity retained.
5. Rerun the previously failed actual-PID invalid-cycle graph rejection/Reload
   case. Reuse unaffected old evidence only with its exact binary/input identity;
   do not label this new executable qualified from earlier passes.
6. With the candidate built, capture `api.describe` and run F01–F05 typed
   open/save/reopen plus existing API/codec vectors once. Updated inventory must
   match source expectation; preservation assertions are unchanged. Mac and x64
   builds and reciprocal edited fixture saves remain integration gates.

Compile the app, `workspace-restore-tests`, `document-controller-tests`,
`parameter-preview-tests` and `rack-move-tests` together with two workers, reusing
the qualification cache and a new runtime output directory. No configure or build
before **2026-10-20 02:45 UTC**: the preceding build ended at 01:44:52 UTC. A build
failure does not waive the hourly limit. Start broader suites only for failures,
changed shared behavior, or the final integration gate.

## Remaining qualification / risks

- Source checks on 20 October UTC: all three changed Python scripts parse;
  all four schema field inventories match their operation contracts; both
  dispatch inventories contain the four additions; `git diff --check` passes.
  PowerShell `Test-Json` also validates all four successful request shapes against
  the complete bundled schema and rejects unknown keys, boolean parameter IDs and
  empty gestures. Receipt: `bin/parameter-preview-parity-20261019/schema-checks.json`.
  These checks neither execute the new fixtures nor establish native behavior.
- No native execution or audible result is claimed for this source checkpoint.
  Strict syntax/schema checks establish only fixture/schema consistency.
- Review completion failures after durable commit separately from precommit
  refusal. A returned or committed operation must not be automatically replayed.
- Verify native vendor editor edits, transport restart and unavailable providers
  against the retained gesture. Opaque state and stable identity are preserved;
  no native metadata or project-container migration is introduced.
- The candidate's main workspace was viewed with computer use, but the user
  stopped that session with Escape before the first requested interaction
  completed. Interactive graph/parameter visual verification remains outstanding;
  native handler tests do not replace it. Computer use remains stopped.
- Main integration awaits native qualification. Keep both adapters on the same
  temporary feature branch; do not split platform development branches.
