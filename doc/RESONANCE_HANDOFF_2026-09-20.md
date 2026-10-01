# Resonance — development handoff, 20 September 2026

Development is paused at your request, at a working checkpoint after native reverse-loop support. The application builds, starts, and passes the current automated regression suite. This is a usable development build with substantial new functionality; the requested Renoise-inspired expansion remains incomplete.

**Application:** [Resonance.app](../bin/mac-native/Resonance.app)  
**User guide:** [mac/README.md](../mac/README.md)  
**Agent API:** [mac/AUTOMATION.md](../mac/AUTOMATION.md)  
**Complete 79-group status ledger:** [RESONANCE_EXPANSION_PROGRESS.md](RESONANCE_EXPANSION_PROGRESS.md)

Open the application above from Finder. The build is for this Mac, requires macOS 14 or newer, and is locally ad-hoc signed. It is not a notarized public release. Save work as `.resonance` to preserve native features; exporting a legacy module deliberately rejects detected data loss.

## What is available

The following includes both the original Mac tracker foundation and the additions made during the expansion. “Available” describes implemented behavior, not completion of every corresponding Renoise feature group.

| Area | Implemented at this checkpoint |
|---|---|
| Pattern editing | Native Metal tracker; keyboard entry, selections, clipboard, transpose and row operations. Shared masked/scoped tools for mix/merge paste, flip, expand/shrink, rotate/nudge, instrument remapping/swapping, curved interpolation, scaling, seeded randomization and numeric humanization. Exact previews and one-step undo for bulk operations. |
| Arrangement | Reusable patterns and order editing; named song sections, navigation, pattern notes and stable track/pattern identities. Paged track-by-order matrix and independent block copying. Matrix drag-to-copy is implemented and tested automatically, with live drag inspection still outstanding. |
| Automation | Recording of plugin/native-control gestures and sample-position playback/export. Pattern-relative envelopes repeat with patterns and follow musical timing. Native target search, point editing, exact values, snapping, ramps and five curve shapes. Stable target IDs, document history and persistence. |
| Mixing | Stereo routing graph; track/group/return inserts; acyclic nested groups; pre/post sends; gain, pan/balance, width, mute/solo and meters. AU/VST3 mono/stereo auxiliary outputs and sidechains, including native dynamics detectors. Graph delay compensation and per-track timing offsets. |
| Plugins | macOS AU and VST3 effects and instruments, custom AU Cocoa/VST3 NSView interfaces, searchable generic parameters, state recall, instrument assignments, automation and rack history. Rack capacity is 64 devices. Cached discovery across launches, explicit Rescan and isolated validation before adding a plugin. |
| Sampling | Import, waveform selection/zoom/pan/drawing, clipboard cut/copy/delete and insert/overwrite/mix/replace paste, rate/layout conversion, copy-to-new, channel operations, normalization, reverse, fades, gain, phase inversion, silence, trim, DC removal and smoothing. Zero-crossing/frame-grid snapping, loop crossfades, and normal/sustain forward, ping-pong and reverse loops. Exact previews, compact undo and native sample persistence. |
| Instruments | Sample-based instrument creation, ITI/XI import, note-to-sample mapping, volume/pan/pitch/filter envelopes with loop/sustain controls, and new-note/duplicate-note behavior. AU/VST3 instruments respond to tracker notes, keyboard audition and CoreMIDI input. |
| Built-in effects | Fifteen native devices: Gainer, DC Offset, Stereo Expander, Digital Filter, EQ5, EQ10, Mixer EQ, Comb Filter, Distortion, Cabinet Simulator, LofiMat, Compressor, Bus Compressor, Gate and Maximizer. Controls share the API, automation, state, history and routing systems. |

### Application and audio foundation

- MOD, XM, S3M, IT and MPTM are editable; other supported legacy formats are playback/preview only. Source-format limits and compatibility behavior still apply.
- Direct CoreAudio output, sample audition, CoreMIDI input, row-quantized step/live recording and native keyboard controls are present.
- Native project version 4 retains exact 8-/16-bit sample PCM, sample headers and instrument key maps, together with arrangement metadata, routing, plugin state and automation. Versions 1–3 remain readable.
- Recovery keeps three rotating generations. Export produces stereo float32 WAV at 48 kHz, compensating reported latency and retaining effect tails. Failed/over-limit exports preserve the previous destination.
- Structural sample/instrument/order/rack changes stop transport before replacing assets. Live pattern edits and suitable parameter gestures can continue during playback. Plugin/rack history and document history remain separate domains.

### Agent-driven editing

The local API can inspect the current song, selection and cursor, then read or mutate patterns, arrangement, samples, instruments, mixer routing, plugin state/parameters and automation. New musical operations have continued to use the same revision-checked command paths as the native controls.

