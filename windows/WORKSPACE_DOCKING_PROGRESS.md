# Retained workspace editors — implementation checkpoint

This 2026-10-07 checkpoint covers the Windows workspace dock for pattern
parameter automation and instrument/envelope editing. Source defines the
behavior described below. The docking implementation passed 298 actual-app
tests and 34 native tests. Final presentation and keyboard-focus refinements
passed 49 focused actual-app tests, with 24 source-matched rendered views
reviewed. The exact builds and evidence are recorded below.

## Implemented scope

`App/WorkspaceDocking.inc` connects the existing retained native editors to a
shared workspace dock. Their windows and child controls are reparented when
docking/floating, preserving field text, selections, drafts and captured musical
targets. This is a native presentation change; musical commands still use the
existing guarded document/API transactions and Undo domains.

At a workspace client width of at least **1424 DIP**, the active editor occupies
a **460-DIP right dock** beside the tracker. The tracker and its selected lower
editor, including the graph, remain available. Below that width, a tab strip
selects Tracker, Automation or Instrument in the main body. Both editors retain
their right placement and state; one shared dock editor is visible at a time.
The workspace maintains at least 666 DIP of client height while docking, leaving
500 DIP for the editor. Pattern focus temporarily suppresses the dock and keeps
its placement for return to another layout.

The toolbar's existing Automation and Instrument actions open their retained
editor using its preferred placement, initially floating. The palette adds
**Workspace / Dock automation beside the tracker** and **Workspace / Dock
instrument beside the tracker**. **Ctrl+Alt+D** within either editor toggles
dock/float. The dock header has Tracker, Automation, Instrument, Float, Hide,
Pinned/Following, Cursor and Return controls. Ctrl+K and Ctrl+Alt+W inside these
editors open the command palette and layout manager. Existing editor-local
keyboard commands and intentional text input remain local.

The parameter editor uses compact Target, Curve, Formula and Tools pages.
The instrument editor uses Envelope, Points, Tools, Instrument and Keymap pages;
Points includes the point fields and playback markers. Compact pages change
visibility and bounds of retained controls, rather than rebuilding them.
Wide/tall floating windows retain their all-in-one presentation. Both floating
editors require at least 440×500 DIP of client space. Hidden canvases reject
pointer, wheel and canvas-edit key input. Page changes do not apply music.

## Context and draft rules

Automation/instrument panels start **pinned**, retaining their captured target.
Each has an independent pin and initial return point. Cursor/unpin requests
attempt to follow the current editing context but preserve pending work, raw
fields, staged changes, drags and retained bank/formula drafts. Raw tool/range
fields also prevent implicit retargeting. Clearing the pin does not imply that
the requested target has already replaced a retained draft.

Automatic following considers only visible, unpinned editors while the
document is idle and focus is outside that editor. Reload and From cursor are
explicit editor actions. Return selects the tracker, disables playback-follow
and navigates to the original opening position. Editor return points resolve
stable pattern identity after reordering; a replaced document or removed
opening pattern rejects the operation.

Moving, hiding or selecting a dock does not apply drafts or create song Undo.
Existing stale-document/revision and draft-generation checks still govern
musical Apply. Layout changes retain current pins, targets and drafts instead
of capturing historical musical context.

## API and saved arrangements

`workspace.panel` supports these placement sets:

| Panel | Placements | Default pin |
| --- | --- | --- |
| `notes`, `samples` | `right`, `hide` | Following |
| `automation`, `instruments` | `right`, `float`, `hide` | Pinned |

