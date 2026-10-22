# Plugin recording: unlocked foreground check and corrections

## Observed candidate

Source `352ee334b18bbe7bc7c52105946ae7e00867853d`, Windows ARM64 executable
SHA-256 `790029382d5a046d49abc8018c06a372a2cb604b0a9df79fd1ad0911c6001162`.
The existing 07:57 UTC build was reused; no rebuild was run for this check.
Following the user's unlock reply, Computer Use operated isolated PID 40692,
main HWND 30347282, using a copied F04 complete-reference fixture, silent output,
automation and a private recovery directory. The process exited normally after
saving only the disposable copy; the original fixture hash remained unchanged.
Never reuse those process/window handles.

Evidence is in `bin/mac-gesture-20261022/foreground/` (launch provenance, saved
disposable project, audio report), `foreground-focus.json` and
`foreground-focus-return.json`. The earlier `foreground-check.json` is the
historical locked-desktop receipt and is preserved. The later
`foreground-observed.json` records this check separately.

Observed through Computer Use:

- Plugin search accepted `gain` during silent playback. Backspace produced
  `gai`. Tab/Shift+Tab returned to the same field. The read-only native probe
  verified exact HWND focus, global UIA focus, Value/Text patterns and selection
  before typing and after returning. Computer Use's own focused-element summary
  still reported the main window, so that summary alone is not an app defect.
- Params opened a five-control Gainer page, with switches and native sliders.
  Dragging Gain from 0 to -13.656 applied on release and enabled Undo.
- Windows > Plugin automation take opened the retained recording window.
  Record continued playback. A Gain gesture was retained as two delivery
  results with the existing-envelope conflict diagnostic; Keep stayed disabled.
  Stop retained that evidence, and explicit Discard cleared the take.
- **Failed:** with Params hidden before Discard, another Record was refused with
  “Finish the retained parameter controls before recording”. Reopening Params
  allowed its timer to restore the accepted -13.656 baseline; Record then worked.
  This is a hidden-window refresh defect, not unreleased delivery ownership.
- **Failed ergonomics:** the recorded slider text displayed
  `-3.671999999999997` while its original input was retained.
- After the reopening workaround, recording the rack Enabled switch captured
  one input, value 0, displayed at 533.5709 seconds with zero delivery loss.
  Stop and Keep completed through native buttons. This is bounded visible flow
  evidence; automated tests separately cover exact history and reopen.
- Floating windows were exposed only as related screenshots. Coordinate input
  worked; no floating-field typing continuity or full accessibility claim is made.

The 663.4904827-second interactive report contains 18,172 drawn frames,
60,565 audio callbacks at 48 kHz / 480 frames, 79 deadline overruns, maximum
callback 14,407.6 microseconds, no starvation/device errors or processor fault.
`realtimeAuditPassed` and `sustainedPresentationQualified` are both false.
This mixed interactive run **does not qualify realtime performance or temporal
flicker**. Do not convert point-in-time screenshots into a flicker pass.

## Source corrections, not yet built

`windows/App/PluginParametersWindow.hpp` schedules a guarded post-take baseline
read even when the clean parameter window is hidden. Closing between completion
and readback retains that one refresh. It does not reopen the window or redirect
focus; partial text, pending/uncertain completion and reload failures keep their
existing protection. Normal hidden windows do not poll indefinitely.

Generated recording-control text uses the host float scalar's concise formatting
when within bounds. Original double-valued input and timestamp are submitted
before formatting; musician-entered text is never rewritten. Tests in
`windows/Tests/Workspace/ParameterControlDeliveryTests.inc` cover hidden-before-
completion, hidden-after-completion, focus, partial text, unchanged revision and
exact original gesture retention. Existing assertions remain intact.

