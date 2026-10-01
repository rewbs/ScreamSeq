---
name: screamseq-qualification
description: Build, launch, inspect and qualify ScreamSeq without disturbing a musician's running session, including UI, API, offline audio, loopback and performance evidence.
---

Read `doc/SCREAMSEQ_ARCHITECTURE.md` for current build/test entry points. Inspect running app processes and exact bundle paths first. Preserve the user's process, unsaved project and audio route. Use a separate build directory, copied QA bundle identity and disposable example song. A different filename alone is not sufficient to prevent Launch Services choosing the wrong instance.

Launch the test executable in a persistent terminal process with `--inspection --automation` for live UI/API checks. Keep the exec session alive. A child spawned from a short-lived command may be killed when that command ends; subsequent app lookup can relaunch without its flags. Verify the actual PID and command line before testing. `--inspection` suppresses recovery/preferences persistence and audible sample-browser preview; use explicit appropriate audio tests when sound is required.

For UI interaction use the available computer-use tool and refresh accessibility state after actions. The user's desktop may interfere; stay scoped to the QA app. Keyboard input into an opening native sheet may arrive before it is ready—observe first. AppKit interface tests can exercise the application's own view event handlers offscreen, but are not proof of real presentation. Locked-desktop screenshots may show blank native controls. Do not claim live visual or 60fps qualification from them.

Build with `SCREAMSEQ_BUILD_DIR=bin/mac-background SCREAMSEQ_BUILD_JOBS=2 RESONANCE_DEVELOPMENT_BUILD=1 bash mac/build.sh` when background resource use matters. Existing `RESONANCE_*` aliases remain supported. Check the script and CTest inventory before choosing tests; names evolve. `mac/test-interface.sh` accepts the build directory environment and snapshot destination. Actual socket tests live in `mac/Tests/test_automation.py`; startup/workspace/recovery tests are separate.

Run targeted tests as changes land, then the required full suite once. Broaden/repeat only for changed code or unresolved concerns. Offline audio fixtures are the first line of audio verification. BlackHole 2ch can test Core Audio output and capture without changing the default device or using the microphone; verify it is installed and select its device explicitly. Physical DAC latency and speaker sound require separate measurements.

The visible workload accepts `--ui-test --ui-test-seconds 60 --ui-test-device 'BlackHole 2ch' --ui-test-graph`, with optional `--ui-test-vst3 <fixture bundle>`. It intentionally raises its own QA window for presentation measurements, so announce that brief interaction. Command-line option values must not be mistaken for a song path. Wait until builds and sanitizer jobs finish for the idle benchmark, and retain separate loaded measurements when relevant. A measured failure needs investigation or an explicit limitation, not a relaxed threshold.

`mac/ui-test.sh` accepts the build-directory environment and creates a uniquely identified QA bundle; a shared `.background` identifier can still collide with an older development app. AppKit may deliver command-line option values through its open-file delegate as well as the explicit argument parser; test both paths. A correct AX tree or cached window image does not prove the window is currently visible to the display server.

Record build/source fingerprint, executable hash, test logs, fixture/song, sample rate, buffer size and observation duration. Measure presentation with sustained frame intervals and audio with callback timing/discontinuities, not just an FPS label. A no-overrun result on one fixture is bounded evidence. Preserve prior checkpoints, and inspect the final packaged bundle/signature. Re-signing changes Mach-O signature bytes; compare code/data sections if validating identity with a differently named QA bundle.

Reports separate implemented behavior, tested behavior, known limits and untested claims. Close only disposable QA instances at the end. If the user needs to switch builds, tell them to save/quit their existing app first rather than doing it for them.
