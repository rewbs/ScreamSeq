"""Generate owned F04 derivatives and observe native recovery without normalizing.

This corpus deliberately includes incompatible inputs. Accepted loads are not parity
passes: reports retain warnings, source protection, exact typed differences and files.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path
import plistlib
import sys

from project_tree import CORPUS, EvidenceError, differences, digest, read_bytes, read_project, sha256
from native_roundtrip import ROOT, write_json

BASELINE = ROOT / "doc/api/codec-observations.json"


def observations(report, output):
    """Compact exact classifications; full RPCs and differences remain in report."""
    rows = []
    for row in report["cases"]:
        item = {key: row[key] for key in ("id", "question", "inputSHA256", "accepted")}
        if row["accepted"]:
            item.update(requiresSaveAs=row["requiresSaveAs"], loadWarnings=row["loadWarnings"],
                savedTypedSHA256=digest(read_project(output / (row["id"] + "-saved.screamseq"))),
                differenceCount=row.get("typedChanges", {}).get("differenceCount"))
        else:
            item["rejectionCode"] = row["rejectionCode"]
        rows.append(item)
    return rows


def cases(original):
    values = []

    def add(name, change, question):
        tree = copy.deepcopy(original)
        change(tree)
        values.append((name, plistlib.dumps(tree, fmt=plistlib.FMT_BINARY, sort_keys=False), question))

    sentinel = {"bool": True, "integer": 9007199254740993, "real": 1.0,
                "signedZero": -0.0, "bytes": b"\x00\xffparity\x00", "text": "music \U0001f3b5"}
    add("unknown-root", lambda p: p.update(parityUnknown=sentinel), "Root extension preservation versus lossy warning")
    add("unknown-native", lambda p: p["native"].update(parityUnknown=sentinel), "Native extension preservation and source protection")
    add("unknown-track", lambda p: p["native"]["tracks"][0][1].update(parityUnknown=sentinel), "Stable entity extension preservation")
    add("unknown-node", lambda p: p["native"]["signalGraph"]["library"][0]["nodes"][2].update(parityUnknown=sentinel),
        "Identity-array extension preservation at a graph processor")
    add("reordered-tracks", lambda p: p["native"]["tracks"].reverse(), "Stable indexed identities versus array ordering")
    add("duplicate-track-id", lambda p: p["native"]["tracks"][1][1].update(id=p["native"]["tracks"][0][1]["id"]),
        "Invalid identity recovery and explicit loss/source protection")
    add("deleted-track-record", lambda p: p["native"]["tracks"].pop(), "Incomplete identity layer recovery")
    add("future-container", lambda p: p.update(version=7), "Future outer version best-effort recovery classification")
    add("future-native", lambda p: p["native"].update(version=18), "Future metadata version recovery classification")
    add("precise-zero-effect-parameter", lambda p: p["native"]["preciseNotes"][0].update(effect=0, parameter=37),
        "F25: optional parameter preservation when effect is zero; API reachability is checked separately")
    add("missing-non-nudge-duration", lambda p: p["native"]["performance"]["commands"][0].pop("duration"),
        "F25: absent duration default versus record recovery")
    add("missing-scratch-library", lambda p: p["native"].pop("scratchGestures"),
        "Missing optional library with retained SK references; do not assume lossless default")
    add("integer-real-next-id", lambda p: p["native"].update(nextID=float(p["native"]["nextID"])),
        "Integral real versus integer metadata validation")
    add("boolean-next-id", lambda p: p["native"].update(nextID=True), "Boolean must not silently become a native allocator ID")
    add("fractional-next-id", lambda p: p["native"].update(nextID=41.5), "Fractional identity recovery")
    add("unsafe-next-id", lambda p: p["native"].update(nextID=9007199254740993), "Unsafe integer identity must not silently round")
    add("supplementary-unicode", lambda p: p["native"]["tracks"][0][1].update(name="Pulse \U0001f3b5 e\u0301"),
        "Exact supplementary-plane and decomposed Unicode preservation")
    add("corrupt-required-module", lambda p: p.update(module=b"not an OpenMPT snapshot"),
        "Required core failure must preserve the previously loaded document")

    def missing_plugin(p, format):
        p["plugins"][0].update(format=format, name="Unavailable parity processor", path="/ParityMissing/Absent.vst3" if format == "VST3" else "",
            classID="00112233445566778899AABBCCDDEEFF" if format == "VST3" else "",
            type=int.from_bytes(b"aufx", "big") if format == "AU" else 0,
            subtype=int.from_bytes(b"SqNo", "big") if format == "AU" else 0,
            manufacturer=int.from_bytes(b"SqQA", "big") if format == "AU" else 0,
            state=b"\x00\xffopaque missing provider state\x00\x80")

    for format in ("AU", "VST3"):
        add("missing-" + format.lower(), lambda p, f=format: missing_plugin(p, f),
            "Unavailable provider must retain stable identity, routes and exact opaque state; audio is not started")
    encoded = copy.deepcopy(original)
    marker = "SCREAMSEQ_PARITY_INVALID_ASCII_SENTINEL"
    encoded["parityInvalidEncoding"] = marker
    data = plistlib.dumps(encoded, fmt=plistlib.FMT_BINARY, sort_keys=False)
    if data.count(marker.encode()) != 1:
        raise EvidenceError("Invalid-encoding probe marker is not unique")
    values.append(("invalid-ascii-string", data.replace(marker.encode(), b"\xff" + marker.encode()[1:]),
                   "Invalid property-list string encoding must not corrupt the current document"))
    return values


def run(client, output):
    output.mkdir(exist_ok=False)
    source = CORPUS / "fixtures/04-complete-reference.screamseq"
    original_bytes = read_bytes(source)
    baseline = json.loads(read_bytes(BASELINE))
    report = dict(format="screamseq-codec-vectors-v1", platform=sys.platform,
        sourceSHA256=sha256(original_bytes), toolSHA256=sha256(read_bytes(Path(__file__))),
        baselineSHA256=sha256(read_bytes(BASELINE)), baselineMatched=False,
        observationComplete=False, safetyPassed=False, parityComplete=False, cases=[], requests=[])
    inputs = {}

    def call(method, params=None):
        entry = dict(method=method, params=params or {})
        report["requests"].append(entry)
        try:
            entry["result"] = client.call(method, params)
            return entry["result"]
        except Exception as error:
            entry["error"] = dict(type=type(error).__name__, code=getattr(error, "code", None), message=str(error), data=getattr(error, "data", None))
            raise

    def write(method, **params):
        return call(method, dict(expectedRevision=call("document.get")["revision"], **params))

    from fixture_loader import FixtureLoader
    report["fixtureLoads"] = []
    loader = FixtureLoader(client, call, report["fixtureLoads"])
    try:
        for name, data, question in cases(read_project(source)):
            path, saved = output / (name + ".screamseq"), output / (name + "-saved.screamseq")
            with path.open("xb") as stream:
                stream.write(data)
            inputs[path] = sha256(data)
            row = dict(id=name, question=question, inputSHA256=sha256(data), accepted=False)
            report["cases"].append(row)
            try:
                input_tree = read_project(path)
            except EvidenceError as error:
                input_tree = None
                row["strictComparatorInputError"] = str(error)
            loader.load(source)
            before = call("document.get")
            try:
                effective_source = loader.load(path)
                row["loadPath"] = report["fixtureLoads"][-1]["method"]
                row["accepted"] = True
            except Exception as error:
                # Explicit load rejection is evidence; transport/unknown outcomes
                # cannot be safely followed with another document replacement.
                if type(getattr(error, "code", None)) is not int or error.code not in {-32602, -32003}:
                    raise
                if isinstance(getattr(error, "data", None), dict) and error.data.get("writeOutcome") in {"unknown", "committed"}:
                    raise
                after = call("document.get")
                row["rejectionCode"] = error.code
                row["rejectedLoadPreservedDocument"] = differences(before, after)
                if not row["rejectedLoadPreservedDocument"]["equal"]:
                    raise EvidenceError(f"Rejected load changed the document: {name}") from error
            if row["accepted"]:
                loaded = call("document.get")
                row["loaded"] = loaded
                row["requiresSaveAs"] = loaded["data"]["requiresSaveAs"]
                row["loadWarnings"] = loaded["data"]["loadWarnings"]
                if row["requiresSaveAs"]:
                    try:
                        write("document.save", path=str(effective_source), overwrite=True)
                    except Exception as error:
                        if type(getattr(error, "code", None)) is not int or error.code not in {-32602, -32003}:
                            raise
                        if isinstance(getattr(error, "data", None), dict) and error.data.get("writeOutcome") in {"unknown", "committed"}:
                            raise
                        row["protectedOverwriteRejected"] = error.code
                    else:
                        raise EvidenceError(f"Protected source overwrite was accepted: {name}")
                    if not differences(loaded, call("document.get"))["equal"]:
                        raise EvidenceError(f"Rejected overwrite changed the document: {name}")
                write("document.save", path=str(saved), overwrite=False)
                saved_tree = read_project(saved)
                row["savedSHA256"] = sha256(read_bytes(saved))
                if input_tree is not None:
                    row["typedChanges"] = differences(input_tree, saved_tree)
                if name.startswith("missing-") and name in {"missing-au", "missing-vst3"}:
                    row["opaquePluginPreserved"] = differences(input_tree["plugins"], saved_tree["plugins"])
                    if not row["opaquePluginPreserved"]["equal"]:
                        raise EvidenceError(f"Unavailable plugin lost its identity or opaque state: {name}")
                if read_bytes(effective_source) != data:
                    raise EvidenceError(f"Probe overwrote its effective load source: {name}")
                loader.load(saved)
                row["canonicalReopened"] = call("document.get")
            row["sourceUnchanged"] = read_bytes(path) == data
            if not row["sourceUnchanged"]:
                raise EvidenceError(f"Probe overwrote its input: {name}")
            print(f"CODEC {name}: {'accepted' if row['accepted'] else 'rejected'}", flush=True)
        report["observationComplete"] = True
        report["safetyPassed"] = True
        expected = baseline["windows" if sys.platform == "win32" else "macos"]
        report["baselineStatus"] = "checked" if expected is not None else "unqualified-platform-capture"
        report["baselineMatched"] = (report["sourceSHA256"] == baseline["originalFixtureSHA256"]
                                     and expected is not None and observations(report, output) == expected)
        report["limitations"] = ["Load/save observations require cross-platform classification before codec convergence",
            "Accepted recovery and a canonical reopen do not prove lossless preservation",
            "Unavailable-plugin audio preparation, edited identity-array merges and allocator migration are separate gates"]
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        try:
            report["originalUnchanged"] = read_bytes(source) == original_bytes
            report["inputsUnchanged"] = {path.name: sha256(read_bytes(path)) == expected for path, expected in inputs.items()}
            if not report["originalUnchanged"] or not all(report["inputsUnchanged"].values()):
                report["safetyPassed"] = False
        except OSError as error:
            report["safetyPassed"] = False
            report["preservationError"] = str(error)
        write_json(output / "report.json", report)
