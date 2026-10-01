# Windows graph pattern curves — 2026-09-21

This checkpoint adds the native graph automation point editor and fixes a shared
modulation sampling defect exposed by its saved-project audio test. Full Mac
parity remains in progress. The upstream baseline is still `bcfe0f8a7`.

## Native editor

Select an automation source in the Graph dock and choose **Pattern curve**.
The editor offers point insertion/dragging, cancellation, keyboard movement,
row/half-row/quarter-row/1⁄256-row snap, zoom/fit/pan, numeric row and percent
fields, enabled state, ramp/clear, all nine outgoing curve types and a formula
field with Check/preview. Ctrl+Enter applies the staged curve as one document
Undo step. Script expressions use the existing bounded mathematical language.

The draft captures its document, graph, source, pattern and revision. Selecting
another graph node does not redirect unfinished edits. Changing pattern rejects
while a draft is pending and restores the visible selection. Field validation,
point collisions and stale writes preserve the draft. Reload commits its new
target only after the read succeeds; completion generation checks preserve newer
fields typed while a worker request is in flight.

The canvas retains screen geometry and worker-evaluated preview samples. Drawing
does not compile formulas, query the document or access plugins. Point handles
at the canvas edges remain clickable. Native controls retain keyboard focus
through preview/layout, and the 900×620 control bounds are covered by tests.
An absent curve displays zero without calling the nonempty formula-preview API.

The page uses the existing `graph.automation.get/set` and
`automation.formula.preview` contracts. `workspace.get.graphCurve` exposes its
captured target, draft/field flags, point selection, viewport and preview status.
No file-format or project-metadata version changes were required.

## Shared audio correction

The first native scripted-curve fixture failed the existing 1e-6 partition
comparison: maximum PCM difference was **2.14576721191e-6**. Re-rendering that
same saved file with the preserved `graph-editor-20260921` executable reproduces
the mismatch. The prior graph runtime fitted each truncated callback to its own
modulation endpoints, so nonlinear curves were approximated differently.

`SignalRuntime` now aligns the 32-sample approximation grid to the absolute song
sample clock. Automation and LFO samples use that grid even when a callback ends
inside it. Pattern/point boundaries and step-at-start discontinuities constrain
the interpolation interval. The fix uses bounded arithmetic and existing
storage; it adds no allocation, locks, script parsing or vendor calls to DSP.
Plugins still receive bounded ramps through the existing scheduling interface.

The saved native fixture now has **zero PCM partition delta** at 44.1/48/96 kHz,
with 17/128/4096/8193-frame partitions and one second per render. It contains a
scripted pattern source modulating a real built-in Gainer in an assigned graph.
The shared regression also compares smooth, exponential/logarithmic and reversed
curves, step-at-start and final scripted segments across 1/7/17/128-frame calls,
with fractional point crossings and a nonzero absolute start.

This is bounded evidence for these fixtures, not a guarantee for every vendor,
modulation combination or realtime workload. The existing OrbitCab partition
discrepancy remains a separate unresolved issue. The shared source and portable
test are also used by Mac; a fresh native Mac build is still required.

## Qualification and artifact

ARM64 Release executable SHA-256:
`33F3EFE40736D2BDB9063E961E74B1B7591A4C4169DBC18ACCD39EE635B4AA22`.
Package: `bin/windows-checkpoints/graph-curves-20260921/`. Its manifest records
the exact source commit/tree and packaged hashes.

- **106 app tests passed**, no failures or skips, in **140.113 seconds**.
  Installed Contourtonist, OrbitCab and Surge XT caches, provider fixtures and
  opt-in live parameter tests were enabled.
- The seven new curve tests cover points/fields, collisions, drag cancellation,
  snap/keyboard use, captured targets and patterns, failed formulas, preview
  values, focus/bounds, Undo/Redo, unrelated-pattern preservation, save/reopen
  and the actual saved-project audio path.
- **29 rebuilt CTests passed**: 26 portable cases in 17.71 seconds and three
  hosted/project cases in 0.48 seconds. Existing hosted allocation checks pass;
  a full host allocation/free/lock and sanitizer audit is not claimed.
- Tests use owned private desktops/processes and disposable projects. The user's
  foreground window, clipboard sequence and system audio defaults are preserved.

Evidence:

- `bin/windows-curve-qualified-build-tests.log`
- `bin/windows-curve-qualified-app-tests.log`
- `bin/windows-curve-hosted-tests.log`
- `bin/windows-curve-dsp-tests.log`
- `bin/windows-curve-evidence/native-graph-curve.screamseq`
- `bin/windows-curve-evidence/before-sampling-fix.json`
- `bin/windows-curve-evidence/native-graph-curve-pcm.json`
- `bin/windows-curve-plugin-evidence/live-parameters.json`

## Remaining scope

Song routing overview, pattern-parameter automation, expanded formula workbench
and reference/completion UI, both envelope-bank levels, instrument envelopes and
graph command lanes remain. This dock still switches editors; simultaneous,
floating and saved workspaces need completion. MIDI/recording/recovery, device
selection, plugin presets/resolution, live opaque-state/structural publication,
x64, reciprocal current-format Mac reopen and long loaded qualification remain.
Private-desktop control tests do not establish foreground aesthetics,
accessibility or sustained 60 Hz presentation.
