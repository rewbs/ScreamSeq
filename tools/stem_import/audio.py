"""Pure-NumPy audio I/O and DSP helpers for stem_import.

Reads uncompressed PCM WAV natively via stdlib `wave`, and transparently shells
out to `ffmpeg` for MP3/FLAC/OGG/M4A or sample-rate conversion.
"""

from __future__ import annotations

import math
import shutil
import subprocess
import wave
from pathlib import Path
from typing import Tuple

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

DEFAULT_SR = 44100


def _decode_with_ffmpeg(path: Path, target_sr: int) -> np.ndarray:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError(
            f"Cannot decode {path.name}: ffmpeg is not on PATH (needed for non-WAV or resampling)"
        )
    cmd = [
        ffmpeg,
        "-v", "error",
        "-i", str(path),
        "-f", "f32le",
        "-acodec", "pcm_f32le",
        "-ac", "2",
        "-ar", str(target_sr),
        "pipe:1",
    ]
    proc = subprocess.run(cmd, capture_output=True, check=False)
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"ffmpeg failed decoding {path}: {err}")
    raw = np.frombuffer(proc.stdout, dtype="<f4")
    if raw.size % 2 != 0:
        raw = raw[:-1]
    return raw.reshape(-1, 2).copy()


def load_audio(path: str | Path, target_sr: int = DEFAULT_SR) -> Tuple[np.ndarray, int]:
    """Load audio as stereo float32 array shaped `(N, 2)` in `[-1, 1]`."""
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"Audio file not found: {p}")

    if p.suffix.lower() == ".wav":
        try:
            with wave.open(str(p), "rb") as wf:
                sr = wf.getframerate()
                nch = wf.getnchannels()
                sw = wf.getsampwidth()
                nframes = wf.getnframes()
                if sr == target_sr and nch in (1, 2) and sw in (1, 2, 3, 4):
                    raw = wf.readframes(nframes)
                    if sw == 1:
                        data = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
                    elif sw == 2:
                        data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
                    elif sw == 3:
                        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
                        vals = (
                            b[:, 0].astype(np.int32)
                            | (b[:, 1].astype(np.int32) << 8)
                            | (b[:, 2].astype(np.int32) << 16)
                        )
                        vals = np.where(vals >= 0x800000, vals - 0x1000000, vals)
                        data = vals.astype(np.float32) / 8388608.0
                    else:
                        data = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
                    data = data.reshape(-1, nch)
                    if nch == 1:
                        data = np.repeat(data, 2, axis=1)
                    return np.ascontiguousarray(data, dtype=np.float32), sr
        except wave.Error:
            pass  # Fall through to ffmpeg for IEEE float WAV / extensible headers.

    return _decode_with_ffmpeg(p, target_sr), target_sr


def save_wav(path: str | Path, stereo: np.ndarray, sr: int = DEFAULT_SR) -> Path:
    """Save stereo float32 `(N, 2)` (or mono `(N,)`) audio as 16-bit PCM WAV."""
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    arr = np.asarray(stereo, dtype=np.float32)
    if arr.ndim == 1:
        arr = np.stack([arr, arr], axis=1)
    arr = np.clip(arr, -1.0, 1.0)
    pcm = np.round(arr * 32767.0).astype("<i2")
    with wave.open(str(out), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(pcm.tobytes())
    return out


def to_mono(stereo: np.ndarray) -> np.ndarray:
    arr = np.asarray(stereo, dtype=np.float32)
    if arr.ndim == 1:
        return arr
    return 0.5 * (arr[:, 0] + arr[:, 1])


def rms(x: np.ndarray) -> float:
    if x.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(x, dtype=np.float64))))


def peak(x: np.ndarray) -> float:
    if x.size == 0:
        return 0.0
    return float(np.max(np.abs(x)))


def stft_mag(mono: np.ndarray, n_fft: int = 2048, hop: int = 512) -> np.ndarray:
    """Compute magnitude STFT shaped `(n_fft//2 + 1, n_frames)`."""
    x = np.asarray(mono, dtype=np.float32)
    if x.size < n_fft:
        x = np.pad(x, (0, n_fft - x.size))
    pad = n_fft // 2
    x_pad = np.pad(x, (pad, pad), mode="reflect" if x.size > pad else "constant")
    frames = sliding_window_view(x_pad, n_fft)[::hop]
    win = np.hanning(n_fft).astype(np.float32)
    spec = np.fft.rfft(frames * win[None, :], axis=1)
    return np.abs(spec).T.astype(np.float32)


def _hz_to_mel(hz: np.ndarray | float) -> np.ndarray | float:
    return 2595.0 * np.log10(1.0 + np.asarray(hz, dtype=np.float64) / 700.0)


def _mel_to_hz(mel: np.ndarray | float) -> np.ndarray | float:
    return 700.0 * (10.0 ** (np.asarray(mel, dtype=np.float64) / 2595.0) - 1.0)


