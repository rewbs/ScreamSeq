# P4 implementation checkpoint

This records prepared source, not phase completion. P0–P3 qualification and the
remaining P4 asset/curve/manual-recording work stay open. Both native platforms
remain on the same temporary implementation branch and must converge on main.

## Recorder duration choice — prepared, unqualified

Parity matrix row 44 identified a fixed 60-second Windows recorder form. Current
source confirms that `SampleRecordingWindow::begin` hard-coded 60 while
`SampleRecordingOperations` already accepts `maxSeconds` through 300, and
`SampleCapture::Buffer::prepare` caps storage at 16,777,216 frames. Mac
`SampleRecording.swift` offers 60 and 300 seconds. No new capture service or
storage policy is needed for this UI gap.

The native Windows form now exposes a labelled New take limit combo with the
same 60/300-second choices and a 60-second default. It submits the selected value
to the existing revision-guarded `sample.recording.start`. Duration is capture
configuration, like endpoint/channel choice, rather than a musical edit or a
song-scoped raw name/output draft. It survives hide/reopen, Refresh and explicit
name/output setup discard for that retained owner; it is not newly persisted as
a user preference or project field. The existing API remains the automation path.

Start snapshots the choice before entering the pending worker boundary. Native
and programmatic selection changes are refused/restored while pending, while a
take is retained, or while a Keep/lifecycle outcome needs review. Invalid combo
indices preserve the last valid choice. The retained lifecycle request includes
the exact submitted duration; Review never repeats Start. An API-created take
does not overwrite the future recording choice. The take description continues
to show its actual `maxSeconds` returned by the capture service, which may be
shorter at high sample rates. Help text makes that limitation explicit. No frame
cap, allocation budget, device default, audio callback or sample payload changes.

The compact form puts the duration beside input channels, places permission
status below them, and reserves separate rows for take details, Review/Accept,
the meter and help. This removes the previous Review/meter overlap. Its floating
minimum is expressed as a 580×560-DIP client area so nonclient DPI metrics cannot
hide the final controls. Five input/destination fields have explicit native
accessible names. The background and meter use the existing Windows contrast
palette; clipping remains described numerically as well as by color.

Prepared checks extend `DraftImportRetentionTests.inc` and
`RecordingLifecycleReviewTests.inc` in `native-tool-window-tests`: default and
300-second request, invalid selection, global-configuration versus song-draft
classification, retained hide/reopen choice, attempted changes during Start,
retained-take lockout, unknown Start parameters and read-only Review. A fake
capture response reports 16,777,216 frames at 96 kHz and verifies that the form
displays the effective 174.76-second duration while retaining the requested 300.
These checks exercise the native form and request contract; they do not claim
300 seconds of physical microphone capture.

## Bounded qualification

Batch this with the pending consolidated Windows application/native UI build.
Run `native-tool-window-tests`, existing session sample-recording/capacity checks
and the affected actual-app recorder/departure scenarios against that same
freeze. Check actual window geometry, native keyboard combo operation and
accessible names at minimum size and 100/150/200% DPI; confirm every recovery
action, status and footer remains visible. Reuse unchanged capture-buffer and
Mac evidence only with matching relevant source/toolchain/fixture inputs. Mac
needs no new build solely for this Windows form change; shared changes elsewhere
in the consolidated batch still require their Mac gates.

Physical device permission, disconnect/reconnect, automatic limit stop, high-rate
memory behavior, retained Keep/Discard and reciprocal save/reopen remain P4/P7
qualification requirements. There was no microphone access, build or test run
for this source change. The native capture engine's existing strict limits and
the original test assertions are unchanged.
