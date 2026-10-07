# Independent Windows dock regions

Source and display audit, 2026-10-07. **Proposed; not implemented or qualified.**
This extends the retained editors in [WORKSPACE_DOCKING_PROGRESS.md](WORKSPACE_DOCKING_PROGRESS.md)
to match the connected-workspace goals in the user's
[UI philosophy](../doc/RESONANCE_UI_PHILOSOPHY.md).

## Current constraint

The read-only display query found one 2880x1800 monitor at 200% and 60 Hz,
with a 1440x852-DIP work area. Re-query before implementation; do not treat this
historical observation as a future machine setting. The query process was
DPI-unaware, so its virtualized 96-DPI result is not the physical display scale.

[WorkspaceDocking.inc](App/WorkspaceDocking.inc) supports one selected right
editor even when both Automation and Instrument have `right` placement.
Both retained tools currently require 440x500 DIPs. The graph shares the merged
lower editor rectangle. Moving these existing forms into shorter rectangles
would hide or overlap controls; a new region model alone is insufficient.

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
