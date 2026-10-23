# Live native parameter take release — 23 October 2026

## Qualified Windows checkpoint and scope

Frozen source `9c5c6ebb1e63ede1c1e1076235f011c5f1fa37f1` built once at
11:02:54 UTC. Seven native groups and all 13 actual-app composing/recording
cases passed without skips. Receipts are in
`bin/mac-gesture-20261022/processing-restoration/`. No test or build was repeated.
This checkpoint contains the shared processing acknowledgements and disposable
baseline comparison described in `PREVIEW-CANCELLATION-ADOPTION-2026-10-23.md`.
It does **not** contain the subsequent Mac product integration below.

Fresh Computer Use on isolated PID 34672 verified precise timeline double-click
insertion, Backspace removal, Undo, automatic velocity edits, local text deletion,
Tab/Shift+Tab, the moving white playhead in an explicitly selected row loop, and
native Save As with visible caret restoration. Read-only API receipts retain
continuous playback epoch 1 throughout note editing. A second epoch follows the
deliberate loop setup, then remains continuous through Record, Balance slider,
Stop recording, Keep and navigation to the two-point Balance automation lane.
The saved QA process exited normally. No musician process or audio default changed.

The 385.9-second silent WASAPI run at 48 kHz / 480-frame period reported zero
processor faults, device errors and deadline overruns, and **one starvation
indicator**. `realtimeAuditPassed:false` is retained in the receipt; this Release
foreground run does not establish a passing host allocation audit. Dedicated
native gates provide their own bounded audit evidence. There is no broad audio
performance or temporal-flicker claim. UIA reports Main for docked focus; visible
caret and keyboard effects supplement native focus assertions. Floating typing
continuity was not qualified. The first floating recording Close-button click
did not hide the window, while its title-bar Close did; helper reactivation means
this remains an observation to investigate, not a confirmed product defect.

Separate package:
`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe`

Executable SHA256:
`72f42e436329d9581f886ed1b7dbb70f604886a8b93dddd2577d679f7c88c801`.
Scanner, ARM64 runtime, retained notices and provenance accompany it; copied
hashes and both executable PE machine types were verified. No rebuild to package.

Current CI runs are Mac `39943351401` and Windows `39943353887`, with native
jobs `120379904341` (Intel), `120379904644` (ARM), `120379912183` (Windows x64).
They were still running at the 11:12 inspection. The latest actual build start
was 11:03:27 UTC; the next coordinated checkpoint must be **no earlier than
12:03:27 UTC**, and must first inspect later starts/receipts. Do not redispatch.

At 11:22 UTC ARM compilation had passed (completed 11:21:15); its tests and the
other two builds remained in progress. No current CI result is yet a full pass.

## Subsequent source batch — not yet compiled or qualified

- `NativeParameterTake.hpp::restoreProcessing` pins document/playback context
  across pending attempts, captures the bounded full owned baseline group, and
  composes processing receipts with non-flushing observation/private comparison.
  Native input cuts and leases remain mandatory before and after vendor calls.
- `ParameterTakeSession.inc::releaseParameterTake` selects live verification
  while audio is active, retaining the existing stopped path otherwise. It never
  stops transport, sleeps, releases an unverified owner or creates a second Undo.
- `ParameterRecordingAPI.inc` exposes `restorationPending`, and reports stopped
  transport requirements for unsupported owned formats. Only VST3 currently has
  the non-flushing observation capability. Empty ownership needs no transport
  stop. Existing strict admission, original timestamps and recovery stay intact.
- `PluginRecording.swift` resumes a definite successful pending Keep/Discard
  after guarded readback, up to eight attempts. It preserves action, document,
  take, post-Keep revision and loss acknowledgement. Errors, uncertain completion,
  changed context or non-boolean pending do not grant automatic replay. Polling
  occurs only while this retained view is visible; hiding it preserves the take
  and remaining intent, and reopening resumes readback. Manual recovery remains.
