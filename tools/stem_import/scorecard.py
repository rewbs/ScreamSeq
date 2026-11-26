"""Automated baseline scorecard: offline audio fidelity vs. MDL editability.

Renders the generated `.it` module offline via `detectseq-render` (OpenMPT) and
compares against the original target stem mix across perceptual/spectral and
structural tracker-editability metrics.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple

import numpy as np

from .audio import (
    DEFAULT_SR,
    load_audio,
    log_mel_spectrogram,
    onset_envelope,
    rms,
    stft_mag,
    to_mono,
)
from .plan import ScratchpadPlan

DEFAULT_RENDERER_CANDIDATES = (
    "/mnt/c/Users/P14/code/detectseq/bin/detectseq-render",
    "detectseq-render",
    "openmpt123",
)


@dataclass
class ScorecardReport:
    # Audio reconstruction fidelity
    log_mel_cosine: float
    log_mel_l1: float
    mr_stft_l1: float
    onset_corr: float
    loudness_ratio_db: float
    alignment_offset_ms: float
    # MDL / Editability metrics
    total_unique_samples: int
    total_events: int
    events_per_sample: float
    total_unique_sample_sec: float
    sample_compression_ratio: float
    max_sample_sec: float
    median_sample_sec: float
    unique_patterns: int
    total_orders: int
    pattern_reuse_ratio: float
    # Combined diagnostic score (0..100)
    fidelity_score: float
    editability_score: float
    composite_score: float
    renderer_used: str

    def to_dict(self) -> dict:
        return asdict(self)

    def save_json(self, path: str | Path) -> Path:
        out = Path(path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(self.to_dict(), indent=2), encoding="utf-8")
        return out


def find_renderer(explicit_path: str | None = None) -> Optional[str]:
    candidates = ([explicit_path] if explicit_path else []) + list(DEFAULT_RENDERER_CANDIDATES)
    for cand in candidates:
        if not cand:
            continue
        p = Path(cand)
        if p.is_file():
            return str(p)
        found = shutil.which(cand)
        if found:
            return found
    return None


def render_it_offline(
    it_path: str | Path,
    out_wav: str | Path,
    sr: int = DEFAULT_SR,
    renderer: str | None = None,
    max_seconds: int = 600,
) -> Tuple[Path, str]:
    """Render an `.it` module offline using `detectseq-render` or `openmpt123`."""
    bin_path = find_renderer(renderer)
    if not bin_path:
        raise RuntimeError(
            "No offline OpenMPT renderer found (checked detectseq-render and openmpt123). "
            "Pass --renderer /path/to/detectseq-render."
        )
    it_p = Path(it_path)
    out_p = Path(out_wav)
    out_p.parent.mkdir(parents=True, exist_ok=True)

    if "detectseq-render" in Path(bin_path).name:
        with tempfile.TemporaryDirectory(prefix="stem_score_") as tmp:
            cmd = [
                bin_path, "trace", str(it_p), tmp,
                "--rate", str(sr),
                "--max-seconds", str(max_seconds),
            ]
            proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if proc.returncode != 0:
                raise RuntimeError(f"detectseq-render failed: {proc.stderr.strip() or proc.stdout.strip()}")
            # detectseq-render trace writes mix.wav (or full_mix.wav / audio.wav)
            wavs = sorted(Path(tmp).glob("*.wav"))
            if not wavs:
                raise RuntimeError(f"detectseq-render produced no WAV in {tmp}")
            # Prefer mix.wav if present
            chosen = next((w for w in wavs if "mix" in w.stem.lower()), wavs[0])
            shutil.copy2(chosen, out_p)
        return out_p, bin_path

    # Fallback: openmpt123
    cmd = [bin_path, "--force", "--samplerate", str(sr), "--output", str(out_p), str(it_p)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"openmpt123 failed: {proc.stderr.strip()}")
    return out_p, bin_path


def _align_signals(ref: np.ndarray, est: np.ndarray, sr: int, max_lag_sec: float = 0.50) -> Tuple[np.ndarray, np.ndarray, float]:
    """Cross-correlate onset envelopes to compensate for small beat-offset alignment."""
    hop = 256
    e_ref = onset_envelope(ref, sr=sr, n_fft=1024, hop=hop)
    e_est = onset_envelope(est, sr=sr, n_fft=1024, hop=hop)
    n = min(e_ref.size, e_est.size)
    if n < 16:
        m = min(ref.size, est.size)
        return ref[:m], est[:m], 0.0
    e_ref = e_ref[:n] - float(np.mean(e_ref[:n]))
    e_est = e_est[:n] - float(np.mean(e_est[:n]))
    max_lag = min(n // 4, int(round(max_lag_sec * sr / hop)))
    best_lag = 0
    best_val = -1e18
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            v = float(np.dot(e_ref[lag:], e_est[: n - lag]))
        else:
            v = float(np.dot(e_ref[: n + lag], e_est[-lag:]))
        if v > best_val:
            best_val = v
            best_lag = lag

    sample_shift = best_lag * hop
    if sample_shift > 0:
        ref = ref[sample_shift:]
    elif sample_shift < 0:
        est = est[-sample_shift:]
    m = min(ref.size, est.size)
    return ref[:m], est[:m], sample_shift * 1000.0 / float(sr)


def compute_scorecard(
    plan: ScratchpadPlan,
    original_stems: Dict[str, Tuple[np.ndarray, str, Path]],
    it_path: str | Path,
    rendered_wav_out: str | Path | None = None,
    renderer: str | None = None,
) -> ScorecardReport:
    """Render `.it` offline and compute fidelity + MDL editability scorecard."""
    sr = plan.sample_rate
    # Build reference mix from original stems
    max_len = max((a.shape[0] for a, _, _ in original_stems.values()), default=0)
    ref_mix = np.zeros((max_len, 2), dtype=np.float32)
    for audio, _, _ in original_stems.values():
        ref_mix[: audio.shape[0]] += audio

    wav_target = Path(rendered_wav_out) if rendered_wav_out else Path(it_path).with_suffix(".render.wav")
    rendered_path, used_bin = render_it_offline(it_path, wav_target, sr=sr, renderer=renderer)
    est_mix, _ = load_audio(rendered_path, target_sr=sr)

    # Account for beat_offset_sec before fine cross-correlation alignment
    offset_samples = int(round(plan.beat_offset_sec * sr))
    ref_mono_full = to_mono(ref_mix[offset_samples:]) if offset_samples < ref_mix.shape[0] else to_mono(ref_mix)
    est_mono_full = to_mono(est_mix)

    ref_m, est_m, align_ms = _align_signals(ref_mono_full, est_mono_full, sr=sr)

    # Loudness ratio before RMS-normalized spectral comparison
    ref_r = max(rms(ref_m), 1e-7)
    est_r = max(rms(est_m), 1e-7)
    loudness_db = float(20.0 * np.log10(est_r / ref_r))

    # Normalize both to unit RMS for pure spectral/structural fidelity comparison
    ref_n = ref_m / ref_r
    est_n = est_m / est_r

    # Log-mel similarity
    mel_ref = log_mel_spectrogram(ref_n, sr=sr, n_fft=2048, hop=512, n_mels=64)
    mel_est = log_mel_spectrogram(est_n, sr=sr, n_fft=2048, hop=512, n_mels=64)
    t_mel = min(mel_ref.shape[1], mel_est.shape[1])
    mel_ref, mel_est = mel_ref[:, :t_mel], mel_est[:, :t_mel]
    log_mel_l1 = float(np.mean(np.abs(mel_ref - mel_est)))
    num = float(np.sum(mel_ref * mel_est))
    den = float(np.linalg.norm(mel_ref) * np.linalg.norm(mel_est)) + 1e-9
    log_mel_cos = float(np.clip(num / den, 0.0, 1.0))

    # Multi-resolution STFT log-magnitude L1
    mr_losses = []
    for n_fft, hop in ((512, 128), (1024, 256), (2048, 512)):
        s_ref = np.log1p(10.0 * stft_mag(ref_n, n_fft=n_fft, hop=hop))
        s_est = np.log1p(10.0 * stft_mag(est_n, n_fft=n_fft, hop=hop))
        t_s = min(s_ref.shape[1], s_est.shape[1])
        mr_losses.append(float(np.mean(np.abs(s_ref[:, :t_s] - s_est[:, :t_s]))))
    mr_stft_l1 = float(np.mean(mr_losses))

    # Rhythmic onset envelope correlation
    on_ref = onset_envelope(ref_n, sr=sr, n_fft=1024, hop=256)
    on_est = onset_envelope(est_n, sr=sr, n_fft=1024, hop=256)
    t_on = min(on_ref.size, on_est.size)
    a_on = on_ref[:t_on] - float(np.mean(on_ref[:t_on]))
    b_on = on_est[:t_on] - float(np.mean(on_est[:t_on]))
    onset_corr = float(
        np.clip(np.dot(a_on, b_on) / (np.linalg.norm(a_on) * np.linalg.norm(b_on) + 1e-9), -1.0, 1.0)
    )

    # MDL / Editability structural stats (exclude muted backup tracks from active stats)
    active_inst_ids = {t.instrument_id for t in plan.tracks if not t.muted}
    active_zones = [
        z for ins in plan.instruments if ins.instrument_id in active_inst_ids for z in ins.zones
    ]
    active_events = [e for t in plan.tracks if not t.muted for e in t.events if not e.is_cut]

    n_samples = len(active_zones)
    n_events = len(active_events)
    durations = [z.duration_sec for z in active_zones]
    total_unique_sec = float(sum(durations))
    total_stem_sec = max(0.1, plan.total_duration_sec * max(1, len(original_stems)))
    sample_comp = total_unique_sec / total_stem_sec
    max_samp_sec = float(max(durations)) if durations else 0.0
    med_samp_sec = float(np.median(durations)) if durations else 0.0
    events_per_samp = n_events / float(max(1, n_samples))

    unique_pats = len(plan.patterns)
    total_orders = max(1, len(plan.order_list))
    pat_reuse = unique_pats / float(total_orders)

    # Diagnostic 0..100 summary scores (for quick experiment comparisons)
    fidelity_score = 100.0 * (
        0.50 * log_mel_cos
        + 0.30 * max(0.0, onset_corr)
        + 0.20 * float(np.exp(-0.5 * mr_stft_l1))
    )
    # Rewards compact sample dictionary, reuse, non-giant slices, and pattern folding
    reuse_term = min(1.0, np.log1p(events_per_samp) / np.log1p(12.0))
    compact_term = float(np.exp(-1.5 * sample_comp))
    slice_len_term = float(np.exp(-0.35 * max(0.0, max_samp_sec - 2.0)))
    pat_term = 1.0 - 0.5 * pat_reuse
    editability_score = 100.0 * (0.35 * reuse_term + 0.30 * compact_term + 0.20 * slice_len_term + 0.15 * pat_term)
    composite_score = 0.60 * fidelity_score + 0.40 * editability_score

    return ScorecardReport(
        log_mel_cosine=round(log_mel_cos, 4),
        log_mel_l1=round(log_mel_l1, 4),
        mr_stft_l1=round(mr_stft_l1, 4),
        onset_corr=round(onset_corr, 4),
        loudness_ratio_db=round(loudness_db, 2),
        alignment_offset_ms=round(align_ms, 2),
        total_unique_samples=n_samples,
        total_events=n_events,
        events_per_sample=round(events_per_samp, 2),
        total_unique_sample_sec=round(total_unique_sec, 3),
        sample_compression_ratio=round(sample_comp, 4),
        max_sample_sec=round(max_samp_sec, 3),
        median_sample_sec=round(med_samp_sec, 3),
        unique_patterns=unique_pats,
        total_orders=total_orders,
        pattern_reuse_ratio=round(pat_reuse, 4),
        fidelity_score=round(fidelity_score, 2),
        editability_score=round(editability_score, 2),
        composite_score=round(composite_score, 2),
        renderer_used=used_bin,
    )
