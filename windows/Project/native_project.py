"""Portable plist qualification layer, not C++ application persistence.

Keep the complete plist tree: never rebuild it from selected known fields.
Song snapshots and plugin state are opaque; no plugin is loaded or substituted.
"""
import plistlib
import struct


MAX_CONTAINER_VERSION = 5
MAX_NATIVE_VERSION = 14
MAX_PROJECT_BYTES = 600 * 1024 * 1024
MAX_MODULE_BYTES = 512 * 1024 * 1024
SNAPSHOT_MAGICS = (b"RSONGS1\0", b"RSONGS2\0")


def split_snapshot(data: bytes) -> tuple[memoryview, memoryview, memoryview]:
    """Validate framing and return read-only module/sample/timing byte views.

    This does not decode the inner module, sample archive or timing archive.
    """
    if not isinstance(data, bytes) or data[:8] not in SNAPSHOT_MAGICS:
        raise ValueError("Unsupported native song snapshot magic/version")
    if len(data) > MAX_MODULE_BYTES:
        raise ValueError("Native song snapshot exceeds size limit")
    timed = data[:8] == SNAPSHOT_MAGICS[1]
    header_size = 20 if timed else 16
    if len(data) < header_size:
        raise ValueError("Truncated native song snapshot header")
    lengths = struct.unpack_from("<III" if timed else "<II", data, 8)
    if not all(lengths) or header_size + sum(lengths) != len(data):
        raise ValueError("Invalid native song snapshot lengths")
    module_end = header_size + lengths[0]
    samples_end = module_end + lengths[1]
    if lengths[0] >= 8 and data[header_size:header_size + 8] in SNAPSHOT_MAGICS:
        raise ValueError("Nested native song snapshots are not supported")
    view = memoryview(data)
    return view[header_size:module_end], view[module_end:samples_end], view[samples_end:]


def _version(value, maximum, label):
    if type(value) is not int or not 1 <= value <= maximum:
        raise ValueError(f"Unsupported {label} version: {value!r}")


def _validate(root):
    if not isinstance(root, dict):
        raise ValueError("Project root must be a dictionary")
    _version(root.get("version"), MAX_CONTAINER_VERSION, "container")
    if not isinstance(root.get("module"), bytes) or not root["module"]:
        raise ValueError("Embedded module must be nonempty data")
    if len(root["module"]) > MAX_MODULE_BYTES:
        raise ValueError("Embedded module exceeds size limit")
    if root["version"] >= 4:
        split_snapshot(root["module"])
    elif root["module"].startswith(b"RSONGS"):
        raise ValueError("Native song snapshot does not match legacy container version")
    if not isinstance(root.get("plugins"), list):
        raise ValueError("Plugins must be an array")
    if root["version"] >= 3 or "native" in root:
        native = root.get("native")
        if not isinstance(native, dict):
            raise ValueError("Native metadata must be a dictionary")
        _version(native.get("version"), MAX_NATIVE_VERSION, "native metadata")
    if "sequence" in root:
        sequence = root["sequence"]
        if type(sequence) is not int or not 0 <= sequence <= 255:
            raise ValueError("Invalid sequence selection")


def loads(data: bytes) -> dict:
    """Decode a project tree without dropping unknown property-list fields."""
    if not isinstance(data, bytes) or not data.startswith(b"bplist00"):
        raise ValueError("Expected binary plist header bplist00")
    if len(data) > MAX_PROJECT_BYTES:
        raise ValueError("Project exceeds size limit")
    root = plistlib.loads(data, fmt=plistlib.FMT_BINARY)
    _validate(root)
    return root


def dumps(root: dict) -> bytes:
    """Encode the entire tree; plist object-table layout may change."""
    _validate(root)
    data = plistlib.dumps(root, fmt=plistlib.FMT_BINARY, sort_keys=False)
    if len(data) > MAX_PROJECT_BYTES:
        raise ValueError("Project exceeds size limit")
    return data


def main(argv=None) -> int:
    """Qualify container framing, optionally repacking to a new file only."""
    import argparse
    import json
    import sys

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", help="Binary .screamseq or legacy .resonance container")
    parser.add_argument("--repack", metavar="NEW_FILE", help="Write binary plist; never overwrite")
    args = parser.parse_args(argv)
    try:
        with open(args.source, "rb") as source:
            root = loads(source.read(MAX_PROJECT_BYTES + 1))
        report = {"container_version": root["version"],
                  "native_metadata_version": root.get("native", {}).get("version"),
                  "plugin_count": len(root["plugins"]),
                  "snapshot": root["version"] >= 4,
                  "song_semantics_validated": False}
        if args.repack:
            data = dumps(root)  # Validate completely before creating any destination.
            with open(args.repack, "xb") as destination:
                destination.write(data)
            report["repacked_to"] = args.repack
        print(json.dumps(report))
        return 0
    except (OSError, ValueError, TypeError, OverflowError, RecursionError) as error:
        print(f"Project qualification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