`focus`, `pinned`, `follow` and `return` are booleans. Workspace requests accept
neither song nor context revision tokens. Unknown fields/panels/placements
reject with `-32602`; editor placement while a document operation is active
rejects with `-32002`. Existing notes/sample inspector semantics remain in
place. See [`Api/README.md`](Api/README.md#workspace-subset) and
[`Api/workspace.schema.json`](Api/workspace.schema.json) for request details.

`workspace.get` adds editor entries to `panels`, `locations`, `pins`, `targets`,
`inspection`, `visible` and `returnPoints`. Its `editorDock` contains `mode`
(`none`, `side`, `tabs`), `active`, `trackerVisible` and DIP bounds. The existing
`parameterAutomation` and `instrumentEnvelope` snapshots expose page, canvas
visibility, captured target and draft diagnostics. `right` continues to name
the selected notes/sample inspector.

Named layout configurations now include automation/instrument locations,
selected dock editor and Tracker-tab selection, alongside the base preset,
inspector visibility, sizes and lower-editor/collapse state. Restoring an older
configuration without editor placement leaves the current editor placement
alone. Current pins, cursor, targets, return points and existing editor drafts
remain unchanged. An unopened editor is initialized when needed by the restored
arrangement.

Normal sessions keep layouts in
`%LOCALAPPDATA%/org.resonance.tracker/workspace-layouts-v1.json`; inspection and
audio qualification use memory only. The existing bounded catalogue, atomic
replacement and concurrent-file-change checks remain in effect. This is UI
preference storage, separate from native song serialization.

## Qualification status and reproduction

The focused candidate build is `bin/windows-ui-parity/Release/ScreamSeq.exe`.
The first focused run exposed client-size clamping and test-side field-read
issues. Native frame sizing now calculates the minimum client size at the
window's actual DPI. A real keyboard regression also reproduced lost focus
while Reload disabled its controls; guarded restoration now preserves that
focus unless the musician has chosen another target.

Candidate 8 passed **298/298 actual-app tests**, without failures or skips, in
702.939 seconds. The private-desktop runner exited successfully. **34/34 native
tests** passed in 15.67 seconds. Its executable SHA-256 is
`626e4a8230bcced200bbc2d1c7150a3b7ec9621f6d7db7f8b8382aea889417ec`.
Evidence is in `bin/windows-docking-final-app-tests.log`,
`bin/windows-docking-final-isolation.log` and
`bin/windows-docking-final-ctest.log`. The six compact instrument tests also
passed independently in 32.191 seconds in
`bin/windows-instrument-compact-candidate8.log`.

Final candidate 11 includes selected dock-tab paint, instrument heading/status
presentation, narrow-layout Return visibility, keyboard-focus corrections and
the compact Formula page's corrected guidance.
Its executable SHA-256 is
`db10d343dc10ee75cfca8a83719c3794e9952a6ef915be2066371c5df771ba8c`.
It passed **49/49 focused actual-app tests**, without failures or skips, in
120.327 seconds, with isolation exit 0. Evidence is in
`bin/windows-docking-candidate11-ui-tests.log` and
`bin/windows-docking-candidate11-ui-isolation.log`. This run includes all six
compact instrument tests and the silent-playback keyboard-focus checks. The
full-suite result above applies specifically to candidate 8.

The preceding candidate 10 failed run remains in
`bin/windows-docking-candidate10-ui-tests.log`. One existing assertion assumed
that right placement always selected the editor; it now requests focus
explicitly after Tracker-preserving placement. A silent-playback fixture sent
Stop during startup Play; it now waits for playing, audio-active and idle state
before stopping. Both test corrections passed in the final 49-test run.

All **24 renderer/native-control compositions** from candidate 11's source
snapshot were reviewed. Twenty-three PNGs were byte-identical to the already
inspected candidate 10 set; the changed Formula page was inspected again. The
views cover default, graph, notes and collapsed
workspaces, the layout manager, wide and narrow dock arrangements, all four
compact automation pages and all five compact instrument pages. Every visible
direct native child's bounds, including children of the retained dock editors,
were positive, within its parent and free of intersections. The real display
was 2880×1800 pixels at 192 DPI; the largest workspace client was 2880×1759 pixels,
the smaller workspace was 2114×1438, and compact floating editors were 880×1000.

Images, the exact source manifest and `capture-evidence.json` are in
`bin/ui-capture/evidence-docking-final-3/`. The scratch capture executable hash is
`9e1a25f98c8685202ad33f30d0a9bdd66c5583c0307dd949120ac5c4b91cb7d7`.
The harness copied current application source, added scratch-only GPU readback,
and composited native controls through same-process print messages. Owner-drawn
buttons explicitly invoked production drawing callbacks. Source fingerprints
matched at capture; the private desktop was never switched, and foreground
window and clipboard were unchanged. These images establish rendered layout
evidence, not foreground interaction or sustained presentation performance.

The new actual-app suites use owned processes and real HWND/API paths:

- `Tests/test_workspace_docking.py`: same-HWND dock/float/hide retention,
  responsive side/tab arrangements, local keys and focus, saved layouts with
  simultaneous retained drafts, pin/follow/return and invalid requests.
- `Tests/test_parameter_compact_ui.py`: every visible native control's bounds,
  retained raw/staged curve state, hidden canvas input, keyboard navigation,
  transforms, bank and formula access.
- `Tests/test_instrument_compact_ui.py`: every visible direct child's bounds
  across five pages at 440×500 and 650×800 client sizes, wide-view restoration,
  retained HWNDs/field text/native selection/keymap scroll, combined draft Apply
  and Undo, hidden canvas input, keyboard focus including asynchronous Reload,
  and guarded raw-tool following.

Set `SCREAMSEQ_TEST_EXE` to a separate QA build. Run through the isolated runner
with a new absolute log path, for example:

```powershell
python -X utf8 windows/Tests/run_isolated.py --log C:\QA\docking-app-tests.log --test test_workspace_docking --test test_parameter_compact_ui --test test_instrument_compact_ui
```

Keep the musician's existing process and audio settings untouched. Actual native
window tests do not establish foreground rendering or performance. The rendered
checks above cover the wide side dock, narrow Tracker/editor tabs and compact
pages at the real display DPI, and explicitly record their compositing method
and executable/source fingerprints.

## Remaining limits

This is one shared dock for two retained editors, not arbitrary panel placement.
Notes/sample inspectors do not float through this API. Independent dock groups,
drag-to-dock rearrangement and simultaneous lower editors remain unavailable.
Two automation/instrument views can coexist by floating one or both editors;
the shared right dock itself selects one editor at a time. Narrow layouts trade
simultaneous tracker visibility for explicit tabs. Floating-window bounds and
compact page selection are retained by their live editor instances; named
workspace layouts do not serialize those details.
