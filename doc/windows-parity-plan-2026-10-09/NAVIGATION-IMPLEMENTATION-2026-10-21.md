# Windows navigation implementation — 2026-10-21

## Scope and current status

The musician approved the [55-destination map](NAVIGATION-MAP-2026-10-21.md)
and chose the song mixing graph beside Pattern, with an explicit Float action.
Implementation is local on `codex/windows-flicker`, based on main
`b98631b48c1ceaac4ceef9d3589a5f5a02d9cc92`. No push or merge is part of this
checkpoint. This is Windows presentation work; shared musical data, audio,
Mac sources, plugin state, project storage and attribution are unchanged.

**Not yet release-qualified.** Candidate `aa0392133b04a387832dc2841efccc2b776fb635`
built successfully, passed 20 of 22 selected native registrations and all 13
actual-app checks. Two native failures and additional visible issues prompted
a correction batch. The correction source and new regressions require the
next permitted build. Do not package the first candidate as a passing release.

## Implemented source

- `windows/App/WorkspaceNavigation.inc`: primary editor strip; separate
  **All editors**, **Windows**, and **Layouts** menus. Panel and window actions
  have explicit placement semantics. Smaller widths retain All editors as the
  overflow route. Target-dependent menu choices retain their captured target.
- **Mixing graph** opens directly beside Pattern. **Float** reuses that owner.
  At widths that can accommodate two surfaces but not a third lower panel,
  the primary graph action collapses that lower page while retaining its state.
  Literal API placement requests do not change another panel's visibility.
- **Pattern** and default **Ctrl+Alt+P** restore keyboard focus without moving
  the editing cursor, changing selection or stopping transport. Existing layout
  commands and customized shortcuts remain separate.
- `WorkspaceDocking.inc`, `WorkspaceLayouts.inc`, `WorkspaceRegions.hpp`:
  fifth retained owner `mixingGraph`, staged all-before-any restore, compact
  tabs, explicit docking/float and same-owner field focus preservation. V5
  editor preferences migrate V2–V4 with the new owner initially hidden. V4/V5
  reject the obsolete Main `notes` alias. Project files are not migrated.
- `SongRoutingWindow.hpp`: compact Graph/Inspector pages retain one set of
  native controls, raw input and draft identity. Resizing and docking preserve
  the world-space canvas center and zoom. First enabling routing fits its nodes.
  Small fitted nodes retain readable names when their rectangles allow it.
  Node actions distinguish the Mixer/recipe panel from the parameter window.
- Explicit floating commands for Instruments, Pattern automation, Precise notes
  and Graph-source curve; the old Precise notes tab always docks its owner.
  Read-only **Note context**, **Precise notes**, **Graph recipes**, and **Graph
  cmds…** now have distinct captions. The Mixer shortcut explicitly docks the
  song graph regardless of its previous floating placement.
- `windows/Api/SessionAdapter.hpp`, `windows/Api/workspace.schema.json`,
  `windows/Api/README.md`: advertised `mixingGraph` workspace subset and V5 layout
  documentation. Musical edits still use the existing guarded shared commands.

## First candidate evidence

Receipts: `bin/navigation-20261021/{build.json,checks.json,native.log,native.xml,
native-control.log,native-control.xml,app.log,BuildInfo.json}` in the primary
checkout. The qualification checkout is
`C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq`.

- ARM64 Release build started **19:57:50.892 UTC**, completed **20:01:17.285 UTC**.
- Executable: `bin/windows-parity-p1/Navigation-Release/ScreamSeq.exe` in the
  qualification checkout; SHA256
  `7ffd6a1e49bf162dd5e982fc7b8e10e4a07be096875edd320d6de6dbc844411c`.
- Native controls passed. The other native selection passed 19/21 registrations.
  `workspace-restore-advanced-tests` exposed a real V5 validation omission:
  `WorkspaceLayouts.inc` rejected the legacy Notes alias only for version 4.
  The correction covers both V4 and V5 with unchanged strict assertions.
- `workspace-command-result-tests` reached its existing graph double-click
  assertion while the new compact Inspector hid the canvas. The fixture now
  explicitly returns to Graph and asserts a nonempty viewport before sending
  the native double-click. Its bus target, history and save/reopen assertions
  remain in place.
