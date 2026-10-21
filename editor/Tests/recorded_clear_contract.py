"""Clear recorded music atomically without capturing effective plugin values."""
from copy import deepcopy
from pathlib import Path
import plistlib
import sys

from Conformance.project_tree import CORPUS, typed


def recorded_clear_contract(client, directory, expect_error):
    # The same owned fixture loader supports Windows Open and Mac's private
    # recovery setup; neither discovers or writes a musician's recovery folder.
    sys.path.insert(0, str(Path(__file__).parent / "Conformance"))
    from fixture_loader import FixtureLoader
    FixtureLoader(client, client.call, []).load(CORPUS / "fixtures/04-complete-reference.screamseq")
    def doc():
        return client.call("document.get")

    def read(method, **params):
        return client.call(method, params)["data"]

    def write(method, **params):
        return client.call(method, dict(expectedRevision=doc()["revision"], **params))

    def save(name):
        path = Path(directory) / (name + ".screamseq")
        write("document.save", path=str(path))
        return plistlib.loads(path.read_bytes())

    assert read("automation.get")["total"] == 0, "F04 recorded-automation fixture changed"
    gain = next(p for p in read("plugin.discover", format="Built-in")
                if p["classID"] == "resonance.gainer.v1")
    write("plugin.add", descriptor=gain)
    write("plugin.add", descriptor=gain)
    plugins = doc()["data"]["nativePlugins"][-2:]
    first, second = [p["instanceID"] for p in plugins]
    write("plugin.parameters.set", plugin=first, values=[dict(id=1, value=-3)])
    write("history.undo")
    before = doc()
    reply = write("automation.recorded.clear")
    assert reply["data"] == dict(removed=0, wouldChange=False, dryRun=False)
    assert doc() == before, "Empty clear changed revision, history or document"
    write("history.redo")  # The no-op must preserve the preceding manual Redo.
    write("plugin.parameters.set", plugin=second, values=[dict(id=1, value=-9)])
    for plugin, frame, value in ((first, 0, -18), (first, 24000, -12),
                                  (second, 0, -24), (second, 48000, -6)):
        write("automation.recorded.edit", plugin=plugin, parameter=1, frame=frame, value=value)
    original = save("recorded-clear-before")
    states = [read("plugin.state.get", plugin=p) for p in (first, second)]
    points = read("automation.get")
    before = doc()
    preview = dict(plugin=first, parameter=1, gesture="clear-dry-run")
    write("plugin.parameters.preview.set", **preview, value=-30)
    owner = read("plugin.parameters.preview.get")
    dry = write("automation.recorded.clear", dryRun=True)
    assert dry["data"] == dict(removed=4, wouldChange=True, dryRun=True)
    assert not dry["changed"] and not dry["playbackStopped"]
    assert doc() == before and read("automation.get") == points
    assert read("plugin.parameters.preview.get") == owner, "Dry clear cancelled the active preview"
    for invalid in (dict(dryRun=1), dict(unexpected=True), dict(plugin=first)):
        expect_error(-32602, lambda invalid=invalid: write("automation.recorded.clear", **invalid))
    expect_error(-32001, lambda: client.call("automation.recorded.clear", dict(expectedRevision="stale")))
    assert doc() == before and read("automation.get") == points
    assert [read("plugin.state.get", plugin=p) for p in (first, second)] == states
    assert read("plugin.parameters.preview.get") == owner, "Invalid clear cancelled the retained preview"
    write("plugin.parameters.preview.cancel", **preview)
    params = dict(expectedRevision=doc()["revision"])
    cleared = client.call("automation.recorded.clear", params, request_id="recorded-clear-once")
    assert cleared["data"] == dict(removed=4, wouldChange=True, dryRun=False)
    assert read("automation.get")["total"] == 0
    assert client.call("automation.recorded.clear", params, request_id="recorded-clear-once") == cleared
    expected = deepcopy(original)
    expected["automation"] = []
    assert typed(save("recorded-clear-empty")) == typed(expected), "Clear changed unrelated saved data"
    assert [read("plugin.state.get", plugin=p) for p in (first, second)] == states
    before = doc()
    write("plugin.parameters.preview.set", **preview, value=-30)
    owner = read("plugin.parameters.preview.get")
    assert not write("automation.recorded.clear")["changed"] and doc() == before
    assert read("plugin.parameters.preview.get") == owner, "Empty clear cancelled a retained preview"
    write("plugin.parameters.preview.cancel", **preview)
    write("history.undo")
    assert read("automation.get") == points
    assert typed(save("recorded-clear-undo")) == typed(original), "One Undo did not restore all lanes"
    write("history.redo")
    assert typed(save("recorded-clear-redo")) == typed(expected), "Redo changed baseline or other music"
    return Path(directory) / "recorded-clear-redo.screamseq", expected
