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

## 12:03 checkpoint: recovery and composing evidence

Frozen `99d72db3278b8929cb92bf813e988dfbd5c7a5dd` built once locally from
12:03:59 to 12:07:10 UTC. The candidate executable SHA256 is
`a041c70cf34e30dd33d3b5dd7b28363090b4af99192ae8eb8f633ac4929a3f63`.
Receipts are in `bin/mac-gesture-20261022/saved-state-recovery/`; never repeat
its recorded build or full check invocation.

All seven selected native groups passed. The original actual-app run passed
13 of 14 cases, including the changed native vendor recovery/controller case.
The composing case observed `needsReview` during a pending parameter-name/page
refresh, before its original-input details were presented. Its wait now requires
`needsReview && !pending`; exact timestamp, reason and music-preservation
assertions remain unchanged. Only that failed case was rerun against the same
executable and passed in 24.299 seconds. `checks-corrected.json` records that
bounded aggregate, hashes the correction/log, and preserves the original failed
`checks.json`. This is a harness readiness correction, not a second full suite
or a replacement pass for different product code.

Fresh Computer Use on isolated PID 41920 exercised the 15-plugin composed F04
fixture. Params opened Recording; an occupied Gain envelope visibly refused
capture and retained the original timestamp/reason. Stop, Discard, new Record,
Balance slider, Stop and Keep succeeded during continuous playback epoch 1.
Song automation opened the kept Balance lane without writing music. Docked
search typing, Backspace, Tab and Shift+Tab had the expected visible effects.
Ctrl+S saved the private project; typed project comparison found zero unrelated
differences. The owned process exited normally. No musician session or audio
default changed. The exact read-only observations, saved project and audio
report are retained under `foreground/`.

This candidate is **held from production packaging**. Its 387.2-second silent
WASAPI run at 48 kHz / 480 frames recorded 37 deadline overruns, one starvation
indicator, maximum callback 13,102.2 microseconds, zero device errors and zero
processor faults. Overruns occurred before and after Keep; attribution to one
operation is unsupported. The preceding 9c5c6ebb1 foreground run used a simpler
song, so its zero-overrun result is not a controlled regression comparison.
`realtimeAuditPassed:false` and `sustainedPresentationQualified:false` remain
explicit. A same-fixture comparison/profile is still required before a clean
performance claim. The earlier separately packaged precise-note build remains
available; it has not been overwritten.

The explicit completed-take Restore saved state action still needs foreground
exercise with an interactive vendor editor. Existing native controller/control
checks passed, but the old Windows fixture only displayed a static label. The
attempted floating-window resize did not change bounds, so no compact foreground
pass is claimed. Computer Use exposes Main as the target for related floaters;
reactivation on typing is not proof of a product focus defect. Native focus
assertions remain necessary, and screenshots do not qualify temporal flicker.

At 12:35 UTC, current Mac ARM job 120403715903 was terminal success; Intel
120403715775 was still building. Windows x64 120403715803 compiled successfully
at 12:31:23 and was running tests. Runs are 39950565741 and 39950565689. Latest
actual build start was 12:04:52 UTC: **no next build before 13:04:52 UTC**, and
refresh CI/other-checkpoint receipts before dispatch. Mac real-device lifecycle,
reciprocal edited-project preservation and native Windows audio remain required
before main integration or full parity claims.

## Next coherent source batch: recorded-lane viewport and vendor UI fixture

Source below is unbuilt; new native assertions have not yet run.

- `windows/App/AbsoluteAutomationWindow.hpp`: Fit now frames the selected
  lane's actual first/last points with padding and a one-second minimum, rather
  than compressing a short late take into the entire song. Empty lanes retain
  0..8 seconds and legal endpoints remain bounded. Fit, pan, zoom, wheel,
  selected-point reveal and same-target reload synchronize the range fields.
  The old Apply guidance now describes automatic saving with Undo.
- `windows/Tests/Workspace/AutomaticEditsApplicationTests.inc`: late two-point
  take, readable handle separation, exact range display, zoom/pan/Fit,
  explicit range/reload retention, empty/full endpoint ranges, and unchanged
  music/revision during viewport navigation. Registration:
  `workspace-command-result-tests` (group `command-results`).
- `windows/Tests/Plugins/FixtureVST3.cpp`: a real plugin-owned Win32 trackbar
  sends native mouse/keyboard edits through IComponentHandler with its retained
  controller. Existing attach notification, stable parameter 7 and lifetime
  checks remain. This adds no production-provider or DSP behavior.
- `windows/Tests/Plugins/EditorTests.cpp`: real HWND slider/arrow-key input,
  original timestamps, no end-track duplicate, actual PCM changes and three
  close/reopen cycles. Both fixture CMake entry points link common controls;
  root CMake registers `vst3-editor` / target `vst3-editor-tests` against the
  current application libraries, avoiding stale imported engine libraries.

Next checkpoint must build ScreamSeq, scanner, fixture, workspace-restore-tests,
vst3-editor-tests, vst3-native-tests, plugin-edit-notification-tests and
document-controller-tests together. Select the new `vst3-editor` registration,
`vst3-native`, `plugin-edit-notification`, workspace command-results,
source-navigation and parameter-preview groups. Rerun the changed complete
composing case and actual vendor-recovery case with the newly built fixture.
Unchanged precise-note/shared-renderer cases may reuse their exact prior inputs
and receipts; a new fixture hash cannot reuse old vendor evidence. Inspect Fit,
range fields, target retention and explicit recovery through Computer Use on a
fresh owned process. Keep all refusal, stopped/closed-editor and original-input
guards; do not manufacture a successful recovery by removing them. Broaden only
for a failure or changed dependency. No main merge or new package follows from
source-only checks.

## 18:52 checkpoint: recorded viewport and real vendor recovery

Frozen `4d1fa80fe952e0df57d158cca654f920a99cc797` built once from
18:52:14.741 UTC to 18:54:58.169 UTC. Executable SHA256:
`e2ea6f2b635c9f0daf22435329c51880e18415297ee0e90c80f69bd0c24f950c`.
Evidence: `bin/mac-gesture-20261022/recorded-viewport/`. All six selected
native registrations passed in 88.26 seconds, including the rebuilt real VST3
editor fixture. Both actual-app composing/recovery cases passed in 26.675
seconds. The qualification checkout remains clean at that source. Do not rerun
its recorded build or checks against newer sources.

The preceding 99d72db32 CI is terminal: Mac ARM and Intel passed. Windows
compiled and passed portable/worker, native, editor and vendor ownership gates,
but its 112-case integration run failed (21 failures, one error, one skip).
Diagnostics include empty audio-device lists and live-startup failures, including
a control-window teardown race. Downloaded log hashes are in
`saved-state-recovery/completed-ci.json`. This remains failed evidence, not an
optional audio skip or a green cross-platform integration gate.

A controlled, sequential 60-second comparison reused the existing 9c5c6ebb1
and 99d72db32 binaries with the identical composed F04 song, SHA256
`07ad2f487ecb8a11e954b19369c22af7d888c01db62522d6b1b6a3df3418370a`,
at 48 kHz / 480 frames. The baseline had four deadline overruns; the candidate
had five. Both completed 6,003 callbacks without processor faults, starvation
or device errors. Maximum callbacks were 10,654 and 12,284.5 microseconds.
Thus the dense-song issue also exists in the earlier binary; these two samples
do not establish statistical equivalence or identify its cause. Reports and
unchanged project/binary checks are in
`same-song-audio-20261023-corrected-launch/`. The first helper attempt omitted
the mandatory automation flag and exited before running audio; its failed
receipt is retained separately. No extra build was used for this comparison.

Fresh Computer Use used two owned processes, both now closed normally:

- PID 15784: kept Balance lane, Fit, zoom and pan with synchronized range
  fields and continuous playback. The actual two-point take was only 507
  frames (10.5625 ms) long. The one-second Fit minimum still left its handles
  8.95 DIP apart: this is a real usability failure missed by the earlier
  50 ms fixture. Its 140.227-second dense-song run had 17 overruns, no
  starvation/device errors or processor faults, maximum callback 12,426.2 us.
