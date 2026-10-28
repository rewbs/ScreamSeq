#!/usr/bin/env python3
"""Check new Mac parity adapters against an explicitly selected disposable app."""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools"))
from resonance_api import APIError, Client


def check(client):
    before = client.call("document.get")
    advertised = client.call("api.describe")["data"]
    assert {"midi.devices.get", "midi.settings.get", "midi.settings.set"} <= set(advertised["reads"] + advertised["writes"])
    assert all(device["id"].startswith("coremidi:") for device in client.call("midi.devices.get")["data"]["devices"])
    assert {"audio.devices.get", "audio.settings.get", "audio.settings.set"} <= set(advertised["reads"] + advertised["writes"])
    outputs = client.call("audio.devices.get")["data"]["devices"]
    assert all(isinstance(device["id"], str) and isinstance(device["default"], bool) for device in outputs)
    audio = client.call("audio.settings.get")["data"]
    selection = dict(endpoint=audio["endpoint"], periodFrames=audio["periodFrames"], expectedAudioRevision=audio["audioRevision"])
    client.call("audio.settings.set", selection)
    assert client.call("audio.settings.get")["data"] == audio
    preview = client.call("audio.settings.set", {**selection, "periodFrames": 512, "dryRun": True})["data"]
    assert preview["audioRevision"] == audio["audioRevision"] and preview["proposed"]["periodFrames"] == 512
    for patch, code in [({"expectedAudioRevision": "stale"}, -32001), ({"periodFrames": True}, -32602),
                        ({"periodFrames": 127}, -32602), ({"endpoint": "missing-output"}, -32602),
                        ({"dryRun": 1}, -32602), ({"extra": 0}, -32602)]:
        try:
            client.call("audio.settings.set", {**selection, **patch})
        except APIError as error:
            assert error.code == code, error
        else:
            raise AssertionError("Invalid audio settings accepted")
    assert client.call("audio.settings.get")["data"] == audio
    assert client.call("document.get") == before, "Output settings must not alter musical history"
    original = client.call("midi.settings.get")["data"]
    fields = ("source", "armed", "channelsCount", "quantization", "latencyMS")

    def rejected(code, params):
        try:
            client.call("midi.settings.set", params)
        except APIError as error:
            assert error.code == code, error
        else:
            raise AssertionError("Invalid/stale MIDI settings accepted")

    proposed = dict(expectedMidiRevision=original["revision"], source="", armed=True,
                    channelsCount=3, quantization=8192, latencyMS=1.5)
    try:
        preview = client.call("midi.settings.set", {**proposed, "dryRun": True})["data"]
        assert preview["revision"] == original["revision"] and preview["proposed"]["channelsCount"] == 3
        assert client.call("midi.settings.get")["data"] == original
        for key, value in [("armed", 1), ("channelsCount", True), ("channelsCount", 0),
                           ("quantization", 65537), ("latencyMS", True), ("latencyMS", 501),
                           ("source", "unknown"), ("dryRun", 1), ("extra", 0)]:
            rejected(-32602, {**proposed, key: value})
        applied = client.call("midi.settings.set", proposed)["data"]
        assert all(applied[field] == proposed[field] for field in fields)
        assert applied["revision"] != original["revision"]
        rejected(-32001, proposed)
        no_op = client.call("midi.settings.set", {**proposed, "expectedMidiRevision": applied["revision"]})["data"]
        assert no_op == applied
        assert client.call("document.get") == before, "MIDI preferences must not alter musical history"
    finally:
        current = client.call("midi.settings.get")["data"]
        client.call("midi.settings.set", {**{field: original[field] for field in fields}, "expectedMidiRevision": current["revision"]})

    client.call("workspace.layout", {"name": "Graph editing"})
    state = client.call("workspace.get")["data"]
    assert {"graph", "graphCurve", "automation"} <= set(state["visible"])
    client.call("workspace.panel", {"panel": "graphCurve", "pinned": True})
    client.call("workspace.panel", {"panel": "graphCurve", "placement": "float"})
    assert client.call("workspace.get")["data"]["pins"]["graphCurve"]
    client.call("workspace.panel", {"panel": "graphCurve", "placement": "right"})
    client.call("workspace.layout", {"name": "Save custom", "savedName": "Parity graph"})
    client.call("workspace.panel", {"panel": "graphCurve", "pinned": False, "placement": "bottom"})
    client.call("workspace.layout", {"name": "Restore custom", "savedName": "Parity graph"})
    restored = client.call("workspace.get")["data"]
    assert restored["locations"]["graphCurve"] == "right" and not restored["pins"]["graphCurve"]
    assert "Parity graph" in restored["savedLayouts"]
    client.call("workspace.layout", {"name": "Delete custom", "savedName": "Parity graph"})
    assert "Parity graph" not in client.call("workspace.get")["data"]["savedLayouts"]
    assert client.call("document.get") == before

    def write(method, **params):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **params})

    assert not client.call("recording.get")["data"]["take"], "Use a disposable app without an existing take"
    take = write("recording.start", channels=[0], instrument=1)["data"]["take"]
    try:
        write("recording.capture", take=take, events=[dict(timestamp="0", status=144, note=60, velocity=100)])
        write("recording.stop", take=take)
        time.sleep(1.5)  # Let the app's automatic-finish timer run.
        retained = client.call("recording.get")["data"]
        assert retained["take"] == take and not retained["capturing"] and retained["missingTime"] > 0
        assert client.call("document.get") == before, "API Stop must retain the take without committing music"
    finally:
        write("recording.discard", take=take)
    print("PASS Mac parity socket: audio/MIDI revision/dry-run/no-op/type guards, unchanged song, independent curve docks and retained stopped take")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True, help="Explicit socket of a disposable inspection app")
    check(Client(parser.parse_args().socket))
