"""Retain five no-edit native save/reopen legs using an explicitly owned app.

No builds, ambient endpoint discovery, normalization, audio, or mutation retries.
Outputs and RPC receipts remain available even when a comparison fails.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import datetime
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import time

from project_tree import CORPUS, EvidenceError, corpus_inventory, differences, digest, read_bytes, read_project, sha256

ROOT = Path(__file__).resolve().parents[3]


def write_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=True, allow_nan=False)
        stream.write("\n")


@contextmanager
def owned_client(executable, output, identity, document=None):
    if sys.platform == "win32":
        if document is not None:
            raise EvidenceError("Positional fixture launch is only used for the Mac Open path")
        sys.path[:0] = [str(ROOT / "windows/Api"), str(ROOT / "windows/Tests")]
        from client import Client, TransportError
        from private_desktop import PrivateDesktop
        with PrivateDesktop() as desktop:
            command = [str(executable), "--inspection", "--automation", "--seconds", "600"]
            pid = desktop.launch(command)
            identity.update(pid=pid, command=command, isolation="never-switched private desktop")
            client = Client(r"\\.\pipe\ScreamSeq.Api." + str(pid), timeout=20)
            deadline = time.monotonic() + 30
            while True:
                try:
                    client.call("document.get")  # Read-only readiness; never retry a write.
                    break
                except TransportError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.1)
            yield client
    elif sys.platform == "darwin":
        sys.path.insert(0, str(ROOT / "mac/Tools"))
        from resonance_api import Client, endpoints
        # Keep Unix socket paths short; discovery is only in this owned directory.
        with tempfile.TemporaryDirectory(prefix="ss-parity-") as temporary:
            private = Path(temporary)
            private.chmod(0o700)
            recovery = private / "Recovery"
            recovery.mkdir(mode=0o700)
            with (output / "host.log").open("x", encoding="utf-8") as log:
                command = [str(executable), "--automation-test", "--inspection"]
                if document is not None:
                    command.append(str(document))
                process = subprocess.Popen(command, stdout=log, stderr=log,
                    env={**os.environ, "RESONANCE_AUTOMATION_TEST_DIRECTORY": str(private)})
                identity.update(pid=process.pid, command=command, isolation="private automation-test socket")
                try:
                    deadline = time.monotonic() + 30
                    while True:
                        found = endpoints(private)
                        if found:
                            if len(found) != 1 or found[0]["pid"] != process.pid:
                                raise EvidenceError("Private endpoint does not belong to the launched process")
                            client = Client(found[0]["socket"], timeout=20)
                            client.fixture_recovery_directory = recovery
                            client.call("document.get")
                            if document is not None and client.call("context.get")["data"].get("file") != str(document):
                                if time.monotonic() >= deadline:
                                    raise EvidenceError("Owned app did not open the requested launch document")
                                time.sleep(.05)
                                continue
                            break
                        if process.poll() is not None:
                            raise EvidenceError(f"Owned app exited during startup: {process.returncode}")
                        if time.monotonic() >= deadline:
                            raise EvidenceError("Owned app did not publish its private endpoint")
                        time.sleep(.05)
                    yield client
                finally:
                    if process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
    else:
        raise EvidenceError("Native roundtrip requires Windows or macOS")


def exercise(client, source, first, second, row):
    """Do not retry failed writes; each fixture owns a separate request journal."""
    row["requests"] = []

    def call(method, params=None):
        entry = dict(method=method, params=params or {})
        row["requests"].append(entry)
        try:
            result = client.call(method, params)
            entry["result"] = result
            return result
        except Exception as error:
            entry["error"] = dict(type=type(error).__name__, message=str(error))
            raise

    def write(method, **params):
        current = call("document.get")
        return call(method, dict(expectedRevision=current["revision"], **params))

    original = read_project(source)
    write("document.open", path=str(source), discard=True)
    row["opened"] = call("document.get")
    write("document.save", path=str(first), overwrite=False)
    saved = read_project(first)
    row["firstSave"] = dict(file=first.name, sha256=sha256(read_bytes(first)), typedSHA256=digest(saved))
    row["originalToFirst"] = differences(original, saved)
    write("document.open", path=str(first), discard=True)
    row["reopened"] = call("document.get")
    write("document.save", path=str(second), overwrite=False)
    reopened = read_project(second)
    row["secondSave"] = dict(file=second.name, sha256=sha256(read_bytes(second)), typedSHA256=digest(reopened))
    row["firstToSecond"] = differences(saved, reopened)
    row["passed"] = row["originalToFirst"]["equal"] and row["firstToSecond"]["equal"]


def exercise_mac(executable, output, source, first, second, row):
    """Normal AppController launch/openPath, with a fresh owned process per open.

    This qualifies persistence, not same-session replacement or rejected loads.
    The codec corpus separately uses recovery.restore in one continuing session.
    """
    row.update(loadPath="native positional launch", processes=[], requests=[])
    original = read_project(source)
    for index, (opened, saved, key) in enumerate(((source, first, "opened"), (first, second, "reopened"))):
        directory = output / ("launch-" + str(index))
        directory.mkdir()
        identity = {}
        row["processes"].append(identity)
        with owned_client(executable, directory, identity, document=opened) as client:
            def call(method, params=None):
                request = dict(method=method, params=params or {})
                row["requests"].append(request)
                try:
                    request["result"] = client.call(method, params)
                    return request["result"]
                except Exception as error:
                    request["error"] = dict(type=type(error).__name__, message=str(error))
                    raise
            row[key] = call("document.get")
            call("document.save", dict(expectedRevision=row[key]["revision"], path=str(saved), overwrite=False))
        tree = read_project(saved)
        row["firstSave" if index == 0 else "secondSave"] = dict(file=saved.name,
            sha256=sha256(read_bytes(saved)), typedSHA256=digest(tree))
    row["originalToFirst"] = differences(original, read_project(first))
    row["firstToSecond"] = differences(read_project(first), read_project(second))
    row["passed"] = row["originalToFirst"]["equal"] and row["firstToSecond"]["equal"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--binary-source-commit", required=True,
                        help="Actual built commit, never inferred from the tooling checkout")
    parser.add_argument("--build-receipt", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="New directory; existing paths are refused")
    inputs_group = parser.add_mutually_exclusive_group()
    inputs_group.add_argument("--input-directory", type=Path,
                        help="Prior leg's first/ directory, with all five original filenames")
    inputs_group.add_argument("--input-file", type=Path, action="append",
                        help="Explicit edited fixture to exchange; repeat for distinct filenames")
    parser.add_argument("--api-vectors", action="store_true",
                        help="Also run the bounded API baseline corpus on an owned copy of F04")
    parser.add_argument("--codec-vectors", action="store_true",
                        help="Observe recovery on owned F04 derivatives, retaining typed differences")
    args = parser.parse_args(argv)
    report = dict(format="screamseq-native-roundtrip-v1", passed=False, fixtures=[], parityComplete=False,
                  qualificationScope="Five no-edit fixture legs; optional API evidence baseline is not a parity pass")
    output = None
    result_code = 2
    try:
        if not re.fullmatch(r"[0-9a-f]{40}", args.binary_source_commit):
            raise EvidenceError("Use the full built commit SHA")
        if not re.fullmatch(r"[0-9a-f]{64}", args.binary_sha256):
            raise EvidenceError("Use the expected executable SHA-256")
        executable = args.executable.resolve(strict=True)
        if sha256(read_bytes(executable)) != args.binary_sha256:
            raise EvidenceError("Executable differs from the explicitly qualified binary")
        receipt = read_bytes(args.build_receipt)
        inventory = corpus_inventory(CORPUS)
        golden = json.loads(read_bytes(Path(__file__).with_name("parity-typed-goldens.json")))
        if inventory != golden:
            raise EvidenceError("Original corpus differs from the pinned typed inventory")
        input_directory = (args.input_directory or CORPUS / "fixtures").resolve(strict=True)
        sources = ([path.resolve(strict=True) for path in args.input_file] if args.input_file else
                   [input_directory / Path(name).name for name in inventory["projects"]])
        if len({path.name for path in sources}) != len(sources):
            raise EvidenceError("Explicit inputs must have distinct filenames; no output may replace another")
        if args.input_file:
            report["qualificationScope"] = "Explicit edited inputs: exact typed preservation through two no-edit saves; does not qualify the originating edit"
        inputs = {path.name: sha256(read_bytes(path)) for path in sources}
        for path in sources:
            read_project(path)
        destination = args.output.resolve()
        destination.mkdir(parents=True, exist_ok=False)
        output = destination
        (output / "first").mkdir()
        (output / "reopened").mkdir()
        (output / "build-receipt").write_bytes(receipt)
        report.update(startedUTC=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            platform=platform.platform(), machine=platform.machine(), python=sys.version,
            executable=str(executable), executableSHA256=args.binary_sha256,
            binarySourceCommit=args.binary_source_commit, buildReceiptSHA256=sha256(receipt),
            inputDirectory=None if args.input_file else str(input_directory),
            inputFiles=[str(path) for path in sources], inputSHA256=inputs,
            originalCorpus=inventory, process={},
            toolSHA256={str(path.relative_to(ROOT)): sha256(read_bytes(path)) for path in
                (Path(__file__), Path(__file__).with_name("project_tree.py"),
                 ROOT / "windows/Tests/private_desktop.py", ROOT / "windows/Api/client.py",
                 ROOT / "mac/Tools/resonance_api.py", Path(__file__).with_name("fixture_loader.py"))})
        with owned_client(executable, output, report["process"]) as client:
            for source in sources:
                row = dict(fixture=source.name, passed=False)
                report["fixtures"].append(row)
                try:
                    if sys.platform == "darwin":
                        launch_output = output / ("open-" + source.stem)
                        launch_output.mkdir()
                        exercise_mac(executable, launch_output, source, output / "first" / source.name,
                                     output / "reopened" / source.name, row)
                    else:
                        exercise(client, source, output / "first" / source.name,
                                 output / "reopened" / source.name, row)
                except Exception as error:
                    row["error"] = dict(type=type(error).__name__, message=str(error))
                    # Transport errors may leave a write in flight. Stop the run;
                    # never send another document replacement to that process.
                    raise
                print(f"{'PASS' if row['passed'] else 'FAIL'} {source.name}", flush=True)
            if args.api_vectors:
                from api_vectors import run
                api_report = run(client, output / "api")
                report["api"] = dict(baselineMatched=api_report["baselineMatched"],
                    parityComplete=api_report["parityComplete"], report="api/report.json",
                    sha256=sha256(read_bytes(output / "api/report.json")))
                print(f"API baseline matched: {api_report['baselineMatched']}; parity complete: False; "
                      f"preservation failures: {len(api_report.get('preservationFailures', []))}", flush=True)
            if args.codec_vectors:
                from codec_vectors import run
                codec_report = run(client, output / "codec")
                report["codec"] = dict(safetyPassed=codec_report["safetyPassed"],
                    observationComplete=codec_report["observationComplete"], parityComplete=False,
                    baselineMatched=codec_report["baselineMatched"],
                    report="codec/report.json", sha256=sha256(read_bytes(output / "codec/report.json")))
        report["inputsUnchanged"] = all(sha256(read_bytes(path)) == inputs[path.name] for path in sources)
        report["binaryUnchanged"] = sha256(read_bytes(executable)) == args.binary_sha256
        report["passed"] = (len(report["fixtures"]) == len(sources) and all(row["passed"] for row in report["fixtures"])
                            and report["inputsUnchanged"] and report["binaryUnchanged"]
                            and report.get("api", {}).get("baselineMatched", True)
                            and report.get("codec", {}).get("safetyPassed", True)
                            and report.get("codec", {}).get("baselineMatched", True))
        result_code = 0 if report["passed"] else 1
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        print(f"Roundtrip error: {error}", file=sys.stderr)
        result_code = 2
    finally:
        if output is not None:
            try:
                report["inputsUnchanged"] = all(sha256(read_bytes(path)) == inputs[path.name] for path in sources)
                report["binaryUnchanged"] = sha256(read_bytes(executable)) == args.binary_sha256
                report["originalCorpusUnchanged"] = corpus_inventory(CORPUS) == inventory
                if not all(report[key] for key in ("inputsUnchanged", "binaryUnchanged", "originalCorpusUnchanged")):
                    report["passed"] = False
                    raise EvidenceError("Input, binary, or original corpus changed during qualification")
            except Exception as error:
                report["passed"] = False
                report["preservationError"] = str(error)
            report["finishedUTC"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            write_json(output / "report.json", report)
            if not report["passed"]:
                result_code = 2 if "error" in report or "preservationError" in report else 1
    return result_code


if __name__ == "__main__":
    sys.exit(main())
