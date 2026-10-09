"""Explicit fixture setup, separate from the API methods under conformance test.

Mac exposes recovery.restore, not document.open. Only a caller-owned private
Recovery directory may stage fixtures for that path. This is not ordinary Open
qualification; native_roundtrip uses positional launch files for Mac no-edit legs.
"""
from pathlib import Path
import uuid

from project_tree import EvidenceError, read_bytes, sha256


class FixtureLoader:
    def __init__(self, client, call, journal):
        self.client, self.call, self.journal = client, call, journal

    def load(self, source):
        source = Path(source).resolve(strict=True)
        data = read_bytes(source)
        recovery = getattr(self.client, "fixture_recovery_directory", None)
        target = source
        entry = dict(source=str(source), sourceSHA256=sha256(data))
        self.journal.append(entry)
        if recovery is None:
            method, params = "document.open", dict(path=str(source), discard=True)
        else:
            # The private directory was created by owned_client, never discovered
            # from a running musician's instance. Refuse an unexpected symlink.
            recovery = Path(recovery)
            if recovery.is_symlink() or not recovery.is_dir():
                raise EvidenceError("Fixture recovery directory is not an owned directory")
            target = recovery / ("conformance-" + uuid.uuid4().hex + ".screamseq")
            with target.open("xb") as stream:
                stream.write(data)
            method, params = "recovery.restore", dict(id=target.name)
        entry.update(method=method, effectiveSource=str(target))
        params["expectedRevision"] = self.call("document.get")["revision"]
        try:
            self.call(method, params)  # Real response/error, no synthetic API alias.
        finally:
            entry["sourceUnchanged"] = read_bytes(source) == data
            entry["effectiveSourceUnchanged"] = read_bytes(target) == data
            if not entry["sourceUnchanged"] or not entry["effectiveSourceUnchanged"]:
                raise EvidenceError("Loading a fixture modified its source")
        return target
