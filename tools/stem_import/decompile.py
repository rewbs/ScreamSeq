"""Two-tier progressive stem decompilers (Tier 1 Vibe Lock + Tier 2 Promote).

Transforms raw audio stems into reusable multi-sample `InstrumentPlan`s and
`TrackPlan`s with cross-stem bleed-aware exemplar selection.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

from .audio import (
    DEFAULT_SR,
    apply_micro_fades,
    chroma_from_mag,
    cosine_sim_matrix,
    estimate_pitch_yin,
    hz_to_midi,
    log_mel_spectrogram,
    midi_to_note_name,
    normalize_slice,
    onset_envelope,
    peak,
    rms,
    save_wav,
    snap_to_zero_crossing,
    stft_mag,
    to_mono,
)
from .grid import GridAnalysis
from .midi_guide import MidiGuide, StemMidiTrack
from .plan import InstrumentPlan, NoteEvent, SampleZone, ScratchpadPlan, TrackPlan, factorize_patterns


@dataclass
class DecompileConfig:
    mode: str = "balanced"            # "vibe" | "balanced" | "decomposed"
    editability: float = 0.5          # 0.0 (max vibe/fidelity) .. 1.0 (max compactness/decomposition)
    promote_stems: Tuple[str, ...] = ()
    keep_vibe_backup: bool = False
    max_total_samples: int = 240      # Safe under IT 255-sample limit
    detectseq_json: Optional[str] = None
    chop_beats: float = 0.0           # 0 = auto from editability & role
    midi_guide: Optional[MidiGuide] = None


def _compute_frame_rms(mono: np.ndarray, hop: int = 256) -> np.ndarray:
    n_frames = max(1, (mono.size + hop - 1) // hop)
    padded = np.pad(mono, (0, n_frames * hop - mono.size))
    blocks = padded.reshape(n_frames, hop)
    return np.sqrt(np.mean(blocks * blocks, axis=1, dtype=np.float64)).astype(np.float32)


def _other_stem_energy_map(
    stems: Dict[str, Tuple[np.ndarray, str, Path]],
    hop: int = 256,
) -> Dict[str, np.ndarray]:
    """For each stem, compute per-frame RMS energy of ALL OTHER stems combined."""
    frame_rms: Dict[str, np.ndarray] = {}
    max_frames = 1
    for name, (audio, _, _) in stems.items():
        r = _compute_frame_rms(to_mono(audio), hop=hop)
        frame_rms[name] = r
        max_frames = max(max_frames, r.size)

    other_map: Dict[str, np.ndarray] = {}
    for name in stems:
        acc = np.zeros(max_frames, dtype=np.float32)
        for other_name, r in frame_rms.items():
            if other_name == name:
                continue
            acc[: r.size] += r * r
        other_map[name] = np.sqrt(acc)
    return other_map


def _cleanliness(
    target_rms: np.ndarray,
    other_rms: np.ndarray,
    start_sample: int,
    end_sample: int,
    hop: int = 256,
) -> float:
    f0 = max(0, start_sample // hop)
    f1 = max(f0 + 1, min(target_rms.size, (end_sample + hop - 1) // hop))
    t_e = float(np.mean(target_rms[f0:f1])) if f0 < target_rms.size else 0.0
    o_e = float(np.mean(other_rms[f0 : min(f1, other_rms.size)])) if f0 < other_rms.size else 0.0
    return t_e / (t_e + 1.5 * o_e + 1e-5)


def _pick_peaks(
    env: np.ndarray,
    threshold: float,
    min_gap_frames: int,
) -> List[int]:
    """Local maxima above `threshold` with minimum spacing."""
    if env.size < 3:
        return []
    peaks: List[int] = []
    last = -min_gap_frames * 2
    for i in range(1, env.size - 1):
        v = float(env[i])
        if v >= threshold and v >= float(env[i - 1]) and v >= float(env[i + 1]):
            if i - last >= min_gap_frames:
                peaks.append(i)
                last = i
            elif peaks and v > float(env[peaks[-1]]):
                peaks[-1] = i
                last = i
    return peaks


def _greedy_cluster(
    features: np.ndarray,
    sim_threshold: float,
    max_clusters: int,
) -> Tuple[List[int], List[np.ndarray]]:
    """Deterministic cosine-similarity clustering with hard cap `max_clusters`."""
    n = features.shape[0]
    if n == 0:
        return [], []
    norms = np.linalg.norm(features, axis=1, keepdims=True) + 1e-8
    unit = features / norms

    centroids: List[np.ndarray] = []
    counts: List[int] = []
    labels: List[int] = []

    for i in range(n):
        v = unit[i]
        if not centroids:
            centroids.append(v.copy())
            counts.append(1)
            labels.append(0)
            continue
        c_mat = np.stack(centroids, axis=0)
        sims = c_mat @ v
        best = int(np.argmax(sims))
        if float(sims[best]) >= sim_threshold or len(centroids) >= max_clusters:
            labels.append(best)
            counts[best] += 1
            # Running normalized centroid
            updated = centroids[best] * (counts[best] - 1) + v
            centroids[best] = updated / (np.linalg.norm(updated) + 1e-8)
        else:
            labels.append(len(centroids))
            centroids.append(v.copy())
            counts.append(1)

    return labels, centroids


def _slice_timbre_feature(mono: np.ndarray, sr: int) -> np.ndarray:
    """Compact percussion/stab timbre descriptor (log-mel + centroid + band ratios)."""
    if mono.size < 64:
        return np.zeros(38, dtype=np.float32)
    win = min(mono.size, int(0.18 * sr))
    seg = mono[:win]
    mel = log_mel_spectrogram(seg, sr=sr, n_fft=512, hop=128, n_mels=32)
    mel_mean = mel.mean(axis=1)
    mag = stft_mag(seg, n_fft=512, hop=128)
    freqs = np.fft.rfftfreq(512, 1.0 / sr)
    power = (mag * mag).mean(axis=1)
    tot = float(power.sum()) + 1e-9
    centroid = float((freqs * power).sum() / tot) / (0.5 * sr)
    low = float(power[freqs < 200.0].sum() / tot)
    mid = float(power[(freqs >= 200.0) & (freqs < 2500.0)].sum() / tot)
    high = float(power[freqs >= 2500.0].sum() / tot)
    zcr = float(np.mean(np.abs(np.diff(np.signbit(seg)))))
    decay = float(np.sqrt(np.mean(seg[win // 2 :] ** 2)) / (np.sqrt(np.mean(seg[: max(1, win // 2)] ** 2)) + 1e-6))
    extra = np.array([centroid, low, mid, high, zcr, min(decay, 2.0)], dtype=np.float32)
    return np.concatenate([mel_mean, extra]).astype(np.float32)


def _chop_fingerprint(mono: np.ndarray, sr: int) -> np.ndarray:
    """Harmonic + rhythmic fingerprint for deduplicating bar/beat chops."""
    if mono.size < 256:
        return np.zeros(44, dtype=np.float32)
    mag = stft_mag(mono, n_fft=2048, hop=512)
    chroma = chroma_from_mag(mag, sr=sr, n_fft=2048)
    mel = log_mel_spectrogram(mono, sr=sr, n_fft=1024, hop=512, n_mels=16)
    # Capture both first-half and second-half profiles so reversed/different-groove bars don't collide
    t_c = chroma.shape[1]
    t_m = mel.shape[1]
    c1 = chroma[:, : max(1, t_c // 2)].mean(axis=1)
    c2 = chroma[:, max(0, t_c // 2) :].mean(axis=1)
    m_mean = mel.mean(axis=1)
    # Coarse 4-bin temporal energy contour
    chunks = np.array_split(np.abs(mono), 4)
    contour = np.array([float(np.sqrt(np.mean(c * c))) for c in chunks], dtype=np.float32)
    contour = contour / (np.linalg.norm(contour) + 1e-8)
    # Weight chroma strongly so different chords stay distinct
    return np.concatenate([2.0 * c1, 2.0 * c2, 0.7 * m_mean, 1.2 * contour]).astype(np.float32)


def _load_detectseq_events(json_path: str | Path, sr: int) -> List[Tuple[int, int, float]]:
    """Load `detectseq/1.0` events as `(sample_idx, cluster_id, magnitude)`."""
    data = json.loads(Path(json_path).read_text(encoding="utf-8"))
    events = data.get("events", [])
    out: List[Tuple[int, int, float]] = []
    for ev in events:
        if "sample_idx" in ev:
            s_idx = int(ev["sample_idx"])
        else:
            s_idx = int(round(float(ev["time_sec"]) * sr))
        cid = int(ev.get("cluster_id", ev.get("voice_id", 0)))
        mag = float(ev.get("magnitude", 1.0))
        out.append((max(0, s_idx), cid, mag))
    out.sort(key=lambda x: x[0])
    return out


def _detect_drum_hits_multiband(
    mono: np.ndarray,
    sr: int,
    editability: float,
) -> List[Tuple[int, int]]:
    """Detect polyphonic percussion onsets across low/mid/high bands.

    Returns list of `(sample_idx, band_id)` tuples.
    """
    hop = 256
    min_gap = max(2, int(round(0.038 * sr / hop)))
    # Higher editability -> slightly cleaner threshold (fewer noise ghosts)
    base_thr = 0.14 + 0.08 * editability
    bands = [
        (0, 30.0, 190.0, base_thr * 1.05),       # Kick / sub
        (1, 190.0, 2600.0, base_thr * 1.00),     # Snare / tom / perc body
        (2, 2600.0, 16000.0, base_thr * 0.90),   # Hats / cymbals / clicks
    ]
    hits: List[Tuple[int, int]] = []
    for band_id, fmin, fmax, thr in bands:
        env = onset_envelope(mono, sr=sr, n_fft=1024, hop=hop, fmin=fmin, fmax=fmax)
        pks = _pick_peaks(env, threshold=thr, min_gap_frames=min_gap)
        for p in pks:
            s_idx = p * hop
            # Reject near-silent hits
            win_end = min(mono.size, s_idx + int(0.05 * sr))
            if rms(mono[s_idx:win_end]) > 0.012:
                hits.append((s_idx, band_id))

    hits.sort(key=lambda x: x[0])
    # Merge hits from the same band within 20ms, keep cross-band hits for polyphony
    # unless their broadband waveform is dominated by one single transient (e.g. bright snare triggering mid+high)
    merged: List[Tuple[int, int]] = []
    win_merge = int(0.018 * sr)
    for s_idx, b_id in hits:
        if not merged:
            merged.append((s_idx, b_id))
            continue
        prev_s, prev_b = merged[-1]
        if s_idx - prev_s <= win_merge:
            # Collapse simultaneous same-transient triggers unless low+high (e.g. kick + hat)
            if {prev_b, b_id} == {0, 2}:
                merged.append((s_idx, b_id))
            else:
                # Keep the earlier onset sample index
                continue
        else:
            merged.append((s_idx, b_id))
    return merged


def _snap_to_local_onset(
    env: np.ndarray,
    sample_idx: int,
    sr: int,
    hop: int = 256,
    window_ms: float = 28.0,
) -> int:
    """Snap a MIDI timestamp to the strongest nearby acoustic transient peak."""
    f_center = int(round(sample_idx / float(hop)))
    rad = max(1, int(round(window_ms * sr / (1000.0 * hop))))
    lo = max(0, f_center - rad)
    hi = min(env.size, f_center + rad + 1)
    if hi <= lo:
        return sample_idx
    pk = lo + int(np.argmax(env[lo:hi]))
    if float(env[pk]) > 0.08:
        return pk * hop
    return sample_idx


def _decompile_drums(
    stem_name: str,
    stereo: np.ndarray,
    other_rms: np.ndarray,
    grid: GridAnalysis,
    sr: int,
    samples_dir: Path,
    next_sample_id: int,
    instrument_id: int,
    track_id: int,
    budget_samples: int,
    cfg: DecompileConfig,
    midi_track: Optional[StemMidiTrack] = None,
) -> Tuple[InstrumentPlan, TrackPlan, int, dict]:
    mono = to_mono(stereo)
    target_rms = _compute_frame_rms(mono, hop=256)
    n_total = stereo.shape[0]
    max_voices = max(4, min(budget_samples, int(round(28 - 10 * cfg.editability))))

    used_detectseq = False
    used_midi = False
    if cfg.detectseq_json:
        ds_events = _load_detectseq_events(cfg.detectseq_json, sr=sr)
        if ds_events:
            used_detectseq = True
            hit_starts = [s for s, _, _ in ds_events]
            raw_clusters = [c for _, c, _ in ds_events]
            unique_cids = sorted(set(raw_clusters))
            cid_remap = {c: i for i, c in enumerate(unique_cids[:max_voices])}
            labels = [cid_remap.get(c, 0) for c in raw_clusters]
        else:
            hit_starts, labels = [], []
    elif midi_track is not None and len(midi_track.notes) >= 8:
        # Use audio-aligned Suno MIDI drum hits, snapped to exact acoustic transients,
        # then sub-cluster within each GM drum class if timbres vary!
        used_midi = True
        env = onset_envelope(mono, sr=sr, n_fft=1024, hop=256)
        hit_starts = []
        gm_pitches = []
        for mn in midi_track.notes:
            s_raw = int(round(mn.audio_time_sec * sr))
            if not (0 <= s_raw < n_total - 256):
                continue
            s_snap = _snap_to_local_onset(env, s_raw, sr=sr, hop=256, window_ms=28.0)
            if rms(mono[s_snap : min(n_total, s_snap + int(0.05 * sr))]) > 0.008:
                hit_starts.append(s_snap)
                gm_pitches.append(mn.pitch)
        if hit_starts:
            feats = np.stack(
                [
                    _slice_timbre_feature(mono[s : min(n_total, s + int(0.18 * sr))], sr=sr)
                    for s in hit_starts
                ],
                axis=0,
            )
            # Combine GM drum class identity with acoustic timbre so distinct kicks/snares
            # stay cleanly separated while identical GM hits reuse one canonical sample.
            by_gm: Dict[int, List[int]] = {}
            for idx, p in enumerate(gm_pitches):
                by_gm.setdefault(p, []).append(idx)
            labels = [0] * len(hit_starts)
            next_cid = 0
            per_gm_cap = max(2, max_voices // max(1, len(by_gm)))
            sim_thr = 0.91 - 0.08 * cfg.editability
            for p in sorted(by_gm.keys()):
                idxs = by_gm[p]
                sub_labels, _ = _greedy_cluster(feats[idxs], sim_threshold=sim_thr, max_clusters=per_gm_cap)
                for loc_i, sub_c in zip(idxs, sub_labels):
                    labels[loc_i] = next_cid + sub_c
                next_cid += (max(sub_labels) + 1) if sub_labels else 1
        else:
            labels = []
    else:
        raw_hits = _detect_drum_hits_multiband(mono, sr=sr, editability=cfg.editability)
        # Deduplicate exact timestamps for slicing; preserve simultaneous distinct hits if needed
        hit_starts = [s for s, _ in raw_hits]
        if hit_starts:
            feats = np.stack(
                [
                    _slice_timbre_feature(mono[s : min(n_total, s + int(0.18 * sr))], sr=sr)
                    for s in hit_starts
                ],
                axis=0,
            )
            sim_thr = 0.89 - 0.08 * cfg.editability
            labels, _ = _greedy_cluster(feats, sim_threshold=sim_thr, max_clusters=max_voices)
        else:
            labels = []

    if not hit_starts:
        ins = InstrumentPlan(instrument_id, f"{stem_name}_kit", stem_name, "drums", "drum_kit", nna="fade")
        tr = TrackPlan(track_id, stem_name, stem_name, "drums", "drum_kit", instrument_id, max_polyphony=1)
        return ins, tr, next_sample_id, {"hits": 0, "voices": 0, "used_detectseq": used_detectseq}

    # Compute adaptive slice bounds for each hit (up to next distinct hit or 0.45s decay)
    unique_starts = sorted(set(hit_starts))
    next_onset_map: Dict[int, int] = {}
    for idx, s in enumerate(unique_starts):
        nxt = unique_starts[idx + 1] if idx + 1 < len(unique_starts) else n_total
        next_onset_map[s] = nxt

    # Group occurrences by cluster
    clusters: Dict[int, List[int]] = {}
    for i, cid in enumerate(labels):
        clusters.setdefault(cid, []).append(i)

    # Sort clusters by mean spectral centroid (kick -> snare -> hats) for intuitive keymap
    def _cluster_centroid(indices: List[int]) -> float:
        vals = []
        for i in indices[:8]:
            s = hit_starts[i]
            seg = mono[s : min(n_total, s + int(0.08 * sr))]
            if seg.size >= 64:
                mag = np.abs(np.fft.rfft(seg * np.hanning(seg.size)))
                freqs = np.fft.rfftfreq(seg.size, 1.0 / sr)
                p = mag * mag
                vals.append(float((freqs * p).sum() / (p.sum() + 1e-9)))
        return float(np.median(vals)) if vals else 1000.0

    ordered_cids = sorted(clusters.keys(), key=lambda c: _cluster_centroid(clusters[c]))[:max_voices]
    base_note = 48  # C-4 in IT (mapped 1:1 to C5Speed via NoteMap=60)

    # Count simultaneous other-cluster hits within +/-35ms so we strongly prefer
    # pure solo-hit exemplars (preventing double-stacked transients on polyphonic rows!)
    co_win = int(0.035 * sr)
    co_counts = [0] * len(hit_starts)
    for i, s_i in enumerate(hit_starts):
        c_i = labels[i]
        cnt = 0
        # Walk backwards and forwards in sorted hit_starts
        j = i - 1
        while j >= 0 and s_i - hit_starts[j] <= co_win:
            if labels[j] != c_i:
                cnt += 1
            j -= 1
        j = i + 1
        while j < len(hit_starts) and hit_starts[j] - s_i <= co_win:
            if labels[j] != c_i:
                cnt += 1
            j += 1
        co_counts[i] = cnt

    zones: List[SampleZone] = []
    cid_to_note: Dict[int, int] = {}
    cid_max_peak: Dict[int, float] = {}
    used_exemplar_times: List[int] = []
    sample_id = next_sample_id

    for k, cid in enumerate(ordered_cids):
        occ_indices = clusters[cid]
        # Score each occurrence by solo isolation + cross-stem cleanliness + tail length + energy
        best_i = occ_indices[0]
        best_score = -1.0
        max_pk = 0.0
        for i in occ_indices:
            s = hit_starts[i]
            nxt = next_onset_map[s]
            end = min(n_total, max(s + int(0.05 * sr), min(nxt, s + int(0.45 * sr))))
            pk = peak(stereo[s:end])
            max_pk = max(max_pk, pk)
            clean = _cleanliness(target_rms, other_rms, s, end)
            dur_bonus = min(1.0, (end - s) / (0.25 * sr))
            solo_bonus = 1.0 if co_counts[i] == 0 else (0.22 / co_counts[i])
            # Penalize reusing the exact same timestamp already picked by another voice
            dup_penalty = 0.15 if any(abs(s - ut) <= co_win for ut in used_exemplar_times) else 1.0
            score = clean * solo_bonus * dup_penalty * (0.65 + 0.35 * dur_bonus) * (0.5 + 0.5 * min(1.0, pk / 0.2))
            if score > best_score:
                best_score = score
                best_i = i
        used_exemplar_times.append(hit_starts[best_i])

        ex_s = snap_to_zero_crossing(stereo, max(0, hit_starts[best_i] - int(0.002 * sr)), search_samples=48)
        ex_nxt = next_onset_map[hit_starts[best_i]]
        ex_end = min(n_total, min(ex_nxt, ex_s + int(0.55 * sr)))
        if ex_end - ex_s < int(0.03 * sr):
            ex_end = min(n_total, ex_s + int(0.06 * sr))

        # Trim trailing near-silence below -48 dB of slice peak
        raw_slice = stereo[ex_s:ex_end]
        sl_pk = peak(raw_slice)
        if sl_pk > 1e-5:
            above = np.where(np.max(np.abs(raw_slice), axis=1) > sl_pk * 0.004)[0]
            if above.size:
                trim_end = min(raw_slice.shape[0], int(above[-1]) + int(0.015 * sr))
                raw_slice = raw_slice[: max(int(0.03 * sr), trim_end)]

        norm_slice, _ = normalize_slice(raw_slice, target_peak=0.95)
        norm_slice = apply_micro_fades(norm_slice, sr=sr, fade_in_ms=1.0, fade_out_ms=6.0)

        key_note = min(119, base_note + k)
        wav_name = f"{stem_name}_v{k:02d}_{midi_to_note_name(key_note).replace('#', 's')}.wav"
        wav_path = save_wav(samples_dir / wav_name, norm_slice, sr=sr)

        def_vol = max(4, min(64, int(round((max_pk / 0.95) * 64.0))))
        zones.append(
            SampleZone(
                sample_id=sample_id,
                name=f"{stem_name[:10]}_hit{k:02d}",
                wav_path=str(wav_path),
                root_note=key_note,
                low_note=key_note,
                high_note=key_note,
                duration_sec=round(norm_slice.shape[0] / float(sr), 4),
                sample_rate=sr,
                source_time_sec=round(ex_s / float(sr), 4),
                cleanliness_score=round(best_score, 3),
                occurrences=len(occ_indices),
                default_volume=def_vol,
            )
        )
        cid_to_note[cid] = key_note
        cid_max_peak[cid] = max(max_pk, 1e-4)
        sample_id += 1

    events: List[NoteEvent] = []
    for i, s in enumerate(hit_starts):
        cid = labels[i]
        if cid not in cid_to_note:
            continue
        t_sec = s / float(sr)
        row_f = grid.time_to_row(t_sec)
        if row_f < -0.25:
            continue
        end = min(n_total, s + int(0.08 * sr))
        occ_pk = peak(stereo[s:end])
        vel = max(12, min(127, int(round((occ_pk / cid_max_peak[cid]) * 127.0))))
        events.append(
            NoteEvent(
                time_sec=round(t_sec, 5),
                row_float=round(max(0.0, row_f), 5),
                note=cid_to_note[cid],
                velocity=vel,
                duration_rows=0.0,
                cluster_id=cid,
            )
        )

    ins = InstrumentPlan(
        instrument_id=instrument_id,
        name=f"{stem_name[:18]}_kit",
        stem_name=stem_name,
        role="drums",
        tier="drum_kit",
        nna="fade",
        zones=zones,
    )
    tr = TrackPlan(
        track_id=track_id,
        name=stem_name,
        stem_name=stem_name,
        role="drums",
        tier="drum_kit",
        instrument_id=instrument_id,
        muted=False,
        max_polyphony=4,
        events=events,
    )
    return ins, tr, sample_id, {
        "hits": len(events),
        "voices": len(zones),
        "used_detectseq": used_detectseq,
        "used_midi": used_midi,
    }


def _decompile_smart_chops(
    stem_name: str,
    role: str,
    stereo: np.ndarray,
    other_rms: np.ndarray,
    grid: GridAnalysis,
    sr: int,
    samples_dir: Path,
    next_sample_id: int,
    instrument_id: int,
    track_id: int,
    budget_samples: int,
    cfg: DecompileConfig,
    muted: bool = False,
    suffix: str = "chops",
) -> Tuple[InstrumentPlan, TrackPlan, int, dict]:
    """Tier-1 Vibe Lock: beat/bar-aligned smart chops with harmonic/timbre deduplication."""
    mono = to_mono(stereo)
    target_rms = _compute_frame_rms(mono, hop=256)
    n_total = stereo.shape[0]

    # Choose chop granularity in beats from role, editability & tempo (double-time DnB needs 2x beats)
    tempo_mult = 2.0 if grid.bpm >= 150.0 else 1.0
    if cfg.chop_beats > 0:
        chop_beats = cfg.chop_beats
    elif role == "vocals":
        chop_beats = (4.0 if cfg.editability < 0.65 else 2.0) * tempo_mult
    elif role in ("chords", "other"):
        chop_beats = (2.0 if cfg.editability < 0.5 else 1.0) * tempo_mult
    else:
        chop_beats = (2.0 if cfg.editability < 0.4 else 1.0) * tempo_mult

    chop_rows = max(1.0, chop_beats * grid.rows_per_beat)
    env = onset_envelope(mono, sr=sr, n_fft=1024, hop=256)
    hop = 256

    # Build grid-aligned slice boundaries (snapping gently to nearby strong onset within +/-25ms)
    snap_samples = int(0.025 * sr)
    boundaries: List[Tuple[float, int, int]] = []  # (row_start, start_samp, end_samp)
    r = 0.0
    while True:
        t0 = grid.row_to_time(r)
        t1 = grid.row_to_time(r + chop_rows)
        s0 = int(round(t0 * sr))
        s1 = int(round(t1 * sr))
        if s0 >= n_total:
            break
        s0 = max(0, s0)
        s1 = min(n_total, s1)
        if s1 - s0 < int(0.05 * sr):
            break

        # Gentle snap to strong onset near boundary for punchier stabs
        if role in ("chords", "bass", "melodic", "other") and s0 > snap_samples:
            f_lo = max(0, (s0 - snap_samples) // hop)
            f_hi = min(env.size, (s0 + snap_samples) // hop + 1)
            if f_hi > f_lo:
                local_pk = f_lo + int(np.argmax(env[f_lo:f_hi]))
                if float(env[local_pk]) > 0.35:
                    s0 = local_pk * hop

        seg = stereo[s0:s1]
        if rms(seg) > 0.006:
            boundaries.append((r, s0, s1))
        r += chop_rows

    if not boundaries:
        ins = InstrumentPlan(instrument_id, f"{stem_name}_{suffix}", stem_name, role, "vibe_chop", nna="cut")
        tr = TrackPlan(track_id, f"{stem_name}_{suffix}" if muted else stem_name, stem_name, role, "vibe_chop", instrument_id, muted=muted, max_polyphony=1)
        return ins, tr, next_sample_id, {"chops_total": 0, "unique_chops": 0, "chop_beats": chop_beats}

    # Fingerprint each active chop and deduplicate repeating phrases/stabs
    feats = np.stack([_chop_fingerprint(mono[s0:s1], sr=sr) for _, s0, s1 in boundaries], axis=0)
    # Editability trades off dedup aggressiveness vs exact preservation
    sim_thr = 0.96 - 0.12 * cfg.editability
    max_chops = max(4, min(budget_samples, 96))
    labels, _ = _greedy_cluster(feats, sim_threshold=sim_thr, max_clusters=max_chops)

    clusters: Dict[int, List[int]] = {}
    for idx, cid in enumerate(labels):
        clusters.setdefault(cid, []).append(idx)

    ordered_cids = sorted(clusters.keys())[:max_chops]
    base_note = 24  # C-2 upward so up to 96 chops fit cleanly in 0..119

    zones: List[SampleZone] = []
    cid_to_note: Dict[int, int] = {}
    cid_max_peak: Dict[int, float] = {}
    sample_id = next_sample_id

    for k, cid in enumerate(ordered_cids):
        occ = clusters[cid]
        # Pick cleanest exemplar in the cluster
        best_idx = occ[0]
        best_score = -1.0
        max_pk = 0.0
        for idx in occ:
            _, s0, s1 = boundaries[idx]
            pk = peak(stereo[s0:s1])
            max_pk = max(max_pk, pk)
            clean = _cleanliness(target_rms, other_rms, s0, s1)
            # Prefer exemplars close to cluster centroid as well as clean
            score = clean * (0.7 + 0.3 * min(1.0, pk / 0.2))
            if score > best_score:
                best_score = score
                best_idx = idx

        _, ex_s0, ex_s1 = boundaries[best_idx]
        ex_s0 = snap_to_zero_crossing(stereo, ex_s0, search_samples=64)
        ex_s1 = snap_to_zero_crossing(stereo, ex_s1, search_samples=64)
        if ex_s1 <= ex_s0 + 64:
            ex_s1 = min(n_total, ex_s0 + 256)

        raw_slice = stereo[ex_s0:ex_s1]
        norm_slice, _ = normalize_slice(raw_slice, target_peak=0.95)
        norm_slice = apply_micro_fades(norm_slice, sr=sr, fade_in_ms=2.0, fade_out_ms=10.0)

        key_note = min(119, base_note + k)
        wav_name = f"{stem_name}_{suffix}_{k:02d}.wav"
        wav_path = save_wav(samples_dir / wav_name, norm_slice, sr=sr)

        def_vol = max(4, min(64, int(round((max_pk / 0.95) * 64.0))))
        zones.append(
            SampleZone(
                sample_id=sample_id,
                name=f"{stem_name[:10]}_ch{k:02d}",
                wav_path=str(wav_path),
                root_note=key_note,
                low_note=key_note,
                high_note=key_note,
                duration_sec=round(norm_slice.shape[0] / float(sr), 4),
                sample_rate=sr,
                source_time_sec=round(ex_s0 / float(sr), 4),
                cleanliness_score=round(best_score, 3),
                occurrences=len(occ),
                default_volume=def_vol,
            )
        )
        cid_to_note[cid] = key_note
        cid_max_peak[cid] = max(max_pk, 1e-4)
        sample_id += 1

    events: List[NoteEvent] = []
    for idx, (row_start, s0, s1) in enumerate(boundaries):
        cid = labels[idx]
        if cid not in cid_to_note:
            continue
        occ_pk = peak(stereo[s0:s1])
        vel = max(12, min(127, int(round((occ_pk / cid_max_peak[cid]) * 127.0))))
        dur_rows = max(0.5, (s1 - s0) / float(sr) / grid.sec_per_row)
        events.append(
            NoteEvent(
                time_sec=round(s0 / float(sr), 5),
                row_float=round(max(0.0, grid.time_to_row(s0 / float(sr))), 5),
                note=cid_to_note[cid],
                velocity=vel,
                duration_rows=round(dur_rows, 4),
                cluster_id=cid,
            )
        )

    ins = InstrumentPlan(
        instrument_id=instrument_id,
        name=f"{stem_name[:16]}_{suffix}",
        stem_name=stem_name,
        role=role,
        tier="vibe_chop",
        nna="cut",
        zones=zones,
    )
    tr = TrackPlan(
        track_id=track_id,
        name=f"{stem_name}_vibe" if muted else stem_name,
        stem_name=stem_name,
        role=role,
        tier="vibe_chop",
        instrument_id=instrument_id,
        muted=muted,
        max_polyphony=1,
        events=events,
    )
    return ins, tr, sample_id, {
        "chops_total": len(events),
        "unique_chops": len(zones),
        "chop_beats": chop_beats,
    }


def _decompile_pitched_multisample(
    stem_name: str,
    role: str,
    stereo: np.ndarray,
    other_rms: np.ndarray,
    grid: GridAnalysis,
    sr: int,
    samples_dir: Path,
    next_sample_id: int,
    instrument_id: int,
    track_id: int,
    budget_samples: int,
    cfg: DecompileConfig,
    midi_track: Optional[StemMidiTrack] = None,
) -> Tuple[Optional[InstrumentPlan], Optional[TrackPlan], int, dict]:
    """Tier-2 Promote: pitch-tracked (or MIDI-guided) chromatic playable multi-sample instrument."""
    mono = to_mono(stereo)
    target_rms = _compute_frame_rms(mono, hop=256)
    n_total = stereo.shape[0]
    fmin, fmax = (32.0, 420.0) if role == "bass" else (65.0, 1400.0)

    raw_notes: List[Tuple[int, int, int, float]] = []  # (start_s, end_s, midi_note, conf)
    used_midi = False

    if midi_track is not None and len(midi_track.notes) >= 6:
        env = onset_envelope(mono, sr=sr, n_fft=1024, hop=256)
        for mn in midi_track.notes:
            s0 = int(round(mn.audio_time_sec * sr))
            if not (0 <= s0 < n_total - 256):
                continue
            s0 = _snap_to_local_onset(env, s0, sr=sr, hop=256, window_ms=25.0)
            s1 = min(n_total, max(s0 + int(0.06 * sr), s0 + int(round(mn.duration_sec * sr))))
            if rms(mono[s0:s1]) > 0.008 and 12 <= mn.pitch <= 108:
                raw_notes.append((s0, s1, mn.pitch, 0.95))
        if len(raw_notes) >= 3:
            used_midi = True

    if not used_midi:
        raw_notes = []
        # Frame-wise pitch & energy tracking at 1/2 row steps (clean note segmentation)
        step_sec = max(0.025, 0.5 * grid.sec_per_row)
        step_samp = max(256, int(round(step_sec * sr)))
        win_samp = max(2048, int(round(0.08 * sr)))

        # Also detect sharp onsets to split repeated notes of the same pitch!
        env = onset_envelope(mono, sr=sr, n_fft=1024, hop=256)
        onset_pks = set(p * 256 for p in _pick_peaks(env, threshold=0.22, min_gap_frames=max(3, int(0.08 * sr / 256))))

        # Walk frames to form pitched note segments
        cur_start: Optional[int] = None
        cur_midi: Optional[int] = None
        cur_confs: List[float] = []

        pos = 0
        while pos + 512 < n_total:
            end = min(n_total, pos + win_samp)
            seg = mono[pos:end]
            frame_r = rms(seg)
            freq, conf = (None, 0.0) if frame_r < 0.01 else estimate_pitch_yin(seg, sr=sr, fmin=fmin, fmax=fmax)
            midi = int(round(hz_to_midi(freq))) if freq is not None else None
            if midi is not None and not (12 <= midi <= 108):
                midi = None

            has_onset = any(pos <= op < pos + step_samp for op in onset_pks)

            if midi is None:
                if cur_start is not None and cur_midi is not None:
                    if pos - cur_start >= int(0.06 * sr):
                        raw_notes.append((cur_start, pos, cur_midi, float(np.mean(cur_confs))))
                    cur_start, cur_midi, cur_confs = None, None, []
            else:
                if cur_start is None:
                    cur_start, cur_midi, cur_confs = pos, midi, [conf]
                elif abs(midi - (cur_midi or midi)) >= 1 or (has_onset and pos - cur_start >= int(0.09 * sr)):
                    if pos - cur_start >= int(0.06 * sr) and cur_midi is not None:
                        raw_notes.append((cur_start, pos, cur_midi, float(np.mean(cur_confs))))
                    cur_start, cur_midi, cur_confs = pos, midi, [conf]
                else:
                    cur_confs.append(conf)
            pos += step_samp

        if cur_start is not None and cur_midi is not None and n_total - cur_start >= int(0.06 * sr):
            raw_notes.append((cur_start, n_total, cur_midi, float(np.mean(cur_confs))))

    # If too few confident pitched notes found (e.g. noisy/polyphonic stem), caller falls back to chops
    if len(raw_notes) < 3:
        return None, None, next_sample_id, {"promoted": False, "reason": "insufficient_pitched_notes"}

    # Group notes by MIDI pitch and choose up to `max_zones` cleanest root exemplars
    by_pitch: Dict[int, List[Tuple[int, int, float]]] = {}
    for s0, s1, midi, conf in raw_notes:
        by_pitch.setdefault(midi, []).append((s0, s1, conf))

    max_zones = max(2, min(budget_samples, 24))
    pitches = sorted(by_pitch.keys())
    if len(pitches) > max_zones:
        # Subsample evenly across pitch span to keep keymap balanced
        pick_idx = np.linspace(0, len(pitches) - 1, max_zones).round().astype(int)
        selected_pitches = sorted(set(pitches[i] for i in pick_idx))
    else:
        selected_pitches = pitches

    # Compute keymap zone ranges (midpoint split, identical to ScreamSeq's importMultisample!)
    zones: List[SampleZone] = []
    sample_id = next_sample_id
    global_max_pk = max((peak(stereo[s0:s1]) for s0, s1, _, _ in raw_notes), default=0.5)
    global_max_pk = max(global_max_pk, 1e-4)

    for i, root_midi in enumerate(selected_pitches):
        cands = by_pitch[root_midi]
        best_cand = cands[0]
        best_score = -1.0
        for s0, s1, conf in cands:
            dur = (s1 - s0) / float(sr)
            clean = _cleanliness(target_rms, other_rms, s0, s1)
            # Favor clean, medium-long, confident notes for playable sustain
            score = clean * conf * min(1.0, dur / 0.45)
            if score > best_score:
                best_score = score
                best_cand = (s0, s1, conf)

        ex_s0, ex_s1, _ = best_cand
        ex_s0 = snap_to_zero_crossing(stereo, max(0, ex_s0 - int(0.003 * sr)), search_samples=64)
        ex_s1 = snap_to_zero_crossing(stereo, min(n_total, ex_s1), search_samples=64)
        if ex_s1 <= ex_s0 + 128:
            ex_s1 = min(n_total, ex_s0 + int(0.15 * sr))

        raw_slice = stereo[ex_s0:ex_s1]
        norm_slice, _ = normalize_slice(raw_slice, target_peak=0.95)
        norm_slice = apply_micro_fades(norm_slice, sr=sr, fade_in_ms=2.0, fade_out_ms=10.0)

        low_note = max(0, (selected_pitches[i - 1] + root_midi) // 2 + 1) if i > 0 else max(0, root_midi - 12)
        high_note = (
            min(119, (root_midi + selected_pitches[i + 1]) // 2)
            if i + 1 < len(selected_pitches)
            else min(119, root_midi + 12)
        )

        wav_name = f"{stem_name}_note_{midi_to_note_name(root_midi).replace('#', 's')}.wav"
        wav_path = save_wav(samples_dir / wav_name, norm_slice, sr=sr)

        def_vol = max(4, min(64, int(round((global_max_pk / 0.95) * 64.0))))
        zones.append(
            SampleZone(
                sample_id=sample_id,
                name=f"{stem_name[:10]}_{midi_to_note_name(root_midi)}",
                wav_path=str(wav_path),
                root_note=root_midi,
                low_note=low_note,
                high_note=high_note,
                duration_sec=round(norm_slice.shape[0] / float(sr), 4),
                sample_rate=sr,
                source_time_sec=round(ex_s0 / float(sr), 4),
                cleanliness_score=round(best_score, 3),
                occurrences=len(cands),
                default_volume=def_vol,
            )
        )
        sample_id += 1

    # Transcribe all detected notes onto the chromatic multisample instrument
    events: List[NoteEvent] = []
    for s0, s1, midi, _ in raw_notes:
        t_sec = s0 / float(sr)
        row_f = grid.time_to_row(t_sec)
        if row_f < -0.25:
            continue
        occ_pk = peak(stereo[s0:s1])
        vel = max(12, min(127, int(round((occ_pk / global_max_pk) * 127.0))))
        dur_rows = max(0.5, (s1 - s0) / float(sr) / grid.sec_per_row)
        events.append(
            NoteEvent(
                time_sec=round(t_sec, 5),
                row_float=round(max(0.0, row_f), 5),
                note=max(0, min(119, midi)),
                velocity=vel,
                duration_rows=round(dur_rows, 4),
                cluster_id=midi,
            )
        )

    ins = InstrumentPlan(
        instrument_id=instrument_id,
        name=f"{stem_name[:16]}_multi",
        stem_name=stem_name,
        role=role,
        tier="pitched_multisample",
        nna="cut",
        zones=zones,
    )
    tr = TrackPlan(
        track_id=track_id,
        name=stem_name,
        stem_name=stem_name,
        role=role,
        tier="pitched_multisample",
        instrument_id=instrument_id,
        muted=False,
        max_polyphony=1 if role == "bass" else 2,
        events=events,
    )
    return ins, tr, sample_id, {
        "promoted": True,
        "used_midi": used_midi,
        "notes": len(events),
        "pitch_zones": len(zones),
        "pitch_range": [min(selected_pitches), max(selected_pitches)],
    }


def _should_promote(stem_name: str, role: str, cfg: DecompileConfig) -> bool:
    promote_set = {s.lower() for s in cfg.promote_stems}
    if stem_name.lower() in promote_set or role.lower() in promote_set:
        return True
    if cfg.mode == "vibe":
        return False
    if cfg.mode == "balanced":
        return role == "bass"
    if cfg.mode == "decomposed":
        return role in ("bass", "melodic")
    return False


def decompile_stems(
    stems: Dict[str, Tuple[np.ndarray, str, Path]],
    grid: GridAnalysis,
    out_dir: str | Path,
    title: str = "ScreamSeq Scratchpad",
    sr: int = DEFAULT_SR,
    cfg: DecompileConfig | None = None,
) -> ScratchpadPlan:
    """Run the full two-tier decompilation pipeline and return a factorized `ScratchpadPlan`."""
    config = cfg or DecompileConfig()
    out_root = Path(out_dir)
    samples_dir = out_root / "samples"
    samples_dir.mkdir(parents=True, exist_ok=True)

    other_energy = _other_stem_energy_map(stems, hop=256)

    # Order stems musically: drums -> bass -> melodic/chords/other -> vocals
    role_order = {"drums": 0, "bass": 1, "melodic": 2, "chords": 3, "other": 4, "vocals": 5}
    ordered_names = sorted(stems.keys(), key=lambda n: (role_order.get(stems[n][1], 9), n))

    # Allocate sample budgets proportionally to each stem's active duration (not equally!)
    active_weights: Dict[str, float] = {}
    for name in ordered_names:
        audio, role, _ = stems[name]
        fr = _compute_frame_rms(to_mono(audio), hop=4096)
        act_sec = float(np.sum(fr > 0.01)) * (4096.0 / sr)
        # Vocals/chords chops need proportionally more unique phrases than drum kits
        role_factor = 1.35 if role in ("vocals", "chords", "melodic", "other") else 0.75
        active_weights[name] = max(0.5, act_sec * role_factor)
    total_w = sum(active_weights.values()) + 1e-9
    stem_budgets: Dict[str, int] = {
        name: max(4, min(80, int(round(config.max_total_samples * (active_weights[name] / total_w)))))
        for name in ordered_names
    }

    instruments: List[InstrumentPlan] = []
    tracks: List[TrackPlan] = []
    stem_metrics: dict = {}

    next_sample_id = 1
    next_inst_id = 1
    next_track_id = 0

    for name in ordered_names:
        audio, role, _ = stems[name]
        remaining_budget = max(4, config.max_total_samples - (next_sample_id - 1))
        stem_budget = min(stem_budgets[name], remaining_budget)
        midi_tr = config.midi_guide.match_stem(name, role) if config.midi_guide is not None else None

        if role == "drums":
            ins, tr, next_sample_id, info = _decompile_drums(
                stem_name=name,
                stereo=audio,
                other_rms=other_energy[name],
                grid=grid,
                sr=sr,
                samples_dir=samples_dir,
                next_sample_id=next_sample_id,
                instrument_id=next_inst_id,
                track_id=next_track_id,
                budget_samples=stem_budget,
                cfg=config,
                midi_track=midi_tr,
            )
            instruments.append(ins)
            tracks.append(tr)
            stem_metrics[name] = {"role": role, "tier": ins.tier, **info}
            next_inst_id += 1
            next_track_id += 1
            continue

        promoted_ok = False
        if _should_promote(name, role, config):
            p_budget = max(6, stem_budget // 2) if config.keep_vibe_backup else stem_budget
            p_ins, p_tr, next_sample_id, p_info = _decompile_pitched_multisample(
                stem_name=name,
                role=role,
                stereo=audio,
                other_rms=other_energy[name],
                grid=grid,
                sr=sr,
                samples_dir=samples_dir,
                next_sample_id=next_sample_id,
                instrument_id=next_inst_id,
                track_id=next_track_id,
                budget_samples=p_budget,
                cfg=config,
                midi_track=midi_tr,
            )
            if p_ins is not None and p_tr is not None:
                promoted_ok = True
                instruments.append(p_ins)
                tracks.append(p_tr)
                stem_metrics[name] = {"role": role, "tier": p_ins.tier, **p_info}
                next_inst_id += 1
                next_track_id += 1

                if config.keep_vibe_backup:
                    rem = max(4, config.max_total_samples - (next_sample_id - 1))
                    b_ins, b_tr, next_sample_id, b_info = _decompile_smart_chops(
                        stem_name=name,
                        role=role,
                        stereo=audio,
                        other_rms=other_energy[name],
                        grid=grid,
                        sr=sr,
                        samples_dir=samples_dir,
                        next_sample_id=next_sample_id,
                        instrument_id=next_inst_id,
                        track_id=next_track_id,
                        budget_samples=min(p_budget, rem),
                        cfg=config,
                        muted=True,
                        suffix="vibe",
                    )
                    instruments.append(b_ins)
                    tracks.append(b_tr)
                    stem_metrics[name]["vibe_backup"] = b_info
                    next_inst_id += 1
                    next_track_id += 1

        if not promoted_ok:
            c_ins, c_tr, next_sample_id, c_info = _decompile_smart_chops(
                stem_name=name,
                role=role,
                stereo=audio,
                other_rms=other_energy[name],
                grid=grid,
                sr=sr,
                samples_dir=samples_dir,
                next_sample_id=next_sample_id,
                instrument_id=next_inst_id,
                track_id=next_track_id,
                budget_samples=stem_budget,
                cfg=config,
                muted=False,
            )
            instruments.append(c_ins)
            tracks.append(c_tr)
            stem_metrics[name] = {"role": role, "tier": c_ins.tier, **c_info}
            next_inst_id += 1
            next_track_id += 1

    pat_rows = grid.rows_per_beat * grid.beats_per_bar * 4  # 4 bars = 64 rows at 4 rpb
    plan = ScratchpadPlan(
        version="0.1",
        title=title,
        mode=config.mode,
        editability=round(config.editability, 3),
        bpm=grid.bpm,
        speed=6,
        rows_per_beat=grid.rows_per_beat,
        beats_per_bar=grid.beats_per_bar,
        pattern_rows=pat_rows,
        sample_rate=sr,
        beat_offset_sec=grid.beat_offset_sec,
        total_duration_sec=grid.total_duration_sec,
        grid_diagnostics=grid.to_dict(),
        instruments=instruments,
        tracks=tracks,
        stem_metrics=stem_metrics,
    )
    return factorize_patterns(plan)
