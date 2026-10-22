# Command palette availability and native focus

## Observed gap

Reference row 02 and acceptance A16 require discoverable commands, keyboard
operation and an explanation when an action is unavailable. Current Windows
`CommandPalette.hpp` enabled Run for every matching row. Its native menu already
had context, worker, history, plugin-target and mouse-gesture availability rules
in `WorkspaceMenuBar.inc`; the palette did not use them.

The new actual-app regression
`WorkspaceShortcutTests.test_palette_availability_reason_rechecks_history_without_losing_query`
fails on the unchanged ARM64 executable from
`96b6971e56f0c3f297a713ac7254afad87c46099`, SHA256
`945bb24bcf3b6596eac146284a77d3053f64b7951b5743d2aef7aa045e2bd554`.
It searches for selected-plugin parameter controls in a fresh song with no
plugin target and observes Run enabled. The original log is
`bin/mac-gesture-20261022/palette-availability-baseline/baseline.log`.
This was one bounded private-desktop run, with no rebuild or musician process.

## Implementation — unbuilt

- `CommandPalette.hpp` accepts a read-only availability callback. Search still
  includes unavailable commands and shortcut assignment remains available.
  The selected reason is shown in the native status label; native list strings
  also include the reason for accessibility. Owner drawing uses an unavailable
  label and restrained text contrast, including system high-contrast colors.
- The palette captures the opening HWND. A visible 250 ms refresh reads only
  native/controller state, never the document worker. Changed rows update in
  place, preserving query, caret, selection, scroll and focused controls. Closing
  stops that timer. Execute rechecks availability before dismissing the palette.
- `WorkspaceShortcutDispatch.inc` supplies the existing menu policy against the
  opening editor, not the palette's search field. A vanished/hidden opening
  editor requires reopening Commands. Palette button capture is distinguished
  from a musical drag, while real drag and native input-lease guards remain.
- `WorkspaceMenuBar.inc` shares text-command dispatch between menus and palette.
  Undo/Redo, Select All and clipboard commands from a native text field act on
  that original field after focus restoration. Song/plugin history aliases use
  the shared history availability. `WorkspaceView.inc` connects the dispatch.

No musical model, audio/device code, persistence, stable identity or public
mutation contract changes. This reuses the Windows menu policy; it does not
claim that every possible backend refusal has a precomputed reason. Existing
command execution still validates the operation. Mac UI/DSP inputs are unchanged.
No cross-platform UI framework is introduced.

## Required bounded qualification

Build ScreamSeq, workspace-restore-tests and command-palette-tests together at
the next coordinated checkpoint, **not before 2026-10-22 15:07:58 UTC**. This
joins the unbuilt scratch-status and native test-group split; it is not an extra
build. Preserve existing 96b receipts and the failed baseline.

Run `command-palette-tests`, `workspace-menu-bar-tests`, the split
`workspace-command-result-tests` and `workspace-scratch-editor-tests`, plus
`workspace-departure-tests`. The standalone palette assertions cover unavailable
Enter/double-click, accessible reasons, shortcut configuration, recovery from
unavailability, caret/focus retention and an execution-time state change. The
application menu group adds real retained text Undo and palette/button capture,
worker refusal and recovery without changing the song. Existing minimum-size
bounds checks remain mandatory.

Actual-app checks: the new availability regression, real USER32 File/Edit menu
tracking, menu catalogue/stale-history checks, retained-editor global shortcut
focus, and all four scratch cases. Reuse the unaffected 96b parameter/recording
and recovery results with their exact input provenance. Broaden only if these
fail or more shared/controller inputs change.

Fresh Computer Use must inspect the new binary: Ctrl+K from Pattern, unavailable
plugin action and explanation, query typing/Backspace/arrows/Tab, a command that
becomes available after a song edit, return to the original field, and compact
palette layout. Repeat scratch drag/release/status and visible Main Undo/Redo.
Retain prior unaffected resize/parameter observations where inputs match. This
does not replace a Narrator/high-contrast/DPI journey or temporal flicker/audio
measurement. No new production package or main merge is approved yet.

## Prepared combined checkpoint

`bin/mac-gesture-20261022/palette-availability/` now contains parsed, unexecuted
build/check/package scripts and a fresh isolated foreground launcher. Its
`freeze.json` pins source `077e35f290c335226ff13655a5d8706df6fc3da3` and the
15:07:58UTC gate. It builds the three targets together, selects the five native
groups above and all eight actual-app cases, and retains source/hash checks and
strict automated/foreground package guards. The earlier `scratch-status/`
preparation is superseded and must not be executed separately. The qualification
checkout remains on96b until its retained-binary checks are finished; advance it
only to the frozen candidate before the single next build. Backup source is
pushed on `codex/mac-gesture-intake`; PR #6 remains96b until that checkpoint.

At15:01UTC, after the96b retained x64 replay finished, the clean qualification
checkout was fast-forwarded to077e. Build/check receipts are still absent in the
new folder: preparation and source alignment do not mean a build has started.
Later Mac-only intake/restore source on the backup branch is intentionally
outside this freeze and needs its own coherent qualification checkpoint.

## Executed 15:08 checkpoint

Frozen077e built once at15:08:30–15:10:49UTC. ARM64 Release executable SHA256:
`13bf7e429db3d6555635a17ef3247f77320a857369aff0b5c70d29611f9dae08`.
All five native groups passed in109.36s (command-results74.63s,
scratch11.74s); all eight selected actual-app cases passed in29.117s.
Original failed96b aggregate evidence is retained. Source/hash checks passed.

