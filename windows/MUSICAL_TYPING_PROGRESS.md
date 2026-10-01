# Pattern and inspector musical typing — 2026-09-21

Windows pattern entry now uses an explicit selected sample or instrument and
auditions the entered note through the existing shared renderer. The sound
chooser sits below Octave and Step. It retains stable sound identity across
catalogue refreshes. Typing clamps notes to the source module's range and rejects
sample slots above 255 before narrowing them to the pattern field. Rows with
precise notes open their existing editor without replacing their events.

Repeated key-down does not enter more rows or retrigger a held note. Each note
edit uses the same revision-guarded `pattern.apply` transaction as the API, so
Undo, Redo and native save/reopen retain normal shared behavior. Cursor advance
checks that navigation has not moved during the document-worker wait.

Musical typing also works in the sample dock and in the detailed sample and
instrument-envelope windows. F6 switches those windows between canvas and
control focus. `ZSXDCVGBHNJM` / `Q2W3ER5T6Y7UI` plays the captured saved sound;
text fields, lists and choosers retain ordinary keyboard input. Reverse and
Normalize remain available as native buttons and command-palette actions;
their former R/N bindings yielded to musical typing. Space controls the song
from the sample/instrument canvas, with repeats suppressed.

The main workspace has a highlighted Live keys button and Ctrl+Alt+L command.
While enabled, musical typing plays across its panels without entering pattern
data. Local sample focus still auditions the inspected sample. The mode does
not intercept vendor editors, command-palette input or other floating tools.
Turning it off, leaving the application, and Stop release or retire held inputs.
It resets when another document opens; MIDI recording is separate unfinished
work. `workspace.get` exposes the real `liveKeyboard` state and a transient
`musicalTyping` snapshot of the chosen sound and owned keys.

## Note ownership and audio boundaries

Held keys capture document identity, stable sound identity, original slot,
pitch, preparation epoch and a per-pitch voice generation. Releases do not
retarget after selecting another sample, changing octave, navigating, or
publishing a newer document view. A replacement note from another native input
or the API takes a new generation; a late release cannot cut it. The separate
piano now observes that same generation guard.

Keys are registered before the document transaction begins. A key-up, focus
change, Stop or Panic during its responsive wait cancels pending audition, so
completing a note edit cannot start an unwanted delayed voice. First-note
preparation uses the existing bounded queue. Simultaneous keyboard inputs for
the same target/pitch share the current voice until its last owner releases.

Native input bookkeeping is on the UI thread. The callback still consumes the
shared bounded preview queue; no UI, catalogue scans or allocations were added to DSP.
Instrument key-off preserves envelopes and tails. A mapped looping sample with
zero instrument fadeout may legitimately keep sounding after key-off; Panic or
Stop remains available. The release test uses an explicit short fadeout rather
than changing this musical behavior.

A deterministic audio regression also found a shared renderer ordering defect:
Panic flushed the queue at the next callback, including notes accepted after the
Panic request. Preview events now carry a cancellation generation. The callback
releases old voices and skips older events while retaining newer notes, including
when Panic arrives after its initial generation read. It visits at most the
128 queue slots and processes at most 32 active events per render. Queue overflow
still requests cancellation and explicitly rejects the overflowing event.
The regression covers 64 older queued notes followed by Panic and a new sound;
the new sound must render in the first callback. The previous implementation
fails the ordering assertion; the corrected renderer passes it.

The added short callback also exposed a preview cut near a tracker-tick boundary:
zeroing channel volume could retire a background voice before its ramp finished,
leaving a residual peak of 0.0000539086 after the original 1024-frame settling
bound at 48 kHz. Preview cuts now use the fade-to-zero flag while retaining the
moving channel until the ramp completes. The unchanged residual assertion then
measures zero. Additional tests cover note-off and Panic at 44.1, 48 and 96 kHz
with six starting phases each. Ordinary song note/cut semantics are unchanged.

## Upstream and installed plugins

A fresh `git fetch origin --prune` found no newer changes. The remote has no
`main`; its default branch remains `codex/screamseq` at `bcfe0f8a7`, already an
ancestor of the Windows branch. The review and full parity plan still cover both
landed commits, `c486a0642` and `bcfe0f8a7`, including unified FX, current-only
format 6/17, plugin lifetime/latency, routing and Mac workspace changes.

