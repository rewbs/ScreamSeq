#!/usr/bin/env python3
"""Optional installed Battery 4 qualification, without windows or device output.

Starts only disposable --automation-test instances. It never connects to the
user's running document. Run after mac/build.sh; requires installed Battery AU
and VST3. Writes concise results and child diagnostics into the output folder.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "mac/Tools"))
from resonance_api import APIError, Client, endpoints


def app_run(output, index, action):
    with tempfile.TemporaryDirectory(prefix="resonance-battery-") as tmp:
        directory = Path(tmp)
        with (output / f"app-{index}.log").open("w") as log:
            process = subprocess.Popen(
                [str(ROOT / "bin/mac-native/ScreamSeq.app/Contents/MacOS/ScreamSeq"), "--automation-test"],
                stdout=log, stderr=log,
                env={**os.environ, "RESONANCE_AUTOMATION_TEST_DIRECTORY": str(directory)})
            found = []
            try:
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    found = endpoints(directory)
                    if found:
                        break
                    assert process.poll() is None, "Test app exited during startup"
                    time.sleep(.05)
                assert len(found) == 1 and found[0]["pid"] == process.pid
                result = action(Client(found[0]["socket"], timeout=300))
                assert process.poll() is None, "Test app crashed"
                return result
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                if found:
                    shutil.rmtree(Path(found[0]["socket"]).parent, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "bin/mac-native/battery-qualification")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = {}

    def exercise(client):
        initial = client.call("document.get")
        started = time.monotonic()
        inventory = client.call("plugin.discover", {"rescan": True})
        results["rescanSeconds"] = time.monotonic() - started
        started = time.monotonic()
        assert client.call("plugin.discover")["data"] == inventory["data"]
        results["warmCacheSeconds"] = time.monotonic() - started
        assert client.call("document.get")["revision"] == initial["revision"]
        try:
            client.call("plugin.discover", {"rescan": 1})
            raise AssertionError("rescan must reject non-booleans")
        except APIError as error:
            assert error.code == -32602
        battery = [p for p in inventory["data"] if "battery 4" in p["name"].lower()]
        assert {p["format"] for p in battery} == {"AU", "VST3"}, battery
        results["inventoryCount"] = len(inventory["data"])
        results["battery"] = []
        for descriptor in battery:
            kind = descriptor["format"]
            scanner = ROOT / "bin/mac-native/plugin-scanner"
            probe_args = (["--validate-vst3", descriptor["path"], descriptor["classID"], "1"] if kind == "VST3"
                          else ["--validate", *[str(descriptor[k]) for k in ("type", "subtype", "manufacturer")]])
            with (args.output / f"scanner-{kind}.log").open("w") as log:
                probe = subprocess.run([str(scanner), *probe_args], stdout=subprocess.PIPE, stderr=log,
                                       timeout=30, check=True, text=True)
            validation = json.loads(probe.stdout)  # Plugin diagnostics cannot corrupt the protocol.
            assert validation["valid"] is True

            def mutate(method, params):
                return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})

            started = time.monotonic()
            mutate("plugin.add", {"descriptor": descriptor})
            loaded = client.call("document.get")["data"]
            assert len(loaded["nativePlugins"]) == 1 and not loaded["pluginError"]
            load_seconds = time.monotonic() - started
            parameters = client.call("plugin.parameters.get", {"slot": 0})["data"]
            assert len(parameters) == validation["parameters"]
            state = client.call("plugin.state.get", {"slot": 0})["data"]["data"]
            assert len(base64.b64decode(state)) > 0
            mutate("plugin.state.set", {"slot": 0, "data": state})
            assert not client.call("document.get")["data"]["pluginError"]
            assert len(client.call("plugin.parameters.get", {"slot": 0})["data"]) == len(parameters)
            mutate("plugin.remove", {"slot": 0})
            assert not client.call("document.get")["data"]["nativePlugins"]
            results["battery"].append({"format": kind, "loadSeconds": load_seconds,
                "stateBytes": len(base64.b64decode(state)), "parameters": len(parameters),
                "probe": validation, "appLoadRestoreRemove": "passed"})
            print(f"PASS Battery 4 {kind}: isolated MIDI/render/state probe; hidden app worker load, {len(parameters)} parameters, state restore and removal", flush=True)
        return inventory["data"]

    inventory = app_run(args.output, 1, exercise)

    def restart(client):
        started = time.monotonic()
        assert client.call("plugin.discover")["data"] == inventory
        results["restartCacheSeconds"] = time.monotonic() - started
        # Full scanning on this installation is measured above. A cached load
        # should have ample margin even on a busy development machine.
        assert results["restartCacheSeconds"] < 2
    app_run(args.output, 2, restart)
    results["hardwareAudioOutput"] = False
    results["visibleWindows"] = False
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2), flush=True)


if __name__ == "__main__":
    main()
