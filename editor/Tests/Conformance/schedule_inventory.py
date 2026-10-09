"""Audit configured CTest selection and expose separate test entry points.

Consumes `ctest --show-only=json-v1`; it never configures, builds or runs a test.
Source declarations and name overlap are not execution evidence.
"""
import argparse
import ast
import fnmatch
import json
from pathlib import Path
import re
import sys

from native_roundtrip import ROOT, write_json
from project_tree import EvidenceError, read_bytes, sha256


def inventory(configured, platform_name):
    workflow = ROOT / ".github/workflows" / ("ScreamSeq-Windows.yml" if platform_name == "windows" else "ScreamSeq-macOS.yml")
    text = workflow.read_text(encoding="utf-8")
    registered, executables = [], set()
    names = set()
    for test in configured["tests"]:
        name = test["name"]
        if name in names:
            raise EvidenceError(f"Duplicate registered test: {name}")
        names.add(name)
        labels = next((entry["value"] for entry in test.get("properties", []) if entry["name"] == "LABELS"), [])
        if platform_name == "windows":
            selected = []
            if set(labels) & {"portable", "worker"}:
                selected.append("portable-worker")
            if set(labels) & {"native-ui", "preferences", "recovery"}:
                selected.append("native-ui-preferences-recovery")
        else:
            selected = ["all-native-ctest"]
        command = test.get("command", [])
        if command:
            executables.add(re.split(r"[/\\]", command[0])[-1].removesuffix(".exe"))
        registered.append(dict(name=name, labels=labels, scheduledBy=selected, command=command))
    if not registered:
        raise EvidenceError("Configured test inventory is empty")
    if platform_name == "windows":
        for expression in ("-L '^(portable|worker)$'", "-L '^(native-ui|preferences|recovery)$'"):
            if expression not in text:
                raise EvidenceError("CI selection changed; update the inventory's explicit selection rules")
    elif 'ctest --test-dir "$SCREAMSEQ_BUILD_DIR" --output-on-failure' not in text:
        raise EvidenceError("Mac CTest scheduling changed; review inventory selection")

    platform_root = ROOT / ("windows" if platform_name == "windows" else "mac")
    standalone = []
    for path in sorted((platform_root / "Tests").rglob("CMakeLists.txt")):
        source = path.read_text(encoding="utf-8")
        targets = re.findall(r"add_executable\(\s*([^\s)]+)", source)
        standalone.append(dict(path=path.relative_to(ROOT).as_posix(), sha256=sha256(read_bytes(path)),
            declaredExecutableExpressions=targets, matchingMainExecutableNames=sorted(set(targets) & executables),
            qualification="Separate configuration/targets are not implied by a matching main executable name"))
    selectors = sorted(set(re.findall(r"\btest_[a-zA-Z0-9_]+\.[a-zA-Z0-9_]+(?:\.[a-zA-Z0-9_]+)?", text)))
    discovery = re.findall(r"unittest\s+discover\s+-s\s+(\S+)\s+-p\s+['\"]?([^'\"\s]+)", text)
    paths = set((platform_root / "Tests").rglob("test_*.py"))
    for directory, pattern in discovery:
        paths.update((ROOT / directory).glob(pattern))
    python_files = []
    for path in sorted(paths):
        tree = ast.parse(path.read_text(encoding="utf-8-sig"))
        declarations = []
        for node in tree.body:
            if isinstance(node, ast.ClassDef):
                for method in node.body:
                    if isinstance(method, (ast.FunctionDef, ast.AsyncFunctionDef)) and method.name.startswith("test_"):
                        qualified = f"{path.stem}.{node.name}.{method.name}"
                        selected = any(qualified == value or qualified.startswith(value + ".") for value in selectors)
                        selected |= any(path.parent == ROOT / directory and fnmatch.fnmatchcase(path.name, pattern)
                                        for directory, pattern in discovery)
                        declarations.append(dict(method=qualified, line=method.lineno, explicitlyScheduled=selected))
        python_files.append(dict(path=path.relative_to(ROOT).as_posix(), declarations=declarations,
            qualification="Static methods only; inherited tests, runtime skips and script entry points are not inferred"))
    unscheduled = [test["name"] for test in registered if not test["scheduledBy"]]
    overlaps = [test["name"] for test in registered if len(test["scheduledBy"]) > 1]
    return dict(format="screamseq-test-schedule-v1", platform=platform_name, workflowSHA256=sha256(read_bytes(workflow)),
        scope="Configured registrations and source scheduling only; no tests executed",
        registeredCount=len(registered), registered=registered, unscheduledRegistered=unscheduled,
        repeatedRegistered=overlaps, standaloneProjects=standalone, pythonSources=python_files,
        explicitPythonSelectors=selectors,
        explicitPythonDiscovery=[dict(directory=directory, pattern=pattern) for directory, pattern in discovery],
        nonCTestWorkflowCommands=[line.strip() for line in text.splitlines()
            if any(token in line for token in ("-tests\"", "test-interface.sh", "native_roundtrip.py", "-m unittest"))],
        coverageGate=not unscheduled and not overlaps)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ctest-json", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=("windows", "macos"))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        data = read_bytes(args.ctest_json)
        report = inventory(json.loads(data.decode("utf-8-sig")), args.platform)
        report["registrationSHA256"] = sha256(data)
        write_json(args.output, report)
        print(f"{report['registeredCount']} registered; {len(report['unscheduledRegistered'])} unscheduled; "
              f"{len(report['repeatedRegistered'])} repeated; {len(report['standaloneProjects'])} separate CMake projects")
        return 0 if report["coverageGate"] else 1
    except (EvidenceError, OSError, ValueError, KeyError) as error:
        print(f"Schedule inventory error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
