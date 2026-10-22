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
