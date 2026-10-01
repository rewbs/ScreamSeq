# Playback, audition and scripted automation

## Playback

| Key | Action |
| --- | --- |
| Space | Play / stop the song |
| Shift–Space | Start the song at the pattern cursor |
| Control–Space | Play selected rows, or the current pattern when no rows are selected |
| Control–Shift–Space | Play that selection/pattern starting at the cursor |

A selection is a dragged or Shift-arrow region, not an ordinary cursor click. Playback includes all channels over the selected rows. If the cursor is outside the selection, playback starts at the selection’s beginning. Starting partway through a looping selection plays its remainder first, then repeats the entire selection.

**Loop**, beside **Follow**, repeats the current playback region. It can be changed during playback and takes effect at the next boundary. Follow only controls scrolling. Selection/pattern boundaries are enforced by the audio engine at row boundaries, including tempo changes; plugin tails may decay after a non-looped region finishes.

Shortcuts work across the workspace. Text entry, ordinary Space on a sample-browser result, control activation and explicitly configured command sequences retain their local actions. The optional Return transport setting remains available. Modified Space shortcuts remain available with that setting.

With the sample or instrument inspector focused, use the normal note keys (Z–M and Q–U by default). The waveform and envelope are suitable focus targets. Text fields and popup type-ahead retain text entry. Audition does not enter pattern data. Releasing a sample key stops its held loop; instruments use their normal release behavior. Changing focus or the selected asset does not lose the original note-off.

## Automation graph

Select a node, then choose **Selected point → next** to change the outgoing segment. Its shape runs until the next node; a final scripted node continues until the pattern ends. Apply saves one document Undo step.

- **+ / −** zoom time around the selected point; **Fit** resets time and value zoom.
- Pinch or Option-scroll zooms time at the pointer.
- Scroll pans time.
- Shift–Option-scroll zooms values at the pointer.

The row and value fields continue to describe actual song coordinates while zoomed.

## Scripted segments

Choose **Scripted…** on a selected node. Edit its formula and inspect the live curve before Apply. Formulas produce a normalized parameter value (0–1), rather than an interpolation weight. The default `mix(start, end, t)` is linear interpolation.

| Variable | Meaning |
| --- | --- |
| `start`, `startValue`, `S` | Selected node’s normalized value |
| `end`, `endValue` | Next node’s value; equals `start` when there is no next node |
| `t`, `beatOffset`, `offset` | Normalized progress through this segment, 0–1 |
| `row` | Current pattern row, including fractional rows |
| `beat` | Current beat position within the pattern |
| `beats` | Beats elapsed since this segment’s start |
| `duration` | Segment duration in beats |
| `startBeat`, `endBeat` | Segment bounds within the pattern |
| `L` | Ordinary linear interpolation between `start` and `end` |
| `pi`, `PI`, `tau`, `e`, `E` | Mathematical constants (`tau = 2*pi`) |

Examples:

```text
mix(start, end, t^3)                       # accelerating transition
mix(start, end, smoothstep(t))             # smooth transition
L + 0.15*sin(tau*beats)                    # one oscillation per beat
start*(1-t)^2                             # final-node fade to zero
mix(start,end,t) + 0.1*(2*noise(beats,17)-1) # deterministic smooth randomness
t < 0.5 ? start : end                     # switch halfway through
```

The `#` explanations above are documentation, not part of the formula syntax.

Operators: `+ - * / % ^ **`, comparisons `< <= > >= == !=`, Boolean `&& || !`, and conditional `condition ? yes : no`. Powers associate right-to-left. Parentheses group expressions.

Functions:

- `sin`, `cos`, `tan`, `abs`, `sqrt`, `exp`, `log`, `log2`, `log10`, `floor`, `ceil`, `round`, `tanh`: one argument; trigonometric arguments use radians.
- `min(a,b)`, `max(a,b)`, `pow(a,b)`, `clamp(value,low,high)`.
- `mix(start,end,weight)` / `lerp(...)`, `smoothstep(t)`, `fract(x)`.
- `tri(phase)`, `saw(phase)`, `square(phase)`: phase measured in cycles; output −1..1.
- `noise(position,seed)`: deterministic smooth value noise, output 0..1.
- `if(condition,yes,no)`: conditional selection.

The expression design takes inspiration from [Parseq’s context variables, interpolation and oscillator functions](https://github.com/rewbs/sd-parseq#interpolation-expressions). This is a musical expression language, not Parseq syntax compatibility or JavaScript execution.

Expressions compile before playback: at most 2,048 characters, 128 operations and 32 nesting levels. Runtime uses fixed stack storage and no allocation, locks, filesystem access or network access. Continuous plugin parameters receive per-sample ramps between evaluations on a 32-sample clock. Discrete parameters retain discrete values. Very fast or discontinuous formulas are limited by that evaluation rate; this is control automation, not an audio-rate oscillator.

The final result is clamped to 0–1. A non-finite final result (for example `sqrt(-1)`) uses ordinary linear interpolation. Syntax errors leave the saved song untouched and report a character position. The preview uses the same evaluator as playback.

Formulas survive native project save/reopen, Undo/Redo and copy/paste/shift. Flip/scale/humanize of a scripted segment is rejected with an explanation; express those changes in the formula. Scripted automation uses native metadata version 11, which older app builds reject. Legacy instrument volume/pan/pitch envelopes retain their tracker-format representation; this scripted editor is for parameter automation.

## Agent API

`api.describe` lists the formula variables, functions and limits. `automation.pattern.get/set` includes optional `formula` on points, required for `curve: "scripted"`.

```json
{"position":0,"value":0.2,"curve":"scripted","formula":"mix(start,end,t^3)"}
```

`automation.formula.preview` accepts `points`, `rows`, optional `rowsPerBeat`, `start`, `end`, and `samples` (2–4,096). It returns `[position,value]` pairs without changing the song. Position units are 1/256 row.

`transport.get` returns playback state and telemetry. Revision-guarded `transport.play` accepts `order`, `cursorRow`, `loop`, and optionally `pattern`, `startRow`, `endRow` (exclusive). `transport.loop` takes `enabled`; `transport.stop` ends playback. These transport operations do not create song Undo steps.
