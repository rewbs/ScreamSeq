#!/usr/bin/env python3
"""Packaged-app sampling API checks on a disposable document.

Default: private --automation-test process, offline rendering and read-only input
catalogue checks; no microphone permission request or audio device is started.

Optional loopback: --pid PID [--socket PATH] --input-device 'BlackHole 2ch'
uses an explicitly owned --inspection/--automation-test process. Permission must
already be authorized. --playback-during-capture additionally asserts that the
operator has configured THAT QA app's output to BlackHole; only then may this
test start transport. It never configures an output or changes system defaults.
Without that flag, provide a loopback signal separately; nonzero PCM is required.
The optional path fails honestly on denied permission, missing device or silence.
"""
import argparse
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "mac/Tools"))
from resonance_api import APIError, Client, endpoints


def expect_error(code, action):
    try:
        action()
    except APIError as error:
        assert error.code == code, (error.code, str(error))
    else:
        raise AssertionError(f"Expected API error {code}")


def write(client, method, **params):
    revision = client.call("document.get")["revision"]
    return client.call(method, {"expectedRevision": revision, **params})


def pcm(client, sample):
    first = client.call("sample.pcm.get", {"sample": sample, "frames": 0})["data"]
    chunks = []
    for offset in range(0, first["totalFrames"], 65536):
        result = client.call("sample.pcm.get", {"sample": sample, "start": offset,
                            "frames": min(65536, first["totalFrames"] - offset)})["data"]
        assert result["format"] == first["format"] and result["channels"] == first["channels"]
        assert result["rate"] == first["rate"] and result["totalFrames"] == first["totalFrames"]
        chunks.append(base64.b64decode(result["data"], validate=True))
    return {key: first[key] for key in ("format", "channels", "rate", "totalFrames")}, b"".join(chunks)


def counts(client):
    song = client.call("document.get")["data"]
    return len(song["samples"]), len(song["instruments"])


def recording_reads(client):
    before = client.call("document.get")
    state = client.call("sample.recording.get")["data"]
    assert not state["take"] and not state["capturing"], "Refuse to overwrite another pending take"
    devices = client.call("sample.recording.devices")["data"]
    assert devices["permission"] in ("authorized", "notDetermined", "denied", "restricted")
    for device in devices["devices"]:
        assert isinstance(device["id"], str) and device["id"]
        assert isinstance(device["name"], str) and device["channels"] > 0
        assert isinstance(device["default"], bool)
    assert not client.call("sample.recording.get")["data"]["take"]
    expect_error(-32602, lambda: client.call("sample.recording.devices", {"unknown": True}))
    expect_error(-32602, lambda: client.call("sample.recording.get", {"unknown": True}))
    for method in ("get", "stop", "discard"):
        expect_error(-32001, lambda method=method: client.call("sample.recording." + method, {"take": "retired"}))
    expect_error(-32001, lambda: client.call("sample.recording.start", {"expectedRevision": "stale"}))
    for invalid in ({"channels": True}, {"channels": 3}, {"firstChannel": -1},
                    {"maxSeconds": 0}, {"maxSeconds": 301}, {"unknown": 1}):
        expect_error(-32602, lambda invalid=invalid: write(client, "sample.recording.start", **invalid))
    assert client.call("document.get") == before, "Input inspection and rejected commands must preserve song/history"
    print("PASS sampling socket input catalogue, idle state, strict fields/types and stale guards; no input opened", flush=True)
    return devices


def seed(client):
    before = client.call("document.get")["data"]
    assert not before["nativePlugins"] and not before["instruments"], "Use a fresh private demo song for this fixture"
    write(client, "document.timing.set", mode="classic", tempo=125, speed=6,
          rowsPerBeat=4, rowsPerMeasure=16, groove=[])
    raw = b"".join(struct.pack("<h", round(6000 * math.sin(2 * math.pi * 440 * i / 48000)))
                   for i in range(48000))
    sample = write(client, "sample.pcm.set", format="s16le", channels=1, rate=48000,
                   data=base64.b64encode(raw).decode(), name="Sampling socket tone")["data"]["sample"]
    pattern = write(client, "pattern.create", rows=8)["data"]["pattern"]
    write(client, "pattern.apply", cells=[{"pattern": pattern, "row": 0, "channel": 0,
                                         "note": 61, "instrument": sample, "volumeCommand": 1, "volume": 64}])
    return pattern, sample


