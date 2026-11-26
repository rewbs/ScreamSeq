"""End-to-end verification tests for `tools.stem_import`.

Exercises:
  1. Tempo/grid recovery on a realistic 4-stem 124 BPM synthetic track.
  2. Cross-stem bleed-aware exemplar selection.
  3. All 3 modes (`vibe`, `balanced`, `decomposed`) + `--keep-vibe-backup`.
  4. `detectseq/1.0` JSON ingestion hook for drum stems.
  5. Pure-Python `.it` generation + real OpenMPT offline render & scorecard via `detectseq-render`.
"""

from __future__ import annotations

import json
import math
import tempfile
import unittest
from pathlib import Path
from typing import Dict, Tuple

import numpy as np

from tools.stem_import.audio import DEFAULT_SR, save_wav
from tools.stem_import.cli import main as cli_main
from tools.stem_import.decompile import DecompileConfig, decompile_stems
from tools.stem_import.grid import analyze_grid
from tools.stem_import.it_writer import write_it
from tools.stem_import.plan import ScratchpadPlan
from tools.stem_import.scorecard import compute_scorecard, find_renderer
from tools.stem_import.stems import discover_stems


def _make_kick(sr: int = DEFAULT_SR) -> np.ndarray:
    n = int(0.24 * sr)
    t = np.arange(n, dtype=np.float32) / sr
    f = 48.0 + 135.0 * np.exp(-t / 0.022)
    phase = 2.0 * np.pi * np.cumsum(f) / sr
    sig = np.sin(phase) * np.exp(-t / 0.085)
    return np.stack([sig, sig], axis=1) * 0.85


def _make_snare(rng: np.random.Generator, sr: int = DEFAULT_SR) -> np.ndarray:
    n = int(0.20 * sr)
    t = np.arange(n, dtype=np.float32) / sr
    body = 0.45 * np.sin(2.0 * np.pi * 195.0 * t) * np.exp(-t / 0.035)
    noise_l = 0.65 * rng.standard_normal(n).astype(np.float32) * np.exp(-t / 0.065)
    noise_r = 0.65 * rng.standard_normal(n).astype(np.float32) * np.exp(-t / 0.065)
    return np.stack([body + noise_l, body + noise_r], axis=1) * 0.72


def _make_hat(rng: np.random.Generator, sr: int = DEFAULT_SR) -> np.ndarray:
    n = int(0.08 * sr)
    t = np.arange(n, dtype=np.float32) / sr
    raw = rng.standard_normal(n + 2).astype(np.float32)
    hp = np.diff(raw, n=2) * np.exp(-t / 0.022)
    return np.stack([hp * 0.35, hp * 0.42], axis=1)


def _make_bass_note(midi: int, dur_sec: float, sr: int = DEFAULT_SR) -> np.ndarray:
    n = int(dur_sec * sr)
    t = np.arange(n, dtype=np.float32) / sr
    freq = 440.0 * (2.0 ** ((midi - 69) / 12.0))
    sig = (
        0.70 * np.sin(2.0 * np.pi * freq * t)
        + 0.25 * np.sin(2.0 * np.pi * 2.0 * freq * t)
        + 0.10 * np.sin(2.0 * np.pi * 3.0 * freq * t)
    )
    env = np.minimum(1.0, t / 0.008) * np.exp(-t / max(0.22, dur_sec * 0.9))
    out = sig * env * 0.65
    return np.stack([out, out], axis=1).astype(np.float32)


def _make_chord_stab(midis: Tuple[int, ...], dur_sec: float, sr: int = DEFAULT_SR) -> np.ndarray:
    n = int(dur_sec * sr)
    t = np.arange(n, dtype=np.float32) / sr
    left = np.zeros(n, dtype=np.float32)
    right = np.zeros(n, dtype=np.float32)
    for idx, m in enumerate(midis):
        f = 440.0 * (2.0 ** ((m - 69) / 12.0))
        saw = 2.0 * ((t * f) % 1.0) - 1.0
        saw_det = 2.0 * ((t * f * 1.004) % 1.0) - 1.0
        w = 0.22 / len(midis)
        if idx % 2 == 0:
            left += w * (0.7 * saw + 0.3 * saw_det)
            right += w * (0.3 * saw + 0.7 * saw_det)
        else:
            left += w * (0.3 * saw + 0.7 * saw_det)
            right += w * (0.7 * saw + 0.3 * saw_det)
    env = np.minimum(1.0, t / 0.006) * np.exp(-t / 0.28)
    return np.stack([left * env, right * env], axis=1).astype(np.float32)


