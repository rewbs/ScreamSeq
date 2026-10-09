# Scratch phrases — implementation and qualification, 7 October 2026

ScreamSeq now supports reusable scratch arrangements: **SK** selects a song-local phrase with independent record-motion and fader envelopes; **SX** stops it early. NF/NR remain the quick nudge commands. The design research, instructions and precise musical semantics are in [Scratch phrases](SCREAMSEQ_SCRATCH_PHRASES.md).

Later work on atomic variations, the Windows curve editor and performance is recorded separately in the [followup report](SCREAMSEQ_SCRATCH_FOLLOWUP_2026-10-07.md). The results below remain the evidence for this original checkpoint.

## Implemented

- SK's phrase number, total beat duration, sample travel in milliseconds and repeat count are editable directly in the pattern. Rows display is available through the existing effect timing units menu. Reverse is an expanded parameter; start/stop offsets retain precise timing.
- A non-modal Mac editor presents paired motion/fader curves, synchronized zoom, snapping, keyboard point editing, exact values and all existing curve shapes. Scripted segments use the expandable formula workbench, autocomplete, curve preview and a scratch-specific reference.
- Baby, Chirp, Transform, One-click flare, Two-click flare, Crab and Scribble starter shapes. They are original starting curves, not claims of exact physical DJ technique.
- A song bank supports linked uses, duplication and Make unique. Changes save immediately and participate in the shared document history. Curve edits and their Undo/Redo publish during playback without restarting it.
- Per-sample sample-position control, an independent smoothed fader, musical-clock timing, exact closed-cycle cue return and continuous accumulation for open cycles. Ownership follows the active sample note; later notes and NNA tails do not inherit the phrase. A 20 Hz per-voice DC blocker handles stationary record positions.
- Shared musical model, validation, renderer, metadata and agent API. Mac and Windows codecs retain curves and scripts; best-effort loading warns about damaged entries while preserving independent phrases. Cross-song pattern clipboard imports/remaps referenced phrases in the paste transaction.
- API reads, create/edit/remove, dry runs, strict revision guards, no-op behavior, history and formula preview. The contract is documented in [AUTOMATION.md](../mac/AUTOMATION.md#scratch-phrases) and the JSON schema.

## Build and evidence

New Mac bundle: `bin/mac-scratch/ScreamSeq.app`. Its distinct development identity is `org.screamseq.tracker.scratch`; the musician's existing `bin/mac-background/ScreamSeq.app` process was left untouched. Test instances used separate bundle identities, inspection mode and disposable documents. No system audio default was changed. No commit or push was performed in this task.

- Base revision: `b7c9c0feabe9a9c9192f2825cc969a583867e6a2`, plus the working tree (including pre-existing changes).
- Source fingerprint: `4b91d85c89910592864137da75a15f0fb470550ff0e577ae3a2e845dcca4d127`.
- Executable SHA-256 after packaging: `4a268438aa1155628b07343fd60715f2436cc30c52a5cfd25c6c42299ef2d9ed`.
- Build manifest: `bin/mac-scratch/ScreamSeq.app/Contents/Resources/BuildInfo.json`.
- Final build, strict bundle signature verification and `git diff --check` passed.

Evidence below lives in `bin/mac-scratch/qualification/`, except the interface log one directory above it.

| Check | Result | Evidence |
| --- | --- | --- |
| Full C++/Objective-C session and engine suite | 112/112 passed | `ctest-final.log` |
| Full AppKit interface suite, including scratch editing and contextual reference | Passed | `../interface-scratch-reference.log` |
| Real private socket suite, windowless host | Passed | `socket-tests.log` |
| Real private socket suite, packaged app | Passed | `socket-app-tests.log` |
| Windows portable scratch API, metadata and inline-field checks | 3/3 passed, also included in full suite | `windows-final-review-tests.log` |
| Live curve API/UI edits and history | Playback continued; zero observed overruns/faults | `live-bank-update.json` |
| Deterministic example exports | Independent renders byte-identical; no clipping or nonfinite PCM | `demo/determinism.json`, `demo/audio-analysis.json` |
| 60-second visible tracker + BlackHole run | Audio passed; strict presentation/freshness gates failed | `clean-scratch/report.json` |

The audio regression suite checks 44.1, 48 and 96 kHz with callback sizes 1, 17, 128 and 4,096 frames, independent forward/reverse trajectory oracles, nonzero cues, repeat closure, tempo/row-length changes, short samples and sample boundaries, 8/16-bit mono/stereo, loops, stop and next-note ownership, fader silence, held-position DC decay and live curve adoption.

An offline stress benchmark of **16 simultaneous sample voices**, each with compiled scripted motion and fader curves, at **48 kHz / 512 frames**, measured mean **1.653 ms**, p99 **1.811 ms**, maximum **1.841 ms** against a **10.667 ms** deadline over 6.005 seconds after warmup. It recorded zero deadline misses, allocations, frees or locks inside the audited render. This is bounded offline-renderer evidence, not a device-latency or universal workload guarantee. See `scratch-benchmark.json`.

The manual live-edit check used explicit **BlackHole 2ch at 48 kHz / 512 frames**. After API curve changes, Undo/Redo and a UI fader drag, playback had processed 1,114,112 frames / 2,176 callbacks with zero overruns or faults; observed p99.9 callback time was 1.130 ms, maximum 2.206 ms. No speakers or microphone were used for this check.

### Sustained UI/audio result

The final build ran the scratch performance fixture for **60.013 seconds** with visible tracker navigation, 299 edits, 14 Undo/Redo cycles and three saves. No build, interface suite or computer-use interaction ran concurrently. The workload includes an Apple low-pass effect added by the existing qualification harness. Output was explicit BlackHole 2ch, 48 kHz / 512 frames.

- Audio: **5,625 callbacks, zero overruns, no fault**, p99.9 **1.470 ms**, maximum **1.860 ms**.
- Display: **59.936 fps** average; CPU frame p99 **0.390 ms**, GPU frame p99 **0.173 ms**, zero buffer starvation, zero late commits/GPU completions against their targets.
- Strict display gates **failed**: 53 counted missed presentations, a **41.666 ms** maximum presentation interval, and **60.420 ms** maximum geometry age. Thresholds were not relaxed.

Timeline analysis places the worst geometry event after a 59.40 ms gap between serviced main ticks, with no corresponding long recorded application/layout operation; the following tick cost 2.23 ms. Many presentation intervals alternate 25 ms and 8.33 ms on the 120 Hz display. These are pacing defects, even though average FPS and renderer costs look good. Earlier pre-scratch reports also failed geometry freshness at 40.372 ms and 35.785 ms. The evidence does not establish a scratch-specific render bottleneck, and this build is **not claimed to have passed the strict 60 fps qualification**. A bounded main-thread/run-loop profile is the next diagnostic step for the broader UI scheduling work.

A matched 60.010-second control removed only the 16 SK commands while preserving the bank, cues, cuts, notes, orders and other metadata. It failed the same three display gates: **59.901 fps**, **29** counted missed presentations and **52.816 ms** maximum geometry age; audio again had zero overruns/faults. CPU/GPU frame p99 were 0.712/0.279 ms. This supports treating the issue as broader frame pacing rather than a demonstrated scratch-specific regression; a single pair of runs is not a statistical equivalence test. See `clean-control/report.json` and `demo/control-comparison.json`.

## Actual UI walkthrough

The following were exercised in the separate native app, rather than inferred from unit tests:

1. Typed SK into an effect cell in a song with no phrases; it opened phrase creation.
2. Chose Two-click flare, used it in the captured cell, and checked the visible linked-use count.
3. Dragged the motion apex; the saved value changed immediately. Global Undo restored the original curve.
4. Selected a point with Tab, changed its curve to Scripted, opened the expanded workbench, edited a formula and returned it to its original segment.
5. Returned to the pattern and edited duration, travel and repeats with Return/Tab. The API confirmed exactly `2 beats / 120 ms / 4 repeats`.
6. Opened the editor from the command palette, dragged a fader point during playback, and checked synchronized zoom and uninterrupted transport.
7. Double-clicked SK to return to its phrase; Make unique assigned a new phrase while preserving the command's other values.
8. Resized the paired editor to approximately its 880 × 620-point minimum; both curves and primary controls remained visible.
9. Opened the final example song without recovery warnings, selected its scripted phrase and verified the corrected cycle-based formula reference in the final build.

Screenshots:

- [Paired phrase](../bin/mac-scratch/qualification/ui/01-paired-phrase.png)
- [Live edit and synchronized zoom](../bin/mac-scratch/qualification/ui/02-live-edit-and-zoom.png)
- [Inline pattern parameters](../bin/mac-scratch/qualification/ui/03-inline-parameters.png)
- [Compact editor](../bin/mac-scratch/qualification/ui/04-minimum-size.png)
- [Scripted phrase in the final build](../bin/mac-scratch/qualification/ui-final/05-scripted-phrase.png)
- [Contextual formula reference in the final build](../bin/mac-scratch/qualification/ui-final/06-scratch-script-reference.png)

## Example arrangement

`bin/mac-scratch/qualification/demo/Scratchbook.screamseq` is a self-contained 16-phrase arrangement at 100 BPM. Pattern 1 contains the seven starter techniques plus a scripted elastic orbit, followed by repeated, reversed and pitched variations. Track 1 holds the scratch voice; three synthesized percussion tracks provide a backing. Samples and phrases are embedded; no plugins or sample packs are required.

- `Scratchbook.wav`: 19.3 seconds, 48 kHz stereo float, peak −7.04 dBFS, RMS −20.67 dBFS.
- `Scratch voice only.wav`: isolated scratch stem, peak −9.32 dBFS.
- `README.txt` and `phrases.json`: timeline, exact parameters and formulas.
- `Scratchbook-performance.screamseq`: orders `[1,1,1,1]`, extending the actual scratch workload to 76.8 seconds for sustained checks.

All demo audio is deterministic original synthesis, including the voice-like sample; it is not a recording of a person. Every phrase produced finite, nonzero audio, with no clipped samples. These checks establish signal integrity; they are not a claim of subjective loudspeaker listening qualification.

## Boundaries and follow-up work

- This operates on sample voices, including sample instruments. Scratching plugin/bus output would require a separate rolling-buffer design with explicit latency and lookback.
- The bank is song-local. Cross-song reuse works through pattern copying; dedicated app-catalogue publishing and hardware jog-wheel/fader recording are not implemented.
- Windows shares the musical behavior, inline commands, API and persistence. Its separate paired-curve editor and native desktop/device qualification remain future work. Portable Windows tests passing on Mac do not establish a native Windows build result.
- Pattern-command insertion/editing follows the existing path and can stop playback. Live editing here refers specifically to phrase-bank curves and their history.
- Make unique currently creates the bank copy and reassigns the cell as two guarded history entries. One Undo restores the cell's former phrase; a second removes the unused copy. Ordinary curve edits and clipboard imports each use one document transaction.
- During scratching, sample loops are traversed physically and sample ends are held; normal loop behavior resumes on release. The editor does not yet overlay the source waveform or current scratch phase.
- Limits are 255 song phrases, 256 nodes per lane and 16 tracks using SK per prepared song. Scripts retain the shared bounded expression language; they are not arbitrary executable code.
