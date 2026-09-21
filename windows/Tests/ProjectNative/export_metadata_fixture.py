"""Test-only oracle: export ONLY native metadata from a hash-pinned Mac plist.

No snapshot extraction, project conversion, app launch or runtime dependency.
The project binary and generated JSON must stay outside Git.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib

REFERENCE_SHA256 = "96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sha256", default=REFERENCE_SHA256)
    args = parser.parse_args()
    source = args.project.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest != args.sha256:
        raise SystemExit(f"Fixture hash mismatch: {digest}")
    native = plistlib.loads(source)["native"]
    if not isinstance(native, dict):
        raise SystemExit("Fixture native metadata is not a dictionary")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(native, ensure_ascii=False, allow_nan=False, indent=2), encoding="utf-8")
    if hashlib.sha256(args.project.read_bytes()).hexdigest() != digest:
        raise SystemExit("Source fixture changed during export")
    print(json.dumps({"sha256": digest, "metadata_version": native["version"], "output": str(args.output)}))


if __name__ == "__main__":
    main()
