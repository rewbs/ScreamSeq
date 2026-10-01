# Native sample and instrument audition — 2026-09-21

The sample and instrument editors and command palette now open a retained
native audition piano. The piano supports two octaves, octave shifts, velocity,
note-on/off buttons, mouse glissando and musical typing with visible focus.
F6 switches piano/text focus; text fields retain ordinary text entry. Escape,
focus leaving the window and Close release owned inputs. Multiple inputs holding
the same pitch share ownership; releasing one cannot cut another. Reload retains
the stable target, and From cursor explicitly selects the current one.

The Windows host exposes `transport.note` and `transport.panic`, with matching
Mac dispatcher/schema/documentation additions. Writes require the current
document revision but do not change document state or Undo. Raw sample targets
bypass instrument mappings; instrument targets retain mappings/envelopes/plugin
assignments. Nonzero note-on starts stopped audition with a paused pattern clock;
preview alongside playback keeps the existing renderer and song transport.
Space from stopped audition starts ordinary song playback.

Preparation runs on the document worker after closing/joining the previous
callback. A bounded control-thread queue retains releases arriving during first
note preparation; Stop/Panic cancel pending preparation. The shared renderer's
128-event SPSC queue handles DSP delivery. Native releases use the original
target and a transient playback epoch, so a late key-up cannot cut a new
preparation's same pitch. Stopped audition matches Mac's saved sample/instrument
and plugin-rack path; it intentionally omits song mixer/graph/pattern automation.

Audition tests exposed legacy IT cut behavior in the shared preview renderer:
freezing the sample increment left a DC decay above 1e-6 through frame 2400 at
48 kHz. Preview cuts now preserve the moving sample and use the native short
volume ramp. The original assertion remains unchanged and passes. Repeated raw
sample pitches also release their preceding voice instead of leaving an
unreachable looping sample. These fixes apply to both platforms; ordinary
OpenMPT song note semantics are untouched.

The detailed sample editor draws bounded live voice markers. The main workspace
stops presenting once audition voices and meters become idle, retaining prepared
audio for the next note. No layout, serialization or plugin discovery occurs in
the callback.

## Qualification

The final ARM64 executable has SHA-256
`594E929963069678A0CB3A8C0C157A91B7F25BACEDFE5CB67A492A8AC7B58706`.
All 29 native CTests pass in 15.70 seconds. The shared hosted-project test covers
preview queue saturation/recovery, Panic, repeated-pitch release, fixed song
position, source preservation and sample rendering across callback partitions.
Its audited callbacks report zero C++ allocations and deallocations, with
positive controls confirming the audit detects both.

Thirteen focused tests pass in 39.007 seconds on the final executable, including
all ten audition cases and the workspace/parameter regressions. The first full
run found two workspace equality failures caused by newly added transient draw
counters. These now belong to `transport.get.presentation`, preserving stable
workspace snapshots. A third failure was the existing foreground-preservation
check during teardown; its interaction checks passed, and the unchanged
preservation assertion passed on rerun. The initial log remains preserved.

The final full application run covers 221 cases in 590.462 seconds: 220 pass,
one reports an error, and none are skipped. The error is a valid worker-busy response in
the formula reference test, which queried the catalogue before the parent graph
preview finished. That test now waits for the existing bounded worker-idle
condition; it does not retry writes or relax assertions. All nine formula
workbench tests pass afterward in 13.793 seconds. This is full-suite coverage
plus a focused correction/rerun, not a claim of a single entirely green run.
The logs are `bin/windows-audition-app-tests.log` and
`bin/windows-audition-formula-rerun.log`.

Actual-application offline audition renders use 44.1, 48 and 96 kHz; blocks of
17, 128, 4096 and 8193 frames; and two seconds per render. Notes start at 0.25
seconds and release at 0.75 seconds. All five fixtures below produce finite,
nonzero sound after a silent prefix, keep song position stationary and retain
the document unchanged. Maximum PCM partition delta is exactly zero for each.

| Fixture | Aggregate absolute sample energy |
| --- | ---: |
| Raw sample | 33898.2026625 |
| Mapped sample instrument | 99823.9298918 |
| VST3 provider instrument | 75240.0011212 |
| Installed Surge XT | 51522.3859848 |
| Surge XT after native save/reopen | 51522.3859848 |

Surge's deterministic fixture enables its six oscillator Retrigger parameters.
It preserves exact saved plugin state across audition and reopen. Silent WASAPI
tests cover sample/instrument notes, saved-state preservation, plugin removal,
plugin Undo and successful audition after restoration. Hardware output is muted
after DSP; the tests do not change the default device or volume. Owned host
reports record the actual 48 kHz endpoint and 480-frame period, callback counts,
duration and faults. These short runs are functional evidence, not capacity or
latency benchmarks.
All five audition host reports show zero deadline overruns, starvation
indicators, device errors and processor faults. The final Surge report contains
55 callbacks for its last prepared host. The idle-sample test confirms the
presentation count remains unchanged while prepared audio continues silently.

Native tests also cover source-editor connections, stale target/revision guards,
read-only inspection behavior, note-off without starting a device, API replay,
overlapping input ownership, repeated key-down, focus/Close release, old playback
epochs, song playback/Panic, Space transition, marker clearing, minimum control
bounds and idle presentation. Evidence is under `bin/windows-audition-evidence/`.
The preserved development package is `bin/windows-checkpoints/audition-20260921/`.
Its manifest records source commit/tree, executable and individual file hashes,
the exact full-run result, and the focused formula rerun separately. No ScreamSeq
QA processes remain after testing.

## Remaining scope

This is not full Mac parity. Pattern-entry/dock musical typing, MIDI input and
recording, sample browser preview, batch/replacement import, export/property controls, instrument
import and visual keymaps remain. Native foreground aesthetics/accessibility,
sustained presentation and loaded realtime capacity need separate qualification.
Windows private-desktop handlers and bounds do not establish foreground visual
quality. Mac API source is updated here but cannot be compiled or run on Windows.
The shared C++ callback audit covers operator new/delete, not direct malloc/free,
locks or vendor-private behavior. The existing OrbitCab partition discrepancy,
Contourtonist calibration behavior, first-open Surge state publication and
reciprocal Mac format-17 reopening remain tracked in `PARITY_PLAN.md`.
