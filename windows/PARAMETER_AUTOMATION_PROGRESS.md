# Native pattern parameter automation — 2026-09-21

The Automation toolbar button and command palette open a modeless native curve
editor. It chooses a stable pattern, plugin and parameter, supports local
parameter search and links to the rack, envelope bank and formula workbench.
Pattern cursor navigation remains independent of the captured curve.

## Editing and API

Windows now exposes `automation.pattern.get/set/remove/copy/transform` through
the document worker and `api.describe`. These use the existing shared song
model, point validation, formula compiler and transform implementation. No
project-format or DSP change is introduced. Get includes the effective
`rowsPerBeat`; the Mac response and public guide/schema were updated to describe
that field. Mac compilation has not been performed on this Windows host.

Points use 256 units per row and normalized values. Nine curve kinds include
scripted and mirrored segments. Available targets resolve against the actual
plugin parameter catalogue. Missing destinations remain unresolved without
retargeting another slot. Enabled lanes reject conflicting pattern commands
and absolute automation. Linked bank uses remain protected until explicitly
unlinked or edited through their master.

Apply captures document revision and stable target identity, makes one document
Undo step and preserves unrelated lanes and patterns. Verify, dry runs and
no-ops leave history unchanged. All point batches and tool options validate
before mutation. Native save/reopen retains the lanes and vendor state.

The native editor supports point fields, click/drag drawing, snapping, range
copy, flip time/value, shift, scale, ramp, sine, seeded humanize, paste and insert
paste. Transform previews remain local until Apply. Its private range clipboard
does not read or replace the system clipboard. Time/value zoom and pan rebuild
cached geometry outside painting. Formula previews are bounded and scheduled
only after changes; a hidden or unchanged window has no continuous preview loop.

Close/reopen retains drafts. Reload explicitly refreshes the captured pattern;
From cursor explicitly changes to the current song/pattern. Target selection,
stale requests and formula/bank completions cannot silently redirect a draft.
Removing the last lane for a deleted plugin now clears the editor's saved lane
identity, rather than displaying a reload error after the successful removal.

Ctrl+Enter applies; Ctrl+R reloads. F6 switches parameter list/canvas focus.
Canvas arrows move the selected point, Shift makes finer moves, Tab cycles
points, Delete removes a point, Home fits, and +/- zoom time. Ctrl+wheel zooms
time; Ctrl+Shift+wheel zooms values; Shift+wheel pans values. Escape restores a
drag or pending point fields before closing. The main Plugins/Mixer/Automation/
Graph buttons now have separate bounds instead of overlapping.

## Audio evidence and limits

Positive controls compare saved enabled and disabled curves through the actual
application preparation/render path. The built-in Gainer test explicitly
selects Gain, rather than its Enabled switch. A deterministic external VST3
gain fixture separately checks audible parameter modulation. Both are routed
through Master and render at 44.1/48/96 kHz with 17/128/4096/8193-frame blocks,
one second per render. The callback-partition tolerance remains `1e-6`.

The installed ARM64 Contourtonist test uses native parameter search and curve
editing, saves/reopens with exact opaque state and stable target preservation,
and checks finite, partition-consistent rendering. It does **not** establish
audible Contourtonist modulation. Its
[v0.2.2 processor](https://github.com/stoatworks-labs/contourtonist/blob/v0.2.2/Source/PluginProcessor.cpp)
starts flat and publishes filter changes from a control timer after receiving
a measurement; this offline fixture supplies no measurement. Identical enabled
and disabled PCM therefore cannot qualify that vendor's audible automation.
This limitation also applies to earlier Contourtonist offline fixtures. It does
not invalidate their state/lifecycle checks or resolve OrbitCab's separate
partition discrepancy.

Private-desktop control and bounds checks are functional evidence, not
foreground aesthetic or sustained presentation qualification. Full parity
remains active: native instrument envelopes, absolute automation dispatch,
sample workflows, MIDI/recording/recovery, persisted workspace/accessibility,
reciprocal Mac reopening, x64 and sustained loaded audio remain open.

## Build and qualification

The ARM64 application was rebuilt in `bin/windows-parity`. Release executable
SHA-256: `B18EEF3BC8505F3AA011C68B916D7C51D145C3CC09CE1BA38000426E3A55CCD8`.

The final full application suite passed **179 tests, zero failures and zero
skips, in 349.882 seconds**. It includes all 13 new automation cases plus the
installed effects/Surge XT lifecycle, editor, alias, preset, library, path repair,
routing, sample, pattern, formula, bank, worker and short silent device checks.
The common canvas/native-window changes are covered by those existing editor
regressions. The public JSON schema parses successfully.

The new cases cover batch validation, all transforms, deterministic previews,
no-op/dry-run, stale writes, conflicting FX, missing targets, linked protection,
independent patterns, history, native reopen, search, point/drag/keyboard
editing, retained drafts, formula/bank/rack connections, minimum-size bounds
and nonoverlapping toolbar controls. The missing-plugin removal display
regression failed against the preceding binary and passes in this build.

Enabled versus disabled audio comparisons changed quarter-second energy by up
to **239.88294032165675** for Gainer and **79.99111662632119** for the external
VST3 gain fixture. Both active fixtures and their disabled controls produced
**zero PCM partition delta** over the rates and blocks above. Contourtonist
also had zero partition delta, subject to its flat-filter limitation above.
The documents remained unchanged by rendering. These short offline comparisons
do not establish sustained loaded realtime performance.

Logs are `bin/windows-parameter-automation-build.log`,
`bin/windows-parameter-automation-tests.log` and
`bin/windows-parameter-automation-app-tests.log`. Fixtures, scene metadata,
enabled/disabled comparisons and PCM reports live in
`bin/windows-parameter-automation-evidence/`.
The preserved package is
`bin/windows-checkpoints/parameter-automation-20260921/`. Its manifest records
the source commit/tree, upstream, executable and all packaged file hashes;
the preceding graph-command checkpoint remains intact. Task-owned QA apps
were closed after qualification.

A fresh fetch found no changes beyond `bcfe0f8a7`, already integrated here.
The remote's default branch is `codex/screamseq`; it has no `main` branch.
