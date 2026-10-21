# Context panel foreground inspection

## Recheck: candidate 55d11e131

The single Windows ARM64 build ran from 23:06:29 to 23:09:35 UTC on
2026-10-20. Source was the clean frozen commit
`55d11e131f6e74916d3aa2fb79fcb83bf72a01c1`; executable SHA-256 was
`525f0d8ce08ab6669022cc4a1053041f1790d766707e169e5d401a583c372a87`.
Output: `parity-ci-layout/ScreamSeq/bin/windows-parity-p1/ContextRepaint-Release`.
Build and check receipts are in the primary checkout's
`bin/context-repaint-20261020/`. No second build was run. The next build is
not before **2026-10-21 00:09:36 UTC** (17:09:36 Pacific on October 20).

All eleven selected CTest registrations passed, accounting for the isolated
rerun described below. They cover controls, context panel, advanced retained
workspace, parameter preview, recipe edits, notification intake, recorded
automation, live parameter/recorded publication and recovery, plus the scratch
fixture setup. The private-desktop real-vendor ownership case also passed.
The context regression now retains a partially typed sample range, exact
text selection and GetFocus across both sidebar toggles, then cancels it.

The first recipe run passed both functional cases but failed its foreground
preservation guard because visible QA interaction overlapped that test. The
original failing log is retained. With GUI input stopped, that test alone
passed unchanged in 4.77 seconds. Keep visible QA and private-desktop observers
sequential; do not disable this guard. No broader suite or rebuild was repeated.

Computer Use inspected owned PID 4868, with the executable path and
`--inspection --automation` command line verified. In the disposable Midnight
Circuit demo, live observations established:

- `Step 1` is fully readable in the widened selector.
- Sidebar hide/show reclaims width; sample, plugin and graph contexts show
  their appropriate actions. Captions remain clean through repeated toggles
  and maximize/restore at 1286x762 and 1440x854 screenshot dimensions.
- Clicking the sample Start field then Ctrl+A visibly selects its text.
  Typing `12`, restoring the sidebar with Ctrl+B, then typing `3` yields `123`
  in that same field, without changing the pattern cell or sample.
- Hiding the sidebar again and pressing Backspace changes the field to `12`.
  Tab moves to End (Ctrl+A visibly selects `256`); Shift+Tab returns the caret
  to Start. Escape cancels the draft and restores `0`, with no song save.
- The previous mixed button captions did not recur in these sequences.

The accessibility snapshot still reports the top-level window as focused,
even when actual keyboard input and the visible caret establish child edit
focus. Some immediate accessibility values also lag the corresponding image.
This is an unresolved accessibility/provider-or-observer question, not a
keyboard focus failure or a screen-reader pass. Investigate the custom root
provider/host-child focus handoff separately; do not substitute this snapshot
for direct native focus assertions or visible keyboard evidence.

Alt+F4 closed only the owned QA process; its persistent launch session exited
successfully and process inventory confirmed exit. Hardware audio remained
disabled. No audible-preview, Narrator, other DPI/multi-monitor, narrow-window
drag-resize or sustained presentation-performance claim is made. Windows x64
native-UI CI and both Mac shared-header builds remain pending for this candidate.

## Earlier inspection: candidate 7fb99d0b2

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
