# Windows MIDI and recording — 2026-10-07

Windows now connects timestamped MIDI input, precise-note takes and recovery to a
retained native **MIDI & recording** window. This closes the missing recording
workflow identified in the original [recording plan](RECORDING_PLAN.md), but does
not complete Windows/Mac parity. The functional checkpoint passes **359/359
application tests** in 875.469 seconds with strict outer isolation, **15/15
focused cases** in 16.046 seconds, and **40/40 native tests** in 18.41 seconds.
Eight source-matched rendering views pass review at 192 DPI. The earlier failed
application runs and their diagnostic distinctions remain preserved below.
Three earlier native desktop-teardown failures remain preserved and their
underlying resource cause remains unattributed; see the evidence below.

## Implemented workflow

The main **MIDI… / Armed / REC ON / Take…** control and command-palette entries
open recording settings, toggle arm, finish or discard a retained take. The
modeless window has source selection and Rescan, an explicit disconnected source,
Arm, selected-sound navigation, adjacent note-column count, timing grid, input
adjustment, loss counters, Finish/Discard and an owner-data event review list.
Its minimum client is 720×570 DIPs. Draft fields, selection and native focus are
retained through polling, pending operations and rejected settings changes.
Review resolves stable pattern/track identities and opens the existing precise
note editor; unavailable targets remain visible instead of being reassigned.

Armed playback starts a take after successful Play. Armed stopped input enters
ordinary tracker notes at the independent edit cursor. Computer typing and Live
keys retain their existing step-entry/audition behavior; they are not silently
reinterpreted as timestamped recording. MIDI and typing retain independent held
identities while sharing an audition voice for the same target and note, so one
source's release does not cut the other source's held note.

Native Stop drains a bounded input watermark, closes held notes and attempts one
guarded recording commit. Any missing-time, exhausted-voice or overflow count,
or an explicit input-loss reason, keeps the stopped take for review. Explicit
Finish may commit its compatible captured events after that review. API
`transport.stop` stops capture without committing music. Failed/stale Finish
retains the take and never retries automatically on every frame. Ordinary
Save/Open/close require explicit Finish or Discard, including `document.open`
with `discard:true`; autosave remains available.

Input received during an explicit finish/discard/restore boundary is quarantined
so it cannot later become a step edit or enter a replacement document. A failed
MIDI step edit releases held MIDI audition voices before reporting the error.
Deferred Stop/Finish requests retain document and take identity across pumped
worker waits; they cannot act on a replacement take.

## Clock, device and worker ownership

`Audio/HostClock` advertises QPC converted to 10,000,000 ticks per second. Decimal
uint64 strings carry host timestamps without JSON-number precision loss. WASAPI
correlates device position/frequency with QPC, includes primed silence in its
submitted-frame count, and reports validity and stream generations. Startup
zero, inaccurate clock readings, starvation/regression and restarted streams do
not fabricate mappings. The hosted renderer advances the origin of every
4,096-frame slice. Retained clock history outlives renderer retirement; a take
binds its first valid mapping once and never retargets to a later stream.

WinMM discovery and connection run off the UI/audio threads. Sources use opaque
device-interface identities, revalidated against the opened handle. Failed
connection preserves the previous available input and unapplied preferences.
Driver callbacks publish only bounded short-message data, with no allocation,
UI work or lifecycle calls. Driver milliseconds are anchored to the advertised
QPC clock with reported uncertainty; this is not a claim of sample-accurate
hardware MIDI timestamps. Ordinary CC reaches existing graph-controller input;
only note on/off and all-notes/all-sound-off controls enter the take.

The callback queue tracks connection/transaction generations, timestamp errors,
overflow, late events and driver errors. Loss quarantines the pending batch and
releases held voices. An unpublished producer reservation at a Stop boundary
reports loss instead of waiting indefinitely or silently finishing a partial
take. Normal Stop retains a bounded final audible clock interval. If a held note
loses its pinned generation or valid history, conservative closure adds an
explicit missing-time diagnostic so automatic Finish cannot hide a truncated
performance. Empty/no-held startup does not manufacture that warning.

