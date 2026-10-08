# Windows parity stopping point and remaining work

The Windows application now has a substantially more complete editing workspace,
but full Mac parity and release quality are not established. Work stops at a
qualified Precise Notes checkpoint. The next largest confirmed UI gap is the
multi-bus Mixer; named/grouped note tracks follow it. Neither should be described
as implemented by the current build.

This report is dated **October 7, 2026, America/Los_Angeles**. The application
remains a native Windows frontend over the shared musical model. No replacement
frontend or project-format migration is required for the next two priorities.

## What is finished at this stopping point

The current workspace supports independent native regions for Pattern, routing,
Instrument, parameter Automation, Graph Curve and Precise Notes, with compact
fallback, retained drafts, captured targets, pin/Return behavior and saved layouts.
Recent checkpoints also provide arrangement/timing and matrix editing, annotations,
sample workflows/library, native MIDI take review and recovery, configurable
shortcuts and context menus. These features retain their individual qualification
limits; the linked progress reports are the detailed record.

The final Precise Notes slice adds fractional offsets, captured sound for empty
rows, and same-row Reload retention of the selected hit, list viewport and focus.
It preserves the existing guarded API, one-step Undo and native persistence.

| Final candidate verification | Result |
|---|---|
| Full actual-application suite | 404/404 passed, 1,027.617 seconds; no failures or skips |
| Focused application suite | 82/82 passed, 150.469 seconds |
| Native suite | 50/50 passed, 101.85 seconds |
| Visual evidence | 18 source-matched views at 192 DPI; geometry checks and two independent reviews pass |
| Isolation and identity | Final runners pass; exact source and binaries unchanged; musician session preserved |

The checkpoint destination is
`bin/windows-checkpoints/precise-note-parity-20261007/`. Its manifest and independent
verification receipt establish publication. The exact candidate and retained
failed attempts are documented in [Precise Notes progress](PRECISE_NOTE_PARITY_PROGRESS.md).
The musician's existing process and audio defaults remain unchanged; a new build
has not been substituted into that running session.

## Remaining work in priority order

### 1. Finish the live Mixer strip workflow

Mac exposes several buses at once with faders, meters and direct mute/solo. The
current Windows Mixer primarily edits one selected bus through fields and Apply.
That is a conspicuous everyday workflow gap even though routing and the mixer API
already exist.

A source-only proposal adds Strips/Details modes, a bounded pool of visible native
strips, stereo meters, gain/balance, mute/solo and access to the retained Details
and Routing editors. It is **unintegrated, unbuilt and untested**. Its source review
and any unresolved findings must be checked before adoption.

Completion requires real native drag/keyboard tests, safe cancellation when a
request or document changes, one Undo per completed gesture, saved-project
roundtrips, bounded controls for large mixers, minimum-size visual review and
rendered audio comparisons. A strip must never retarget a different bus during
scroll/reorder. An uncertain mutation outcome must be inspected rather than
retried blindly. Existing routing, inserts and sends should be reused.

### 2. Add named and grouped note tracks with independent column mute

The shared model, native codec and renderer already preserve track groups and
column mutes. Current Windows adapters and pattern headers do not expose the
Mac-compatible editing workflow. Raw column annotations exist, but the grid still
shows generic channel labels.

Add `track.get`, `track.create`, `track.group`, `track.ungroup` and
`track.column.set` using the shared helpers. Connect named/grouped headers, group
spans, per-column mute and native commands. Keep stable IDs and all existing
notes, effects, routes and processors intact. Ungroup must preserve processing.

The difficult boundary is live Undo/Redo: Windows currently treats these metadata
changes as requiring playback stop. Column mute and nonstructural grouping edits
need the shared renderer's safe publication path. Structural creation/grouping
still needs complete validation before changing playback, one Undo, persistence
and reciprocal Mac compatibility tests. This is audited preparation only.

### 3. Complete workspace polish and user-facing verification

The independent regions and retained editors are implemented; they should not be
reported as wholly missing. Broader panel placement, simultaneous Main-editor
workflows and remaining editor menus/completion affordances still need a focused
current-source inventory against Mac before expanding the UI.

The larger immediate quality gap is qualification of the actual foreground app:
keyboard-only use across floating tools, accessibility names and screen-reader
behavior, multiple monitors and display scales, resize behavior and sustained
presentation. Current private-desktop captures at 192 DPI do not establish those
results. Conduct a real end-to-end composing session, including opening an older
layout and recovering interrupted work, to catch friction beyond isolated controls.

### 4. Finish device and plugin robustness

The WASAPI source currently requires stereo endpoints and has no endpoint-change
notification registration. Automatic device removal/default-device recovery and
other channel layouts remain implementation work. Hardware MIDI timing, reconnect
and loaded recording sessions need controlled physical-device qualification.

Plugin parameter changes have a live path, but opaque `plugin.state.set` still
uses stop-before-publication. Seamless opaque-state replacement requires its own
ownership, latency and realtime review. Broader vendor editor lifetime, state
recovery and changing-latency cases remain release work. Reproduce the historically
recorded OrbitCab block-partition discrepancy on current source before diagnosing
or declaring it fixed; do not weaken the audio comparison threshold.

### 5. Prove cross-platform and release readiness

Build Mac and Windows from the same source and exchange current format-17 projects
in both directions. Verify exact musical data, history-sensitive edits, unavailable
plugins and rendered behavior; a Windows save/reopen test is not reciprocal Mac
qualification. Audio Units should remain preserved and unavailable on Windows.

Run sustained loaded playback/recording, loopback, endpoint switching, latency and
memory measurements, and full host allocation/free/lock audits. Expand the real
plugin matrix. Current ARM64 evidence does not qualify x64 or plugin bridging.
The final checkpoint makes no claim of Mac runtime parity, physical MIDI/audio
qualification, sustained foreground performance or complete accessibility.

## Where to resume

Start with Mixer strips, then track grouping/mute, then the foreground and release
gates above. Keep each slice independently reviewable; do not combine the Mixer
proposal with a docking rewrite or a new musical model.

The retained preparation checkout is:
`C:/Users/P14/.codex/worktrees/precise-note-parity/ScreamSeq-windows`.
Its ignored `bin/mixer-strip-proposal/` contains exact proposed source and tests,
manifest and source-review history. The ignored `bin/next-ui-parity-audit/` and
`bin/note-track-parity-audit/` contain the two current-source audits. These are
preparation, excluded from the Precise Notes runtime package. Preserve this
checkout until those artifacts have been transferred or deliberately adopted.

Use [the continuation notes](RESUME_PROGRESS.md) for checkpoint identities and
[the parity plan](PARITY_PLAN.md) for earlier phase reports. Older “remaining”
lists describe their historical checkpoint; this report is the current priority
order. Full Windows/Mac parity remains unfinished, and the goal is paused at the
user's requested stopping point.
