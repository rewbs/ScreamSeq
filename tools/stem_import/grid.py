"""Tempo, beat-phase, downbeat, and drift analysis for multi-stem tracks."""

from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Dict

import numpy as np

from .audio import DEFAULT_SR, onset_envelope, to_mono


@dataclass
class GridAnalysis:
    bpm: float
    beat_offset_sec: float
    rows_per_beat: int
    beats_per_bar: int
    total_duration_sec: float
    total_rows: int
    median_abs_drift_ms: float
    max_drift_ms: float
    bpm_confidence: float

    @property
    def sec_per_beat(self) -> float:
        return 60.0 / self.bpm

    @property
    def sec_per_row(self) -> float:
        return 60.0 / (self.bpm * self.rows_per_beat)

    def time_to_row(self, t_sec: float) -> float:
        return (t_sec - self.beat_offset_sec) / self.sec_per_row

    def row_to_time(self, row: float) -> float:
        return self.beat_offset_sec + row * self.sec_per_row

    def to_dict(self) -> dict:
        return asdict(self)


def _combined_onset_envelope(
    stems: Dict[str, tuple[np.ndarray, str]],
    sr: int,
    hop: int = 256,
) -> tuple[np.ndarray, np.ndarray]:
    """Compute weighted multi-stem onset envelope and low-band accent envelope."""
    role_weights = {
        "drums": 2.5,
        "bass": 1.3,
        "chords": 1.0,
        "melodic": 0.9,
        "other": 0.9,
        "vocals": 0.4,
    }
    envs = []
    low_envs = []
    weights = []
    min_len = None
    for _, (audio, role) in stems.items():
        mono = to_mono(audio)
        e = onset_envelope(mono, sr=sr, n_fft=1024, hop=hop)
        le = onset_envelope(mono, sr=sr, n_fft=1024, hop=hop, fmin=30.0, fmax=220.0)
        min_len = e.size if min_len is None else min(min_len, e.size)
        envs.append(e)
        low_envs.append(le)
        weights.append(role_weights.get(role, 1.0))

    if not envs or min_len is None or min_len < 16:
        return np.zeros(16, dtype=np.float32), np.zeros(16, dtype=np.float32)

    w_sum = sum(weights)
    comb = np.zeros(min_len, dtype=np.float32)
    low = np.zeros(min_len, dtype=np.float32)
    for e, le, w in zip(envs, low_envs, weights):
        comb += (w / w_sum) * e[:min_len]
        low += (w / w_sum) * le[:min_len]
    return comb, low


