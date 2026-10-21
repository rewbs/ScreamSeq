# Contextual left panel

**Foreground follow-up:** with renewed user permission, the retained
`7fb99d0b2` app was inspected through Computer Use. Context changes and width
reclamation worked, but step-value clipping and stale control pixels after
layout changes were observed. [The visual review](CONTEXT-PANEL-VISUAL-2026-10-20.md)
records the source corrections and required recheck. They are unbuilt.
Earlier "Computer Use remains stopped" statements below are historical.

**Latest qualification:** the corrected native context group and actual-app
width/focus case both passed on `b1581c9c8`. See the
[20 October candidate results](RECIPE-SAMPLE-QUALIFICATION-2026-10-20.md).
The earlier failure below remains historical evidence; visual/DPI review is
still open.

## Scope and observed problem

Requested independently of Windows/Mac parity. The Windows workspace's fixed
164-DIP left strip combined a sample list, file/history buttons, song navigation
and pattern input controls regardless of the active task. Its width was embedded
in both legacy and region geometry. Mac appearance is not the design target.

## Source implementation

- Pattern/notes/effects: pattern and order navigation, octave, edit step and
  selected sound; precise notes, selection tools, arrangement, timing, sample
  discovery and rendering.
- Samples: retained native scrolling sample list, waveform/loops, library
  preview, import, audio recording and instrument mapping.
- Graph recipes: selected plugin parameters, Fit, groups/modulation, port trims,
  rack plugin discovery and the song mixing graph.
- Mixer: routing, selected bus effect/instrument routing, group/return creation
  and bus details.
- Plugin rack: discovery, vendor editor, selected-parameter automation/activity,
  instrument assignments and routing.
- Instrument and automation owners: related sound/routing/modulation tools.

These are shortcuts into existing owners. There are no new musical mutation
handlers, captured targets or Apply buttons. Graph recipes and the song mixing
graph remain distinctly named. Native editor focus determines presentation;
moving into the panel itself freezes its editor context so its actions do not
change beneath the musician. Parameter availability is reflected in the buttons.
Repeated unchanged frames reuse a presentation stamp instead of relaying out
native controls during playback.

Hide/Show remains reachable in the upper left even when hidden, in View, and
through a customizable Ctrl+B binding. Existing explicit keyboard profiles take
priority. The visibility flag belongs to named local layouts, with an optional
boolean and a visible default for old layouts. Hiding returns 164 DIPs to both
layout solvers; song metadata, history, pins, drafts and selected samples remain
owned by their original editors. File/history commands remain in native menus,
shortcuts and command search. OpenMPT attribution remains in About and notices,
and in the panel footer when visible.

Native buttons and selectors retain Tab navigation and accessible control names.
The text heading summarizes the active task; this is not an additional inspector.
Narrow/short layouts omit actions that would overlap the footer, leaving the
same commands available in menus and search.

## Files and ownership

`windows/App/WorkspaceContextPanel.inc` owns presentation, action dispatch and
inspection. `WorkspaceState.hpp` owns visibility and legacy geometry;
`WorkspaceDocking.inc` consumes the same content origin. `WorkspaceView.inc`
creates and lays out native controls; `WorkspaceDraw.inc` paints summaries.
`WorkspaceLayouts.inc` validates and persists the optional local field.
Menu, command and shortcut integration remain in their existing modules.
`Main.cpp` exposes the presentation through `workspace.get`.

## Bounded qualification

**20 October UTC: Windows ARM64 build passed; seven of eight targeted test
groups passed. The context-panel group failed and remains unqualified.**
Source `e79133155d62c5eff0ceaf15beba6f987b96256f` built from 03:48:08 through
03:49:50 UTC, after the previous hourly gate, with two workers. App and workspace
test targets share the `ContextPanel-Release` output directory. App SHA-256 is
`4c01af17b6e9f0fc4112e9f61442b90868b988fb6baf17d1dc8a336609367dc0`.
Build receipts, exact manifests, native XML/log and schedule audit are retained in
`bin/context-panel-20261019/`. No next build before **04:50 UTC**.

