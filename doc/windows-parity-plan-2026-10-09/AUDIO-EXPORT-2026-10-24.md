# Full-song audio export parity

This is a chronological implementation/evidence record. For the current frozen
source and next gate, use [the continuation queue](CONTINUATION-QUEUE-2026-10-20.md)
and the latest named checkpoint's freeze/receipts. The initial source-only statements below
describe discovery, not the latest test results. The
[06:01 checkpoint](#24-october-0601-checkpoint-and-correction) retains both failed
qualification and diagnostic passes. Matrix rows41,42,58 now reflect those
boundaries without changing the original Mac reference provenance.

## Discovery boundary

Current Windows source has **Export module** (`EditingView.inc`,
`WorkspaceCommands.inc`, `WorkspaceMenuBar.inc`, `DocumentController.cpp`).
The old matrix's missing-module-export entries are historical, not current proof.
At discovery there was no Windows user-facing audio export command. Mac `main.swift` has
`exportAudio()`, using `TrackerSession` and `mac/Audio/AudioExport.mm` to write
48 kHz stereo float32 WAV. Neither host had `document.exportAudio` at discovery.
This is a remaining composing journey: record/Keep, Save, export audible song.

## Initial source continuation (historical; execution results below)

`editor/hosted/AudioExportStream.{hpp,cpp}` now owns the full-song private
renderer, plugin chain, recorded/musical automation, native column mutes,
latency trim, bounded tail and WAV stream encoding. Mac keeps its existing
temporary-file/fsync/rename wrapper and public signature. No project format,
stable identity, plugin recipe or Undo semantics change. Private renderer and
chain stay on the heap for small document-worker stacks.

The shared writer takes an immutable module, plugin states, recorded points,
optional native song and sequence, plus a fresh seekable output stream. It
returns stereo frames written. Default partition remains 512 frames; tests
can supply 17 or 4096. This is offline worker code, never the audio callback.
It rejects invalid partitions, nonzero stream position and nonfinite/negative
plugin latency/tail (latency capped at 60 seconds). Write failures stop promptly.
It retains the existing one-hour total render bound and 60-second tail cap.
Native file owners must discard incomplete staging output on every failure.

Callers must use `Document::snapshotData()` or the project wrapper's equivalent
module snapshot, preserving exact native sample and timing archives. Plain
module serialization is not an acceptable substitute.

New `audio-export-stream` is registered on both platforms and included in the
Windows worker target. Assertions cover exact independent dry core PCM and
duration, WAV headers, callback partitions, audible hosted/recorded processing,
native column mutes, precise sub-row notes against independent core PCM,
injected stream failures and unchanged source/history.
These assertions have **not executed**. Existing Mac `export` additionally
covers actual AU latency/tails, a 512 KiB worker stack and failed-destination
preservation; it remains required after extraction. Prior Mac passes do not
qualify this changed rendering source.

## Original implementation sequence (execution results below)

1. Windows native streaming staging writer (source now present in
   `windows/Project/AtomicOutputStream.hpp`, not built): hold an exclusively created handle,
   render without accumulating the WAV in memory, flush/close, atomically publish
   with explicit overwrite policy. Test failure and concurrent-destination
   preservation. `atomic-output-stream` asserts header seeking, Unicode paths,
   chunked writes, no-overwrite race rejection, explicit overwrite, render/stream
   failure preservation, locked-destination publication failure and removal of
   staging files. This test is unexecuted.
   Project byte-buffer saving is not suitable for a one-hour WAV.
2. `document.exportAudio` is now in both native dispatchers and Windows
   SessionAdapter, plus schema/guide/reviewed inventory (source only).
   Require current document revision and absolute `.wav`
   destination; preserve recovered source protection. Define accepted plugin
   snapshot and unkept-take admission explicitly: use accepted stored plugin
   recipes and full song snapshots, exclude temporary previews and uncaptured
   native-editor state, refuse retained MIDI/sample/plugin takes. Do not flush
   live vendors, cancel previews or use Mac `projectData()` as a read (it commits
   pending edits). Dry run validates snapshot/destination, never vendor startup.
   Export does not add history,
   change the song's Save destination or stop the existing transport. Windows
   `document-controller-audio-export` asserts WAV creation, strict invalid/stale
   rejection, overwrite, exact project/view/stop-count/Redo preservation. The
   Mac actual socket saving journey adds matching WAV/header/guard/no-change
   checks. Mac publication now accepts an optional explicit no-overwrite mode,
   using link or exclusive rename as its existing project-save implementation
   does, with a retained-destination regression. All native tests are unbuilt.
3. Windows File > Export audio… and command-palette entry now use a native
   chooser, asynchronous worker status, explicit failure message and retained
   originating focus. Mac now uses the same guarded contract, capturing revision
   before its chooser. Windows finishes queued precise/automation fields before
   choosing, retains caret on chooser return and treats export as non-navigation.
   Its retained command owner never retries an uncertain file write; readback
   reports file existence/size as observations, not proof of a successful export.
   Native `command-results` adds chooser cancel/stale and pending-note/caret/Save
   destination assertions. The real chooser and actual app path are untested.
4. Compose/record/Keep/Undo/Redo/Save/export/reopen journey: assert unrelated
   music and native project stay exact, epoch advances continuously, and compare
   exported audio using shared fixtures. Test invalid/stale/overwrite failures.
   The existing actual Windows composed F04/Balance recording journey now adds
   WAV export after Keep/reopen, strict no-overwrite refusal, audible PCM/header
   checks, exact native file/context preservation and an advancing unchanged
   playback epoch. It is source-only. Its addition does not alter the frozen
   retained-reveal checkout's older five app cases.

Both native controls and the API are now source-only and unqualified.
Cheap reviewed inventory tests passed (6 tests); this is not execution evidence
for either native export implementation. Fresh Mac `export`, shared
`audio-export-stream`, Windows `atomic-output-stream`, worker export and both
actual app transports are required. Preserve existing fixture PCM assertions.
The frozen
`retained-reveal` candidate 24788ebc34f35a94f24138d63a1df248cddff445 deliberately
excludes this continuation; its evidence must never be attributed to this code.

The native Mac export command now waits for the existing worker/read owner,
directs retained takes to review, refuses an unfinished manual slider gesture,
and collects already delivered vendor input before capturing the chooser's
revision. This preserves the old native command's input boundary without making
external API dry runs or invalid calls commit pending input. The chooser and
native command remain unqualified; Mac compilation, API no-mutation assertions
and native foreground review are required before claiming parity.

Next bounded Windows checkpoint: shared `audio-export-stream`, native
`atomic-output-stream`, worker `document-controller-audio-export` and existing
module-export guards, plus workspace command-result/menu/automatic-edit groups.
Run the composed recording journey with its new WAV stage and the layered VST3
journey where the deterministic fixture is available. Inspect File > Export audio
with a real native chooser, cancel/overwrite, queued precise edits and caret,
then check the resulting WAV and unchanged song destination/playback epoch.
Mac must run its existing AU latency/tail/small-stack export test, new shared
writer test, socket saving journey and both architecture builds at the same
coordinated checkpoint. Prior device evidence may be reused only for unchanged
callback inputs; the failed sustained foreground run remains a release blocker.

The final pre-build native continuation uses a saved/imported song's filename
stem for the export suggestion and supplies its folder as a chooser default,
while allowing Windows to retain the user's remembered export location. Unsaved
songs retain the Untitled fallback; arbitrary musical titles are not treated as
Windows filenames. Audio and module export share this native behavior.

Audio export result Review now names the correct operation instead of falling
through to the sample-operation status. The existing native command-result
group adds a real successful WAV write followed by an injected presentation
failure and an external replacement. Re-entering Export must review the retained
receipt exactly once, preserve the replacement file and song, and never reopen
the chooser or write again. This assertion is unbuilt. The real chooser's
suggested name and remembered folder are fresh foreground checks; no additional
build or test registration is introduced. Resolve the current
`audio-export/freeze.json`; earlier frozen manifests are retained separately.

Source review found module export did not finish automatic field edits before
capturing its revision, and its command fell through to navigation after the
chooser. Save and both exports now call one native automatic-field flush helper;
each still owns its existing take/vendor guards and the external APIs remain
read-only. Both export choosers use one document/revision-guarded focus/caret
restoration helper, including cancellation and chooser exceptions. Module export
returns without navigating. Existing command-result assertions add an injected
audio chooser failure and a module chooser that observes the latest precise
velocity before cancellation, retaining field focus/selection and exact Undo.
This is source-only, included in the same nine-group checkpoint; do not treat
the earlier module-export pass as evidence for the changed native command.

The existing mandatory native VST3 composing journey now also calls the
portable actual-API `editor/Tests/Conformance/audio_export.py` reader/validator.
After its qualified-source resampling derivatives are created, export the
processed recorded song plus both/sample/plugin/neither-layer variants. Require
valid finite float32 WAVs, bounded complete-sequence duration, at least64 changed
unsaturated samples for each independent layer comparison, exact document and
stopped editor context, and unchanged source-file hashes. The explicitly bypassed
effect derivatives are reused only for source isolation; the original processed
song is also exported. This is unexecuted test source, not vendor or hardware
qualification. It stays within the existing layered app case, so the checkpoint
still has three app journeys and one build. Each successful run retains its WAV
hashes and `full-song-export/report.json`.

The final WAV assertions also compare the pre-recording and recorded processed
song, requiring64 changed unsaturated samples so a VST3 export that ignores
recorded automation cannot pass on sample audibility alone. Each WAV read is
bounded before allocation to two minutes: F04 has three order occurrences and
the production writer permits up to60 seconds of hosted tail. This replaces
the unexecuted one-minute fixture ceiling, not a failed assertion; the bound
must include both complete music and valid tails. Native state and history
equality, independent layer assertions and every prior failed receipt remain
unchanged. The added baseline makes six WAV exports within the one existing
layered app journey.


## 24 October 06:01 checkpoint and correction

Observed candidate `dbb15bfa07d6964cc06e0ca91ed422c784f2983b` was pushed to PR7
and built once locally at06:01:26–06:05:21 UTC. ARM64 executable SHA256:
`11294b5330a6a918e418b0edff7f3f73461cff4de6f390048ca123b57a68bf94`.
All receipts remain in `bin/mac-gesture-20261022/audio-export/`. No production
package or main merge followed. The next checkpoint cannot start before
**2026-10-24 07:01:52 UTC**, extended by any later actual CI build start; all
preceding CI must be terminal. At06:17 both Mac architecture builds and Windows
CI remained in progress. Never replay the recorded local build or old CI jobs.

The original runner stopped at inventory admission without executing tests:
CTest correctly included the required `document-controller-scratch` fixture.
`qualified-checks.json` separately records the corrected inventory and actual
execution: seven of nine native groups passed, plus setup. Worker audio/module
exports, atomic publication, hosted-project/render-stage tests, menus,
automatic edits and WASAPI lifecycle passed. The two failures are retained:

- `workspace-command-result-tests`: the new module chooser/Undo fixture compared
  a wrapper containing revision and Redo availability. Undo correctly advances
  revision and creates Redo. Source correction instead compares **every serialized
  project byte**, explicitly requires changed revision and Redo availability,
  then verifies exact accepted bytes after Redo and unchanged native destination.
  The subsequent audio chooser/result-review case was not reached; it still
  requires execution.
- `audio-export-stream`: dry512 PCM exactly matched an independent Renderer512,
  but the exact cross-partition assertion failed. Existing CLI diagnostic on the
  same executable reports finite PCM, `partitionExact:false`, maximum delta
  `8.64267e-07` across rates44100/48000/96000 and blocks17/128/4096. That two-second
  diagnostic does not explain complete-song duration or prove the WAV writer
  correct. The **exact failing assertion remains mandatory**. Source now compares
  each full-song partition with its own independent core rendering, reports frame
  counts/first changed sample/max delta, and defers the existing failure until
  after subsequent mute/precise/recorded-DSP/failure/preservation checks. No
  tolerance was relaxed and no engine behavior changed. Investigate those fresh
  results before deciding whether the failure is writer behavior or an invalid
  assumption about stock-engine partition exactness.

A distinct `diagnostic-app.json` ran the three composing journeys against the
same frozen binary, retaining source and native VST3 provider hashes. The
interleaved precise-note/plugin-take history case passed. Both full-song export
journeys failed with the real pipe error "Request began but reply timed out;
outcome uncertain, inspect before retry". Source inspection found a five-second
**server dispatch** deadline, not a client readiness failure. The partial staging
file from the first terminated owned fixture remains as diagnostic evidence.
These failures are not relabeled as successful exports.

Source correction gives admitted `document.exportAudio` requests the existing
Mac socket's120-second total response budget, preserving five-second admission
cancellation and ordinary-operation deadlines. Active jobs are now retained so
native shutdown wakes the pipe owner before joining it; close during execution
still reports uncertain outcome. Existing `ApiTests` adds actual message-owner
and pipe coverage: export completion after5.25 seconds, exact request-ID replay
without another host call, ordinary-operation timeout, queued export cancellation
without later execution, and bounded close inside an active request. Its ordinary
30-second test ceiling is unchanged. Build `ApiTests` and run its registration
at the next checkpoint; this is new required coverage, not a substitute for the
actual composing exports. Guide/describe document the budget and no-retry rule.
Only full-song export requests use a120-second test client. The six-export
journey owns a900-second process, matching six bounded renders plus setup;
ordinary readiness and edit deadlines remain unchanged. No failed check was
skipped or asserted away. These corrections are **unbuilt**.

Fresh Computer Use used owned PID41068, now exited normally, with silent output
and explicit diagnostic clocks. It verified native File > Export audio,
filename suggestion, remembered folder, export completion, automatic velocity
editing, chooser cancellation returning the visible caret, text Backspace
(98→9 with all four hits retained), Tab to Save now and Shift+Tab back to velocity.
The WAV was27.019708 seconds, finite stereo48k float32, peak0.245405; SHA256
`7de090433c4948d53b3da0395309ddd9f5c821f42326043cb4820a532b698aac`.
A subsequent API snapshot had playing=true, epoch1, accepted velocity9 and no
retained draft. There was no paired pre-export epoch snapshot, so do not infer
that this UI run proves epoch preservation. UIA omitted the docked fields;
visible caret/value and existing native assertions provide the bounded evidence.
No overwrite, module chooser, resize/float, Narrator or temporal flicker claim.
See `foreground-diagnostic/ui-receipt.json` and `ui-observation.json`.

The same300.0508357-second diagnostic at48k/480frames retained25 deadline
overruns, zero starvation/device errors and no realtime-audit or presentation
pass. Stage analysis matched24 prepared-render events to outer callbacks with
no omitted or unmatched stage events; one outer overrun had no stage event.
All24 had their largest measured stage in engine rendering, which includes
native instruments and routing callbacks. This is QPC wall-time decomposition,
not CPU duration or individual plugin/stack attribution. Do not optimize a
particular processor from this alone. Report SHA256
`c6e2f3ed90ffc920cbf4f8ca31e9b71ad43cdeaf358846452f95ece28ed18f52` and checked
`foreground-diagnostic/analysis.json` preserve the evidence. Production remains
held pending the native failures, full composing exports, Mac gates, reciprocal
edited projects and sustained Windows performance qualification.


Correction source is frozen at `209f65464b9b978aa7b655fe602e2ea443b8b586`, backed
up on `codex/mac-gesture-next-checkpoint` and fast-forwarded into the clean
qualification checkout. `bin/mac-gesture-20261022/audio-export-followup/` contains
prepared `checkpoint.ps1`, `build.ps1`, `check.ps1` and `freeze.json`. None has
started a new build. They pin the source and07:01:52 gate, require a fresh
terminal preceding-CI receipt and select ten native groups plus scratch setup,
including newly built ApiTests, followed by three actual-app journeys. The
runner collects independent app diagnostics even after a native failure, but
its final aggregate still fails and no package/foreground qualification is
permitted from partial evidence. It never reruns a recorded attempt. The
previous `audio-export` build and reports remain intact. Documentation-only
commits after the freeze do not belong to its compiled source.

At06:29 UTC current Mac ARM job120796834728 completed: compilation succeeded,
134/135 CTests passed; the sole native failure was the same exact dry WAV
partition assertion. This reinforces that it is not Windows-only, without
settling the underlying engine-versus-writer contract. Its terminal log SHA256:
`730a289468656f1495a4562657766b25fbd20387b4b0c564713e119758b38185`.
`audio-export/completed-ci.json` pins steps and source. Intel and Windows were
still running; inspect their terminal evidence before the next checkpoint.
The read-only `collect-audio-export-ci.py` downloads only completed native jobs
and preserves already downloaded logs. No failed job was restarted.


The next source continuation adds a complete recorded-decay PCM acceptance
case inside the existing shared export test: built-in comb feedback changes at
frame1024, then export duration and every sample are compared with an
independently scheduled NativePlugin fed the dry core PCM. The fixture must
contain audible samples beyond its short initial tail; the final advertised
tail and a two-minute allocation bound remain exact requirements. This is
additional coverage, not a reproduced product bug: PluginScheduling and the
musical command/lane preparers reserve known parameter ranges before rendering,
so recorded feedback already contributes to the initial tail budget. The
suspected stale-tail writer change was removed after that source review.
No export/DSP product code changed in this continuation. Resolve the newest
`audio-export-followup/freeze.json`; its earlier unexecuted209 manifest is
retained separately. The exact failed dry partition assertion remains required.

At06:56 an analytic source check identified a possible explanation for the tiny
partition differences. `soundlib/MixerLoops.cpp` is byte-equivalent after newline
normalization to pinned upstream f83cedb0; the active mixer is integer. Its
rounded click-offset decay does not commute with summing separate voice offsets.
`Fastmix.cpp` decays ending voices through the remaining chunk, then merges their
residues into the shared offset for the next chunk. A two-offset model using
1280000 and987654, first chunks17/512, differs by at most110 integer mix units
(8.19564e-7 normalized). `audio-export/stock-offset-model.json` retains source
identity and results. This is **not an application render**, does not establish
the cause of the demo failure and does not replace or relax any assertion.
Inspect the next complete same-partition independent-core comparisons before
changing either the shared renderer or its compatibility expectations.

The new frozen candidate is6a9d3d4b7; later primary commits currently contain
documentation only and are excluded from that compiled source. Eleven native
groups include precise cursor retention. If the mandatory exact PCM group still
fails, `audio-export-followup/launch-diagnostic.py` permits only an explicitly
diagnostic fresh UI process after the entire automated attempt is terminal and
the silent device/source/provider/binary checks pass. It uses ordinary callback
timing, no cycle instrumentation. Its receipt cannot qualify a release; the
normal passing-candidate launcher and package gate remain strict.

The primary checkout now also contains an **unbuilt, post-freeze** worker
acceptance extension in `windows/Tests/LiveParameterPreviewTests.inc`, selected
by `document-controller-live-parameters`. Its demo song must export nonzero
sample audio. A second complete export while Gainer's audible preview is held
at-12dB must exactly match the earlier accepted0dB WAV and preserve the preview
owner/value, manual/opaque baseline, immutable view/history and running host.
This asserts the API's accepted-song contract; native File export intentionally
finishes queued UI intent first. It is not part of frozen6a9d's current build or
eleven-group attempt. Include the controller registration at a subsequent
cohesive checkpoint; do not repeat the current build for this test addition.

## 07:02 checkpoint: compiled corrections and remaining export gate

Frozen `6a9d3d4b7d931cb291f8f11b6ba29a0115de379b` built once at
07:02:11–07:05:50 UTC. Executable SHA256:
`d912ec2702526228a82bc03c43557a071aa7c23cc8e96aa7a416c472848ff8c6`.
`audio-export-followup/qualified-checks.json` is terminal: **10/11 native
groups plus scratch setup pass; all three actual-app journeys pass**. The
module-export exact history correction, native Save/menu/focus checks, precise
cursor retention/identity guards and actual pipe timeout/cancellation checks
now have rebuilt evidence. Silent WASAPI and its separate cycle-delivery probe
passed their bounded gates. No aggregate pass, package or merge occurred.

The F04 composing, layered VST3/sample composing and interleaved precise-note /
plugin-take journeys completed in172.667seconds. They include recorded edits,
cross-domain chronological history, exact unrelated-state preservation,
save/reopen and completed full-song exports. The preceding five-second export
reply failures did not recur. These results do not qualify commercial plugins,
physical recording inputs or sustained presentation.

The shared WAV group remains red. Dry export at512 frames is exact against an
independent core. At17/4096 it is also exact against the core at the same
partition, with376128frames in every case and no padding. Against512,17 has
18430changed samples (maximum8.12113e-7),4096 has7300(maximum7.37607e-7).
The next failure is an invalid precise-note oracle setup: the Renderer
constructor prepares native effect metadata but does not create event
schedules; `PluginChain::attachMusicalAutomation` explicitly calls
`preparePreciseNotes`. The test's independent Renderer omitted that call.
The primary source now adds it while retaining exact sample/duration checks.
This correction is **unbuilt**. Hosted gain, recorded-comb tails and the later
failure/preservation assertions were not reached in this run. The exact dry
cross-partition assertion remains mandatory; no tolerance was loosened.

Fresh Computer Use on owned PID37312 directly observed double-click insertion
(four to five hits), canvas Backspace removal, velocity61→98 automatic editing,
Ctrl+S retaining the caret, text Backspace98→9 retaining all four hits, and
Tab/Shift+Tab between velocity and Save now. The playback cursor was visible
when playback crossed the inspected row. A left-border handle drag changed
offset/velocity and Ctrl+Z restored them. Native File > Export audio completed
to a new disposable WAV and retained the velocity caret. Three read-only
observations pin playback epoch1, advancing frames1648320→4259040→9810720 and
no fault through edit/Save/export. Float retained the same four events. A
floating resize attempt produced no geometry change; do not count it as a
resize pass or infer a product defect from Main-targeted helper input. The
floating window is exposed only as related UI. Docked field UIA remains absent;
caret observations supplement, rather than replace, the passing native tests.

At07:15 CI40072558416(Mac) and40072558372(Windows) were still compiling.
Latest actual start07:02:41 sets the next checkpoint no earlier than08:02:41,
subject to later starts and terminal preceding jobs. Never repeat current
receipts. Include the unbuilt live-preview/export worker acceptance and oracle
setup correction in the next coherent batch. Preserve all failed logs.

PID37312 subsequently exited normally(code0), binary hash unchanged. Its
480.059-second silent48kHz/480-frame ordinary-timing run recorded37 deadline
overruns, zero starvation/device errors and no processor fault. Both
`realtimeAuditPassed` and `sustainedPresentationQualified` arefalse. Extra cycle
and stage instrumentation was disabled. These are failed sustained-performance
observations, not an acoustic result or evidence that a particular plugin
caused the overruns. The32-event timing ring omitted5 additional events.
`foreground-diagnostic/foreground-observations.json` records the bounded UI
checks and remaining limits. The native exported WAV has1296946frames,
27.019708seconds, finite stereo float PCM at48kHz, peak0.2454051524 and SHA256
`0d16fced0db1bccd174520134ee4210034a863df1ae5d96cabd0f80d17075c20`.
Do not reuse the exited PID or window handles.

### Source continuation: collect independent failures and stock partitions

The shared export test now runs eight independent named cases in one
registration: dry core, dry partitions, native mutes, precise core, recorded
gain, recorded tail, invalid/failed streams and source/history preservation.
Each failed case increments the final failure count; the registration remains
red if any case fails. All existing exact assertions and tolerance bounds are
retained. A precise-or-partition failure therefore no longer hides later tail
and atomic-failure evidence until another hourly compilation.

`mac/Tests/ReferenceRenderer.cpp` and `RenderAudit.cpp` accept an optional final
block-size argument(1..4096); existing invocations retain512. Allocation of the
selected output buffer occurs before the callback audit. `mac/test_audio.py`
accepts `SCREAMSEQ_AUDIO_BLOCKS=512,17,4096`, still compares every sample exactly
to the separate pinned stock process at the same block size, and separately
reports cross-partition differences within stock. That characterization is
not a tolerance or a replacement for the shared failed assertion. The runner
checks `reference-source.json`, pinned upstream identity, stock-test success,
library hash and renderer hash before trusting the oracle. Old harness binaries
cannot silently ignore the block request.

This is **unbuilt source**, with only Python AST parsing and diff checks done.
At a coordinated Mac checkpoint, build the normal render-audit target and the
pinned oracle using `mac/build-reference.sh`, then run
`SCREAMSEQ_AUDIO_BLOCKS=512,17,4096 python3 mac/test_audio.py`. Retain the source
receipt and complete result/log even on failure. Do not build the stock library
outside the hourly checkpoint or infer upstream behavior from the analytic
integer-decay model alone. Until that evidence exists, the test-contract versus
engine-compatibility question remains unresolved. No product DSP changed in
this continuation. Existing6a product/UI receipts retain their bounded scope;
the new C++ test/harness source cannot inherit their compilation pass.

The Mac workflow now has a manual-only `qualification: stock` mode for that
checkpoint. It runs one ARM job with two build workers, only tracker-core-tests
and render-audit from ScreamSeq, then the independent pinned upstream library.
It generates the existing core corpus and retains comparison/build logs,
reference provenance and harness hashes even on assertion failure. It creates
no application bundle, starts no device and does not replace normal PR gates.
Dispatch it only alongside the next coordinated checkpoint, at the exact
completed source ref; record the returned run before any retry. The ordinary
CI inspector's `Build ` step prefix includes both stock build stages, so their
actual starts extend the next hourly gate. This workflow path is also unrun;
static review/Python parsing do not constitute a passing stock comparison.

At07:30 the frozen6a Mac ARM job120808941853 was terminal. Compilation passed,
134/135 CTests passed, and the only native failure was the same precise oracle.
The dry17/4096 changed-sample counts, first offsets and maximum deltas exactly
match Windows. Full native interface, socket/conformance and owned AU/VST3
virtual-device recording gates passed. Intel remained compiling; Windows had
completed compilation and was testing. No jobs were restarted. Terminal ARM
log SHA256 `3aadc5b8fcdfaa07b5121cb18a26e36ea39acf74a3a3d15a3053718491b29797`
is pinned in `audio-export-followup/completed-ci.json`.

### Source continuation: retain sample setup through capture observations

Source inspection found that `SampleRecordingWindow::timer(3)` calls `perform`
every 150 ms during capture, and `perform` disables the sample-name edit and
Keep-as combo until the read returns. The status request does not own these
fields. `SampleRecordingWindow.hpp` now distinguishes observation from mutation:
polls and opening/reconciling a take keep raw fields enabled, while pending
departure, recording lifecycle and Keep guards remain in force. Raw edits still
advance the existing generation and retain their captured document/revision.
No musical model, API, history, file format or device routing changes.

`recordingSetupRetention` in `DraftImportRetentionTests.inc` now injects typing
and an output-choice change during successful and busy status reads. It checks
enabled controls, exact focus/selection/caret, newer raw text, generation,
captured target, retained take and zero musical commits. It also asserts that
Start and Keep still disable the raw fields. This runs in the existing
`native-tool-window-tests` registration (not a workspace-restore subgroup).
The fake provider opens no microphone. These changes are **unbuilt**; do not
inherit the prior candidate's UI or compilation evidence. Add this registration
and `document-controller-live-parameters` to the next bounded checkpoint, then
check fresh native focus/typing in the isolated application. Continuous capture
typing still needs a controlled input fixture; an idle recorder is insufficient.

At07:40 the Intel compilation had passed and its tests were running. Windows
was still testing. The next build remains no earlier than08:02:41 UTC, pending
terminal CI and later starts. No additional build or test execution was made
for this source continuation.

### Next frozen batch and terminal Windows evidence

The observation fix is committed/backed up as
`244a5549ca5f8fc1a2d5a6709f185686b2ad8a71`. The clean qualification checkout is
fast-forwarded to that source; its retained previous executable is still6a.
`bin/mac-gesture-20261022/recording-observation/freeze.json` pins this boundary.
Prepared `checkpoint.ps1` rechecks prior CI, the hourly gate and the clean source,
pushes only the frozen PR head, dispatches one exact-ref manual stock PCM job,
then invokes one local build. An uncertain dispatch or existing receipt must be
inspected, never automatically repeated. These scripts have only been parsed,
not executed. No build may start before08:02:41 UTC, or later actual starts;
Intel CI was still live at07:46.

The local batch builds the app/scanner plus three changed test owners. Its
bounded checks are `audio-export-stream`, `document-controller-live-parameters`
and `native-tool-window-tests`, plus the declared scratch setup. Two actual-app
cases exercise sample-recorder draft/departure/reopen ownership without opening
a microphone and the cross-domain plugin-recording composing journey. The ten
passing6a native groups and other two composing journeys retain their bounded
evidence for unchanged inputs. Fresh Computer Use on the new executable remains
required; recording status polling is covered by the injected native read tests,
not by an idle recorder screenshot. Sustained audio and reciprocal edited saves
are still unresolved; this batch alone cannot authorize a production package.

Windows x64 CI job120808941221 is terminal: compilation passed,161/162 portable
and worker checks passed (sole failure `audio-export-stream`), all53 native
editor groups passed, and the separate scratch/editor-vendor and pipe/typed
gates passed. The122-case application suite reported25 failures and one skip:
22 distinct playback-startup failures and three secondary audition teardown
failures. Recording/routing diagnostics show an empty device catalogue; no
application test was relaxed or reclassified as a pass. The local6a composing
journeys did run successfully with WASAPI, but do not turn this remote gate
green. Full log SHA256:
`3edf8d40ec046345bbb17efd878b2282ba96cbe00415013ce6e0f91b43e99d5b`, pinned in
`audio-export-followup/completed-ci.json`. No CI job was restarted.

Before dispatch, the same observation batch also corrects recorder Tab order.
The constructor previously grouped buttons before combo/edit creation, so native
traversal reached actions before capture setup. The new order is Input,
Refresh, Channels, Limit, Name, Keep as, any visible recovery actions, then
Record/Stop/Keep/Discard, Discard setup and Close. Windows skips unavailable
controls normally. No custom keyboard routing or automatic recording is added.
The existing native owner group sends actual Tab and Shift+Tab messages through
the controls in idle, capturing and stopped states, and requires exact setup,
take and draft preservation. This source and its assertions are unbuilt. The
prepared freeze may advance to include it only while no checkpoint/build/dispatch
receipt exists; retain the earlier freeze as a superseded preparation, not as an
executed attempt. Include setup-first traversal in fresh foreground checks.

The same source continuation adds root Tab/Shift+Tab entry and focuses the first
available setup control after initial loading only if the active recorder still
has root/null focus. A child or another owner's focus is not reclaimed after a
pumping read. Visible reopen retains the name caret. The native fixture now
checks root entry in all three states and exact reopen setup/take preservation
(the explanatory status is allowed to change). The actual-app recorder case
also walks Input→Refresh→Channels→Limit→Name and requires visible reopen to
retain its selected text range and draft identity.

One new pre-correction diagnostic ran that extended app case against retained6a,
without a build or microphone. It failed in0.677s at initial input focus, before
the added Tab assertions. `recording-observation/recorder-baseline.json` pins the
old executable and new test hashes; keep this failed receipt. The new source is
not qualified by this failure and must pass the next scheduled check.

At07:53 the preceding Intel job120808941485 was terminal. Like ARM, compilation,
native interface, socket/conformance, sample library and native AU/VST3 device
recording gates passed;134/135 CTests passed, with only the precise export oracle
failing. Dry partition diagnostics exactly match ARM/Windows. Intel log SHA256
`9b587b9387474a80561be3b24ada2adc71e96c8dd4b9e8435e1bb12ca9563d88` is retained in
`audio-export-followup/completed-ci.json`. Both preceding workflows are terminal;
the time gate still prohibits a new build before08:02:41 UTC.

## 24 October 08:13 UTC — observation checkpoint and stock oracle

Frozen source `066eb70524e6fdef18a4215d8dfd0fd03bcf30aa` built once locally,
08:03:01–08:04:45 UTC. PR7 now points to this source. Executable SHA256 is
`c29ae3d5d4b592171be4a1dc32643663ad032e37fbf2a5a3df645559286b630a`.
Keep `recording-observation/build.json`, `qualified-checks.json`, native XML/log
and app log; neither the build nor the completed checks may be repeated.

The two actual-app cases passed: recorder initial Input focus, forward setup
traversal, visible reopen caret and retained draft ownership; and the full
cross-domain plugin-recording composition/history/save/reopen journey.
`document-controller-live-parameters` also passed the accepted-song export during
an active manual preview, with source/history/preview/transport preserved.
The scratch setup passed. Two native registrations remain red:

- `native-tool-window-tests` fails recorder root keyboard entry. The old message
  does not identify forward versus reverse entry. This occurs before the new
  success/busy polling assertions, so those assertions are **not qualified**.
- `audio-export-stream` now executes eight independent cases: seven pass,
  including the corrected precise-note PCM oracle; dry callback partition
  equality fails. Same-partition output still exactly matches independent core.

The source continuation replaces null-start `GetNextDlgTabItem` with explicit
first/last visible, enabled, tab-stop traversal of the recorder's direct children.
Ordinary child traversal remains native. Microsoft documents a null starting
HWND as failure in [GetNextDlgTabItem](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getnextdlgtabitem).
The assertion now reports direction and expected/observed control IDs, without
changing the requirement. This correction is **unbuilt**, and the qualification
checkout remains frozen at066.

Fresh Computer Use on owned diagnostic PID40296 opened the recorder through
Tools → Sound → Record microphone or audio input. Its layout and labels were
readable at the current desktop scale; a refreshed accessibility observation
identified Input6501 as focused. No microphone was opened. The recorder appears
only as related UI, not its own targetable window. A Main-targeted Tab reactivated
Main and moved the pattern selection; this helper limitation is **not** evidence
of recorder traversal failure or success. No foreground typing, reverse Tab or
capture-poll continuity is claimed. The recorder closed via its visible Close
button, then the owned process exited normally with code0 after Alt+F4. Binary
hash remained unchanged; never reuse this process or its window handles.

The read-only post-close snapshot reports unchanged document content, continuing
playback epoch1 and no processor fault. The253.4235-second silent48kHz/480-frame
run recorded34 deadline overruns,5 starvation indicators and0 device errors;
maximum callback15,827.9µs. `realtimeAuditPassed=false` and
`sustainedPresentationQualified=false`. This is a failed performance observation,
not acoustic or sustained presentation qualification. Files are retained under
`recording-observation/foreground/`.

Independent stock job120820900236/run40076515535 completed successfully. It built
and tested retained upstream `f83cedb0cd5446e4dfaa83ac97e3087107e26767` and passed
**90 exact PCM comparisons**, at44.1/48/96kHz and512/17/4096-frame blocks, with
zero intercepted allocation/release/mutex calls. The demo-derived sequence0
fixture reproduces the local dry-export differences exactly: at48kHz,17-frame
blocks change18,430 samples with maximum8.121132850646973e-7;4096-frame blocks
change7,300 samples with maximum7.37607479095459e-7. All have376,128 frames and
ScreamSeq equals stock exactly at each matching block size. Full log SHA256:
`0371ccb2ad70a55f579ed09dd6a4ac61a289b022490abb60c7fa6b7b8943c614`, pinned by
`recording-observation/completed-ci.json`.

**Interpretation and unresolved contract:** exact stock output at every caller
block size and exact equality between those block sizes cannot both hold for
this fixture. This now has independent upstream evidence, not only an inferred
rounding explanation. Preserve the failed assertion and avoid speculative engine
changes. Recommended resolution is to keep the stock-compatible render contract
and explicitly distinguish it from export-buffer determinism; any canonical
export buffering proposal must define its boundary and retain exact independent
stock checks. No assertion or tolerance has been weakened in this continuation.

At08:11 the Windows and both Mac native CI jobs were still compiling. Do not
restart them. The stock library stage started08:05:43, extending the earliest
next checkpoint to **09:05:43 UTC**, subject to any later actual starts and all
current jobs becoming terminal. No package or main merge is permitted from this
candidate. The next checkpoint must exercise the root-entry correction and the
previously unreached polling assertions, plus fresh bounded foreground evidence.

## Source continuation — correct the contradictory dry-export test

The independent upstream result resolves the previous open question in favor of
the repository's existing stock-compatibility requirement. The optional
`blockFrames` argument explicitly selects the engine render partition, not an
output-file chunk size; both production callers use512. Changing the engine or
silently canonicalizing that argument would change the behavior being tested.

`AudioExportStreamTests.cpp` now names the case `dry stock-compatible partitions`.
It retains exact per-sample equality with the independently constructed core at
each requested partition, exact WAV sizes, no padding, and the cross-partition
diagnostic differences. It adds explicit equal duration across partitions. It
removes the erroneous additional requirement that the two different stock
outputs equal each other. This is an explicit correction to an impossible test
expectation supported by the separately built upstream oracle; no epsilon is
introduced and no product/DSP behavior changes. The recorded-gain partition,
precise-note exactness, recorded-tail, failures and preservation assertions are
unchanged. The public C++ header documents the existing render-partition contract.

Keep the earlier failed receipts as failures. This changed test is unbuilt and
must execute on all three native architectures at the next coordinated
checkpoint. The independent90-case oracle evidence can be reused while renderer,
retained engine, fixture generation, compiler inputs and oracle harness remain
unchanged; a changed engine path requires fresh comparison. This test correction
does not address the measured audio deadline failures.

### Recording keyboard continuation and bounded stage profile

The plugin recording window has the same source-level root Tab gap. It already
sets initial focus and orders its children, but its root key handler did not
handle Tab after root focus (for example, retained-work Review explicitly focuses
the root). `NativeToolWindow::tabEntry` now provides an opt-in first/last available
direct-child lookup shared by the sample and plugin recorders. Only those owners
call it; there is no new global key fallback. Ctrl+Tab continues to other routing.
The plugin review's existing native group now checks root Tab/Shift+Tab in idle
and capturing states, skipping disabled scope/actions and preserving the complete
take snapshot, write count and music. Existing child traversal and text-copy tests
remain. Required local groups for the next batch are `native-tool-window-tests`,
`workspace-plugin-search-tests` and `audio-export-stream`; reuse unchanged
live-preview export/controller results from066, not its two failed registrations.
All these source changes remain unbuilt.

One60-second opt-in stage/cycle profile ran on the existing066 binary in a
never-switched private desktop, using the same disposable song and silent output.
PID5068 exited0, source/binary/fixture identities are pinned under
`recording-observation/stage-profile-corrected/`. The initial launcher omitted
`--automation` required by the private recovery-directory option; PID24784 exited1
in0.23s without an audio report. That failed preparation is retained separately
under `stage-profile/`, not counted as a measurement or retried in place.

The valid profile had6,019 callbacks and5 deadline overruns,0 starvation/device
errors, and0 failed cycle queries. All five retained stage events spent more
than99.9% of measured render wall time in `engineIncludingInstruments`; preparation,
outer hosted processing and timing publication were small. Maximum callback was
11,755.4µs. This localizes the next investigation to engine/routed/instrument
processing, but does not identify a particular plugin or distinguish CPU work
from scheduling delays. Cycle counts are retained as counts, not converted into
time. Instrumentation adds overhead, the private desktop does not test visible
presentation, and no performance pass follows. Do not change DSP based only on
these stage totals; obtain a narrower target-only profile before optimization.

At08:24 both native workflows remained in progress; the stock job is terminal
success. The09:05:43 UTC minimum gate is unchanged. No additional build, CI
dispatch, package or main merge occurred during this continuation.
