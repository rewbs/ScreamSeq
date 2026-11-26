#!/usr/bin/env python3
"""
Local usage & friction analytics aggregator for ScreamSeq.

Reads JSONL v1 logs written by ScreamSeq (Windows and/or macOS) and prints:
  1. Top 5 Friction Hotspots (ranked by weighted impact & frequency)
  2. General Usage Overview (sessions, commands, panels, layouts, transitions, transport)

Zero third-party dependencies (Python 3 stdlib only).
"""

from __future__ import annotations

import argparse
import collections
import datetime
import json
import os
import pathlib
import sys
from typing import Any, Dict, Iterable, List, Optional, Tuple


FRICTION_WEIGHTS: Dict[str, float] = {
    "bookmark": 3.0,
    "draft_held": 2.5,
    "rejected_edit": 2.0,
    "undo_burst": 2.0,
    "panel_bounce": 1.8,
    "palette_zero_result": 1.6,
    "palette_abandoned": 1.2,
    "deep_menu": 1.0,
}


def default_log_paths() -> List[pathlib.Path]:
    env = os.environ.get("SCREAMSEQ_ANALYTICS_LOG", "").strip()
    if env:
        return [pathlib.Path(env)]
    candidates: List[pathlib.Path] = []
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA", "")
        if local:
            candidates.append(pathlib.Path(local) / "org.resonance.tracker" / "usage-analytics.jsonl")
    elif sys.platform == "darwin":
        candidates.append(pathlib.Path.home() / "Library" / "Application Support" / "Resonance" / "usage-analytics.jsonl")
    else:
        xdg = os.environ.get("XDG_DATA_HOME", str(pathlib.Path.home() / ".local" / "share"))
        candidates.append(pathlib.Path(xdg) / "ScreamSeq" / "usage-analytics.jsonl")
    return candidates


def parse_iso8601(ts: str) -> Optional[datetime.datetime]:
    if not ts:
        return None
    try:
        if ts.endswith("Z"):
            ts = ts[:-1] + "+00:00"
        return datetime.datetime.fromisoformat(ts)
    except ValueError:
        return None


def load_events(paths: Iterable[pathlib.Path], since_days: Optional[float]) -> Tuple[List[Dict[str, Any]], int]:
    cutoff: Optional[datetime.datetime] = None
    if since_days is not None and since_days > 0:
        cutoff = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=since_days)

    events: List[Dict[str, Any]] = []
    malformed = 0
    for path in paths:
        if not path.exists():
            continue
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = raw.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    malformed += 1
                    continue
                if not isinstance(obj, dict) or obj.get("v") != 1:
                    malformed += 1
                    continue
                if cutoff is not None:
                    dt = parse_iso8601(str(obj.get("ts", "")))
                    if dt is not None:
                        if dt.tzinfo is None:
                            dt = dt.replace(tzinfo=datetime.timezone.utc)
                        if dt < cutoff:
                            continue
                events.append(obj)
    return events, malformed


def hotspot_key_and_label(ev: Dict[str, Any]) -> Tuple[str, str]:
    kind = str(ev.get("event", "unknown"))
    panel = str(ev.get("panel", "unknown"))
    detail = ev.get("detail") if isinstance(ev.get("detail"), dict) else {}

    if kind == "bookmark":
        return f"bookmark:{panel}", f"Explicit bookmark in [{panel}]"
    if kind in ("draft_held", "rejected_edit"):
        ctx = str(detail.get("context") or panel)
        reason = str(detail.get("reason", ""))[:60]
        tag = "Draft held" if kind == "draft_held" else "Rejected edit"
        return f"{kind}:{ctx}:{reason.lower()}", f"{tag} in [{ctx}]: {reason}"
    if kind == "panel_bounce":
        a = str(detail.get("panelA", panel))
        b = str(detail.get("panelB", "?"))
        pair = " <-> ".join(sorted([a, b]))
        return f"panel_bounce:{pair}", f"Panel bounce loop ({pair})"
    if kind == "undo_burst":
        return f"undo_burst:{panel}", f"Rapid Undo burst (3+ in 5s) in [{panel}]"
    if kind == "palette_zero_result":
        q = str(detail.get("query", "")).lower()
        return f"palette_zero:{q}", f"Command Palette 0 results for '{q}'"
    if kind == "palette_abandoned":
        q = str(detail.get("query", "")).lower()
        return f"palette_abandoned:{q}", f"Abandoned Command Palette query '{q}'"
    if kind == "deep_menu":
        path = str(detail.get("menuPath") or detail.get("command") or panel)
        return f"deep_menu:{path.lower()}", f"Deep menu / More... action: {path}"
    return f"{kind}:{panel}", f"{kind} in [{panel}]"


