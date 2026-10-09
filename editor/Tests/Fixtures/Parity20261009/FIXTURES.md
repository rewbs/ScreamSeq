# Portable UI fixtures

Use [04-complete-reference.screamseq](fixtures/04-complete-reference.screamseq) for the broadest comparison. All five files reopen in the Mac app and round-trip with identical property trees. Windows app execution still needs verification. These are test songs for UI inspection, not a mix or audio benchmark.

| File | Intended use | State |
|---|---|---|
| [01-midnight-circuit](fixtures/01-midnight-circuit.screamseq) | Baseline, empty and default states | Built-in demo; 8 channels, 4 samples, no sample instruments or native rack plugins |
| [02-editors-and-effects](fixtures/02-editors-and-effects.screamseq) | Pattern, sample, instrument, scratch and automation editors | Generated audio, named tracks/orders, fractional notes, extra FX columns, 15 built-ins, scripted parameter envelope |
| [03-routing-and-modulation](fixtures/03-routing-and-modulation.screamseq) | Initial graph and trim inspection | Adds mixer buses, reusable recipe, LFO at zero depth, graph automation, linked trims and pattern graph commands |
| [04-complete-reference](fixtures/04-complete-reference.screamseq) | Recommended full reference | Adds linked envelope-bank template, meaningful ±0.2 modulation depth, named graph processor, drum group routing and a return send |

| [05-arrangement-copy-after](fixtures/05-arrangement-copy-after.screamseq) | Current-main arrangement copy result | Copy INTRO / Pulse to RETURN / Spare; destination becomes independent P002 and includes precise notes plus all FX columns |

## Exact state for fixture 04

Indices in this section distinguish UI numbering from API numbering. API pattern/channel indices are zero-based; sample/instrument indices are one-based. Stable IDs are included to make cross-platform inspection unambiguous.

| Object | State to find |
|---|---|
| Song | Midnight Circuit; modern tempo 124.5 BPM; 6 ticks/row; 4 rows/beat; 16 rows/bar; groove `[1.12, 0.88, 1.12, 0.88]` |
| Channels | UI 1–8: Pulse, Sub, Kick, Hat, Scratch, Chords A, Chords B, Spare; stable IDs n2–n9 |
| Patterns | API P0/n1 “Intro / precise hits”; P1/n27 “Variation / scratch”; 64 rows each |
| Orders | P0, P1, P0; section labels INTRO, VARIATION, RETURN |
| Note track | Chords A + Chords B grouped as Polyphonic chords; mixer group n30 |
| Precise notes | P0 row 0/channel 0: notes 49, 56, 61, 56 (C-4, G-4, C-5, G-4); instrument 1; velocities 98, 79, 110, 61; positions 0, 16384, 32768, 49152 in 1/65536-row units |
| Precise-note display | Row offsets 0, 0.25, 0.5, 0.75; beat offsets 0, 0.0625, 0.125, 0.1875. Pattern 1 was duplicated after these were authored, so eight events exist across the song |
| Extra FX | Channel 0 has 3 columns; channel 4 has 2. P0/channel 0: row 4 FX2 pitch-slide +2.5 semitones for 2 rows; row 8 FX3 nudge-forward 35% for 1 beat; row 12 FX2 parameter-set binding 1 to 60% |
| Binding | ID 1, “DC offset enable”, points to rack DC Offset parameter 0 |
| Scratch | Phrase 1 “Parity two-click flare”; P0 row 0/channel 4 has SK with instrument 5, 4 beats, 120 ms travel, two repeats, forward; row 16 has SX |
| Generated samples | Samples 5–7 derive from Parity_C3.wav, Parity_G3.wav, Parity_C4.wav; each mono 24 kHz, signed 16-bit, 1 second. Sample 5 renamed “Parity loop / scratch source”, volume 48, forward loop frames 2400–19200 |
| Instrument 1/n20 | “Pulse with ADSR”; volume points `(0,0), (4,64), (16,40), (40,0)`; sustain node 2; pan points `(0,16), (8,48), (24,32)` enabled |
| Rack slots 1–15 | Gainer, DC Offset, Stereo Expander, Digital Filter, EQ5, EQ10, Mixer EQ, Comb Filter, Distortion, LofiMat, Cabinet Simulator, Compressor, Gate, Maximizer, Bus Compressor |
| Parameter automation | P0, rack Gainer, parameter ID 1 Gain. Positions 0/4096/8192/12288 in 1/256-row units = rows 0/16/32/48. Values 0.35/0.8/0.45/0.65; shapes smooth/linear/scripted/linear |
| Formula | Row-32 outgoing segment: `start+(end-start)*t + 0.06*sin(t*pi*4)` |
| Recipe | n34, number 1, “Pulse movement”; five nodes: input n35, output n36, Gain processor n37 (built-in Gainer), Slow pulse LFO n38, Pattern motion automation n39 |
| Audio graph | n35 output 0 → n37 input 0 → n36 input 0; unity audio cable gains |
| Modulation | n38 → n37 parameter 1; base 0.8, min −0.2, max +0.2, enabled, not quantized; LFO rate 1, phase 0 |
| Graph automation | n39/P0 curve: position/value 0/0.1 smooth, 4096/0.9 smooth, 8192/0.25 linear |
| Trims | n37 input i:0 −6 dB, output o:0 +6 dB, inverse link. Pattern motion adds 0…3 dB to the input trim, with inverse compensation on linked output |
| Graph assignment | Pulse/n2 ordinary assignment n34, Amount 70%, Wet 80% |
| Graph lanes | Pulse has two lanes. P0 row 0/lane 1 start n34 70%/80%; row 16/lane 2 Amount 35%; row 32/lane 1 stop n34 with tails |
| Buses | 8 tracks, Master n12, Polyphonic chords n30, Rhythm group n32, Parallel texture return n33 |
| Group routing | Kick/n4 and Hat/n5 output to Rhythm group; Chords A/B output to Polyphonic chords; groups feed Master |
| Send | Pulse → Parallel texture, −12 dB, post-fader, enabled; return fader −6 dB and pan +0.2 |
| Envelope bank | Entry n40 “Parity ADSR template”, one linked volume-envelope use on instrument n20; captured tick span 41 |