Mac ARM job [119708483967](https://github.com/rewbs/ScreamSeq/actions/runs/39744191656/job/119708483967)
passed compilation, 134 native registrations, recovery/picker, interface and
conformance steps, but the new `--ui` provider step failed before its assertions:
“VST3 is missing or outside the trusted plugin locations:
bin/mac-ci/test-plugins/ResonanceFixture.vst3”. `FixtureTrust.hpp` passes the
argument to `PluginTrust::canonical`, which requires an absolute path. The
workflow now resolves its build directory before invoking the existing test.
Trust rules and assertions are unchanged; provider behavior remains unqualified.

Mac Intel job 119708484219 also completed with the same fixture-path failure;
its 134 native registrations, recovery/picker, interface and conformance steps
passed. Windows job 119708483477 passed compilation, 158 portable/worker and
49 native UI/preferences/recovery registrations, vendor ownership, scratch and
conformance. Its 91-case application run retained 14 failure records and two
skips: the runner exposes no audio devices, so transport/recording/audition
cases could not obtain the required endpoint/clock. Assertions remain intact;
these are failed checks, not a Windows application-suite pass. Verified artifact
ZIPs and extracted evidence are retained under `bin/mac-gesture-20261022/ci/`.

An additional same-binary, 30.2035-second diagnostic used a never-switched
private desktop and another disposable F04 copy (PID 25360, exited normally).
Its measured ten-second interval drew 38 occluded frames within the existing
44-frame bound while advancing 1,000 audio callbacks. The full report contained
3,018 callbacks, one deadline overrun and no device/starvation/processor fault.
This establishes bounded occlusion pacing only. It neither explains the
interactive overruns nor qualifies realtime performance or visible flicker.
The original fixture and executable hashes remained unchanged.

The next correction also classifies pure native-control validation failures
as `notCommitted` before intake. This covers parameter range, original-time
and envelope-conflict refusal. The typed classification deliberately ends
before append or audible preview; uncertainty after acceptance is preserved.
The real-provider worker fixture now requires typed refusal for invalid range
and pre-take timestamps, then retains its existing successful capture, exact
event count, history, recovery and reopen assertions. This source is unbuilt.

## Correction foreground check, 22 October 09:09–09:21 UTC

After the user's unlock reply, Computer Use resumed on the already-built
`d699a71338147cd1b0ac5596b7b814aa1abd52f3` candidate, PID 17072 / main HWND
12387380. No build was repeated. The earlier access-denied receipt remains
unchanged; `correction/foreground-observed.json` records this separate result.

Observed passes: the Plugins search retained typed text, Backspace edited it,
and Tab/Shift+Tab moved away and back. Two read-only native probes independently
confirmed the exact field HWND, global/native keyboard focus, and text/value/
selection support (`foreground-focus.json`, `foreground-focus-return.json`).
Params opened the five Gainer controls with sliders and could be hidden.

**Recording qualification failed.** Record reported “Playback timing is not
available yet; retry after the first timed buffer.” Read-only snapshots show
playing/audioActive true, advancing rendered frames, device recordingClock valid
and no retained take. Stop/Play really created a new playback epoch (1 to 3) and
device generation (1 to 2), but the UI kept displaying the earlier refusal.
This does not establish that the replacement timeline also failed, or prove
three new worker submissions: after an unclassified
failure the recording window retains its unresolved completion and disables
Record. Refresh confirmed an empty take but correctly did not infer a completed
write from empty readback. Main-window Close consequently reopened take review
and reported that Keep or Discard was required, although neither was enabled.

Source review identifies two separate clocks: the public `recordingClock.valid`
was the device correlation, whereas arming checks the shared
`RecordingFrameTimeline::generation`. The latter can terminally reject a host
boundary, musical frame regression or rate change. This binary does not expose
which condition occurred. No clock tolerance or assertion has been relaxed,
and the cause of this live timing failure is **not yet established**.

The owned process exited normally at its 900-second limit. The original F04
hash is unchanged. Its report records 48 kHz / 480-frame periods, silent output,
45,520 callbacks in the final stream, 55 deadline overruns, one starvation
indicator, no device/processor errors, and `sustainedPresentationQualified:false`.
The counters reset on transport reopen; they are not whole-session totals.
This is neither a realtime-performance pass nor a temporal-flicker pass.
Hidden Params after Keep/Discard and the following Record remain unqualified
on this candidate because recording could not start. No production package.

### Follow-up source, unbuilt

- `windows/Session/ParameterRecordingOperations.inc` classifies stopped or
  unavailable-clock start rejection as `notCommitted` before take/provider
  ownership. The preceding manual flush/revision guard is preserved; failures
  after acceptance are not relabeled. Terminal failure gets an explicit restart
  message rather than an endless first-buffer retry.
- `editor/RecordingFrameClock.hpp` retains one immutable failure diagnosis,
  published through a lock-free atomic flag. Publication/mapping rejection
  conditions are unchanged. No allocation, log or lock enters the callback.
- `windows/App/Main.cpp` exposes this diagnosis separately under
  `transport.get.recordingClock.frameTimeline`. This enables the next live check
  to distinguish device correlation from musical coverage without guessing.
- Existing shared clock, real-provider worker and native review fixtures now
  cover exact failure boundaries, immutable first failure, stopped/untimed/
  invalidated start refusal, unchanged history/baseline, and explicit retry,
  close protection and keyboard focus. These additions have not run yet.

Next coordinated checkpoint: **not before 10:05 UTC, 22 October**. Build the
affected app, workspace, controller, provider and recorded-automation targets
once; use the correction's seven-native/six-app selection (the new cases are in
those existing registrations). Recheck stopped Record/refusal/close first, then
silent F04 playback with immediate and delayed Record. On any timing refusal,
retain `frameTimeline` before restarting; investigate that measured boundary
before altering clock rules. Finish the original hidden Params/Keep/Discard
sequence only when recording works. Preserve earlier passing focus evidence,
and repeat affected focus interactions on the new executable. Mac shared clock
and native-provider gates remain required; d699 ARM's provider UI step has
failed, with its job log not yet downloadable at 09:23. Do not merge/package.

### Current Mac CI diagnosis, 22 October 09:31 UTC