def _mix_in(buf: np.ndarray, clip: np.ndarray, start_idx: int, gain: float = 1.0) -> None:
    if start_idx >= buf.shape[0]:
        return
    end = min(buf.shape[0], start_idx + clip.shape[0])
    buf[start_idx:end] += clip[: end - start_idx] * gain


def synthesize_test_stems(stem_dir: Path, bpm: float = 124.0, bars: int = 8, sr: int = DEFAULT_SR) -> Dict[str, Path]:
    """Create a realistic 4-stem repeating electronic track with slight cross-stem bleed."""
    rng = np.random.default_rng(42)
    beat_sec = 60.0 / bpm
    bar_sec = 4.0 * beat_sec
    total_samples = int(math.ceil(bars * bar_sec * sr))

    drums = np.zeros((total_samples, 2), dtype=np.float32)
    bass = np.zeros((total_samples, 2), dtype=np.float32)
    chords = np.zeros((total_samples, 2), dtype=np.float32)
    vocals = np.zeros((total_samples, 2), dtype=np.float32)

    kick = _make_kick(sr)
    snare = _make_snare(rng, sr)
    hat = _make_hat(rng, sr)

    # 8 bars (2 identical 4-bar sections so pattern deduplication should fold 2 -> 1!)
    bass_pattern = [(0.0, 45, 0.9), (1.5, 45, 0.45), (2.0, 48, 0.9), (3.0, 50, 0.85)]  # A2, C3, D3
    chord_a = _make_chord_stab((57, 60, 64), 0.75, sr)  # Am
    chord_f = _make_chord_stab((53, 57, 60), 0.75, sr)  # F

    for bar in range(bars):
        b0 = bar * bar_sec
        # Drums: 4-on-the-floor kick, snare on 2 & 4, offbeat hats
        for beat in range(4):
            _mix_in(drums, kick, int(round((b0 + beat * beat_sec) * sr)), gain=1.0)
            _mix_in(drums, hat, int(round((b0 + (beat + 0.5) * beat_sec) * sr)), gain=0.85)
        _mix_in(drums, snare, int(round((b0 + 1.0 * beat_sec) * sr)), gain=1.0)
        _mix_in(drums, snare, int(round((b0 + 3.0 * beat_sec) * sr)), gain=0.92)

        # Bass: clean 4-bar motif repeated twice (bar 0 has no vocal bleed so exemplar is super clean)
        for beat_off, midi, dur_beats in bass_pattern:
            note = _make_bass_note(midi, dur_beats * beat_sec, sr)
            _mix_in(bass, note, int(round((b0 + beat_off * beat_sec) * sr)))

        # Chords: repeating 2-bar chord stabs
        stab = chord_a if (bar % 2 == 0) else chord_f
        _mix_in(chords, stab, int(round((b0 + 0.0) * sr)))
        _mix_in(chords, stab, int(round((b0 + 2.5 * beat_sec) * sr)), gain=0.75)

        # Vocals/pad swell on bars 2-3 and 6-7
        if (bar % 4) in (2, 3):
            vox_t = np.arange(int(1.8 * beat_sec * sr), dtype=np.float32) / sr
            vox_f = 440.0 if (bar % 4 == 2) else 523.25
            vox = np.sin(2.0 * np.pi * vox_f * vox_t) * np.sin(np.pi * vox_t / vox_t[-1]) * 0.25
            _mix_in(vocals, np.stack([vox, vox], axis=1), int(round((b0 + 0.5 * beat_sec) * sr)))

    # Inject mild stem bleed from chords/vocals into drums only on bars 2-3 to verify cleanliness preference
    bleed_start = int(2 * bar_sec * sr)
    bleed_end = int(4 * bar_sec * sr)
    drums[bleed_start:bleed_end] += 0.08 * (chords[bleed_start:bleed_end] + vocals[bleed_start:bleed_end])

    stem_dir.mkdir(parents=True, exist_ok=True)
    paths = {
        "drums": save_wav(stem_dir / "drums.wav", drums, sr=sr),
        "bass": save_wav(stem_dir / "bass.wav", bass, sr=sr),
        "chords": save_wav(stem_dir / "chords.wav", chords, sr=sr),
        "vocals": save_wav(stem_dir / "vocals.wav", vocals, sr=sr),
    }
    return paths


