# Full-song audio export parity

## Observed boundary

Current Windows source has **Export module** (`EditingView.inc`,
`WorkspaceCommands.inc`, `WorkspaceMenuBar.inc`, `DocumentController.cpp`).
The old matrix's missing-module-export entries are historical, not current proof.
At discovery there was no Windows user-facing audio export command. Mac `main.swift` has
`exportAudio()`, using `TrackerSession` and `mac/Audio/AudioExport.mm` to write
48 kHz stereo float32 WAV. Neither host had `document.exportAudio` at discovery.
This is a remaining composing journey: record/Keep, Save, export audible song.

## Source continuation (not built or qualified)

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

## Next cohesive implementation batch

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
