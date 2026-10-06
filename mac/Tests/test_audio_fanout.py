"""Many-to-many cable edits through the real local API socket.

Imported by test_automation.py; runs only against that suite's private document.
Rendered summation and processor-call counts belong to MixerPluginRoutingTests.
"""
from resonance_api import APIError
import plistlib


def audio_fanout(client, directory):
    def write(method, **params):
        revision = client.call("document.get")["revision"]
        return client.call(method, {"expectedRevision": revision, **params})

    def mixer():
        data = client.call("mixer.get")["data"]
        return {key: data[key] for key in (
            "active", "buses", "detached", "detachedChains",
            "disconnectedMainInputs", "instruments", "pluginConnections", "sidechains")}

    def reject(code, operation):
        before = mixer()
        revision = client.call("document.get")["revision"]
        try:
            operation()
        except APIError as error:
            assert error.code == code, (error.code, str(error))
        else:
            raise AssertionError("Expected atomic rejection")
        assert mixer() == before
        assert client.call("document.get")["revision"] == revision

    write("mixer.enable", enabled=True)
    tracks = [bus["id"] for bus in mixer()["buses"] if bus["kind"] == "track"]
    assert len(tracks) >= 3
    returns = [write("mixer.bus.add", kind="return", name=name)["data"]["bus"]
               for name in ("Parallel A", "Parallel B")]
    initial_outputs = {bus["id"]: bus["output"] for bus in mixer()["buses"]}

    # Two channel outputs each feed two destinations; original dry paths stay.
    for track in tracks[:2]:
        sends = []
        for target in returns:
            sends.append({"target": target, "gainDB": 0})
            write("mixer.sends.set", bus=track, sends=sends)
    routed = mixer()
    assert all(bus["output"] == initial_outputs[bus["id"]] for bus in routed["buses"])
    assert all(len(next(bus for bus in routed["buses"] if bus["id"] == track)["sends"]) == 2
               for track in tracks[:2])

    # A newly added, unconnected processor must accept cables without moving it
    # onto a channel or replacing any of the existing paths.
    plugins = []
    for _ in range(3):
        reply = write("plugin.add", descriptor={"format": "Built-in",
                      "classID": "resonance.gainer.v1", "name": "Gainer",
                      "type": 0, "subtype": 0, "manufacturer": 0}, detached=True)
        plugins.append(next(plugin["id"] for plugin in client.call("graph.get")["data"]["plugins"]
                            if plugin["slot"] == reply["data"]["slot"]))
    source, first, second = plugins
    write("mixer.sidechains.set", plugin=source, input=0,
          sources=[{"source": tracks[0]}, {"source": tracks[1]}])
    assert {route["source"] for route in mixer()["sidechains"]
            if route["plugin"] == source} == set(tracks[:2])
    for target in (first, second):
        write("mixer.plugin.connection.set", source=source, target=target, output=0, input=0)
    write("mixer.sidechains.set", plugin=first, input=0, sources=[{"source": tracks[2]}])
    write("mixer.plugin.route", plugin=first, output=0, targets=returns)
    write("mixer.plugin.route", plugin=second, output=0, targets=returns)
    full = mixer()
    assert len([route for route in full["pluginConnections"] if route["source"] == source]) == 2
    assert all({route["target"] for route in full["instruments"]
                if route["plugin"] == plugin and route["output"] == 0} == set(returns)
               for plugin in (first, second))

    # A repeated exact cable is a no-op, while stale read/merge/write cannot
    # erase a destination added by another editor.
    revision = client.call("document.get")["revision"]
    assert not write("mixer.plugin.connection.set", source=source, target=first,
                     output=0, input=0)["changed"]
    assert client.call("document.get")["revision"] == revision
    assert not write("mixer.plugin.route", plugin=first, output=0, targets=returns[::-1])["changed"]
    assert mixer() == full
    assert client.call("document.get")["revision"] == revision
    write("mixer.plugin.connection.set", source=source, target=first,
          output=0, input=0, gainDB=-6)
    reject(-32001, lambda: client.call("mixer.plugin.route", {
        "expectedRevision": revision, "plugin": first, "output": 0, "targets": returns[:1]}))
    reject(-32602, lambda: write("mixer.plugin.route", plugin=first, output=0,
                                targets=[returns[0], returns[0]]))
    reject(-32602, lambda: write("mixer.plugin.connection.set", source=first,
                                target=source, output=0, input=0))

    # Cut exactly one branch, then restore it with a single unified Undo.
    before_cut = mixer()
    cut = {"kind": "plugin-connection", "source": source, "target": second,
           "output": 0, "input": 0}
    assert not write("graph.connections.remove", connections=[cut], dryRun=True)["changed"]
    assert mixer() == before_cut
    write("graph.connections.remove", connections=[cut])
    cut_state = mixer()
    assert len([route for route in cut_state["pluginConnections"] if route["source"] == source]) == 1
    assert cut_state["instruments"] == before_cut["instruments"]
    assert cut_state["sidechains"] == before_cut["sidechains"]
    write("history.undo")
    assert mixer() == before_cut
    write("history.redo")
    assert mixer() == cut_state
    write("history.undo")

    path = str(directory / "many-to-many.screamseq")
    write("document.save", path=path)
    saved = mixer()
    with open(path, "rb") as stream:
        stored = plistlib.load(stream)["native"]["mixer"]
    for key in ("instruments", "sidechains", "pluginConnections", "detachedChains"):
        assert stored[key] == saved[key], key
    print("PASS audio fan-out socket: two-by-two bus sends, detached processor fan-in/out, "
          "preserved dry paths, exact cut, no-op/dry-run/stale/cycle rejection, Undo/Redo and native save")
