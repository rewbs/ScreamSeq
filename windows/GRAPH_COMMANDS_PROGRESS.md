# Native graph command lanes — 2026-09-21

The pattern header's Graphs button and command palette open a modeless native
graph command editor. It exposes Row, Start, Stop, Clear, Amount and Wet commands,
eight lanes per mixer bus, precise offsets, Amount/Wet percentages and stopped
effect tails. Open graph connects a command to the existing reusable graph
editor without overwriting an existing graph draft.

## Editing and display

The editor captures the document, revision and stable pattern/bus identities.
Apply uses the existing `graph.commands.set` API and document Undo. It merges the
selected row/column into that pattern's command collection, retaining other
rows, lanes, buses and patterns. Verify validates without history. Remove only
removes the selected cell. Enabling a lane preserves already-enabled lanes.
Commands keep the shared 65536-units-per-row timing and native format 17.

Close/reopen retains drafts. Changing bus/lane while a draft exists is rejected
without discarding its action or values. Reload cell explicitly refreshes the
captured cell; From cursor explicitly captures the current song/cursor. A deleted
bus or replaced document cannot redirect an existing draft. The modeless window
leaves pattern navigation independent. Ctrl+Enter applies, Ctrl+R reloads, F6
switches target/row controls, and Enter in the row field loads that row.

Enabled graph lanes appear beside the pattern grid, aligned with its row and
playhead positions. Their strip occupies at most 40% of the pattern area and
scrolls horizontally across additional lanes. Click/arrows select rows and
lanes; Enter or double-click opens the editor; Delete removes a command with
Undo. F6 switches between pattern and graph lanes. Group bus lanes are included.
The Wet command displays its wet value; offset commands carry a tilde.

The serial document worker publishes a compact immutable lane projection, with
a sorted cell index and graph numbers. Painting and hit-testing query no plugin
or document API. Ordinary note edits reuse the projection. Bus renames publish
a new projection while old snapshots remain immutable. The projection counts
against the aggregate view budget; graph/mixer candidate validation rejects
growth before committing or stopping playback. Pattern duplication preflight
also accounts for copied graph commands.

## Qualification

The ARM64 application and document-controller tests were rebuilt. Six focused
native application tests passed in **25.286 seconds**, covering all six actions,
exact offsets, collection preservation, no-op/dry-run, stale/invalid rejection,
deleted targets, Close/reopen, independent patterns, native controls, lane
navigation/deletion, minimum window bounds, Undo/Redo and native save/reopen.

The worker regression passed projection indexing, exact offsets, reuse across
ordinary note edits, rename Undo, retained snapshot conservation and precommit
cache-budget rejection without a playback stop or partial write.

The final full application run passed **166 tests, zero failures and zero skips,
in 279.123 seconds**. It includes the installed effect/instrument lifecycle,
native editor, alias, preset, library, path repair and routing cases, plus the
existing pattern, sample, formula, envelope and worker regressions. The current
source added no new DSP semantics. Short silent hardware workflows remain
bounded evidence, separate from sustained loaded audio qualification.

Release executable SHA-256:
`8116D9AD168E688FDAB26E7C6AD39F591E5963CA3FB43C77AEC75CE4CF2AAB8C`.

An installed ARM64 Contourtonist recipe was controlled through native Start,
precisely offset Wet and Stop-with-tails commands, saved and reopened with exact
recipe preservation. Its offline fixture produced **zero PCM partition delta**
at 44.1/48/96 kHz and 17/128/4096/8193-frame blocks, one second per render. Both
the rack effect and graph recipe retain their independent saved instances. This
does not resolve the separate OrbitCab callback-partition discrepancy.

Early qualification needed two harness corrections: palette-only command IDs
have no child HWND, and the separate installed-plugin render process needs the
same explicit private scan cache as the app. Neither required an application
change or a relaxed render threshold.

Logs: `bin/windows-graph-commands-build.log`,
`bin/windows-graph-commands-tests.log`,
`bin/windows-graph-commands-view-tests.log` and
`bin/windows-graph-commands-app-tests.log`. The command fixture and PCM report
are in `bin/windows-graph-commands-evidence/`.
The preserved package is `bin/windows-checkpoints/graph-commands-20260921/`;
its manifest records the source commit/tree, upstream baseline and every file
hash. The preceding song-routing checkpoint is preserved separately. A fresh
fetch found no newer upstream changes beyond `bcfe0f8a7`, already in this branch.

Private-desktop controls and geometry do not establish foreground aesthetic or
sustained presentation quality. Long loaded audio, broad vendor coverage, x64
and reciprocal Mac reopening remain separate gates. Full parity remains active;
next editors are parameter automation and instrument envelopes, followed by the
remaining sample, recording, workspace and qualification work in PARITY_PLAN.md.