`DocumentController` owns all take data. UI summaries share existing immutable
music caches and omit full event arrays. Capture validates the whole batch before
applying it to a copied take. No capture/lifecycle operation allocates musical
Undo or changes the song revision. Realtime work remains confined to the prepared
render path and bounded callback queue.

## API, musical history and project compatibility

The six Mac-compatible methods are `recording.start`, `recording.get`,
`recording.capture`, `recording.stop`, `recording.commit` and
`recording.discard`. Writes require the current `expectedRevision`; operations
after Start require the opaque current take ID. Capture accepts at most 1,024
events per request and the take is bounded to 65,536 events. Full get exposes
events, base revision, compatibility, capture state and loss diagnostics.
Quantization is 0..65,536 row units; positive `latencyMS` places input earlier,
within -500..500 ms. Whole-request rejection and successful request-ID replay
remain intact. See [recording schema](Api/recording.schema.json),
[MIDI schema](Api/midi.schema.json) and [API contract](Api/README.md).

Windows-only `midi.devices.get`, `midi.settings.get` and `midi.settings.set` use
an independent `expectedMidiRevision`; session-local input settings do not enter
song history. Settings changed during a retained take apply to a future take,
not its captured columns/sound/timing rules. Private driver injection is available
only with the explicit automated inspection/audio qualification switch; it is
not an ordinary input API or a promised virtual MIDI port.

The extracted shared `prepareNoteRecordingCommit` is used by Windows and the Mac
bridge. It validates the complete candidate, merges equal pattern/track/time/kind
events with the latest value, and optionally replaces precise notes in touched
rows. Row replacement clears only note, instrument and ordinary volume fields,
retaining unrelated FX. Actual changed commit is one document Undo/Redo. Dry run
retains the take; successful no-op commit consumes it without a fake revision.

Autosave closes held notes in a copied take only. A changed duration updates the
serialized fingerprint while a separate live-state guard remains stable during
disk waits. An explicit Restore is a different boundary: pending MIDI is drained
and capture stopped without commit before current work is protected. Failure
keeps that stopped take reviewable. Imported/restored takes receive fresh IDs and
are stopped; incompatible provenance cannot be revived by Undo or matching
geometry. Unknown Windows recovery fields remain preserved. Commit/Discard removes
the retained wrapper so a later Save cannot resurrect it.

Container 6 / metadata 17 is unchanged. The optional bounded `recoveryTake.inputError`
field retains an input-loss explanation across recovery. Current Windows and Mac
source both read and preserve it, and Mac `recording.get` exposes it. Older Mac
builds that reject unknown recovery-take keys may reject a copy containing this
field; reciprocal current-source Mac reopen is still unqualified. Existing
project state, PCM, native metadata and opaque plugin state remain covered by the
recovery/controller preservation tests. Mac source tests were extended, but no
Mac executable was built or run here.

## Verified checkpoint qualification

Current ARM64 application: `bin/windows-ui-parity/Release/ScreamSeq.exe`,
candidate 5 SHA256
`DEAA2252608550EBD6B309A8426AF68862093465B7BD1DE05BC1C0F2F1FD98AF`.
Candidate 5 contains the two visual corrections below and its native MIDI-window
test passes. All 15 latest focused cases pass in 16.046 seconds with strict outer
isolation (`bin/windows-recording-final-focused3.log`). The final full run passes
359/359 in 875.469 seconds, with no failures or skips and strict outer exit 0:
`bin/windows-recording-final-rerun2-app-tests.log`, `-isolation.log` and
`-exe-sha256.txt`. Its recorded identity and the current executable hash both
match candidate 5 above. The final native suite passes 40/40 after the test-only
lifecycle change described below. Eight candidate-5 scenes pass original-resolution
visual review with zero native bounds/intersection findings and matching source
provenance. Earlier failed runs remain separate evidence, never relabeled as passes.

Checkpoint location: `bin/windows-checkpoints/recording-20261007/`; the package
manifest is the authority for packaging completion. This report qualifies the
functional and scoped visual checkpoint, not every remaining release/parity gate.

Pre-visual candidate 4 is retained separately, SHA256
`3D794ECB37ED88E6B1A4E1172B95520986C7132227838D2D7248B293508ED70F`;
`bin/windows-recording-build-4.log` records that build.

