"""Qualify file-only zero-duration defaults without changing the codec baseline.

The original F04 and all generated inputs remain immutable. Each save must match
one of two explicit wire representations: the omitted field, or integer zero at
that same command. No unrelated field, identity, opaque state or PCM may change.
"""
import copy
import plistlib

from fixture_loader import FixtureLoader
from native_roundtrip import write_json
from project_tree import CORPUS, EvidenceError, differences, read_bytes, read_project, sha256


def run(client, output):
    output.mkdir(exist_ok=False)
    source = CORPUS / "fixtures/04-complete-reference.screamseq"
    source_bytes = read_bytes(source)
    original = read_project(source)
    report = dict(format="screamseq-duration-defaults-v1", passed=False,
                  sourceSHA256=sha256(source_bytes), cases=[], fixtureLoads=[])
    loader = FixtureLoader(client, client.call, report["fixtureLoads"])
    inputs = {}
    try:
        for name, index in (("parameter-set", 2), ("scratch", 3), ("scratch-stop", 4)):
            candidate = copy.deepcopy(original)
            command = candidate["native"]["performance"]["commands"][index]
            if command.pop("duration") != 0:
                raise EvidenceError("Duration fixture no longer targets a zero-duration command")
            path = output / (name + ".screamseq")
            with path.open("xb") as stream:
                stream.write(plistlib.dumps(candidate, fmt=plistlib.FMT_BINARY, sort_keys=False))
            inputs[path] = sha256(read_bytes(path))
            row = dict(id=name, commandIndex=index, inputSHA256=inputs[path], saves=[])
            report["cases"].append(row)
            loader.load(path)
            # Two separate loads/saves prove the accepted command also survives
            # reopening the output. Mac's fixture loader uses private recovery;
            # this does not claim qualification of its native Open panel.
            for leg in ("saved", "reopened"):
                document = client.call("document.get")
                row[leg + "Warnings"] = document["data"]["loadWarnings"]
                row[leg + "RequiresSaveAs"] = document["data"]["requiresSaveAs"]
                if row[leg + "Warnings"] or row[leg + "RequiresSaveAs"]:
                    raise EvidenceError(f"{name}: zero default caused recovery warnings/protection")
                saved = output / (name + "-" + leg + ".screamseq")
                client.call("document.save", dict(expectedRevision=document["revision"], path=str(saved)))
                tree = read_project(saved)
                omitted, explicit = differences(candidate, tree), differences(original, tree)
                row["saves"].append(dict(path=saved.name, sha256=sha256(read_bytes(saved)),
                                         omittedComparison=omitted, explicitZeroComparison=explicit))
                if not (omitted["equal"] or explicit["equal"]):
                    raise EvidenceError(f"{name}: save changed more than the optional integer-zero field")
                loader.load(saved)
                reopened = client.call("document.get")["data"]
                if reopened["loadWarnings"] or reopened["requiresSaveAs"]:
                    raise EvidenceError(f"{name}: saved representation reopened with recovery warnings/protection")
            print(f"DURATION {name}: retained exactly through two saves/reopens", flush=True)
        report["passed"] = True
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report["originalUnchanged"] = read_bytes(source) == source_bytes
        report["inputsUnchanged"] = all(sha256(read_bytes(path)) == digest for path, digest in inputs.items())
        if not report["originalUnchanged"] or not report["inputsUnchanged"]:
            report["passed"] = False
        write_json(output / "report.json", report)
