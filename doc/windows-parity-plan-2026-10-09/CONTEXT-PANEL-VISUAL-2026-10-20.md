# Context panel foreground inspection

The user explicitly renewed permission to inspect the isolated QA window.
Computer Use (`@oai/sky`) inspected the existing Windows ARM64 executable
from candidate `7fb99d0b2`, SHA-256
`586aade4af899b29f72d6ca6126db25cf3f30d1c23cfdb5d8e382511506f5001`.
This was the retained `RecordedTake-Release/ScreamSeq.exe` in the
`parity-ci-layout/ScreamSeq` checkout. No build was started.

There was no running ScreamSeq process before launch. The owned process was
PID 28084, launched with `--inspection --automation` into the demo document.
Its actual executable and command line were verified. Hardware audio was
disabled; the musician's settings/default audio route were not changed.
The owned window was closed through Alt+F4, and both window enumeration and
the process inventory confirmed its exit. No project was saved.

## Observed, not inferred from state tests

- The initial panel showed pattern/order, note-entry and composing actions.
- Clicking Plugins changed it to plugin discovery/editor/automation/routing
  actions. Unavailable plugin-specific actions were visibly disabled.
- Ctrl+B hid and restored the panel and visibly reclaimed its column for both
  the pattern and lower editor. The restore button remained reachable.
- Clicking Samples showed the sample list and sample-specific actions.
  Selecting Sub bass, then pressing Down, selected Soft kick and updated both
  waveforms while retaining the pattern cursor.
- Clicking Graph showed graph-recipe actions, with a distinct Song mixing
  graph link. With no graph selected, plugin-parameter and Fit actions were
  disabled.
- Native maximization expanded the layout and retained the sample selection.
  A preliminary border drag did not resize the window; it is not a narrow
  layout pass. Captures covered the initial 1286x762 and expanded 1440x854
  window surfaces, not multiple monitors or display-scale settings.

Two visible defects prevent a clean visual qualification:

1. The narrow step selector displayed `Step...`, concealing the value.
2. Hide/show and resizing left stale sibling-button pixels. For example, the
   Mixer tab displayed Notes; later sample action and inspector captions
   contained mixed fragments. Further observations retained those artifacts.
   Accessibility still reported the correct logical names, establishing why
   state/handler checks alone missed this defect.

The Computer Use accessibility snapshot consistently reported the top-level
window as focused, including after a sample text-field click. This is
inconclusive for retained native edit focus. No text was entered on that
uncertain focus and no foreground focus-preservation pass is claimed. Earlier
private-desktop retained-draft checks remain separate evidence. Narrator,
high contrast, other DPI settings, audible preview and physical audio were
not tested here.

## Source corrections, awaiting the next build

`NativeControls::place` now prevents copying old client pixels during changed
geometry and invalidates the whole moved child. This addresses the observed
stale-pixel mechanism; it is a source correction, not yet a demonstrated visual
fix. Identical cached geometry still does no layout/repaint work. It does not
change control text, native selection or keyboard focus.

The context panel gives octave and step their own full-width rows, keeping
both meaning and value visible. Sound selection and actions move down one
row; the existing short-window action clipping policy remains.

Prepared native checks cover complete moved-field invalidation, idle repeated
layout, retained text/selection/Undo/focus, and measured space for `Step 16`.
At the next hourly build, include ScreamSeq, `native-control-tests` and
`workspace-restore-tests` with the other prepared targets. Run the control,
context-panel and retained-layout groups, then repeat this visible hide/show
and maximize/restore sequence with samples, plugins and native edit focus.
Because the placement helper is shared by Windows editors, the next x64 CI
native-UI group is required. No Mac or musical model source changed.

The renewed foreground authorization covers this isolated QA inspection;
an earlier Escape is no longer the reason to defer these checks. Any new
human interruption must still stop input immediately.
