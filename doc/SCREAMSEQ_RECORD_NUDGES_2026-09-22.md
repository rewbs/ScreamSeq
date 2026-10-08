# Record nudge effects — 22 September 2026

Implemented `NF` (nudge forward) and `NR` (nudge reverse), available in every FX
column. Type either code, or press `?` and search **nudge**. The editor exposes
strength in percent, fractional row/beat onset, and duration in rows including
recovery. A useful starting comparison is NR at 25% versus 75%, duration 1 row.

The shared renderer adds signed speed to sample playback, with a quintic push
and a longer, gently shaped release. Stronger pushes accelerate faster. Against
normal playback, 50% reaches zero and stronger pushes reverse. A push in the
current direction accelerates it. Repeated pushes start at the current velocity
and add momentum before recovering; their accumulated speed contribution is
bounded to ±16. Existing pitch bends multiply this speed. The engine evaluates
motion every audio sample and preserves fractional position through reversals.
Plugin processing retains its ordinary buffers.

The API uses `nudge-forward` / `nudge-reverse` with normalized `value` 0–1 and a
positive `duration` in 65536 units per row. Single-cell editing, whole-pattern
editing, all eight FX columns, dry-run, revision guards, no-op handling,
Undo/Redo, clipboard and native save/reopen are covered. See
[the API reference](../mac/AUTOMATION.md#record-nudges-nf--nr).

## Verification

- 76 CTest tests passed, including `record-nudge`.
- Independent signed phase integration at 44.1, 48 and 96 kHz; forward/backward
  playback, both nudge directions, 0/25/50/100% strength, and callback sizes
  1/17/128/4096 frames. Scratch PCM agrees within 0.000003 across partitions.
- Loop/endpoint matrix at 8, 48 and 192 kHz, 8/16-bit mono/stereo samples,
  1/2/7/257-frame loops, ordinary/ping-pong/reverse/sustain modes, overlapping
  pushes and note release. Explicit zero-speed hold/restart regression.
- Realtime audit found zero host allocations, frees or locks while rendering.
  AddressSanitizer and UndefinedBehaviorSanitizer passed (leak detection disabled
  for the native framework test process).
- 30 stock OpenMPT PCM comparisons remain bit-exact with no native effects.
- AppKit interface tests and real application socket tests passed. Live UI
  inspection verified `NR` typing, the NF/NR effect-search results, NF opening
  from search, and applying NR at 25%, row offset 0.125 and duration 0.75.
  The actual socket returned value 0.25, position 8192 and duration 49152.
- The Windows portable project codec passed 2176 checks including both new
  command kinds. Windows native controls and API dispatch were updated, but
  the Windows executable was not built or launched on this Mac.

## Scope and limits

These effects scratch sample voices, including their continuing channel-owned
voices. They cannot reverse a plugin instrument's internal oscillators or its
output. An unlooped sample ends if the nudge carries it beyond an endpoint.
Durations must end within the pattern. Starting in the middle skips preceding
nudges; entering/repeating a pattern resets the push. Keyboard preview voices
are excluded. The combined native pitch/nudge track limit is 16.

The qualified application is `bin/mac-background/ScreamSeq.app`. Disposable QA
processes use separate bundle identities and inspection mode. No system audio
route was changed. Changes remain uncommitted.

## Performance observations

A 16-voice, 16-track fixture runs alternating full-strength NF/NR commands every
row. An offline render of 30 seconds at each rate, 128 frames per call, measured:

| Rate | Mean thread CPU | 99th percentile thread CPU | Maximum thread CPU | Buffer duration |
| --- | ---: | ---: | ---: | ---: |
| 48 kHz | 136 µs | 310 µs | 611 µs | 2667 µs |
| 96 kHz | 129 µs | 346 µs | 673 µs | 1333 µs |

This measures renderer cost, not a hardware playback guarantee. The machine was
heavily contended (load averages above 280 on ten CPU cores); wall-time exceeded
the buffer duration in 1498/11250 and 4324/22500 offline calls respectively.
Accordingly this **does not pass a wall-clock realtime deadline qualification**.
The callback audit and rendered-audio comparisons pass independently. A sustained
Core Audio/physical-output run on an otherwise available machine remains untested
for this feature. No 60 fps presentation claim is made from the live UI check.

The new mixer fast path avoids copying interpolation taps away from boundaries;
the entire audio matrix and sanitizer checks were rerun after this change.
[Qualification evidence](mac-native-qualification/2026-09-22-record-nudge/) includes
the benchmark source, raw CPU/wall metrics and test logs.
