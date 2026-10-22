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

## Next checkpoint

No new build or build-triggering PR push/merge before **09:00 UTC, 22 October**.
Batch these corrections with the already-prepared shared raw-rack adapter.
Reuse valid earlier evidence by source dependencies; rebuild affected native
targets once, run the bounded recording/native-focus cases, then repeat the
failed foreground sequence without the reopening workaround. Include both
successful Keep and Discard followed by Record, partial-text preservation,
typing/Tab/Backspace, and a separately bounded silent playback performance check.
The corrected Mac provider invocation must actually reach and pass AU/VST3
assertions. No production package or merge is justified by the current candidate.