Fixture 03 has the same recipe before its processor was named and before LFO depth, drum routing, send or bank were added. That is intentional: screenshots 29–34 show this earlier state. Fixture 02 has the editor/effect content without the later graph topology. Stable plugin instance UUIDs are saved in the files and API snapshots; use them rather than numeric rack positions after reordering.

## Sample library and multisample import

Add the supplied `fixtures/audio` folder to the sample browser and search `Parity`. Select `Parity_C3.wav`: the Mac browser detects three note variations. Choose “Import as one instrument…” in a normal disposable app session to inspect the root-note/range preview. This sheet is deliberately suppressed in `--inspection`; the gallery includes its isolated component reference.

These WAVs were generated mathematically from sinusoids, a third harmonic and an exponential decay. They contain no recordings or third-party material. Their PCM is embedded after import, so normal project loading does not depend on their filesystem paths. The fixtures do not use AU or VST3 plugins.

## Recreate and validate

The native files are the handoff; recreation is optional. Run the Mac scripts from a checkout containing this report at its repository location. Start a separate app executable with `--inspection --automation`, confirm its PID, and use **Open Demo** before running the first script. It modifies that disposable document and writes the four report fixtures.

```sh
python3 doc/mac-ui-reference/2026-10-08/tools/build_fixtures.py --pid YOUR_QA_PID
python3 doc/mac-ui-reference/2026-10-08/tools/enrich_fixture.py --pid YOUR_QA_PID
python3 doc/mac-ui-reference/2026-10-08/tools/validate_fixtures.py --write-manifest
```

The first script saves 01, 02 and 03 in sequence. The second starts from the resulting 03 state and saves 04. If using a fresh app, open 03 before enrichment. API writes use current revision guards and shared editing methods. Plugin UUIDs/revisions may differ on regeneration; regenerate the manifest intentionally rather than treating new bytes as the original captured files.

From an extracted package on either OS:

```sh
python tools/validate_fixtures.py
```

This checks structural invariants and exact fixture/WAV checksums using only the Python standard library. It is not a native Windows load test.

For actual Mac reopen/save checks, pass the path to a disposable QA executable:

```sh
python3 doc/mac-ui-reference/2026-10-08/tools/verify_mac_reopen.py \
  --executable bin/mac-ui-library/ScreamSeq-UI-Library.app/Contents/MacOS/ScreamSeq
```

For component references, from the repository root:

```sh
SCREAMSEQ_BUILD_DIR=bin/mac-ui-library \
  bash doc/mac-ui-reference/2026-10-08/tools/capture-components.sh \
  --snapshots doc/mac-ui-reference/2026-10-08/screenshots/components
```

This report-local wrapper adds the missing GraphTrimControls compilation input and creates a temporary component-only test entry point in the build directory. It preserves layout assertions and explicitly does not claim a full-interface-suite pass. The synthetic component data resides in `mac/Tests/InterfaceTests.swift::layoutChecks` and its called checks; those stress states are not interchangeable with a loadable song.

Rebuild the offline gallery with Python plus Pillow:

```sh
python3 doc/mac-ui-reference/2026-10-08/tools/build_gallery.py
```

The file checksums and exact counts are in [fixtures/manifest.json](fixtures/manifest.json). Mac load/save observations are in [evidence/mac-reopen-validation.json](evidence/mac-reopen-validation.json). Do not use a module-only export to transfer the complete native graph/envelope/automation state.

## Current-main arrangement-copy fixture (05)

Start from fixture 04. In Arrangement Matrix, select INTRO / Pulse (API order 0, channel 0), click Copy block, select RETURN / Spare (order 2, channel 7), choose Overwrite, then Paste block. Open fixture 05 to inspect the saved result directly.

The default `makeUnique:true` changes only RETURN to new pattern 2; INTRO still references pattern 0. The whole destination pattern is cloned before the block is copied, retaining its original Pulse content, timing, automation, graph commands and other channels. Spare receives four precise hits and all three native effect records, and grows to three effect columns. Pattern count becomes three and the song contains sixteen precise-note events.

Equivalent API parameters (all indices zero-based):

```json
{"sourceOrder":0,"targetOrder":2,"sourceChannel":0,"targetChannel":7,"channelCount":1,"mode":"overwrite","makeUnique":true,"clip":false,"expectedRevision":"CURRENT_REVISION"}
```

Call `arrangement.copyBlock` with `dryRun:true` first. `wouldChange` includes native-only edits; `changedCells` counts only ordinary six-field tracker cells. Use the same revision to Apply. One Undo restores fixture 04’s musical state; Redo restores fixture 05. After Undo, copying INTRO / Pulse onto RETURN / Pulse is an alias no-op and must preserve Redo. The supplied `tools/build_arrangement_fixture.py --pid PID` checks these properties against full saved property trees (Undo intentionally retains the allocator high-water mark) and records the responses.

Scratch clarification: the saved phrase command in fixtures 02–05 has `repeats:2`. The earlier report incorrectly described one repeat; the fixture bytes were not changed.

The saved-project comparison found one intentional Undo difference: `native.nextID` stays at 43 rather than reverting to 41, so allocated IDs are never reused across history branches. Every other property, including embedded module/plugin bytes, returns to its original value. Redo restores the complete post-copy tree exactly. The first strict check and its diagnosis are retained in evidence.