def analyze(events: List[Dict[str, Any]], top_n: int = 5) -> Dict[str, Any]:
    bookmark_notes: Dict[str, str] = {}
    for ev in events:
        if ev.get("event") == "bookmark_note":
            d = ev.get("detail") if isinstance(ev.get("detail"), dict) else {}
            bid = str(d.get("bookmarkId", ""))
            note = str(d.get("note", "")).strip()
            if bid and note:
                bookmark_notes[bid] = note

    sessions: set[str] = set()
    platforms: collections.Counter[str] = collections.Counter()
    commands: collections.Counter[str] = collections.Counter()
    panels: collections.Counter[str] = collections.Counter()
    layouts: collections.Counter[str] = collections.Counter()
    transitions: collections.Counter[str] = collections.Counter()
    transport: collections.Counter[str] = collections.Counter()
    doc_actions: collections.Counter[str] = collections.Counter()
    friction_by_type: collections.Counter[str] = collections.Counter()
    undo_count = 0
    total_commands = 0
    total_session_ms = 0

    hotspot_buckets: Dict[str, Dict[str, Any]] = {}

    for ev in events:
        sid = str(ev.get("session", ""))
        if sid:
            sessions.add(sid)
        plat = str(ev.get("platform", "unknown"))
        platforms[plat] += 1

        category = str(ev.get("category", ""))
        kind = str(ev.get("event", ""))
        panel = str(ev.get("panel", ""))
        layout = str(ev.get("layout", ""))
        detail = ev.get("detail") if isinstance(ev.get("detail"), dict) else {}

        if panel:
            panels[panel] += 1
        if layout:
            layouts[layout] += 1

        if category == "usage":
            if kind == "command":
                total_commands += 1
                label = str(detail.get("label") or detail.get("id") or "unknown")
                commands[label] += 1
                if "undo" in label.lower():
                    undo_count += 1
            elif kind == "panel_focus":
                src = str(detail.get("from", ""))
                dst = str(detail.get("to", ""))
                if src and dst:
                    transitions[f"{src} -> {dst}"] += 1
            elif kind == "transport":
                action = str(detail.get("action", "unknown"))
                transport[action] += 1
            elif kind == "document_action":
                action = str(detail.get("action", "unknown"))
                doc_actions[action] += 1
            elif kind == "app_stop":
                dur = detail.get("durationMs")
                if isinstance(dur, (int, float)) and dur > 0:
                    total_session_ms += int(dur)
        elif category == "friction":
            if kind == "bookmark_note":
                continue
            friction_by_type[kind] += 1
            key, label = hotspot_key_and_label(ev)
            weight = FRICTION_WEIGHTS.get(kind, 1.0)
            bucket = hotspot_buckets.get(key)
            if bucket is None:
                bucket = {
                    "key": key,
                    "label": label,
                    "event": kind,
                    "count": 0,
                    "score": 0.0,
                    "panels": collections.Counter(),
                    "notes": [],
                    "precedingActions": collections.Counter(),
                }
                hotspot_buckets[key] = bucket
            bucket["count"] += 1
            bucket["score"] += weight
            if panel:
                bucket["panels"][panel] += 1
            if kind == "bookmark":
                bid = str(detail.get("bookmarkId", ""))
                note = bookmark_notes.get(bid) or str(detail.get("note", "")).strip()
                if note and len(bucket["notes"]) < 5:
                    bucket["notes"].append(note)
            actions = ev.get("last10Actions")
            if isinstance(actions, list) and actions:
                tail = " -> ".join(str(a) for a in actions[-3:])
                bucket["precedingActions"][tail] += 1

    ranked = sorted(
        hotspot_buckets.values(),
        key=lambda b: (-b["score"], -b["count"], b["label"]),
    )[:top_n]

    top_hotspots = [
        {
            "rank": idx + 1,
            "label": b["label"],
            "event": b["event"],
            "count": b["count"],
            "score": round(b["score"], 2),
            "topPanels": [p for p, _ in b["panels"].most_common(3)],
            "sampleNotes": b["notes"],
            "commonPreceding": [p for p, _ in b["precedingActions"].most_common(2)],
        }
        for idx, b in enumerate(ranked)
    ]

    return {
        "totalEvents": len(events),
        "sessionCount": len(sessions),
        "totalRecordedMinutes": round(total_session_ms / 60000.0, 1),
        "platforms": dict(platforms),
        "topFrictionHotspots": top_hotspots,
        "frictionCounts": dict(friction_by_type.most_common()),
        "usageOverview": {
            "totalCommands": total_commands,
            "undoCount": undo_count,
            "undoRatio": round(undo_count / total_commands, 3) if total_commands else 0.0,
            "topCommands": commands.most_common(10),
            "topPanels": panels.most_common(8),
            "topLayouts": layouts.most_common(6),
            "topPanelTransitions": transitions.most_common(8),
            "transport": dict(transport),
            "documentActions": dict(doc_actions),
        },
    }


