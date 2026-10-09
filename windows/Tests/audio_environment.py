"""Capture read-only audio availability from an explicitly owned inspection app.

This does not qualify playback, select an endpoint, or waive a failing test.
Explicit API errors are retained as diagnostic results; transport failures fail.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "editor/Tests/Conformance"))
from native_roundtrip import owned_client, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This probe requires Windows")
    executable = args.executable.resolve(strict=True)
    actual = hashlib.sha256(executable.read_bytes()).hexdigest()
    if actual != args.binary_sha256:
        parser.error("Executable differs from the selected binary")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = dict(format="screamseq-audio-environment-v1", executable=str(executable),
                  executableSHA256=actual, host=platform.platform(), machine=platform.machine(),
                  process={}, requests=[], diagnosticOnly=True, completed=False)
    try:
        with owned_client(executable, output, report["process"]) as client:
            for method in ("audio.devices.get", "audio.settings.get", "transport.get"):
                entry = dict(method=method)
                report["requests"].append(entry)
                try:
                    entry["result"] = client.call(method)
                except Exception as error:
                    entry["error"] = dict(type=type(error).__name__, code=getattr(error, "code", None), message=str(error))
                    if not isinstance(getattr(error, "code", None), int):
                        raise
        report["completed"] = True
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
    finally:
        report["binaryUnchanged"] = hashlib.sha256(executable.read_bytes()).hexdigest() == actual
        write_json(output / "report.json", report)
    print(json.dumps(report, indent=2))
    return 0 if report["completed"] and report["binaryUnchanged"] else 1


if __name__ == "__main__":
    sys.exit(main())
