#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_seconds="${1:-60}"
if ! [[ "$tracker_seconds" =~ ^[0-9]+$ ]] || (( tracker_seconds < 15 || tracker_seconds > 3600 )); then
  echo 'Usage: bash mac/ui-test.sh [15..3600 seconds] [--no-build] [--vst3] [--graph] [--device "BlackHole 2ch"]' >&2
  exit 2
fi
if (( $# )); then shift; fi
tracker_build="${SCREAMSEQ_BUILD_DIR:-${RESONANCE_BUILD_DIR:-bin/mac-native}}"
tracker_skip=0
tracker_graph=0
tracker_vst3=0
tracker_extra=()
while (( $# )); do
  case "$1" in
    --no-build) tracker_skip=1; shift ;;
    --vst3) tracker_vst3=1; shift ;;
    --graph) tracker_graph=1; tracker_extra+=(--ui-test-graph); shift ;;
    --device)
      if [[ "${2:-}" != 'BlackHole 2ch' ]]; then echo 'Only explicit BlackHole 2ch qualification is supported' >&2; exit 2; fi
      tracker_extra+=(--ui-test-device "$2"); shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done
if (( ! tracker_skip )); then RESONANCE_DEVELOPMENT_BUILD=1 bash mac/build.sh; fi
tracker_build="$(cd "$tracker_build" && pwd)"
if (( tracker_vst3 )); then tracker_extra+=(--ui-test-vst3 "$tracker_build/test-plugins/ResonanceFixture.vst3"); fi
mkdir -p "$tracker_build/qualification"
tracker_run="$(mktemp -d "$tracker_build/qualification/ui-run-XXXXXX")"
tracker_app="$tracker_run/ScreamSeq QA.app"
ditto "$tracker_build/ScreamSeq.app" "$tracker_app"
python3 - "$tracker_app/Contents/Info.plist" <<'PYINFO'
import pathlib, plistlib, sys, uuid
p = pathlib.Path(sys.argv[1]); info = plistlib.loads(p.read_bytes())
info.update(CFBundleIdentifier='org.screamseq.tracker.qualification.' + uuid.uuid4().hex,
            CFBundleName='ScreamSeq QA', CFBundleDisplayName='ScreamSeq QA')
p.write_bytes(plistlib.dumps(info))
PYINFO
codesign --force --sign - "$tracker_app"
tracker_fixture=()
# Instrument graphs have independent copies per raw channel; the dense 127-track
# fixture intentionally tests drawing separately from the smaller combined graph.
if (( ! tracker_graph )); then
  "$tracker_build/qualification-fixture" "$tracker_build/qualification/dense-ui.mptm"
  tracker_fixture=("$tracker_build/qualification/dense-ui.mptm")
fi
# Launch this executable directly so the process id is unambiguous even when
# Launch Services knows about an older packaged build. Only this instance is stopped.
"$tracker_app/Contents/MacOS/ScreamSeq" \
  ${tracker_fixture[@]+"${tracker_fixture[@]}"} --inspection --ui-test --ui-test-seconds "$tracker_seconds" ${tracker_extra[@]+"${tracker_extra[@]}"} \
  > "$tracker_build/qualification/ui-run.log" 2>&1 &
tracker_pid=$!
trap 'kill "$tracker_pid" 2>/dev/null || true' EXIT
python3 - "$tracker_pid" "$tracker_seconds" "$tracker_build" <<'PY'
import json, os, pathlib, sys, time
pid, seconds = map(int, sys.argv[1:3])
build = pathlib.Path(sys.argv[3])
source = pathlib.Path('/tmp/resonance-ui-test.json')
deadline = time.monotonic() + seconds + 90
while time.monotonic() < deadline:
    try:
        data = json.loads(source.read_text())
        if data.get('processID') == pid:
            prefix = 'vst3-' if data.get('requestedVST3') else ''
            if data.get('usesGraph'): prefix += 'graph-'
            target = build / 'qualification' / f'ui-{prefix}{seconds}s.json'
            target.write_text(json.dumps(data, indent=2) + '\n')
            print(json.dumps(data, indent=2))
            sys.exit(0 if data.get('passed') else 1)
    except (FileNotFoundError, json.JSONDecodeError):
        pass
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        sys.exit('UI qualification process exited before writing its report')
    time.sleep(0.5)
sys.exit('Timed out waiting for the UI report; keep the display unlocked and visible')
PY
