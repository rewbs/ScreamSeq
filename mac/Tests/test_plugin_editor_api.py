"""Rack-editor socket admission in a disposable host; native windows have separate tests."""


def plugin_editor_api(client, expect_error):
    def document():
        return client.call("document.get")

    def write(method, **params):
        return client.call(method, {"expectedRevision": document()["revision"], **params})

    gain = next(p for p in client.call("plugin.discover", {"format": "Built-in"})["data"]
                if p["classID"] == "resonance.gainer.v1")
    slot = write("plugin.add", descriptor=gain)["data"]["slot"]
    before = document()
    identity = before["data"]["nativePlugins"][slot]["instanceID"]
    key = {"plugin": identity, "parameter": 1, "gesture": "editor-presentation"}
    write("plugin.parameters.preview.set", **key, value=-12)
    preview = client.call("plugin.parameters.preview.get")["data"]
    for method in ("plugin.editor.open", "plugin.editor.close"):
        expect_error(-32001, lambda: client.call(method, {"expectedRevision": "stale", "slot": slot}))
        for patch in ({"slot": True}, {"slot": 0.5}, {"slot": -1}, {"slot": 64}, {"unknown": 0}, {"dryRun": True}):
            expect_error(-32602, lambda patch=patch: write(method, **({"slot": slot} | patch)))
        assert document() == before
        assert client.call("plugin.parameters.preview.get")["data"] == preview
    # Built-ins expose host controls, so opening a vendor window is a clear
    # refusal, while close is idempotent and leaves the active preview intact.
    expect_error(-32003, lambda: write("plugin.editor.open", slot=slot))
    params = {"slot": slot, "expectedRevision": before["revision"]}
    closed = client.call("plugin.editor.close", params, request_id="editor-close-once")
    assert closed["data"] == {"open": False, "plugin": identity}
    assert not closed["changed"] and not closed["playbackStopped"]
    assert client.call("plugin.editor.close", params, request_id="editor-close-once") == closed
    assert write("plugin.editor.close", slot=slot)["data"] == closed["data"]
    assert document() == before and client.call("plugin.parameters.preview.get")["data"] == preview
    write("plugin.parameters.preview.cancel", **key)
    write("plugin.remove", slot=slot)
    print("PASS rack-editor socket: strict/stale admission, presentation no-op/replay, builtin refusal and retained preview")
