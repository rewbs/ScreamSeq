"""Intermediate `ScratchpadPlan` IR and pattern factorization.

All heuristic decompilers (and future ML/RL models) emit a `ScratchpadPlan`.
Emitters (`.it` writer, live ScreamSeq API bridge) and the automated scorecard
consume this same representation.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
from pathlib import Path
from typing import Dict, List, Optional, Tuple


@dataclass
class SampleZone:
    """One extracted audio sample mapped onto a key range of a multi-sample instrument."""
    sample_id: int
    name: str
    wav_path: str
    root_note: int          # MIDI note (0..119; 60 = C-5 in IT / C-4 in ScreamSeq)
    low_note: int
    high_note: int
    duration_sec: float
    sample_rate: int
    source_time_sec: float  # Timestamp in original stem where exemplar was taken
    cleanliness_score: float = 1.0
    occurrences: int = 1
    default_volume: int = 64  # IT 0..64


@dataclass
class InstrumentPlan:
    instrument_id: int  # 1-based
    name: str
    stem_name: str
    role: str
    tier: str           # "drum_kit" | "vibe_chop" | "pitched_multisample" | "stab_kit"
    nna: str = "cut"    # "cut" | "continue" | "fade"
    zones: List[SampleZone] = field(default_factory=list)


@dataclass
class NoteEvent:
    time_sec: float
    row_float: float          # Exact musical row position (0-based)
    note: int                 # MIDI note 0..119, or 254 (cut), 255 (off)
    velocity: int             # 1..127 (mapped to 1..64 for IT)
    duration_rows: float = 0.0
    column: int = 0           # Polyphony column within the track (assigned during factorization)
    is_cut: bool = False
    cluster_id: int = -1


@dataclass
class TrackPlan:
    track_id: int
    name: str
    stem_name: str
    role: str
    tier: str
    instrument_id: int
    muted: bool = False
    max_polyphony: int = 1
    events: List[NoteEvent] = field(default_factory=list)


@dataclass
class PatternCell:
    row: int
    channel: int              # Global 0-based channel index
    note: int                 # 0..119, 254=cut, 255=off, -1=none
    instrument: int = 0       # 1-based instrument, 0=none
    volume: int = -1          # 0..64, -1=none
    delay_ticks: int = 0      # 0..(speed-1) for IT SDx sub-row timing
    cut_ticks: int = -1       # >=0 for IT SCx sub-row cut
    row_fraction: float = 0.0 # Exact [0, 1) sub-row fraction for ScreamSeq PreciseNote


@dataclass
class PatternData:
    pattern_id: int
    rows: int
    cells: List[PatternCell] = field(default_factory=list)


@dataclass
class ScratchpadPlan:
    version: str
    title: str
    mode: str
    editability: float
    bpm: float
    speed: int
    rows_per_beat: int
    beats_per_bar: int
    pattern_rows: int
    sample_rate: int
    beat_offset_sec: float
    total_duration_sec: float
    grid_diagnostics: dict
    instruments: List[InstrumentPlan] = field(default_factory=list)
    tracks: List[TrackPlan] = field(default_factory=list)
    channel_names: List[str] = field(default_factory=list)
    channel_muted: List[bool] = field(default_factory=list)
    patterns: List[PatternData] = field(default_factory=list)
    order_list: List[int] = field(default_factory=list)
    stem_metrics: dict = field(default_factory=dict)

    def to_dict(self) -> dict:
        return asdict(self)

    def save_json(self, path: str | Path) -> Path:
        out = Path(path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(self.to_dict(), indent=2), encoding="utf-8")
        return out

    @classmethod
    def from_json(cls, path: str | Path) -> "ScratchpadPlan":
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        instruments = [
            InstrumentPlan(
                **{k: v for k, v in ins.items() if k != "zones"},
                zones=[SampleZone(**z) for z in ins.get("zones", [])],
            )
            for ins in data.get("instruments", [])
        ]
        tracks = [
            TrackPlan(
                **{k: v for k, v in tr.items() if k != "events"},
                events=[NoteEvent(**e) for e in tr.get("events", [])],
            )
            for tr in data.get("tracks", [])
        ]
        patterns = [
            PatternData(
                pattern_id=p["pattern_id"],
                rows=p["rows"],
                cells=[PatternCell(**c) for c in p.get("cells", [])],
            )
            for p in data.get("patterns", [])
        ]
        return cls(
            version=data["version"],
            title=data["title"],
            mode=data["mode"],
            editability=data["editability"],
            bpm=data["bpm"],
            speed=data["speed"],
            rows_per_beat=data["rows_per_beat"],
            beats_per_bar=data["beats_per_bar"],
            pattern_rows=data["pattern_rows"],
            sample_rate=data["sample_rate"],
            beat_offset_sec=data["beat_offset_sec"],
            total_duration_sec=data["total_duration_sec"],
            grid_diagnostics=data["grid_diagnostics"],
            instruments=instruments,
            tracks=tracks,
            channel_names=data.get("channel_names", []),
            channel_muted=data.get("channel_muted", []),
            patterns=patterns,
            order_list=data.get("order_list", []),
            stem_metrics=data.get("stem_metrics", {}),
        )


def _quantize_event_position(row_float: float, speed: int) -> Tuple[int, int, float]:
    """Convert continuous row position into `(int_row, delay_ticks, exact_fraction)`."""
    clamped = max(0.0, row_float)
    row_int = int(clamped)
    frac = clamped - row_int
    tick = int(round(frac * speed))
    if tick >= speed:
        row_int += 1
        tick = 0
        frac = 0.0
    return row_int, tick, frac


def factorize_patterns(plan: ScratchpadPlan, max_channels: int = 64) -> ScratchpadPlan:
    """Assign polyphony columns, slice into fixed-length patterns, and deduplicate bars."""
    speed = plan.speed
    pat_rows = plan.pattern_rows

    # 1. Assign columns within each track and global channel offsets
    channel_names: List[str] = []
    channel_muted: List[bool] = []
    track_channel_base: Dict[int, int] = {}

    for tr in plan.tracks:
        tr.events.sort(key=lambda e: (e.row_float, 0 if e.is_cut else 1, e.note))
        # Greedy voice allocation across up to tr.max_polyphony columns
        n_cols = max(1, min(tr.max_polyphony, 8))
        # Track when each column becomes free (in rows) and last note/row used
        col_free_at = [-1e9] * n_cols
        col_last_row = [-1] * n_cols
        col_last_tick = [-1] * n_cols

        used_cols = 1
        for ev in tr.events:
            r_int, tick, _ = _quantize_event_position(ev.row_float, speed)
            chosen = -1
            # Prefer a column that has no event on the exact same integer row
            for c in range(n_cols):
                if col_last_row[c] != r_int and col_free_at[c] <= ev.row_float + 1e-4:
                    chosen = c
                    break
            if chosen < 0:
                for c in range(n_cols):
                    if col_last_row[c] != r_int:
                        chosen = c
                        break
            if chosen < 0:
                # All columns busy on this exact row; pick column whose previous note ended earliest
                chosen = int(min(range(n_cols), key=lambda c: (col_free_at[c], col_last_tick[c])))
            ev.column = chosen
            used_cols = max(used_cols, chosen + 1)
            col_last_row[chosen] = r_int
            col_last_tick[chosen] = tick
            dur = ev.duration_rows if ev.duration_rows > 0 else 0.5
            col_free_at[chosen] = ev.row_float + dur

        # Enforce global channel budget
        remaining = max_channels - len(channel_names)
        if remaining <= 0:
            tr.max_polyphony = 0
            continue
        actual_cols = min(used_cols, remaining)
        for ev in tr.events:
            ev.column = ev.column % actual_cols
        track_channel_base[tr.track_id] = len(channel_names)
        tr.max_polyphony = actual_cols
        for c in range(actual_cols):
            suffix = f"#{c + 1}" if actual_cols > 1 else ""
            channel_names.append(f"{tr.name}{suffix}")
            channel_muted.append(tr.muted)

    plan.channel_names = channel_names
    plan.channel_muted = channel_muted

    # 2. Populate per-chunk cell maps keyed by (row, channel)
    max_row = 0
    for tr in plan.tracks:
        for ev in tr.events:
            r_int, _, _ = _quantize_event_position(ev.row_float, speed)
            max_row = max(max_row, r_int + 1)

    grid_rows = int(plan.grid_diagnostics.get("total_rows", pat_rows))
    num_chunks = max(1, (max(max_row, grid_rows) + pat_rows - 1) // pat_rows)
    chunk_cells: List[Dict[Tuple[int, int], PatternCell]] = [{} for _ in range(num_chunks)]

    for tr in plan.tracks:
        if tr.track_id not in track_channel_base:
            continue
        base_ch = track_channel_base[tr.track_id]
        # Place note-ons first, then explicit cuts where cell is otherwise empty
        cut_requests: List[Tuple[int, int, int]] = []  # (global_row, ch, cut_tick)
        for ev in tr.events:
            r_int, tick, frac = _quantize_event_position(ev.row_float, speed)
            chunk_idx = r_int // pat_rows
            if chunk_idx >= num_chunks:
                continue
            local_row = r_int % pat_rows
            ch = base_ch + ev.column
            if ev.is_cut:
                cut_requests.append((r_int, ch, tick))
                continue
            vol_it = max(1, min(64, int(round(ev.velocity * 64.0 / 127.0))))
            chunk_cells[chunk_idx][(local_row, ch)] = PatternCell(
                row=local_row,
                channel=ch,
                note=max(0, min(119, ev.note)),
                instrument=tr.instrument_id,
                volume=vol_it,
                delay_ticks=tick,
                cut_ticks=-1,
                row_fraction=round(frac, 5),
            )
            # Schedule explicit note-cut for melodic/chop notes with finite duration
            # when another note does not immediately retrigger the same column.
            if ev.duration_rows > 0 and tr.tier in ("pitched_multisample", "vibe_chop", "stab_kit"):
                cut_r, cut_tick, _ = _quantize_event_position(ev.row_float + ev.duration_rows, speed)
                if cut_r > r_int:
                    cut_requests.append((cut_r, ch, cut_tick))

        for g_row, ch, cut_tick in cut_requests:
            chunk_idx = g_row // pat_rows
            if chunk_idx >= num_chunks:
                continue
            local_row = g_row % pat_rows
            key = (local_row, ch)
            if key not in chunk_cells[chunk_idx]:
                chunk_cells[chunk_idx][key] = PatternCell(
                    row=local_row,
                    channel=ch,
                    note=254,  # IT Note Cut ^^^
                    instrument=0,
                    volume=-1,
                    delay_ticks=0,
                    cut_ticks=cut_tick if cut_tick > 0 else -1,
                    row_fraction=cut_tick / float(speed),
                )

    # Trim trailing completely empty patterns (e.g. from sample-rounding overhang)
    while len(chunk_cells) > 1 and not chunk_cells[-1]:
        chunk_cells.pop()

    # 3. Deduplicate identical patterns into compact pattern table + order list
    signature_to_pat_id: Dict[tuple, int] = {}
    patterns: List[PatternData] = []
    order_list: List[int] = []

    for cells_map in chunk_cells:
        sorted_cells = [cells_map[k] for k in sorted(cells_map.keys())]
        sig = tuple(
            (c.row, c.channel, c.note, c.instrument, c.volume, c.delay_ticks, c.cut_ticks)
            for c in sorted_cells
        )
        if sig in signature_to_pat_id:
            order_list.append(signature_to_pat_id[sig])
        else:
            pat_id = len(patterns)
            signature_to_pat_id[sig] = pat_id
            patterns.append(PatternData(pattern_id=pat_id, rows=pat_rows, cells=sorted_cells))
            order_list.append(pat_id)

    plan.patterns = patterns
    plan.order_list = order_list
    return plan
