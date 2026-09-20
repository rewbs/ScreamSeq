#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Run explicitly selected local fixtures, one at a time. No screen/device tests.
export RESONANCE_BUILD_DIR="$PWD/bin/mac-background"
export CTEST_PARALLEL_LEVEL=1
nice -n 10 ctest --test-dir "$RESONANCE_BUILD_DIR" --output-on-failure -E '^(midi|stress)$'
nice -n 10 "$RESONANCE_BUILD_DIR/plugin-picker-tests"
nice -n 10 "$RESONANCE_BUILD_DIR/recovery-tests"
nice -n 10 "$RESONANCE_BUILD_DIR/sample-library-tests"
nice -n 10 python3 mac/Tests/test_automation.py
nice -n 10 python3 mac/Tests/test_automation.py --app
nice -n 10 python3 mac/Tests/test_startup.py
nice -n 10 python3 mac/Tests/test_recovery.py
nice -n 10 python3 mac/Tests/test_sample_library.py
nice -n 10 bash mac/test-interface.sh --snapshots "$RESONANCE_BUILD_DIR/qualification/editor-snapshots"
