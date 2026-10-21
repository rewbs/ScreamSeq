# Windows navigation implementation — 2026-10-21

## Scope and current status

The musician approved the [55-destination map](NAVIGATION-MAP-2026-10-21.md)
and chose the song mixing graph beside Pattern, with an explicit Float action.
Implementation is local on `codex/windows-flicker`, based on main
`b98631b48c1ceaac4ceef9d3589a5f5a02d9cc92`. No push or merge is part of this
checkpoint. This is Windows presentation work; shared musical data, audio,
Mac sources, plugin state, project storage and attribution are unchanged.

**Not yet release-qualified.** The second candidate
`bc7d03f103a81f2079507de06284103741e60d10` built successfully, passed 20 of 22
selected native registrations and 11 of 13 actual-app checks. The original
restore failure is resolved. Follow-up corrections below are source-only and
need the next permitted build. Neither candidate may be packaged as a passing
release.

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

## Second-candidate results and follow-up — 21 October, 21:16 UTC

Receipts are in `bin/navigation-20261021/correction/` (ignored local evidence).
The single ARM64 Release build started at **21:02:45.683 UTC** and completed
at **21:04:37.584 UTC**. Configure/build both exited zero. Executable SHA256:
`ddf3850297bda824d60a6ecba08d2833e5952d5d44bde5358146a05801422736`.
`checks.json` confirms frozen source and unchanged executable through all checks.
The next conservative build gate is **2026-10-21 22:04:38 UTC**. Never rerun
the existing correction build/check scripts: their receipts are final.

Observed failures and source changes:

| Check | Observation / diagnosis | Follow-up |
| --- | --- | --- |
| `workspace-command-result-tests` | Graph double-click did not open Mixer Details. The fixture added nodes, hid the canvas through Inspector, then clicked a node rectangle without checking that it was inside the visible viewport. | Establish a 1440 × 900 DIP client, return to Graph, use the real Fit action and assert the node center is in the viewport before the same double-click, target, history and save/reopen assertions. This diagnosis still needs the rebuilt native test. |
| `workspace-menu-bar-tests` | Pattern menu assertion failed after cursor motion. The setup forced focus to Main while logical focus still named a compact graph; layout repairs this invalid focus, invalidating the menu's captured HWND. | Capture the menu from the actual retained graph HWND. Keep the unchanged cursor/song assertions and explicitly assert Pattern receives focus. Do not relax the production focus/target guard. Needs rebuilt test. |
| `test_native_controls_use_retained_workspace_path` | Requested the obsolete button label `Notes`. | Use the implemented `Note context` label. A separate isolated run against the same executable passed (`renamed-label-check.log`); all pin/follow/return and palette assertions remain. |
| `test_template_curve_stays_visible_during_drag` | Bank capture refused with “Song changed; bank draft retained” before the drag. Source shows the curve's queued automatic save can advance the revision after the Bank captures it. | `GraphCurveOwnerChildren.inc::openBank` flushes the queued completed edit before creating the child. Busy, failed or invalid edits retain their data and prevent a new child. Existing retained Bank owners are raised without flushing/replacing their drafts. New deterministic native checks cover busy/invalid input, exactly one save, expired timer, capture-name text/focus and current revision. The app capture fixture asserts saved points and revision before continuing its original drag assertions. Needs rebuilt checks. |

The other mixer/envelope checks passed, including zero neighboring-strip layout
messages across 24 previews. This is bounded automated evidence, not a fresh
foreground temporal-flicker qualification.

Foreground diagnostic QA used the exact second-candidate executable with
`--inspection --automation --seconds 900`, disposable Midnight Circuit, PID
34440. Fresh discovery found the correct process/window. Computer Use activation
failed twice with `GetCursorPos: Access is denied (0x80070005)`. No UI interaction
or visual/keyboard pass was obtained. The user was asked to unlock the desktop;
do not reuse this PID or handle for the next run. No production package, push or
merge was made.

Next validation must use new `followup/` receipts and a separate
`Navigation-Followup-Release` output, frozen to the follow-up commit. Keep the
22 native registrations and 13 actual-app checks, adding the Bank capture/link/
history/native-reopen test because the opening boundary now flushes a completed
edit. Preserve all new failures and do not weaken assertions. After passing,
perform the full foreground checklist in the correction gate above, including
Ctrl+Alt+D field focus, then adapt packaging to that receipt and executable hash.