def _estimate_bpm_comb(
    env: np.ndarray,
    sr: int,
    hop: int,
    bpm_min: float = 72.0,
    bpm_max: float = 195.0,
    bpm_hint: float | None = None,
) -> tuple[float, float]:
    """Autocorrelation + harmonic comb scoring + 0.01 BPM phasor refinement."""
    fps = sr / hop
    x = env - np.mean(env)
    if float(np.dot(x, x)) < 1e-10:
        return bpm_hint or 120.0, 0.0

    # If a reliable hint (e.g. Suno MIDI nominal BPM or filename "(177BPM)") is provided,
    # search tightly around it so half-time / double-time ambiguity is completely eliminated!
    if bpm_hint is not None and 40.0 <= bpm_hint <= 240.0:
        coarse_bpm = float(bpm_hint)
        conf = 0.95
    else:
        nfft = 1 << int(np.ceil(np.log2(2 * x.size)))
        spec = np.fft.rfft(x, n=nfft)
        acf = np.fft.irfft(spec * np.conj(spec), n=nfft)[: x.size]
        if acf[0] > 1e-12:
            acf = acf / acf[0]

        bpms = np.arange(bpm_min, bpm_max + 0.01, 0.25)
        scores = np.zeros_like(bpms)
        for i, bpm in enumerate(bpms):
            period = fps * 60.0 / bpm
            s = 0.0
            for mult, wt in ((1.0, 1.0), (2.0, 0.85), (4.0, 0.65), (0.5, 0.35)):
                lag = int(round(period * mult))
                if 2 <= lag < acf.size:
                    lo = max(1, lag - 1)
                    hi = min(acf.size, lag + 2)
                    s += wt * float(np.max(acf[lo:hi]))
            prior = float(np.exp(-0.5 * ((np.log2(bpm / 124.0)) / 0.65) ** 2))
            scores[i] = s * (0.88 + 0.12 * prior)

        best_idx = int(np.argmax(scores))
        coarse_bpm = float(bpms[best_idx])
        med = float(np.median(scores))
        conf = float(np.clip((scores[best_idx] - med) / (abs(scores[best_idx]) + 1e-6), 0.0, 1.0))

    # Fine 0.01 BPM phasor sweep around coarse_bpm (+/- 1.5 BPM)
    lo_b = max(40.0, coarse_bpm - 1.5)
    hi_b = min(240.0, coarse_bpm + 1.5)
    fine_bpms = np.arange(lo_b, hi_b + 1e-6, 0.01)
    best_bpm = coarse_bpm
    best_score = -1.0
    t_sec = np.arange(env.size, dtype=np.float64) / fps
    for bpm in fine_bpms:
        ang1 = 2.0 * np.pi * t_sec * (bpm / 60.0)
        ang2 = 2.0 * np.pi * t_sec * (bpm / 30.0)
        val = abs(np.dot(env, np.exp(1j * ang1))) + 0.55 * abs(np.dot(env, np.exp(1j * ang2)))
        if val > best_score:
            best_score = float(val)
            best_bpm = float(bpm)

    # Snap to nearest integer BPM if within 0.12 BPM
    nearest = round(best_bpm)
    if abs(best_bpm - nearest) <= 0.12:
        best_bpm = float(nearest)
    return best_bpm, conf


def _estimate_phase_and_downbeat(
    env: np.ndarray,
    low_env: np.ndarray,
    bpm: float,
    sr: int,
    hop: int,
    beats_per_bar: int = 4,
) -> float:
    """Find beat-0 offset in `[0, bar_sec)` maximizing grid + downbeat accent."""
    fps = sr / hop
    beat_frames = fps * 60.0 / bpm
    n = env.size
    if n < int(beat_frames * 2):
        return 0.0

    # Step 1: find beat phase in [0, beat_frames)
    n_steps = max(32, int(round(beat_frames * 2)))
    phases = np.linspace(0.0, beat_frames, n_steps, endpoint=False)
    best_phase = 0.0
    best_phase_score = -1.0
    for ph in phases:
        # Score beat positions (weighting low-band kick/bass accents strongly so offbeat hats don't invert phase)
        beats = np.arange(ph, n - 1, beat_frames)
        if beats.size == 0:
            continue
        idx = np.round(beats).astype(np.int32)
        score = float(env[idx].mean()) + 1.25 * float(low_env[idx].mean())
        half = np.arange(ph + 0.5 * beat_frames, n - 1, beat_frames)
        if half.size:
            h_idx = np.round(half).astype(np.int32)
            score += 0.25 * float(env[h_idx].mean())
        if score > best_phase_score:
            best_phase_score = score
            best_phase = float(ph)

    # Step 2: refine beat phase to sub-frame precision using a local phasor around best_phase
    # so hits center cleanly on tick 0 instead of picking up a global 1-tick (SD1) lag.
    t_sec = np.arange(n, dtype=np.float64) / fps
    beat_sec = 60.0 / bpm
    coarse_offset = best_phase / fps
    # Weight frames near the coarse beat positions to lock exact transient center
    dist_to_beat = np.abs(((t_sec - coarse_offset + 0.5 * beat_sec) % beat_sec) - 0.5 * beat_sec)
    near_mask = dist_to_beat <= min(0.06, 0.20 * beat_sec)
    weights = (env + 1.25 * low_env) * near_mask.astype(np.float64)
    if float(weights.sum()) > 1e-6:
        z = np.dot(weights, np.exp(2j * np.pi * (t_sec - coarse_offset) / beat_sec))
        delta_sec = (float(np.angle(z)) / (2.0 * np.pi)) * beat_sec
        # STFT spectral-flux peaks ~1 hop (~5.8 ms) after the acoustic transient attack;
        # compensate by 1 hop so snapped note onsets center cleanly on tick 0 (avoiding SD5/SD1).
        refined = (coarse_offset + delta_sec - (hop / float(sr))) % beat_sec
    else:
        refined = coarse_offset

    # Guard against tiny negative jitter near t=0 wrapping to (beat_sec - eps)
    # and dropping beat 0! Only keep positive offsets cleanly inside (4 ms, beat_sec - 35 ms).
    if refined <= 0.004 or refined >= beat_sec - 0.035:
        return 0.0
    return float(refined)


