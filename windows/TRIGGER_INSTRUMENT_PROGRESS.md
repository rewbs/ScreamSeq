# Windows trigger instruments and Surge XT — 2026-09-21

Full Mac parity remains active. This checkpoint implements empty trigger
creation and tests an installed Windows instrument through the native rack,
worker, saved project, offline renderer and WASAPI. It does not close the
remaining plugin, graph, pattern, workspace or release gates in `PARITY_PLAN.md`.

## Implemented

`instrument.create(empty:true,name?,dryRun?)` now follows the current Mac API.
It validates a complete candidate before committing, rejects mapped samples,
unsupported module formats and full slots, and appends a genuinely empty
keymap. Converting a sample-only song first creates matching instruments for
every original sample number. Existing pattern bytes and sample playback are
preserved. Creation is one document Undo step with stable IDs through Redo and
native save/reopen.

The native rack's **New trigger** action creates the instrument and assigns it
to the selected instrument plugin on MIDI channel 1. Assignment remains a
separate plugin-history step, as on Mac. A failed assignment retains the new
instrument's stable identity for **Retry assign**, avoiding duplicate creation.
The command palette exposes the action. Long plugin names use a bounded
fallback name. Existing sample-backed instrument creation remains available.

## Installed instrument

Surge XT's official ARM64 beta, pinned to the 2026-09-18 nightly commit
`58914e59c608ed4384ba6002e44c3465c58b2e71`, was downloaded from its
[publisher release](https://github.com/surge-synthesizer/surge/releases/tag/Nightly).
This is the native ARM64 **NO_LUA beta**, not the stable x64 distribution.
Publisher SHA-256 digests were verified for both archives:

- Plugin-only ZIP: `cf039b41b6b19d025fbda4bc7597b6a9d43f621c87a934e26be603a98ee46140`.
- Portable factory content: `e7ab155ef41861190eefc71014925e35f5991964f197e317069ba1af8201158a`.
- Installed VST3 binary: `ee569bffae488bf03a5796cf0e7b2b869ff14a5bbb46b0e663856896bbabcd2e`.

Only its instrument VST3 was installed in the private per-user QA directory
`%LOCALAPPDATA%/Programs/Common/VST3/ScreamSeq-QA/SurgeXT-20260918/`.
Adjacent portable `SurgeXTData` and `SurgeXTUserData` folders isolate factory
content and plugin preferences. The scanner verifies native ARM64 machine type
and class `ABCDEF019182FAEB566D624153675854`. The private `surge-cache.json`
does not replace the existing two-effect cache. Original archives, publisher
metadata and hashes remain under `bin/windows-plugin-qualification/downloads/`.

## Qualification and investigated failures

The final ARM64 executable SHA-256 is
`18ca1ad9db5cac11cd1f3d69688501375938f9ed0d661995300d1696d26e7199`.
The packaged checkpoint records its exact source commit/tree and evidence hashes.

- The full Windows Python suite discovered 66 tests: **65 passed**, with the
  separate hardware opt-in skipped. This includes both installed effects and
  Surge's actual native trigger button, empty mapping, assignment, separate
  histories, editor close and exact saved-state reopen.
- Worker conversion tests produced identical PCM at 44.1, 48 and 96 kHz before
  and after sample-only conversion. Dry-run, pattern conservation, Undo/Redo
  and stable saved identities passed.
- Direct Surge lifecycle checks passed at all three rates: note rendering,
  parameter edit, opaque state, concurrent restoration and removal. Native
  editor qualification completed four open/close/destruction cycles at 48 kHz.
- Surge advertises integer controls such as `B LFO 4 Trigger Mode` as continuous.
  A requested endpoint of 1 returns 0.995 after cold restoration. Its
  [parameter conversion](https://github.com/surge-synthesizer/surge/blob/58914e59c608ed4384ba6002e44c3465c58b2e71/src/common/Parameter.h#L624)
  maps integers into 0.005–0.995. The lifecycle probe accepts canonical readback
  only for its explicitly edited parameter, with identical complete saved
  bytes both after cold restoration and after replaying the canonical value.
  Unrelated drift and changed state still fail. No provider behavior or general
  numeric threshold was changed to accommodate this plugin.
- The initial Init Saw project failed PCM comparison (`maxPartitionDelta`
  0.512805089355). A direct-provider control also differed when repeating the
  *same* 512-frame size, excluding a partition-only explanation. Enabling the
  six oscillator Retrigger parameters in a disposable fixture produced **zero
  PCM difference** at 44.1/48/96 kHz for 17/128/4096/8193-frame requests. The
  original failing fixture and reports remain preserved; plugin defaults were
  not modified. The provider probe now sends MIDI to instrument fixtures and
  rejects silent instrument output.
- The deterministic fixture contains only plugin trigger notes and key-offs
  throughout every pattern. A held-note control matches the release fixture's
  first quarter-second exactly; after tracker key-off the last quarter-second
  has less than 20% of the held control's energy. Onset is within 20 ms at all
  tested rates/partitions. Both complete project views remain unchanged by DSP.
- The first native editor open changes Surge's opaque component state solely
  from `<instanceZoomFactor v="-1"/>` to `200`, with no parameter change. The
  current opaque-state policy consequently **stops playback**. This remains a
  host limitation; the state was not discarded or treated as a parameter edit.
  The failed run is retained in `bin/windows-surge-audio.log`.
- After explicitly initializing/saving that editor state while stopped, the
  instrument ran **21.05 seconds** at 48 kHz / 480-frame WASAPI periods with its
  editor open: 2,106 callbacks, maximum callback 2,178.6 microseconds, zero
  deadline overruns, starvation indicators and device/MMCSS errors. Closing
  the editor preserved active playback and exact saved baseline. The output
  was silenced after full DSP; this is not a physical speaker test.

Reproduce the audio check with `Tests/qualify_trigger_instrument.py`, passing
absolute `--exe`, `--cache`, `--project` and a fresh `--output` directory. Its
input is the disposable project retained by `test_plugins_app.py` using
`SCREAMSEQ_TEST_INSTRUMENT_CACHE` and `SCREAMSEQ_PLUGIN_EVIDENCE_DIR`.
The script explicitly records the stopped editor initialization and keeps
the input project unchanged.

Evidence: `bin/windows-trigger-{final-build,regressions,final-worker}.log`,
`bin/windows-surge-{qualified-lifecycle,ui-lifecycle,audio-initialized}.log`,
`bin/windows-plugin-qualification/surge-{qualified-lifecycle,ui-lifecycle,
partition-probe}.json`, and `surge-audio-initialized/result.json` in that folder.

## Remaining limits

Opaque live-state replacement is still required, including editor-only state
changes which vendors store in their component blob. Program/port/alias controls,
presets/library, missing-plugin recovery, full routing and unified FX/precise
note workflows remain in the parity plan. OrbitCab's partition discrepancy is
still unresolved. The Surge result does not waive it.

The native accessibility tree exposed the new trigger/assignment controls and
correct selected instrument. Foreground activation failed with
`GetCursorPos failed: Access is denied. (0x80070005)`; the retry's image showed
only the desktop background. This is **not** new visual qualification. No
sustained presentation, full allocation/free/lock audit, x64 build, commercial
instrument matrix or reciprocal Mac reopen is claimed. All QA processes were
task-owned; no musician session or system audio defaults were changed.
