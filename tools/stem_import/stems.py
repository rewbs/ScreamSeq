"""Stem discovery, musical-role classification, and optional stem separation."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import urllib.request
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

from .audio import DEFAULT_SR, load_audio, onset_envelope, stft_mag, to_mono

AUDIO_EXTENSIONS = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aiff", ".aif"}

VALID_ROLES = ("drums", "bass", "melodic", "chords", "vocals", "other")

_FILENAME_ROLE_HINTS: List[Tuple[Tuple[str, ...], str]] = [
    (("drum", "perc", "kick", "snare", "hat", "beat", "kit", "rhythm"), "drums"),
    (("bass", "sub", "808"), "bass"),
    (("vocal", "vox", "voice", "sing", "choir", "backing"), "vocals"),
    (("chord", "pad", "keyboard", "keys", "piano", "organ", "rhodes", "guitar", "brass", "strings"), "chords"),
    (("synth", "lead", "arp", "melody", "pluck", "bell", "flute", "solo"), "melodic"),
    (("other", "inst", "music", "fx"), "other"),
]


def parse_bpm_hint_from_name(path: str | Path) -> Optional[float]:
    """Extract BPM hint from Suno-style names like 'Hermes Agent Stems (177BPM).zip'."""
    text = Path(path).name
    m = re.search(r"(\d{2,3}(?:\.\d+)?)\s*bpm", text, flags=re.IGNORECASE)
    if not m:
        return None
    val = float(m.group(1))
    return val if 40.0 <= val <= 240.0 else None


def classify_stem_role(name: str, stereo: np.ndarray, sr: int = DEFAULT_SR) -> str:
    """Infer stem role from filename first, falling back to spectral heuristics."""
    lower = name.lower()
    for tokens, role in _FILENAME_ROLE_HINTS:
        if any(tok in lower for tok in tokens):
            return role

    # Acoustic fallback: check low-band ratio & percussive crest factor
    mono = to_mono(stereo)
    if mono.size < 2048:
        return "other"
    mag = stft_mag(mono, n_fft=2048, hop=512)
    freqs = np.fft.rfftfreq(2048, 1.0 / sr)
    power = mag * mag
    total = float(power.sum()) + 1e-9
    low_ratio = float(power[freqs < 180.0].sum()) / total
    env = onset_envelope(mono, sr=sr, n_fft=1024, hop=256)
    crest = float(np.percentile(env, 98) / (np.mean(env) + 1e-6)) if env.size else 0.0

    if low_ratio > 0.55 and crest < 6.0:
        return "bass"
    if crest >= 5.5:
        return "drums"
    return "other"


def discover_stems(
    input_path: str | Path,
    role_overrides: Dict[str, str] | None = None,
    sr: int = DEFAULT_SR,
    sep_backend: str | None = None,
    sep_api_url: str | None = None,
    work_dir: Path | None = None,
) -> Dict[str, Tuple[np.ndarray, str, Path]]:
    """Load a directory of stems (or separate a single audio file) into `{name: (stereo, role, path)}`."""
    p = Path(input_path)
    overrides = {k.lower(): v.lower() for k, v in (role_overrides or {}).items()}

    if p.is_file():
        if p.suffix.lower() == ".zip":
            extract_dir = (work_dir or p.parent) / f"{p.stem}_extracted"
            extract_dir.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(p) as zf:
                zf.extractall(extract_dir)
            p = extract_dir
        elif sep_backend or sep_api_url:
            stem_dir = separate_mix(
                p,
                out_dir=(work_dir or p.parent) / f"{p.stem}_stems",
                backend=sep_backend or ("api" if sep_api_url else "demucs"),
                api_url=sep_api_url,
            )
            p = stem_dir
        else:
            audio, _ = load_audio(p, target_sr=sr)
            role = overrides.get(p.stem.lower()) or classify_stem_role(p.stem, audio, sr=sr)
            return {p.stem: (audio, role, p)}

    if not p.is_dir():
        raise FileNotFoundError(f"Input path does not exist: {p}")

    files = sorted(
        f for f in p.rglob("*")
        if f.is_file() and f.suffix.lower() in AUDIO_EXTENSIONS and not f.name.startswith(".") and "__MACOSX" not in f.parts
    )
    if not files:
        raise ValueError(f"No audio stem files found in {p}")

    stems: Dict[str, Tuple[np.ndarray, str, Path]] = {}
    for f in files:
        # Skip full mix reference files if individual stems exist
        stem_key = f.stem
        if len(files) > 1 and stem_key.lower() in ("mix", "full", "full_mix", "master", "original"):
            continue
        audio, _ = load_audio(f, target_sr=sr)
        role = overrides.get(stem_key.lower()) or classify_stem_role(stem_key, audio, sr=sr)
        if role not in VALID_ROLES:
            raise ValueError(f"Invalid role '{role}' for stem '{stem_key}'; expected one of {VALID_ROLES}")
        stems[stem_key] = (audio, role, f)

    if not stems:
        raise ValueError(f"No usable stems remained in {p}")
    return stems


def separate_mix(
    mix_path: str | Path,
    out_dir: str | Path,
    backend: str = "demucs",
    api_url: str | None = None,
    model: str = "htdemucs",
) -> Path:
    """Optional stem separation hook (local `demucs` CLI or external HTTP API)."""
    mix = Path(mix_path)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    if backend == "demucs":
        demucs_bin = shutil.which("demucs")
        if not demucs_bin:
            raise RuntimeError(
                "Local stem separation requested (--separate demucs), but `demucs` is not installed. "
                "Either provide a directory of downloaded stems, install `demucs`, or pass `--sep-api-url`."
            )
        cmd = [demucs_bin, "-n", model, "-o", str(out), str(mix)]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            raise RuntimeError(f"demucs failed: {proc.stderr.strip()}")
        stem_dir = out / model / mix.stem
        if not stem_dir.is_dir():
            raise RuntimeError(f"Expected demucs stems in {stem_dir}, but folder was not created")
        return stem_dir

    if backend == "api":
        if not api_url:
            raise ValueError("--sep-api-url is required when using --separate api")
        payload = json.dumps({"input_path": str(mix.resolve()), "output_dir": str(out.resolve())}).encode("utf-8")
        req = urllib.request.Request(
            api_url,
            data=payload,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=300) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        stems_path = Path(body.get("stems_dir", out))
        if not stems_path.is_dir():
            raise RuntimeError(f"Stem-separation API returned missing directory: {stems_path}")
        return stems_path

    raise ValueError(f"Unknown separation backend: {backend}")
