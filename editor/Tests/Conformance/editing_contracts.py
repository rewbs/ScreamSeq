"""Shared editing contracts through fresh, owned native app endpoints.

Runs existing assertions without rebuilding or discovering a musician's app.
Each case owns its process and files; an uncaught failure ends that case.
The runner never retries it or replaces that process's uncertain song. Existing
contracts retain their own expected-rejection assertions and readbacks.
"""
import importlib
from pathlib import Path
import sys

from fixture_loader import FixtureLoader
from native_roundtrip import ROOT, owned_client, write_json
from project_tree import EvidenceError, differences, read_bytes, read_project, sha256

CONTRACTS = ("parameter_preview", "recipe_parameter", "recipe_preview",
             "rack_dry_run", "integer_api", "request_replay")


def run(executable, output):
    output.mkdir(exist_ok=False)
    sys.path.insert(0, str(ROOT / "editor/Tests"))
    report = dict(format="screamseq-editing-contracts-v1", passed=False, parityComplete=False,
                  platform=sys.platform, toolSHA256=sha256(read_bytes(Path(__file__))), cases=[])
    try:
        for name in CONTRACTS:
            directory = output / name
            directory.mkdir()
            row = dict(name=name, passed=False, process={}, requests=[], fixtureLoads=[])
            report["cases"].append(row)
            try:
                module = importlib.import_module(name + "_contract")
                row["contractSHA256"] = sha256(read_bytes(Path(module.__file__)))
                with owned_client(executable, directory, row["process"]) as native:
                    if sys.platform == "win32":
                        from client import ApiError as Error
                    else:
                        from resonance_api import APIError as Error

                    class Journal:
                        def __getattr__(self, key):
                            return getattr(native, key)

                        def call(self, method, params=None, request_id=None):
                            entry = dict(method=method, params=params or {}, requestID=request_id)
                            row["requests"].append(entry)
                            try:
                                entry["result"] = native.call(method, params, request_id=request_id)
                                return entry["result"]
                            except Exception as error:
                                entry["error"] = dict(type=type(error).__name__, code=getattr(error, "code", None),
                                                     message=str(error), data=getattr(error, "data", None))
                                raise

                    client = Journal()

                    def expect_error(code, action):
                        try:
                            action()
                        except Error as error:
                            if error.code != code:
                                raise EvidenceError(f"Expected API code {code}, received {error.code}") from error
                        else:
                            raise EvidenceError(f"Expected API rejection {code}")

                    contract = getattr(module, name + "_contract")
                    contract(client, directory, Error if name == "request_replay" else expect_error)
                    first, second = directory / "final.screamseq", directory / "reopened.screamseq"
                    client.call("document.save", dict(expectedRevision=client.call("document.get")["revision"], path=str(first)))
                    first_bytes = read_bytes(first)
                    loader = FixtureLoader(client, client.call, row["fixtureLoads"])
                    loader.load(first)
                    client.call("document.save", dict(expectedRevision=client.call("document.get")["revision"], path=str(second)))
                    row["reopenComparison"] = differences(read_project(first), read_project(second))
                    row["savedSHA256"] = sha256(first_bytes)
                    row["reopenedSHA256"] = sha256(read_bytes(second))
                    if not row["reopenComparison"]["equal"] or read_bytes(first) != first_bytes:
                        raise EvidenceError("Editing contract changed its accepted tree through save/reopen")
                row["passed"] = True
                print("PASS native editing contract: " + name, flush=True)
            except Exception as error:
                row["error"] = dict(type=type(error).__name__, message=str(error))
                print("FAIL native editing contract: " + name + ": " + str(error), flush=True)
            finally:
                write_json(directory / "report.json", row)
        report["passed"] = len(report["cases"]) == len(CONTRACTS) and all(row["passed"] for row in report["cases"])
        return report
    finally:
        write_json(output / "report.json", report)
