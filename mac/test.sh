#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_build="${SCREAMSEQ_BUILD_DIR:-${RESONANCE_BUILD_DIR:-bin/mac-native}}"
tracker_jobs="${SCREAMSEQ_BUILD_JOBS:-${RESONANCE_BUILD_JOBS:-8}}"
mkdir -p "$tracker_build"
tracker_build="$(cd "$tracker_build" && pwd)"
# Propagate both names for older Python qualification entry points.
export SCREAMSEQ_BUILD_DIR="$tracker_build" RESONANCE_BUILD_DIR="$tracker_build"
export SCREAMSEQ_BUILD_JOBS="$tracker_jobs" RESONANCE_BUILD_JOBS="$tracker_jobs"
bash mac/build.sh
bash mac/build-reference.sh
"$tracker_build/tracker-core-tests" "$PWD"
"$tracker_build/pattern-tools-tests"
"$tracker_build/automation-target-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/plugin-library-tests"
"$tracker_build/note-track-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/pattern-commands-tests"
"$tracker_build/automation-tools-tests"
"$tracker_build/sample-project-tests"
"$tracker_build/sample-crossfade-project-tests"
"$tracker_build/sample-crossfade-tests"
"$tracker_build/sample-loop-tests"
"$tracker_build/native-reverse-loop-tests"
"$tracker_build/sample-snap-tests"
"$tracker_build/sample-drawing-tests"
"$tracker_build/sample-copy-tests"
"$tracker_build/sample-clipboard-tests"
"$tracker_build/sample-archive-tests"
"$tracker_build/sample-processing-tests"
"$tracker_build/sample-session-tests"
"$tracker_build/native-song-tests"
"$tracker_build/mixer-graph-tests"
"$tracker_build/sidechain-tests"
"$tracker_build/mixer-runtime-tests"
"$tracker_build/routing-prototype-tests"
"$tracker_build/native-mixer-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/multi-bus-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/plugin-tests"
"$tracker_build/native-effects-tests"
"$tracker_build/filter-tests"
"$tracker_build/eq-effects-tests"
"$tracker_build/comb-tests"
"$tracker_build/oversampling-tests"
"$tracker_build/distortion-tests"
"$tracker_build/lofi-tests"
"$tracker_build/cabinet-tests"
"$tracker_build/dynamics-tests"
"$tracker_build/maximizer-tests"
"$tracker_build/bus-compressor-tests"
"$tracker_build/rack-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/native-plugin-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/musical-automation-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
"$tracker_build/session-tests"
"$tracker_build/automation-tests"
"$tracker_build/plugin-inventory-tests"
"$tracker_build/plugin-lifecycle-tests"
"$tracker_build/plugin-shutdown-tests" "$tracker_build/test-plugins/ResonanceFixture.vst3"
bash mac/test-shutdown.sh
"$tracker_build/plugin-picker-tests"
python3 mac/Tests/test_automation.py
python3 mac/Tests/test_automation.py --app
python3 mac/Tests/test_startup.py
"$tracker_build/midi-tests"
"$tracker_build/recovery-tests"
bash mac/test-interface.sh
"$tracker_build/export-tests"
"$tracker_build/stress-tests"
python3 mac/test_audio.py
if [[ "${1:-}" == "--device" || "${1:-}" == "--soak" ]]; then
  "$tracker_build/device-tests" "$@"
  "$tracker_build/session-tests" --device
  "$tracker_build/sample-session-tests" --device
fi