def selection_render(client, directory):
    pattern, source_sample = seed(client)
    source = pcm(client, source_sample)
    source_pattern = client.call("pattern.get", {"pattern": pattern})["data"]
    selection = {"pattern": pattern, "firstRow": 2, "lastRow": 4,
                 "firstChannel": 0, "lastChannel": 0, "name": "Socket selected phrase"}
    before = client.call("document.get")
    preview = write(client, "sample.renderSelection", **selection, createInstrument=True, dryRun=True)
    assert not preview["changed"] and preview["data"]["frames"] == 0 and preview["data"]["dryRun"]
    assert client.call("document.get") == before
    expect_error(-32001, lambda: client.call("sample.renderSelection", {**selection, "expectedRevision": "stale"}))
    for invalid in ({"firstRow": True}, {"lastRow": 1}, {"lastRow": 8}, {"firstChannel": -1},
                    {"lastChannel": 999}, {"tailSeconds": True}, {"tailSeconds": -1}, {"tailSeconds": 61},
                    {"createInstrument": 1}, {"name": ""}, {"unknown": True}):
        expect_error(-32602, lambda invalid=invalid: write(client, "sample.renderSelection", **{**selection, **invalid}))
        assert client.call("document.get") == before

    full = write(client, "sample.renderSelection", **{**selection, "firstRow": 0, "lastRow": 7,
                                                    "name": "Socket full reference"})["data"]
    assert full["frames"] == full["selectedFrames"] == 46080 and full["preRollFrames"] == 0, full
    full_pcm = pcm(client, full["sample"])
    assert full_pcm[0] == {"format": "s16le", "channels": 2, "rate": 48000, "totalFrames": 46080}
    assert any(full_pcm[1]), "Offline tone must produce nonzero sample PCM"
    before_counts = counts(client)
    current = client.call("document.get")["revision"]
    selected = write(client, "sample.renderSelection", **selection, createInstrument=True)["data"]
    assert selected["frames"] == selected["selectedFrames"] == 17280 and selected["preRollFrames"] == 11520, selected
    assert selected["instrument"] > 0 and selected["sample"] == before_counts[0] + 1
    selected_pcm = pcm(client, selected["sample"])
    assert selected_pcm[1] == full_pcm[1][11520 * 4:(11520 + 17280) * 4], "Selection PCM must equal exact crop after row-zero warmup"
    mapping = client.call("instrument.get", {"instrument": selected["instrument"]})["data"]
    after_counts = counts(client)
    assert source == pcm(client, source_sample)
    assert source_pattern == client.call("pattern.get", {"pattern": pattern})["data"]
    expect_error(-32001, lambda: client.call("sample.renderSelection", {**selection, "expectedRevision": current}))
    write(client, "history.undo")
    assert counts(client) == before_counts, "One Undo removes the new sample and instrument conversion"
    assert source == pcm(client, source_sample)
    write(client, "history.redo")
    assert counts(client) == after_counts and selected_pcm == pcm(client, selected["sample"])
    assert client.call("instrument.get", {"instrument": selected["instrument"]})["data"] == mapping

    tail = write(client, "sample.renderSelection", **selection, tailSeconds=.025)["data"]
    assert tail["selectedFrames"] == 17280 and tail["tailFrames"] == 1200 and tail["frames"] == 18480, tail
    assert pcm(client, tail["sample"])[1][:17280 * 4] == selected_pcm[1]
    project = directory / "sampling-selection.screamseq"
    write(client, "document.save", path=str(project))
    assert project.is_file() and project.stat().st_size > 0
    print("PASS sampling socket render: exact inclusive duration/crop/tail, dry-run/invalid/stale, preserved source and one Undo/Redo", flush=True)
    return {"project": project, "pattern": pattern, "sourceSample": source_sample,
            "sample": selected["sample"], "instrument": selected["instrument"],
            "pcm": selected_pcm, "instrumentState": mapping}


