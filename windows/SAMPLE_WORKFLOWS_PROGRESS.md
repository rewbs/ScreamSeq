# Windows sample workflow checkpoint — 2026-10-07

This checkpoint closes three concrete interaction gaps against the Mac sample
editor: independent normal/sustain loop rows with joint preview and Apply,
clipboard paste preview with rate and gain controls, and snapping with loop
targets, grid origin and automatic selection snapping. The shared sample
operations already implement the musical behavior, compact Undo and native
project persistence. This work connects the Windows editor to those operations;
it does not change the project format or introduce a separate PCM algorithm.

Candidate 3 passes the complete 330-test application suite, all 32 affected
application tests and all 36 native CTests. A later isolated focus probe
reproduced a remaining native-focus issue. Final candidate 4 contains that
correction and page-specific Ctrl+Enter menu hints; all 33 affected application
tests pass individually and its 19 rendering views are reviewed. **The final
outer test run failed strict foreground isolation with exit 1; the foreground
change is unattributed and final-build isolation qualification remains
unresolved.** Candidate 3's full 330-test run passed strict isolation. Earlier
workspace reports are historical evidence for their own checkpoints, not
qualification of these new sample interactions. Full Windows/Mac parity remains
active.

## Source reference and interaction contract

The Mac references are `mac/App/SampleLoopEditor.swift`,
`SampleClipboardEditor.swift` and `SampleSnapEditor.swift`. Current Windows
backend validation is in `windows/Session/AssetOperations.cpp`; shared loop,
splice and snapping behavior lives under `editor/`. Source was inspected for
this checkpoint rather than assuming the dated parity reports were current.

The Windows layout uses five retained pages: Drawing, Process, Loops, Paste
and Snap. The waveform, selection and Copy/Cut/Delete/To new actions remain
accessible around the pages. Native fields retain their HWNDs and raw text
through page changes. The minimum client is 900×720 DIPs, with 900×760 and
1060×850-DIP clients also used for rendering review.

### Independent loop rows

`sample.loops.set` accepts either or both complete `normal` and `sustain`
objects, each with `start`, exclusive `end`, `enabled`, and optional `pingpong`
and `reverse` booleans. An enabled loop must be nonempty and inside the sample.
Ping-pong and reverse are mutually exclusive and require an enabled loop.
Omitted loop objects retain their saved metadata. Supplying both rows prepares
and commits them atomically in one Undo transaction; it does not rewrite PCM.

The UI stages the two rows independently, including Use selection and boundary
snapping. Preview and Apply submit the rows jointly. Raw text and the originating
document/sample/revision remain attached to the draft; an unrelated edit must
not erase it or silently rebase it. Reload is explicit. Preview, rejection and
no-op preserve song revision and history; a real Apply uses the shared backend's
normal playback-stop and Undo behavior.

### Clipboard preview and conversion

`sample.paste` takes `clipboardId`, `at`, mode `insert`, `overwrite`, `mix` or
`replace`, and an exclusive `end` only for Replace. Rate mode is `resample` or
`keep-frames`. Source gain is available in every mode; destination gain is
valid only in Mix. Both gains are finite values from −96 to +24 dB. Insert and
Replace require both channels because they alter time; Overwrite and Mix can
target one channel. Keeping frames across different rates changes the pasted
sound's playback duration and pitch. Resampling preserves its duration.

A dry-run report includes inserted/removed/result frames, changed and clipped
sample values, peak, history size and resulting geometry. Apply of an unchanged
preview must reuse its exact captured revision and clipboard ID. If another
copy replaces the private clipboard, the old preview must reject rather than
silently paste the new content. Paste options, target and selection changes
invalidate the preview. The clipboard remains private to the application
session; no system clipboard transfer is added.

### Snapping

`sample.snap.get` is read-only and accepts 1–64 boundary positions. Zero-crossing
mode uses channel choice and radius 0–65536. Grid mode uses step
1–268435456 and origin within the sample; zero-only fields are rejected in grid
mode and grid-only fields are rejected in zero mode. Direction is nearest,
before or after. A tie selects the earlier boundary, unmatched positions remain
unchanged, and off-grid sample ends are not implicitly added as grid points.

Selection snapping changes only the local selection. Loop snapping changes only
the pending loop row until joint Apply. Automatic snapping follows selection
changes; target, revision, selection version and raw-option checks prevent a
late result from replacing newer interaction. Drawing points are not snapped.
The current Mac implementation exposes a normal-loop snap action; Windows
exposes both retained rows while using the same boundary query semantics.

