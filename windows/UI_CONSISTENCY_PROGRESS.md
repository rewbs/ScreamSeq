# Windows native visual consistency

Qualified presentation checkpoint, 2026-10-07. This continues the
[sections and annotations checkpoint](ANNOTATION_PROGRESS.md). It changes
Windows drawing and its tests; shared musical editing, project storage and API
semantics remain at that baseline.

## Implemented behavior

Arrangement, Recovery and MIDI recording retain their native report lists with
consistent dark selection, separate active and inactive colors, visible keyboard
focus and clipped Unicode column text. Drawing uses actual native header
positions, including resized, reordered and horizontally scrolled columns.
Windows continues to provide selection, type-ahead, scrolling and accessibility.
Theme and system-color notifications refresh colors without replacing retained
controls. Unused header space uses the same dark surface. High-contrast mode
delegates to Windows system drawing.

The main pattern header measures its available space and chooses a full,
compact or short label. It reuses the bounded DirectWrite layout cache and keeps
the 50-DIP grid header and 18-DIP rows. Arrangement's Section column and retained
Section/Pattern drafts remain connected.

## Qualification history

- Builds 1 and 2 succeed. The production executable SHA256 is
  `5C42640F27DEF773459B5AFC38FA9420EDD4954310ABD7E40FBF761D92DEA729`.
  Logs: `bin/windows-ui-consistency-build-1.log` and `-build-2.log`.
- Native run 1 passes **41/42** in **20.81 seconds**. Its new report-paint test
  sees a process GDI count increase from 43 to 49 during its initial 200 focus
  and paint cycles. The assertion fails; that outcome is retained in
  `bin/windows-ui-consistency-native-1.log` and `-native-1-details.log`.
  The candidate's changed source and executable bytes are preserved in
  `bin/windows-ui-consistency-candidate1-source`.
- Diagnostics 1–4 preserve that initial failure. Subsequent batches remain
  flat, including with the production list's classic theme and ASCII-only
  labels. These observations do not establish the cause of the initial six
  resources. The test now owns its parent/font through exception cleanup,
  matches production theme setup, records 200 warm-up cycles, then requires
  exactly zero growth in each of four 200-cycle batches. It also recreates the
  report twice and requires the post-destruction process count to remain flat.
  Diagnostic 5 passes all three list lifecycles: measured paints remain 49→49
  and both recreations remain 44→44. This qualifies steady painting and repeated
  destruction, not zero allocation on first use. Original logs, source snapshots
  and hashes remain under `bin/windows-ui-consistency-native-control-diagnostic-*`.
- Native run 2 passes **42/42** in **28.18 seconds**, exit **0**. The suite
  exercises actual native selection/focus pixels, keyboard/type-ahead, column
  clipping and scrolling, retained state through theme notifications, cached
  header measurements and the existing portable regressions. Evidence:
  `bin/windows-ui-consistency-native-2.log`, `-native-2-details.log` and
  `-native-2-executables.json`.

- Focused application run 1 passes **46/46** in **61.043 seconds**, strict outer
  exit **0**. The eight modules cover annotations, song tools, native controls,
  idle/offline rendering, the renderer probe, recovery, MIDI recording and
  workspace layouts. Logs use `bin/windows-ui-consistency-focused-1-*`.
- Scratch capture build 1 fails because its UTF-8 helper expects an owned
  `std::wstring`, not the measured label's `std::wstring_view`. Build 2 supplies
  that explicit conversion and succeeds. Production code is unaffected. The
  failed helper sources and compiler log are preserved in
  `bin/windows-ui-consistency-capture-build-1-source`.
- Capture 1 completes all 14 views, but its validator samples antialiased text
  at the right-edge midpoint of two horizontally scrolled selected rows. The
  original failed report/validator are retained. Capture 2 samples the blank
  top inset inside the focus rectangle and passes exact-color/geometry checks.
  Visual review then finds bright unused header space after the last column.
  It remains an observed defect in both earlier capture directories, not a
  visually qualified final candidate.
- The follow-up drawing change fills only uncovered native header area during
  post-paint, excluding every actual column rectangle. The native fixture checks
  its real pixel color and GDI state restoration.
- Build 3 succeeds without compiler warnings or errors. Final executable SHA256:
  `C018127596FE6A4B07C806F80B832E9D296137A275EDCF71AD42207D08C23B1E`.
  Native run 3 passes **42/42** in **29.94 seconds**, exit **0**. The new
  uncovered-header pixel check passes. This run's cold GDI count is 43→50,
  all 2,400 measured paints remain 50→50, and both recreations remain 45→45.
  Focused application run 2 passes **46/46** in **60.950 seconds**, strict
  outer exit **0**, with no failures or skips.
- Scratch build 3 and capture 3 use that final production source/executable.
  All **14 views** pass geometry checks and visual review at **192 DPI**:
  default/minimum workspace headers; active/inactive Arrangement selections at
  default, minimum and horizontal-scroll sizes; retained Section/Pattern pages;
  populated Recovery and MIDI take lists. The final views remove the bright
  unused header area. Long native fields retain their normal truncation/scroll
  behavior. All nine actual-state assertions pass, with foreground and clipboard
  unchanged on a never-switched private desktop. Evidence:
  `bin/ui-capture/evidence-ui-consistency-candidate3`.

The annotation checkpoint's full 373-case run is prior-baseline evidence, not a
full rerun of this presentation candidate. Captures are under
`bin/ui-capture/evidence-ui-consistency-candidate*`; capture, build and validator
logs retain their individual outcomes under `bin/windows-ui-consistency-*`.

Checkpoint destination: `bin/windows-checkpoints/ui-consistency-20261007/`.
Its manifest confirms packaging and records final source and executable
identities, preserved earlier outcomes and the immutable annotation baseline.

## Limits and next work

No foreground presentation, physical input timing, other display scales or
actual system high-contrast transition is qualified by these tests. Global
display/audio settings and the musician's separate running app remain intact.
System scrollbars are unchanged. Full Windows/Mac parity remains open; the
[arrangement matrix](MATRIX_PLAN.md) and [independent dock groups](INDEPENDENT_DOCKING_PLAN.md)
are separate work.