def mel_filterbank(
    sr: int,
    n_fft: int,
    n_mels: int = 64,
    fmin: float = 30.0,
    fmax: float | None = None,
) -> np.ndarray:
    """Triangular Mel filterbank matrix shaped `(n_mels, n_fft//2 + 1)`."""
    if fmax is None:
        fmax = min(16000.0, 0.5 * sr)
    mels = np.linspace(_hz_to_mel(fmin), _hz_to_mel(fmax), n_mels + 2)
    hz = _mel_to_hz(mels)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    fb = np.zeros((n_mels, freqs.size), dtype=np.float32)
    for m in range(n_mels):
        left, center, right = hz[m], hz[m + 1], hz[m + 2]
        if right <= left:
            continue
        up = (freqs - left) / max(center - left, 1e-9)
        down = (right - freqs) / max(right - center, 1e-9)
        fb[m] = np.maximum(0.0, np.minimum(up, down))
    # Area-normalize each filter
    enorm = 2.0 / np.maximum(hz[2 : n_mels + 2] - hz[:n_mels], 1e-9)
    fb *= enorm[:, None].astype(np.float32)
    return fb


def log_mel_spectrogram(
    mono: np.ndarray,
    sr: int = DEFAULT_SR,
    n_fft: int = 2048,
    hop: int = 512,
    n_mels: int = 64,
) -> np.ndarray:
    mag = stft_mag(mono, n_fft=n_fft, hop=hop)
    fb = mel_filterbank(sr, n_fft, n_mels=n_mels)
    mel = fb @ (mag * mag)
    return np.log1p(10.0 * mel).astype(np.float32)


def chroma_from_mag(mag: np.ndarray, sr: int, n_fft: int) -> np.ndarray:
    """12-bin pitch-class profile shaped `(12, T)` from magnitude STFT."""
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    valid = (freqs >= 55.0) & (freqs <= 4200.0)
    f_val = freqs[valid]
    midi = 69.0 + 12.0 * np.log2(f_val / 440.0)
    pc = np.mod(np.round(midi).astype(np.int32), 12)
    power = (mag[valid] ** 2)
    chroma = np.zeros((12, mag.shape[1]), dtype=np.float32)
    for k in range(12):
        mask = pc == k
        if np.any(mask):
            chroma[k] = power[mask].sum(axis=0)
    norm = np.linalg.norm(chroma, axis=0, keepdims=True) + 1e-8
    return chroma / norm


def onset_envelope(
    mono: np.ndarray,
    sr: int = DEFAULT_SR,
    n_fft: int = 1024,
    hop: int = 256,
    fmin: float | None = None,
    fmax: float | None = None,
) -> np.ndarray:
    """Positive log-compressed spectral-flux onset strength envelope."""
    mag = stft_mag(mono, n_fft=n_fft, hop=hop)
    if fmin is not None or fmax is not None:
        freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
        lo = 0.0 if fmin is None else fmin
        hi = 0.5 * sr if fmax is None else fmax
        band = (freqs >= lo) & (freqs <= hi)
        mag = mag[band]
    log_mag = np.log1p(20.0 * mag)
    diff = np.maximum(0.0, np.diff(log_mag, axis=1, prepend=np.zeros_like(log_mag[:, :1])))
    flux = diff.mean(axis=0)
    # Remove slow baseline with local median/moving average
    win = max(5, int(round(0.25 * sr / hop)))
    kernel = np.ones(win, dtype=np.float32) / win
    baseline = np.convolve(flux, kernel, mode="same")
    env = np.maximum(0.0, flux - 0.85 * baseline)
    mx = float(env.max()) if env.size else 0.0
    return (env / mx).astype(np.float32) if mx > 1e-9 else env.astype(np.float32)


def snap_to_zero_crossing(stereo: np.ndarray, idx: int, search_samples: int = 64) -> int:
    """Find nearest zero-crossing around `idx` to prevent slice clicks."""
    n = stereo.shape[0]
    if n <= 2:
        return max(0, min(n - 1, idx))
    lo = max(0, idx - search_samples)
    hi = min(n - 1, idx + search_samples)
    mono = to_mono(stereo[lo : hi + 1])
    best = idx
    best_dist = search_samples + 1
    for i in range(1, mono.size):
        if (mono[i - 1] <= 0.0 <= mono[i]) or (mono[i - 1] >= 0.0 >= mono[i]):
            cand = lo + i
            d = abs(cand - idx)
            if d < best_dist:
                best_dist = d
                best = cand
    return best