## Qualification

The fully tested candidate-3 baseline is production SHA256
`b07071219bc72dbfad3abacc56d157c8446386b129f1b3a87f592d3db614799e`, built in the
separate `bin/windows-ui-parity/` tree and retained as
`bin/ScreamSeq-sample-candidate3.exe`. Current verified results are:

- **32/32 affected application tests passed**, no failures or skips, in
  188.699 seconds: `bin/windows-sample-workflows-candidate3-ui.log`.
- **36/36 native CTests passed** in 16.46 seconds:
  `bin/windows-sample-workflows-final-ctest.log`.
- Final candidate 4 has **19/19 rendering views reviewed**, with its own
  source/executable identity and all visible native control geometry recorded
  as described below.
- The complete **330/330 application suite passed**, no failures or skips, in
  **835.843 seconds**: `bin/windows-sample-workflows-final-app-tests.log`, run
  through `bin/run-sample-workflows-final.ps1` against the exact candidate-3
  baseline above. Its strict outer isolation check passed.
- Final candidate 4's **33/33 individual affected UI tests passed**, no
  individual failures or skips,
  in **194.613 seconds**:
  `bin/windows-sample-workflows-candidate4-ui-final.log`. **The outer run failed
  strict foreground isolation with exit 1**, recorded in
  `bin/windows-sample-workflows-candidate4-isolation-final.log`; this is not an
  overall qualification pass. Its changes are
  native-focus restoration and page-specific Ctrl+Enter menu hints; the
  full-suite and native CTest claims above remain tied to candidate 3.
  Its production SHA256 is
  `a53273eed4eff1acfdeeb7fddac74c2e789bcbd388f3a180e3f4cea42dc0b918`.

### Unresolved foreground-isolation failure

The final candidate-4 outer runner observed foreground HWND `68421086` / PID
`7792` before the run and HWND `329004` / PID `11404` afterward; its owned worker
PID was `9148`. The earlier candidate-4 run also failed the outer check, changing
from HWND `591264` / PID `21216` to no foreground HWND; its owned worker PID was
`26024`. That traceback is retained in
`bin/windows-sample-workflows-candidate4-outer-isolation-failure.log`.

The cause is **unattributed**. A separate read-only inspection found the caller
and input desktop both named Default and foreground HWND `329004` / PID `11404`.
No reactivation was attempted, and no additional test run was made to chase a
matching foreground value. Passing individual cases does not erase the outer
failure. Candidate-4 foreground-isolation qualification remains unresolved.
The 19 rendering captures passed their own foreground, desktop and clipboard
checks; those separate checks do not qualify the failed application test run.

### Functional coverage and retained failures

The affected tests cover joint loop Apply/Undo/Redo/save-reopen; invalid and
stale raw drafts; paste preview identity, conversion, clipping and exact Apply;
selection/loop snapping and origin; automatic selection with pending drawing;
page retention and F6 navigation; and all visible controls at the minimum client
size. Existing detailed-sample, settings and context-menu tests pass after their
intentional staged-loop interaction updates. The native draft test exercises
the retained target, strict raw bounds, atomic replacement, rejection and
successful Apply baseline without an HWND or musical mutation.

The first candidate passed **31/32** affected tests in 183.158 seconds. Applying
sample settings preserved raw selection text but left it unusable at the new
revision, so the subsequent range/process action failed to change PCM. The fix
advances that editor's own retained field revision only after an unchanged-size
settings or loop metadata edit; unrelated external revisions still reject old
fields. The regression now also checks the resulting 8–24 selection explicitly.
The original failed result remains in
`bin/windows-sample-workflows-candidate1-ui.log`; it is not counted as a pass.

A subsequent inspection-only probe against an exact candidate-3 executable
clone found that native loop/paste Preview buttons and Ctrl+Enter Apply cleared
native focus during pending work. The operations completed correctly; field
text and the exact `[0,1]` Edit selection survived, but the focused HWND became
null.
`bin/ui-capture/sample-focus-candidate3-probe.json` records all four paths.
The probe used actual `BM_CLICK` and queued keyboard messages on an owned
private desktop without audio or source edits. The final candidate restores the
still-valid native control focused when pending work began: the Preview button
after a click, or the Edit after Ctrl+Enter. The earlier 32 tests did not exercise
this focus-after-operation path.