- PID 43520: real vendor slider drag plus Right key captured three ordered
  original timestamped inputs. A later vendor edit after Stop preserved
  protection after Keep. Restore saved state was disabled during playback and
  explicitly refused while the vendor editor remained open. After stopping
  playback and closing only that editor, recovery succeeded, restored visible
  manual value .625, released protection and retained all three kept points.
  Song automation opened that exact lane; native Ctrl+S saved the private
  project. Read-only snapshots prove identical automation and document revision
  across refusal, recovery, navigation and save. The 467.620-second simple-song
  run had zero overruns, one starvation indicator, no device errors/faults and
  maximum callback 4,218.5 us. It is not a clean performance pass.

`recorded-viewport/foreground-check.json` deliberately records `status:held`
and `productionPackageAllowed:false`. Screenshots do not qualify flicker;
both audio reports retain `realtimeAuditPassed:false` and
`sustainedPresentationQualified:false`. No new production package or main
merge was made. The separately delivered 9c5c6ebb1 precise-note executable
remains unchanged.

At 19:08 UTC, new CI runs 40006849624 (Mac) and 40006850177 (Windows) were
still compiling the exact 4d1fa80fe source. Latest actual start was 18:52:42:
**no next checkpoint before 19:52:42 UTC**, and refresh later starts plus
completion of these runs before dispatch. Do not overlap another checkpoint.

## Next correction: sample-scale recorded editing

These changes are source-only; the preceding executable does not contain them.

- `AbsoluteAutomationWindow.hpp`: Fit uses the actual multi-point span with
  padding down to a sample, retaining a useful one-second view for a singleton
  and 0..8 seconds for an empty lane. Canvas Backspace deletes the selected
  point through existing automatic edit/history publication; text Backspace
  remains native text editing.
- `AutomationCanvas.hpp`: configurable position quantum and minimum zoom span
  let the absolute-time owner use one 48 kHz frame. Pattern/graph owners retain
  the existing one-unit mapping and quarter-row zoom defaults. Without this
  separation, fixing Fit alone still collapses adjacent sample-time points.
  No shared musical model, DSP, API or project format changes are introduced.
- `AutomaticEditsApplicationTests.inc`: uses the exact failed foreground
  timestamps and requires handles to occupy over 80% of the canvas. Covers
  adjacent samples at song start, late in the song and the legal endpoint,
  synchronized fields and zoom, native text Backspace, canvas Backspace/Delete,
  retained focus and exact one-step Undo.
- `test_plugin_recording.py`: extends the composed recording journey from
  Keep/navigation into immediate selected-point Backspace, exact Undo/Redo,
  another Undo restoring the take, then the existing Keep Undo and save/reopen.
  Playback epoch, continuing frames and unrelated music remain mandatory.

Next bounded local gate: command-results (including the new cases), source
navigation, parameter-preview and scratch-editor groups because the shared
native canvas geometry header changed; the extended composed-song app case.
Reuse the exact 4d vendor fixture/editor and recovery evidence if those inputs
remain unchanged. Build app/scanner/workspace targets once in an isolated
output, with all affected CI targets at the same hourly checkpoint. Perform
fresh Computer Use on the corrected short take: Fit, zoom, pan, canvas versus
text Backspace, Undo/Redo and keyboard focus while playback continues. Broad
validation is triggered by a geometry/default regression, changed shared
rendering or native provider dependency, or new composing failure. Dense-song
audio profiling, Mac native-device lifecycle and reciprocal edited-project
exchange remain open integration requirements; do not relax them to package.

## Cooldown continuation: enable the missing Mac live-device gate

The previous turn made concrete progress: the 4d checkpoint, fresh vendor UI
recovery evidence and the sample-span correction were completed. The latter is
committed at `45012bc4530a23cb33bd30c5b7226b382bf779ab`, backed up, and present
in the clean qualification checkout; it has not been built. Current PR CI still
tests 4d. At 19:18 UTC, Mac ARM was terminal success, Windows compiled and was
testing, and Intel was still compiling. The 19:52:42 gate remains provisional
on completion of those exact runs and any later actual build starts.

Current source adds the previously missing real Core Audio recording gate to
the existing Mac CI lifecycle invocation, using `--ui --loopback-record`.
This does not introduce another build or repeat the UI lifecycle command. Both
architectures will exercise the existing strict real-session multi-target
Keep/live release/Undo/Redo/save/reopen/Discard assertions rather than stopping
at the ordinary editor lifecycle. The tests now print actual before/after
frames and callback counts and check that the explicitly selected virtual
device is unambiguous, its buffer size is retained and the default output is
unchanged.

`.github/scripts/prepare-macos-audio.sh` provisions only a disposable
GitHub-hosted macOS worker. It refuses other environments before any download,
installation or service action. It downloads the official BlackHole 2ch 0.7.1
package with pinned SHA256
`57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf`,
checks package signing, installs it and restarts the runner's Core Audio service
before the native lifecycle process is launched. This never runs on the user's Windows host
or a musician's Mac and does not set a default route. Device absence and setup
failure remain failures; there is no fallback to artificial audio or a skip.
The dependency is a test device, not an application UI/audio framework.

