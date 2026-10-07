# Precise pattern effects — 6 October 2026

This checkpoint implements the requested high-resolution effect families and direct pattern parameter editing. It also retains the preceding fixes for plugin editor geometry, Dot/Shift-Delete editing and voice-local BS/BL playback.

## Musical behavior

There are 21 new typed commands, alongside PS/PL, BS/BL, NC and NF/NR:

| Area | Commands |
| --- | --- |
| Gain and pan | GS set gain, GL slide gain, PN set pan, PA slide pan |
| Pitch | PR relative pitch, PT tone portamento, VB vibrato, AR arpeggio |
| Modulation | TM tremolo, PB panbrello, TR tremor |
| Notes | RT retrigger, NO release, ND delay |
| Samples and envelopes | SO sample offset, DR direction, EP envelope position, EN envelope enable |
| Song timing | TS decimal tempo, TL continuous tempo slide, RL row length |

Continuous numeric parameters retain double precision; counts, selectors and switches remain discrete. Event placement uses 65,536 integer units per row and resolves to audio samples. Sample modulation and interpolation run below tracker-tick resolution. Tempo ramps use an integrated musical clock rather than extra tracker ticks. New sample onsets reset voice-local controls; retained NNA tails retain theirs. Retriggers support gain factors/steps and actual plugin note delivery.

The [precision map](NATIVE_PATTERN_PRECISION.md) accounts for all 79 engine effect definitions and 14 volume-column definitions. Combined legacy effects use independent FX columns. Discrete selectors and imported hardware-specific operations retain their original meaning; they do not acquire fictitious fractional IDs. Existing module playback is unchanged when native extensions are absent.

Sample gain/pan/pitch/envelope controls affect native sample voices. For plugin sound controls, PS/PL target stable parameters and BS/BL use the supported pitch-wheel protocol. Plugin note events retain MIDI velocity and pitch precision limits.

## Editing

On macOS:

- FX cells have aligned parameter slots; slash separators are gone. Widths depend on the commands actually present in that pattern and FX column. Empty/legacy columns remain compact. Strong channel rails are separate from lighter internal FX dividers.
- Click a parameter, press Return, or type to edit it in place. Left/Right chooses a slot; Tab/Shift-Tab navigates an open draft. Return commits one command and one Undo step; Escape cancels. Untouched values retain their original precision.
- Beats are the default. Right-click the grid → **Effect timing units → Rows** changes display and editing units without changing the stored song. Beat-based modulation rates convert to cycles per row; Hz remains Hz.
- Common parameters are visible by default. **Edit all effect parameters inline…** expands the same draft for waveform, phase, reset and other advanced settings. It preserves unsaved text and the captured cell/revision.
- Press **?** in an FX slot to find an effect. Legacy names also find the corresponding native effects.
- Dot clears the chosen field. Shift-Delete removes the current channel row and shifts that channel's following content up.

Windows consumes the same compact slot descriptions and beat/row conversion. Its Pattern FX inspector provides the timing-unit chooser and advanced fields; the macOS draft-expansion menu is not a Windows gesture.

All new commands have shared validation, atomic API edits, Undo/Redo, copy/paste/transform handling and native project persistence. Read the `native` array returned by `pattern.commands` for field names, units, ranges, defaults and scope. Edit a cell with `pattern.effect.set`; its `kind:"native"` command names the operation and its `parameters` object. Both native frontends consume the shared catalogue.

## Qualification

The build and local evidence are in `bin/mac-precision-fx/`. The musician's existing `bin/mac-graph-fluency` process was preserved; UI checks used a separate bundle identity and disposable song.

- Full CTest suite: **108/108 passed** before the final presentation-only compactness/search/unit-display follow-ups. All five affected catalogue/runtime, grid and portable Windows suites passed afterward.
- Focused tests: native catalogue/runtime, decimal tempo and seek/length clock agreement, delayed row-note effects, AU/VST3 retrigger PCM/velocity, pitch/nudges, persistence and portable Windows grid helpers.
- Native rendering was exercised at 44.1, 48 and 96 kHz with 1/17/128/4096-frame partitions. Dedicated delayed-note tests use 48 kHz. Timing and native runtime audits detected no intercepted allocations, frees or locks.
- **30 stock-engine renders were bit-exact**, with zero intercepted allocations, frees or mutex calls. The oracle is pinned upstream revision `f83cedb0cd5446e4dfaa83ac97e3087107e26767`.
- The actual local socket suite passed. A real AppKit walkthrough verified beat-duration editing, exact preservation of strength/onset, one-step Undo and the equivalent row display. It also verified rate conversion (2 cycles/beat = 0.5 cycles/row at four rows/beat), preservation of advanced settings, expansion of an unsaved inline draft, Escape cancellation, and correct `0J` / `AR` results in the effect finder.
- A 32-voice offline benchmark at 48 kHz measured five seconds per case after warmup, with no deadline misses or audited callback allocation/free/lock operations:

