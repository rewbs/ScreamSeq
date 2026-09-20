#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# This build never replaces either the delivered app or its frozen checkpoint.
exec nice -n 10 env RESONANCE_BUILD_DIR="$PWD/bin/mac-background" \
  RESONANCE_BUILD_JOBS=2 RESONANCE_DEVELOPMENT_BUILD=1 bash mac/build.sh
