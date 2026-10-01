#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash mac/build.sh
mkdir -p bin/mac-native/distribution
codesign --verify --strict bin/mac-native/ScreamSeq.app
ditto -c -k --sequesterRsrc --keepParent bin/mac-native/ScreamSeq.app "bin/mac-native/distribution/ScreamSeq-0.1.0-$(uname -m).zip"