Verified focused evidence remains tied to its tested binary:

| Evidence | Result and provenance |
|---|---|
| Seven recording application cases | Candidate 2: 7/7, no failures/skips, 7.539 s; `bin/windows-recording-focused-1.log`, `-isolation.log`, `-sha256.txt`. App SHA256 `BB9F52C7565075453512562F438F455665B28FE36873FDCD663E04542982BC41`. |
| Three actual-HWND recording window cases | 3/3 in 2.431 s; `bin/windows-recording-ui-2.log` and `-isolation.log`. Earlier candidate-2 UI evidence, not substituted for final-candidate qualification. |
| Recording and two recovery controller modes | Candidate 2: 3/3 in 0.786 s, strict outer isolation passed; `bin/windows-recording-controller-candidate2.log` and `-isolation.log`. Controller SHA256 `B9D902CAE943C94A123363FB7F48F3B3B1F74B1468D9679FCE0877D275BE8545`. |
| Updated controller, including held-note clock loss | Candidate 3: 3/3 in 0.840 s, no failures/skips, strict outer isolation passed; `bin/windows-recording-controller-candidate3.log`, `-isolation.log`, `-sha256.txt`. Controller SHA256 `0749DBEBFBEBDD6100F80776FC1DBFBE674420382B6C9734E14B1C7324683799`. The candidate-4 app-only change does not change this worker binary. |
| Rejected MIDI step batch releases held voice | Candidate 4: 1/1 in 1.253 s; `bin/windows-recording-rejected-step-4b.log` and `-isolation.log`. |
| Initial full native CTest run | Candidate 4: 39/40 in 22.55 s; `bin/windows-recording-final-ctest.log`. `command-palette-tests` failed restoring its original test-thread desktop after UI assertions. Cause remains unattributed. |
| Instrumented full native CTest run | 39/40 in 19.48 s; `bin/windows-recording-final-ctest-diagnostic.log`. Command palette passes; `native-control-tests` fails at Restore thread desktop. This second historical teardown failure remains unattributed. |
| Third full native CTest run | Candidate 5: 39/40 in 18.83 s; `bin/windows-recording-final-ctest-candidate5.log`. Instrumented command-palette/native-control cases pass; `midi-recording-window-tests` fails restoring its desktop after the bounds checks. This historical failure is retained separately from the passing replacement harness below. |
| Revised native GUI harnesses | Five affected cases pass in 2.63 s; `bin/windows-recording-native-harness-focused.log`. The test-only build succeeds in `bin/windows-recording-native-harness-build.log`. Negative checks verify a throwing body and a leaked owned HWND remain failures. |
| Final full native CTest suite | 40/40 pass in 18.41 s; `bin/windows-recording-native-harness-full.log`. The observer/GUI-worker boundary removes the post-GUI desktop-migration dependency without changing production; `bin/windows-recording-native-harness-production-sha256.txt` records the unchanged application identity. |
| Final candidate-5 visual review | Eight reviewed images, no visual/geometry findings; `bin/ui-capture/evidence-midi-candidate5-final/capture-evidence.json`. Exact production hash above, `sourceMatches=true`, `visualReview.finalCandidate=true`, no open issues. Native MIDI-window test also passes: `bin/windows-recording-window-test-5.log`. |
| Earlier candidate-5 focused cases | 15/15, no failures/skips, 15.409 s; `bin/windows-recording-final-focused2.log`, `-isolation.log`, `-sha256.txt`. Strict outer isolation passed. The original 14/15 run and reproduced fixture race are retained below. |
| Initial candidate-5 full application suite | 359 test methods in 900.952 s: 354 pass, five fail; unittest reports 15 failure records because one method has 11 failed subtests. `bin/windows-recording-final-app-tests.log` and its isolation log are retained. Outer exit 1 is from inner failures, not an isolation-guard failure. |
| Affected workspace/recording cases after clock-comparison correction | 39/39 in 24.973 s, strict outer isolation passed; `bin/windows-recording-workspace-clock-focused.log`, `-isolation.log`, `-sha256.txt`. The test helper validates the decimal host clock and normalizes only that value; all other workspace/MIDI state remains compared. Production hash is unchanged. |
| Candidate-5 full application rerun | 359 test methods in 879.803 s: 357 pass, one fails and one errors; `bin/windows-recording-final-rerun-app-tests.log`, `-isolation.log`, `-exe-sha256.txt`. The rejected-MIDI-step case fails its premature initial held-voice assertion; the recording API lifecycle case errors when explicit Stop is rejected as busy. The latter has a source-supported deferred-Stop explanation but did not reproduce in 20 diagnostic attempts. Outer exit 1 is from these inner cases, with no isolation-guard failure. |
| Latest candidate-5 focused cases after both completion waits | 15/15, no failures/skips, 16.046 s; `bin/windows-recording-final-focused3.log`, `-isolation.log`, `-sha256.txt`. Strict outer isolation passed on the unchanged candidate-5 application above. |
| Final full application run after both completion waits | 359/359, no failures/errors/skips, 875.469 s; `bin/windows-recording-final-rerun2-app-tests.log`, `-isolation.log`, `-exe-sha256.txt`. Strict outer exit 0; recorded and current executable identities match candidate 5. |

