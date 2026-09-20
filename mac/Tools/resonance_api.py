#!/usr/bin/env python3
"""ScreamSeq's dependency-free local API client and agent-friendly command line.

This speaks one JSON-RPC 2.0 request per Unix-domain connection. It never opens
the app, implicitly starts playback, changes the system audio route, or executes arbitrary code in it.
"""
import argparse
import json
import math
import os
from pathlib import Path
import socket
import stat
import sys
import time
import uuid

MAX_BYTES = 32 * 1024 * 1024
DISCOVERY = Path.home() / "Library/Application Support/Resonance/Automation"


class APIError(RuntimeError):
    def __init__(self, error):
        self.code = error.get("code", -32003)
        self.data = error.get("data", {})
        super().__init__(error.get("message", "API error"))


def endpoints(directory=DISCOVERY):
    result = []
    for path in sorted(directory.glob("*.json")):
        try:
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
                continue
            endpoint = json.loads(path.read_text())
            sock = Path(endpoint["socket"]).lstat()
            if not stat.S_ISSOCK(sock.st_mode) or sock.st_uid != os.getuid() or sock.st_mode & 0o077:
                continue
            os.kill(endpoint["pid"], 0)
            result.append(endpoint)
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return result


class Client:
    def __init__(self, socket_path=None, pid=None, timeout=120):
        if socket_path is None:
            candidates = [e for e in endpoints() if pid is None or e["pid"] == pid]
            if len(candidates) != 1:
                raise RuntimeError("Enable Automation > Enable Local API in ScreamSeq. If several instances are enabled, specify --pid or --socket.")
            socket_path = candidates[0]["socket"]
        self.socket_path = str(socket_path)
        self.timeout = timeout

    def call(self, method, params=None, request_id=None):
        request = {"jsonrpc": "2.0", "id": request_id or str(uuid.uuid4()), "method": method, "params": params or {}}
        encoded = json.dumps(request, separators=(",", ":"), allow_nan=False).encode() + b"\n"
        if len(encoded) > MAX_BYTES:
            raise ValueError("Request exceeds 32 MiB")
        for attempt in range(6):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(self.timeout)
                connection.connect(self.socket_path)
                connection.sendall(encoded)
                received = bytearray()
                while not received.endswith(b"\n"):
                    part = connection.recv(65536)
                    if not part:
                        raise RuntimeError("Connection ended before a reply. Mutation outcome may be unknown; check the document revision before retrying.")
                    received.extend(part)
                    if len(received) > MAX_BYTES:
                        raise RuntimeError("Response exceeds 32 MiB")
            response = json.loads(received)
            if response.get("jsonrpc") != "2.0" or response.get("id") != request["id"]:
                raise RuntimeError("Mismatched API response")
            if "error" not in response:
                return response["result"]
            error = APIError(response["error"])
            if error.code != -32002 or attempt == 5:
                raise error
            time.sleep(min(0.05 * 2**attempt, 0.5))
        raise AssertionError("unreachable")


def note_value(text):
    if isinstance(text, int) or text.isdecimal():
        return int(text)
    pitches = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6,
               "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    normalized = text.upper().replace("-", "")
    if len(normalized) < 2 or normalized[:-1] not in pitches or not normalized[-1].isdigit():
        raise ValueError("Use a tracker note such as C-4 or C#4, or its numeric value")
    return 1 + pitches[normalized[:-1]] + 12 * int(normalized[-1])


def drum_roll(client, instrument=2, every=2, start_volume=4, end_volume=64, note="C-4", apply=False):
    """Prepare a partial-field batch from the cursor through the last eligible row.

    Volumes form a geometric progression over hit positions; intervening cells
    and existing effect columns are preserved. One applied batch = one undo step.
    """
    if every < 1 or not (1 <= start_volume <= end_volume <= 64):
        raise ValueError("Use every >= 1 and 1 <= start-volume <= end-volume <= 64")
    context = client.call("context.get")
    cursor = context["data"]
    pattern = client.call("pattern.get", {"pattern": cursor["pattern"], "startRow": cursor["row"],
        "rowCount": 1, "startChannel": cursor["channel"], "channelCount": 1})
    if pattern["revision"] != context["revision"]:
        raise RuntimeError("Song changed while reading context; prepare the roll again")
    rows = list(range(cursor["row"], pattern["data"]["rows"], every))
    cells = []
    for i, row in enumerate(rows):
        t = i / max(1, len(rows) - 1)
        volume = math.floor(start_volume * (end_volume / start_volume) ** t + 0.5)
        cells.append({"pattern": cursor["pattern"], "row": row, "channel": cursor["channel"],
            "note": note_value(note), "instrument": instrument, "volumeCommand": 1, "volume": volume})
    return client.call("pattern.apply", {"expectedRevision": context["revision"], "cells": cells, "dryRun": not apply})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", help="Explicit Unix socket; useful with more than one app instance")
    parser.add_argument("--pid", type=int, help="Select a discovered app process")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("endpoints", help="List enabled application instances")
    commands.add_parser("schema", help="Print the bundled machine-readable method schema")
    call = commands.add_parser("call", help="Call a method with JSON params (use - to read stdin)")
    call.add_argument("method")
    call.add_argument("params", nargs="?", default="{}")
    call.add_argument("--request-id", help="Reuse only when retrying exactly the same request")
    roll = commands.add_parser("drum-roll", help="Preview a crescendo roll from the current cursor")
    roll.add_argument("--instrument", type=int, default=2)
    roll.add_argument("--every", type=int, default=2)
    roll.add_argument("--start-volume", type=int, default=4)
    roll.add_argument("--end-volume", type=int, default=64)
    roll.add_argument("--note", default="C-4")
    roll.add_argument("--apply", action="store_true", help="Commit the roll as one undo step; default is a dry run")
    args = parser.parse_args()
    try:
        if args.command == "endpoints":
            result = endpoints()
        elif args.command == "schema":
            result = json.loads(Path(__file__).with_name("resonance-api.schema.json").read_text())
        else:
            client = Client(args.socket, args.pid)
            if args.command == "call":
                params = json.loads(sys.stdin.read() if args.params == "-" else args.params)
                if not isinstance(params, dict):
                    raise ValueError("Params must be an object")
                result = client.call(args.method, params, args.request_id)
            else:
                result = drum_roll(client, args.instrument, args.every, args.start_volume, args.end_volume, args.note, args.apply)
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(json.dumps({"error": str(error), "code": getattr(error, "code", None), "data": getattr(error, "data", {})}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