- All 13 actual-app cases passed: direct graph ownership/navigation, advertised
  API subset, retained controls/inspectors/layouts, idle wake, guarded transport,
  four mixer interaction/paint checks and all three envelope drag regressions.
  Source and executable hashes were unchanged throughout these checks.

## Foreground observations and limits

Computer Use inspected only QA PID **30572**, the exact candidate executable,
with `--inspection --automation --seconds 900`. The built-in Midnight Circuit
song was disposable; no musician process or system audio route was changed.
QA ended normally with exit 0; no ScreamSeq process remained afterward.

Observed directly:

- Direct graph launcher, separate directory categories, compact inspector,
  side-by-side presentation when maximized, and Float/Dock on the same graph.
- Ctrl+Alt+P returned to Pattern; Down moved the cursor from row 0 to row 1.
  Readback is retained in `focus-after-return.json`.
- Routing gain accepted `-6.5` without losing focus; Tab advanced to the next
  enabled control, Shift+Tab returned, and Backspace left `-6.` in the field.
- Resize could leave graph nodes offscreen; fitted node labels disappeared
  below the old zoom threshold. Compact Inspector and sidebar captions clipped,
  Remove node stretched unnecessarily, and Float appeared twice. Corrections
  are in source and **not yet observed in a rebuilt executable**.

The native child controls are still absent from the tool's accessibility tree;
its focused-element report exposes only the root. The keyboard observations
above use visible text/caret and application readback, not a claim of Narrator
qualification. This run was silent inspection, not audio or temporal flicker
qualification. `foreground-report.json` explicitly reports sustained
presentation unqualified. Prior mixer/envelope temporal evidence remains bounded
to the actual inputs and executable that produced it.

## Correction gate

**No build before 2026-10-21 21:01:18 UTC**, conservatively one hour after the
first build completed. Preserve at most one build per hour thereafter. Inspect
receipts before executing anything; never repeat an already recorded build.

1. Freeze the correction commit and fast-forward the clean qualification
   checkout. Use a separate `Navigation-Correction-Release` output directory.
   `bin/navigation-20261021/correction/build.ps1` checks the gate and commit.
2. One batched build, then the bounded native and actual-app selections in
   `correction/check.ps1`. The affected restore, context-panel/navigation,
   menu, command-result, keyboard and retained-owner checks are mandatory.
   New native assertions cover center/zoom through resize, float, dock and
   hidden Inspector; default two-surface layout; explicit panel destinations;
   V4/V5 rejection; field focus/raw text/caret; and navigation while the cursor
   changes under an open menu. Do not weaken failures to fit the build budget.
3. Isolated Computer Use recheck: default-size Pattern plus graph, readable
   nodes after Enable/Fit, resize/maximize/restore, Graph/Inspector, Float/Dock,
   directory placement, Ctrl+Alt+P and actual typing/Tab/Shift+Tab/Backspace.
   Check Ctrl+Alt+D from an edited field retains its text and focus. Preserve
   all musician windows. A screenshot alone does not qualify temporal flicker.
4. Only after passing, package the exact executable/scanner/runtime/notices
   into a new Downloads directory with `BuildInfo.json`, executable hash and
   `ReleaseProvenance.json`; verify copied hashes. Preserve the prior released
   white-flash build. Report the usable executable path and remaining limits.

Mac builds are unnecessary for this Windows-only presentation batch. Shared-main
integration, both Mac architectures, native Windows x64 and reciprocal project
fixtures remain part of the broader parity gate when integrating shared changes.

## Next parity batch

The next functional priority remains the native plugin-gesture recording flow,
tracked in [manual automation follow-up](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md).
Current production source has `PluginOperations::beginRecordedEditors` and
ownership/timestamp/loss foundations, but no production arming caller. Completing
session intake, stop/conflict barriers, recovery, preview/admission and UI/API
is required before presenting Record as usable. Keep that audio/state work in
its own coherent batch after navigation qualification, with the realtime-audio
and API skills, existing take fixtures, and both-platform validation.

Further navigation parity work should follow the approved map: target-specific
formula/bank return paths, accessibility of native children, and careful promotion
of high-use windows such as Arrangement. This checkpoint does not claim that all
55 destinations, all 117 parity rows, vendor plugins, screen-reader behavior or
all display/DPI combinations are qualified.
