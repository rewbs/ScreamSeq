# Native instrument envelopes — 2026-09-21

The Instrument toolbar button and command palette open a retained, modeless
Windows instrument editor. Volume, pan and pitch/filter envelopes use the
shared OpenMPT instrument data and existing asset/envelope APIs. Instrument
settings, sample keymaps, points, flags and markers can apply in one document
Undo step. No project-format or DSP change is introduced.

## Controls and data ownership

The window captures stable instrument identity, its current numeric slot,
document identity and revision. Close/reopen preserves drafts; Reload refreshes
the captured instrument; From cursor explicitly captures the current song and
instrument. Target/kind changes cannot redirect pending fields. External edits
and document replacement leave the old draft visible and block stale Apply.

Points support native integer tick/value fields, click/drag, double-click
insertion and keyboard edits. The first node remains at tick zero. Loop,
sustain and release markers move with inserted/deleted nodes; release 255
means unset. Flags include enabled, loop, sustain, carry and pitch filter mode.
The shared API/schema now accepts carry and releaseNode in instrument.patch
on both platforms. Mac source was updated but has not been built on this host.

The editor exposes name, volume, pan, fadeout, new-note and duplicate-note
actions, plus sample assignment over a chosen key range. New from sample uses
the existing conversion path, preserving sample-mode pattern assignments when
the song first acquires instruments. Point and marker drafts are explicit;
invalid values do not discard them or partially apply another setting.

Range copy, flip time/value, shift, scale, ramp, sine, seeded humanize, paste and
insert use the shared envelope tools. Previews remain local until Apply. The
private range clipboard never accesses the desktop clipboard. The envelope
bank receives native points, flags and marker positions. Linked uses remain
protected until explicitly unlinked or changed through the master.

Ctrl+Enter applies and Ctrl+R reloads. F6 switches between node controls and
canvas. Canvas arrows move points, Shift uses four-unit steps, Tab cycles,
Insert starts a point, Delete removes, Home fits, +/- zoom, and Ctrl+Left/Right
pan. Ctrl+wheel zooms and ordinary wheel pans. Escape restores a drag or pending
point/marker fields before hiding. Native double-click dispatch is opt-in, so
existing tool windows retain their original click behavior.

Curve geometry is cached outside paint. Playback lines consume the renderer's
bounded voice-position snapshots and clear when transport stops. Painting
does not call the document worker or read live engine channels. The top-bar
song title/status move to leave the new Instrument button unobstructed.

## Qualification

The ARM64 Release application was built in `bin/windows-parity`. Executable
SHA-256: `044606329AEA71238C817399DE06E9A8BBC95ACB9B8D8A6CE309FE6643150449`.

Eight new actual-app cases exercise API validation/no-op/history, all envelope
kinds, settings and keymaps, captured stale drafts, every transform, bank
linking, mouse/keyboard editing, minimum control bounds, native reopen, offline
audio and short silent live markers. The final focused run of the live-marker
and canvas-edge regressions passes. The final full application suite passed
**187 tests, zero failures and zero skips, in 411.976 seconds**. This includes
the existing graph, mixer, pattern, formula, bank, sample, worker and installed
effect/Surge XT lifecycle, preset, alias, library, path and routing cases.

The enabled envelope changes quarter-second rendered energy by up to
32.98756134844268 versus its disabled control. Both fixtures are finite and
preserve the document, with maximum partition difference `5.14090061188e-7`
at 44.1/48/96 kHz and 17/128/4096/8193-frame blocks. The existing `1e-6`
threshold is unchanged. Each offline render covers one second.

The bank round-trip test caught and fixed an envelope-duration scaling error:
shared instrument templates end at `lastTick * 256 + 1`, rather than extending
by a whole tick before scaling. The paste tests also caught an unnecessary
range-end check; paste/insert now derive their extent from the clipboard as
the API requires. Native edge handles remain selectable at values 0 and 64.

Live telemetry checks observe advancing envelope positions in the retained
window and empty markers after Stop. The hardware output is silenced after DSP;
no system audio route or volume is changed. Private-desktop tests leave the
foreground session and clipboard unchanged. These controls and geometry tests
are not foreground visual or sustained frame-presentation evidence.

The public schema and the updated Mac regression syntax parse. Mac runtime
qualification remains outstanding. Logs and evidence are under
`bin/windows-instrument-envelope-*`. The canonical full-suite log is
`bin/windows-instrument-envelope-app-tests.log`; native scene, enabled/disabled
PCM and live-marker reports are in `bin/windows-instrument-envelope-evidence/`.
The final live test observed ticks 6, 7 and 8, then no markers after Stop, with
68 callbacks and zero overruns/faults. This is a short functional check.

The preserved package is
`bin/windows-checkpoints/instrument-envelopes-20260921/`. Its manifest records
the source commit/tree, upstream, executable and all packaged file hashes.
Previous checkpoints remain intact. No task-owned QA apps remain running.
A fresh fetch found no upstream commits beyond `bcfe0f8a7`, already integrated
here; the default branch is `codex/screamseq`, with no remote `main`.

## Remaining scope

This is progress toward full parity. Native instrument import/audition,
keymap visualization, independent pins/docking, wider instrument settings and
foreground aesthetic/accessibility qualification remain open. Imported
coincident envelope nodes can be viewed; the existing point-replacement API
requires strict tick ordering for edits. The editor follows that shared rule.
Short silent playback and offline comparisons do not establish long-session
realtime or sustained presentation performance. Reciprocal current Mac builds
and project reopen, x64, absolute automation, sample/MIDI/recording/recovery,
live opaque-state replacement and OrbitCab's partition discrepancy remain in
the parity plan.
