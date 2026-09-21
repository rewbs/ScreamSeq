#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_build="${SCREAMSEQ_BUILD_DIR:-${RESONANCE_BUILD_DIR:-$PWD/bin/mac-native}}"
tracker_build="$(cd "$tracker_build" && pwd)"
tracker_tmp="$(mktemp -d /tmp/screamseq-shutdown.XXXXXX)"
trap 'rm -rf "$tracker_tmp"' EXIT
xcrun swiftc -O -whole-module-optimization -num-threads 2 -swift-version 5 -D SCREAMSEQ_SHUTDOWN_TEST -target "$(uname -m)-apple-macosx14.0" \
  -import-objc-header mac/Bridge/TrackerSession.h mac/App/*.swift mac/Tests/AppShutdownTests.swift \
  -L "$tracker_build" -lTrackerMac -lTrackerPlugins -lTrackerEditor -lOpenMPTCore -lTrackerFLAC -lc++ \
  -framework AppKit -framework Metal -framework MetalKit -framework AVFoundation -framework AudioToolbox -framework CoreAudio -framework CoreMIDI \
  -o "$tracker_build/app-shutdown-tests"
SCREAMSEQ_SHUTDOWN_FIXTURE="$tracker_build/test-plugins/ResonanceFixture.vst3" \
  RESONANCE_AUTOMATION_TEST_DIRECTORY="$tracker_tmp" "$tracker_build/app-shutdown-tests" --automation-test
