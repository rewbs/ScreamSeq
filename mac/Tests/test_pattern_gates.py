"""Persistent gate authoring through a private application socket."""
from resonance_api import APIError
import plistlib


def pattern_gates(client, directory):
    def write(method, **params):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **params})

    before = client.call("pattern.effects.get", {"pattern": 0})["data"]
    commands = [{k: v for k, v in c.items() if k != "track"} for c in before["commands"] if not (c["channel"] == 0 and c["position"] // 65536 in (0, 1) and c["column"] < 2)]
    fields = {"target": "volume", "floor": 0.125, "ceiling": 1.25, "period": 0.375,
              "phase": -0.0625, "width": 0.3, "rise": 1.75, "fall": 0.0625,
              "riseShape": "ease-in", "fallShape": "ease-out", "linked": False}
    commands.extend([
        {"channel": 0, "position": 1234, "column": 0, "kind": "native", "native": "gate", "parameters": fields},
        {"channel": 0, "position": 0, "column": 1, "kind": "native", "native": "gate", "parameters": {"target": "pan", "floor": -1, "ceiling": 1}},
        {"channel": 0, "position": 65536 + 9876, "column": 0, "kind": "native", "native": "gate-stop", "parameters": {"target": "all"}},
    ])
    count = max(2, next((c["count"] for c in before["columns"] if c["channel"] == 0), 1))
    request = {"pattern": 0, "columns": [{"channel": 0, "count": count}], "commands": commands}
    revision = client.call("document.get")["revision"]
    write("pattern.effects.set", **request, dryRun=True)
    assert client.call("document.get")["revision"] == revision
    write("pattern.effects.set", **request)
    saved = client.call("pattern.effects.get", {"pattern": 0})["data"]
    assert not write("pattern.effects.set", **request)["changed"]
    write("history.undo")
    assert client.call("pattern.effects.get", {"pattern": 0})["data"] == before
    write("history.redo")
    assert client.call("pattern.effects.get", {"pattern": 0})["data"] == saved
    for change in ({"period": 0}, {"rise": True}, {"width": 1.1}, {"floor": 2}, {"linked": True}):
        bad = {**commands[-3], "parameters": {**fields, **change}}
        try:
            write("pattern.effects.set", pattern=0, commands=[bad])
        except APIError as error:
            assert error.code == -32602
        else:
            raise AssertionError("Invalid gate accepted")
        assert client.call("pattern.effects.get", {"pattern": 0})["data"] == saved
    path = directory / "pattern-gates.screamseq"
    write("document.save", path=str(path))
    with path.open("rb") as stream:
        data = plistlib.load(stream)
    metadata = next(v for v in data.values() if isinstance(v, dict) and "performance" in v)
    native = next(c for c in metadata["performance"]["commands"] if c.get("native") == "gate" and c["parameters"]["target"] == "volume")
    assert all(native["parameters"][key] == value for key, value in fields.items())
    write("pattern.effects.set", pattern=0, columns=[{"channel": c["channel"], "count": c["count"]} for c in before["columns"]], commands=[{k: v for k, v in c.items() if k != "track"} for c in before["commands"]])
    print("PASS gate socket: simultaneous targets, fractional placement, transition clipping inputs, stop, dry-run/no-op, atomic rejection, Undo/Redo and saved metadata")
