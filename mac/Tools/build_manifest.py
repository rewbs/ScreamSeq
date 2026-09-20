#!/usr/bin/env python3
"""Identify a development binary by the exact local sources it was built from."""
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
paths = set()
for folder in ("editor", "mac"):
    paths.update(p for p in (root / folder).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD", "-z"], cwd=root)
paths.update(root / name.decode() for name in changed.split(b"\0") if name)
# Editor-only core extensions may still be untracked in a development checkout.
paths.add(root / "soundlib/NativeReverseLoop.h")
paths.add(root / "soundlib/NativeNoteEffects.h")
hashes = {
    str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(paths) if p.is_file()
}
manifest = {
    "baseRevision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "builtAtUTC": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "sourceHashes": hashes,
    "sourceFingerprint": hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
}
destination = pathlib.Path(sys.argv[1])
destination.write_text(json.dumps(manifest, indent=2) + "\n")
