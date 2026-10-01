"""Build/run ARM64 persistence tests, with hashes and a read-only Mac oracle.

All historical versions are generated fixtures, not Mac-produced old files.
No audio, UI, plugins, clipboard, or network operations are performed.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess

REFERENCE_SHA256 = "96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7"
CMAKE = Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
ROOT = Path(__file__).resolve().parents[3]
SCRATCH = Path("C:/Users/P14/AppData/Local/hermes/cache/scratch")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def typed(value):
    if isinstance(value, dict):
        return ("dict", tuple((k, typed(v)) for k, v in sorted(value.items())))
    if isinstance(value, list):
        return ("list", tuple(map(typed, value)))
    if isinstance(value, float):
        return ("float-bits", struct.pack(">d", value))
    return (type(value).__name__, value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New evidence directory under Hermes scratch")
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--cmake", type=Path, default=CMAKE)
    parser.add_argument("--engine-libs", type=Path, default=ROOT / "bin/windows-snapshot-fix/Release")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(SCRATCH.resolve()) or out.exists():
        raise SystemExit("Use a NEW output directory under Hermes scratch")
    env = dict(os.environ)
    for name in ("TMPDIR", "TEMP", "TMP"):
        env[name] = str(SCRATCH)
        if Path(os.environ.get(name, "")).resolve() != SCRATCH.resolve():
            raise SystemExit(f"Set {name} to {SCRATCH} before running fixture commands")
    native, process = ctypes.c_ushort(), ctypes.c_ushort()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.IsWow64Process2.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ushort), ctypes.POINTER(ctypes.c_ushort)]
    if not kernel.IsWow64Process2(kernel.GetCurrentProcess(), ctypes.byref(process), ctypes.byref(native)) or native.value != 0xAA64:
        raise SystemExit("These qualifications require a real ARM64 Windows host")
    if digest(args.reference) != REFERENCE_SHA256:
        raise SystemExit("Actual Mac reference hash mismatch; do not substitute a generated fixture")
    out.mkdir(parents=True)
    files = sorted((ROOT / "windows/Project").glob("*"))
    files += sorted(Path(__file__).parent.glob("*"))
    files += sorted((ROOT / "windows/Tests/ProjectNative").glob("*"))
    files += [ROOT / "editor/TrackerDocument.cpp", ROOT / "editor/TrackerDocument.hpp", ROOT / "editor/SampleArchive.cpp", ROOT / "editor/SampleArchive.hpp"]
    files += [args.engine_libs / f"{name}.lib" for name in ("TrackerEditor", "OpenMPTCore", "TrackerFLAC")]
    source_hashes = {str(p): digest(p) for p in files if p.is_file()}
    report = {"native_machine": hex(native.value), "python_process_machine": hex(process.value),
              "temp_environment": {k: env[k] for k in ("TMPDIR", "TEMP", "TMP")},
              "source_and_library_hashes": source_hashes, "reference_sha256": REFERENCE_SHA256, "runs": []}

    def record():
        (out / "test-results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    def run(label, argv):
        completed = subprocess.run([str(x) for x in argv], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        (out / f"{label}.log").write_text(completed.stdout, encoding="utf-8")
        report["runs"].append({"label": label, "command": [str(x) for x in argv], "exit_code": completed.returncode})
        record()
        print(f"{label}: exit {completed.returncode}", flush=True)
        if completed.returncode:
            print(completed.stdout)
            raise SystemExit(completed.returncode)

    build = out / "build"
    run("configure", [args.cmake, "-S", Path(__file__).parent, "-B", build, "-G", "Visual Studio 17 2022", "-A", "ARM64", f"-DSCREAMSEQ_ENGINE_LIB_DIR={args.engine_libs}"])
    run("build", [args.cmake, "--build", build, "--config", "Release", "--parallel", "3"])
    bindir = build / "Release"
    exe = bindir / "persistence-regression.exe"
    image = exe.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if struct.unpack_from("<H", image, pe + 4)[0] != 0xAA64:
        raise SystemExit("Test executable is not ARM64")
    report["executable_machine"] = "0xaa64"
    for mode in ("promotion", "recovery", "historical", "validation", "lifecycle"):
        run(mode, [exe, mode, out / mode])
    run("ctest", [args.cmake.with_name("ctest.exe"), "--test-dir", build, "-C", "Release", "--output-on-failure"])
    fixture = out / "actual-mac-reference.screamseq"
    shutil.copyfile(args.reference, fixture)
    run("actual-mac-tree", [exe, "mac", out / "mac-tree", fixture])
    mac = plistlib.loads(args.reference.read_bytes())
    reopened = plistlib.loads((out / "mac-tree/actual-mac-noop.screamseq").read_bytes())
    if typed(mac) != typed(reopened):
        raise SystemExit("Independent plistlib oracle found a changed typed tree")
    report["independent_plistlib_entire_typed_tree_equal"] = True
    for name in ("module", "plugins", "automation"):
        if typed(mac.get(name)) != typed(reopened.get(name)):
            raise SystemExit(f"Opaque section changed: {name}")
    metadata = out / "actual-mac-metadata.json"
    metadata.write_text(json.dumps(mac["native"], ensure_ascii=False, allow_nan=False), encoding="utf-8")
    run("actual-mac-metadata", [bindir / "NativeMetadataTests.exe", metadata])
    io = out / "io"
    io.mkdir()
    run("project-io", [bindir / "ProjectIOTests.exe", io])
    native_out = out / "native-project"
    native_out.mkdir()
    run("native-project", [bindir / "NativeProjectTests.exe", fixture, native_out])
    report["reference_unchanged"] = digest(args.reference) == REFERENCE_SHA256 and digest(fixture) == REFERENCE_SHA256
    report["sources_unchanged_during_qualification"] = all(digest(Path(p)) == h for p, h in source_hashes.items())
    report["executable_hashes"] = {p.name: digest(p) for p in bindir.glob("*.exe")}
    record()
    if not report["reference_unchanged"] or not report["sources_unchanged_during_qualification"]:
        raise SystemExit("Source/fixture changed during qualification; inspect report and rerun")
    print("PASS complete ARM64 persistence qualification; Mac execution not performed", flush=True)


if __name__ == "__main__":
    main()