### Recording integration discovery retained for the next batch

Source rechecked at `bc7d03f10`: `RecordedGestureTake` already owns original-time
events, stable identities, clock epoch, provider incarnation/loss, bounded
storage, sealing/draining and atomic multi-target completion. Do not duplicate
it. `windows/Session/RecordedPluginEditors.inc` already intercepts uncoalesced
vendor input, retains preview retry data and excludes owned vendor state from
manual persistence. `beginRecordedEditors` still has no production arming
caller. `DocumentController` owns only the MIDI take and single-parameter preview.

The next coherent implementation needs these native adapter boundaries together:

1. Add a worker-owned plugin take to `DocumentController.hpp` and a dedicated
   session include, using `HostedProject::recordingTimeline()` and retaining its
   `absoluteFrames` owner. Arm against document revision, stable rack IDs and
   the playback epoch. Flush earlier manual input before claiming providers.
2. Define an actual native intake barrier across `WindowsVST3.cpp`'s
   `pluginMainCall` owner and native parameter-control messages. Preserve each
   event's original timestamp/sequence; a later worker timestamp or ordinary
   empty poll cannot establish Stop. Drain in bounded turns; capture precedes
   preview coalescing. Do not block or allocate in the audio callback.
3. Extend `ParameterPreviewOperations.inc` with transient multi-target delivery
   and release restoration. Reuse `PluginOperations::commitRecordedGestures`
   and the existing prepared live-publication/history path. Queue refusal must
   retain the take; failed baseline restoration must retain vendor exclusion.
4. Integrate save/open/history/Stop admission in `DocumentController.cpp` and
   recoverable retained input in `RecoveryOperations.inc`. MIDI recovery alone
   is insufficient. Recovery needs raw unmapped events, mapped canonical frames,
   provider/loss metadata and explicit review; never replay recovered vendor
   notifications against a new provider incarnation.
5. Expose guarded begin/status/finish/discard operations in the API/schema and
   native recording controls only with the lifecycle above. Use existing
   recorded-point operations for persisted edits. Adapt the Mac session's
   poll-time consumer to the shared original-time capture contract before
   claiming cross-platform timing parity.

Validate with `RecordedEditorOwnershipTests.inc`, recorded-automation/frame-clock
fixtures, live publication refusal/Undo tests and recovery roundtrips. Add a real
vendor and native-slider take with delayed polling, Stop, queue saturation and
provider replacement, followed by canonical point/PCM comparisons. Shared-header
changes require both native platform builds at the batch gate; reciprocal
save/reopen and plugin-state preservation remain final integration checks. This
is discovery/proposed work, not an implemented Record control or a new runtime
claim. Navigation qualification remains the immediate release prerequisite.

## Unlocked foreground observations — 21 October, 21:27–21:34 UTC

After the user unlocked Windows, a fresh isolated process (PID 11316) ran the
unchanged `bc7d03f10` executable/hash above, with inspection/audio disabled and
disposable Midnight Circuit. This checks the already-built navigation changes;
it does **not** qualify the unbuilt `30c654692` envelope-bank correction.
Evidence is `correction/unlocked-observations.json`, `unlocked-workspace.json`
and `unlocked-context.json`, plus the Computer Use observations in this thread.

- Default-size Pattern and mixing graph were visible together. Enable/Fit
  showed readable Track 1–8 and Master labels. Maximize/restore preserved their
  position. A native Size interaction reduced Main to compact tabs without
  losing graph nodes; Graph/Inspector controls remained usable.
- Gain accepted `-6.5`; Tab/Shift+Tab returned to it and Backspace left `-6.`.
  Ctrl+Alt+D floated the graph with immediate accessibility focus explicitly
  on edit ID 3807, value `-6.`. Explicit Dock and the floating-window menu reused
  that same draft. API readback confirmed the retained text and placement.
- Ctrl+Alt+P returned to Pattern; Down moved the editing cursor from row 0 to
  row 1, confirmed by the accessible grid and API. All editors clearly separated
  Editor panels from Separate windows, and the respective graph destinations
  docked/floated the owner.