| Workload | Buffer | p99 | Maximum | Deadline |
| --- | ---: | ---: | ---: | ---: |
| Gain + pan | 128 | 0.077 ms | 0.149 ms | 2.667 ms |
| Gain + pan | 512 | 0.243 ms | 0.269 ms | 10.667 ms |
| Gain + pan + vibrato | 128 | 0.250 ms | 0.385 ms | 2.667 ms |
| Gain + pan + vibrato | 512 | 0.936 ms | 1.100 ms | 10.667 ms |

These are bounded offline measurements, not a promise about commercial-plugin capacity or physical device latency.

### Visible UI and presentation

Screenshots from the separate QA application:

- [Aligned parameter slots and channel rails](../bin/mac-precision-fx/qualification/precision-slots.png)
- [Expanded inline parameter editor](../bin/mac-precision-fx/qualification/precision-inline-expanded.png)
- [Effect finder](../bin/mac-precision-fx/qualification/effect-finder.png)

The initial 60-second native-effect and dense-pattern runs had no missed display presentations or audio overruns, but failed the unchanged 34 ms geometry-freshness gate (49.83 ms and 35.43 ms respectively). Traces showed delayed/coalesced main UI updates; one native-fixture gap included a 24.22 ms AppKit layout. The renderer now requests a coalesced full host update after one display cadence, rechecking freshness when that request reaches the main thread. This advances playback state before publishing geometry; it does not simply restamp an old playhead. Focused regressions cover coalescing, a normal timer winning the race, pause/stop, retired requests and standalone views.

The final AppKit suite passes, including the recovery regressions. After the scheduling correction, the first native run encountered one shared 105–111 ms main/render scheduling hiatus with no long traced application operation. It failed presentation and geometry gates. A subsequent native run with run-loop tracing passed all gates. The dense run still failed geometry freshness: a 69.84 ms snapshot gap partly overlapped a 23.52 ms AppKit layout, with the remaining delay unclassified. Neither trace justifies blaming a particular inspector or the operating system. No threshold was relaxed.

| Final visible workload | Presented samples | Missed presentations | Max presentation interval | Max snapshot age | CPU / GPU p99 | Audio overruns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Native effects, 60 s (repeat with run-loop tracing) | 3475 | 0 | 25.00 ms | 19.00 ms | 0.350 / 1.230 ms | 0 |
| 127-channel dense pattern, 60 s | 3476 | 0 | 24.99 ms | **69.84 ms — fails 34 ms gate** | 0.540 / 1.477 ms | 0 |

Both used explicit BlackHole 2ch output at 48 kHz / 512 frames. Audio callback p99.9 was 0.910 ms and 3.730 ms respectively. **Overall strict UI performance qualification remains incomplete** because of the dense geometry failure and the earlier intermittent scheduling hiatus. The implemented features and functional tests pass; this remaining UI scheduling investigation is separate follow-up work.

Initial reports and traces are retained under `bin/mac-precision-fx/qualification/evidence/`, alongside `recovery-native`, `recovery-dense` and `final-native` evidence. [Verification manifest](../bin/mac-precision-fx/qualification/evidence/verification.json) records the final results. All disposable QA processes were closed; the musician's original process remains running.

### Build identity

- Bundle: `bin/mac-precision-fx/ScreamSeq.app`
- Base revision: `b7c9c0feabe9a9c9192f2825cc969a583867e6a2`
- Source fingerprint: `cd8d35212f032015de46dacf6935d802d864d061404cca50da4ab2d3f92c6694`
- Executable SHA-256: `619e7f84c2dddcb1f90e8961114930406db090a6e1dea1c9a431b641edcb7fc1`
- Every recorded source hash matches the working tree; the packaged bundle passes strict deep signature verification. Changes remain uncommitted.

## Boundaries

- Starting partway through a pattern does not reconstruct preceding native sample-effect state. Native tempo seeking is covered independently; this is not complete sample-voice pre-roll reconstruction.
- Pattern FX edits currently stop playback through the existing editing API. This task does not claim a new seamless live publication path for those edits.
- The Mac build and portable Windows model/codec/field helpers are qualified locally. Native Windows desktop/device execution still needs Windows CI or a Windows machine; no new CI run or push is claimed here.
- Existing instrument scripts are baked into bounded envelope points before playback. EP seeks a fractional position in those same curves; EN is a discrete enable switch. Neither parses scripts on the audio thread.
