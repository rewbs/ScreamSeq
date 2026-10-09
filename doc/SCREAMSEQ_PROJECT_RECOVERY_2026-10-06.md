# Best-effort project loading — 2026-10-06

Native project files now attempt validated recovery instead of rejecting the entire song when optional metadata is incompatible. Required song/sample snapshot corruption still fails atomically. API edits remain strict.

## Implemented

- File-only recovery of recognized older snapshot framing, metadata versions, missing optional sections, unsupported fields, and independently valid entries within partially invalid sections.
- Legacy NF/NR row durations convert to beats using the pattern's rows-per-beat value. Extremely small durations that need rounding produce an additional warning.
- Invalid plugin records and recorded automation recover independently, with slot remapping so surviving automation cannot silently target a different plugin.
- Load warnings and source protection are exposed through the API. The Mac shows a persistent warning banner and a nonmodal report; Save offers a recovered-copy filename.
- UI, API, and autosave paths protect incompatible originals from overwrite, including symlink and hardlink aliases. Windows has equivalent project recovery and save protection; skipped opaque fragments are archived inertly where applicable.
- No attempt is made to execute unsupported features or manufacture a valid required snapshot from corrupt bytes. Inseparable invalid metadata groups may be omitted with warnings.

## Actual song recovery

The supplied file was `~/Documents/Untitled.screamseq` (there is no `~/Documents.` directory). It contained 67 NF/NR commands with the older row-duration field.

Saved `~/Documents/Untitled Recovered.screamseq` through the running application's API, then reopened the copy with no warnings. All 78 commands survived. A structural comparison found only the 67 duration conversions in native metadata. The embedded song/sample snapshot, plugin records and state, and recorded automation were unchanged byte for byte.

Original SHA-256, unchanged after recovery:

`11bf64512a1a01a07e5c8450889b7d55ffb078b0696fb79e7165612ef640a206`

## Qualification

- Updated application built successfully at `bin/mac-background/ScreamSeq.app`.
- Full CTest run: 108/109 passed; the sole failure was an obsolete expectation that newer container versions must be rejected. Updated that expectation; the affected test and five recovery/model/portable-Windows checks then passed (6/6).
- AppKit interface tests passed, including load-report and recovered-copy behavior.
- Actual-file open, source overwrite rejection, save-copy, clean reopen, and preservation comparisons passed.
- `git diff --check` passed.
- Native Windows desktop execution was not performed; portable Windows metadata, graph, and mixer tests ran on macOS.

Evidence: `bin/mac-background/qualification/project-recovery/verification.json`, `/tmp/screamseq-recovery-qualified-build.log`, `/tmp/screamseq-recovery-ctest.log`, `/tmp/screamseq-recovery-final-tests.log`.

## Display qualification limitation

The recovered song was opened in the normal updated Mac app. Accessibility confirmed the loaded song, warning report, samples, and routing. The captured pattern region was blank with zero presented frames. Debugger inspection found the view attached and unpaused, but its window's occlusion flags did **not** include `NSWindowOcclusionStateVisible`; snapshot preparation was consequently skipped. Cached window captures and accessibility access did not establish actual on-screen visibility. No rendering guard was weakened and no rendering fix is claimed. Repeat visual qualification when the window is visible on the active desktop.

The original app bundle was not replaced. The updated development app was left open with the recovered copy, with no song edits or audible playback initiated during this check.
