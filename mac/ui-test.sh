#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
tracker_seconds="${1:-60}"
if ! [[ "$tracker_seconds" =~ ^[0-9]+$ ]] || (( tracker_seconds < 15 || tracker_seconds > 3600 )); then
  echo 'Usage: bash mac/ui-test.sh [15..3600 seconds] [--no-build]' >&2
  exit 2
fi
if [[ "${2:-}" != '--no-build' ]]; then bash mac/build.sh; fi
tracker_extra=()
if [[ "${3:-}" == '--vst3' ]]; then
  tracker_extra=(--ui-test-vst3 "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3")
fi
mkdir -p bin/mac-native/qualification
bin/mac-native/qualification-fixture bin/mac-native/qualification/dense-ui.mptm
# Launch this executable directly so the process id is unambiguous even when
# Launch Services knows about an older packaged build. Only this instance is stopped.
bin/mac-native/ScreamSeq.app/Contents/MacOS/ScreamSeq \
  "$PWD/bin/mac-native/qualification/dense-ui.mptm" --ui-test --ui-test-seconds "$tracker_seconds" "${tracker_extra[@]}" \
  > bin/mac-native/qualification/ui-run.log 2>&1 &
tracker_pid=$!
trap 'kill "$tracker_pid" 2>/dev/null || true' EXIT
python3 - "$tracker_pid" "$tracker_seconds" <<'PY'
import json, os, pathlib, sys, time
pid, seconds = map(int, sys.argv[1:])
source = pathlib.Path('/tmp/resonance-ui-test.json')
deadline = time.monotonic() + seconds + 90
while time.monotonic() < deadline:
    try:
        data = json.loads(source.read_text())
        if data.get('processID') == pid:
            prefix = 'vst3-' if data.get('requestedVST3') else ''
            target = pathlib.Path(f'bin/mac-native/qualification/ui-{prefix}{seconds}s.json')
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
