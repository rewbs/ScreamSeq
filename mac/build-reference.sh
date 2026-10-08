#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_root="$PWD"
tracker_revision=f83cedb0cd5446e4dfaa83ac97e3087107e26767
tracker_build="${SCREAMSEQ_BUILD_DIR:-${RESONANCE_BUILD_DIR:-bin/mac-native}}"
tracker_jobs="${SCREAMSEQ_BUILD_JOBS:-${RESONANCE_BUILD_JOBS:-8}}"
if ! [[ "$tracker_jobs" =~ ^[1-9][0-9]?$ ]] || (( tracker_jobs > 32 )); then
  echo 'SCREAMSEQ_BUILD_JOBS must be 1..32' >&2; exit 2
fi
if ! git cat-file -e "${tracker_revision}^{commit}" 2>/dev/null; then
  cat >&2 <<EOF
The pinned stock OpenMPT commit $tracker_revision is missing from local history.
Use a full ScreamSeq clone, or explicitly fetch the reference once:
  git fetch https://github.com/OpenMPT/openmpt.git $tracker_revision
Then rerun mac/build-reference.sh. This script does not access the network.
EOF
  exit 2
fi
mkdir -p "$tracker_build"
tracker_build="$(cd "$tracker_build" && pwd)"
tracker_source="$tracker_build/reference-source/$tracker_revision"
mkdir -p "$tracker_build/reference-source"
tracker_staging="$(mktemp -d "$tracker_build/reference-source/.archive-XXXXXX")"
trap 'rm -rf "$tracker_staging"' EXIT
# The oracle includes its own engine, build rules, fixtures and licenses. Recreate
# this script-owned disposable tree: the upstream Makefile uses source globs, so
# overwriting tracked files alone would retain locally added sources/configuration.
# No current ScreamSeq headers or previous oracle objects are used.
git archive --format=tar "$tracker_revision" | tar -x -C "$tracker_staging"
rm -rf "$tracker_source"
mv "$tracker_staging" "$tracker_source"
trap - EXIT
mkdir -p "$tracker_source/bin/screamseq-reference"
echo "Building stock OpenMPT $tracker_revision; log: $tracker_build/reference-build.log"
if ! (
  cd "$tracker_source"
  # Supplying the pinned SVN revision prevents the upstream Makefile from
  # discovering the enclosing ScreamSeq checkout and labeling it as the oracle.
  make -j"$tracker_jobs" CONFIG=macos FLAVOUR=screamseq-reference FLAVOUR_DIR=screamseq-reference/ \
    MPT_SVNVERSION=25686 EXAMPLES=0 OPENMPT123=0 SHARED_LIB=0 STATIC_LIB=1 TEST=1 \
    NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1 \
    NO_PORTAUDIO=1 NO_PORTAUDIOCPP=1 NO_PULSEAUDIO=1 NO_SDL2=1 \
    NO_FLAC=1 NO_SNDFILE=1
) > "$tracker_build/reference-build.log" 2>&1; then
  tail -n 60 "$tracker_build/reference-build.log" >&2
  exit 1
fi
if ! (
  cd "$tracker_source"
  bin/screamseq-reference/libopenmpt_test
) > "$tracker_build/stock-test-results.log" 2>&1; then
  tail -n 60 "$tracker_build/stock-test-results.log" >&2
  exit 1
fi
xcrun clang++ -O2 -std=c++20 -I"$tracker_source" \
  "$tracker_root/mac/Tests/ReferenceRenderer.cpp" \
  "$tracker_source/bin/screamseq-reference/libopenmpt.a" \
  -o "$tracker_build/reference-renderer"
python3 - "$tracker_build" "$tracker_revision" "$tracker_source" <<'PY'
import hashlib, json, pathlib, sys
build, revision, source = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
(build / 'reference-source.json').write_text(json.dumps({
    'upstreamRepository': 'https://github.com/OpenMPT/openmpt.git',
    'upstreamRevision': revision,
    'sourceDirectory': str(source),
    'librarySHA256': digest(source / 'bin/screamseq-reference/libopenmpt.a'),
    'rendererSHA256': digest(build / 'reference-renderer'),
    'stockTests': 'passed',
}, indent=2) + '\n')
PY
echo "Stock tests passed; built $tracker_build/reference-renderer"
