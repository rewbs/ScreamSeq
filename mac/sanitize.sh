#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Correctness only: sanitizer instrumentation invalidates performance timing.
tracker_flags='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake -S mac -B bin/mac-sanitized -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_C_FLAGS=$tracker_flags" "-DCMAKE_CXX_FLAGS=$tracker_flags" \
  "-DCMAKE_OBJCXX_FLAGS=$tracker_flags" '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build bin/mac-sanitized -j "${SCREAMSEQ_BUILD_JOBS:-${RESONANCE_BUILD_JOBS:-8}}" --target automation-target-tests plugin-library-tests note-track-tests pattern-commands-tests automation-tools-tests native-reverse-loop-tests sample-loop-tests sample-crossfade-project-tests sample-crossfade-tests sample-snap-tests sample-drawing-tests sample-copy-tests sample-clipboard-tests sample-project-tests sample-archive-tests sample-session-tests sample-processing-tests tracker-core-tests native-song-tests pattern-tools-tests session-tests export-tests midi-tests plugin-scanner native-plugin-tests musical-automation-tests automation-tests plugin-inventory-tests plugin-lifecycle-tests mixer-graph-tests mixer-runtime-tests native-mixer-tests multi-bus-tests sidechain-tests routing-prototype-tests native-effects-tests rack-tests filter-tests eq-effects-tests comb-tests oversampling-tests distortion-tests lofi-tests cabinet-tests dynamics-tests maximizer-tests bus-compressor-tests
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1
bin/mac-sanitized/tracker-core-tests "$PWD"
bin/mac-sanitized/pattern-tools-tests
bin/mac-sanitized/automation-target-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/plugin-library-tests
bin/mac-sanitized/note-track-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/pattern-commands-tests
bin/mac-sanitized/automation-tools-tests
bin/mac-sanitized/sample-project-tests
bin/mac-sanitized/sample-crossfade-project-tests
bin/mac-sanitized/sample-crossfade-tests
bin/mac-sanitized/sample-loop-tests
bin/mac-sanitized/native-reverse-loop-tests
bin/mac-sanitized/sample-snap-tests
bin/mac-sanitized/sample-drawing-tests
bin/mac-sanitized/sample-copy-tests
bin/mac-sanitized/sample-clipboard-tests
bin/mac-sanitized/sample-archive-tests
bin/mac-sanitized/sample-processing-tests
bin/mac-sanitized/sample-session-tests
bin/mac-sanitized/native-song-tests
bin/mac-sanitized/native-effects-tests
bin/mac-sanitized/filter-tests
bin/mac-sanitized/eq-effects-tests
bin/mac-sanitized/comb-tests
bin/mac-sanitized/oversampling-tests
bin/mac-sanitized/distortion-tests
bin/mac-sanitized/lofi-tests
bin/mac-sanitized/cabinet-tests
bin/mac-sanitized/dynamics-tests
bin/mac-sanitized/maximizer-tests
bin/mac-sanitized/bus-compressor-tests
bin/mac-sanitized/rack-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/mixer-graph-tests
bin/mac-sanitized/sidechain-tests
bin/mac-sanitized/mixer-runtime-tests
bin/mac-sanitized/routing-prototype-tests
bin/mac-sanitized/native-mixer-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/multi-bus-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/session-tests
bin/mac-sanitized/automation-tests
bin/mac-sanitized/export-tests
bin/mac-sanitized/midi-tests
bin/mac-sanitized/native-plugin-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"
bin/mac-sanitized/musical-automation-tests "$PWD/bin/mac-sanitized/test-plugins/ResonanceFixture.vst3"

bin/mac-sanitized/plugin-inventory-tests
bin/mac-sanitized/plugin-lifecycle-tests
