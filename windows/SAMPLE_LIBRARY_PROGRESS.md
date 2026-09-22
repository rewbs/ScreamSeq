# Windows sample library, independent preview and family import — 2026-09-22

Windows now implements the Mac sample-library workflow and all eight
`sample.library.*` methods from `mac/AUTOMATION.md`. Existing shared imports,
musical Undo and native persistence remain the editing boundary.

## Implemented

- Separate request/index workers publish immutable, bounded indexes. Searches
  retain the previous index during rescan; root changes cancel older generations.
  The same 17 extensions, hidden/reparse/package filtering, overlapping-root
  deduplication, Unicode word/phrase/exclusion search, AND folder tags, natural
  order, root boundaries, pagination and bounded facets are implemented.
- Library UUIDs are independent of song revisions. Root/rescan writes use
  `expectedLibraryRevision`; replies use `revision: library:<uuid>`,
  `changed:false`, `playbackStopped:false`, and no document ID. The adapter avoids
  song snapshots, retains successful write replies and rejects duplicate
  in-flight write IDs while the UI pumps messages.
- Roots/cache use atomic replacement. Removing roots never removes sample files.
  Damaged caches rebuild; damaged preferences use explicit defaults with a
  warning. Inspection/audio tests use in-memory roots unless given a private
  test directory. Normal settings live under
  `%LOCALAPPDATA%/org.resonance.tracker/SampleLibrary`.
- Inspection uses the shared decoder on its own worker, returning metadata and
  256 min/max pairs without PCM. Preview is bounded to 30 seconds / 2,097,152
  source frames. Its eight-entry / 32 MiB PCM cache invalidates by size/mtime.
- Preview has its own WASAPI stream, mono/stereo identity, gain and 2 ms fades.
  Windows converts the source rate to the existing stereo default endpoint using
  [its quality conversion flags](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audclnt-streamflags-xxx-constants).
  The song retains its existing low-period device path. No defaults change.
  Callbacks own no storage; control-thread retirement joins before releasing PCM.
  Stop/new selections invalidate pending preview generations. Inspection mode
  never opens preview hardware.
- **Browse samples…**, beside the sample list and in the command palette, opens
  a modeless native browser: search, folders/tags, pages, waveform/metadata,
  preview/Stop/gain/auto-preview, multi-selection, folder management, file chooser
  and raw/mapped-instrument import. Keyboard controls include Tab, Ctrl+F,
  Ctrl+R, Space preview, Enter import and Escape Stop.
- Filename pitch families match the Mac convention and octave suggestions. A
  native review captures song identity/revision, exposes name/octave fields,
  checks exact zones through the shared dry-run API, and imports in one Undo
  step. Edited/checked drafts survive Close/reopen. Stale destinations reject;
  Use current song explicitly captures a new destination and requires recheck.
  Imported sounds become the musical-typing selection.

`workspace.get.sampleLibrary` exposes service status, pending requests, preview
counters and the retained browser/family review.

## Qualification

Final ARM64 executable SHA-256:
`433725F00777C7D0926937BCB5AC3F9A59A7A1A37A62BEB3945F932C60D15E61`.

- Build: `bin/windows-sample-library-final-ui-build.log`.
- Primary native CTests: **30/30 passed**, 13.34 seconds,
  `bin/windows-sample-library-final-ctest.log`.
- Index/worker: **2/2 passed**, 0.39 seconds,
  `bin/windows-sample-library-index-final-ctest.log`.
- Adapter/replay: **2/2 passed**, 4.43 seconds,
  `bin/windows-sample-library-adapter-tests.log`, including independent revisions,
  stale guards, exact replay and reentrant duplicate-write rejection.
- Preview tests decode real 8/16-bit mono/stereo WAVs and cover waveform/cache
  invalidation, limits and controlled cancellation. Callback PCM is identical
  at 44.1/48/96 kHz for blocks 1/17/128/4096; scoped C++ allocations/frees are zero.
  This is not a driver/malloc/lock audit.
- Silent WASAPI qualification at those rates rendered 8,820 / 9,600 / 19,200
  source frames with automatic completion and explicit Stop while another song
  stream continued. All runs reported zero overruns, starvation, device or MMCSS
  errors (`bin/windows-sample-preview-device-final.log`). Silence follows DSP.
  This short test does not establish speaker/loopback quality or sustained load.
- Focused actual-app tests: **10/10 passed**, 7.189 seconds,
  `bin/windows-sample-library-browser-focused2.log`: discovery/search/tags/pages,
  revisions/cache/transport, native browser bounds and controls, silent preview,
  imports, Undo/persistence, stale family drafts, retained Close/reopen, rebase,
  and imported sound selection. Earlier harness corrections use the actual
  transport `fault` field and wait for deferred native opening; logs are retained.
- Full isolated application suite: **257/257 passed**, no failures or skips,
  664.289 seconds (`bin/windows-sample-library-final-app-tests.log`). Strict
  outer foreground/clipboard isolation checks also passed
  (`bin/windows-sample-library-final-isolation.log`).

The preserved ARM64 development package is
`bin/windows-checkpoints/sample-library-20260922/`; its manifest records the
committed source, executable hash and verification evidence.

## Open limits

Foreground aesthetics/accessibility and sustained display timing remain
unqualified; bounds tests are not a visual Mac comparison. Maximum-size library
throughput/RSS has not been measured. Cancellation discards stale results but
does not interrupt a running codec. Preview currently uses the default stereo
endpoint; explicit devices and other layouts remain work. Session library
revisions do not merge simultaneous preferences writes from separate processes.
The browser gain field currently applies on Preview; Mac's immediate adjustment
of a running preview remains a small UI follow-up, along with clearer playback
feedback. The preview voice already has a lock-free gain control.
No Mac runtime or reciprocal project round trip was performed. The remaining
device/MIDI/recording/recovery, workspace, plugin and release gates remain active.