def apply_micro_fades(
    stereo: np.ndarray,
    sr: int = DEFAULT_SR,
    fade_in_ms: float = 1.5,
    fade_out_ms: float = 8.0,
) -> np.ndarray:
    """Apply short click-free cosine ramps at the start and end of a slice."""
    out = np.array(stereo, dtype=np.float32, copy=True)
    n = out.shape[0]
    if n <= 4:
        return out
    fi = min(n // 4, max(0, int(round(fade_in_ms * sr / 1000.0))))
    fo = min(n // 3, max(0, int(round(fade_out_ms * sr / 1000.0))))
    if fi > 1:
        ramp = 0.5 * (1.0 - np.cos(np.linspace(0.0, math.pi, fi, dtype=np.float32)))
        out[:fi] *= ramp[:, None]
    if fo > 1:
        ramp = 0.5 * (1.0 + np.cos(np.linspace(0.0, math.pi, fo, dtype=np.float32)))
        out[n - fo :] *= ramp[:, None]
    return out


def normalize_slice(
    stereo: np.ndarray, target_peak: float = 0.92
) -> Tuple[np.ndarray, float]:
    """Normalize slice peak to `target_peak`, returning `(normalized, original_peak)`."""
    pk = peak(stereo)
    if pk < 1e-6:
        return np.zeros_like(stereo, dtype=np.float32), 0.0
    gain = target_peak / pk
    return (stereo * gain).astype(np.float32), pk


def estimate_pitch_yin(
    mono: np.ndarray,
    sr: int = DEFAULT_SR,
    fmin: float = 36.0,
    fmax: float = 1200.0,
    threshold: float = 0.18,
) -> Tuple[float | None, float]:
    """Pure-NumPy YIN fundamental estimator returning `(freq_hz, confidence)`."""
    x = np.asarray(mono, dtype=np.float64)
    # Use a centered analysis window (up to 4096 samples ~ 93 ms) to skip attack transient
    win = min(x.size, 4096)
    if win < 256:
        return None, 0.0
    start = min(max(0, int(0.15 * x.size)), x.size - win)
    seg = x[start : start + win]
    seg = seg - np.mean(seg)
    energy = float(np.dot(seg, seg))
    if energy < 1e-8:
        return None, 0.0

    tau_min = max(2, int(sr / fmax))
    tau_max = min(win // 2 - 1, int(sr / fmin))
    if tau_max <= tau_min + 2:
        return None, 0.0

    # Fast autocorrelation via FFT for YIN difference function:
    # d(tau) = sum_{j=0}^{W-1} (x[j] - x[j+tau])^2
    w_len = win - tau_max
    if w_len < 64:
        return None, 0.0
    nfft = 1 << int(math.ceil(math.log2(win + w_len)))
    a = np.zeros(nfft, dtype=np.float64)
    b = np.zeros(nfft, dtype=np.float64)
    a[:w_len] = seg[:w_len]
    b[:win] = seg[:win]
    # Cross-correlation sum_j seg[j] * seg[j + tau]
    corr = np.fft.irfft(np.conj(np.fft.rfft(a)) * np.fft.rfft(b), n=nfft)[: tau_max + 1]
    sq = seg * seg
    csum = np.concatenate([[0.0], np.cumsum(sq)])
    e0 = csum[w_len]
    etau = csum[w_len : w_len + tau_max + 1] - csum[: tau_max + 1]
    diff = np.maximum(0.0, e0 + etau - 2.0 * corr)

    cmnd = np.ones_like(diff)
    running = np.cumsum(diff[1:])
    taus = np.arange(1, tau_max + 1, dtype=np.float64)
    cmnd[1:] = diff[1:] * taus / np.maximum(running, 1e-12)

    # First local minimum below threshold
    tau = -1
    for t in range(tau_min, tau_max):
        if cmnd[t] < threshold:
            while t + 1 < tau_max and cmnd[t + 1] < cmnd[t]:
                t += 1
            tau = t
            break
    if tau < 0:
        rel = int(np.argmin(cmnd[tau_min:tau_max]))
        tau = tau_min + rel

    # Parabolic interpolation around tau
    if 1 <= tau < tau_max:
        y0, y1, y2 = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        denom = (y0 - 2.0 * y1 + y2)
        shift = 0.5 * (y0 - y2) / denom if abs(denom) > 1e-12 else 0.0
        tau_interp = tau + float(np.clip(shift, -0.5, 0.5))
    else:
        tau_interp = float(tau)

    conf = float(np.clip(1.0 - cmnd[tau], 0.0, 1.0))
    if conf < 0.55:
        return None, conf
    freq = sr / tau_interp
    if not (fmin <= freq <= fmax):
        return None, 0.0
    return float(freq), conf


def hz_to_midi(freq_hz: float) -> float:
    return 69.0 + 12.0 * math.log2(max(freq_hz, 1e-6) / 440.0)


def midi_to_note_name(midi: int) -> str:
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    octave = (midi // 12) - 1
    return f"{names[midi % 12]}{octave}"


def cosine_sim_matrix(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    an = a / (np.linalg.norm(a, axis=1, keepdims=True) + 1e-8)
    bn = b / (np.linalg.norm(b, axis=1, keepdims=True) + 1e-8)
    return an @ bn.T