The controller cases cover atomic invalid batches and revision guards,
start-before-Play, actual timed renderer mapping, copied live recovery,
same-revision fingerprints, stale takes, exact optional loss metadata, malformed
restore rejection, lost clock without a later MIDI event, final stopped bounds,
imported unknown fields, merge/replace semantics, one Undo/Redo and exact native
Save/reopen. Existing immutable recovery and manual VST3 overlay/protection cases
also pass. Native timing tests exercise three sample rates and callback partitions
17/128/4096/8193, retained history and generation rejection. The optional actual
Mac fixture is absent in the hosted run and is reported as skipped there.

A separate silent WASAPI fixture passes with 30 valid timed callbacks, three
unmapped startup notifications and three Stops, no device/fault/overrun/starvation
errors: `bin/windows-recording-native-wasapi-silence-1.log`. It measures a short
owned silent run, not acoustic accuracy or sustained glitch-free capacity.
Native callback/prepared-render C++ allocation/free probes report zero in their
measured scopes; direct malloc/free, locks and driver/vendor internals are outside
that evidence. No physical MIDI device or hotplug behavior is qualified by the
injected queue tests.

## Retained failures and corrections

The first app build failed a C++ JSON/string rewritten comparison overload in
`RecordingIntegration.inc`; `bin/windows-recording-build-1.log` is preserved.
The comparison was made explicit before the successful candidate-2 build.

Initial native fixtures failed `large callback slices have distinct presentation
times` and `bounded simultaneous producers fit capacity`. The former now allows
one 100 ns conversion-rounding unit; the latter verifies explicit panic/loss on
bounded CAS contention instead of incorrectly requiring zero contention loss.
The original `windows-recording-native-hosted-1.log` and
`windows-recording-native-device-1.log` are retained beside subsequent passes.
Unpublished Stop-boundary reservations then received a production loss-reporting
fix and a dedicated native regression.

The first combined UI/voice run passed two of four cases. Its two failures were
focus assumptions in the actual-HWND fixtures: clicking Rescan legitimately
focuses its button, and Enter dispatched to a list without focusing it still
activates the focused Refresh button. The tests now use local F5 from the latency
edit to verify retained caret/focus, and explicitly focus the event list before
Enter. `bin/windows-recording-ui-voice-1.log` retains the original failures.
All three corrected UI cases pass in `windows-recording-ui-2.log`; native
MIDI/typing shared-release coverage passed in the original run.

The first candidate-5 final focused run passed 14/15 in 23.323 seconds:
`bin/windows-recording-final-focused.log`. Its outer exit 1 came from the failed
inner case, not an isolation-guard failure. The take correctly captured and
stopped four events, but the fixture sent `WM_COMMAND` to Refresh review while
the initial source rescan had temporarily disabled that button. The action was
correctly ignored. `bin/windows-midi-review-race-3.log` reproduces the disabled
control with `eventCount=4` and `reviewedCount=0`; race logs 1/2/3 are retained.
The fixture now waits for no pending operation and the real button to be enabled
before one command. The unchanged four-event assertion passes in
`bin/windows-midi-review-ready-1.log` (1.326 seconds), followed by all 15 cases in
`windows-recording-final-focused2.log`. No production change or rebuild was made
for this correction.