Bulk operations expose previews; commits reject stale revisions, and supported requests deduplicate retries. The bundled Python client includes the crescendo drum-roll example. The machine-readable schema and advertised capabilities have been kept in step with the implementation. The latest additions are `sample.loops.set` reverse modes and corresponding sample metadata/history support.

Enable **Automation → Enable Local API** in the app to let an agent connect. This is a private local interface; it does not require giving the agent mouse control for ordinary song edits.

### Plugin cache and Battery 4

The previously reported problems were investigated and fixed:

- The picker reuses a persisted inventory and offers explicit Rescan. A measured 75-plugin inventory took 30.5 seconds to rescan, approximately 1.5 ms from the in-memory cache, and 285 ms on the first cached request after restarting the hidden app.
- Battery AU's crash was traced to its factory being invoked off the main thread. AU lifecycle/state/property operations now run on the main thread.
- Battery VST3 was rejected by the previous audio-bus limit. The host now supplies complete bus arrays and supports up to 64 declared buses per direction; supported auxiliary outputs can subsequently be routed through the expanded mixer.
- Installed Battery 4 **4.3.0**, AU and VST3, passed creation, MIDI/render/state probes and hidden actual-app add/restore/remove tests. These tests used default state; loaded kits, every preset, vendor-specific interfaces and long sessions are not fully qualified.

Detailed evidence: [Battery qualification](mac-native-qualification/2026-09-19-battery/README.md). Runtime plugins still run inside the application, so general protection against a crashing plugin is unfinished.

## The final feature completed before pausing

Normal and sustain loops now independently support reverse traversal. Playback runs forward through the attack, reaches the loop end, then repeats that region backward. The endpoint convention is explicit: the last frame repeats once at the first turn. This uses a separate opt-in native traversal path with bounded scratch storage and chronological interpolation, leaving ordinary legacy playback unchanged.

The new modes have native controls, API previews/commits, compact undo/redo, sample copying/splicing support, saved-song persistence and module-export loss protection. Reverse and ping-pong modes are mutually exclusive. Crossfading currently accepts forward loops only. Sustain release transfers from the current physical sample position into the existing release behavior. **Explicit Note-Off “stop immediately / finish this loop” policy is still pending.**

Native reverse loops use an optional versioned sample-snapshot extension. Current builds read older snapshots; older builds reject snapshots containing this extension rather than silently changing their sound. Keep reverse-loop projects with this build or a newer one. See [snapshot format and compatibility](../mac/SAMPLE_SNAPSHOTS.md).

## Verification at the stopping point

| Check | Result and scope |
|---|---|
| Complete application | Release build and packaging succeeded. Actual app startup loaded a 127-channel, 32-order fixture and exposed ready UI context/API. |
| Regression | **43/43** CTest groups passed. |
| Memory/undefined behavior | **7/7** targeted sanitizer groups passed: reverse loops, loop editing, crossfades, sample copying, clipboard, archive and actual session integration. This checkpoint did not rerun the entire sanitizer suite; broader earlier results remain in the ledger. |
| Legacy audio fidelity | **30/30** separate stock-libopenmpt comparisons matched exactly, including frame counts, at 44.1/48/96 kHz. Audited callbacks reported zero intercepted allocations, releases or mutex calls. |
| Reverse-loop audio | **800** independent PCM/unrolled-waveform comparisons passed across five source formats, five output rates, both PCM depths, mono/stereo, normal/sustain loops and short/long loop lengths. Different callback sizes agreed exactly. **660,000** traversal/interpolation-tap comparisons passed. |
| Saved loop projects | **320** loop mode/layout/format history/audio cases and **80** production API → native save/reopen → complete WAV reference comparisons passed. |
| Agent API | Both the standalone test host and hidden packaged app passed the complete socket tests, including reverse modes, previews, strict validation, stale revisions, retry behavior and undo. |
| Native controls | Automated interface/gesture-order tests passed. The sample panel was freshly rendered and inspected. In the running development app, Reverse → Preview → Apply succeeded and native Undo visibly restored the original forward loop. |
| Physical audio integration | A final silent CoreAudio session passed, including preview/no-op retention, actual sample edits stopping safely, and reverse-loop undo/redo. This was a short integration check, not a sustained timing qualification. |
| Build identity | The final source manifest and local signature are preserved with the checkpoint evidence below. |

Evidence is preserved in [2026-09-20 checkpoint qualification](mac-native-qualification/2026-09-20-checkpoint/README.md), rather than depending only on temporary logs.

### Quality gates still open

**Sustained smooth 60 fps is not yet demonstrated.** The renderer uses a display link and background encoding, but recent visible runs reached about 59.48/59.83 fps with occasional 50–83 ms gaps. Earlier long combined testing also recorded an audio deadline overrun. These results are not waived. The final sustained display and 30-minute combined UI/audio gates have not passed.