Restore (48.79 s), advanced restore (33.94 s), command results (74.07 s), parameter
preview (4.81 s), recipe edits (4.09 s), menu bar (0.88 s) and pattern display
(3.55 s) passed. Total test time was 171.12 s. Registration audit found 198
registered tests, none unscheduled or repeated.

The context test failed at `Visible context origin differs in region layout`
after its visibility, Tab order, saved/legacy/malformed layout, capture refusal,
sample focus and graph context assertions completed. Source inspection identified
an invalid fixture assumption: its initial physical window may be monitor-capped,
and docking/focusing native notes can select compact tabs, where the hidden
tracker rectangle is intentionally empty. The source correction uses the existing
exact-DIP resize helper before asserting wide region geometry, explicitly checks
the region mode, and adds compact native-tab width/focus checks. This correction
has **not been rebuilt or executed**; the original failure is retained. Later
retained-draft assertions in that group did not execute.

An additional actual-app PID-pipe/native-message check passed against the same
unchanged executable: `WorkspaceDockingTests.test_context_panel_width_and_compact_native_selection`
(2.779 s). It establishes a 1600×900-DIP region layout and a 1000×720-DIP compact
layout, verifies 164-DIP reclaimed width in both, and preserves the native notes
HWND, target, raw fields, selected event, tools, focus, context and document.
Log: `bin/context-panel-20261019/application-context.log`. An initial harness
attempt incorrectly requested an omitted full `draft` array; its KeyError is
retained in `application-context-initial-shape.log`. The corrected case uses the
published `draftCount`, `selectedEvent`, raw fields and unchanged document.
This pass is independent evidence of application behavior, not a retroactive
pass for the failed C++ group or a foreground visual review.

Five additional actual-app interaction cases passed on the same unchanged
executable on 20 October, **9.695 seconds**:
`test_pattern_backspace_clears_and_undo_restores`,
`test_real_menu_loop_allows_new_song`,
`test_retained_editor_global_bindings_preserve_focus_and_raw_draft`,
`test_browser_search_retains_focus_and_arrows_preview`, and
`test_native_preview_gain_applies_immediately_and_partial_text_preserves_target`.
Their test methods are unchanged since the built source. The first three live in
`windows/Tests/test_workspace_shortcuts.py`, the last two in
`windows/Tests/test_sample_library.py`. Logs and the executable/source receipt:
`bin/context-panel-20261019/interaction-regression-configured.{log,json}`.
The initial invocation stopped in fixture setup because TMPDIR was unset;
`interaction-regression.{log,json}` retains those five setup errors. Setting the
existing required scratch-directory variable allowed the tests to execute.
These are isolated native-message/API checks, not audible-preview or visual
qualification, and do not qualify subsequent unbuilt source.

Build only ScreamSeq and workspace-restore-tests for this Windows presentation
batch. Run the new private-desktop `workspace-context-panel-tests`, existing
workspace restore/region, menu and shortcut tests where their binaries match,
and the split parameter-preview/recipe-edit groups on the new test executable.
The new native case checks contextual control visibility, width reclamation,
hidden-control focus, named-layout round trips, old/malformed configuration,
mouse-capture refusal and retained native drafts without song/history changes.
Add reciprocal context checks at resized/DPI layouts before claiming ergonomics.

Broaden if focus or layout tests expose a shared dispatch regression. No shared
musical, project or Mac sources change in this batch, so the earlier exact-source
audio/codec evidence remains applicable to those unchanged components. This
does not qualify the separate recipe-preview commit `62bcbba1f`.

Computer use remains stopped after the user's Escape. Private-desktop handler
checks cannot establish real visual quality, readability, pointer ergonomics or
screen-reader presentation; those remain explicitly unverified.

## Follow-up limits

The panel does not yet offer user pinning/reordering or a persistent song browser.
Those should follow experience with the task-specific actions rather than add a
second asset browser now. Visibility is saved with named layouts, not as a new
global preference. Mac's independent native navigation is unchanged.
