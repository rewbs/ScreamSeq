"""Bounded actual-transport contract probes; known drift is never a parity pass."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import uuid

from project_tree import CORPUS, EvidenceError, differences, read_bytes, read_project, sha256
from native_roundtrip import ROOT, write_json
from fixture_loader import FixtureLoader

VECTORS = Path(__file__).with_name("api-vectors.json")
BASELINE = ROOT / "doc/api/platform-differences.json"


def schema_methods(path):
    schema = json.loads(read_bytes(path))
    methods = []
    for entry in schema["oneOf"]:
        method = entry["properties"]["method"]
        names = method.get("enum", [method.get("const")])
        if not names or any(type(name) is not str or not name for name in names):
            raise EvidenceError(f"Unrecognized method declaration in {path}")
        methods.extend(names)
    if len(methods) != len(set(methods)):
        raise EvidenceError(f"Duplicate schema method in {path}")
    return sorted(methods)


def substitute(value, variables):
    if isinstance(value, str) and value.startswith("$"):
        if value not in variables:
            raise EvidenceError(f"Unresolved vector variable {value}")
        return variables[value]
    if isinstance(value, list):
        return [substitute(child, variables) for child in value]
    if isinstance(value, dict):
        return {key: substitute(child, variables) for key, child in value.items()}
    return value


def run(client, output):
    platform_name = "windows" if sys.platform == "win32" else "macos"
    corpus = json.loads(read_bytes(VECTORS))
    baseline = json.loads(read_bytes(BASELINE))
    report = dict(format="screamseq-api-conformance-v1", platform=platform_name,
                  vectorSHA256=sha256(read_bytes(VECTORS)), baselineMatched=False,
                  toolSHA256=sha256(read_bytes(Path(__file__))),
                  schemaSHA256=sha256(read_bytes(ROOT / "mac/Tools/resonance-api.schema.json")),
                  baselineSHA256=sha256(read_bytes(BASELINE)),
                  parityComplete=False, cases=[], requests=[])
    output.mkdir(exist_ok=False)

    def call(method, params=None, request_id=None):
        request = dict(method=method, params=params or {}, requestID=request_id or uuid.uuid4().hex)
        report["requests"].append(request)
        try:
            result = client.call(method, params, request_id=request["requestID"])
            request["result"] = result
            return result
        except Exception as error:
            request["error"] = dict(type=type(error).__name__, message=str(error),
                                    code=getattr(error, "code", None), data=getattr(error, "data", None))
            raise

    def write(method, **params):
        return call(method, dict(expectedRevision=call("document.get")["revision"], **params))

    report["fixtureLoads"] = []
    loader = FixtureLoader(client, call, report["fixtureLoads"])

    def outcome(method, params, request_id=None):
        try:
            return "success", call(method, params, request_id)
        except Exception as error:
            if type(getattr(error, "code", None)) is not int or error.code not in {-32600, -32601, -32602, -32001}:
                raise  # No replacement or retry after a transport/unknown failure.
            return error.code, None

    try:
        description = call("api.describe")["data"]
        schema = schema_methods(ROOT / "mac/Tools/resonance-api.schema.json")
        reads, writes = description["reads"], description["writes"]
        if (len(reads) != len(set(reads)) or len(writes) != len(set(writes)) or set(reads) & set(writes)):
            raise EvidenceError("Advertised read/write methods overlap or contain duplicates")
        advertised = sorted(set(reads) | set(writes))
        report["inventory"] = dict(reads=sorted(reads), writes=sorted(writes),
            schemaMethods=schema, missingFromPlatform=sorted(set(schema) - set(advertised)),
            platformMethodsWithoutSharedSchema=sorted(set(advertised) - set(schema)))
        if schema != baseline["schema"]["methods"]:
            raise EvidenceError("Shared schema inventory changed; review and update the conformance baseline")
        expected_inventory = baseline["platforms"][platform_name]
        inventory_checked = "reads" in expected_inventory and "writes" in expected_inventory
        report["inventory"]["baselineStatus"] = "checked" if inventory_checked else "unqualified-platform-capture"
        if inventory_checked and (sorted(reads) != expected_inventory["reads"] or sorted(writes) != expected_inventory["writes"]):
            raise EvidenceError("Advertised method inventory changed; review the exact platform delta")
        known = {entry["id"] for entry in baseline["contracts"]}
        if any(vector.get("difference") not in known for vector in corpus["cases"] if vector.get("difference")):
            raise EvidenceError("Vector uses an unregistered platform difference")

        source = CORPUS / "fixtures/04-complete-reference.screamseq"
        original = read_project(source)
        loader.load(source)
        write("document.save", path=str(output / "before.screamseq"), overwrite=False)
        plugin = original["plugins"][0]
        graph = original["native"]["signalGraph"]["library"][0]
        node = next(node for node in graph["nodes"] if node["kind"] == "plugin")
        graph_parameters = call("graph.plugin.get", dict(graph=graph["id"], node=node["id"]))["data"]["parameters"]
        graph_parameter = next(parameter for parameter in graph_parameters if parameter["writable"])
        parameters = call("plugin.parameters.get", dict(slot=0))["data"]
        parameter = next(parameter for parameter in parameters if parameter["writable"])
        variables = {"$plugin": plugin["instanceID"], "$graph": graph["id"], "$node": node["id"],
                     "$parameter": parameter["id"], "$value": parameter["value"],
                     "$graphParameter": graph_parameter["id"], "$graphValue": graph_parameter["value"]}
        seen = set()
        for vector in corpus["cases"]:
            if vector["id"] in seen:
                raise EvidenceError("Duplicate vector ID")
            seen.add(vector["id"])
            before = call("document.get")
            params = substitute(vector["params"], variables)
            if vector.get("guard") == "fresh":
                params["expectedRevision"] = before["revision"]
            elif vector.get("guard") == "stale":
                params["expectedRevision"] = "conformance-intentionally-stale"
            result, _ = outcome(vector["method"], params)
            after = call("document.get")
            unchanged = differences(before, after)
            expected = vector["expected"][platform_name]
            expected_unchanged = vector.get("documentUnchanged", {}).get(platform_name, True)
            expected_paths = vector.get("changedPaths", {}).get(platform_name, [])
            actual_paths = [difference["path"] for difference in unchanged["differences"]]
            row = dict(id=vector["id"], observed=result, expected=expected,
                baselineMatched=result == expected and unchanged["equal"] == expected_unchanged
                    and actual_paths == expected_paths, documentUnchanged=unchanged,
                preservationPassed=unchanged["equal"],
                knownDifference=vector.get("difference"), resolutionPhase=vector.get("phase"))
            report["cases"].append(row)
            print(f"{'KNOWN DIFFERENCE' if row['knownDifference'] else 'CHECK'} {row['id']}: {result}", flush=True)
            if not unchanged["equal"]:
                # Retain the defect, then reset only the owned disposable song so
                # independent probes still execute. Never turn this into preservation.
                changed = output / (vector["id"] + ".screamseq")
                write("document.save", path=str(changed), overwrite=False)
                row["persistedDifference"] = differences(read_project(output / "before.screamseq"), read_project(changed))
                row["persistedOutput"] = changed.name
                row["baselineMatched"] = row["baselineMatched"] and (
                    row["persistedDifference"] == vector.get("persistedDifference", {}).get(platform_name))
                loader.load(source)

        # Invalid request ID reuse is deliberately different from uncertain write
        # replay. Neither adapter promises a durable cache. This probes one live cache.
        before = call("document.get")
        request_id = uuid.uuid4().hex
        rejected, _ = outcome("document.patch", dict(expectedRevision=before["revision"], speed=True), request_id)
        replay, _ = outcome("document.patch", dict(expectedRevision=before["revision"], title=before["data"]["title"]), request_id)
        expected = corpus["rejectedRequestIDReuse"][platform_name]
        unchanged = differences(before, call("document.get"))
        report["cases"].append(dict(id="rejected-request-id-reuse", observed=replay, expected=expected,
            firstRejection=rejected, knownDifference="API-REPLAY-ERROR", resolutionPhase="P6a",
            baselineMatched=rejected == -32602 and replay == expected and unchanged["equal"], documentUnchanged=unchanged))

        write("document.save", path=str(output / "after-noops.screamseq"), overwrite=False)
        report["noOpPersistence"] = differences(read_project(output / "before.screamseq"),
                                                read_project(output / "after-noops.screamseq"))
        if not report["noOpPersistence"]["equal"]:
            raise EvidenceError("No-op probes or explicit recovery did not restore the saved baseline")

        # One real shared edit checks atomic validation, dry-run, successful replay,
        # exact single Undo/Redo and persisted unrelated metadata/plugin state.
        pattern_before = call("pattern.get", dict(pattern=0))["data"]
        cell = pattern_before["cells"][0]
        patch = dict(pattern=0, row=cell["row"], channel=cell["channel"], note=61 if cell["note"] != 61 else 62)
        initial = call("document.get")
        preview = write("pattern.apply", cells=[patch], dryRun=True)
        if preview["changed"] or not differences(call("document.get"), initial)["equal"]:
            raise EvidenceError("Pattern dry-run changed document/history")
        code, _ = outcome("pattern.apply", dict(expectedRevision=initial["revision"],
                                               cells=[patch, dict(patch, row=65535)]))
        if code != -32602 or not differences(call("document.get"), initial)["equal"]:
            raise EvidenceError("Invalid batch was not rejected atomically")
        request_id = uuid.uuid4().hex
        params = dict(expectedRevision=initial["revision"], cells=[patch])
        applied = call("pattern.apply", params, request_id)
        applied_doc = call("document.get")
        repeated = call("pattern.apply", params, request_id)
        if not differences(applied, repeated)["equal"] or not differences(call("document.get"), applied_doc)["equal"] or not applied["changed"]:
            raise EvidenceError("Successful replay applied twice or lost its exact response")
        reused, _ = outcome("pattern.apply", dict(expectedRevision=applied_doc["revision"], cells=[patch]), request_id)
        if reused != -32600 or not differences(call("document.get"), applied_doc)["equal"]:
            raise EvidenceError("A retained successful ID accepted different request content")
        no_op = write("pattern.apply", cells=[patch])
        if no_op["changed"] or not differences(call("document.get"), applied_doc)["equal"]:
            raise EvidenceError("Repeated current cell value created a new edit/history entry")
        edited = call("pattern.get", dict(pattern=0))["data"]
        expected_pattern = json.loads(json.dumps(pattern_before))
        expected_pattern["cells"][0]["note"] = patch["note"]
        if not differences(edited, expected_pattern)["equal"]:
            raise EvidenceError("Cell patch changed unrelated pattern fields")
        write("history.undo", domain="document")
        if not differences(call("pattern.get", dict(pattern=0))["data"], pattern_before)["equal"]:
            raise EvidenceError("One Undo did not restore the exact prior pattern")
        write("history.redo", domain="document")
        if not differences(call("pattern.get", dict(pattern=0))["data"], edited)["equal"]:
            raise EvidenceError("One Redo did not restore the exact edited pattern")
        destination = output / "edited.screamseq"
        write("document.save", path=str(destination), overwrite=False)
        saved = read_project(destination)
        project_baseline = read_project(output / "before.screamseq")
        report["unrelatedPersistence"] = differences({k: v for k, v in project_baseline.items() if k != "module"},
                                                     {k: v for k, v in saved.items() if k != "module"})
        if not report["unrelatedPersistence"]["equal"]:
            raise EvidenceError("Single tracker cell edit changed native/plugin/other container state")
        loader.load(destination)
        if not differences(call("pattern.get", dict(pattern=0))["data"], edited)["equal"]:
            raise EvidenceError("Saved cell edit did not reopen exactly")
        report["editHistoryReplayPersistence"] = True
        report["baselineMatched"] = inventory_checked and all(row["baselineMatched"] for row in report["cases"])
        report["knownDifferences"] = sorted({row["knownDifference"] for row in report["cases"] if row["knownDifference"]})
        report["preservationFailures"] = [row["id"] for row in report["cases"] if not row["documentUnchanged"]["equal"]]
        # Known drift is still a release defect, even if the evidence ratchet matches.
        report["parityComplete"] = False
        report["limitations"] = ["Mac predictions require actual socket execution",
            "Not every advertised API parameter or result contract is exercised",
            "Mac inventory must be reviewed and pinned after its first current native run",
            "No playback, physical input, third-party provider or cache eviction qualification"]
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        write_json(output / "report.json", report)