d699 Mac ARM job `119734779069` is terminal failure. Compilation, 134 native
registrations, recovery/picker, sample library, full interface and typed fixture/
conformance steps passed. The added owned-editor step reached its UI path and
failed with `Cannot register process-local AU fixture`. Source confirms that
`main` registered the component for closed-editor lease checks, then
`auNotificationLifetime` tried to register the same identity again. Its new
fixture instance captures the editor-mode flag at construction, so another
component registration is unnecessary.

`mac/Tests/NativePluginTests.mm` now passes the already-registered descriptor to
the notification lifecycle case. AU registration still must succeed initially;
all notification, original-time, lease, close/reopen and provider-identity
assertions remain. This is a test setup correction, **not a provider pass**, and
is unbuilt. Re-run the affected native executable once with `--ui` on each Mac
architecture at the next coordinated checkpoint. No old CI run was restarted.
Intel and Windows d699 jobs were still running at this observation.

The obsolete `finish-navigation-validation` one-shot also fired with d1c8's
01:53 checkpoint. Its existing receipt records a failed build; it was not
repeated. The stale reminder is now paused, alongside the already-paused mixer
reminder. Active source and the 10:05 build gate remain current.

## 10:05 checkpoint and hidden review correction

Frozen source `3cefb9ab9ff0a892586c21442e5c8d78746fbd03` built once at
10:05:33–10:08:46 UTC. ARM64 executable SHA-256:
`c6a7d7bc4f9d1feb4cf242979dac9c14b5623ec2b57960886ceb53d96bcb3799`.
Receipts/logs are in `bin/mac-gesture-20261022/clock-followup/`.
Seven native registrations passed. Five of six app cases passed; the native
recording/Keep/history/Discard journey failed on `document.open` with a retained
native editor guard. Do not replace this failed receipt with a diagnostic pass.

One isolated rerun of that exact case passed. A second targeted diagnostic forced
the stopped second take into the review, hid it (stopping its poll timer), then
performed API Discard. This reproduced the guard deterministically.
`reopen-failure.json` shows worker `retained:false`, but hidden
`workspace.parameterRecording.take.retained:true`, with no pending or uncertain
completion. Thus this is stale native presentation, not permission to discard a
real draft. No sleeps, assertion relaxation or repeated broad suite were used.

Prepared source: `ParameterRecordingWindow::observeBoundary` receives confirmed
Start/Stop/Keep/Discard results from `Application::finishDocumentOperation`.
It updates cached departure state even while hidden, performs no worker read,
and leaves pending/uncertain local receipts untouched. Foreign documents and
older completions cannot clear a current take. Existing polling still refreshes
rows/controls. Native tests exercise these guards and unchanged focus; the actual
app test now forces the hidden state before Discard and asserts immediate reopen.
The resolved native-input warning also changes to a completion message after
Keep/Discard. These changes have not been built or executed.

### Diagnostic foreground evidence on 3cefb

Owned silent F04 PID 40384/main HWND 12845678 was launched with a disposable
project/recovery directory. This was explicitly diagnostic because the app gate
failed; neither the normal launcher nor package guard was weakened. Computer Use
observed stopped Record refusal with usable Record/Close, successful Record after
Play, and retained refused Gain input because F04 already modulates that target.
Discard cleared that take. The next take captured two Balance inputs at 22.12;
Params was hidden before Stop/Keep. Keep completed, and a subsequent Record
started without reopening Params, then Stop/Discard completed.

One related-screenshot Keep click did not activate the control. Fresh main-window
coordinates did, with the take cleared and song dirty. The related floating
element index was unavailable to the helper. Do not report these helper targeting
limitations as an application Keep/focus defect.

Main rack search accepted `balance`, Backspace produced `balanc`, and Tab then
Shift+Tab returned to search. Read-only `qualify_foreground_focus.ps1` receipts
`foreground/focus-search.json` and `focus-return.json` both passed exact global
and native focus, Value/Text patterns and selection for field HWND 6032864.
Floating-field typing continuity and clean performance were not requalified.
The clock observer recorded healthy generation 1, stopped generation 1, then
healthy generation 2; no terminal frame-timeline failure was observed during
this run. This is not proof the earlier timing failure is fixed. Snapshots do
not qualify temporal flicker, and the diagnostic workload had audio overruns.

Next build/CI checkpoint **not before 11:06 UTC**. Rebuild the affected app and
workspace target once with the next cohesive batch; rerun the changed recording
journey and native review registration, then the hidden API completion/foreground
focus paths. Reuse unchanged seven-native evidence by dependency. Keep current
Mac shared/provider CI gates and final reciprocal project checks required.

## Previous checkpoint instructions (superseded by the update above)

No new build or build-triggering PR push/merge before **09:00 UTC, 22 October**.
Batch these corrections with the already-prepared shared raw-rack adapter.
Reuse valid earlier evidence by source dependencies; rebuild affected native
targets once, run the bounded recording/native-focus cases, then repeat the
failed foreground sequence without the reopening workaround. Include both
successful Keep and Discard followed by Record, partial-text preservation,
typing/Tab/Backspace, and a separately bounded silent playback performance check.
The corrected Mac provider invocation must actually reach and pass AU/VST3
assertions. No production package or merge is justified by the current candidate.
