# Independent Windows dock regions

Initial source and display audit, 2026-10-07. **Region integration is implemented
in the separate `independent-dock-regions` worktree; bounded qualification passes
393 app cases, 48 native targets, 33 focused cases and 37 reviewed views.**
See [current implementation and bounded evidence](INDEPENDENT_DOCKING_PROGRESS.md).
This extends the retained editors in [WORKSPACE_DOCKING_PROGRESS.md](WORKSPACE_DOCKING_PROGRESS.md)
to match the connected-workspace goals in the user's
[UI philosophy](../doc/RESONANCE_UI_PHILOSOPHY.md).

## Baseline constraint

The read-only display query found one 2880x1800 monitor at 200% and 60 Hz,
with a 1440x852-DIP work area. Re-query before implementation; do not treat this
historical observation as a future machine setting. The query process was
DPI-unaware, so its virtualized 96-DPI result is not the physical display scale.

At the start of this work, [WorkspaceDocking.inc](App/WorkspaceDocking.inc) supported one selected right
editor even when both Automation and Instrument have `right` placement.
The initial audit found both retained tools required 440x500 DIPs. Short docked
forms now pass their native foundation tests; floating minima remain unchanged.
The baseline workspace used one selected right editor, with the graph in the
merged lower rectangle. The implementation now uses the regions described below;
the progress report records final qualification, historical failures and limits.

## Proposed bounded implementation

Use independent right, bottom and secondary hosts, informed by
[Mac DockWorkspace](../mac/App/DockWorkspace.swift). Preserve the current HWNDs,
raw fields, captured targets, canvas viewports, caret/selection and return points.
Placement changes must not reload data or create musical Undo.
New contextual panels should follow the cursor by default, as the user's
philosophy requires; retain explicit existing pins. Following must still refuse
to redirect a pending edit or overwrite any retained draft. Distinguish a
requested follow state from a target that could not yet change safely.

For the near-maximized current display, investigate pattern upper-left,
instrument upper-right, graph lower-left and automation lower-right. Start with
roughly 800 DIP left and 440–460 right, then measure actual frame/control bounds.
Hide the separate Notes/Samples inspector in this preset so it does not consume
another 340 DIP. Add short docked canvas layouts with scrollable detail sections;
keep canvas interaction and Apply reachable at every supported size.

Replace the single active editor with independent region selections and give
Graph an explicit lower-left rectangle. Expose region focus, placement, pin,
follow and return through existing workspace commands and saved layouts. Use
strict backward-compatible preference parsing; old saved layouts must still
restore without dropping drafts. Graph floating can follow separately.

At the current default/minimum sizes, retain tabs and saved layout switching.
Do not promise four useful simultaneous editors by reducing type size. A resize
must preserve the selected region and deliberate keyboard focus, reveal a useful
fallback and release held musical input when its owner becomes hidden.

## Qualification and later work

Exercise simultaneous visibility, actual native-control bounds/intersections,
independent pins/targets, dirty drafts across dock/float/layout changes, keyboard
routing, resize fallback, hide/return, and unchanged song revision/playback.
Review actual current-display and minimum renderings, including long labels and
invalid drafts. Keep foreground, other scales, accessibility and reciprocal Mac
runtime as separate qualification boundaries.

Graph's Pattern curve page currently replaces its routing canvas. Simultaneous
graph-source automation requires an independent curve host later; the proposed
three-region work must not claim that separate gap complete.

## Refined source audit and implementation boundary

The existing Instrument compact canvas begins at y258, with controls through
y350. Automation's canvas begins at y196 and its footer consumes 116 DIPs;
Formula/Tools detail controls reach y384. Neither existing 440×500 form can simply
be placed inside a 300–310-DIP dock body. Add an explicit short-dock layout while
preserving the current floating minima and wider forms.

Keep the same HWNDs, page controls, captured targets/revisions, raw fields, caret
and draft generations. The Curve/Envelope pages need compact page/target chrome,
a roughly 100–120-DIP canvas, a point-edit row and fixed Apply/Reload/status.
Keep every other action reachable on retained detail pages or a scrollable detail
section. Scrolling the whole existing form would hide the canvas and Apply and
does not satisfy this layout. Do not reduce the font to force a fit.

Graph's current controls end at y271 and status starts at h−23. Investigate a
304-DIP body at roughly 800-DIP width, giving about 205 DIPs of routing canvas.
Validate all graph pages and curve hit-testing before lowering the current
380-DIP lower-editor minimum. Route graph drawing, input, context menus and timer
visibility through its own rectangle. They currently depend on the tracker and
the merged lower-editor rectangle; merely changing geometry is insufficient.

Use compact local region headers in the simultaneous layout, replacing the
current full-width 34-DIP editor toolbar there. Preserve the tall tab fallback
below the useful four-pane threshold. Never enlarge the owner past its work area
to satisfy the old 666-DIP client-height clamp. Re-query the real display/client
before selecting and qualifying the final threshold and split limits.

Keep the saved-layout catalogue's bounded version-1 envelope, atomic writes and
compare-and-swap behavior. Version the editor configuration separately. Accept
both existing seven-field layouts and eight-field layouts containing the exact
legacy `{locations:[automation,instruments],active,tracker}` configuration.
Migrate legacy placements to the right region without losing the active tab or
tracker selection. Validate and prepare the entire new configuration before
changing presentation. Saved layouts must not own or overwrite live pins,
captured targets, return points or drafts. New editors may follow by default;
already explicit pins remain, and a requested follow held by a draft is visible.

Current ownership: root integrates the region model and Main/graph/workspace
behavior; the automation editor's short form is isolated to its existing header.
The matrix worktree and its qualification binaries remain separate and frozen.
