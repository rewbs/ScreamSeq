"""Module publication/replay through an owned native endpoint, without retries."""
from pathlib import Path
import plistlib
import uuid

from Conformance.project_tree import typed


def module_export_contract(client, directory, api_error):
    directory = Path(directory)

    def call(method, params=None, request_id=None):
        return client.call(method, params or {}, request_id=request_id)

    def doc():
        return call("document.get")

    def write(method, **params):
        return call(method, dict(expectedRevision=doc()["revision"], **params))

    def reject(code, params, request_id=None, method="document.exportModule"):
        try:
            call(method, params, request_id)
        except api_error as error:
            assert error.code == code, (method, error.code, str(error))
            return error.code, str(error), error.data
        raise AssertionError(f"{method} accepted an invalid export/replay")

    assert doc()["data"]["format"].lower() == "mptm", "Contract requires a fresh MPTM song"
    baseline_path = directory / "export-baseline.screamseq"
    write("document.save", path=str(baseline_path))
    baseline = typed(plistlib.loads(baseline_path.read_bytes()))
    title, redo_title = "Export kept", "Export redo"
    assert doc()["data"]["title"] != title
    write("document.patch", title=title)
    write("document.patch", title=redo_title)
    write("history.undo")
    before = doc()
    context = call("context.get")["data"]
    output = directory / "export.mptm"
    params = dict(expectedRevision=before["revision"], path=str(output))
    prefix = uuid.uuid4().hex

    def unchanged():
        assert doc() == before, "Export/replay altered document, revision or history"
        # These fields are supplied by the Mac application adapter. Windows
        # verifies path/dirty through the native application/worker fixtures.
        current = call("context.get")["data"]
        for field in ("file", "dirty"):
            if field in context:
                assert current[field] == context[field], f"Export changed {field}"

    preview = call("document.exportModule", dict(params, dryRun=True))
    assert not preview["changed"] and not preview["data"]["written"] and not output.exists()
    unchanged()
    for extra in (dict(path="relative.mptm"), dict(path=str(output.with_suffix(".it"))),
                  dict(path=str(directory / "missing" / "export.mptm")),
                  dict(overwrite=1), dict(dryRun=1), dict(unexpected=True)):
        reject(-32602, dict(params, **extra))
        assert not output.exists()
        unchanged()
    reject(-32001, dict(params, expectedRevision="stale"))
    unchanged()

    accepted_id = prefix + "-exclusive"
    accepted = call("document.exportModule", params, accepted_id)
    exported = output.read_bytes()
    assert exported.startswith(b"IMPM")
    assert not accepted["changed"] and not accepted["playbackStopped"]
    assert accepted["data"]["written"] and accepted["data"]["projectVersion"] is None
    assert accepted["revision"] == before["revision"]
    unchanged()

    # A cached receipt must describe the earlier operation, not repeat it or
    # reinterpret it against the now externally changed destination.
    sentinel = b"Owned external replacement; do not overwrite during replay"
    output.write_bytes(sentinel)
    assert call("document.exportModule", params, accepted_id) == accepted
    assert output.read_bytes() == sentinel
    reject(-32600, dict(params, overwrite=True), accepted_id)
    reject(-32600, {}, accepted_id, method="document.get")
    assert output.read_bytes() == sentinel
    unchanged()

    rejected_id = prefix + "-existing"
    rejection = reject(-32602, params, rejected_id)
    output.unlink()
    assert reject(-32602, params, rejected_id) == rejection
    assert not output.exists(), "A retained rejection retried publication"
    reject(-32600, dict(params, overwrite=True), rejected_id)
    unchanged()

    output.write_bytes(sentinel)
    replace_params = dict(params, overwrite=True)
    replace_id = prefix + "-replace"
    replaced = call("document.exportModule", replace_params, replace_id)
    assert replaced["data"]["written"] and output.read_bytes() == exported
    output.write_bytes(sentinel)
    assert call("document.exportModule", replace_params, replace_id) == replaced
    assert output.read_bytes() == sentinel
    unchanged()
    assert not list(directory.glob("*.staged.*")), "Export leaked its staging file"

    write("history.redo")
    assert doc()["data"]["title"] == redo_title, "Export consumed pending Redo"
    write("history.undo")
    assert doc()["data"]["title"] == title
    write("history.undo")
    undone_path = directory / "export-undone.screamseq"
    write("document.save", path=str(undone_path))
    assert typed(plistlib.loads(undone_path.read_bytes())) == baseline, \
        "Export/replay introduced history or changed unrelated persisted state"
    write("history.redo")
    assert doc()["data"]["title"] == title
    after_path = directory / "export-after.screamseq"
    write("document.save", path=str(after_path))
    print("PASS module export: strict admission, publication, terminal replay, native identity and exact history")
    return after_path, title
