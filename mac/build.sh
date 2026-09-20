#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_arch="$(uname -m)"
tracker_build="${SCREAMSEQ_BUILD_DIR:-${RESONANCE_BUILD_DIR:-bin/mac-native}}"
tracker_jobs="${SCREAMSEQ_BUILD_JOBS:-${RESONANCE_BUILD_JOBS:-8}}"
if ! [[ "$tracker_jobs" =~ ^[1-9][0-9]?$ ]] || (( tracker_jobs > 32 )); then
  echo 'RESONANCE_BUILD_JOBS must be 1..32' >&2; exit 2
fi
mkdir -p "$tracker_build"
tracker_build="$(cd "$tracker_build" && pwd)"
tracker_app="$tracker_build/ScreamSeq.app"
cmake -S mac -B "$tracker_build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build "$tracker_build" -j "$tracker_jobs"
mkdir -p "$tracker_app/Contents/MacOS" "$tracker_app/Contents/Resources"
xcrun swiftc -O -whole-module-optimization -num-threads "$tracker_jobs" -g -swift-version 5 -target "${tracker_arch}-apple-macosx14.0" \
  -import-objc-header mac/Bridge/TrackerSession.h mac/App/*.swift \
  -L "$tracker_build" -lTrackerMac -lTrackerPlugins -lTrackerEditor -lOpenMPTCore -lTrackerFLAC -lc++ \
  -framework AppKit -framework Metal -framework MetalKit -framework AVFoundation -framework AudioToolbox -framework CoreAudio -framework CoreMIDI \
  -o "$tracker_app/Contents/MacOS/ScreamSeq"
xcrun swiftc -O mac/App/RecoveryStore.swift mac/Tests/RecoveryTests.swift -o "$tracker_build/recovery-tests"
xcrun swiftc -O mac/App/PluginPicker.swift mac/Tests/PluginPickerTests.swift -o "$tracker_build/plugin-picker-tests"
xcrun swiftc -O -swift-version 5 mac/App/SampleLibrary.swift mac/App/SampleMultisample.swift mac/App/SampleAudition.swift mac/Tests/SampleLibraryTests.swift -framework AVFoundation -o "$tracker_build/sample-library-tests"
xcrun swiftc -O -swift-version 5 -import-objc-header mac/Bridge/TrackerSession.h \
  mac/App/AutomationServer.swift mac/App/EditorNavigation.swift mac/Tests/AutomationHost.swift \
  -L "$tracker_build" -lTrackerMac -lTrackerPlugins -lTrackerEditor -lOpenMPTCore -lTrackerFLAC -lc++ \
  -framework AppKit -framework AudioToolbox -framework CoreAudio -framework CoreMIDI \
  -o "$tracker_build/automation-test-host"
cp "$tracker_build/plugin-scanner" "$tracker_app/Contents/MacOS/plugin-scanner"
xcrun swift mac/Tools/Icon.swift "$tracker_app/Contents/Resources/ScreamSeq.icns"
python3 mac/Tools/bundle_notices.py "$tracker_app/Contents/Resources"
cp mac/Tools/screamseq_api.py mac/Tools/resonance_api.py mac/Tools/resonance-api.schema.json "$tracker_app/Contents/Resources/"
python3 mac/Tools/build_manifest.py "$tracker_app/Contents/Resources/BuildInfo.json"
cp mac/Info.plist "$tracker_app/Contents/Info.plist"
if [[ "${RESONANCE_DEVELOPMENT_BUILD:-0}" == 1 ]]; then
  python3 - "$tracker_app/Contents/Info.plist" <<'PYINFO'
import plistlib, sys
from pathlib import Path
p = Path(sys.argv[1]); info = plistlib.loads(p.read_bytes())
info.update(CFBundleIdentifier='org.resonance.tracker.background',
            CFBundleName='ScreamSeq Background', CFBundleDisplayName='ScreamSeq Background')
p.write_bytes(plistlib.dumps(info))
PYINFO
fi
codesign --force --sign - "$tracker_app/Contents/MacOS/plugin-scanner"
codesign --force --sign - "$tracker_app"
echo "Built $tracker_app"
