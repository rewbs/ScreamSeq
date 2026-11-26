"""Pure-Python Standard MIDI File (SMF) parser & audio-aligned guide for Suno MIDI packs.

Reads a `.mid` file, a directory of `.mid` files, or a `.zip` archive (such as
Suno Studio's `... MIDI.zip`), extracts nominal tempo + per-stem note timelines,
filters cross-stem bleed tracks (e.g. ch=9 drum notes inside Bass.mid), and
computes an affine audio-to-MIDI time alignment (`t_audio = scale * t_midi + offset`)
to compensate for Suno MP3/MIDI offset and slight linear stretch.
"""

from __future__ import annotations

import re
import struct
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

from .audio import DEFAULT_SR, onset_envelope, to_mono


@dataclass
class MidiNote:
    midi_time_sec: float
    audio_time_sec: float
    duration_sec: float
    beat_float: float
    pitch: int
    velocity: int
    channel: int
    track_name: str


@dataclass
class StemMidiTrack:
    stem_label: str
    midi_path: str
    notes: List[MidiNote] = field(default_factory=list)


@dataclass
class MidiGuide:
    nominal_bpm: float
    tempo_median_bpm: float
    tempo_std_bpm: float
    time_scale: float = 1.0
    time_offset_sec: float = 0.0
    alignment_corr: float = 0.0
    stems: Dict[str, StemMidiTrack] = field(default_factory=dict)

    def match_stem(self, stem_name: str, role: str) -> Optional[StemMidiTrack]:
        """Match an audio stem filename (e.g. '2 Drums', '0 Lead Vocals') to its MIDI track."""
        norm = _normalize_stem_key(stem_name)
        if norm in self.stems:
            return self.stems[norm]
        # Substring / token match (e.g. 'lead vocals' -> 'vocals')
        for key, track in self.stems.items():
            if key and (key in norm or norm in key):
                return track
        # Fallback by role if unambiguous
        if role in self.stems:
            return self.stems[role]
        return None


def _normalize_stem_key(name: str) -> str:
    # Strip leading track numbers like "0 ", "2 ", parentheses like "Hermes Agent (Drums)"
    m = re.search(r"\(([^)]+)\)", name)
    core = m.group(1) if m else name
    core = re.sub(r"^\d+\s*[-_.]?\s*", "", core).strip().lower()
    core = re.sub(r"\s+", " ", core)
    return core


def _read_vlq(data: bytes, pos: int) -> Tuple[int, int]:
    val = 0
    while True:
        b = data[pos]
        pos += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            return val, pos