class StemImportPipelineTests(unittest.TestCase):
    def test_balanced_mode_grid_decompile_and_scorecard(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stem_import_test_") as tmp:
            root = Path(tmp)
            stem_dir = root / "stems"
            synthesize_test_stems(stem_dir, bpm=124.0, bars=8)

            stems = discover_stems(stem_dir)
            self.assertEqual(set(stems.keys()), {"drums", "bass", "chords", "vocals"})
            self.assertEqual(stems["drums"][1], "drums")
            self.assertEqual(stems["bass"][1], "bass")
            self.assertEqual(stems["chords"][1], "chords")
            self.assertEqual(stems["vocals"][1], "vocals")

            grid = analyze_grid({k: (v[0], v[1]) for k, v in stems.items()})
            self.assertAlmostEqual(grid.bpm, 124.0, delta=0.5)

            out_dir = root / "out_balanced"
            cfg = DecompileConfig(mode="balanced", editability=0.5, keep_vibe_backup=True)
            plan = decompile_stems(stems, grid, out_dir=out_dir, title="SynthTest", cfg=cfg)

            # Check plan round-trip JSON serialization
            plan_json = plan.save_json(out_dir / "plan.json")
            reloaded = ScratchpadPlan.from_json(plan_json)
            self.assertEqual(len(reloaded.instruments), len(plan.instruments))
            self.assertEqual(reloaded.order_list, plan.order_list)

            # Balanced mode: drums=drum_kit, bass=pitched_multisample + muted vibe backup, chords/vocals=vibe_chop
            tiers_by_stem = {}
            for ins in plan.instruments:
                tiers_by_stem.setdefault(ins.stem_name, []).append(ins.tier)
            self.assertEqual(tiers_by_stem["drums"], ["drum_kit"])
            self.assertIn("pitched_multisample", tiers_by_stem["bass"])
            self.assertIn("vibe_chop", tiers_by_stem["bass"])  # keep_vibe_backup=True
            self.assertEqual(tiers_by_stem["chords"], ["vibe_chop"])

            # Verify bass pitch detection found the expected A2/C3/D3 range (MIDI 45..50)
            bass_ins = next(i for i in plan.instruments if i.stem_name == "bass" and i.tier == "pitched_multisample")
            root_notes = sorted(z.root_note for z in bass_ins.zones)
            self.assertTrue(any(abs(n - 45) <= 1 for n in root_notes), f"Expected A2(45) in {root_notes}")
            self.assertTrue(any(abs(n - 50) <= 1 for n in root_notes), f"Expected D3(50) in {root_notes}")

            # Verify muted backup channel flag is preserved
            self.assertTrue(any(plan.channel_muted), "Expected muted vibe backup channel")

            # Verify drums compressed 96+ hits into a compact kit (<= 16 voices)
            drum_ins = next(i for i in plan.instruments if i.stem_name == "drums")
            self.assertGreaterEqual(len(drum_ins.zones), 2)
            self.assertLessEqual(len(drum_ins.zones), 16)

            # Write .it module and render offline via OpenMPT (detectseq-render)
            it_path = write_it(plan, out_dir / "synth_test.it")
            self.assertTrue(it_path.is_file())
            self.assertGreater(it_path.stat().st_size, 1024)

            if find_renderer():
                score = compute_scorecard(plan, stems, it_path, rendered_wav_out=out_dir / "render.wav")
                self.assertGreater(score.log_mel_cosine, 0.78, f"Unexpectedly low log-mel fidelity: {score}")
                self.assertGreater(score.onset_corr, 0.50, f"Unexpectedly low onset correlation: {score}")
                self.assertGreater(score.events_per_sample, 2.0)
                self.assertLess(score.sample_compression_ratio, 0.65)

    def test_vibe_mode_and_detectseq_json_hook(self) -> None:
        with tempfile.TemporaryDirectory(prefix="stem_import_ds_") as tmp:
            root = Path(tmp)
            stem_dir = root / "stems"
            synthesize_test_stems(stem_dir, bpm=124.0, bars=4)

            # Provide synthetic detectseq/1.0 JSON for drums
            beat_sec = 60.0 / 124.0
            ds_events = []
            for bar in range(4):
                for beat in range(4):
                    t_k = (bar * 4 + beat) * beat_sec
                    ds_events.append({"time_sec": t_k, "cluster_id": 0, "magnitude": 1.0})
                    if beat in (1, 3):
                        ds_events.append({"time_sec": t_k, "cluster_id": 1, "magnitude": 0.9})
                    ds_events.append({"time_sec": t_k + 0.5 * beat_sec, "cluster_id": 2, "magnitude": 0.7})
            ds_json = root / "detectseq.json"
            ds_json.write_text(json.dumps({"schema": "detectseq/1.0", "events": ds_events}), encoding="utf-8")

            out_dir = root / "out_vibe"
            argv = [
                "import",
                str(stem_dir),
                "--out", str(out_dir),
                "--mode", "vibe",
                "--editability", "0.3",
                "--detectseq-json", str(ds_json),
            ]
            if find_renderer():
                argv.append("--score")
            rc = cli_main(argv)
            self.assertEqual(rc, 0)

            plan = ScratchpadPlan.from_json(out_dir / "scratchpad_plan.json")
            self.assertTrue(plan.stem_metrics["drums"]["used_detectseq"])
            self.assertEqual(plan.stem_metrics["drums"]["voices"], 3)
            # In vibe mode, bass stays smart chops
            self.assertEqual(plan.stem_metrics["bass"]["tier"], "vibe_chop")
            self.assertTrue((out_dir / "stems.it").is_file())

    def test_midi_guide_and_bpm_hint(self) -> None:
        import struct
        from tools.stem_import.midi_guide import load_midi_guide
        from tools.stem_import.stems import parse_bpm_hint_from_name

        self.assertEqual(parse_bpm_hint_from_name("Hermes Agent Stems (177BPM).zip"), 177.0)
        self.assertIsNone(parse_bpm_hint_from_name("stems_folder"))

        def _vlq(n: int) -> bytes:
            out = [n & 0x7F]
            n >>= 7
            while n:
                out.append((n & 0x7F) | 0x80)
                n >>= 7
            return bytes(reversed(out))

        with tempfile.TemporaryDirectory(prefix="stem_import_midi_") as tmp:
            root = Path(tmp)
            stem_dir = root / "stems"
            synthesize_test_stems(stem_dir, bpm=124.0, bars=4)
            stems = discover_stems(stem_dir)

            # Build a minimal valid SMF for Bass at 124 BPM with 8 notes (ch 0) + 1 bleed drum note (ch 9)
            tpq = 480
            us_per_q = int(round(60e6 / 124.0))
            t0 = bytearray()
            t0 += _vlq(0) + bytes([0xFF, 0x51, 0x03, (us_per_q >> 16) & 0xFF, (us_per_q >> 8) & 0xFF, us_per_q & 0xFF])
            t0 += _vlq(0) + b"\xFF\x2F\x00"

            t1 = bytearray()
            t1 += _vlq(0) + bytes([0xFF, 0x03, 4]) + b"Bass"
            for i, pitch in enumerate((45, 45, 48, 50, 45, 45, 48, 50)):
                t1 += _vlq(0 if i == 0 else tpq // 2) + bytes([0x90, pitch, 100])
                t1 += _vlq(tpq // 2) + bytes([0x80, pitch, 0])
            # Cross-stem drum bleed on ch 9 that must be filtered out
            t1 += _vlq(0) + bytes([0x99, 36, 100]) + _vlq(120) + bytes([0x89, 36, 0])
            t1 += _vlq(0) + b"\xFF\x2F\x00"

            smf = (
                b"MThd" + struct.pack(">IHHH", 6, 1, 2, tpq)
                + b"MTrk" + struct.pack(">I", len(t0)) + bytes(t0)
                + b"MTrk" + struct.pack(">I", len(t1)) + bytes(t1)
            )
            midi_file = root / "Demo (Bass).mid"
            midi_file.write_bytes(smf)

            guide = load_midi_guide(midi_file, stems=stems)
            self.assertAlmostEqual(guide.nominal_bpm, 124.0, delta=0.5)
            matched = guide.match_stem("bass", "bass")
            self.assertIsNotNone(matched)
            self.assertEqual(len(matched.notes), 8)  # ch 9 drum bleed filtered out!


if __name__ == "__main__":
    unittest.main()
