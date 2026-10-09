"""Run every declared native interface group once in its own bounded process.

The harness is compiled once by test-interface.sh. Every failure is retained and
the complete run fails if any group fails; there are no assertion or crash retries.
"""
import json
from pathlib import Path
import subprocess
import sys
import time


def run(executable, arguments, invoke=subprocess.run):
    # This driver always owns the complete group list, never a caller-selected
    # subset that could be confused with full qualification.
    if any(arg in {"--group", "--list-groups"} for arg in arguments):
        raise ValueError("Use the compiled harness directly for a single group")
    listing = invoke([str(executable), "--list-groups"], capture_output=True, text=True,
                     check=True, timeout=15)
    groups = json.loads(listing.stdout)
    if (not isinstance(groups, list) or not groups or
            any(not isinstance(group, str) or not group or group.startswith("-") for group in groups)
            or len(set(groups)) != len(groups) or "core-layout" not in groups):
        raise ValueError("Invalid or duplicate native group inventory")
    failures = []
    for group in groups:
        started = time.monotonic()
        print(f"RUN interface group {group}", flush=True)
        try:
            result = invoke([str(executable), "--group", group, *arguments], timeout=120)
            if result.returncode:
                failures.append((group, f"exit {result.returncode}"))
        except subprocess.TimeoutExpired:
            # subprocess.run kills and joins only its own child on timeout.
            failures.append((group, "exceeded 120-second bound"))
        print(f"END interface group {group}: {time.monotonic() - started:.3f}s", flush=True)
    for group, failure in failures:
        print(f"FAIL interface group {group}: {failure}", flush=True)
    print(f"Interface groups: {len(groups) - len(failures)}/{len(groups)} passed", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(run(Path(sys.argv[1]).resolve(strict=True), sys.argv[2:]))