Package version/checksum were reviewed against the
[Homebrew cask](https://github.com/Homebrew/homebrew-cask/blob/master/Casks/b/blackhole-2ch.rb)
and [upstream installation guidance](https://github.com/ExistentialAudio/BlackHole#installation-instructions).
The exact curl download used by CI succeeded locally and matched the pinned
bytes; no package was installed. Bash syntax and the non-runner refusal passed.
Evidence is `bin/mac-gesture-20261022/mac-device-preflight/preflight.json`.
The earlier urllib probe received HTTP 406; curl succeeded. No Mac execution
or live-device success is claimed before the next checkpoint.

The dense Windows audio investigation remains open. A bounded WPR CPU capture
attempt failed immediately with `0xc5585011` (unable to enable profiling policy);
the named session was then confirmed not recording. Its failure receipt is in
`dense-audio-profile/`. No usable CPU collection agent or WPA exporter was found
in the installed Build Tools, so no stack attribution was obtained and no speculative DSP change
was made. This tooling restriction does not block the other parity work. A
future device-specific profiler or bounded diagnostic instrumentation is needed
to distinguish DSP cost from scheduling delays; the observed overruns remain.

## Further source correction: pending recorded-point navigation

Source review found that the plugin selector already flushed pending automatic
edits, but the recorded parameter list and time-unit selector did not. They
rejected a valid pending edit until its debounce elapsed. Undo behaved similarly;
From rack could cancel the timer and replace a pending draft. The new correction
flushes queued valid edits before parameter/unit changes, Undo/Redo, From rack
and last-touched navigation. A requested parameter is captured by stable ID
before save refreshes the list, and revalidated afterwards. Invalid raw input
and stale revisions still refuse navigation; no silent rebase or retry is added.

The command-results native group now covers rapid parameter change with exact
unrelated-point preservation and one-step Undo, units/focus retention, immediate
Undo of a pending field edit, invalid text retention for each affected action,
and an external document edit during the pending draft. These assertions and
the source correction are unbuilt. Add rapid typing followed by parameter/unit
navigation and Undo to the next foreground checklist. The bounded four-native,
one-composing-app local gate stays the same; the Mac live-device CI addition
joins the same coordinated checkpoint, not a separate build.

## Same-batch continuation: native Save and song history include queued lane edits

Source inspection found that the shell flushed queued precise-note edits for
native Save/history, but did not do so for the absolute Song automation editor.
The next candidate finishes an already queued recorded-point edit before native
Save and song-level Undo/Redo. Redo availability accounts for the new edit that
would replace its branch. It does not replay failed or uncertain automatic
writes: a repeated Save saves accepted music while retaining the rejected raw
draft, and song history refuses an unresolved lane draft. No shared API, project
format or audio-provider change is introduced.

The command-results native group now asserts immediate Save, exact field text,
caret/focus, one-step Undo/Redo, invalid-input refusal, no retry, and reopened
saved points. The existing complete composing app case adds a native Save of a
fresh point value during playback, exact unrelated-point preservation, text
selection/focus and Undo. The existing precise-note native Save app case and
menu-bar group join the bounded local gate because the shell boundary changed:
five native groups and two actual-app cases, using one build. These additions
are source-only until that checkpoint.

The same boundary also covers closing Song automation: its generic tool shell
previously cancelled the debounce and left a valid edit requiring a retry after
reopening. The native owner now finishes an already queued edit before hiding,
retains invalid input visibly on failure, and cancels an in-progress drag first.
The same command-results group verifies close/reopen, exact one-step Undo and
invalid-input retention; no additional build or test group is needed.

At 19:35 UTC the preceding 4d Mac ARM job passed and Intel remained in tests.
Windows compiled and passed 158 portable/worker tests, 51 native groups, vendor
ownership and typed conformance, but its 112-case app run failed 22 cases with
one optional skip; live startup reports show no audio endpoints. Preserve that
failure. Downloaded Windows artifact `12525264408` has SHA256
`f8258e61dccad2b9bbe4271537924610c9ef8b7957932c308c5ae1afc9d53b36`.
Its five fixture save/reopen comparisons and edited metadata, composing-history
and envelope-bank reports pass, with report hashes checked. This is the Windows
leg only; it does not establish reciprocal edited-project preservation.

## 20:10 UTC checkpoint: short recorded gestures and remaining limits

Candidate `5799372032e82fda77c048d4a99bc90b1c127bee` compiled once locally
at 19:53:18 UTC. PR7 was pushed at that coordinated checkpoint. Executable
SHA256 is `64eac162f9bed177764af8bca71fe80063f861140ee788c74c78b0ce1d589327`.
Receipts are in `bin/mac-gesture-20261022/sample-span-navigation/`.
Four of five native groups passed. Command-result failed because the new test
selected catalogue row 1 while expecting parameter ID 2. The actual Gainer
catalogue is Enabled (0), Gain (1), Balance (2). The correction resolves rows
from stable IDs; it retains all target, history, focus, invalid and stale-draft
assertions. Later assertions in that failed native group were not executed.
The failed receipt is unchanged. A separate diagnostic app run passed both the
complete composing/recording journey and native precise-note Save case in
29.393 seconds. This is diagnostic evidence, not an all-green gate.

Fresh Computer Use on isolated PID 35960 confirmed that Fit separates the
507-frame take's two handles, zoom out expands the range, Fit restores it,
dragging saves without Apply and the native Undo button restores the exact
automation inventory. Three PID-scoped read-only snapshots show increasing
frames (3535680, 15696384, 16594944), playback epoch 1 throughout and unchanged
unselected points. The disposable song was saved and the process exited
normally; its binary was unchanged. Do not reuse its handles. The foreground
receipt is explicitly **held** and prohibits packaging.

The helper exposes Song automation only as related UI beneath Main. Sending a
key targeted at Main reactivates Main, even after the floating edit visibly
has its caret. This run does not qualify floating typing continuity or establish
an application focus bug. Some freshly emitted child indexes were rejected by
the helper cache; fresh screenshot coordinates allowed the pointer diagnosis.
Long binary decimal tails remain an ergonomic follow-up. Neither screenshots
nor this partial native accessibility tree establish temporal flicker or
Narrator qualification.

Audio over 395.284 seconds at 48 kHz / 480 frames reported 31 deadline overruns,
2 starvation indicators, zero device errors and no processor fault.
`realtimeAuditPassed` and `sustainedPresentationQualified` are false. No broad
performance pass or new production package follows. The previously usable
Downloads build remains `ScreamSeq-Windows-ARM64-composing-9c5c6ebb1/ScreamSeq.exe`.

### Reciprocal preservation completed without another build

The retained-app Mac exchange run `40013382403` and Windows return runs
`40013857540` (ARM input) / `40013869640` (Intel input) all passed. Each native
architecture/return leg passed no-edit fixtures, edited render/import,
metadata, composing history and envelope-bank preservation. Downloaded artifact
digests, exact binaries, input hashes and report hashes are retained in
`recorded-viewport/reciprocal-exchange.json`.

The actual compiled CI identity is merge commit
`7a375c3f8779c9f165c6af147671a59bb46cf77c`, whose parents are main
`0fe8f34008fe6517d7598affc31a4948a2ab14b8` and reviewed head
`4d1fa80fe952e0df57d158cca654f920a99cc797`. Fetching that exact merge and comparing
trees verified that both merge and head have tree
`8a2b8de77053bad1a7eb1c7a21941ce65ab62d13`. This evidence covers unchanged shared
codec/preservation inputs, not the later native Save/navigation UI changes.
The preceding Windows app suite remains failed: its services successfully
started but the actual device catalogue was empty. Do not label CI green or
weaken live-startup assertions.

### Source continuation awaiting the next checkpoint

`AbsoluteAutomationWindow.hpp` now finishes a queued valid edit before Select,
Previous, Next or Add point. Explicit point-index navigation captures the
requested index before save refreshes the fields; relative navigation follows
the edited point after sorting. Invalid, stale or uncertain drafts cannot be
discarded by navigation. Standard native controls now have explicit accessible
names, including distinct previous/next and pan/zoom labels, and native
Tab/Shift+Tab order follows the visible groups. No shared musical model, public
API, codec or audio-provider behavior changes.

The existing command-result group adds immediate point navigation, requested
destination/focus, exact one-step Undo, invalid-input preservation, accessible
names and forward/reverse native traversal. These corrections are source-only.
Keep the five native groups and bounded composing/Save app gate; use a fresh
isolated Computer Use process for the new executable, with honest floating
keyboard limitations. Do not rerun the preceding diagnostic app cases against
the unchanged binary. Before the next build, inspect current CI and actual
later starts. At 20:07 UTC all three compilation jobs were running. The earliest
known next checkpoint is **20:53:46 UTC**, subject to later starts or active CI.
No main merge or package is permitted from these partial receipts.

## 20:20 UTC continuation: automatic recorded-point composing checks

The next source batch additionally flushes an already queued valid field edit
before a canvas click/double-click. Previously the mouse handler silently
ignored clicks while a field draft awaited its timer. Rejected or uncertain
drafts still refuse the new canvas action and retain the field; native tests
require the clicked point, focus, unchanged other points and exact one-step Undo.
Generated recorded values reuse `PluginParameterField::editableText`, including
its strict endpoint handling, so promoted floats display as `0.2` rather than
`0.20000000298023224`. Active raw input and caret are still protected. New native
assertions require generated-text no-op, exact frame/scalar preservation when
editing only time, and unchanged raw spelling/selection after automatic save.
These product corrections are unbuilt.

A bounded audit of `test_absolute_automation_ui.py` on the unchanged 579 binary
found obsolete manual-Apply/stop-playback expectations and a retired editor used
after `document.open`. Initial receipts retain two failures and a geometry pass.
Corrected seconds/frames/history/reopen and stale/invalid/target-removal journeys
then passed (17.833 seconds). The updated live journey passed (8.146 seconds):
automatic field save, exact Undo/Redo and Save all retained active playback,
the same epoch, advancing frames, zero overruns and no fault; explicit Stop then
reopen preserved the exact lane and opaque plugin state. This is a short gain
fixture, not evidence against the dense-song performance failure. The geometry
journey passed separately in 4.626 seconds after selecting an explicit 0–8 second
viewport instead of assuming the old unfitted range. Receipts, executable/test
hashes and live snapshots are in `absolute-journey-audit/`. No build was run.

These tests now assert accepted automatic music instead of transient dirty
timing. Invalid/stale draft protection, exact unrelated data, no-op/history,
native controls, sample-frame precision and save/reopen checks remain. Four
absolute-editor journeys join the existing single-build Windows integration
stage (116 cases); this does not skip missing-device failures. The next local
gate is five native groups plus six actual-app cases, followed by fresh Computer
Use. Reuse the diagnostic passes only for their unchanged inputs; they do not
qualify the source-only formatter, click, accessible-name or Tab-order changes.

At 20:16 UTC the current Mac ARM and Windows builds had compiled successfully;
their tests and the Intel build were still running. The next checkpoint remains
no earlier than 20:53:46 UTC and after fresh confirmation of terminal preceding
jobs. The exact next source is pinned in `recorded-point-navigation/freeze.json`.

At 20:21 UTC Mac ARM job `120624496235` completed successfully. Its downloaded
log SHA256 is `23d4a5132689d47729b107f847546134b5118b5952449245a3cdd10f78f7738c`.
It passed 134 CTests, the full native interface and socket/typed gates. Crucially,
the log confirms actual `--ui --loopback-record` execution against explicitly
selected BlackHole 2ch, buffer 512: the real-session multi-target lifecycle
passed live Keep/Undo/Redo, release/Discard and save/reopen with frames
5120→9728 and callbacks 10→19. This closes the previous missing-device-execution
evidence for that ARM fixture, not physical audio, arbitrary vendors or Intel.
The actual CI merge commit `0ba43dd056eaf41cd6156f8628779fad77fb472c` and reviewed
head579937203 both have tree `9570dc1a379fb5cf644d7d814eb131c4a5e2575d`, verified
by fetching and comparing the exact merge. No UI correction after579 is covered.

## 20:30 UTC continuation: device diagnostics and completed Windows CI

The application report now includes the device's already-collected maximum
wake gap, wait timeouts, negotiated buffer/channel counts, shared mode and
fallback error. Only report serialization changes: no callback instrumentation,
DSP, device settings, public editing API or thresholds change. Maxima are not
time-aligned, so they cannot identify the cause of the31 overruns by subtraction.
The next normal foreground report must parse these fields and retain the existing
failure counts and false audit/presentation qualifications. This remains unbuilt
and joins the existing five-group/six-app-case checkpoint, not a separate build.

Windows579 CI job120624496182 is terminal: compilation,158 portable/worker tests,
scratch checks, vendor ownership and typed/socket gates passed. Native groups
passed50/51; command-result is the same stable-parameter-row fixture failure
corrected in the pending source. The112-case app suite recorded22 failures and
one optional skip. The audio availability probe returned no devices; live
startup failures and three secondary teardown failures remain recorded. The
completed log SHA256 is
`0ce88d11b0af2e8b3a01afa20e68bfdef7ecc980a39732667dd4b9490ffa6af6`.
Do not relabel CI green or omit these assertions. Intel compiled successfully
at20:24:58 and its remaining tests were still running at20:28.

The exact retained x64 CI executable (SHA256
`4791d07e04988392ba62df55610319f79442bb1447a6ddaa3386f8f41f0786f1`)
passed three unchanged live journeys locally in136.710 seconds: six-domain
composing/recording, queued precise-field native Save, and the95-second sustained
recording hold with history/reopen. The process ran on a private desktop under
ARM64 emulation with an available endpoint and silent output. The outer artifact
12529143173 digest, nested archive, BuildInfo and executable hashes are verified
in `sample-span-navigation/ci-x64/verified-identity.json`; `replay.json` records
the exact CI merge and matching reviewed head. The only changed client files
were an unselected absolute-editor module and unexecuted C++ native tests.
No assertions/timeouts changed. This is bounded evidence for three missing-device
failures, not a full suite replay, native x64 hardware or foreground qualification.

Source review also found Delete point/Clear lane refusing a valid queued field
before its timer fired. They now finish that edit first, then perform deletion
through the existing automatic lane commit. A rejected/uncertain draft still
blocks deletion. New native checks require exact unrelated-lane preservation,
Undo deletion restoring the edited value, the following Undo restoring the
original value, and invalid raw input retained without changes. This is part of
the same pending checkpoint; no extra build/test cycle has run for these edits.

## 20:45 UTC continuation: recorded activity selection and read-time input

Current Windows `ParameterActivityWindow::commit` reread the lane and always
selected its first row. Mac `ParameterActivityEditor.loadRecorded` already
retains the edited frame identity. Windows now selects the accepted frame after
value/time edits, including sorting past another point; deletion or a move
outside the visible page leaves no selection rather than redirecting the next
edit to another point. This does not change the shared edit API or storage.

A new actual-app journey against the retained579 x64 binary initially found
the requested value had not been saved. A diagnostic rerun with full activity
state reached the confirmed first-row selection failure. Both failed receipts
are retained in `activity-selection/` and `activity-selection-diagnostic/`.
Source inspection found Save/Delete clicks were silently ignored while the
observation read pumped native messages. The correction retains one explicit
point action during observation, then dispatches after that read with the same
draft generation. It never queues a second mutation from inside a mutation or
replays a submitted uncertain outcome. Changed fields refuse the deferred edit;
Save/history/close drain an already requested action without submitting raw
fields that have never been accepted explicitly.

The native activity group now exercises a Save pumped inside an observation,
project-Save draining exactly once, changed raw input refused, a later-row value
edit, preserved native field focus/raw text/selection, time reorder, deletion and
unrelated-point preservation. The actual-app
journey checks accepted point values/timestamps and selection, exact other-lane
preservation, Undo/Redo, continuous playback and save/reopen. It awaits accepted
state without repeating native input. These new assertions and product changes
remain unbuilt. Include `parameter-activity-window-tests` in the existing build:
the next local gate becomes **six native groups and seven app cases**, and the
same Windows CI integration step becomes117 cases. Fresh Computer Use must also
inspect Activity recorded-point selection after editing a later row.

Intel579 job120624496559 is now fully passed:134 CTests, native interface and
socket/typed checks. Its actual BlackHole2ch512 device lifecycle passed live
Keep/Undo/Redo/release/Discard and save/reopen (frames16384→19456,
callbacks32→38). Downloaded log SHA256:
`ecaaf84ce35ce71a55d4665e6681a09675eb417f0312c63850a500b829204db1`.
Both Mac architectures therefore cover this required device fixture on the
tree-identical579 CI merge. AU stopped-only restoration, arbitrary vendors,
physical hardware and sustained performance remain separate limits. Do not
repeat either completed Mac job. The next hourly gate is still20:53:46 UTC,
subject to a fresh check for later starts. No newer package or merge is qualified.

## 21:11 UTC checkpoint: retained point editing and diagnostic UI

Source `06338394055881fc38069ae9fcd0fd32d0dddb05` was built once locally,
20:54:19–20:56:19 UTC. Executable SHA256:
`91c6f33ac27d876f3771e86ed783433990ba029811977a1b601d77c2fd58e2fd`.
PR7 was advanced at the same checkpoint. The latest actual CI build start was
20:54:49; no next build before **21:54:49 UTC**, and only after checking later
starts and completion of the current jobs. Do not repeat this attempt.

Five of six native groups passed: source navigation, scratch editor, parameter
preview, menu bar and parameter activity. The command-result private child
exceeded its existing 90-second bound. Inside it, automatic edits passed in
26.093 seconds (including the new navigation, formatting, Delete/Clear, exact
history, native names and focus assertions), recovery passed in39.986 seconds,
and export, graph usability and three routing cases passed. It reached catalogue
refusal before the cumulative deadline. Preserve `recorded-point-navigation/`
`checks.json`, `native.log` and `native.xml`: this gate **failed**.

A separate diagnostic run of all seven selected actual-app journeys passed in
63.939 seconds, using the exact candidate and clean qualification source. Its
receipt is `app-diagnostic/checks.json`; it does not overwrite the failed native
gate or qualify a package. In particular, the new Activity journey accepted
later-row edits and time reordering, retained selection, preserved unrelated
automation, and passed Undo/Redo, continuous playback and save/reopen.

Fresh Computer Use on owned PID36252 (now exited normally) verified short-take
Fit, compact generated values, exact visible Undo, and docked precise-note Save
caret, Tab/Shift+Tab, text Backspace and typing. A click on the second recorded
handle also revealed a real defect: frame7510073/value0.8 became
frame7510080/value1.6541353 without an intentional drag. Read-only snapshots
`after-click.json` and `after-undo.json` establish the edit and exact restoration;
all snapshots retained playback epoch1 while frames advanced4080096→9157536→
14081856. Main-targeted helper input could not qualify the floating Activity
parameter picker or typing continuity. Do not classify that limitation as a
new application defect. Native Activity focus assertions passed separately.

The 315.800-second silent foreground workload at48kHz/480 frames still reported
30 deadline overruns, zero starvation/device errors and no processor fault.
New diagnostic fields parsed correctly: lowLatencyShared, buffer1056 frames,
two endpoint channels, zero fallback error/wait timeouts, maximum callback
13.022ms, service13.040ms and wake gap24.440ms. These are independent maxima,
not time-aligned samples; they do not attribute the stalls. Both realtime audit
and sustained-presentation flags remain false. No performance/flicker claim,
production package or merge follows. Foreground evidence is deliberately under
`foreground-diagnostic/`, with automated/foreground qualification false.

### Source continuation for the next hourly checkpoint

1. Split the oversized native group into command results (export/recovery),
   automatic edits, and routing commands (graph usability and all routing
   cases). Every existing assertion remains; private-child90s and CTest120s
   limits are unchanged. CI labels select all three without another build.
   The next local selection is **eight native groups / seven app cases**.
2. Activity observation no longer freezes merely because native text input
   arrives while a read pumps messages. Observation does not adopt point fields;
   field-adopting reads retain generation checks, and document/revision/watch
   guards remain strict for every read. Native tests cover pumped text through
   both observation/source reads, raw draft, focus/caret, unchanged music,
   continued cursor consumption and rejection of a revision change mid-read.
3. Recorded-point canvas selection now waits for movement outside the native
   DPI-scaled drag box with the left button held before changing music. Native
   assertions include stationary moves, one-pixel jitter, a move without the
   button, exact unchanged revision/music, real drag preview/automatic save,
   and exact one-step Undo/Redo. Fresh Computer Use must repeat a plain click
   and a deliberate drag on the short507-frame take.

These corrections are source-only. Do not infer their success from the063
diagnostics. Current CI runs40022802380/40022802381 were still compiling at
21:07 UTC; inspect final results without restarting them. The prior579 Mac
device and reciprocal compatibility evidence remains valid for unchanged
shared code, but does not qualify these Windows UI corrections.

### 21:18 UTC continuation: visible Activity trace and keyboard access

The same063 foreground session showed an apparently empty Enabled trace while
its reading reported a constant1. Source confirms that equal observed minimum
and maximum mapped every point to the clipped bottom border. Mac's trace uses
the declared parameter range; Windows retains its observed-range presentation
but now puts a constant value at the vertical centre, keeps extrema two DIPs
inside the clip, and draws a marker even for the first captured sample. Measured
values and contribution filtering are unchanged; this is not fabricated activity.
The transient workspace trace snapshot exposes plot/last-point coordinates in
owner-client DIPs for exact native and actual-app geometry assertions.

Activity now also has eleven explicit native accessible control names and
forward/reverse Tab order following its visible groups. Its existing native
group checks names through Windows' accessible provider, all recorded-page Tab
stops, constant traces at-1/0/1, exact native values and no musical mutations.
The existing actual-app Activity journey now requires the initial constant
Enabled trace to be captured and visibly inside the plot before editing Gain.
No extra registration or app case: the next checkpoint remains eight groups /
seven app cases. These additions are unbuilt; preserve the original unexecuted
freeze when updating `recorded-edit-guard/` to the next source commit. No new
build, private test process, foreground process or CI dispatch ran for this
continuation.063 CI was authoritatively still compiling at21:12 UTC.

### 21:21 UTC continuation: stable recent-event selection

Current source also rebuilt the Recent changes list on every observation poll
and restored only its row number. Once the bounded512-row page rolled forward,
that row could mean another event and Open source would follow the wrong entry.
The correction retains the displayed event sequence and watch token, remaps
selection and top visible row after an append, and clears selection when its
event expires. It leaves unchanged native list content in place. Musical data,
the8192-point observer bound and512-row display bound remain unchanged.

The existing native Activity group now observes real LB_RESETCONTENT messages
to require zero list rebuilds for unchanged data; it verifies native focus,
selection and scroll through a rolling page, retained source navigation,
expired selection refusing navigation, and exact unchanged lane/revision.
These assertions are source-only and remain in the same eight-group/seven-case
checkpoint. ARM063 compiled at21:13:54; its tests and the Intel/Windows jobs
were still running at21:20:21. No new build was started or old job restarted.

### 21:33 UTC continuation: inspect a trace and reach its source

Current Mac `ParameterTraceView.swift::mouseDown` selects the nearest observed
sample; `ParameterActivity.swift::trace.onSelect` freezes that view and retains
its source for navigation. Windows previously drew the trace without a mouse
handler. `ParameterActivityWindow.hpp` now selects a native-value event by its
stable sequence, freezes the displayed capture, marks the selected event and
shows its time/value/source. Left/Right and Home/End inspect visible events;
Enter uses the existing guarded source-navigation path. F6 moves between the
trace and the native details list. Normalized graph contributions remain
excluded from this native-unit plot. This changes observation/navigation only,
not the shared API, musical history, DSP or saved project representation.

A raw recorded-point draft refuses trace selection without losing focus or
text. A changed document/revision refuses source navigation. Selection during
either pumped observation or source read preserves the exact displayed points
and cursor until Resume. Hit testing uses the same inset as drawing. Target
refresh clears selection, while explicit list selection chooses the list source.
The existing native Activity group asserts these boundaries, exact unchanged
music, keyboard traversal and source identity. Its existing actual-app journey
now also clicks Gain's trace and requires the linked absolute editor to show
that exact plugin/parameter without changing any lane. All these new assertions
are **unbuilt**; the checkpoint remains eight native groups/seven app cases.
Fresh Computer Use must check trace selection/keyboard/source navigation in
addition to plain recorded-point click versus deliberate drag and Save focus.
The trace also explicitly handles Tab/Shift+Tab to the following details group
or preceding time-range control; a custom canvas must not trap ordinary Tab.
Native assertions exercise both directions and F6 through actual child controls.

Read-only063 ARM CI inspection confirms134/134 CTests, the full native interface
and socket/typed gates, and the actual BlackHole2ch512-frame live take lifecycle:
frames16384→19456, callbacks32→38. Exact job120651361907 built the CI merge
`2f04f203b023a559bd31d579fc7873c45bc8ed5f` of063 into main0fe8f34.
The retained log SHA256 is
`7dc9539cae217aff4c32ec912c63f9470b7243bc47b14b5c2b5ddc30eb9fc975`.
Windows/Intel tests were still active at21:32:06. This is Mac evidence for the
earlier candidate, not qualification of the pending Windows corrections. No
build, test process, package, main merge or CI restart ran for this continuation.

### 21:36 UTC: terminal Windows063 CI

Job120651362736 compiled and passed158/158 portable/worker registrations,
51/51 native editor groups, vendor ownership and typed/pipe conformance. The
117-case integration step failed24 assertions across21 distinct live-startup
cases (three additional teardown failures) and recorded one skip. Routing
readiness diagnostics report `precedingOwnedAudio.data.devices: []`; the other
failures require an active WASAPI clock or report a terminated owned child.
This supports the endpoint limitation but does not turn those failures into
passes or qualify the complete live suite. The exact retained log SHA256 is
`7750da328f0ceafbd96fa7269330c4b71cff67737c2848d6929783ef16aae5d8`.
The local063 cumulative group timeout remains separate failed evidence even
though the same group completed on CI. Its source split retains all assertions
and bounds. Intel was still running at the read-only collection; do not restart
either job. The next local8/7 selection and fresh foreground gate remain required.

### Recording-review continuation: retained original input identity

Source inspection found two related review faults in
`windows/App/ParameterRecordingWindow.hpp`. Accepted native deliveries leave
the retained queue, but its list kept a numeric row selection; a following
input could silently replace the selected original input. Also, a new take
with the same plugin/parameter catalogue could skip target rebuilding and
retain the preceding take's list selection or display a delivery page behind
the old parameter choice.

The native view now keys its target catalogue and displayed page by take,
preserves a uniquely matching original delivery through removal and status
changes, and clears selection when the original input expires or is ambiguous.
It does not invent a persisted event ID or alter the delivery/recovery format.
List-state notifications are suppressed only during presentation, so the
unchanged details text/caret selected for copying is not cleared and rewritten.
Captured-event pages remain append-only within a take; fresh pages select their
first input and scroll it into view; ordinary polls preserve scroll position.
The existing native recording-review fixture asserts new-take
identity, identical-row replacement, removed preceding input, outcome changes,
expired/duplicate inputs, native focus/text selection and exact unchanged song
and write count. These assertions are **unbuilt**.

Add `workspace-plugin-search-tests` to the existing bounded selection (same
workspace executable and build targets): the next gate is **nine native groups
/ seven actual-app cases**, then fresh Computer Use. This group took4.27 seconds
on063 Windows CI. Keep every earlier assertion and failed receipt; no new build
or test was run for this source continuation. The hourly gate is unchanged.

Both063 Mac jobs are now terminal successes. Intel job120651362328 passed134
CTests, full native interface/socket/typed gates and the actual BlackHole2ch512
take lifecycle (frames12800→18432, callbacks25→36). Its retained log SHA256 is
`eefafd899346fb654f10ff4be6114724556213bf13140170e3381ec69406d3b5`.
Read-only fetch verified CI merge2f04f203 and reviewed063383940 have the exact
same tree, `99e65b719d47f0cbff527c3d2f25919b66c9de4c`. These passes protect the
unchanged Mac/shared inputs; Windows live-CI failures and pending UI corrections
remain separate. No build or old job was restarted.

### 21:55 checkpoint: recorded editing, observer navigation and audio diagnosis

Frozen `20467c6eee7081c04b344c96bc8ae15413111a27` built once from
21:55:08–21:57:09 UTC. Executable SHA256:
`fb0a340451077995b5411d5d0dcdb287ffa8a8c267eb4670869ec034e9480a49`.
`bin/mac-gesture-20261022/recorded-edit-guard/` retains the build/check receipts.
All **nine native groups passed** (138.76 seconds). **Five of seven actual-app
cases passed**: full composing/recording/history/reopen, precise live Save,
absolute time/history/reopen, live point editing and mouse/keyboard/zoom.
The gate remains failed, not superseded by diagnostic runs:

- The stale-draft case's setup left the second automatic point unsaved with
  `Document worker is busy; no mutation was queued`. Its exact two-point local
  draft survived, but it needed manual retry. This is a product defect.
- The Activity case selected a **baseline** trace event but expected recorded
  automation. The implementation correctly routes baseline provenance to the
  plugin rack. The fixture now asserts that destination and exact Gain/dB
  selection, then uses the explicit Song automation action for the lane.
- Separate, retained diagnostic attempts found two readiness issues in the
  fixture: audio activation alone does not establish prepared activity targets,
  and the rack's timer presents the parameter after navigation returns. The
  fixture now waits for those bounded, explicit conditions and checks the full
  `Gain / dB` native label. It does not reopen/retry a failed observer. A later
  diagnostic still retained Enabled, motivating the selector-race correction
  below; no further run of this unbuilt correction has occurred.

Fresh Computer Use on isolated PID4652 used this exact binary and a disposable,
silent `qa-composing.screamseq`. It verified recorded-point selection versus
dragging: clicking changed no accepted automation, deliberate dragging saved,
and Undo restored the entire original automation snapshot. All five API reads
retained playback epoch1 and active playback. The constant Enabled trace was
visible, click froze its selected event, and Open source selected the rack.
Docked precise velocity retained its visible caret through Ctrl+S, traversed
Tab/Shift+Tab, and accepted End/Backspace (97→9) and typing7 (9→97) without
deleting any of its four hits. The QA process exited normally with unchanged
binary. Evidence: `foreground-diagnostic/foreground.json` and its named API
snapshots. Main-only helper targeting prevents a floating-keyboard continuity
claim; UIA still reported the root for docked text, so native focus assertions
remain necessary. Resize/dock/float was not repeated in this diagnostic.

This is **not a performance pass**: 317.488 seconds at48kHz/480 frames recorded
22 deadline overruns, zero starvation/device errors, max callback13.332ms,
service13.354ms and wake gap29.503ms. Both realtime audit and sustained
presentation flags are false. Screenshots do not qualify temporal flicker.
No package or merge was made. CI40030164522/40030164525 was still compiling at
22:08:46; latest observed build start21:55:37 makes the next checkpoint no earlier
than **22:55:37 UTC**, subject to later starts and all prior jobs becoming terminal.

#### Source continuation awaiting the next build

1. `AbsoluteAutomationWindow` now observes the native owner's busy/recovery
   admission state before consuming automatic input. It retains an **unsent**
   edit until idle; it does not catch/retry failed or uncertain writes. Existing
   revision guards still reject a changed song. The automatic-edit native group
   adds deterministic busy/release, exact one-step Undo, raw text/caret/focus and
   newer-document rejection assertions.
2. `ParameterActivityWindow` previously restored and discarded processor or
   parameter choices made while its polling read pumped messages. It now keeps
   the latest stable target/parameter intent and loads it after the owner is
   idle, guarded by captured document/revision/draft generation. Newer explicit
   actions cancel the intent; hiding cancels it. Native tests inject selection
   during observation, busy deferral, changed revision, newer raw input and
   closing. The app journey explicitly waits for parameter1 before accepting
   its trace, so Enabled cannot satisfy a Gain navigation check.
3. `windows/Audio/DeadlineEvidence.hpp` adds32 fixed, first-failure records to
   WASAPI. Each record aligns callback ordinal/frame offset/stream generation,
   QPC service start/frequency, wake gap, pre-render/render/post-render/service
   durations, padding, frame count and both existing deadline budgets. Overflow
   increments an omitted count without overwriting earlier failures. The audio
   owner writes plain fixed storage; only a lifecycle-serialized, **joined**
   reader can copy it. A live or merely faulted-but-unjoined worker refuses the
   read. No allocation, logging, locks, clocks or serialization were added to
   the callback; existing clock reads are reused. Overrun thresholds/counters
   remain unchanged. Main serializes the evidence only in its stopped report.
   These diagnostics distinguish phases of the same failing callback; they do
   not yet attribute CPU stacks or establish a performance improvement.

The existing `WasapiDeviceTests.cpp` now covers bounded exact records/reset,
host C++ allocation/free audit, live/unjoined refusal and joined hardware
accounting. Its existing executable is added to the main Windows CMake project
as `wasapi-diagnostic`, avoiding a second standalone build. Next checkpoint:
the same nine native groups **plus `wasapi-lifecycle`**, the same seven app cases,
then explicit `wasapi-diagnostic --silence 3 500` on this host, followed by fresh
Computer Use and an idle silent-app report with aligned deadline evidence.
Native audio absence/faults remain failures; do not infer hardware success from
the no-hardware lifecycle group. The audio service audit covers host C++ new/delete,
not driver/vendor allocations or lock instrumentation. Both native UI changes
and audio diagnostics are **source-only** until that build/checkpoint passes.

Mac/shared musical model, codecs, plugin state and project/API write semantics
are unchanged. Prior reciprocal preservation and Mac evidence can be reused
only for unchanged inputs. New Windows source cannot inherit20467's local passes.

#### Recording target navigation continuation (source only)

Review of `ParameterRecordingWindow` found that its target combo could change
while `automation.recording.get/events` pumped messages, but the pending-action
branch discarded the notification. The combo could then name a different
parameter from the retained event page. The window now retains the latest
stable plugin/parameter choice until the owner is idle, while keeping the
displayed combo aligned with the current rows. A changed document, revision,
take or removed target rejects the deferred choice; a newer direct choice,
failed read or Close cancels it. Close also works during a polling read.

Keep retains the explicitly chosen stable target for its Song automation
shortcut, including a choice queued earlier in the same refresh. Commit,
Discard and Start cancel obsolete page navigation after capturing that intent.
This does not queue/replay a musical write or change recording/history/API
semantics. The existing lifecycle completion and input-loss guards remain.

The existing recording-review fixture in `workspace-plugin-search-tests` adds
message-pumped target selection during both summary and event-page reads,
busy-owner deferral, latest-choice precedence, exact target/details/focus,
changed revision/take, removed target, read failure and Close assertions. All
navigation must preserve exact musical data and the write count. It also
retains the existing Keep/restoration navigation and receipt assertions.
The next checkpoint still has ten native groups/seven actual-app cases plus
the explicit silent hardware audit. No extra build or test run was made for
this continuation. Fresh Computer Use should include the recording target
selector and Close while polling, alongside Activity navigation, precise
Save/caret and live recorded-point editing.

#### Direct Activity edits and failed-target presentation (source only)

Current Mac `ParameterActivity.swift` supports direct recorded table edits via
`automation.recorded.edit`; the Windows Activity form still required Save point.
Windows now queues edits to an existing selected point after a short typing
pause, coalesces successive input and waits for the owner to become idle. The
existing captured revision, selected frame, generation, atomic API, shared Undo
and persistence path remain authoritative. Read-only observation cannot submit
the edit reentrantly. New point fields still require **Add point** so incomplete
time/value entry cannot create accidental points; existing points offer
**Save now** as an immediate alternative. The page explains automatic editing.

An explicit Save/Delete queued during observation retains its original generation
and cannot be replaced by newer typing. Invalid, stale, rejected or uncertain
automatic input remains raw and is not retried by project Save or history.
Newer text arriving during a write is not silently rebased. Switching the details
page does not drop an already queued edit when another editor owns the worker.
The native fixture preserves all reentrant-read, exact-lane, invalid/raw/caret,
stale and explicit-intent assertions; it additionally requires exactly one
automatic write after idle, no write on stale/invalid input, coalescing, hidden
page completion and no partial New-point creation. The existing actual-app
Activity journey now edits value and time **without clicking Save**, retaining
its exact unrelated-lane, stable selection, Undo/Redo, opaque plugin state,
continued playback and save/reopen assertions. Counts remain ten native groups
and seven app cases. These behavioral changes are not yet built or executed.

Recording review also restores the previous selector when loading a new target
fails and its old take/page is still current, retaining exact rows, selected
input, details and focus. Native failures are injected separately into the
summary and event-page reads. This closes the failure path where a new label
could appear above old inputs; it does not fabricate successful reads.

The preceding20467 Mac ARM job120676170951 is terminal success. Its downloaded
log SHA256 is `c91fe169cd4164d7cc4d67628f82c9dc7d4895c3270ded2044810a1f4b7e6ef6`:
134 CTests, native interface and socket/typed contracts passed, plus the actual
BlackHole2ch512 live take lifecycle (frames16384→25600, callbacks32→50).
The log and completion receipt are retained in `recorded-edit-guard/`.
Windows and Intel remained active at the22:34:56 observation. No job was restarted,
and none of this Mac evidence qualifies the new Windows source.

20467 Windows job120676170980 subsequently finished with a failed live-app gate:
compilation,158 portable/worker tests and53 native groups passed;117 app cases
reported24 failures and one skip. The failures are21 distinct live-startup
failures plus three secondary missing-window teardown failures. Its retained
audio-environment probe returns `audio.devices.get.devices=[]`; both
AudioEndpointBuilder and Audiosrv were Running after explicit service startup.
The app binary hash in that probe is
`3fb979c8971e24a63177b152096abb177c5a7c137fc78279a74a885e8df11604`.
Do not convert unavailable audio into a pass or infer current Windows UI success.

Evidence is `recorded-edit-guard/windows-ci-classification.json`, completed job
log SHA256 `01654b36d795ef1459c54b8e2044681e8b5f4185340ade516ccb3899ca6a0f47`
and downloaded artifact12536447132 SHA256
`f5df0c519b86f9f8b0e68d70f95cb444edf10f5521d201e3a3de3f7fbd35b553`.
The classification retains the exact diagnostic payloads and failure counts.
No app or CI job was rerun for this inspection; Intel job120676171254 remained
active at the latest read. The hourly gate still requires it to finish and all
actual build starts to be rechecked.

The same unbuilt Activity batch now uses `NativeControls::curveColors` and the
shared native curve handle/focus border, matching the other Windows automation
editors. This replaces fixed trace colors with the existing system high-contrast
palette and preserves selection through size/outline as well as color. Its
native group now sends all three appearance-change messages and requires exact
observer/draft/selection state, text caret/focus and unchanged music. No desktop
theme was changed. Reusing the previously tested palette does not establish a
new live high-contrast or Narrator pass; fresh normal-theme presentation and the
separate full accessibility gate remain required. No additional build target or
test registration is introduced.

### 23 October 23:09 UTC: owner-admission checkpoint and fresh foreground evidence

Built `3408dd9b2201f49d95e8e7b0bdbf7309613d7260` once at22:55:54 UTC;
compilation finished22:58:02. ARM64 executable SHA256:
`efdb2f9a3d2404dcf5a99b0419b3f0e68ea4b652b3d03271016d4e6bd2272ec7`.
The clean qualification checkout and binary remained unchanged. PR7 was pushed
at this coordinated checkpoint. Evidence is in
`bin/mac-gesture-20261022/recorded-owner-admission/`; do not repeat its
`build.ps1`, `check.ps1`, or recorded attempts.

All ten selected native groups and all seven actual-app journeys passed. This
qualifies the previously source-only busy-admission, recording target navigation,
automatic Activity time/value edits, failure rollback and appearance-state
assertions within their test scope. Exact Undo/Redo, unrelated project data,
opaque state, save/reopen, focus and playback continuity assertions were retained.
The explicit silent WASAPI audit passed:155 callbacks, zero overruns/starvation/
device faults and zero host C++ allocations/frees in its render-service scope.
That short empty-render audit does not qualify the full composing workload.

Fresh isolated Computer Use used owned PID29336, silent output and a disposable
`qa-composing.screamseq`. It exited normally with unchanged binary. Observed:

- Constant Activity trace and native focus border are visible; Close works.
- Precise timeline double-click inserted a fifth hit; canvas Backspace restored
  four hits. No Write note action was used.
- Velocity typing updated the selected handle automatically. Native Ctrl+S
  retained its visible caret; subsequent text Backspace changed83 to8 without
  deleting a hit. Tab reached Save now; Shift+Tab returned to velocity.
- Two read-only API snapshots retained active playback, advanced frames from
  12376992 to16537248 and preserved the exact automation model. Both native Save
  operations used the disposable destination.

Foreground evidence is deliberately **partial**. Floating Activity selection
was not successfully changed: targeting its dropdown through Main reactivated
Main/cancelled the popup, and a returned floating UIA element was unavailable
in the helper cache. This is not classified as an application focus defect.
Docked precise controls remained absent from the helper tree; visible caret and
native assertions provide bounded focus evidence. Fresh recording selector/Keep
navigation, recorded-point drag, resize/dock/float and a visible playhead crossing
were not completed in this session. Earlier evidence may be reused only where
its inputs are unchanged. No Narrator or temporal-flicker pass is claimed.

The full silent session rendered350.5541666 seconds at48kHz/480 frames with
1056-frame buffer:32 deadline overruns, zero starvation/device errors/processor
faults, max callback15.6007ms and max service15.6127ms. The new joined-only
deadline ring retained all32 failures with no omissions. Each failure exceeded
both its render budget and service period, with the callback as its largest
measured phase. Pre/post device-service work was small in these samples. This
narrows investigation to callback execution or preemption during it; elapsed
durations are not CPU stacks and do not establish a DSP cause. Report SHA256:
`1b15d4b96db59a904969b69e5ace0e2f3d0916e6c0b7f35c8a0fd39ed537ed98`.
`foreground/deadline-analysis.json` validates frame/clock/phase identities.
Realtime audit and sustained-presentation qualification remain false.

The compiler also exposed three invalid UTF-8 punctuation bytes introduced in
the Activity legend. They caused visibly concatenated ranges in3408. The source
correction changes only those bytes to valid UTF-8 en dashes/middle dot; strict
UTF-8 decoding of all15 changed textual inputs passed. The correction is unbuilt
and cannot inherit successful visual rendering. No new test or extra build is
needed before the next coordinated checkpoint. Preserve all failed/limited
receipts, including `known-source-limit.json` and `foreground-check.json`.
**No production package or main merge is qualified by this checkpoint.**

Prior20467 Intel CI is now terminal success:134 CTests, full native interface,
socket/typed gates and BlackHole512 live take lifecycle all passed. Log SHA256:
`c827fb15927521b22abe664a585c6ec59e4eed49b002619780c6739141283e88`.
Both prior Macs passed; the prior Windows missing-device gate remains failed.
Current3408 CI runs40036686704/40036686763 were active at23:07:54;
ARM compilation passed, Intel and Windows were compiling. Do not restart them.
The next build is **not before23:56:23 UTC**, subject to rechecking later starts
and waiting for all preceding jobs to finish. Next steps: collect current CI,
compile the legend correction at that gate with other coherent source work,
reuse unchanged passing tests, complete the outstanding foreground journeys,
and investigate the dense callback using the retained aligned failure evidence.

### Activity parameter search continuation (source only)

Current Mac `ParameterActivity.swift` has a search field before its parameter
selector. Windows Activity lacked it, leaving long plugin parameter catalogues
laborious to navigate (reference27, live screenshot27/F02; recorded points are
reference28). Windows now has a native search edit and Ctrl+F shortcut, with
name/unit/stable-ID matching. Its field sits above the parameter selector in
the existing header area; the trace and recording controls retain their space.
Native Tab order is processor, search, parameter, then the existing action groups.

Filtering changes presentation only: it does not start a watch, save a point,
alter history or change the observed parameter. An excluded current parameter
remains listed as **observed; outside search**. Duplicate names retain distinct
stable IDs. Explicit selection resolves the filtered ID and requires that exact
parameter still exist, rather than silently falling back to another catalogue
row. Typing during a pumped observation retains the raw query/caret and defers
selector rebuilding until the read completes. A newer query cancels obsolete
queued target navigation; invalid recorded fields remain intact. This does not
change shared musical operations, persistence, API edits or audio processing.

`NativeTextSearch.hpp` extracts the existing docked plugin/program search's
ordinal case-insensitive matching for reuse by Activity; dock behavior is
unchanged. It is Windows text presentation logic, so it stays native. The
existing `parameter-activity-window-tests` group gains stable/non-contiguous ID,
duplicate name, reorder, case/unit/ID, zero-match, clear, busy owner, pumped read,
invalid-draft preservation, native names, caret and Ctrl+F/Tab assertions. The
existing Activity actual-app journey now searches Gain, a missing query and dB
before explicit selection, then retains exact automatic edit/history/reopen,
opaque state and playback assertions. No extra registration or app process is
added to the suite.

Source review, Python syntax and UTF-8 decoding passed; C++ and app checks are
unbuilt/unexecuted. The next bounded local selection is the Activity and existing
workspace plugin-search native groups plus the Activity actual-app journey.
Reuse3408's other unchanged composing, source-navigation, Save, routing and
hardware-audit evidence; do not rerun all ten groups/seven app cases merely for
this filter. Current CI must finish and23:56:23UTC/later-start limits still apply.
Fresh Computer Use must check normal/compact header layout, search typing and
text Backspace, Ctrl+F, Tab/Shift+Tab, explicit selection and retained observation
while playback runs. Floating helper limitations remain explicit; native tests
alone do not establish visible keyboard continuity. Pending recording foreground
checks and dense audio investigation remain required before release/merge.

### Recording review page navigation continuation (source only)

Source inspection found the recording window's page buttons still lost clicks
during pumped reads, unlike its recently corrected target selector. Direct
Next/Previous also assigned the requested offset before a read; failure could
leave old inputs labelled by a new offset and enable the wrong page direction.
The existing guarded navigation record now includes its bounded page offset.
Clicks during summary/event reads or another owner's busy operation retain the
latest destination without reentry. Explicit page navigation refers to the
currently displayed stable target; a subsequent target choice supersedes it.
Take/document/revision changes, failed reads and Close cancel the queued read.
Failed direct navigation restores the previous offset/total along with the
previous target, selected original input and details/caret. Next on the final
page stays there. Ctrl+Page Up/Down and accessible button names expose the native
page controls from the keyboard. Keep/Discard/input delivery semantics are
unchanged; no musical write is queued by paging.

The existing workspace-plugin-search fixture now checks both summary and event
read failures, exact old page/details/selection/caret, busy deferral, original
timestamps on page2, final-page bounds, forward/reverse keyboard paging,
message-pumped Previous, rapid bounded Next/Previous, stale revision and Close.
All navigation must preserve exact song state and write count. The existing
full F04 composing/recording app journey now uses144 original back-and-forth
Balance slider inputs, crossing the128-row review page boundary. It compares
both native review pages with the complete API archive, including original
timestamps/values, before the existing strict Keep/Undo/Redo, post-Keep editing,
Discard and exact unrelated-project save/reopen checks. The occupied Gain
refusal remains mandatory. Queue limits and assertions are not relaxed.

This joins the same unexecuted `activity-search` checkpoint, now **two native
groups and two app journeys** (Activity and complete composing/recording).
Python syntax and source whitespace checks passed; no compilation or runtime
test has yet run. Fresh foreground review must include next/previous pages and
Ctrl+Page Up/Down alongside search/retained focus and outstanding recording
navigation. The23:56:23UTC lower bound, terminal preceding CI and later-start
checks still apply. Source changes do not inherit3408's functional pass.

### Activity navigation after automatic editing (source only)

Inspection found an additional composing interruption: selecting another
recorded point, parameter, processor, page or New immediately after typing
refused navigation until the automatic-edit debounce had elapsed. Idle
navigation now finishes only the already queued edit before navigating. The
destination is captured before that write: parameter and processor identities
remain stable, and a clicked point is resolved by its original frame after a
time edit sorts the page. Clicking the edited point itself follows its accepted
new frame. This reuses the guarded shared recorded-edit operation and history;
there is no new musical operation or serialization format.

Invalid, stale, new-point and previously uncertain drafts still require explicit
resolution. A busy worker retains its unsent edit and current selection; a later
idle selection can finish it. New input pumped during the original write is not
silently rebased onto its resulting revision. A failed post-write read cannot
cause another selection or project Save to replay that write. These restrictions
protect music rather than reinstate an Apply step for ordinary valid input.

The existing Activity native group adds exact three-point reorder/selection,
same-point identity, invalid text/caret, busy admission, uncertain completion,
New versus explicit Add, parameter selection and newer-input assertions. It
retains all previous failure, draft, live observation and search assertions.
The continuation also retains point-row clicks received inside an observation
read. The existing navigation timer captures the document/revision, field
generation and point frame. While that navigation is queued, the debounce timer
leaves its original edit unsent; the guarded navigation drain removes its own
intent before flushing the edit. This prevents timer ordering from invalidating
the queued destination with the edit's own revision. The same ordering applies
to queued parameter/processor changes. New typing invalidates old navigation,
but remains eligible for its own original-target automatic edit. Close and
explicit subsequent navigation still cancel obsolete destinations.

Native assertions exercise both timer orders, busy admission, exact moved-point
selection, parameter selection, newer-input cancellation and completion of the
newer automatic edit. The existing actual-app Activity journey now leaves the
point immediately after typing its new time, checks the destination's exact
fields, then returns to the moved point before the unchanged Undo/Redo and
save/reopen checks. The two-group/two-app checkpoint scope is unchanged, with
fresh foreground selection immediately after typing added to the UI checklist.
Python syntax, UTF-8 decoding and whitespace checks passed. No new build or
runtime test has run.