def _measure_drift(
    env: np.ndarray,
    bpm: float,
    beat_offset_sec: float,
    sr: int,
    hop: int,
) -> tuple[float, float]:
    """Measure local onset-vs-grid drift in ms across beat windows."""
    fps = sr / hop
    beat_frames = fps * 60.0 / bpm
    start_frame = beat_offset_sec * fps
    search = max(2, int(round(0.2 * beat_frames)))
    drifts_ms = []
    pos = start_frame
    while pos + search < env.size:
        c = int(round(pos))
        lo = max(0, c - search)
        hi = min(env.size, c + search + 1)
        win = env[lo:hi]
        if win.size and float(win.max()) > 0.15:
            pk = lo + int(np.argmax(win))
            drifts_ms.append(abs(pk - pos) * 1000.0 / fps)
        pos += beat_frames
    if not drifts_ms:
        return 0.0, 0.0
    arr = np.asarray(drifts_ms, dtype=np.float64)
    return float(np.median(arr)), float(np.max(arr))


def analyze_grid(
    stems: Dict[str, tuple[np.ndarray, str]],
    sr: int = DEFAULT_SR,
    bpm_override: float | None = None,
    bpm_hint: float | None = None,
    rows_per_beat: int = 4,
    beats_per_bar: int = 4,
) -> GridAnalysis:
    """Estimate shared tempo, beat phase, and drift across all stems."""
    hop = 256
    max_samples = max((a.shape[0] for a, _ in stems.values()), default=0)
    duration_sec = max_samples / float(sr) if sr > 0 else 0.0

    env, low_env = _combined_onset_envelope(stems, sr=sr, hop=hop)
    if bpm_override is not None and bpm_override > 0:
        bpm = float(bpm_override)
        conf = 1.0
    else:
        bpm, conf = _estimate_bpm_comb(env, sr=sr, hop=hop, bpm_hint=bpm_hint)

    offset = _estimate_phase_and_downbeat(env, low_env, bpm, sr=sr, hop=hop, beats_per_bar=beats_per_bar)
    med_drift, max_drift = _measure_drift(env, bpm, offset, sr=sr, hop=hop)
    sec_per_row = 60.0 / (bpm * rows_per_beat)
    exact_rows = max(0.0, (duration_sec - offset) / sec_per_row)
    total_rows = max(rows_per_beat * beats_per_bar, int(np.ceil(exact_rows - 0.05)))

    return GridAnalysis(
        bpm=round(bpm, 3),
        beat_offset_sec=round(offset, 5),
        rows_per_beat=rows_per_beat,
        beats_per_bar=beats_per_bar,
        total_duration_sec=round(duration_sec, 4),
        total_rows=total_rows,
        median_abs_drift_ms=round(med_drift, 2),
        max_drift_ms=round(max_drift, 2),
        bpm_confidence=round(conf, 3),
    )
