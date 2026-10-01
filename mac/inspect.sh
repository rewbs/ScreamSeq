#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# A separate bundle identity keeps UI inspection distinct from the user's app
# and avoids Launch Services selecting an older generated test bundle.
if [[ "${1:-}" != '--no-build' ]]; then bash mac/build.sh; fi
tracker_bundle="$PWD/bin/mac-native/development/ScreamSeq Development.app"
mkdir -p "$(dirname "$tracker_bundle")"
ditto bin/mac-native/ScreamSeq.app "$tracker_bundle"
python3 - "$tracker_bundle/Contents/Info.plist" <<'PY'
import plistlib,sys
from pathlib import Path
p=Path(sys.argv[1]); data=plistlib.loads(p.read_bytes())
data['CFBundleIdentifier']='org.resonance.tracker.development'
data['CFBundleName']='ScreamSeq Development'
data['CFBundleDisplayName']='ScreamSeq Development'
p.write_bytes(plistlib.dumps(data))
PY
codesign --force --sign - "$tracker_bundle"
echo "$tracker_bundle"