- API descriptions, public guide and all seven schema descriptions change
  together; no project format or persistent identifier changes.

## Mandatory next validation

1. Compile both Mac architectures and the Windows candidate once at the next
   coordinated hourly checkpoint. Reuse current Windows UI evidence only if its
   compiled inputs are unchanged; new native Mac lifecycle behavior needs its own
   evidence. Run the changed PluginRecording interface group and recording socket
   admission case, plus existing native plugin/preview/recording groups.
2. `PluginRecordingInterfaceTests.swift` covers same-intent retries, strict pending
   booleans, unknown/error readback, changed take/revision/loss and the eight-retry
   bound. Preserve existing compact-window, focus and native recovery assertions.
3. Run the real native session fixture with `native-plugin-tests <fixture> --ui
   --loopback-record` on an explicitly selected BlackHole 2ch device. The changed
   `ParameterTakeSessionTests.inc` requires actual continuing frames/callbacks,
   live Keep/release, one-step live Undo/Redo, save/reopen, live Discard and exact
   unrelated music/plugin preservation. Its stopped lifecycle checks remain.
   **Existing CI invokes `--ui` without `--loopback-record`; its lifecycle pass
   does not execute this device/session journey.** Do not silently skip or replace
   this requirement with artificial audio progress.
4. Native foreground Mac recording and Windows full composing journeys, reciprocal
   edited-project save/reopen, provider failures and native audio remain integration
   gates. No merge or complete-parity claim follows from partial local evidence.

## Remaining limits

AU lacks a qualified non-flushing observation contract and retains stopped
restoration. VST3 plugins with divergent hidden/discrete state or incoherent state
round trips also refuse live release. Explicit opaque Discard still requires
stopped playback and closed owned editors. A kept take with hidden-state divergence
needs a separately designed saved-plugin-state recovery action which preserves
the already-kept automation and history; do not relabel that action Discard or
relax native ownership/input cuts. Real vendor callback behavior is not proven by
the fixture. These are remaining parity work, not completed features.

## Kept-state recovery primitive prepared between checkpoints

After `f4d0fb7a4`, `NativeParameterTake::restoreSavedStateStopped` separates
explicit recovery of a completed take from `abandonStopped`. Discard now rejects
completed takes even for a direct native caller. Both operations share one private
stopped state-repair implementation and its retained, whole-group adoption receipt.
The new completed-take entry preserves the take's completion, original event
evidence and Stop cut. It does not unretire captured input, publish automation,
change chronological history, or implicitly release the session's take owner.
It requires stopped audio and closed owned editors and explicitly abandons later
native input only as part of successful whole-group replacement.

`StoppedPluginRepairTests.inc` now exercises both unkept Discard and completed
Keep against real mixed AU/VST3 hidden-state divergence. It checks refusal of the
wrong lifecycle action, a malformed later baseline leaving all provider states
and input evidence intact, exact unrelated provider preservation, one completed
musical publication, no second Keep publication, retry without vendor replacement,
and reopened fixture PCM with the host realtime audit. The visible native fixture
also requires open-editor refusal before recovery. The setup models the actual
recovery entry after ordinary preview cancellation has succeeded but opaque
verification has refused; it does not claim that failed opaque preparation rolls
back an earlier accepted cancellation.

These additions are **source only**. API/native UI exposure remains the next step:
an explicit Restore saved plugin state action for completed takes, guarded by the
current document/revision/take, with wording that later unsaved plugin input is
abandoned while kept automation remains. Normal Finish restoration must never
silently choose this action. Keep existing recovery-copy access, stopped transport
and closed-editor requirements. Windows already repairs closed isolated editor
copies on ordinary completion, but post-Stop input still requires deliberate
abandonment; any public recovery option must have consistent semantics on both
platforms and strict type validation, rather than being accepted silently on one.
Add native/API/full-session history and reopen checks before calling it delivered.

