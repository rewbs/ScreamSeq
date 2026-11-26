"""Optional bridge to push a `ScratchpadPlan` into a live ScreamSeq instance.

Translates `ScratchpadPlan` into ScreamSeq's revision-guarded JSON-RPC API
(using `mac/Tools/screamseq_api.py`), importing multi-sample instruments and
emitting exact 1/65536-row `PreciseNote` events.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List

from .plan import ScratchpadPlan


def _import_client():
    repo_root = Path(__file__).resolve().parents[2]
    tools_dir = repo_root / "mac" / "Tools"
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    import screamseq_api  # type: ignore

    return screamseq_api


def build_api_payload_preview(plan: ScratchpadPlan) -> dict:
    """Build a dry-run summary of the ScreamSeq API calls for `plan`."""
    multisamples = []
    for ins in plan.instruments:
        multisamples.append(
            {
                "name": ins.name,
                "role": ins.role,
                "tier": ins.tier,
                "sources": [
                    {"path": z.wav_path, "rootNote": z.root_note + 1}  # OpenMPT 1-based NOTE_MIN
                    for z in ins.zones
                ],
            }
        )

    precise_by_pattern: Dict[int, List[dict]] = {}
    for pat in plan.patterns:
        events = []
        for c in pat.cells:
            pos = int(round((c.row + c.row_fraction) * 65536))
            note_val = c.note if c.note >= 254 else (c.note + 1)
            vel = max(1, min(127, int(round(c.volume * 127.0 / 64.0)))) if c.volume >= 0 else 127
            events.append(
                {
                    "track": c.channel,
                    "position": pos,
                    "instrument": c.instrument,
                    "note": note_val,
                    "velocity": vel,
                }
            )
        precise_by_pattern[pat.pattern_id] = events

    return {
        "bpm": plan.bpm,
        "rowsPerBeat": plan.rows_per_beat,
        "orderList": plan.order_list,
        "multisamples": multisamples,
        "preciseNoteCount": sum(len(v) for v in precise_by_pattern.values()),
    }


def push_plan_to_screamseq(
    plan: ScratchpadPlan,
    socket_path: str | None = None,
    pid: int | None = None,
    dry_run: bool = True,
) -> dict:
    """Import multi-sample instruments into a running ScreamSeq instance.

    Note: By default `dry_run=True` validates without mutating the musician's
    open document. Pass `dry_run=False` explicitly on a disposable document.
    """
    api = _import_client()
    client = api.Client(socket_path=socket_path, pid=pid)
    snapshot = client.call("song.get")
    rev = snapshot["revision"]
    imported: List[dict] = []

    for ins in plan.instruments:
        if len(ins.zones) >= 2:
            res = client.call(
                "instrument.importMultisample",
                {
                    "expectedRevision": rev,
                    "name": ins.name,
                    "sources": [
                        {"path": z.wav_path, "rootNote": z.root_note + 1}
                        for z in ins.zones
                    ],
                    "dryRun": dry_run,
                },
            )
        elif len(ins.zones) == 1:
            res = client.call(
                "sample.import",
                {
                    "expectedRevision": rev,
                    "paths": [ins.zones[0].wav_path],
                    "instruments": True,
                    "dryRun": dry_run,
                },
            )
        else:
            continue
        if not dry_run and "revision" in res:
            rev = res["revision"]
        imported.append({"instrument": ins.name, "result": res})

    return {
        "dryRun": dry_run,
        "finalRevision": rev,
        "imported": imported,
        "planPreview": build_api_payload_preview(plan),
    }
