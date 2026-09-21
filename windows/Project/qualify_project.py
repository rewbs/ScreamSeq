"""Read-only source qualification using the Python container codec, not an app.

Repack into a disposable directory, reopen from disk, and compare the complete
plist tree and opaque snapshot sections. Never restore or play a partial song.
"""
import hashlib
import os
from pathlib import Path
import platform
import struct
import tempfile

import native_project


def _fingerprint(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _read(path):
    with open(path, "rb") as source:
        return source.read(native_project.MAX_PROJECT_BYTES + 1)


def _same_tree(left, right):
    """Compare decoded values/types, ignoring plist object-table layout only."""
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            _same_tree(value, right[key]) for key, value in left.items())
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _same_tree(a, b) for a, b in zip(left, right))
    if isinstance(left, float):
        return struct.pack(">d", left) == struct.pack(">d", right)
    return left == right


def qualify(source):
    """Return bounded container evidence; raise if any roundtrip check fails.

    Input remains read-only. TMPDIR controls the disposable repack directory.
    Like the codec, this is not a hardened parser for hostile plist graphs.
    """
    source = Path(source).resolve()
    original = _read(source)
    root = native_project.loads(original)
    # Keep the comparison baseline independent even if the encoder regresses
    # and mutates the tree handed to it.
    encoded = native_project.dumps(native_project.loads(original))
    with tempfile.TemporaryDirectory(prefix="screamseq-project-qualification-",
                                     dir=os.environ.get("TMPDIR")) as scratch:
        output = Path(scratch) / "repacked.screamseq"
        with output.open("xb") as destination:
            destination.write(encoded)
        reopened_bytes = _read(output)
        reopened = native_project.loads(reopened_bytes)
        if reopened_bytes != encoded:
            raise ValueError("Repacked file differs from the encoded bytes")
        if not _same_tree(root, reopened):
            raise ValueError("Decoded plist tree changed during roundtrip")
        if root["module"] != reopened["module"]:
            raise ValueError("Opaque snapshot/module bytes changed during roundtrip")
        sections = {}
        if root["version"] >= 4:
            before = native_project.split_snapshot(root["module"])
            after = native_project.split_snapshot(reopened["module"])
            if before != after:
                raise ValueError("Opaque snapshot sections changed during roundtrip")
            sections = {name: _fingerprint(data) for name, data in
                        zip(("module", "samples", "timing"), before)}
    if _read(source) != original:
        raise ValueError("Source changed during qualification")
    return {
        "scope": "python-container-roundtrip-only",
        "source": {"path": str(source), **_fingerprint(original)},
        "repacked": _fingerprint(reopened_bytes),
        "container_bytes_equal": original == reopened_bytes,
        "container_version": root["version"],
        "native_metadata_version": root.get("native", {}).get("version"),
        "sequence": root.get("sequence"),
        "plugin_count": len(root["plugins"]),
        "snapshot_magic": root["module"][:8].decode("ascii") if sections else None,
        "snapshot_sections": sections,
        "preserved_top_level_fields": sorted(root),
        "preserved_native_fields": sorted(root.get("native", {})),
        "checks": {
            "decoded_tree_equal": True,
            "source_unchanged": True,
            "snapshot_bytes_equal": True,
            "snapshot_sections_equal": True if sections else None,
            "repacked_file_reopened": True,
        },
        "song_semantics_validated": False,
        "windows_app_restore_validated": False,
        "mac_reopen_validated": False,
        "audio_fidelity_validated": False,
        "runtime": {"python": platform.python_version(), "platform": platform.platform()},
        "tool_sha256": {Path(path).name: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                        for path in (__file__, native_project.__file__)},
    }


def main(argv=None):
    """Emit JSON evidence only after all checks pass; never take an output song."""
    import argparse
    import json
    import sys

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="Binary .screamseq or legacy .resonance container (read-only)")
    args = parser.parse_args(argv)
    try:
        report = qualify(args.source)
    except (OSError, ValueError, TypeError, OverflowError, RecursionError) as error:
        print(f"Project roundtrip qualification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