The first candidate-4 run passed 32 of 33 affected tests in 191.738 seconds.
Its new focus test incorrectly expected the previously focused Edit after
`BM_CLICK` moved focus to the Preview button. The actual result retained that
button correctly. The corrected regression checks the button after a click,
the Edit after Ctrl+Enter, and retained raw text and Edit selection in both
cases. The failed expectation is preserved in
`bin/windows-sample-workflows-candidate4-ui.log`; the corrected 33-test rerun
passes individually on the unchanged candidate-4 executable, while its outer
isolation failure remains unresolved as described above. The final regression
covers both loop and paste actions, including exact Edit selection retention.

The ignored rendering harness captures all five pages at three client sizes,
plus joint-loop preview, rejected stale Apply, clipping paste preview and
grid-origin automatic selection. It uses real sample data,
shared worker operations, the actual Direct2D renderer and native HWND controls
on a never-switched private desktop. These are renderer readbacks with
native-control compositing; the images cannot qualify foreground
presentation, physical input timing, other DPI settings or sustained frame
pacing.

### Current rendering review

Final candidate 4 produced **19 reviewed renderer/native-control compositions**
under `bin/ui-capture/evidence-sample-candidate4-final`, tied to production SHA256
`a53273eed4eff1acfdeeb7fddac74c2e789bcbd388f3a180e3f4cea42dc0b918`.
`capture-evidence.json` records the exact production and scratch executable
identities, a manifest of 183 Windows source files, no source drift and an
unchanged production executable. Every visible direct native control has a
positive, in-bounds rectangle without intersecting another control. The actual
windows report **192 DPI (200%)**, with exact 1060×850, 900×760 and 900×720-DIP
clients.

All 19 PNGs are byte-identical to the reviewed candidate-3 set in
`evidence-sample-candidate3-final`; the minimum-size joint-loop preview was
directly viewed again. Candidate 3 retained fourteen images from the inspected
preliminary set and had five changed loop images, all directly inspected. The
loop help now fits
two complete lines, Crossfade has an explicit Frames label, and dashed normal
and sustain lines distinguish unsaved bounds from saved loop markers. The
closed crossfade-mode selector uses native ellipsis for its long Preserve
duration choice. The complete choice remains in the selector's item text.
Paste clipping feedback and grid-origin automatic selection were also directly
rechecked. No visual defect remains open in the captured states.

Capture assertions confirm that joint-loop preview, paste preview with clipping
and automatic selection snap preserve revision, and that stale loop Apply is
rejected without changing music. The minimum-size paste report displays all of
its 87-clipped-values feedback; grid step 16 with origin 3 snaps the entered
12–53 selection to 19–51. These checks supplement the isolated functional tests.

The ignored harness renders the actual application Direct2D/D3D11 surface and
composites the real retained HWND controls in the same process. Owner-drawn
buttons explicitly invoke the production drawing callback; Edit/ListBox frames
use native `WM_PRINT`. It creates a disposable stereo sample through the worker
API and never opens an audio device, switches the desktop, changes the system
clipboard or replaces the production executable. This is offscreen rendering
evidence, not a foreground screenshot or presentation-performance measurement.

### Earlier review and correction

Candidate 1 produced 19 captures under
`bin/ui-capture/evidence-sample-candidate1-preliminary`, tied to production SHA256
`2d772afa52582820a3d5eec02ed527080a64c40ca412adbe51f29ad607e3469f`. The source
manifest matched, the executable stayed unchanged, and all visible direct native
controls had positive, in-bounds, nonintersecting rectangles. Every image was
inspected. The actual windows reported 192 DPI; their clients were exactly
1060×850, 900×760 and 900×720 DIPs.

The preliminary review found one visual defect: the loop help paragraph clipped
its third line at 900-DIP width. Candidate 3 corrects it, as verified above.
Other reviewed labels and status text fit, including the complete paste
preview with 87 clipped values and automatic grid-origin snapping from 12–53 to
19–51. Capture assertions confirmed revision neutrality for both previews and
selection snapping, and rejection of stale loop Apply. These are preliminary
results, not a substitute for the later functional test results.

## Remaining scope

Full Windows/Mac parity remains active. This sample checkpoint does not complete
independent dock groups, recovery, recording, every editor's contextual menus,
cross-platform project round trips, accessibility or the outstanding
foreground/performance qualification gates. In particular, the final
candidate-4 test run's unattributed strict foreground-isolation failure remains
an open qualification issue.
