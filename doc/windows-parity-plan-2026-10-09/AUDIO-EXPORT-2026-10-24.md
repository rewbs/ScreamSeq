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
3. Add Windows File > Export audio… and command-palette entry using a native
   chooser, asynchronous worker status, explicit failure message and retained
   originating focus. Mac should use the same guarded contract.
4. Compose/record/Keep/Undo/Redo/Save/export/reopen journey: assert unrelated
   music and native project stay exact, epoch advances continuously, and compare
   exported audio using shared fixtures. Test invalid/stale/overwrite failures.

No Windows export control is present yet; the API is source-only and unqualified.
Cheap reviewed inventory tests passed (6 tests); this is not execution evidence
for either native export implementation. Fresh Mac `export`, shared
`audio-export-stream`, Windows `atomic-output-stream`, worker export and both
actual app transports are required. Preserve existing fixture PCM assertions.
The frozen
`retained-reveal` candidate 24788ebc34f35a94f24138d63a1df248cddff445 deliberately
excludes this continuation; its evidence must never be attributed to this code.