def render_text(report: Dict[str, Any], top_n: int) -> str:
    lines: List[str] = []
    lines.append("============================================================")
    lines.append("  ScreamSeq Local Usage & Friction Analytics Summary")
    lines.append("============================================================")
    lines.append(
        f"Events: {report['totalEvents']}  |  Sessions: {report['sessionCount']}  |  "
        f"Platforms: {', '.join(f'{k}={v}' for k, v in report['platforms'].items()) or 'none'}"
    )
    if report["totalRecordedMinutes"] > 0:
        lines.append(f"Recorded closed-session time: {report['totalRecordedMinutes']} min")
    lines.append("")

    lines.append(f"TOP {top_n} FRICTION HOTSPOTS")
    lines.append("------------------------------------------------------------")
    hotspots = report["topFrictionHotspots"]
    if not hotspots:
        lines.append("  No friction events recorded yet.")
    else:
        for h in hotspots:
            lines.append(f"  #{h['rank']}  [{h['score']:.1f} pts | {h['count']}x]  {h['label']}")
            if h["sampleNotes"]:
                lines.append(f"      Notes: {' ; '.join(h['sampleNotes'])}")
            if h["commonPreceding"]:
                lines.append(f"      Common lead-in: {h['commonPreceding'][0]}")
    if report["frictionCounts"]:
        summary = ", ".join(f"{k}={v}" for k, v in report["frictionCounts"].items())
        lines.append(f"  Breakdown: {summary}")
    lines.append("")

    usage = report["usageOverview"]
    lines.append("GENERAL USAGE OVERVIEW")
    lines.append("------------------------------------------------------------")
    lines.append(
        f"  Commands: {usage['totalCommands']}  |  Undos: {usage['undoCount']} "
        f"({usage['undoRatio'] * 100:.1f}%)"
    )
    if usage["transport"]:
        t = ", ".join(f"{k}={v}" for k, v in usage["transport"].items())
        lines.append(f"  Transport: {t}")
    if usage["documentActions"]:
        d = ", ".join(f"{k}={v}" for k, v in usage["documentActions"].items())
        lines.append(f"  Documents: {d}")
    if usage["topCommands"]:
        lines.append("  Top commands:")
        for cmd, cnt in usage["topCommands"][:8]:
            lines.append(f"    - {cmd}: {cnt}")
    if usage["topPanels"]:
        lines.append("  Most active panels: " + ", ".join(f"{p} ({c})" for p, c in usage["topPanels"][:6]))
    if usage["topLayouts"]:
        lines.append("  Layouts: " + ", ".join(f"{l} ({c})" for l, c in usage["topLayouts"][:5]))
    if usage["topPanelTransitions"]:
        lines.append("  Frequent panel hops:")
        for hop, cnt in usage["topPanelTransitions"][:6]:
            lines.append(f"    - {hop}: {cnt}")
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Analyze ScreamSeq local usage & friction analytics JSONL logs.")
    parser.add_argument("files", nargs="*", type=pathlib.Path, help="JSONL log file(s). Defaults to the platform log path.")
    parser.add_argument("--top", type=int, default=5, help="Number of friction hotspots to report (default: 5).")
    parser.add_argument("--since-days", type=float, default=None, help="Only include events from the last N days.")
    parser.add_argument("--json", action="store_true", dest="as_json", help="Emit structured JSON instead of text.")
    args = parser.parse_args(argv)

    paths = args.files if args.files else default_log_paths()
    events, malformed = load_events(paths, args.since_days)
    report = analyze(events, top_n=max(1, args.top))
    report["sources"] = [str(p) for p in paths]
    report["malformedLines"] = malformed

    if args.as_json:
        print(json.dumps(report, indent=2))
    else:
        print(render_text(report, top_n=max(1, args.top)))
        if malformed:
            print(f"\n(Skipped {malformed} malformed JSONL line(s))", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
