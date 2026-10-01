"""Independent test-only json/plistlib oracle; never imported by the app.

Fixtures are generated from the authoritative Mac schema, not Mac-produced
files. Unknown-root fixtures deliberately extend that schema: their preservation
is NOT evidence that the current strict Mac reader accepts unknown root fields.
"""
import copy
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile


CURVES = (
    "step", "linear", "smooth", "exponential", "logarithmic", "step-next",
    "exponential-reverse", "logarithmic-reverse", "scripted",
)


def scratch():
    paths = [Path(os.environ[k]).resolve() for k in ("TMPDIR", "TEMP", "TMP")]
    assert paths[0] == paths[1] == paths[2], paths
    assert paths[0].parts[-3:] == ("hermes", "cache", "scratch"), paths
    print("Verified TMPDIR=TEMP=TMP=" + str(paths[0]), flush=True)
    return paths[0]


def generated_root():
    entries = []
    for i, curve in enumerate(CURVES):
        point = {"position": 0, "value": 0.25, "curve": curve}
        if curve == "scripted":
            point["formula"] = "start+(end-start)*t"
        entries.append({
            "id": f"2517E627-B66B-4165-B582-{i:012X}",
            "name": "Generated 包絡線 🎵 " + curve,
            "shape": {
                "span": 16384, "rowsPerBeat": 4, "instrument": False,
                "flags": 0, "markers": [0, 0, 0, 0, 4294967295],
                "points": [point, {"position": 16383, "value": 0.75, "curve": "linear"}],
            },
        })
    return {"version": 1, "revision": "catalogue:90E20E5C-C04A-41E2-AD65-7BE337014128", "entries": entries}


def typed_equal(a, b):
    assert type(a) is type(b), (type(a), type(b), a, b)
    if isinstance(a, float):
        assert struct.pack(">d", a) == struct.pack(">d", b), (a, b)
    elif isinstance(a, dict):
        assert a.keys() == b.keys(), (a.keys(), b.keys())
        for key in a:
            typed_equal(a[key], b[key])
    elif isinstance(a, list):
        assert len(a) == len(b)
        for x, y in zip(a, b):
            typed_equal(x, y)
    else:
        assert a == b, (a, b)


def unique_object(pairs):
    result = {}
    for k, v in pairs:
        assert k not in result, "duplicate key in produced JSON"
        result[k] = v
    return result


def reject_constant(value):
    raise AssertionError("non-finite JSON: " + value)


def load_json(raw):
    # JSON permits bare -0. Python's default integer parser discards its sign;
    # interpret that token as IEEE negative zero for a sign-preserving oracle.
    return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject_constant,
                      parse_int=lambda token: -0.0 if token == "-0" else int(token))


def run_case(executable, directory, kind):
    root = generated_root()
    if kind != "schema-json":
        root["future"] = {
            "integerZero": 0, "realZero": -0.0, "integerOne": 1, "realOne": 1.0,
            "true": True, "false": False, "null": None,
            "minimum": -(2**63), "maximum": 2**64 - 1,
            "exactBeyondDouble": 9007199254740993,
            "smallest": float.fromhex("0x0.0000000000001p-1022"),
            "largest": float.fromhex("0x1.fffffffffffffp+1023"),
            "nested": {"日本語 🎹": ["string", "embedded\0NUL", [], {}]},
        }
        if kind == "opaque-plist":
            del root["future"]["null"]  # plist has no JSON-null scalar.
            root["future"]["date"] = datetime.datetime(2020, 2, 3, 4, 5, 6, 125000)
            root["future"]["uid"] = plistlib.UID(0xFF0100)
            root["future"]["data"] = bytes([0, 255, 1, 0, 128])
    # Deliberately opposite extensions prove content detection, not suffix routing.
    path = directory / (kind + (".json" if kind == "opaque-plist" else ".plist"))
    if kind == "opaque-plist":
        original = plistlib.dumps(root, fmt=plistlib.FMT_BINARY, sort_keys=True)
        typed_equal(root, plistlib.loads(original))  # Ensure oracle fixture did not collapse types/zero sign.
        decode = plistlib.loads
    else:
        original = json.dumps(root, ensure_ascii=False, indent=2, allow_nan=False).encode("utf-8")
        if kind == "future-json":
            original = original.replace(b'"realZero": -0.0', b'"realZero": -0')
        typed_equal(root, load_json(original))
        decode = load_json
    path.write_bytes(original)
    scratch()  # Verify immediately before every fixture-consuming subprocess.
    completed = subprocess.run([str(executable), "oraclePublish", str(path)], capture_output=True, timeout=60)
    assert completed.returncode == 0, (completed.returncode, completed.stdout, completed.stderr)
    published = path.read_bytes()
    assert published != original
    assert published.startswith(b"bplist00") == (kind == "opaque-plist")
    actual = decode(published)
    expected = copy.deepcopy(root)
    expected["entries"][0]["name"] = "Published 日本語 🎶"
    assert actual["revision"] != root["revision"]
    assert actual["revision"].startswith("catalogue:")
    expected["revision"] = actual["revision"]
    typed_equal(expected, actual)
    response = load_json(completed.stdout)
    assert response["revision"] == "catalogue:" + hashlib.sha256(published).hexdigest()
    typed_equal(response["entries"], actual["entries"])
    assert set(response) == {"version", "revision", "entries"}
    print("PASS independent oracle:", kind, "9 curves; typed values; no-op/dry-run; publication; reopen", flush=True)


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="envelope-oracle-", dir=scratch()) as name:
        directory = Path(name) / "目録-🎵"
        directory.mkdir()
        for kind in ("schema-json", "future-json", "opaque-plist"):
            run_case(executable, directory, kind)
    print("PASS all 3 generated oracle cases (no Mac application reopen)")


if __name__ == "__main__":
    main()
