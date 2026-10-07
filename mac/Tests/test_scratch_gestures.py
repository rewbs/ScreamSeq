"""Scratch phrase editing through the real, private application socket."""
from resonance_api import APIError
import plistlib


def scratch_gestures(client, directory):
    def write(method, **params):
        revision = client.call("document.get")["revision"]
        return client.call(method, {"expectedRevision": revision, **params})

    def bank():
        return client.call("scratch.gestures.get")["data"]

    before = bank()
    first = write("scratch.gestures.set", preset="baby", name="Socket baby")["data"]["id"]
    second = write("scratch.gestures.set", preset="two-click-flare")["data"]["id"]
    assert first != second
    phrase = next(p for p in bank()["gestures"] if p["id"] == first)
    revision = client.call("document.get")["revision"]
    assert not write("scratch.gestures.set", id=first, name=phrase["name"],
                     motion=phrase["motion"], fader=phrase["fader"])["changed"]
    assert revision == client.call("document.get")["revision"]
    for changes in ({"motion": []}, {"name": ""}, {"unexpected": 1}, {"id": True}):
        try:
            write("scratch.gestures.set", **{"id": first, **changes})
        except APIError as error:
            assert error.code == -32602
        else:
            raise AssertionError("Invalid phrase accepted")
        assert revision == client.call("document.get")["revision"]

    old_effects = client.call("pattern.effects.get", {"pattern": 0})["data"]
    commands = [c for c in old_effects["commands"] if not (
        c["channel"] == 0 and c["position"] // 65536 == 0 and c["column"] == 0)]
    commands.append({"channel": 0, "column": 0, "position": 1234,
                     "kind": "native", "native": "scratch",
                     "parameters": {"gesture": first, "beats": 0.875,
                                    "travelMs": 123.456, "repeats": 4, "reverse": True}})
    write("pattern.effects.set", pattern=0, commands=commands)
    assert next(p for p in bank()["gestures"] if p["id"] == first)["uses"] == 1
    try:
        write("scratch.gestures.remove", id=first)
    except APIError as error:
        assert error.code == -32602
    else:
        raise AssertionError("Removed a referenced phrase")
    linked_bank = bank()
    linked_effects = client.call("pattern.effects.get", {"pattern": 0})["data"]
    target = {"pattern": 0, "row": 0, "channel": 0, "column": 0}
    revision = client.call("document.get")["revision"]
    write("scratch.gestures.clone", id=first, target=target, dryRun=True)
    assert bank() == linked_bank and client.call("document.get")["revision"] == revision
    copy_id = write("scratch.gestures.clone", id=first, target=target, name="Independent variation")["data"]["id"]
    copied = client.call("pattern.effects.get", {"pattern": 0})["data"]["commands"]
    original = next(c for c in linked_effects["commands"] if c["channel"] == 0 and c["position"] // 65536 == 0 and c["column"] == 0)
    expected = {**original, "parameters": {**original["parameters"], "gesture": copy_id}}
    assert expected in copied
    write("history.undo")
    assert bank() == linked_bank and client.call("pattern.effects.get", {"pattern": 0})["data"] == linked_effects
    write("history.redo")
    assert expected in client.call("pattern.effects.get", {"pattern": 0})["data"]["commands"]
    write("history.undo")
    before_edit = bank()
    write("scratch.gestures.set", id=first, fader=[
        {"position": 0, "value": 1, "curve": "scripted", "formula": "0.5+0.5*sin(t*pi*6)"},
        {"position": 65536, "value": 1}])
    after_edit = bank()
    write("history.undo")
    assert bank() == before_edit
    write("history.redo")
    assert bank() == after_edit
    path = directory / "scratch-phrases.screamseq"
    write("document.save", path=str(path))
    with path.open("rb") as stream:
        saved = plistlib.load(stream)
    metadata = next(value for value in saved.values()
                    if isinstance(value, dict) and "scratchGestures" in value)
    assert any(p["id"] == first and p["fader"][0]["formula"] == "0.5+0.5*sin(t*pi*6)"
               for p in metadata["scratchGestures"])
    # Put the private fixture back so the suite does not accumulate phrase refs.
    write("pattern.effects.set", pattern=0, commands=old_effects["commands"])
    write("scratch.gestures.remove", id=first)
    write("scratch.gestures.remove", id=second)
    assert bank() == before
    print("PASS scratch socket: presets, inline command contract, scripts, shared uses, strict validation, Undo/Redo and native metadata")