def _parse_single_smf(path: Path) -> Tuple[int, List[Tuple[int, int]], List[Tuple[int, int, int, int, int, str]]]:
    """Parse SMF returning `(tpq, tempos[(tick, us_per_q)], notes[(on_tick, off_tick, ch, pitch, vel, track_name)])`."""
    data = path.read_bytes()
    if len(data) < 14 or data[:4] != b"MThd":
        raise ValueError(f"Not a valid MIDI file: {path}")
    _, hlen = struct.unpack(">4sI", data[:8])
    _, ntrks, division = struct.unpack(">HHH", data[8:14])
    tpq = division if not (division & 0x8000) else 480
    pos = 8 + hlen

    tempos: List[Tuple[int, int]] = []
    notes: List[Tuple[int, int, int, int, int, str]] = []

    for ti in range(ntrks):
        if pos + 8 > len(data):
            break
        cid, clen = struct.unpack(">4sI", data[pos : pos + 8])
        pos += 8
        end = min(len(data), pos + clen)
        if cid != b"MTrk":
            pos = end
            continue
        t = 0
        running = 0
        tname = ""
        active: Dict[Tuple[int, int], Tuple[int, int]] = {}  # (ch, pitch) -> (on_tick, vel)

        while pos < end:
            delta, pos = _read_vlq(data, pos)
            t += delta
            if pos >= end:
                break
            b = data[pos]
            if b & 0x80:
                status = b
                pos += 1
                if status < 0xF0:
                    running = status
            else:
                status = running

            if status == 0xFF:
                if pos >= end:
                    break
                mtype = data[pos]
                pos += 1
                mlen, pos = _read_vlq(data, pos)
                mdata = data[pos : pos + mlen]
                pos += mlen
                if mtype == 0x03:
                    tname = mdata.decode("utf-8", errors="replace").strip()
                elif mtype == 0x51 and len(mdata) == 3 and ti == 0:
                    us = (mdata[0] << 16) | (mdata[1] << 8) | mdata[2]
                    if us > 0:
                        tempos.append((t, us))
            elif status in (0xF0, 0xF7):
                mlen, pos = _read_vlq(data, pos)
                pos += mlen
            else:
                cmd = status & 0xF0
                ch = status & 0x0F
                if cmd in (0xC0, 0xD0):
                    pos += 1
                elif cmd in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    if pos + 2 > end:
                        break
                    d1, d2 = data[pos], data[pos + 1]
                    pos += 2
                    if cmd == 0x90 and d2 > 0:
                        prev = active.get((ch, d1))
                        if prev is not None:
                            notes.append((prev[0], max(prev[0] + 1, t), ch, d1, prev[1], tname))
                        active[(ch, d1)] = (t, d2)
                    elif cmd == 0x80 or (cmd == 0x90 and d2 == 0):
                        prev = active.pop((ch, d1), None)
                        if prev is not None:
                            notes.append((prev[0], max(prev[0] + 1, t), ch, d1, prev[1], tname))
        # Close any hanging notes
        for (ch, pitch), (on_t, vel) in active.items():
            notes.append((on_t, on_t + tpq // 2, ch, pitch, vel, tname))
        pos = end

    if not tempos:
        tempos = [(0, 500000)]
    tempos.sort(key=lambda x: x[0])
    return tpq, tempos, notes


def _tick_to_sec_fn(tpq: int, tempos: List[Tuple[int, int]]):
    # Precompute cumulative seconds at each tempo breakpoint
    breaks_t = [tempos[0][0]]
    breaks_s = [0.0]
    breaks_us = [tempos[0][1]]
    for i in range(1, len(tempos)):
        t_prev, us_prev = tempos[i - 1]
        t_cur, us_cur = tempos[i]
        dt = max(0, t_cur - t_prev)
        breaks_t.append(t_cur)
        breaks_s.append(breaks_s[-1] + (dt / tpq) * (us_prev / 1e6))
        breaks_us.append(us_cur)

    arr_t = np.asarray(breaks_t, dtype=np.int64)

    def convert(tick: int) -> float:
        idx = int(np.searchsorted(arr_t, tick, side="right")) - 1
        idx = max(0, min(idx, len(breaks_t) - 1))
        dt = tick - breaks_t[idx]
        return breaks_s[idx] + (dt / tpq) * (breaks_us[idx] / 1e6)

    return convert


def _filter_notes_for_stem(stem_key: str, raw_notes: List[Tuple[int, int, int, int, int, str]]) -> List[Tuple[int, int, int, int, int, str]]:
    """Drop obvious Suno cross-stem MIDI bleed tracks."""
    is_drum_stem = any(k in stem_key for k in ("drum", "perc", "kick", "snare", "hat", "beat"))
    if is_drum_stem:
        ch9 = [n for n in raw_notes if n[2] == 9]
        return ch9 if ch9 else raw_notes

    # Non-drum stems: drop ch=9 drum bleed tracks
    non_drum = [n for n in raw_notes if n[2] != 9]
    if "vocal" in stem_key:
        # Suno sometimes leaks Electric Guitar into Vocals.mid
        vox_only = [n for n in non_drum if "guitar" not in n[5].lower()]
        if vox_only:
            return vox_only
    return non_drum


def load_midi_guide(
    midi_source: str | Path,
    stems: Dict[str, Tuple[np.ndarray, str, Path]] | None = None,
    sr: int = DEFAULT_SR,
    extract_dir: Path | None = None,
) -> MidiGuide:
    """Load Suno MIDI file/folder/zip and optionally align its clock to the audio stems."""
    p = Path(midi_source)
    midi_files: List[Path] = []
    if p.is_file() and p.suffix.lower() == ".zip":
        target = (extract_dir or p.parent) / f"{p.stem}_midi"
        target.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(p) as zf:
            zf.extractall(target)
        midi_files = sorted(target.rglob("*.mid")) + sorted(target.rglob("*.midi"))
    elif p.is_file():
        midi_files = [p]
    elif p.is_dir():
        midi_files = sorted(p.rglob("*.mid")) + sorted(p.rglob("*.midi"))
    else:
        raise FileNotFoundError(f"MIDI source not found: {p}")

    if not midi_files:
        raise ValueError(f"No .mid files found in {p}")

    all_bpms: List[float] = []
    stem_tracks: Dict[str, StemMidiTrack] = {}

    for mf in midi_files:
        tpq, tempos, raw_notes = _parse_single_smf(mf)
        # Skip initial tick-0 lead-in tempo when estimating song BPM
        body_tempos = tempos[1:] if len(tempos) > 2 else tempos
        bpms = [60e6 / us for _, us in body_tempos if us > 0]
        if bpms:
            all_bpms.extend(bpms)

        t2s = _tick_to_sec_fn(tpq, tempos)
        key = _normalize_stem_key(mf.stem)
        filtered = _filter_notes_for_stem(key, raw_notes)
        parsed_notes: List[MidiNote] = []
        for on_t, off_t, ch, pitch, vel, tname in filtered:
            t_on = t2s(on_t)
            t_off = t2s(off_t)
            parsed_notes.append(
                MidiNote(
                    midi_time_sec=t_on,
                    audio_time_sec=t_on,
                    duration_sec=max(0.02, t_off - t_on),
                    beat_float=on_t / float(tpq),
                    pitch=pitch,
                    velocity=vel,
                    channel=ch,
                    track_name=tname,
                )
            )
        parsed_notes.sort(key=lambda n: (n.midi_time_sec, n.pitch))
        stem_tracks[key] = StemMidiTrack(stem_label=key, midi_path=str(mf), notes=parsed_notes)

    med_bpm = float(np.median(all_bpms)) if all_bpms else 120.0
    std_bpm = float(np.std(all_bpms)) if all_bpms else 0.0
    # Nominal integer BPM if within 0.35 BPM of integer
    nom_bpm = float(round(med_bpm)) if abs(med_bpm - round(med_bpm)) <= 0.35 else round(med_bpm, 3)

    guide = MidiGuide(
        nominal_bpm=nom_bpm,
        tempo_median_bpm=round(med_bpm, 3),
        tempo_std_bpm=round(std_bpm, 3),
        stems=stem_tracks,
    )

    if stems:
        _align_guide_to_audio(guide, stems, sr=sr)

    return guide


def _align_guide_to_audio(
    guide: MidiGuide,
    stems: Dict[str, Tuple[np.ndarray, str, Path]],
    sr: int = DEFAULT_SR,
) -> None:
    """Fit `t_audio = scale * t_midi + offset` via windowed onset cross-correlation."""
    # Prefer drums stem for crispest transient alignment
    ref_audio = None
    ref_notes: List[MidiNote] = []
    for sname, (audio, role, _) in stems.items():
        if role == "drums":
            tr = guide.match_stem(sname, role)
            if tr and len(tr.notes) >= 20:
                ref_audio = audio
                ref_notes = tr.notes
                break
    if ref_audio is None:
        # Fallback to any stem with matching MIDI notes
        for sname, (audio, role, _) in stems.items():
            tr = guide.match_stem(sname, role)
            if tr and len(tr.notes) >= 20:
                ref_audio = audio
                ref_notes = tr.notes
                break
    if ref_audio is None:
        return

    hop = 256
    fps = sr / float(hop)
    env = onset_envelope(to_mono(ref_audio), sr=sr, n_fft=1024, hop=hop)
    midi_env = np.zeros_like(env)
    for n in ref_notes:
        f = int(round(n.midi_time_sec * fps))
        if 0 <= f < midi_env.size:
            midi_env[f] += n.velocity / 127.0

    # Measure local lag across overlapping 20s windows to detect both offset AND linear stretch
    dur_sec = ref_audio.shape[0] / float(sr)
    win_sec = min(20.0, max(6.0, dur_sec / 4.0))
    beat_sec = 60.0 / max(40.0, guide.nominal_bpm)
    max_lag_frames = min(env.size // 4, int(round(0.45 * fps)))

    centers: List[float] = []
    lags_sec: List[float] = []
    weights: List[float] = []

    t0 = 0.0
    while t0 + 0.6 * win_sec < dur_sec:
        f0 = int(t0 * fps)
        f1 = min(env.size, int((t0 + win_sec) * fps))
        e1 = env[f0:f1]
        e2 = midi_env[f0:f1]
        if float(e2.sum()) > 1.5 and float(e1.sum()) > 1.0:
            best_lag = 0
            best_val = -1.0
            ml = min(max_lag_frames, e1.size // 3)
            for lag in range(-ml, ml + 1):
                if lag >= 0:
                    c = float(np.dot(e1[lag:], e2[: e1.size - lag]))
                else:
                    c = float(np.dot(e1[: e1.size + lag], e2[-lag:]))
                if c > best_val:
                    best_val = c
                    best_lag = lag
            if best_val > 2.0:
                centers.append(t0 + 0.5 * win_sec)
                lags_sec.append(best_lag / fps)
                weights.append(best_val)
        t0 += 0.75 * win_sec

    if not centers:
        return

    # Unwrap any single-beat wrap jumps relative to median lag
    lags_arr = np.asarray(lags_sec, dtype=np.float64)
    med_lag = float(np.median(lags_arr))
    for i in range(lags_arr.size):
        diff = lags_arr[i] - med_lag
        if abs(diff) > 0.5 * beat_sec:
            shift = round(diff / beat_sec) * beat_sec
            lags_arr[i] -= shift

    # Weighted linear fit: lag(t) = a * t + b  =>  t_audio = (1 + a) * t_midi + b
    x = np.asarray(centers, dtype=np.float64)
    w = np.asarray(weights, dtype=np.float64)
    if x.size >= 3 and (x.max() - x.min()) > 15.0:
        # Robust fit: drop points > 45ms from initial weighted line
        coef = np.polyfit(x, lags_arr, 1, w=np.sqrt(w))
        resid = np.abs(lags_arr - np.polyval(coef, x))
        inliers = resid < 0.045
        if int(np.sum(inliers)) >= 2:
            coef = np.polyfit(x[inliers], lags_arr[inliers], 1, w=np.sqrt(w[inliers]))
        slope, intercept = float(coef[0]), float(coef[1])
        # Sanity bound slope to +/- 1.5%
        if abs(slope) > 0.015:
            slope, intercept = 0.0, float(np.average(lags_arr, weights=w))
    else:
        slope, intercept = 0.0, float(np.average(lags_arr, weights=w))

    guide.time_scale = round(1.0 + slope, 6)
    guide.time_offset_sec = round(intercept, 5)
    guide.alignment_corr = round(float(np.mean(weights)), 2)

    # Apply aligned timestamps to every note in every stem track
    for tr in guide.stems.values():
        for n in tr.notes:
            n.audio_time_sec = max(0.0, guide.time_scale * n.midi_time_sec + guide.time_offset_sec)
            n.duration_sec = max(0.02, n.duration_sec * guide.time_scale)
