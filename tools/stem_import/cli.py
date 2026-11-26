"""CLI entry point for ScreamSeq's experimental stem-to-tracker prototype."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List

from .audio import DEFAULT_SR
from .decompile import DecompileConfig, decompile_stems
from .grid import analyze_grid
from .it_writer import write_it
from .midi_guide import load_midi_guide
from .scorecard import compute_scorecard
from .screamseq_bridge import build_api_payload_preview, push_plan_to_screamseq
from .stems import discover_stems, parse_bpm_hint_from_name


def _parse_role_overrides(items: List[str] | None) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for item in items or []:
        if "=" not in item:
            raise ValueError(f"Expected NAME=ROLE for --role, got: {item}")
        k, v = item.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python3 -m tools.stem_import",
        description="Experimental stem-to-ScreamSeq decompiler prototype (Vibe Lock + Promote).",
    )
    sub = p.add_subparsers(dest="command", required=True)

    imp = sub.add_parser("import", help="Decompile a folder of stems (or single mix) into a playable .it + plan.")
    imp.add_argument("input", help="Directory of stem audio files, or a single audio file.")
    imp.add_argument("--out", "-o", required=True, help="Output directory for samples, plan JSON, .it, and scorecard.")
    imp.add_argument("--title", default=None, help="Song title (defaults to input folder/file name).")
    imp.add_argument("--mode", choices=("vibe", "balanced", "decomposed"), default="balanced",
                     help="vibe: clustered drums + smart chops; balanced: promotes bass; decomposed: promotes bass+melodic.")
    imp.add_argument("--editability", type=float, default=0.5,
                     help="0.0 (max vibe/fidelity) to 1.0 (max sample reuse & decomposition).")
    imp.add_argument("--promote", default="", help="Comma-separated stem names or roles to promote to pitched multisamples.")
    imp.add_argument("--keep-vibe-backup", action="store_true",
                     help="Keep a muted Tier-1 smart-chop backup track alongside each promoted stem.")
    imp.add_argument("--role", action="append", default=[],
                     help="Override stem role classification, e.g. --role synth=melodic (repeatable).")
    imp.add_argument("--bpm", type=float, default=None, help="Explicit BPM override (auto-detected if omitted).")
    imp.add_argument("--rows-per-beat", type=int, default=4, help="Tracker rows per beat (default 4 = 16th notes).")
    imp.add_argument("--chop-beats", type=float, default=0.0, help="Override smart-chop length in beats (0=auto).")
    imp.add_argument("--max-samples", type=int, default=240, help="Total sample budget across all stems (<=255 for .it).")
    imp.add_argument("--detectseq-json", default=None, help="Optional detectseq/1.0 JSON for drum hit extraction.")
    imp.add_argument("--midi", default=None, help="Optional Suno MIDI file, folder, or .zip archive for tempo + note guidance.")
    imp.add_argument("--separate", choices=("demucs", "api"), default=None,
                     help="Optional stem separation backend when input is a single full mix.")
    imp.add_argument("--sep-api-url", default=None, help="HTTP endpoint for --separate api.")
    imp.add_argument("--score", action="store_true", help="Render generated .it offline and print baseline scorecard.")
    imp.add_argument("--renderer", default=None, help="Path to detectseq-render or openmpt123 for --score.")
    imp.add_argument("--push-api", action="store_true", help="Send multisample import preview to live ScreamSeq API.")
    imp.add_argument("--apply-api", action="store_true", help="Actually commit --push-api (default is dry-run).")
    imp.add_argument("--pid", type=int, default=None, help="Target ScreamSeq PID for --push-api.")
    imp.add_argument("--socket", default=None, help="Target ScreamSeq Unix socket for --push-api.")
    return p


def _print_summary(plan, it_path: Path, score=None) -> None:
    print(f"\n=== ScreamSeq Stem Import ({plan.mode}, editability={plan.editability:.2f}) ===")
    print(
        f"BPM: {plan.bpm:.2f} (conf={plan.grid_diagnostics['bpm_confidence']:.2f}, "
        f"med_drift={plan.grid_diagnostics['median_abs_drift_ms']:.1f} ms) | "
        f"Offset: {plan.beat_offset_sec*1000:.1f} ms | "
        f"Patterns: {len(plan.patterns)} unique / {len(plan.order_list)} orders"
    )
    print(f"Module: {it_path}")
    print("\nInstruments:")
    for ins in plan.instruments:
        tot_dur = sum(z.duration_sec for z in ins.zones)
        print(
            f"  [{ins.instrument_id:02d}] {ins.name:<20s} role={ins.role:<8s} tier={ins.tier:<19s} "
            f"samples={len(ins.zones):>3d} ({tot_dur:.2f}s)"
        )
    if score is not None:
        print("\n=== Baseline Scorecard (Offline OpenMPT Render vs. Original Mix) ===")
        print(
            f"  Fidelity   : {score.fidelity_score:5.1f}/100  "
            f"(log-mel cos={score.log_mel_cosine:.3f}, onset corr={score.onset_corr:.3f}, "
            f"MR-STFT L1={score.mr_stft_l1:.3f}, lvl={score.loudness_ratio_db:+.1f} dB)"
        )
        print(
            f"  Editability: {score.editability_score:5.1f}/100  "
            f"(samples={score.total_unique_samples}, events/sample={score.events_per_sample:.1f}x, "
            f"comp={score.sample_compression_ratio:.3f}, max_slice={score.max_sample_sec:.2f}s, "
            f"pat_reuse={score.unique_patterns}/{score.total_orders})"
        )
        print(f"  Composite  : {score.composite_score:5.1f}/100")


def main(argv: List[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "import":
        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        overrides = _parse_role_overrides(args.role)
        stems = discover_stems(
            args.input,
            role_overrides=overrides,
            sr=DEFAULT_SR,
            sep_backend=args.separate,
            sep_api_url=args.sep_api_url,
            work_dir=out_dir,
        )
        midi_guide = None
        if args.midi:
            midi_guide = load_midi_guide(args.midi, stems=stems, sr=DEFAULT_SR, extract_dir=out_dir)
            print(
                f"MIDI guide loaded: {len(midi_guide.stems)} tracks, "
                f"nominal={midi_guide.nominal_bpm:.2f} BPM (median={midi_guide.tempo_median_bpm:.2f}), "
                f"audio_align: scale={midi_guide.time_scale:.6f}, offset={midi_guide.time_offset_sec*1000:+.1f} ms"
            )

        bpm_hint = (midi_guide.nominal_bpm if midi_guide else None) or parse_bpm_hint_from_name(args.input)
        grid = analyze_grid(
            {k: (v[0], v[1]) for k, v in stems.items()},
            sr=DEFAULT_SR,
            bpm_override=args.bpm,
            bpm_hint=bpm_hint,
            rows_per_beat=args.rows_per_beat,
        )
        promote_list = tuple(s.strip() for s in args.promote.split(",") if s.strip())
        cfg = DecompileConfig(
            mode=args.mode,
            editability=max(0.0, min(1.0, args.editability)),
            promote_stems=promote_list,
            keep_vibe_backup=args.keep_vibe_backup,
            max_total_samples=max(16, min(255, args.max_samples)),
            detectseq_json=args.detectseq_json,
            chop_beats=args.chop_beats,
            midi_guide=midi_guide,
        )
        title = args.title or Path(args.input).stem
        plan = decompile_stems(stems, grid, out_dir=out_dir, title=title, sr=DEFAULT_SR, cfg=cfg)
        plan_path = plan.save_json(out_dir / "scratchpad_plan.json")
        it_path = write_it(plan, out_dir / f"{Path(args.input).stem}.it")

        score = None
        if args.score:
            score = compute_scorecard(
                plan,
                original_stems=stems,
                it_path=it_path,
                rendered_wav_out=out_dir / "rendered_mix.wav",
                renderer=args.renderer,
            )
            score.save_json(out_dir / "scorecard.json")

        if args.push_api:
            res = push_plan_to_screamseq(
                plan,
                socket_path=args.socket,
                pid=args.pid,
                dry_run=not args.apply_api,
            )
            print(f"API push ({'applied' if args.apply_api else 'dry-run'}): {len(res['imported'])} instruments")
        else:
            _ = build_api_payload_preview(plan)

        _print_summary(plan, it_path, score=score)
        print(f"Plan JSON: {plan_path}")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
