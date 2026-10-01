# Plugin discovery cache and Battery 4 loading

Local qualification on 2026-09-19, Apple Silicon, installed Battery 4 4.3.0 (AU and VST3). Tests use private, disposable application instances, no visible windows and no audio-device output. The user's running app/document was not targeted.

## Findings and fixes

- The original AU crash was a HIToolbox main-queue assertion in Battery's factory, reached through `AudioComponentInstanceNew` on `org.resonance.document`. See the extracted [crash stack](crash-diagnosis.json). AU creation/configuration, initialization, property/state operations and disposal now run on the main thread. Audio rendering and scheduled parameter delivery remain on their render paths.
- Battery VST3 validation originally returned `Unsupported VST3 audio bus layout`: the host rejected more than 16 audio buses. It now supports up to 64 buses per direction and supplies a complete, preallocated `ProcessData` bus array, including channel-pointer arrays for inactive auxiliaries. The first stereo output is used; auxiliary routing has not been added.
- The plugin inventory persists across launches, with explicit **Rescan** in the Add plugin picker and `plugin.discover`'s optional `rescan: true`. Cache hits launch no scanner processes. Invalid/truncated/wrong-version/wrong-architecture caches rebuild; a failed complete scan preserves the previous list. Individual incompatible VST3 discovery candidates continue to be skipped. Selected plugins still receive an isolated validation probe before adding.
- Third-party stdout messages are redirected away from the scanner's JSON response pipe.

## Observed results

[Full measurements](results.json), [plugin versions](plugin-versions.json).

| Check | Result |
|---|---:|
| Installed inventory | 75 entries |
| Explicit full rescan | 30.507 s |
| Subsequent cached inventory request | 1.536 ms |
| Cached request after restarting hidden app | 285.249 ms |
| Battery VST3 app add (including validation) | 3.529 s |
| Battery AU app add (including validation) | 3.174 s |
| Battery VST3 parameters / captured state | 2,224 / 51,126 bytes |
| Battery AU parameters / captured state | 128 / 51,218 bytes |

Both formats passed isolated creation, note-on/off, 32 render blocks, parameter enumeration and state restoration. Both also passed loading on the actual app's document worker, state capture/restore and removal without a crash. The inventory measurements are request times, not picker animation measurements. The tests used default plugin state and did not load sample kits or qualify every preset/custom editor.

## Regression coverage

- [10/10 CTest suites](ctest.log): existing core, AU/VST3 audio, session, MIDI, export, stress and API checks, plus cache and AU lifecycle suites.
- Cache tests cover cold/warm/restart behavior, explicit rescan, empty inventory, malformed data, failures preserving the last good cache and an unwritable cache location.
- A process-local AU fixture checks main-thread lifecycle/property/state calls, worker rendering, disposal and failed-initialization cleanup. It requires no installed commercial plugin.
- The deterministic VST3 instrument fixture now exposes 32 mixed mono/stereo outputs and checks activation, complete render bus arrays and inactive channel pointers. Existing automation, waveform equality and zero-allocation/free/lock assertions continue to pass.
- [ASan/UBSan suite](sanitizers.log), plus the [final cache sanitizer check](cache-sanitizer.log), passed. These are correctness checks, not timing qualifications.
- [Hidden actual-app API checks](api-app.log) passed.
- [Actual AppKit picker checks](picker-layout.log) verify labels, selection, duplicate display names, Rescan, disabled Add for an empty inventory and control bounds without showing the window. A bitmap attempt on the never-shown alert omitted compositor-backed controls and was discarded; no pixel inspection or frame-rate claim is based on it.

## Reproduce and build

```sh
bash mac/build.sh
ctest --test-dir bin/mac-native --output-on-failure
bin/mac-native/plugin-picker-tests
python3 mac/Tests/test_automation.py --app
python3 mac/Tests/test_battery.py
bash mac/sanitize.sh
```

The optional Battery runner requires installed Battery AU and VST3. It starts its own hidden app and never selects an existing user endpoint.

The [packaged build log](build-package.log), [source manifest](BuildInfo.json) and [archive/signature checks](artifacts.json) identify the delivered build. The ZIP passes integrity verification and contains the same source manifest as the signed app. Runtime plugins still execute in-process; these fixes do not provide general third-party crash isolation. Existing long-duration UI/audio performance gates remain unchanged.