The first full application run failed four whole-workspace equality cases in
`test_workspace` and the invalid-request atomicity case in `test_workspace_layouts`
(11 subtests). The new `midi.hostTime` QPC value advances between reads. The
test-only `stable_workspace` helper validates a decimal timestamp and normalizes
only that value, retaining every other MIDI setting, revision, loss counter and
workspace field in equality checks. All 39 affected cases then pass in 24.973
seconds with strict outer isolation. The complete full-suite rerun then passed
357 methods, with one failure and one error, in 879.803 seconds.
Production remains candidate 5 with the exact hash above. The original
`windows-recording-final-app-tests.log` preserves all 15 failure records and the
359-method result; it must not be relabeled as passing after any correction.

The second full run, `bin/windows-recording-final-rerun-app-tests.log`, fails
`test_rejected_midi_step_batch_releases_previously_held_voice` at its initial
`voicePositions` assertion (`test_audition.py:493`), before the rejected-batch
release assertion. It errors in
`test_api_take_revision_batch_validation_stale_retention_and_save_open_guards`
at explicit `recording.stop` (`test_recording.py:120`): “Document worker is busy;
no mutation was queued.” The outer wrapper exits 1 from those inner cases without
an isolation-guard failure. Both original full-run logs remain preserved.

Twenty diagnostic executions of the unchanged original recording case all pass;
`bin/windows-recording-stop-race-{1..20}.log` and their isolation logs retain that
nonreproduction. The wrapper records original call ordering and responses without
retrying writes, and would collect four bounded read-only observations on the
original Stop error. Source shows that `document.patch` stops transport while the
document worker is busy, scheduling capture Stop for the UI service. An immediate
explicit API Stop can therefore meet that later worker operation. This is a
source-supported explanation, not a reproduced interleaving. The test now waits
on a coherent cached `workspace.get` snapshot for the same take to be stopped
and the worker idle, asserting exact document/base revisions and incompatibility.
It then issues explicit Stop once and asserts unchanged revision and full take
data apart from host time. The original test is retained at
`bin/diagnostics/test_recording_before_stop_wait.py`. This is a narrow fixture
completion barrier, not a generic write retry or production change. It passes
in the latest 15-case focused suite and the final 359-case full run. The original
busy interleaving still was not runtime-reproduced; these passing corrections do
not change that diagnostic distinction.

The initial held-voice failure is reproduced without masking the original
assertion: `bin/windows-recording-midi-step-diagnostic-1.log` (one case, 3.265 s,
outer exit 1) and `bin/windows-recording-midi-step-diagnostic-summary.json`.
The failing fixed-delay observation has one callback and zero voices. The next
read-only observation has three callbacks and the expected sample-1 voice, which
remains healthy through two seconds of read-only observation, with zero input
loss and no reinjection. The narrow test correction waits for that actual voice
before sending the rejected batch, then requires the exact range error and zero
voices/positions over advancing callbacks, preserving fault/loss/generation
checks. This changes only the fixture. Both corrections pass the latest focused
suite, 15/15 in 16.046 seconds, followed by the full 359/359 in 875.469 seconds,
both with strict outer isolation on the unchanged candidate-5 application.

Source review found two genuine issues after candidate 2: a held note could lose
its clock without a subsequent MIDI event to increment loss, and a rejected step
batch could skip an earlier held note's release. Candidate 3 adds explicit worker
clock-loss diagnosis and distinguishes active expired history from stopped final
bounds. Candidate 4 adds cleanup on failed input-batch processing. Both corrections
have targeted regressions; earlier successful candidates are not relabeled as
containing them. `windows-recording-rejected-step-4.log` also passed (1.362 s);
the 4b fixture strengthens that success by requiring accepted injected input,
the actual note-range validation error and zero driver loss, so queue panic
cannot substitute for the intended failure path.