The installed per-user ARM64 QA bundles remain byte-for-byte identical to their
recorded publisher downloads/builds: Contourtonist 0.2.2, OrbitCab 2.5.0 and
Surge XT from the 2026-09-18 source build. Their paths, provenance and prior
lifecycle evidence are in `UPSTREAM_PLUGIN_QUALIFICATION.md` and
`TRIGGER_INSTRUMENT_PROGRESS.md`. The current application suite exercises real
plugin creation, native editor/parameter changes, saved state, removal, Undo,
reopen, presets and location repair. Typed pattern entry additionally drives
installed Surge XT and preserves its opaque state.

## Qualification

The ARM64 executable has SHA-256
`DBDDA9756632CB4355926AD5711608D257BC940B2647B6D388D41E73899C1C43`.
All 29 native CTests pass in 14.17 seconds. The shared hosted-project regression
covers the ordered Panic sequence, queue overflow/recovery and release ramps
at three rates and six phases, with both note-off and Panic. Audited renders
report zero C++ allocations and deallocations; direct malloc/free, locks and
vendor-private behavior remain outside that audit.

All 17 audition/typing application tests pass in 44.122 seconds on this binary.
They include selected-sound entry, one edit per physical press, module note
bounds, precise-row preservation, Undo/Redo/reopen, native inspector focus and
stale captures, Live keys/text separation, original voice ownership, key-up and
Stop during a worker wait, installed Surge XT typing and saved-state retention.

Five offline fixtures cover raw samples, mapped sample instruments, the VST3
provider instrument, installed Surge XT and Surge after native reopen. Each
uses 44.1/48/96 kHz, blocks of 17/128/4096/8193 frames, and two seconds per render.
All retain the document and song position, produce finite nonzero sound after
a silent prefix, and have exactly zero PCM difference across partitions.
The ten short silent WASAPI reports use 48 kHz / 480-frame periods and report
zero deadline overruns, starvation indicators, device errors and processor
faults. These are functional checks, not sustained capacity or acoustic latency
measurements. Output is silenced after DSP; device defaults and volume are
unchanged.

The first full application run completed 228 cases in 606.890 seconds, with
227 passes and one failure in the unchanged foreground-preservation assertion
during teardown. The Surge test body passed, and its separate diagnostic run
passed in 5.170 seconds. That log remains preserved. The ordering/ramp bugs above
were found separately through source review and deterministic offline regression;
they were not the cause of the foreground assertion.

The final full application run on the corrected renderer completed 228 cases
in 602.730 seconds: 227 passed, one failed, none skipped. The failure was the
initial visibility assertion in
`test_native_send_update_preserves_other_routes_dry_stale_and_retained_draft`:
the song-routing tool was not visible after its open command. All audition,
typing, installed-plugin lifecycle and formula cases passed. The complete
song-routing module passes all 10 cases in 43.229 seconds on the same executable; this does not
erase the full-run failure or establish its cause. Keep the intermittent
routing-open failure as an unresolved qualification issue.

Logs and fixtures are under `bin/windows-typing-*`; final audio/workflow evidence
is in `bin/windows-typing-ordered-evidence/`. The preserved package is
`bin/windows-checkpoints/musical-typing-20260921/`, with source commit, executable,
individual file hashes, notices and exact regression results in its manifest.

## Remaining parity work

Sample properties, batch/replacement import and browser preview;
instrument import and visual keymaps; device selection, MIDI input/recording and
recovery; configurable shortcuts, persisted/floating docks and accessibility
remain. Full live-key behavior in every floating tool is not implemented.
Worker-busy note-on outside audition preparation still reports that no note was
queued; it does not silently postpone a note until another edit completes.

Foreground aesthetic comparison, sustained presentation, loaded realtime
capacity, x64/bridging and reciprocal Mac format-17 reopening still need their
own qualification. Private-desktop handlers and geometry are not foreground
visual evidence. OrbitCab's buffer-partition discrepancy, Contourtonist's
calibration/control-timer behavior and live opaque-state publication remain
tracked failures or limitations. This checkpoint does not establish full parity.