A later, scoped **one-minute audio-only workload** with 127 tracks, twenty mixed devices, four groups, two returns, four instrument outputs, six sidechains and concurrent control edits passed with zero overruns at 48 kHz / 128 frames. Its maximum callback was 1,542.83 μs against a 2,666.7 μs deadline. That is useful evidence for the tested graph, not a guarantee for arbitrary 64-plugin projects.

The current sample-loop workflow was inspected live, but many newer mixer/effect/automation workflows still need full live interaction checks. Automated offscreen tests do not certify sustained presentation, arbitrary drag gestures or every third-party editor. Broader real-song, device, plugin and OS coverage remains necessary.

## What remains to implement

These are the major outstanding items from the original scope. The linked 79-group ledger retains each individual requirement and its precise partial status; a baseline marked “Present” does not mean full Renoise-level parity.

| Area | Remaining work |
|---|---|
| Pattern editing — A01–A15 | Multiple polyphonic note columns per named track; independent column mute; richer volume/pan/delay/effect columns; sub-row note timing and accurate live recording; mono/poly/chord recording and quantization; effect-command assistance; probability/exclusive triggers/ghost behavior; fractional timing controls and tempo automation; groove/swing; metronome/count-in. Existing entry/navigation/effect behavior still needs the full requested parity/UX qualification. Logical group scope depends on the richer track model. |
| Arrangement — B01–B07 | Block aliases, per-order-slot mute, queued pattern/section changes at musical boundaries, richer block/section/range loops, track duplication/reordering and hierarchical arrangement. Complete live matrix-drag inspection and polish the existing order workflows. Audio mixer groups already exist; they are not yet the complete hierarchical arrangement model. |
| Automation — G01–G06 | Envelope clipboard and repeat/insert paste; shift/flip, sine generation and timing humanization; comprehensive seek/chase at arbitrary rows and muted clips; pattern commands for effect parameters; target learn and future non-plugin automation destinations. Full gesture and sustained live qualification remains. |
| Mixing — E01–E06, E13 | Full strip overview/selected-effect controls, multiband sends, independent hardware output pairs and surround auxiliary layouts. Dynamic graph changes need safe renegotiation. Current stereo routing/PDC needs the sustained combined acceptance run. |
| Plugins — E07–E12 | Runtime process isolation/crash recovery; aliases, multitimbral MIDI channels and MIDI-controlled effects; plugin MIDI-output routing; favorites/categories/hidden entries/preset browsing; automatic suspension; dynamic parameter/bus/latency handling. Broader vendor/kit/editor testing. AUv3 view-controller interfaces are absent. VST2 and Windows plugins are not implemented; Windows hosting remains outside the requested direction. |
| Sampling — C01–C12 | Explicit loop-finish policy; musical/offset/slice-marker snapping; manual/transient slicing and nondestructive aliases; slice-to-pattern conversion; repitch beat sync and pitch-preserving stretch; complete one-shot/fade/seek/interpolation controls; microphone/line recording, monitoring and takes; pattern-synced recording with latency correction; rendering effect chains into samples; high-resolution sample storage and multisampling. |
| Instruments — D01–D14 | Graphical keyzones, velocity layers, overlapping/release/round-robin/random zones; choke groups and full voice policies; modulation sets; per-sample/shared effect chains; multi-target macros; scale/legato/glide controls; Scala/MTS-ESP tuning; combined sample/plugin/external-MIDI instruments; phrases, phrase generation/baking/presets/live coding; complete asset duplication/reordering/remapping/cleanup/preset workflows. |
| Built-in effects — F01–F12 | Analog Filter and Exciter; advanced digital filter models, adjustable smoothing/oversampling and EQ graph/mirrored-channel options; Delay/Multitap/Repeater; Reverb/mpReverb/Convolver; Chorus/Flanger/Phaser/Ringmod; instrument control devices; Hydra/Meta Mixer/XY Pad; LFO/Key/Velocity/Signal Follower devices; Formula; macro racks and parallel/mid-side/frequency splitters; Notepad. Implemented devices still need broader live and sustained qualification. |

## Suggested order when development resumes

1. Close the display-pacing and combined audio/UI quality gates, then inspect the newer mixer, effects, automation and sample gestures on a stable visible desktop.
2. Finish sample release/loop-finish semantics and the remaining sampler controls, followed by slicing/recording and the native instrument model.
3. Introduce the richer logical track/note-column/timing model needed by advanced pattern editing and hierarchical arrangement.
4. Complete plugin crash isolation/dynamic changes and automation editing/chase; continue the remaining native effect families.

Every resumed feature should continue to ship through the shared API, persistence and undo paths with independent audio checks where relevant. This ordering is a handoff recommendation, not a scheduled restart.

## Workspace state

The code remains in the existing checkout at `/Users/rewbs/code/openmpt`, including the substantial pre-existing uncommitted/untracked Mac implementation. No reset, cleanup, commit, push or pull request was performed for this handoff. The local signed application is the deliverable; development and qualification processes were closed before returning the system. The implementation goal is paused, not complete, and will wait for you to resume it.
