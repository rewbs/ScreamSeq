# Full-song audio export parity

This is a chronological implementation/evidence record. For the current frozen
source and next gate, use [the continuation queue](CONTINUATION-QUEUE-2026-10-20.md)
and `audio-export-followup/freeze.json`. The initial source-only statements below
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
