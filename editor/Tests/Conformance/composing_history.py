"""F04 cross-domain chronological history through the actual native API.

This covers the stopped composing transaction journey, not live audio admission,
foreground controls or the complete A01/A18 acceptance gates.
"""
from pathlib import Path

from edited_metadata import editable_copy
from fixture_loader import FixtureLoader
from native_roundtrip import write_json
from project_tree import CORPUS, EvidenceError, differences, digest, read_bytes, read_project, sha256


def run(client, output):
    output.mkdir(exist_ok=False)
    source = CORPUS / "fixtures/04-complete-reference.screamseq"
    original = read_project(source)
    immutable = {source: sha256(read_bytes(source))}
    report = dict(format="screamseq-composing-history-v1", passed=False, parityComplete=False,
                  sourceSHA256=immutable[source], toolSHA256=sha256(read_bytes(Path(__file__))),
                  requests=[], fixtureLoads=[], saves=[])

    def require(condition, message):
        if not condition:
            raise EvidenceError(message)

    def call(method, params=None):
        row = dict(method=method, params=params or {})
        report["requests"].append(row)
        try:
            row["result"] = client.call(method, params)
            return row["result"]
        except Exception as error:
            row["error"] = dict(type=type(error).__name__, code=getattr(error, "code", None),
                                message=str(error), data=getattr(error, "data", None))
            raise

    def read(method, **params):
        return call(method, params)["data"]

    def write(method, **params):
        return call(method, dict(expectedRevision=call("document.get")["revision"], **params))

    def save(name, expected=None):
        path = output / (name + ".screamseq")
        write("document.save", path=str(path), overwrite=False)
        tree = read_project(path)
        immutable[path] = sha256(read_bytes(path))
        row = dict(file=path.name, sha256=immutable[path], typedSHA256=digest(tree))
        report["saves"].append(row)
        if expected is not None:
            row["comparison"] = differences(expected, tree)
            require(row["comparison"]["equal"], name + ": complete saved tree differs")
        return path, tree

    loader = FixtureLoader(client, call, report["fixtureLoads"])
    try:
        loader.load(source)
        _, baseline = save("baseline", original)
        events = [{k: value for k, value in item.items() if k in
                   ("channel", "position", "note", "instrument", "velocity", "effect", "parameter")}
                  for item in read("pattern.notes.get", pattern=0)["events"]]
        require(events[0]["velocity"] == 98, "F04 first precise-note coordinate changed")
        events[0]["velocity"] = 97
        pcm = read("sample.pcm.get", sample=5)
        # F04's volume envelope is linked to bank n40. Use its independent pan
        # envelope here; linked-master editing has its own bank acceptance gate.
        envelope = read("instrument.envelope.get", instrument="n20", envelope="pan")
        require(envelope["points"] == [[0, 16], [8, 48], [24, 32]], "F04 pan envelope changed")
        plugin = original["plugins"][0]["instanceID"]
        definition = next(g for g in read("graph.get")["library"] if g["id"] == "n34")
        edge = next(e for e in definition["audio"] if e["source"] == "n37" and e["target"] == "n36")
        edge["gain"] = .75
        sends = next(b for b in read("mixer.get")["buses"] if b["id"] == "n2")["sends"]
        require(len(sends) == 1 and sends[0]["target"] == "n33" and sends[0]["gainDB"] == -12,
                "F04 Pulse send changed")
        sends[0]["gainDB"] = -15
        steps = [
            ("note", "pattern.notes.set", dict(pattern=0, events=events)),
            ("loop", "sample.loops.set", dict(sample=5, normal=dict(start=2500, end=19200, enabled=True))),
            ("envelope", "instrument.envelope.transform",
             dict(instrument="n20", envelope="pan", operation="scale", options=dict(amount=.5))),
            ("plugin", "plugin.parameters.set", dict(plugin=plugin, values=[dict(id=1, value=-6)])),
            ("graph", "graph.update", dict(definition=definition)),
            ("send", "mixer.sends.set", dict(bus="n2", sends=sends)),
        ]
        trees = [baseline]
        first_revision = call("document.get")["revision"]
        for name, method, params in steps:
            before = call("document.get")
            preview = write(method, **params, dryRun=True)
            require(not preview["changed"] and not preview["playbackStopped"] and call("document.get") == before,
                    name + ": dry run changed document/history")
            reply = write(method, **params)
            require(reply["changed"] and call("document.get")["revision"] != before["revision"],
                    name + ": intended edit did not commit")
            _, tree = save("edited-" + name)
            expected = editable_copy(trees[-1])
            if name == "note":
                pattern_id = original["native"]["patterns"][0][1]["id"]
                notes = expected["native"]["preciseNotes"]
                replaced = [n for n in notes if n["pattern"] == pattern_id]
                note = next(n for n in replaced if n["track"] == "n2" and n["position"] == 0)
                note["velocity"] = 97
                # The shared precise-note replacement appends the changed
                # pattern's ordered events; other patterns retain their order.
                # Assert that exact wire result, never sort compared output.
                expected["native"]["preciseNotes"] = [n for n in notes if n["pattern"] != pattern_id] + replaced
            elif name in ("loop", "envelope"):
                expected["module"] = tree["module"]
                require(tree["module"] != trees[-1]["module"], name + ": embedded edit did not persist")
                require(read("sample.pcm.get", sample=5) == pcm, name + ": loop/envelope edit changed sample PCM")
                if name == "loop":
                    sample = read("sample.get", sample=5)
                    require(sample["loopStart"] == 2500 and sample["loopEnd"] == 19200 and sample["loop"],
                            "Sample loop did not apply")
                else:
                    require(read("instrument.envelope.get", instrument="n20", envelope="pan")["points"]
                            == [[0, 8], [8, 24], [24, 16]], "Envelope scaling did not apply")
            elif name == "plugin":
                expected["plugins"][0]["state"] = tree["plugins"][0]["state"]
                require(tree["plugins"][0]["state"] != trees[-1]["plugins"][0]["state"],
                        "Manual plugin parameter did not persist")
            elif name == "graph":
                graph = next(g for g in expected["native"]["signalGraph"]["library"] if g["id"] == "n34")
                next(e for e in graph["audio"] if e["source"] == "n37" and e["target"] == "n36")["gain"] = .75
            elif name == "send":
                next(b for b in expected["native"]["mixer"]["buses"] if b["id"] == "n2")["sends"][0]["gainDB"] = -15.0
            comparison = differences(expected, tree)
            report["saves"][-1]["intendedEditComparison"] = comparison
            require(comparison["equal"], name + ": edit changed unrelated project fields")
            trees.append(tree)
        # A stale full replacement must not redirect the captured first edit.
        before = call("document.get")
        try:
            call("pattern.notes.set", dict(expectedRevision=first_revision, pattern=0, events=events))
        except Exception as error:
            require(getattr(error, "code", None) == -32001, "Stale edit did not reject explicitly")
            require((getattr(error, "data", None) or {}).get("writeOutcome") not in ("unknown", "committed"),
                    "Stale rejection has an uncertain/committed outcome")
        else:
            raise EvidenceError("Stale edit was accepted")
        require(call("document.get") == before, "Stale edit changed document/history")
        for index in range(len(steps) - 1, -1, -1):
            write("history.undo")
            save("undo-" + steps[index][0], trees[index])
            before = call("document.get")
            current_sends = next(b for b in read("mixer.get")["buses"] if b["id"] == "n2")["sends"]
            reply = write("mixer.sends.set", bus="n2", sends=current_sends)
            require(not reply["changed"] and call("document.get") == before, "No-op consumed history/Redo")
        for index, (name, _, _) in enumerate(steps):
            write("history.redo")
            path, _ = save("redo-" + name, trees[index + 1])
        write("history.undo")
        write(steps[-1][1], **steps[-1][2])
        require(not read("document.get")["canRedo"], "Real edit did not retire the old Redo branch")
        path, _ = save("forked-final", trees[-1])
        loader.load(path)
        path, _ = save("reopened", trees[-1])
        report["exchangeInput"] = path.name
        report["passed"] = True
        print("COMPOSING HISTORY: six domains, dry runs, stale rejection, chronological Undo/Redo and reopen exact", flush=True)
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report["inputsAndOutputsUnchanged"] = all(sha256(read_bytes(p)) == value for p, value in immutable.items())
        if not report["inputsAndOutputsUnchanged"]:
            report["passed"] = False
        write_json(output / "report.json", report)