At 11:29 UTC the exact `9c5c6ebb1` Mac ARM job `120379904644` was terminal
success. Intel and Windows jobs remained live; no job was restarted. This pass
qualifies the frozen prerequisite candidate only, not `f4d0fb7a4` or this recovery
primitive. The next build gate remains 12:03:27 UTC pending fresh job/start checks.

## Explicit completed-take recovery — source prepared, not yet qualified

The next cohesive batch exposes the primitive through both native UIs and
`automation.recording.commit` with optional strict boolean `restoreSavedState`.
Default/false preserves normal completion. True requires an already-completed
retained take, stopped playback/audio and every owned editor closed. It abandons
later unsaved native input and restores accepted plugin state without changing
kept automation, song revision or its existing Undo entry. No project format or
stable identity changes. An unkept take must still use ordinary Keep or Discard.

Concrete ownership and interfaces:

- Mac `ParameterTakeSession.inc` calls the retained owner's stopped whole-group
  repair, releasing it only after success. `ParameterRecordingAPI.inc` validates
  the explicit intent before commit/restoration. Windows
  `ParameterRecordingOperations.inc` guards completed/stopped context, and
  `RecordedPluginEditors.inc` permits abandoning later input only for explicit
  recovery with all owned editors closed, rechecking after vendor/lease calls.
- Mac `PluginRecording.swift` and Windows `ParameterRecordingWindow.hpp` replace
  completed-take Discard with **Restore saved state**, explain the abandonment,
  retain recovery-copy access and guard queued actions against changed context.
  Ordinary Finish restoration never acquires this flag. Explicit recovery does
  not automatically retry pending or uncertain responses. Windows now labels
  ordinary cleanup **Finish restoring** and does not request a second musical
  loss acknowledgement for an already-kept take.
- The native delivery adapters allow only literal boolean true to send this
  deliberate recovery past retained refused-input review. Pending delivery,
  changed loss/ownership and uncertain completion still protect original input;
  the session remains authoritative for completed/stopped/closed validation.
- Both API schemas, discovery responses and guides describe the same contract.

New assertions (written, not run): Mac retained UI checks cover action visibility,
compact containment/focus, exact revision/token/flag, playback restart during a
read, uncertain/pending no-replay and ordinary retry isolation. Native input
adapter checks cover false/numeric/string versus literal true without erasing
evidence. The real-device `ParameterTakeSessionTests.inc` adds malformed/unkept
and active-completed refusal, then explicit stopped API recovery followed by
existing exact music/plugin, one Undo/Redo and save/reopen checks.

Windows `ParameterRecordingControllerTests.inc` adds malformed/unkept refusal,
post-Stop vendor input, Keep while playback continues with a retained cleanup
error, stopped/open-editor refusal, ordinary retry refusing to abandon later
input even after Close, then explicit recovery preserving accepted automation,
opaque baseline and revision. Existing one-step Undo/Redo and native reopen
remain mandatory. `ParameterControlDeliveryTests.inc` checks strict intent and
preserved evidence. `ParameterRecordingWindowTests.inc` covers native controls,
queued-playback rejection, contained actions and uncertain completion/readback.
Actual-app `test_plugin_recording.py` adds schema discovery and strict recovery
refusals to the complete composing journey before ordinary Keep. Mac's real
socket admission case checks discovery too; it does not replace the device test.

Cheap checks so far: JSON parsing of both schemas, Python AST parsing of both
changed socket/app scripts, and whitespace review. These establish syntax only.
No compilation, native test, new executable, foreground qualification or main
merge is claimed for this source batch.

Next checkpoint must include `document-controller-tests` as a build target and
the isolated `test_recovery.RecoveryControllerTests.test_manual_vendor_editor_overlay_and_late_protection_guard`
case: that is where the changed real-vendor controller and ownership fixtures
execute; the ordinary CTest recovery registration is not equivalent. Retain the
seven previous native groups and 13 app cases, adding this one app-runner case.
Both Mac architecture builds, changed interface/native groups and the actual
BlackHole session lifecycle remain required. After passing automated checks,
inspect recording controls and recovery action layout/focus in a fresh isolated
QA process. No previous process ID or screenshot qualifies the new controls.

