# Windows plugin instrument aliases — 2026-09-21

The rack's **Instrument assignments** page and command palette open a modeless
native editor for several tracker instruments sharing one plugin. The window
captures the plugin's stable ID and document revision. Instruments retain their
existing names; each route selects MIDI channel 1–16. All aliases share the
plugin's sound state, parameter automation and audio outputs, as on Mac.

## Native workflow

Add an existing instrument, change its instrument/channel, remove a route, then
Preview or Apply the complete list. Choices exclude instruments already owned
by another plugin and duplicate entries. The first route is the primary shown
by the legacy single-instrument picker. A missing retained instrument is labeled
explicitly and can be replaced or removed.

Preview performs the real API dry run without modifying history or playback.
Apply uses one guarded plugin-history transaction and the existing structural
stop-before-publication path. This is not live alias publication. No editor
window, processor copy or extra audio adapter is created per alias.

The list, combos and buttons support keyboard navigation. F6 switches list and
instrument choice, Delete removes a selected route, Ctrl+Enter applies and
Ctrl+R reloads. Close/Escape retains the draft; reopening raises it, even when
the rack selection changed. Reload/discard explicitly refreshes the captured
plugin. Discard/close releases a draft when the plugin has been removed. Pending
worker requests disable editing and check captured document/generation before
installing results. Painting consumes cached state and has no continuous timer.

`workspace.get.pluginInstruments` reports captured identity/revision, inventory,
selection, routes, dirty/pending/stale flags and status. The minimum window is
620×420 logical pixels. Foreground aesthetics/accessibility are not qualified.

## API corrections found during integration

- `plugin.instruments.set` previously returned a plain get response. It now
  returns Mac's `wouldChange`, `dryRun` and proposed `routing` object. An unchanged
  assignment list does not rewrite preserved records or create history.
- `instrument.plugin.set` now returns `wouldChange`, avoids unrelated record
  normalization and validates plugin/adapter capacity during dry runs too.
- The legacy `plugin.assign` previously dropped other aliases. It now replaces
  the primary while retaining other aliases, preserves an existing alias's MIDI
  channel when promoting it, atomically removes the chosen instrument from its
  previous owner and clears all routes only for explicit zero.
- Alias writes require an instrument plugin, including empty lists; read metadata
  recognizes the retained Audio Unit music-device type as Mac does. Unavailable
  platform plugins remain inspectable and their opaque state is preserved.
- Shared duplicate/ownership/capacity validation now maps to invalid parameters
  (`-32602`), matching Mac, instead of a host failure (`-32003`). The stronger
  error-code assertion failed before this correction. Validation precedes
  playback stops and history publication.

Musical data, serialization and DSP formats are unchanged. These operations use
the existing shared assignment/capacity validators and plugin history. Full Mac
API parity is still a wider audit; these fixes cover the assignment family.
Assignment comparisons capture only the small routing lists, avoiding an extra
full-rack decode/copy of vendors' opaque sound states during validation.

## Evidence

The new API-contract regression failed against the preserved formula-workbench
checkpoint before the fix (`bin/windows-alias-before.log`). Native regressions
cover dry runs and no-ops, rejection of duplicate/owned/invalid routes, primary
promotion and owner transfer, exact Undo/Redo, unrelated opaque-field retention,
save/reopen, native draft guards and explicit recovery after source removal.

A real installed Surge XT instance was assigned to two native tracker triggers
on MIDI channels 1 and 9. The native controls, Preview/Apply, exact saved sound
state, save/reopen, removal and plugin Undo were exercised. This verifies the
assignment/lifecycle path; it does not claim that Surge supplies independent
multitimbral sounds or establish recorded MIDI input/long-session audio capacity.
Unavailable-AU fixtures separately verify inspectable cross-platform assignments
and opaque-state preservation; they are not AU playback tests on Windows.

The five focused actual-app tests passed in **8.648 seconds**, with no skips,
using the final ARM64 Release build. Test-owned private desktops and exact-PID
API connections preserve the musician's foreground window, clipboard and app.
The native minimum-window test checks every control and toolbar non-overlap.

Executable SHA-256:
`FE6DA67059CF1E75E554256D38601B2C25968F693B899648AE1D8CD259997E1A`.
Build log: `bin/windows-alias-build.log`.
Focused tests: `bin/windows-alias-tests.log`.

The complete app suite passed **128 tests**, no failures or skips, in
**204.123 seconds** against this executable. Installed Contourtonist, OrbitCab
and Surge XT caches, native provider fixtures and opt-in live parameter checks
were enabled. The live parameter stages recorded zero callback overruns; these
remain short, bounded fixtures, not sustained capacity qualification. The saved
scripted graph again produced zero PCM partition delta at 44.1/48/96 kHz with
17/128/4096/8193-frame blocks and one second per render.

Full log: `bin/windows-alias-qualified-app-tests.log`.
Plugin evidence: `bin/windows-alias-final-plugin-evidence/`.
Curve evidence: `bin/windows-alias-final-curve-evidence/`.
All owned test processes closed; foreground/clipboard preservation checks
passed and no system audio defaults changed.

The preserved package is `bin/windows-checkpoints/plugin-aliases-20260921/`.
Its manifest records source commit/tree and individual artifact hashes. The
formula workbench checkpoint `87764a790` remains preserved separately. Shared
DSP is unchanged from `d619ae439`; previous CTests remain historical evidence,
not additional runs against this UI/API checkpoint.

## Remaining work

Presets/library organization, explicit plugin-path resolution, missing-plugin
recovery, live opaque-state/structural publication and the OrbitCab partition
discrepancy remain. Surge's first-editor opaque zoom-state change still stops
playback. Native parameter automation/instrument envelopes, song overview,
recording/recovery, persisted floating workspace, accessibility and global
shortcut parity, foreground presentation, x64, reciprocal Mac reopen and long
loaded realtime qualification remain on `PARITY_PLAN.md`.
