"""Qualify an explicitly retained CI app; never configure or build.

An optional prior artifact supplies the actual Windows or Mac save leg. Archive,
executable, receipt, source delta and preceding save hashes are checked before launch.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import zipfile

from native_roundtrip import ROOT, main as native_main, write_json
from project_tree import CORPUS, EvidenceError, read_bytes, sha256


def read_json(path):
    return json.loads(read_bytes(path, 16 * 1024 * 1024).decode("utf-8-sig"))


def one(root, pattern):
    paths = sorted(root.rglob(pattern))
    if len(paths) != 1:
        raise EvidenceError(f"Expected exactly one {pattern}, found {len(paths)}")
    return paths[0]


def identity_at(path):
    identity = read_json(path)
    if not re.fullmatch(r"[0-9a-f]{40}", identity.get("sourceCommit", "")):
        raise EvidenceError("Archive has no full built source commit")
    for key in ("executableSHA256", "archiveSHA256", "buildReceiptSHA256"):
        if not re.fullmatch(r"[0-9a-f]{64}", identity.get(key, "")):
            raise EvidenceError(f"Invalid retained identity: {key}")
    return identity


def check_hash(path, expected):
    actual = sha256(read_bytes(path))
    if actual != expected:
        raise EvidenceError(f"Retained file hash mismatch: {path}")
    return actual


def tooling_only(path):
    return (path.startswith(("doc/", ".github/workflows/", "editor/Tests/Conformance/"))
            or path == "assets/branding/BRANDING.md"
            or (path.startswith(("windows/Tests/", "mac/Tests/")) and path.endswith(".py")))


def source_delta(commit):
    # CI checks out this repository with full history. A prior PR merge object may
    # no longer be a branch tip; fetch only that explicit hexadecimal object.
    token = os.environ.pop("SCREAMSEQ_FETCH_TOKEN", None)
    exists = subprocess.run(["git", "cat-file", "-e", commit + "^{commit}"], cwd=ROOT,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if exists.returncode:
        environment = dict(os.environ)
        if token:
            # An old PR merge object may need an authenticated read after checkout
            # removed its credentials. Keep the header out of argv/config/logs and
            # remove the token from the environment before any native app launches.
            environment.update(GIT_CONFIG_COUNT="1",
                GIT_CONFIG_KEY_0="http.https://github.com/.extraheader",
                GIT_CONFIG_VALUE_0="AUTHORIZATION: basic " + base64.b64encode(("x-access-token:" + token).encode()).decode())
        subprocess.run(["git", "fetch", "--no-tags", "origin", commit], cwd=ROOT, env=environment, check=True)
    def names(args):
        return [name.decode("utf-8") for name in subprocess.check_output(["git", *args], cwd=ROOT).split(b"\0") if name]
    changed = sorted(set(names(["diff", "--name-only", "-z", commit, "HEAD"]) +
                         names(["diff", "--name-only", "-z", "HEAD"]) +
                         names(["ls-files", "--others", "--exclude-standard", "-z"])))
    product_changes = [name for name in changed if not tooling_only(name)]
    if product_changes:
        raise EvidenceError("Retained app cannot qualify changed product/build inputs: " + ", ".join(product_changes))
    return changed


def unpack(archive, destination):
    destination.mkdir(exist_ok=False)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as bundle:
            seen, total = set(), 0
            for item in bundle.infolist():
                path = PurePosixPath(item.filename)
                key = item.filename.casefold()
                if (path.is_absolute() or ".." in path.parts or "\\" in item.filename
                        or ":" in item.filename or key in seen
                        or (item.external_attr >> 16) & 0o170000 == 0o120000):
                    raise EvidenceError("Unsafe or duplicate Windows archive member")
                seen.add(key)
                total += item.file_size
                if total > 600 * 1024 * 1024:
                    raise EvidenceError("Native archive exceeds extraction bound")
            bundle.extractall(destination)
    else:
        with tarfile.open(archive, "r:gz") as bundle:
            members = bundle.getmembers()
            if sum(member.size for member in members) > 600 * 1024 * 1024:
                raise EvidenceError("Native archive exceeds extraction bound")
            if len({member.name for member in members}) != len(members):
                raise EvidenceError("Duplicate Mac archive member")
            # Python 3.12 data filtering preserves safe internal bundle symlinks
            # and executable modes while rejecting traversal, devices and escapes.
            bundle.extractall(destination, filter="data")


def retained_app(artifact, output):
    identity_path = one(artifact, "native-app-identity.json")
    identity = identity_at(identity_path)
    archive = identity_path.parent / ("native-app.zip" if sys.platform == "win32" else "native-app.tar.gz")
    check_hash(archive, identity["archiveSHA256"])
    changed = source_delta(identity["sourceCommit"])
    unpack(archive, output / "app")
    if sys.platform == "win32":
        executable = output / "app/ScreamSeq.exe"
        receipt = identity_path.parent / "BuildInfo.json"
    elif sys.platform == "darwin":
        executable = output / "app/ScreamSeq.app/Contents/MacOS/ScreamSeq"
        receipt = output / "app/ScreamSeq.app/Contents/Resources/BuildInfo.json"
    else:
        raise EvidenceError("Retained native qualification requires Windows or macOS")
    check_hash(executable, identity["executableSHA256"])
    check_hash(receipt, identity["buildReceiptSHA256"])
    if read_json(receipt).get("baseRevision") != identity["sourceCommit"]:
        raise EvidenceError("Build receipt and archive source commit disagree")
    return executable, receipt, identity, changed


def prior_leg(report_path, identity, expected_names):
    report = read_json(report_path)
    if (report.get("format") != "screamseq-native-roundtrip-v1"
            or report.get("binarySourceCommit") != identity["sourceCommit"]
            or report.get("executableSHA256") != identity["executableSHA256"]
            or report.get("buildReceiptSHA256") != identity["buildReceiptSHA256"]
            or not all(report.get(key) is True for key in
                       ("inputsUnchanged", "binaryUnchanged", "originalCorpusUnchanged"))):
        raise EvidenceError("Prior leg lacks matching binary/source or preservation evidence")
    rows = report.get("fixtures", [])
    if len(rows) != len(expected_names) or {row.get("fixture") for row in rows} != set(expected_names):
        raise EvidenceError("Prior leg fixture set differs from the required exchange")
    sources = []
    for row in rows:
        if not (row.get("passed") is True and row["originalToFirst"]["equal"] is True
                and row["firstToSecond"]["equal"] is True):
            raise EvidenceError("Prior fixture leg did not preserve its exact typed tree")
        path = report_path.parent / "first" / row["fixture"]
        check_hash(path, row["firstSave"]["sha256"])
        check_hash(report_path.parent / "reopened" / row["fixture"], row["secondSave"]["sha256"])
        sources.append(path)
    return sources


def input_legs(artifact, layout, target_identity):
    original_names = sorted(path.name for path in (CORPUS / "fixtures").glob("*.screamseq"))
    edited_names = ["rendered.screamseq", "imported.screamseq"]
    lineage = {}
    if layout == "windows":
        identity = identity_at(one(artifact, "native-app-identity.json"))
        report_path = one(artifact, "parity-conformance/report.json")
        no_edit = prior_leg(report_path, identity, original_names)
        expected_inputs = {name: sha256(read_bytes(CORPUS / "fixtures" / name)) for name in original_names}
        if read_json(report_path).get("inputSHA256") != expected_inputs:
            raise EvidenceError("The first Windows leg did not originate from the five pinned fixtures")
        asset_report = one(artifact, "parity-fixture-output/*/assets.json")
        assets = read_json(asset_report)
        if assets.get("renders") != 1 or assets.get("imports") != 1:
            raise EvidenceError("Native fixture did not record one render and one import")
        check_hash(asset_report.parent / "original.screamseq",
                   sha256(read_bytes(CORPUS / "fixtures/04-complete-reference.screamseq")))
        log = one(artifact, "screamseq-retained-editors.log")
        if not re.search(r"workspace-parity-fixture-tests\s+\.+\s+Passed\s", read_bytes(log).decode("utf-8-sig")):
            raise EvidenceError("Originating native edited-fixture test has no passing CTest result")
        edited = [asset_report.parent / name for name in edited_names]
        lineage.update(nativeAssetReportSHA256=sha256(read_bytes(asset_report)), nativeTestLogSHA256=sha256(read_bytes(log)))
    else:
        exchange_path = one(artifact, "retained-run.json")
        exchange = read_json(exchange_path)
        if (exchange.get("format") != "screamseq-retained-run-v1" or exchange.get("passed") is not True
                or exchange.get("mode") != "exchange" or set(exchange.get("legs", {})) != {"no-edit", "edited"}):
            raise EvidenceError("Prior exchange is incomplete or failed")
        identity = exchange["appIdentity"]
        report_path = exchange_path.parent / "no-edit/report.json"
        edited_report = exchange_path.parent / "edited/report.json"
        for name, path in (("no-edit", report_path), ("edited", edited_report)):
            check_hash(path, exchange["legs"][name]["reportSHA256"])
        no_edit = prior_leg(report_path, identity, original_names)
        edited = prior_leg(edited_report, identity, edited_names)
        lineage.update(priorExchangeSHA256=sha256(read_bytes(exchange_path)))
    if identity["sourceCommit"] != target_identity["sourceCommit"]:
        raise EvidenceError("Both platform apps must come from the same frozen source commit")
    lineage.update(sourceIdentity=identity, priorNoEditReportSHA256=sha256(read_bytes(report_path)),
                   inputs={path.name: sha256(read_bytes(path)) for path in no_edit + edited})
    return {"no-edit": no_edit, "edited": edited}, lineage


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-artifact", required=True, type=Path)
    parser.add_argument("--input-artifact", type=Path)
    parser.add_argument("--input-layout", choices=("windows", "exchange"), default="windows")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    report = dict(format="screamseq-retained-run-v1", passed=False, parityComplete=False, legs={},
                  mode="exchange" if args.input_artifact else "baseline",
                  artifactOrigin={key: os.environ.get(key) for key in
                                  ("APP_RUN", "APP_ARTIFACT", "INPUT_RUN", "INPUT_ARTIFACT", "GITHUB_RUN_ID")})
    output = None
    code = 2
    try:
        destination = args.output.resolve()
        destination.mkdir(parents=True, exist_ok=False)
        output = destination
        executable, receipt, identity, changed = retained_app(args.app_artifact.resolve(strict=True), output)
        report.update(appIdentity=identity, toolingSourceChanges=changed,
                      toolSHA256=sha256(read_bytes(Path(__file__))),
                      toolingCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip())
        common = ["--executable", str(executable), "--binary-sha256", identity["executableSHA256"],
                  "--binary-source-commit", identity["sourceCommit"], "--build-receipt", str(receipt)]
        if args.input_artifact:
            legs, report["lineage"] = input_legs(args.input_artifact.resolve(strict=True), args.input_layout, identity)
        else:
            legs = {"baseline": []}
        for name, sources in legs.items():
            arguments = common + ["--output", str(output / name)]
            if name == "baseline":
                arguments += ["--api-vectors", "--codec-vectors"]
            elif name == "no-edit":
                arguments += ["--input-directory", str(sources[0].parent)]
            else:
                for source in sources:
                    arguments += ["--input-file", str(source)]
            result = native_main(arguments)
            leg_path = output / name / "report.json"
            report["legs"][name] = dict(exitCode=result, reportSHA256=sha256(read_bytes(leg_path)))
        report["passed"] = all(leg["exitCode"] == 0 for leg in report["legs"].values())
        code = 0 if report["passed"] else 1
    except (EvidenceError, OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        print(f"Retained qualification error: {error}", file=sys.stderr)
    finally:
        if output is not None and output.is_dir() and not (output / "retained-run.json").exists():
            write_json(output / "retained-run.json", report)
    return code


if __name__ == "__main__":
    sys.exit(main())
