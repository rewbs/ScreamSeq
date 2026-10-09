# Sampling implementation and qualification — 7 October 2026

## Delivered

- Native input recording: Core Audio on macOS and WASAPI on Windows, with device/channel selection, elapsed time, input level, clipping status, bounded recording and retained takes.
- Add a take as a new sample or as a sample plus mapped instrument. Existing assets remain intact; creation is one shared Undo step and the added audio is embedded in the song.
- Render selected pattern rows/channels directly to a new sample or instrument using the shared hosted playback engine, including instrument effects, channel routing, inserts, automation and master processing.
- Agent API methods for listing inputs, starting/stopping/inspecting/discarding/committing takes and rendering selections. Mutating imports use revision guards and atomic shared editing.
- Native menus, context menus and command-palette entries. The Mac sample inspector has a visible Record action even in a narrow dock.

See [Recording samples and instruments](SCREAMSEQ_SAMPLING.md) for controls, API examples and exact rendering semantics.

## Verification

Implementation commit: `bff5957ae2f78a2c72f95b0a56f1c1ed40932d35`. The follow-up UI polish changes the Mac sample header and resets the recorder's details after a take is consumed/discarded.

| Check | Result |
| --- | --- |
| Mac application build, including final UI polish | Passed |
| Complete Mac CTest suite | 118/118 passed |
| Native AppKit interface suite after final UI polish | Passed |
| Packaged-app sampling API | Passed: inclusive crop/duration, tail, validation, revision guards, source preservation, sample/instrument Undo/Redo and exact PCM save/reopen |
| Live Core Audio input through BlackHole 2ch | Passed: captured nonzero stereo PCM, edited the song during capture, retained takes across rejected/dry-run commits, then committed and round-tripped exact PCM |
| Actual Mac UI recording journey | Passed: Record, Stop, name, create instrument, Add to song; new waveform displayed and instrument selected |
| Actual Mac UI selection journey | Passed: selected four rows, recorded to instrument, obtained 23,040 stereo frames at 48 kHz (0.48 seconds) |
| Recorder close/reopen | Passed: closing stops capture and reopening retains the take |
| Isolated capture-buffer sanitizers/realtime audit | Passed; synthetic buffer ingestion only, not qualification of every device/driver callback |
| Windows native application build | Passed in CI on implementation commit `bff5957ae` |
| Windows portable tests / worker tests / scratch private-desktop checks | 45/45, 52/52 and 3/3 passed respectively |

The live input test used BlackHole, not the physical microphone. It captured 20,992 frames at 48 kHz and verified their identity through Undo/Redo and native project persistence. A separate manual UI recording captured 614,400 frames (12.8 seconds). System audio defaults and the musician's running application were left unchanged; the separate QA process was closed after inspection.

Packaged-app checks exposed a real small-worker-stack overflow that standalone tests did not reveal. Large renderer/plugin-chain objects now live on the heap in selection rendering and the adjacent Mac audio-export path. Regression coverage exercises a GCD worker and a 512 KiB export worker, including exact exported PCM comparison.

Windows qualification passed in [CI run 37606471741](https://github.com/rewbs/ScreamSeq/actions/runs/37606471741), building implementation commit `bff5957ae`. The new `sampling`, `sample-recording` and `document-controller-sampling` tests executed, including exact crop and hosted gain comparison, atomic Undo/Redo and saving. The follow-up polish changes only Mac UI and documentation; Windows and shared runtime sources are unchanged from the successful run. Physical Windows microphone capture and interaction with the new recorder window remain deferred: its native code compiled, and its recording lifecycle passed with a fake capture device. The three private-desktop UI checks exercise the scratch editor, not the new recorder.

## Retained local evidence

Evidence is under `bin/mac-scratch/qualification/` and is intentionally not committed as generated test output:

- `sampling-header-build.log`, `sampling-final-ctest.log`, `sampling-header-interface.log`.
- `sampling-socket-final/result.json` and `sampling-live-capture-verified/result.json`.
- `sampling-final-recorder.png`, `sampling-final-waveform.png`, `sampling-recorder-live.png`, `sampling-recorder-retained.png`.
- `sampling-ui-result.json` and the disposable `sampling-ui.screamseq` test project.
- `microphone-backend/retained-tests-evidence.json` for the scoped sanitizer/realtime audit.

Windows CI logs and their SHA-256 manifest are retained separately under `bin/windows-sampling-portable/ci-bff5957ae/`; `evidence.json` identifies the exact source, run and downloaded artifact.

The final inspected isolated bundle is `bin/mac-scratch/qualification/ScreamSeqSampling.app`, bundle ID `org.screamseq.tracker.sampling`. Its executable SHA-256 is `99ca0115160bb5f4ac6dfa17c457624ba03b4dc517d84ee474df3629a1ae1dea`; its build manifest records source fingerprint `bfd81516ee7d1871581ff2d41b50a641432f987adc3bf8fc552afcf1c0502a02`. The automated live-input run used an earlier executable with the same capture/selection implementation, before the adjacent export fix and final UI polish.

## Current boundaries

- Takes must be added to the song to enter saving/autosave; staged PCM is session-only. Closing/replacing the song requires adding or discarding it first.
- Input is mono or stereo, without monitoring. Native sample storage is 16-bit; there is no automatic normalization.
- Selection rendering is pattern-local and traverses selected rows once. It warms from that pattern's beginning, not earlier song orders. Excluded source channels also contribute no sidechain signal.
- Recording defaults to 60 seconds; the API supports up to 300 seconds subject to the frame cap. Mac also exposes five minutes in the recorder; Windows currently exposes 60 seconds.
- This work does not claim that the earlier strict display-performance qualification or unrelated graph follow-up is complete.