Fresh Computer Use on owned PID38224 observed default-size Commands, unavailable
Edit/Plugins Undo explanations and disabled Run. After a scratch edit, Edit Undo
was available; selecting it and clicking Run executed history and closed the
palette. Scratch middle-point drag changed0.5/100 to0.5625/71.4656290531777,
kept the curve visible at release and reported a successful phrase save. Main
Ctrl+Z/Ctrl+Y refreshed the retained curve correctly. Dock search accepted gain,
Backspace to gai, Tab/Shift+Tab and subsequent n back to gain during playback.
The disposable copy was saved and the QA process exited normally; original F04
hash remained unchanged. No musician process or audio route was changed.

Computer Use still exposes floating windows only as related UI. Typing targeted
at Main reactivates Main; it did not qualify direct palette/floating-field typing.
Later UIA set_value attempts reported cached-element unavailability; coordinate
selection worked. Dock focus was proven by the caret and actual text changes,
but Computer Use's focused_element reported Main: exact accessible-focus reporting
needs follow-up. Native keyboard/caret assertions passed. These limits are
recorded in `palette-availability/foreground-check.json`; no Narrator, compact
resized palette, high-DPI or temporal flicker claim is made.

Silent WASAPI ran350.2746093s at48kHz/480frames:29 deadline overruns,
zero device errors/starvation/processor faults. Realtime audit and sustained
presentation qualification are false. This remains functional UI evidence.

A separate candidate package is available at
`C:/Users/P14/Downloads/ScreamSeq-Windows-ARM64-plugin-077e35f29/ScreamSeq.exe`.
Its copied executable/scanner hashes, ARM64 PE machines, runtime and notices were
verified. BuildInfo and ReleaseProvenance record frozen source; it is not a full
parity release. The newer Mac intake/restoration source is excluded.

PR#6 now points at077e. Windows run39802007518 and Mac run39802008354 are
the paired checkpoint, still running at15:15UTC; do not restart them. The latest
compile start was15:09:04UTC, so no next coordinated build before16:09:04UTC.
Main has not been merged. CI and current native audio evidence still require
review before integration; retained96b reciprocal exchange is recorded separately.

### 15:50UTC CI review and exact-binary audio replay

Windows39802007518/job119904965173 built successfully and passed158 portable/
worker registrations,50 native editor/preference/recovery registrations, scratch,
vendor-ownership and conformance gates. The93-case app group failed15 times across
12 distinct playback/startup cases (three additional teardown records), with two
skips. Transport cases explicitly reported that a WASAPI endpoint was required;
other failures occurred before a live playback clock or app startup. This is
consistent with the preceding runner-device limitation, not proof by itself
that every assertion would pass with a device. The CI result remains failed.

Downloaded artifact12437828292 was verified against GitHub ZIP SHA256
`ba52de3bee720e98a7ea5f5143a2e0d9b027cdd97348b369420a737d92d6a485`.
Its retained x64 executable SHA256 is
`70a9afa713eaeaf7df5815a41df29c69b99fd9a8dfd43b33b06f2cd282259947`.
Build receipt source5159720f4f2355c0a8f16ed6d433ff625d4d9f06 is the synthetic
PR merge; its tree was checked equal to077e before execution. Archive, nested
bundle, executable and build-receipt hashes and the x64 PE machine were verified.

All12 affected cases then passed unchanged in34.950s using that exact executable
on this Windows ARM64 host under x64 emulation, silent private desktops and the
available endpoint. No rebuild, broader rerun, assertion/timeout relaxation or
system route change. This replay kept the preceding local12-case order; it is
not a rerun of all93 tests in their CI order or native x64 hardware qualification.
Receipts and original CI logs are under
`bin/mac-gesture-20261022/palette-availability/ci/windows-x64/`, particularly
`verified-identity.json`, `device-order-diagnostic.json` and its log. Executable
hash remained unchanged. All owned replay processes were closed by the harness.

Mac ARM job119904967607 passed all gates. Intel119904968198 built successfully
and remained in its test phase at the last check. No CI job was restarted.
Main integration remains pending; a merge-triggered CI run also counts toward
the coordinated16:09:04UTC gate. Newer session code is outside this candidate.

### 16:02UTC exact search-focus observation

A fresh owned077e process29036 used another disposable F04 copy. Computer Use
clicked dock search, typed `gain`, moved forward with Tab and returned with
Shift+Tab. The existing read-only `qualify_foreground_focus.ps1` observer passed
after the click and after Shift+Tab: native GUI-thread focus and global UIA
GetFocusedElement both identified the exact search HWND, with ValuePattern,
TextPattern and selection available. The observer supplied no input or activation.
Receipts: `palette-availability/focus-observer/search-click.json` and
`search-shift-tab.json` (including observer/binary hashes and process creation).

Computer Use's `focused_element` still said Main in the same sequence. The
earlier discrepancy is therefore a tool reporting observation for this field,
not a demonstrated application accessible-focus defect. This does not establish
Narrator behavior or floating-window typing. Alt+F4 closed only the QA process,
normal exit0, original F04 unchanged. Its103.526s silent48kHz/480-frame diagnostic
reported8 deadline overruns and no device/starvation/processor errors; realtime
and temporal-presentation qualification remain false.

Both Mac job logs now show all scheduled gates passed, including Intel's native
interface and fixture stages ending15:49UTC. The run reports success; the jobs
listing briefly retained an in-progress Intel status despite all steps complete.
PR#6's description now names077e and this bounded evidence, and is ready for
review. It contains no later Mac take session/restoration source. Do not trigger
its merge build before16:09:04UTC.