def capture_loopback(client, directory, device_name, pattern, playback):
    assert device_name == "BlackHole 2ch", "Only an explicitly selected BlackHole 2ch virtual input is allowed"
    catalog = client.call("sample.recording.devices")["data"]
    assert catalog["permission"] == "authorized", f"Microphone permission is {catalog['permission']}; grant it explicitly in this QA app before this optional test"
    matches = [device for device in catalog["devices"] if device["name"] == device_name]
    assert len(matches) == 1 and matches[0]["channels"] >= 2, "Requested loopback input is unavailable or ambiguous"
    assert not client.call("sample.recording.get")["data"]["take"], "Another take is already staged"
    assert not client.call("transport.get")["data"]["playing"], "Stop the owned QA transport before this test"
    take = None
    started_playback = False
    try:
        if playback:
            # This explicit flag is the operator's assertion that the owned QA
            # app was routed to BlackHole in Audio Settings. There is deliberately
            # no fallback to starting transport without that authorization.
            write(client, "transport.play", pattern=pattern, startRow=0, endRow=8, cursorRow=0, loop=True)
            started_playback = True
        initial = write(client, "sample.recording.start", device=matches[0]["id"], firstChannel=0,
                        channels=2, maxSeconds=8)["data"]
        take = initial["take"]
        assert take and initial["capturing"] and initial["device"] == matches[0]["id"]
        assert initial["channels"] == 2 and initial["maxSeconds"] <= 8
        # This edit deliberately changes the document revision during capture.
        # It is outside the sounding range; commit must append against the new
        # current revision without requiring the obsolete starting revision.
        write(client, "pattern.apply", cells=[{"pattern": pattern, "row": 7, "channel": 1,
                                             "note": 67, "instrument": 1}])
        deadline = time.monotonic() + 4
        last = initial
        while time.monotonic() < deadline:
            last = client.call("sample.recording.get", {"take": take})["data"]
            assert not last.get("error"), last
            if last["seconds"] >= .4:
                break
            time.sleep(.03)
        assert last["seconds"] >= .4 and last["frames"] > 0, "Input failed to deliver a bounded short capture"
        stopped = client.call("sample.recording.stop", {"take": take})["data"]
        assert not stopped["capturing"] and stopped["frames"] >= last["frames"]
        assert stopped["baseRevision"] == initial["baseRevision"]
        if started_playback:
            write(client, "transport.stop"); started_playback = False
        before_counts = counts(client)
        args = {"take": take, "name": "Socket loopback take", "createInstrument": True}
        expect_error(-32001, lambda: client.call("sample.recording.commit", {**args, "expectedRevision": initial["baseRevision"]}))
        expect_error(-32602, lambda: write(client, "sample.recording.commit", **{**args, "name": ""}))
        assert client.call("sample.recording.get", {"take": take})["data"]["frames"] == stopped["frames"]
        current = client.call("document.get")
        checked = write(client, "sample.recording.commit", **args, dryRun=True)
        assert not checked["changed"] and counts(client) == before_counts
        assert client.call("document.get") == current
        assert client.call("sample.recording.get", {"take": take})["data"]["take"] == take
        committed = write(client, "sample.recording.commit", **args)["data"]
        take = None
        assert committed["frames"] == stopped["frames"] and committed["channels"] == 2
        audio = pcm(client, committed["sample"])
        values = struct.unpack("<" + "h" * (len(audio[1]) // 2), audio[1])
        assert max(abs(value) for value in values) > 32, "No meaningful loopback signal; do not count silence as capture qualification"
        assert audio[0]["totalFrames"] == stopped["frames"] and audio[0]["rate"] == stopped["sampleRate"]
        mapping = client.call("instrument.get", {"instrument": committed["instrument"]})["data"]
        assert not client.call("sample.recording.get")["data"]["take"]
        write(client, "history.undo"); assert counts(client) == before_counts
        write(client, "history.redo"); assert pcm(client, committed["sample"]) == audio
        assert client.call("instrument.get", {"instrument": committed["instrument"]})["data"] == mapping
        project = directory / "sampling-loopback.screamseq"
        write(client, "document.save", path=str(project))
        print("PASS optional loopback socket: guarded input, nonzero PCM, current-revision append after edit, retained failed/dry take and one Undo/Redo", flush=True)
        return {"project": project, "sample": committed["sample"], "instrument": committed["instrument"],
                "pcm": audio, "instrumentState": mapping}
    finally:
        if take:
            # The take was created by this test, so failed qualification must
            # stop its microphone and not leave a hidden active capture behind.
            try:
                client.call("sample.recording.stop", {"take": take})
                client.call("sample.recording.discard", {"take": take})
            except Exception as error:
                print(f"WARNING: could not stop/discard test take {take}: {error}", file=sys.stderr)
        if started_playback:
            write(client, "transport.stop")


class PrivateApp:
    def __init__(self, bundle, directory):
        self.executable = bundle / "Contents/MacOS/ScreamSeq"
        self.directory = directory
        self.log = (directory / "app.log").open("w+")
        self.process = None
        self.endpoint = None

    def launch(self, project=None):
        assert self.process is None
        command = [str(self.executable), "--automation-test"] + ([str(project)] if project else [])
        self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log,
            env={**os.environ, "RESONANCE_AUTOMATION_TEST_DIRECTORY": str(self.directory)})
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            assert self.process.poll() is None, "Packaged app exited before its private API was ready"
            found = [entry for entry in endpoints(self.directory) if entry["pid"] == self.process.pid]
            if found:
                assert len(found) == 1
                self.endpoint = found[0]
                return Client(self.endpoint["socket"], timeout=180)
            time.sleep(.05)
        raise AssertionError("Packaged app did not publish its private API")

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill(); self.process.wait(timeout=5)
        self.process = None
        if self.endpoint:
            shutil.rmtree(Path(self.endpoint["socket"]).parent, ignore_errors=True)
            self.endpoint = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    build = Path(os.environ.get("SCREAMSEQ_BUILD_DIR", os.environ.get("RESONANCE_BUILD_DIR", ROOT / "bin/mac-scratch")))
    parser.add_argument("--app", type=Path, default=build / "ScreamSeq.app", help="Explicit separately identified QA bundle")
    parser.add_argument("--pid", type=int, help="Existing owned QA process; never an ambient musician session")
    parser.add_argument("--socket", help="Explicit socket for that owned --pid")
    parser.add_argument("--input-device", choices=["BlackHole 2ch"], help="Opt in to real loopback input; permission must already be authorized")
    parser.add_argument("--playback-during-capture", action="store_true", help="Assert the owned QA app output was explicitly configured to BlackHole; start its transport during capture")
    parser.add_argument("--artifacts", type=Path, help="Retain evidence in a new directory (default: temporary directory)")
    args = parser.parse_args()
    if args.socket and not args.pid:
        parser.error("--socket requires --pid to verify the owned QA process")
    if args.input_device and not args.pid:
        parser.error("--input-device requires an explicitly owned --pid with prior permission and output setup")
    if args.playback_during_capture and not args.input_device:
        parser.error("--playback-during-capture requires --input-device 'BlackHole 2ch'")
    bundle = args.app.resolve()
    info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
    assert info["CFBundleIdentifier"] not in ("org.resonance.tracker", "org.screamseq.tracker"), "Use a separately identified QA bundle"
    temporary = tempfile.TemporaryDirectory(prefix="screamseq-sampling-") if not args.artifacts else None
    directory = Path(temporary.name) if temporary else args.artifacts.resolve()
    if not temporary:
        directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    host = PrivateApp(bundle, directory)
    report = {"application": str(bundle), "bundleID": info["CFBundleIdentifier"],
              "executableSHA256": hashlib.sha256(host.executable.read_bytes()).hexdigest(),
              "microphoneRequested": bool(args.input_device), "passed": False}
    try:
        if args.pid:
            command = subprocess.check_output(["ps", "-p", str(args.pid), "-o", "args="], text=True).strip()
            assert str(host.executable) in command and ("--inspection" in command or "--automation-test" in command), "PID is not this explicitly owned QA app"
            found = [entry for entry in endpoints() if entry["pid"] == args.pid]
            assert len(found) == 1, "The owned QA app must publish one normal discovery endpoint"
            assert not args.socket or Path(args.socket) == Path(found[0]["socket"]), "Socket does not belong to the verified QA PID"
            client = Client(socket_path=found[0]["socket"], timeout=180)
            report["pid"] = args.pid
        else:
            client = host.launch(); report["pid"] = host.process.pid
        assert not client.call("transport.get")["data"]["playing"], "Use a stopped disposable document"
        report["inputCatalogue"] = recording_reads(client)
        selection = selection_render(client, directory)
        report["selection"] = {"sample": selection["sample"], "instrument": selection["instrument"],
                               "pcmSHA256": hashlib.sha256(selection["pcm"][1]).hexdigest(), **selection["pcm"][0]}
        if args.input_device:
            captured = capture_loopback(client, directory, args.input_device, selection["pattern"], args.playback_during_capture)
            report["capture"] = {"sample": captured["sample"], "instrument": captured["instrument"],
                                 "pcmSHA256": hashlib.sha256(captured["pcm"][1]).hexdigest(), **captured["pcm"][0]}
            checks = [selection, captured]
        else:
            report["capture"] = "NOT RUN: no input device requested"
            checks = [selection]
        # Reopen saved copies in our own silent process even when the original
        # functional run used an operator-owned QA window. Never close that PID.
        host.stop()
        for checked in checks:
            restored = host.launch(checked["project"])
            assert pcm(restored, checked["sample"]) == checked["pcm"], "Native save/reopen preserves exact captured/rendered PCM"
            assert restored.call("instrument.get", {"instrument": checked["instrument"]})["data"] == checked["instrumentState"]
            assert not restored.call("sample.recording.get")["data"]["take"], "Committed takes do not restart a microphone on reopen"
            host.stop()
        report["passed"] = True
        print("PASS packaged sampling socket and native reopen; no system audio defaults changed", flush=True)
    except Exception as error:
        report["error"] = str(error)
        host.log.flush(); host.log.seek(0)
        print(host.log.read()[-12000:], file=sys.stderr)
        raise
    finally:
        host.stop(); host.log.close()
        (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    main()
