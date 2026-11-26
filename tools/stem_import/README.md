# `tools/stem_import` — Experimental Stem-to-ScreamSeq Prototype

Lightweight, pure-Python + NumPy (+ `ffmpeg`) baseline for turning downloaded
stems (Suno exports, Demucs separations, or multi-track renders) into playable
multi-sample ScreamSeq / Impulse Tracker (`.it`) scratchpads with an automated
**Audio Fidelity vs. MDL Editability** scorecard.

## Two-Tier Progressive Workflow

1. **Tier 1 — Vibe Lock (`--mode vibe`)**
   - **Drums**: Multi-band polyphonic onset detection (or direct `detectseq/1.0`
     JSON ingestion via `--detectseq-json`) + timbre clustering + cross-stem
     bleed-aware cleanest-exemplar extraction $\rightarrow$ reusable drum kit.
   - **Bass / Chords / Melodic / Vocals**: Beat/bar-aligned smart chops with
     chroma + log-mel deduplication so repeating loops reuse the same samples
     and fold into repeated patterns in the order list.
2. **Tier 2 — Promote (`--mode balanced` / `--mode decomposed` / `--promote ...`)**
   - Promotes selected monophonic/pitched stems (e.g. `bass`, `melodic`) via
     YIN pitch tracking into **chromatic playable multi-sample instruments**
     (midpoint keymaps matching ScreamSeq's `importMultisample`).
   - Pass `--keep-vibe-backup` to retain the Tier-1 smart-chop track muted
     alongside the promoted track for bar-by-bar A/B blending.

## Quick Start (WSL / Linux / macOS)

```bash
# Import a folder or .zip of downloaded stems (optionally with Suno's MIDI .zip!)
python3 -m tools.stem_import import "/path/to/My Song Stems (177BPM).zip" \
  --midi "/path/to/My Song MIDI.zip" \
  --out /tmp/my_scratchpad \
  --mode balanced \
  --editability 0.5 \
  --keep-vibe-backup \
  --score

# With an existing detectseq/1.0 drum extraction JSON:
python3 -m tools.stem_import import /path/to/stems \
  --out /tmp/my_scratchpad \
  --detectseq-json /path/to/drums_detectseq.json \
  --score

# Starting from a single unseparated full-mix file (uses local demucs or HTTP API):
python3 -m tools.stem_import import /path/to/suno_track.mp3 \
  --separate demucs \
  --out /tmp/my_scratchpad \
  --score
```

Outputs written to `--out`:
- `samples/*.wav`: normalized, zero-crossing-snapped, micro-faded extracted slices
- `scratchpad_plan.json`: intermediate `ScratchpadPlan` IR (input/target for future ML/RL)
- `<name>.it`: OpenMPT / ScreamSeq-ready Impulse Tracker 2.14 module (stereo 16-bit PCM, multi-sample keymaps, `SDx`/`SCx` sub-row timing)
- `rendered_mix.wav` + `scorecard.json` (when `--score` is passed)

## Verification

```bash
python3 -m unittest discover -s tools/stem_import/tests -v
```
