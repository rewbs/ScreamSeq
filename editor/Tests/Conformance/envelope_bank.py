"""F04 linked-master, independent-copy and unlink journey on a native endpoint.

This checks saved musical state/history. It does not qualify bank controls,
catalogue publication, family import, foreground input or live audio.
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
    report = dict(format="screamseq-envelope-bank-journey-v1", passed=False, parityComplete=False,
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
        slots = [slot for slot, _ in original["native"]["instruments"]]
        require(dict(original["native"]["instruments"])[3]["id"] == "n22",
                "F04 secondary instrument identity changed")
        instruments = {slot: read("instrument.get", instrument=slot) for slot in slots}
        expected_instruments = editable_copy(instruments)
        samples = {slot: read("sample.pcm.get", sample=slot) for slot, _ in original["native"]["samples"]}
        original_volume = editable_copy(instruments[1]["envelopes"][0])
        require(original_volume["points"] == [[0, 0], [4, 64], [16, 40], [40, 0]]
                and original_volume["sustainPoint"] == 2, "F04 volume envelope changed")
        bank = editable_copy(original["native"]["envelopeBank"])
        require(bank["entries"][0]["id"] == "n40" and bank["links"] == [
            dict(target=dict(kind="volume", owner="n20", pattern=""), template="n40", span=41)],
            "F04 linked master changed")
        master = editable_copy(bank["entries"][0])
        shape = editable_copy(master["shape"])
        shape["points"][1]["value"] = .75
        later_shape = editable_copy(shape)
        later_shape["points"][1]["value"] = .5
        independent = dict(kind="volume", instrument="n21")
        linked = dict(kind="volume", instrument="n22")
        steps = [
            ("independent", "envelope.bank.apply", dict(template="n40", target=independent, linked=False)),
            ("second-linked", "envelope.bank.apply", dict(template="n40", target=linked, linked=True)),
            ("master", "envelope.bank.save", dict(id="n40", name=master["name"], shape=shape)),
            ("unlink", "envelope.bank.unlink", dict(target=linked)),
            ("master-after-unlink", "envelope.bank.save", dict(id="n40", name=master["name"], shape=later_shape)),
        ]
        trees = [baseline]

        def materialize(slot, peak):
            envelope = editable_copy(original_volume)
            envelope["points"][1][1] = peak
            expected_instruments[slot]["envelopes"][0] = envelope
            for field in ("enabled", "sustain", "sustainPoint"):
                expected_instruments[slot][field] = envelope[field]

        def inspect_unchanged_assets():
            require({slot: read("instrument.get", instrument=slot) for slot in slots} == expected_instruments,
                    "Bank edit changed unrelated instrument settings or an independent envelope")
            require({slot: read("sample.pcm.get", sample=slot) for slot in samples} == samples,
                    "Bank edit changed sample PCM")

        for name, method, params in steps:
            before = call("document.get")
            preview = write(method, **params, dryRun=True)
            require(not preview["changed"] and preview["data"]["wouldChange"]
                    and call("document.get") == before, name + ": dry run changed state or missed the edit")
            save("dry-" + name, trees[-1])
            reply = write(method, **params)
            require(reply["changed"], name + ": intended change was not committed")
            expected = editable_copy(trees[-1])
            expected_bank = expected["native"]["envelopeBank"]
            if name == "independent":
                materialize(2, 64)
            elif name == "second-linked":
                materialize(3, 64)
                expected_bank["links"].append(
                    dict(target=dict(kind="volume", owner="n22", pattern=""), template="n40", span=41))
            elif name == "master":
                expected_bank["entries"][0]["shape"] = editable_copy(shape)
                materialize(1, 48)
                materialize(3, 48)
            elif name == "unlink":
                expected_bank["links"] = [entry for entry in expected_bank["links"]
                                          if entry["target"]["owner"] != "n22"]
            else:
                expected_bank["entries"][0]["shape"] = editable_copy(later_shape)
                materialize(1, 32)
            inspect_unchanged_assets()
            _, tree = save(name)
            if name != "unlink":
                # Envelope materialization changes the embedded module. Assert
                # all instrument fields and sample PCM above; every outer/native
                # value must still equal this independently described edit.
                expected["module"] = tree["module"]
                require(tree["module"] != trees[-1]["module"], name + ": points did not persist")
            comparison = differences(expected, tree)
            report["saves"][-1]["intendedEditComparison"] = comparison
            require(comparison["equal"], name + ": bank edit changed unrelated project data")
            trees.append(tree)

        # Leave a real Redo entry before testing no-op and rejected bank edits.
        write("history.undo")
        save("before-rejections", trees[-2])
        before = call("document.get")
        require(before["data"]["canRedo"], "Bank fixture lacks Redo")
        noop = write("envelope.bank.unlink", target=linked)
        require(not noop["changed"] and call("document.get") == before, "No-op unlink consumed Redo")
        invalid = editable_copy(shape)
        invalid["points"][1]["position"] = 0
        for method, params in [
            ("envelope.bank.remove", dict(id="n40")),
            ("envelope.bank.save", dict(id="n40", name=master["name"], shape=invalid)),
        ]:
            try:
                write(method, **params)
            except Exception as error:
                require(getattr(error, "code", None) == -32602, "Bank rejection had an unexpected outcome")
            else:
                raise EvidenceError("Invalid/used master edit was accepted")
            require(call("document.get") == before, "Rejected bank edit changed document/history")
        save("after-rejections", trees[-2])
        write("history.redo")
        save("after-rejected-redo", trees[-1])

        for index in range(len(steps) - 1, -1, -1):
            write("history.undo")
            save("undo-" + steps[index][0], trees[index])
        for index, (name, _, _) in enumerate(steps):
            write("history.redo")
            path, _ = save("redo-" + name, trees[index + 1])
        loader.load(path)
        path, _ = save("reopened", trees[-1])
        inspect_unchanged_assets()
        require(read("envelope.bank.list")["links"] == trees[-1]["native"]["envelopeBank"]["links"],
                "Reopen changed linked target identities")
        report["exchangeInput"] = path.name
        report["passed"] = True
        print("ENVELOPE BANK: linked uses, independent copy, unlink, rejection, Undo/Redo and reopen exact", flush=True)
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report["inputsAndOutputsUnchanged"] = all(sha256(read_bytes(path)) == value for path, value in immutable.items())
        if not report["inputsAndOutputsUnchanged"]:
            report["passed"] = False
        write_json(output / "report.json", report)
