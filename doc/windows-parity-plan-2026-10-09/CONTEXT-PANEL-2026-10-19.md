# Contextual left panel

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

This checkpoint records source work, **not a successful build or visual review**.
The preceding build completed at 02:47:43 UTC on 20 October. The next build must
start no earlier than **03:48 UTC**. This change will share that build with the
already committed split of the oversized parameter/recipe UI test group.

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
does not qualify the separate, uncommitted recipe-preview work.

Computer use remains stopped after the user's Escape. Private-desktop handler
checks cannot establish real visual quality, readability, pointer ergonomics or
screen-reader presentation; those remain explicitly unverified.

## Follow-up limits

The panel does not yet offer user pinning/reordering or a persistent song browser.
Those should follow experience with the task-specific actions rather than add a
second asset browser now. Visibility is saved with named layouts, not as a new
global preference. Mac's independent native navigation is unchanged.
