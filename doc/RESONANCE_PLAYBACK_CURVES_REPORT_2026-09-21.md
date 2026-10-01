# Resonance playback and scripted automation — 21 September 2026

The requested transport shortcuts, loop control, inspector audition and parameter-automation improvements are implemented. The ready build is preserved at `bin/mac-checkpoints/2026-09-21-playback-formulas/Resonance.app`. The previously running application and its unsaved song were left untouched. Save that session before switching builds.

## What changed

- Shift–Space plays the song from the cursor. Control–Space plays selected rows, or the current pattern without a selection. Control–Shift–Space starts that range at the cursor. Selected rows play across all channels. A cursor outside the range falls back to its beginning.
- **Loop**, beside **Follow**, repeats whichever song, pattern or selection is playing. Changes during playback take effect at a musical boundary. Range endings follow actual row processing, including tempo changes.
- Playback keys work across the workspace, with local text entry, sample-browser preview, control activation and explicit command bindings taking precedence.
- Normal note keys audition the focused sample/instrument inspector without writing pattern notes. Releases remain associated with the original asset after focus or selection changes. Direct sample audition works even when an unrelated tracker instrument occupies the same slot.
- Parameter automation has time/value zoom, time panning, Fit, and an explicit **Selected point → next** curve selector. Applying an edit retains the parameter, selected node and viewport.
- **Scripted** segments have a formula field, live preview, syntax feedback, normalized progress, start/end values, beat context, math functions, oscillators, conditionals and deterministic noise. A final scripted node extends to pattern end. Formula preview and audio use the same evaluator.
- Formulas compile before playback into bounded instructions. Continuous plugin parameters receive per-sample ramps between evaluations on a 32-sample clock. Callback evaluation does not allocate or lock. Discrete parameters stay discrete.
- Formulas survive native save/reopen, Undo/Redo, copy/paste and shifts. Invalid edits leave the saved document unchanged. Formulas require native metadata version 11; older builds cannot open projects using this extension.
- Agent access includes revision-guarded `transport.play`, `.stop`, `.loop`, read-only `transport.get` and `automation.formula.preview`, and scripted point data in `automation.pattern.get/set`. Schema and API discovery are updated.
- Text fields now support the standard Edit → Select All command.

The [usage guide](../mac/PLAYBACK_AND_CURVES.md) includes shortcuts, zoom gestures, formula variables/functions, examples and API shapes. It is also bundled with the application.

## Verification

Evidence is preserved in `bin/mac-checkpoints/2026-09-21-playback-formulas/evidence/`.

| Check | Result |
| --- | --- |
| Complete CTest regression suite | 70/70 passed |
| Final transport, formula, formula-session and musical-automation checks | 4/4 passed after final fixes |
| Native interface suite, rerun against final sources | Passed |
| Existing local socket API and workspace integration suites | Passed |
| Expression compiler/evaluator under address and undefined-behavior sanitizers | Passed |
| Stock engine audio fidelity | 30 comparisons, maximum and RMS error zero; no intercepted callback allocation, release or locks |
| Core Audio virtual output/capture | Dry, AU effect/instrument, VST3 effect/instrument with automation and graph playback all matched reference PCM exactly; zero callback overruns |
| Scripted automation audio | Buffer-size-independent output at 44.1, 48 and 96 kHz; blocks from 1 to 4,096 samples |
| Range playback | Exact row boundaries, cursor starts, repeated loops, live loop disable and tempo-changing selections |

Live application inspection exercised the new shortcuts while an inspector held focus, selection looping from a cursor, sample audition, curve changes, formula errors/corrections, Apply and graph zoom. A separate disposable instance was used throughout. The virtual-device run included 30 seconds of graph processing and did not change the system's speaker routing.

## Remaining qualification and limits

The sustained display-presentation test could not begin: macOS reported its window as occluded, even with the test application active and its window raised. The preserved report has `measurementStarted: false`. This is an unmet test prerequisite, **not a 60 fps pass or a measured performance failure**. Repeat `mac/ui-test.sh` with a visible desktop to finish that qualification.

Scripted curves currently apply to parameter automation. Legacy tracker instrument volume/pan/pitch envelopes retain their existing representation. Formulas are bounded musical expressions, not JavaScript or a Parseq-compatible runtime. Flip/scale/humanize operations reject scripted segments rather than silently changing their meaning; those transformations can be expressed in the formula. Rapid/discontinuous formulas are limited by the 32-sample evaluation clock. Plugin-specific smoothing and physical speaker/DAC behavior are not established by virtual loopback tests.
