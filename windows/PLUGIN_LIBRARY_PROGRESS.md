# Windows plugin library — 2026-09-21

The rack's **Browse…** button and command palette open a retained native plugin
browser with search, kind/format/category filters, favorites, hidden entries and
custom categories. Plugin name, format/kind and category have separate drawn
columns. The existing quick picker also excludes hidden entries after preference
changes. The `plugin.library.get/set` contracts follow the reviewed Mac source.

## Native behavior and performance

Typing/filtering uses a retained catalog with a short local debounce; it does
not read preferences, scan plugins or send worker requests per keystroke.
Reload/Rescan explicitly refresh catalog/preferences. Painting uses cached rows
and has no continuous polling. Discovery and preset inspection also avoid the
former unnecessary copy of every vendor's opaque rack state.

Selections use stable catalog IDs. A row disappearing under a filter is
deselected, never silently retargeted. Add uses the selected descriptor and the
current document's captured revision. It goes through the ordinary plugin API
and Undo; preference changes have no musical history.

F6 switches search/list, Ctrl+F focuses search, Ctrl+R reloads and Enter adds a
selected row or applies the category field. Category drafts disable unrelated
selection/filter edits, survive Close/reopen and remain after stale-write
rejection. Apply category saves; Escape or explicit Reload discards the draft.
The browser is usable at its 640×540 logical minimum; native bounds/focus checks
are included. Foreground visual/accessibility quality is not qualified here.

## Independent preference storage and API

`plugin.library.get` decorates descriptors with stable catalog identity,
favorite/hidden flags, custom/effective category and returns revision, warning
and availability metadata. Search is case/diacritic insensitive. VST3 identity
includes normalized installation path and uppercase class ID; display names do
not participate. Built-ins use effect IDs and AU uses component codes. Unlike
sound presets, library preferences deliberately distinguish installation paths.

`plugin.library.set` uses only `expectedLibraryRevision`. The document adapter
advertises that guard and retains successful exact-request replay. These calls
bypass musical revision checking, editor-state capture, history allocation and
transport changes. No-op/dry-run calls preserve exact disk bytes and revision.
Categories are trimmed; default preferences remove that entry. Missing plugins
do not erase preferences.

Normal sessions use the legacy-compatible per-user path under
`%LOCALAPPDATA%/org.resonance.tracker/plugin-library-v1.json`. Inspection/audio
qualification sessions require an explicit private `--plugin-test-library`
path and never automatically use the musician's preferences. Read/no-op/dry
paths do not create folders or files.

Files are bounded to 2 MiB and 4096 customized entries. Parsing rejects duplicate
decoded keys, excessive nesting, oversized text and invalid schemas/types.
A nonblocking path-keyed cross-process mutex protects read/modify/write.
Writes use the existing flushed staging/atomic replacement path; failed
publication retains the original and cleans up staging data. Stale writers fail
with `-32001`; a held lock fails writes with `-32002`.

Corrupt, locked or inaccessible preferences return a warning and empty revision
from library reads while discovery and insertion stay usable. The UI disables
preference editing and preserves the original file. Reload retries explicitly.

## Evidence

Seven focused app tests passed, no skips, in **15.463 seconds** before the final
quick-picker integration. They cover dry/no-op/trim/replay, Unicode search,
filters, capacity/malformed/duplicate-key limits, shared-instance stale writes,
nonblocking locks, failed atomic publication, corrupt preference preservation,
native controls, retained stale drafts, keyboard focus and minimum bounds.

Installed Contourtonist, OrbitCab and Surge XT entries were filtered. Surge was
inserted and undone, while favorites survived. A separate app reading a scanner
fixture with the same class/path and a changed display name retained preferences.
The scanner's strict uppercase class-ID cache invariant remains intact.

An owned silent WASAPI instance stayed active through three preference edits,
with advancing frames, unchanged document and zero callback overruns. This is a
short bounded check, not sustained audio-capacity qualification.

Build log: `bin/windows-library-build.log`.
Focused log: `bin/windows-library-tests.log`.
Full application log: `bin/windows-library-app-tests.log`.
Final executable SHA-256:
`10605EB5769A132CFA47ADD5ABB0FBEB8595F2647BDB777FFC56AAC2C7D2D13B`.
The full application suite passed **143 tests**, no failures or skips, in
**192.616 seconds** against the final executable, including the quick-picker
hidden-entry assertion. Installed-plugin caches, native provider fixtures and
live parameter tests were enabled. Gainer/Contourtonist/OrbitCab's bounded
parameter stages recorded zero callback overruns. The scripted graph fixture
again had zero PCM partition delta at 44.1/48/96 kHz and
17/128/4096/8193-frame blocks, with one second per render.

The API transport and replay-cache targets were freshly configured/rebuilt in
`bin/windows-library-api/`: **2 CTests passed**, **4.54 seconds**. Log:
`bin/windows-library-api.log`. Earlier shared DSP CTests remain historical;
this checkpoint does not claim a new complete realtime or sanitizer audit.

Evidence is retained in `bin/windows-library-plugin-evidence/` and
`bin/windows-library-curve-evidence/`. Test-owned processes closed and the native
desktop tests passed foreground/clipboard preservation checks. System audio
defaults and the musician's preferences were unchanged.

The package is `bin/windows-checkpoints/plugin-library-20260921/`, with source
commit/tree, individual hashes, app/scanner/API-test executables, dependency
notices, logs and bounded audio/PCM evidence. The preset checkpoint `b2a652873`
remains preserved separately. Full visual/presentation qualification is pending.

## Remaining work

Explicit missing-plugin/path resolution, live opaque-state/structural
publication and the OrbitCab partition discrepancy remain. Surge's first-editor
opaque zoom-state change still stops playback. Song overview, native parameter
automation/instrument envelopes, recording/recovery, persisted workspace and
accessibility/shortcut parity remain. Foreground visual/60 Hz qualification,
x64, reciprocal Mac reopen and sustained loaded realtime audits remain open.
Full parity is active; see `PARITY_PLAN.md`.
