# Native plugin qualification — 19 September 2026

Local Apple Silicon development build of Resonance, an independent OpenMPT derivative. Scope: macOS AU/VST3 effects and instruments, custom plugin windows, parameter recording and playback, project persistence, latency alignment and WAV export. This is not broad vendor or hardware qualification.

## Passing checks

| Check | Evidence | Result |
|---|---|---|
| Core, plugins, session, MIDI, export, stress, native plugins | [ctest.log](ctest.log) | 7/7 suites pass |
| Dry engine against stock libopenmpt | [audio-test-results.json](audio-test-results.json), [fidelity.log](fidelity.log) | 30 comparisons at 44.1/48/96 kHz, exact frame counts and finite PCM; zero intercepted callback allocations, frees or locks |
| Actual Core Audio output/capture | [virtual-loopback.log](virtual-loopback.log) | Dry, AU effect, AU instrument, VST3 effect with automation, VST3 instrument with automation: 96,000 stereo frames each, zero sample error, no timestamp gaps or callback overruns at 48 kHz / 512 frames |
| Custom editors and automation | [native-editor-tests.log](native-editor-tests.log) | AU/VST3 view attachment and close/reopen; VST3 gestures reach audio without UI polling; recording during virtual-device playback, window restoration, baseline preservation and save/reopen pass |
| Memory and undefined behavior | [sanitizers.log](sanitizers.log) | Core, session, export, MIDI and native plugin checks pass |
| Earlier complete build/test script | [earlier-full-suite.log](earlier-full-suite.log) | Includes stock, recovery and native interface checks; subsequent note-release changes were rechecked by the final suites above |

The virtual capture test uses only the existing, explicitly named BlackHole 2ch route and preserves default routing and device configuration. It does not measure a physical DAC or speakers. The deterministic VST3 fixture has effect, instrument and delayed-instrument classes; it is built for tests and is not installed or shipped with the application. Its callback checks cover first use, automation, note releases and sample/instrument latency alignment. The normal build performs allocation/free/lock auditing. The sanitizer build disables that interposer because its startup conflicts with ASan; sanitizer timings are not performance evidence.

The AppKit editor tests drive the application's views and controls directly. They establish behavior even when the desktop cannot be captured, but do not prove visible presentation or 60 fps. Apple’s AU editor framework emitted duplicate-class warnings in the log; the tests completed successfully.

Installed Native Instruments Bite and FM8 also passed isolated instantiation, render and state probes (13 and 1,103 parameters, respectively). These are compatibility samples, not comprehensive preset or custom-interface qualification. Arbitrary vendor preset/nonparameter edits while recording existing automation remain unqualified.

## Visible inspection and blocked performance check

Earlier in this implementation, the running app's Plugins rack, restored tracker-instrument assignment, and plugin-owned VST3 custom window were inspected through the desktop UI. The searchable 4,096-parameter native table additionally has interface-test coverage.

The later requested 60-second AU/VST3 visible workload failed before setup. [Its raw report](ui-vst3-60s-blocked.json) says `measurementStarted: false`, `applicationActive: false`, `windowOccluded: true`, and `passed: false`; it has no measured audio callbacks or presented frames. The desktop tool then explicitly reported that the Mac was locked and could not be automatically unlocked. This report is a failed prerequisite, not an audio or graphics performance pass.

That attempt identifies source fingerprint `c10619bc5126869a914c5c381d6a8387d3df296eee8158bc65d031ede11d3d0a`. The final package manifest is recorded separately in `BuildInfo.json`, with artifact hashes in `artifacts.json`. Documentation and final note-release fixes postdate the blocked visible attempt.

The plugin build's visible 60 fps check and the original ten-minute UI / thirty-minute combined release gates remain open. Earlier failed long runs, including an audio deadline overrun, remain in [the earlier qualification record](../2026-09-19/README.md).

## Reproduce

From the repository root:

```sh
bash mac/test.sh
bash mac/sanitize.sh
bin/mac-native/native-plugin-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3" --ui --loopback-record
bin/mac-native/loopback-tests 'BlackHole 2ch' "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
# Keep the desktop unlocked and the application visible for these:
bash mac/ui-test.sh 60 --no-build --vst3
bash mac/ui-test.sh 600 --no-build --vst3
bash mac/ui-test.sh 1800 --no-build --vst3
```

See [compatibility](../../../mac/COMPATIBILITY.md) for supported routing and current limitations. No Windows plugin support, upstream submission, or public distribution was performed.
