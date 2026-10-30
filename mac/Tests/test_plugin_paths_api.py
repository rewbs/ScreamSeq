"""Explicit VST3 path repair through the real private application socket."""
import copy
import json
import plistlib
import shutil
import subprocess

from resonance_api import APIError


def plugin_paths_api(client, directory, build):
    fixture = build / "test-plugins/ResonanceFixture.vst3"
    moved = directory / "Moved fixture 音.vst3"
    shutil.copytree(fixture, moved)
    descriptors = json.loads(subprocess.check_output([str(build / "plugin-scanner"), "--list-vst3", str(fixture)]))
    descriptor = next(item for item in descriptors if not item["isInstrument"])

    def write(method, **fields):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **fields})

    def tree():
        path = directory / "path-repair.screamseq"
        write("document.save", path=str(path), overwrite=True)
        return plistlib.loads(path.read_bytes())

    added = write("plugin.add", descriptor=descriptor)
    plugin = client.call("document.get")["data"]["nativePlugins"][added["data"]["slot"]]["instanceID"]
    graph = write("graph.create", name="Path repair socket")["data"]["graph"]
    write("graph.node.add", graph=graph, kind="plugin", slot=added["data"]["slot"])
    definition = next(item for item in client.call("graph.get")["data"]["library"] if item["id"] == graph)
    node = next(item["id"] for item in definition["nodes"] if item["kind"] == "plugin")
    scanned = write("plugin.path.scan", plugin=plugin, path=str(moved))
    assert not scanned["changed"]
    candidate = next(item for item in scanned["data"]["candidates"] if item["descriptor"]["path"] == str(moved.resolve()))
    fields = dict(plugin=plugin, path=str(moved.resolve()), expectedModuleSHA256=candidate["moduleSHA256"])
    before = tree()
    try:
        write("plugin.path.set", **{**fields, "expectedModuleSHA256": "0" * 64})
    except APIError as error:
        assert error.code == -32602 and error.data["writeOutcome"] == "notCommitted", error
    else:
        raise AssertionError("Changed module hash accepted")
    preview = dict(plugin=plugin, parameter=7, gesture="path-repair-preview")
    write("plugin.parameters.preview.set", **preview, value=.23)
    owner = client.call("plugin.parameters.preview.get")
    for method, values in [("plugin.path.scan", dict(plugin=plugin, path=str(moved))), ("plugin.path.set", fields)]:
        try:
            write(method, **values)
        except APIError as error:
            assert error.code == -32602, error
        else:
            raise AssertionError("Location mutation discarded retained preview")
        assert client.call("plugin.parameters.preview.get") == owner
    write("plugin.parameters.preview.cancel", **preview)
    dry = write("plugin.path.set", **fields, dryRun=True)
    assert not dry["changed"] and dry["data"]["wouldChange"] and tree() == before
    changed = write("plugin.path.set", **fields)
    assert changed["changed"]
    after = tree()
    expected = copy.deepcopy(before)
    next(item for item in expected["plugins"] if item["instanceID"] == plugin)["path"] = str(moved.resolve())
    assert after == expected, "Rack repair changed more than the saved path"
    assert not write("plugin.path.set", **fields)["changed"]
    write("history.undo")
    assert tree() == before
    write("history.redo")
    assert tree() == after
    graph_fields = dict(graph=graph, node=node, path=fields["path"], expectedModuleSHA256=fields["expectedModuleSHA256"])
    assert write("graph.plugin.path.set", **graph_fields)["changed"]
    expected = copy.deepcopy(after)
    definition = next(item for item in expected["native"]["signalGraph"]["library"] if item["id"] == graph)
    next(item for item in definition["nodes"] if item["id"] == node)["plugin"]["path"] = fields["path"]
    assert tree() == expected, "Graph repair changed more than the saved path"
    write("history.undo")
    assert tree() == after
    write("history.redo")
    assert tree() == expected
    write("graph.remove", graph=graph)
    slot = next(index for index, item in enumerate(client.call("document.get")["data"]["nativePlugins"]) if item["instanceID"] == plugin)
    write("plugin.remove", slot=slot)
    print("PASS plugin path socket: real scan, hash/refusal receipts, retained preview, dry run, exact rack/graph preservation, no-op and chronological Undo/Redo")
