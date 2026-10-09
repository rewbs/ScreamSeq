# ScreamSeq graph redesign — stopping-point report

2 October 2026. Paused at the user's request. The graph implementation has advanced substantially beyond the 1 October checkpoint. **This is a tested development checkpoint, not a complete release qualification.**

## Completed

- **Notes/MIDI routing:** editable typed Notes cables, stable routing identities, MIDI-channel mapping and held-note ownership. Only plugin-instrument notes are forwarded. Removing a destination releases its owned notes/pedals; adding or restoring one waits for the next note-on.
- **Live graph editing:** prepared structural changes, instrument inputs/assignments, supported bus activation and latency handling, retained processor state, and rejection that preserves the previous audible graph and Undo history.
- **Physical audio ports:** real Main/sidechain/auxiliary inputs and outputs, deterministic stereo pairs and odd mono slices, plugin-to-plugin routing, instrument audio inputs and aggregate channel-stage ports. Followers can use actual plugin/stage outputs.
- **Graph actions:** processing-group dry-path bypass, source mute, ordered detached chains, exact cable cuts, branch-aware healing, recipe clipboard, make-independent, processor duplication and compatible live effect presets.
- **Observation:** exact processor-copy selection, port/cable scope and spectrum, temporary Listen, Notes ownership counters and parameter-source navigation. Live recorded-automation editing supports Undo/Redo.
- **Final usability/correctness fixes:** adding effects preserves group dry paths and downstream placement; recorded-point fields refresh coherently; instrument navigation reveals filtered-out targets and selects the appropriate instrument copy; group detachment preserves only valid dry boundaries; connected auxiliary ports report their accepted routing membership; followers receive valid silent PCM when their instrument is idle or a retained transition source is silent.
- **Shared contracts:** musical behavior, validation, guarded APIs, Undo and persistence are maintained across Mac and Windows adapters. The retained OpenMPT engine and attribution remain intact.

The [full implementation report](FINAL_REPORT.md) describes the supported behavior and boundaries. The [journey ledger](JOURNEY_WALK.md) records measured actions, panel switches and redundant confirms, without inventing unmeasured baselines. The [22-image gallery](index.html) identifies the executable checkpoint for every screenshot and marks defects corrected afterward.

## Verification

- **Final source `684a5023a`: full Mac build/signature verification, 102/102 CTest suites (138.90 seconds), and actual packaged-app API/workspace checks passed.** Build inputs and executable identity are retained. Earlier corrected-runtime checks also passed the separate host API and normal application-shutdown regression.
- **Six Core Audio loopback cases passed** at 48 kHz / 512 frames, each with 96,000 captured stereo frames, zero maximum/RMS PCM error and zero callback overruns.
- Installed **Replika and Serum 2, AU and VST3, passed all four** bounded commercial-plugin audio/transition tests with nonzero output.
- A sanitizer-discovered projected-bus overflow was repaired. **12/12 applicable ASan/UBSan suites passed** on the corrected runtime; leak/TSan qualification is not claimed.
- Windows commit `261b15387` passed **36/36 portable suites and 47/47 native workers**, including actual VST3 PCM rendering. See [Windows evidence](WINDOWS_NATIVE.md).
- The later native Mac walkthrough covered grouped Add/bypass, live recorded points, Notes ownership/MIDI mapping, all five wide-fixture input/output slices and physical instrument Main/aux inputs. **275 samples over 21 minutes** all reported playback active, no faults and zero overruns. Polling does not prove gap-free PCM or 60 fps.
- The final focused audio/API suites passed **4/4**; copied AppKit regressions passed with all **158 Swift/bridge inputs matching**. [The full report](FINAL_REPORT.md) identifies each checkpoint; earlier audio/sanitizer passes do not silently qualify later source.
- Final Windows CI is **in progress** for exact source `684a5023a`: [run 36960036036](https://github.com/rewbs/ScreamSeq/actions/runs/36960036036).

## Left to do

1. **Rewalk the last corrections in the actual Mac app:** recorded-point detail refresh; filtered instrument navigation and automatic copy selection; explicit-dry-path group detachment; automatically activated port appearance. The desktop became unavailable during testing. Automated regressions pass, but these visual checks are still open.
2. **Finish the remaining full-journey visual rechecks:** fresh group/name/export; branched detach's explicit healing choice; endpoint repatch/history; the complete new-LFO path; confirmed overload diagnosis; working-zoom multichannel labels; and the full pattern/instrument/return regression. Earlier checkpoints cover parts of these journeys; the ledger states exactly which.
3. **Investigate one unreproduced playback stop** in the earlier `e42639558` native walk. There was no reported fault or overrun, and no recorded Stop/Space action explains it. The later 21-minute pass is separate evidence, not a proven fix for that event.
4. **Finish native Windows qualification:** collect the final followup CI result; desktop/HWND/WASAPI/device interaction checks remain deferred by the user.
5. **Return to the strict 60 fps gate.** Explicitly excluded from this pass. The previous measured failure remains a failure; no threshold was relaxed.
6. **Resolve two design policies:** live opaque instrument-preset replacement with held notes, and external routing to a particular dynamic reusable copy while that copy is absent. Instrument opaque presets remain stopped-only. Aggregate stage routing is available, but it is not exact-copy external routing.

Other deliberate limits: feedback cycles are rejected; arbitrary root/mixed-control clipboard/export is not supported by the recipe clipboard; unsupported or changed physical plugin layouts reject safely; vendor GUI drag-target support is not universal. These are stated in the full report rather than presented as finished capabilities.

## Handoff

Work is on `codex/graph-completion`; final implementation commit: [`684a5023a`](https://github.com/rewbs/ScreamSeq/commit/684a5023acda1a1823341d2387ee1a8040e2ef91). No merge to `main` is part of this pause. The private QA song was saved, playback explicitly stopped, and only its process closed. The musician's session, bundle and system audio defaults were not replaced. Build products and private songs remain outside Git. Source, regression tests, reports and compact evidence are committed and pushed for resumption.
