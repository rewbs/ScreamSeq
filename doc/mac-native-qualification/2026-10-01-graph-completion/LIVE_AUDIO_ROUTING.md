# Live audio routing checkpoint — 2026-10-02

This supplements the earlier completion ledger with the current shared scheduler and macOS hosted regressions. Native Windows execution and final packaged-app qualification are recorded separately. No display/FPS claim is made here.

## Implemented and rendered

- Real rack plugin instruments can receive supported main and auxiliary audio inputs. They run in the prepared processor DAG, after their actual input/follower dependencies, while their retained MIDI/event queues and vendor instance advance once. Stereo-pair ports preserve the provider's physical-channel mapping. Audio feedback is rejected before publication.
- Root followers can read the current output of a scheduled plugin instrument. A prepared dry transition reuses the actual instrument output for its shadow rather than rendering MIDI twice.
- Root group dry maps can select an ingress later than an existing wet egress. The compiler adds only the required path padding and separates input-capture order from processor-output order, so two parallel branches may exchange dry inputs without a fictitious feedback cycle.
- Stopped latency refresh rebuilds group boundary delay buffers as well as mixer compensation. Group identity, bypass state and the modulation callback remain intact; replacement storage is allocated and checked on the control owner.

The normal checkpoint passed ten suites: `native-mixer`, `native-signal-graph`, `plugin-latency`, `mixer-publication`, `mixer-plugin-routing`, `song-group-runtime`, `signal-graph-session`, and the portable Windows graph, mixer and metadata suites. Logs: [native group](logs/group-input-pdc-native.log), [other nine](logs/group-input-pdc-tests.log), [instrument inputs](logs/instrument-input-native.log).

The instrument fixture uses actual AU and VST3 providers at 44.1/48/96 kHz with 17/512/4096-frame caller blocks. It patches main and sidechain inputs during playback, changes a current-block follower contribution, removes routes, and compares settled PCM with an independently rendered channel. Normal builds audit callback allocations, frees and locks.

The group fixture uses actual VST3 delay rings, two branches of different depths and crossed explicit dry mappings. At 44.1/48/96 kHz and 17/128/4096-frame caller blocks, it compares both wet and bypassed PCM against independent delayed samples, including stopped latency change from 13 to 7 samples per vendor. The pure fixture also requires bit-identical callback partitioning and exact added path padding.

## Qualification boundaries

The instrument-input PCM fixture has no assigned MIDI notes; routed held-note and note-on timing have separate fixtures. The combination of MIDI-generated audio with a nonzero-latency incoming audio path has not yet been qualified for relative note/audio onset alignment. No timing policy change for that combination is implied.

These are offline, hosted rendering tests. They do not establish physical-device latency, commercial-plugin capacity, UI presentation timing, or whole-app loopback behavior. ASan/UBSan and the final typed stage-follower checkpoint are recorded below when complete.

## Typed aggregate-stage follower checkpoint

A root follower can now select a declared auxiliary output of a channel graph stage through `audioStage` plus `output`. The saved model retains that typed identity; the prepared host resolves it to the current stage processor. A follower alone activates the required output buffer. Main and auxiliary PCM are delivered before the dependent parameter target runs. The tap denotes the summed audible/tail contributions of that stage, not an individual reusable copy or the channel's main mix.

The final normal pass is **12/12**: [native three](logs/stage-follower-native.log) and [model/API nine](logs/stage-follower-adapters.log). The new shared hosted fixture runs actual AU/VST3 at 44.1/48/96 kHz with 17/128/4096-frame caller blocks, using distinct main/output-1/output-2 gains (0.5/0.25/0.75). It independently computes the per-sample attack/release envelope and final parameter-modulated PCM, then exercises a live cut, retarget, restoration and feedback rejection. It checks callback allocation/free/lock counters and unchanged source transport. Both platform adapter suites cover typed validation, exact cut, Undo/Redo and persistence. The same hosted fixture is registered for native Windows CI; local portable Windows tests do not replace that platform run.

An initial regression correctly caught missing follower delivery in the synthetic graph-processor branch. The host now delivers the actual stage PCM through the same current-block follower path used by rack plugins; the final oracle passes without substituting the bus mix.

## Sanitizer repair and adapter capacity

The first ASan/UBSan pass found a real container overflow while attaching a song with detached chains. Runtime compilation appends silent chain roots to the projected mixer, but adapter installation indexed the shorter persisted bus vector with a runtime bus index. The corrected lookup uses the prepared runtime bus identity and kind. A synthetic Return root therefore cannot accidentally claim a tracker channel, in addition to removing the out-of-bounds read.

The same audit found that the native adapter reservation counted only persisted buses. It now includes projected roots before reserving MIDI generators and sample-graph adapters, and checks the final runtime graph and schedule cardinalities before installing adapters. Detached chains alone also cause prepared-only implicit mixer materialization. No saved mixer is changed by playback preparation.

A new regression renders both an implicit song with a detached chain and a song with 64 detached chains plus 50 Return buses, checking channel bindings and the 250-adapter bound. The post-repair normal `native-mixer`, `native-source-migration` and `native-signal-graph` suites pass **3/3** in 14.45 seconds: [normal results](logs/projected-adapter-tests.log). The [original sanitizer failure](logs/sanitizer-before-adapter-fix.log) is retained.

The isolated ASan/UBSan pass is now **12/12**, covering `mixer-publication`, `native-mixer`, `rack-preset`, `multi-bus`, `plugin-latency`, `native-note-routing`, `native-source-migration`, `song-modulation`, `native-signal-graph`, `signal-group-runtime`, `song-group-runtime` and `mixer-plugin-routing`. The repaired mixer and signal suites pass, including the original overflowing fixture and the new adapter-capacity regression. The [12-suite run](logs/sanitizer-runtime-tests.log) took 103.87 seconds. Four hosted binaries were then relinked to the repaired static archive and [rerun successfully](logs/sanitizer-relinked-host-tests.log), 4/4 in 25.04 seconds; all 12 results therefore cover their applicable current source. [Source/archive/executable hashes](logs/sanitizer-runtime-sha256.txt) identify the repaired checkpoint.

The build uses `-fsanitize=address,undefined -fno-omit-frame-pointer`; execution uses `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. This establishes no detected address/undefined-behavior failure in these fixtures. Leak checking and thread sanitization are not claimed. The normal test build, not the sanitizer stubs, supplies callback allocation/free/lock auditing. All runs were offline and did not open an audio device or change the musician's session.