Computer Use reports the floating graph only as related transient UI, not as a
separately targetable window. A subsequent `type_text` reactivated Main and its
pattern-paste handler, so **floating typing continuity is not qualified by that
attempt**. Immediate post-shortcut field focus is directly observed; retain the
native focus/caret regression gate for both directions. Docked child controls
remain root-only in the accessibility tree. This is neither full screen-reader
qualification nor temporal flicker/audio evidence. The failed automated gates
still prohibit packaging this candidate. Recheck the new executable after its
next gated build; preserve this bounded evidence rather than repeating discovery.

## Follow-up candidate — 21 October, 22:05–22:19 UTC

Frozen source `86538cb4226d74c257eefc86c88ceb1ee82cc1b9` built once in the
clean qualification checkout, 22:05:43.746–22:07:41.647 UTC. ARM64 Release
executable SHA-256:
`318bb274d082efc31d83371d580454d5eac44372f73b44f67fb2d39a133d4098`.
Receipts are in `bin/navigation-20261021/followup/`; the next conservative
build gate is **23:07:42 UTC**. Do not repeat this build.

All **22 native registrations and 14 actual-app checks passed**. Native controls
took 6.22 seconds, the other 21 registrations 299.69 seconds, and the actual-app
checks 29.31 seconds. This includes the previously failing graph double-click
destination, Pattern menu focus, retained Note context navigation, all three
envelope drag regressions, and Bank capture/link/master/history/native reopen.
Mixer previews again caused zero neighboring-strip layout messages across 24
previews. Source and executable hashes were unchanged through the checks.

A fresh isolated QA process, PID 35936, launched the exact executable with
`--inspection --automation --seconds 900` and disposable Midnight Circuit.
Computer Use discovered the correct window, but activation and one freshly
discovered retry both failed with `GetCursorPos: Access is denied (0x80070005)`.
The user was asked to unlock the desktop. `foreground-check.json` records
**blocked**, not passed. There is no new visible, keyboard, temporal or audio
claim. The prior unlocked observations above remain bounded evidence for
unchanged navigation source, but do not substitute for inspecting this executable.

Production packaging remains pending the fresh foreground check. The prepared
`followup/package.ps1` requires that receipt and will produce
`~/Downloads/ScreamSeq-Windows-ARM64-navigation-86538cb42/ScreamSeq.exe` only after
it passes. That path is a planned destination, not a delivered binary. Nothing
was pushed or merged. Subsequent recording recovery edits are a separate,
unbuilt source batch; see [the staged recording recovery batch](PLUGIN-GESTURE-RECOVERY-2026-10-21.md).

## Final foreground check and production package

After the user unlocked Windows again, fresh isolated PID 25564 ran the exact
`86538cb42` executable with inspection/automation enabled and audio hardware
disabled. The earlier blocked receipt is retained as
`followup/foreground-blocked.json`; `followup/foreground-check.json` now records
the passing **bounded navigation** qualification and its limitations.

Fresh Computer Use observations confirmed default-size Pattern plus Mixing
graph, readable Enable/Fit labels, maximize/restore, compact Graph/Inspector,
gain text `-6.5`, Tab/Shift+Tab and Backspace leaving `-6.`, and Ctrl+Alt+D with
immediate accessible focus on floating gain edit 3807. Explicit Dock and the
Separate windows menu preserved that draft. Ctrl+Alt+P followed by Down moved
Pattern selection from row 0 to row 1. All editors visibly separates Editor
panels from Separate windows and explicitly labels the graph destinations.
`followup/unlocked-workspace.json` confirms floating placement and retained gain.

The earlier narrow 889px resize observation is reused: the only `windows/App`
change from `bc7d03f10` to this candidate is `GraphCurveOwnerChildren.inc`, so
the relevant navigation/layout inputs are unchanged. Floating typing continuity
remains outside the tool's targetability; native focus/caret gates passed.
Docked graph accessibility remains root-only. These observations do not establish
temporal flicker, audio performance or full screen-reader qualification.

The production ARM64 Release package was created at:
`~/Downloads/ScreamSeq-Windows-ARM64-navigation-86538cb42/ScreamSeq.exe`.
Executable SHA-256 remains
`318bb274d082efc31d83371d580454d5eac44372f73b44f67fb2d39a133d4098`.
Scanner, ARM64 VC runtime DLLs, retained notices, source-hash BuildInfo and
ReleaseProvenance accompany it. All packaged hashes and source/copy executable
hashes were verified. No additional build, push or main merge occurred. The
later recording recovery prerequisite is deliberately absent from this package.