At 11:37 UTC, exact candidate `9c5c6ebb1` had compiled on Windows x64 and Mac Intel;
their jobs were still running tests. Mac ARM remained terminal success. The
fresh CI receipt confirmed no later build start than 11:03:27 UTC. Do not build
before 12:03:27 UTC, and recheck live jobs/other checkpoints before dispatch.

## Kept-take navigation and terminal prerequisite CI

`a33b7dca98948e2373e1541cc8fc04134ff085ff` contains explicit recovery and is
backed up on `codex/mac-gesture-next-checkpoint`. Subsequent source closes a
confirmed composing-journey gap in Mac `PluginRecordingView`: Windows already
links a kept take to Song automation; the Mac window had no equivalent callback
or control. The new **Song automation…** action reuses
`openParameterSource(kind:recorded)` / `ParameterActivity.inspectRecorded`, which
opens existing editable song-time points independently of a live processor.

The action captures document, take, stable plugin and parameter when Keep is
chosen, preserves that target across asynchronous reads and live restoration
retries, and becomes usable only after a definite matching completed/released
receipt. Idle polling retains the target. New takes, changed documents, removed
plugins and unknown Keep outcomes cannot redirect or fabricate navigation.
Interface assertions cover a second selected target, queued selection changes,
pending release, no-write navigation, removal/document guards and compact layout
with both original-input paging and the new action. The existing native recorded
inspector already covers unloaded stable IDs including UInt32-max parameters;
this change adds only native presentation/navigation, no new editing/API state.
This source is unbuilt and its new assertions are not yet run.

Windows `ParameterRecordingWindow` also now captures the navigation target in
its queued Keep intent, retains it after a successful Keep whose native cleanup
is pending, and carries it through Finish/explicit recovery. Previously a later
selection could substitute another parameter at cleanup. The native review
fixture requires a second selected parameter to survive pending restoration,
selection of the first parameter and explicit recovery without a music write
from navigation. Existing uncertain receipt and target/document checks remain.

At 11:47 UTC the exact prerequisite `9c5c6ebb1` jobs were terminal: Mac ARM and
Intel success; Windows x64 compilation success and overall failure. The saved
job log `bin/mac-gesture-20261022/processing-restoration/windows-ci-job-120379912183.log`
shows 158 portable/worker and 50 native passes, successful vendor-ownership and
conformance steps, but 112 integration cases with 22 failure records and one
skip. Its actual `audio.devices.get` probe returned `devices:[]`; transport
checks explicitly report unavailable WASAPI and other live journeys fail app
readiness. These remain failures, not substituted passes or skipped requirements.
Both native architectures still need their required real-device evidence.

Prepared scripts under `bin/mac-gesture-20261022/saved-state-recovery/` must pin
the final navigation/recovery commit before dispatch. They require fresh prior
CI/job evidence, no overlapping checkpoints, and the 12:03:27 UTC earliest gate.
They have been parsed only; no build or tests have run for this new source.

The cheap API inventory preflight then failed its reviewed schema hash before
any compilation: the seven live-release descriptions and new optional strict
`restoreSavedState` property changed the LF-normalized schema. After reviewing
that exact diff and both native adapters, `doc/api/platform-differences.json`
now records hash `49068ec822bfdd86e72404df75f08d47f5d707cd9becffdf4aedd99c3966adb6`
and an explicit source-only review. Method inventories, vector expectations and
the open live-release qualification status remain unchanged. The original failed
receipt is `saved-state-recovery/api-preflight.log`; retain it beside the corrected
preflight result. Updating the reviewed schema is not a runtime parity pass.