The candidate-4 native desktop-teardown failure is retained and not claimed fixed.
A test-only diagnostic extension records Win32 error, desktop, GUI and remaining
HWND state. Its direct diagnostic run passed; logs are
`windows-recording-palette-diagnostic-build.log` and `-run.log` in `bin/`.
The instrumented full suite then passed the command palette but failed a different
test, `native-control-tests`, at Restore thread desktop: 39/40 in 19.48 seconds,
`bin/windows-recording-final-ctest-diagnostic.log`. A third full run passed both
instrumented cases but failed `midi-recording-window-tests` at Restore MIDI test
desktop after bounds checks: 39/40 in 18.83 seconds,
`bin/windows-recording-final-ctest-candidate5.log`. All three teardown failures
remain preserved with their underlying resource cause unattributed. No production
fix is claimed.
The MIDI diagnostic's sixth direct run reproduces Win32 error 170 (resource in
use) after explicit owner/edit destruction checks passed. Both current-thread
top-level and message-only window inventories are empty, and GUI state handles
are null: `bin/windows-recording-midi-window-diagnostic-stress.log`. The observed
input desktop is `Screen-saver`; it was not changed by the test and is not an
established cause. No surviving IME window or hook is established by this evidence.
The test-only [PrivateGuiTest.hpp](Tests/PrivateGuiTest.hpp) now runs each GUI body
on a fresh worker attached to a private desktop. The observer never leaves its
original desktop. Fixture-owned windows must be destroyed before worker exit;
then the observer joins the worker, checks desktop closure, and checks its
original desktop, foreground and clipboard even when the body failed. Unknown
OS windows are not destroyed manually. Exact negative checks prove a throwing
body propagates and a marked owned-window leak fails before thread exit.
All five affected harnesses pass, followed by all 40 native tests. This removes
the demonstrated post-GUI thread-migration dependency; it does not identify the
hidden resource responsible for historical error 170. No production source or
application executable changed for this correction.
The prior recovery checkpoint had a different native-test teardown failure;
similar wording alone does not establish the same cause. No foreground
reactivation or musician-session replacement is part of qualification.

The first candidate-4 capture attempt rendered six scenes before its disposable
imported-take fixture correctly failed validation: release events incorrectly
specified instrument 1. The ignored fixture now uses instrument 0; the original
partial output and `bin/ui-capture/midi-capture4.log` are preserved. The refreshed
eight-scene set is `bin/ui-capture/evidence-midi-candidate4-final-2/`, with successful
scratch log `bin/ui-capture/midi-capture4b.log`. All eight images were inspected at
original resolution: native 720×570 and 900×720 client layouts are readable and
native bounds/intersection checks pass. Main-workspace rendering at 1057×719
revealed the new MIDI button overlapping the drawn document title/status; the
native heading also interpreted its ampersand as a mnemonic. Metadata explicitly
sets `visualReview.finalCandidate=false`. Source corrections move the drawn
header past the MIDI control and use a literal heading. All eight candidate-5
images were inspected at original resolution and pass, including the main
workspace and minimum recovered-event review. Final evidence is
`bin/ui-capture/evidence-midi-candidate5-final/capture-evidence.json` with
`sourceMatches=true`, 137 production Windows source hashes, exact production
executable identity, `visualReview.finalCandidate=true` and no open issues.
The candidate-4 set remains reviewed development evidence with its findings.

These are actual renderer/readback and same-process native-control compositions
at 192 DPI / 200% on a never-switched private desktop, with foreground and
clipboard isolation checks. They are not foreground screenshots, 100%/150%
qualification or sustained presentation evidence. No native CUA or public-desktop
capture is claimed. Reproduction helpers remain ignored under `bin/ui-capture/`.

## Remaining gates

The functional and scoped visual checkpoint is qualified as recorded above.
The three historical native migration failures and unknown underlying resource remain
in the evidence despite the passing replacement lifecycle. Physical MIDI
latency/jitter, real input hotplug/device reorder,
real sample/plugin instrument performance capture, reciprocal current-source Mac
project/take reopen, supported display scales, accessibility, sustained foreground
presentation, long loaded audio/loopback and broader realtime/vendor audits remain
separate gates. Independent dock groups and remaining editor workflow parity also
remain open. The broad Windows/Mac parity goal is active.
