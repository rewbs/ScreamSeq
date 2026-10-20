"""Strict, typed property-tree evidence for native ScreamSeq project exchanges.

This is a comparator, not a project loader or a compatibility normalizer. It
never edits a project, strips unknown fields, coerces numbers, or ignores IDs.
Only the explicit record command writes a golden manifest.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import sys
from xml.parsers.expat import ExpatError

FORMAT = "screamseq-typed-tree-v1"
MAX_BYTES = 600 * 1024 * 1024
MAX_DEPTH = 256
CORPUS = Path(__file__).resolve().parents[1] / "Fixtures" / "Parity20261009"


class EvidenceError(ValueError):
    pass


class UniqueDictionary(dict):
    def __setitem__(self, key, value):
        if key in self:
            raise EvidenceError(f"Duplicate property-list dictionary key: {key!r}")
        super().__setitem__(key, value)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_bytes(path: Path, limit: int = MAX_BYTES) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise EvidenceError(f"Input exceeds {limit} bytes: {path}")
    return data


def read_project(path: Path):
    data = read_bytes(path)
    try:
        value = plistlib.loads(data, dict_type=UniqueDictionary)
    except (ValueError, TypeError, OverflowError, RecursionError, struct.error, ExpatError) as error:
        raise EvidenceError(f"Invalid property list {path}: {error}") from error
    if not isinstance(value, dict):
        raise EvidenceError(f"Native project root must be a dictionary: {path}")
    return value


def pointer(path: str, key: str) -> str:
    return path + "/" + key.replace("~", "~0").replace("/", "~1")


def typed(value, *, _ancestors=frozenset(), _depth=0):
    """Canonical tagged values; dictionaries unordered, arrays ordered.

    Integers use decimal strings (no floating conversion); real values use their
    exact IEEE-754 binary64 representation, including signed zero and NaN bits.
    Opaque data compares length plus SHA-256, never decoded/normalized contents.
    """
    if _depth > MAX_DEPTH:
        raise EvidenceError("Property tree exceeds the comparison depth limit")
    if value is None:
        return ["null"]
    if type(value) is bool:
        return ["bool", value]
    if type(value) is int:
        return ["integer", str(value)]
    if type(value) is float:
        return ["real64", struct.pack(">d", value).hex()]
    if type(value) is str:
        return ["string", value]
    if type(value) is bytes:
        return ["data", len(value), sha256(value)]
    if type(value) is plistlib.UID:
        return ["uid", str(value.data)]
    if type(value) is datetime.datetime:
        if value.tzinfo is not None:
            raise EvidenceError("Unexpected timezone-aware property-list date")
        return ["date-utc", value.isoformat(timespec="microseconds")]
    if not isinstance(value, (dict, list)):
        raise EvidenceError(f"Unsupported property-tree value: {type(value).__name__}")
    if id(value) in _ancestors:
        raise EvidenceError("Cyclic property tree cannot be compared")
    ancestors = _ancestors | {id(value)}
    convert = lambda child: typed(child, _ancestors=ancestors, _depth=_depth + 1)
    if isinstance(value, dict):
        if any(type(key) is not str for key in value):
            raise EvidenceError("Property-list dictionary keys must be strings")
        return ["dictionary", [[key, convert(value[key])] for key in sorted(value)]]
    return ["array", [convert(child) for child in value]]


def digest(value) -> str:
    canonical = json.dumps(typed(value), ensure_ascii=True, separators=(",", ":"))
    return sha256(canonical.encode("ascii"))


def differences(left, right, limit=100):
    """Bound report size, not equality checks: truncated differences still fail."""
    if limit < 1:
        raise EvidenceError("Difference limit must be positive")
    a, b = typed(left), typed(right)
    result = []
    total = 0

    def add(path, kind, before, after):
        nonlocal total
        total += 1
        if len(result) < limit:
            result.append(dict(path=path or "/", kind=kind, before=summary(before), after=summary(after)))

    def summary(node):
        if isinstance(node, list) and node[0] in ("array", "dictionary"):
            encoded = json.dumps(node, ensure_ascii=True, separators=(",", ":")).encode("ascii")
            return dict(type=node[0], count=len(node[1]), typedSHA256=sha256(encoded))
        if isinstance(node, list) and node[0] == "string" and len(node[1]) > 160:
            return dict(type="string", characters=len(node[1]), preview=node[1][:160],
                        sha256=sha256(node[1].encode("utf-8", errors="surrogatepass")))
        return node

    def visit(x, y, path):
        if x[0] != y[0]:
            add(path, "type", x[0], y[0])
        elif x[0] == "dictionary":
            first, second = dict(x[1]), dict(y[1])
            for key in sorted(first.keys() | second.keys()):
                child = pointer(path, key)
                if key not in first:
                    add(child, "added", None, second[key])
                elif key not in second:
                    add(child, "removed", first[key], None)
                else:
                    visit(first[key], second[key], child)
        elif x[0] == "array":
            if len(x[1]) != len(y[1]):
                add(path, "array-length", len(x[1]), len(y[1]))
            for index, (first, second) in enumerate(zip(x[1], y[1])):
                visit(first, second, pointer(path, str(index)))
        elif x != y:
            add(path, "value", x, y)

    visit(a, b, "")
    return dict(equal=total == 0, differenceCount=total, truncated=total > len(result), differences=result)


def corpus_inventory(root: Path):
    root = root.resolve()
    provenance = json.loads(read_bytes(root / "provenance.json"))
    files = {}
    for member in provenance["files"]:
        relative = member["path"]
        path = (root / relative).resolve()
        if Path(relative).is_absolute() or not path.is_relative_to(root) or relative in files:
            raise EvidenceError(f"Invalid or duplicate corpus path: {relative}")
        if type(member["bytes"]) is not int or member["bytes"] < 0:
            raise EvidenceError(f"Invalid corpus byte count: {relative}")
        data = read_bytes(path)
        if len(data) != member["bytes"] or sha256(data) != member["sha256"]:
            raise EvidenceError(f"Original archive member changed: {relative}")
        files[relative] = member
    projects = {}
    for relative in sorted(files):
        if relative.endswith(".screamseq"):
            value = read_project(root / relative)
            projects[relative] = dict(fileSHA256=files[relative]["sha256"], typedSHA256=digest(value),
                                     rootMembers={key: digest(value[key]) for key in sorted(value)})
    if len(projects) != 5:
        raise EvidenceError(f"Expected all five original projects, found {len(projects)}")
    return dict(format=FORMAT, referenceBaseline=provenance["referenceBaseline"],
                archiveSHA256=provenance["archiveSha256"], members=files, projects=projects)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    compare = sub.add_parser("compare", help="Compare two native files without any normalization")
    compare.add_argument("left", type=Path)
    compare.add_argument("right", type=Path)
    compare.add_argument("--limit", type=int, default=100)
    for name in ("verify-corpus", "record-corpus"):
        command = sub.add_parser(name)
        command.add_argument("--corpus", type=Path, default=CORPUS)
        command.add_argument("--golden", type=Path, default=Path(__file__).with_name("parity-typed-goldens.json"))
    args = parser.parse_args(argv)
    try:
        if args.command == "compare":
            left, right = read_project(args.left), read_project(args.right)
            report = dict(format=FORMAT, left=str(args.left), right=str(args.right),
                          leftTypedSHA256=digest(left), rightTypedSHA256=digest(right),
                          **differences(left, right, args.limit))
            print(json.dumps(report, indent=2, ensure_ascii=True))
            return 0 if report["equal"] else 1
        inventory = corpus_inventory(args.corpus)
        if args.command == "record-corpus":
            # Deliberate recording is the only write. Existing goldens cannot be
            # silently replaced during verification or on a failed comparison.
            with args.golden.open("x", encoding="utf-8", newline="\n") as output:
                json.dump(inventory, output, indent=2, ensure_ascii=True)
                output.write("\n")
            print(f"Recorded {len(inventory['projects'])} typed goldens: {args.golden}")
            return 0
        expected = json.loads(read_bytes(args.golden))
        if inventory != expected:
            raise EvidenceError("Typed corpus differs from the pinned golden inventory")
        print(f"Verified all five typed goldens and every original archive member ({FORMAT})")
        return 0
    except (EvidenceError, OSError, KeyError, json.JSONDecodeError) as error:
        print(f"Conformance error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
