# Native sample detail — 2026-09-21

The sample dock's Detail button and the command palette open a retained native
sample editor. It uses the existing shared sample API and document history;
there is no DSP, native-format or public API change.

## Editing behavior

The editor captures document, revision and stable sample identity. Close keeps
drafts; Reload rereads that identity, and From cursor explicitly chooses the
current song's sample. A sample selection change cannot redirect an unapplied
drawing or field draft. Reads and write completions recheck the captured revision
and generation. Unrelated document changes make the editor stale until Reload.

The waveform supports exact frame windows, fit, zoom selection, pan and anchored
Ctrl-wheel zoom. Separate sample identities retain their selection and viewport.
Reads are bounded to 4096 peak bins, and painting consumes that retained cache.
At one frame per pixel or closer, mouse drawing interpolates the gesture's
individual frames. Exact frame/amplitude fields also stage points. Linear and
step interpolation, left/right/both channels, Apply and Discard use the shared
`sample.draw` operation. Escape or lost capture cancels the current gesture;
one Apply uses one document Undo transaction. Strokes are bounded to 4096 points.

The process selector exposes all 14 existing operations, including gain,
normalize, shaped fades, DC removal, smoothing and stereo channel operations.
Preview uses `dryRun` and reports actual changed/clipped counts. Crossfade
controls select normal/sustain loops, preserve-duration/overlap modes, linear/
equal-power curves and exact fade length; preview reports the resulting loop
period. Loop controls use a selected frame range with forward, ping-pong or
reverse direction. Zero-crossing and grid snapping adjust the selection.

Private sample clipboard controls include copy, cut, delete, insert, overwrite,
mix, replace and copy-to-new. They preserve the system clipboard. Musical edits
use the existing validation, prepared sample patches, stop-before-publication,
document Undo/Redo and native persistence. Keyboard canvas controls include
Ctrl+A/C/X/V, Delete, arrows, Shift+arrows, Ctrl+arrows, Home, +/- and Ctrl+Z/Y.
F6 switches between canvas and exact point entry; Ctrl+Enter applies the drawing.

## Qualification

Nine focused application tests passed in 99.816 seconds. They cover all 14
processing operations against the shared API, exact drawing/channel results,
normal crossfade modes/curves, viewport retention, native control bounds,
clipboard modes, stale drafts, history, save/reopen and rendered audio.

Review also identified an Undo identity hazard: history can remove the captured
sample. Completion now rechecks that stable identity before reading its slot,
leaving a removed target stale until explicit From cursor. The new dense-point
test exposed repeated native layout during nested edit notifications and ran
past the disposable application's deadline. The window now repositions controls
only when size, DPI or pending state changes. Four final focused tests pass in
12.135 seconds, including all 4096 staged points, atomic rejection of a 4097th,
Undo removal, silent device behavior and minimum-size control bounds. This is
bounded functional evidence, not a sustained presentation benchmark.

The final ARM64 executable has SHA-256
`CBA33FE3CC06D66DA65CAB945E77FA58CFB9E8819D4A3160DEF491045C3A4709`.
The complete regression suite passes all 211 tests in 571.645 seconds with no
failures or skips, including all 12 detailed sample cases, sustain crossfade
and native cut/delete coverage. The canonical log is
`bin/windows-sample-detail-app-tests.log`. The preserved checkpoint is
`bin/windows-checkpoints/sample-detail-20260921/`; its manifest records the
committed source, executable hash, test counts and individual file hashes.
The post-test process inventory contains no remaining ScreamSeq QA processes.

The short silent WASAPI check preserves active playback across waveform reads,
process previews, a zero-gain no-op and an invalid gain request. A real native
gain edit stops before publication. Hardware output is muted after DSP; no
system route or volume is changed.

On the final executable, the native gain fixture produces finite rendered audio
at 44.1, 48 and 96 kHz with blocks of 17, 128, 4096 and 8193 frames, one second
per render. The active gain result has aggregate L1 energy 29099.5931593 versus
53667.4921908 before processing; the maximum quarter-second energy difference
is 53.24161723537462. Maximum PCM partition deltas are 3.8743019104e-7 (active)
and 5.43892383575e-7 (baseline), both below the unchanged 1e-6 threshold. Native
save/reopen preserves the exact edited PCM and both render runs leave their
documents unchanged. This fixture proves audible processing; it is not a
throughput benchmark. The final short silent run reaches 213 callbacks with
zero reported overruns or fault before the applied edit stops transport.

The tests address native control handlers on an isolated desktop, exact PCM
and loop readback, stale draft retention, shared API equivalence, history,
save/reopen, bounded waveform requests, layout bounds and offline audio.
These checks do not establish foreground visual quality or long loaded realtime
performance. The detailed window does not yet provide audition, recording,
sample property/export/import-many controls, advanced paste gain/rate options or
live voice markers; the existing sample dock retains its playback markers.
Mac runtime and reciprocal reopen remain separate qualification gates.
