#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash mac/build.sh
mkdir -p bin/mac-audit
make -j8 CONFIG=macos FLAVOUR=mac-audit FLAVOUR_DIR=mac-audit/ \
  EXAMPLES=0 OPENMPT123=0 SHARED_LIB=0 STATIC_LIB=1 TEST=1 \
  NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1 \
  NO_PORTAUDIO=1 NO_PORTAUDIOCPP=1 NO_PULSEAUDIO=1 NO_SDL2=1 \
  NO_FLAC=1 NO_SNDFILE=1 > bin/mac-native/reference-build.log 2>&1
bin/mac-audit/libopenmpt_test > bin/mac-native/stock-test-results.log 2>&1
xcrun clang++ -O2 -std=c++20 -I. mac/Tests/ReferenceRenderer.cpp bin/mac-audit/libopenmpt.a -o bin/mac-native/reference-renderer
bin/mac-native/tracker-core-tests "$PWD"
bin/mac-native/pattern-tools-tests
bin/mac-native/automation-target-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/plugin-library-tests
bin/mac-native/note-track-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/pattern-commands-tests
bin/mac-native/automation-tools-tests
bin/mac-native/sample-project-tests
bin/mac-native/sample-crossfade-project-tests
bin/mac-native/sample-crossfade-tests
bin/mac-native/sample-loop-tests
bin/mac-native/native-reverse-loop-tests
bin/mac-native/sample-snap-tests
bin/mac-native/sample-drawing-tests
bin/mac-native/sample-copy-tests
bin/mac-native/sample-clipboard-tests
bin/mac-native/sample-archive-tests
bin/mac-native/sample-processing-tests
bin/mac-native/sample-session-tests
bin/mac-native/native-song-tests
bin/mac-native/mixer-graph-tests
bin/mac-native/sidechain-tests
bin/mac-native/mixer-runtime-tests
bin/mac-native/routing-prototype-tests
bin/mac-native/native-mixer-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/multi-bus-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/plugin-tests
bin/mac-native/native-effects-tests
bin/mac-native/filter-tests
bin/mac-native/eq-effects-tests
bin/mac-native/comb-tests
bin/mac-native/oversampling-tests
bin/mac-native/distortion-tests
bin/mac-native/lofi-tests
bin/mac-native/cabinet-tests
bin/mac-native/dynamics-tests
bin/mac-native/maximizer-tests
bin/mac-native/bus-compressor-tests
bin/mac-native/rack-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/native-plugin-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/musical-automation-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
bin/mac-native/session-tests
bin/mac-native/automation-tests
bin/mac-native/plugin-inventory-tests
bin/mac-native/plugin-lifecycle-tests
bin/mac-native/plugin-picker-tests
python3 mac/Tests/test_automation.py
python3 mac/Tests/test_automation.py --app
python3 mac/Tests/test_startup.py
bin/mac-native/midi-tests
bin/mac-native/recovery-tests
bash mac/test-interface.sh
bin/mac-native/export-tests
bin/mac-native/stress-tests
python3 mac/test_audio.py
if [[ "${1:-}" == "--device" || "${1:-}" == "--soak" ]]; then
  bin/mac-native/device-tests "$@"
  bin/mac-native/session-tests --device
  bin/mac-native/sample-session-tests --device
fi
