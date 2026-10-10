# Parity implementation progress

Implementation resumed after the documentation-only review under the active user goal, “Go ahead with the implementation as per the latest plan.” The complete scope is the [reviewed parity plan](README.md); [latest planning review](final-planning-review.md) retains the planning checkpoint. **P0a is merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding.** Earlier receipts below retain their original scope and dates.

## Prioritized mixing-graph usability — 10 October, 09:42 UTC

The user prioritized graph usability from the remaining P5 scope. Current source
confirmed that song routing, reusable recipes and advanced graph workflows already
existed, but the song canvas had no context menu, double-click inspection, wheel
navigation or direct new-plugin browser. Mac `GraphAddMenu.swift` and
`GraphActionCatalog.swift` provide the search-first and contextual action reference.
This batch makes the existing Windows operations easier to reach; it does not
claim complete Mac graph parity.

- Mixer now exposes **Mixing graph…**, and the separate reusable canvas is labelled
  **Graph recipes**. The command catalogue identifies their different purposes.
- The mixing graph has visible **Add effect…**, **Add group** and **Add return**
  buttons. Select a bus or regular effect and use Add effect / Insert to open the
  existing native plugin library with that stable bus captured as destination.
  `plugin.add` still inserts and routes atomically, with one Undo. New group/return
  buses become the selection; these actions reuse `mixer.bus.add`.
- Native right-click / keyboard context menus expose inspection, effect browsing,
  insert-chain and recipe assignment pages, wire disconnection and layout actions.
  Selection/revision/generation are checked again after the native menu loop.
  Double-click or Enter opens the selected bus, plugin or recipe inspector.
- Wheel pans vertically, Shift+wheel horizontally, and Ctrl+wheel zooms around the
  pointer. Home fits the graph. Ctrl+Y joins Ctrl+Shift+Z for local Redo. Visible
  hints explain the gestures. Navigation does not change song data or history.
- Plugin-library search accepts Up/Down and Enter without requiring a focus change
  to the results list. It applies pending local filtering before choosing and
  restores focus after a completed pending operation when focus was not moved.
- The visible graph follows completed external edits (including library insertion)
  while preserving raw route/layout drafts, node selection and navigation. Busy work,
  gestures, menu tracking and unresolved results defer refresh. Existing Review,
  revision guards, stage-role restrictions and native draft registration remain.

Implementation is in `windows/App/SongRoutingWindow.hpp`, `MixerEditor.inc`,
`PluginLibraryWindow.hpp`, `WorkspaceCommands.inc` and `WorkspaceDocking.inc`.
No shared model, DSP, public API, native storage or Mac source changed.

One ARM64 Release build of the application and native test harness at `eef65e5bb`
passed. Two native groups pass (receipts and command results, 97.00 seconds total).
The new `applicationMixingGraphUsability` case checks actual native controls,
filtered keyboard insertion, stable destination, atomic Undo/Redo, automatic graph
refresh without stealing browser focus, draft preservation, group/return actions,
USER32 menu tracking, double-click registration/inspection and save/reopen.
Eleven distinct actual-application cases pass across the initial and focused runs,
covering route fan-out, stage protection, sidechains, insert order, graph assignments,
plugin preferences, anchored zoom/pan and minimum-size control bounds.

Two old fixture sequences failed both on the new app and the previous unchanged
InteractionFix app: they bypassed the current required Review after an unattributed
cycle failure, and used a routing HWND retired by document replacement. The fixtures
now explicitly verify no replay/no mutation during Review and reopen the tool after
song replacement. Their original state/history/persistence assertions remain.
Only those cases and the added navigation/bounds test were run afterward; no second
build was needed. `08a5f60ed` changes only the Python qualification file.

The [bounded receipt](GRAPH-USABILITY-2026-10-10.json) preserves fingerprints and
initial failures. The deliverable is
`bin/windows-parity-p1/GraphUsability-Release/ScreamSeq.exe` in the integration
checkout, with the adjacent scanner. The previous build remains intact. These are
private-desktop interaction/geometry checks, not foreground visual or physical-audio
qualification. No Mac build was run for Windows-only UI changes.

P5 still includes convergence of song/recipe graph surfaces, unified searchable
canvas insertion (including cable insertion), and direct access to the wider graph
workflow. Those are future batches; the broader parity goal remains paused.

## Browser and pattern input fixes — 10 October, 09:24 UTC

The user's browser reports were traced to disabling all controls during every
search/decode, which loses Win32 focus and repeatedly repaints the controls.
Selection preview also defaulted off. The pattern command catalogue omitted
Backspace, and paste arriving during a pumped worker wait was rejected as busy.
New real-application tests reproduced search-focus loss and Backspace doing
nothing on the prior menu-fix build.

`SampleLibraryWindow.hpp` keeps search, filters and sample-list HWNDs enabled
during independent reads; mutations remain blocked while pending. Selection
inspection is coalesced on the browser timer, newer selections cancel old
previews, and stale decode results cannot replace current selection metadata.
Auto-preview now defaults on, with a retained off choice and explicit Space
preview. Unchanged lists are not reset and retained caret identity is restored
when filtering changes the list. Redundant enable/disable transitions were
removed. The tests establish focus/input continuity; sustained visual flicker
and physical speaker output were not independently measured.

`WorkspaceShortcutDispatch.inc` binds Backspace to the existing pattern Clear
command, retaining native text-field ownership and configurable bindings.
`EditingView.inc` holds at most one pending paste while busy, captures its song,
revision, cursor, selection, focus, mode and clipboard text, and checks the target
before and after parsing. A paste queued behind Copy can consume only that
successful copy, never an older clipboard if Copy failed. The main loop drains
it when idle through the original revision-guarded `pattern.paste` operation.
No song-format or shared editing/API semantics changed.

One ARM64 application/native-harness/preview build from `86e29e954` succeeded.
Five targeted native groups pass: sample-browser input during a deliberately
blocked decode, pattern selection/queued paste, retained receipts, asset outcomes,
and preview voice/decoder PCM. Nine actual-application cases pass, including
per-character search focus, real list clicks and arrows, explicit Space preview,
Backspace/Undo, import/history, preview gain and retained family/editor drafts.
The paste fixture's first run used `pattern.get` at the worker boundary where
that API-level read is unavailable; it now reads the published pattern snapshot.
Only the test executable was rebuilt, and that failed group alone was repeated.
All assertions remain; the application binary stayed unchanged.

The [bounded receipt](BROWSER-PATTERN-FIX-2026-10-10.json) retains the initial
failures and exact build/test fingerprints. The new application is
`bin/windows-parity-p1/InteractionFix-Release/ScreamSeq.exe` in the integration
checkout, with its adjacent scanner. The earlier MenuFix build and running
session were not replaced. Save/close that session before switching. Broader
parity work stays paused.

## Native menu hotfix — 10 October, 08:39 UTC

User testing found all native menu commands disabled, including File → New.
Reproduction in USER32's actual menu loop confirmed that its owner mouse capture
was mistaken for an unfinished editor gesture. The earlier menu tests sent
WM_ENTERMENULOOP/WM_INITMENU directly and did not exercise native modal tracking;
the earlier bounded build result therefore did not establish menu usability.

`WorkspaceMenuBar.inc` now excludes capture only when the current GUI thread is
in native menu mode and both capture and menu ownership belong to this window.
Editor drags, non-menu capture, busy state, stale targets and contextual history
continue to gate commands. The native menu harness checks these guards. A real
application regression enters USER32's loop, waits for actual owner capture,
checks File commands, selects New, then selects Edit → Undo and checks document
identity and history. It failed on the previous build and passes on the fix.
The existing menu binding/stale-history and new/demo/save/reopen cases also pass.

One batched ARM64 build produced the app and workspace harness from `c9d729e55`;
only the Python test's capture synchronization was strengthened afterward and
rerun on the identical executable. No second build was needed. The running
application and its binary were left untouched. Launch the corrected executable
from `bin/windows-parity-p1/MenuFix-Release/ScreamSeq.exe` in the integration
checkout, keeping its adjacent scanner. The earlier `Release/ScreamSeq.exe`
remains the old build. Save and close an existing session before switching.
The [hotfix receipt](MENU-CAPTURE-FIX-2026-10-10.json) retains source/executable
fingerprints, the original failure and all focused results. Mac/audio code was
unchanged; broader parity work and qualification remain paused.

## Usable Windows build — 10 October, 07:44 UTC onward

The user requested only the minimal steps to obtain a working build and commit /
push accumulated work. **The full parity goal remains paused.** This checkpoint
supersedes the unbuilt status in the historical entries below; it does not close
P0b–P8 or the cross-platform integration gate.

Windows ARM64 Release built successfully from `1d74ef4055a35d18808fc7fb8d4267717d948231`.
The application and scanner are in the integration checkout's
`bin/windows-parity-p1/Release/`; keep the build directory together when launching
`ScreamSeq.exe`. Source and executable hashes, exact commands, timestamps and
retained log locations are in the [bounded checkpoint receipt](USABLE-BUILD-2026-10-10.json).
The failed configuration attempt was repaired by placing the portable-test
aggregate dependency after its target definition; no compilation occurred in
that failed attempt. The subsequent single application build succeeded.

Eight selected native/worker checks now pass: shared parameter preparation,
transient parameter preview, playback regions, native audio bus, live parameters,
controller scratch setup and atomic live-queue admission, and plugin search /
value controls / parameter navigation. Three real-application cases pass on
private desktops: new/demo identity and save/reopen, rack buttons with stale
parameter drafts and history, and vendor-editor recovery with retained/coalesced
notifications. Embedded app/workspace-test manifests passed Common Controls 6,
PerMonitorV2 and asInvoker checks.

Initial smoke failures were retained and repaired without weakening assertions:
two API test requests passed JSON null instead of an object; a Python test called
a button helper instead of the combo notification helper; plugin UI fixtures
assumed physical desktop dimensions. The exact-client sizing helper also omitted
the installed native menu, producing a 39-pixel height deficit at 192 DPI.
Fixtures now establish a 1440×800-DIP client and include the menu in frame sizing.
The two affected test executables were rebuilt once together, followed by two
workspace-test-only sizing retries. Failed receipts remain recorded. Only failed
checks were repeated; **ScreamSeq.exe stayed byte-for-byte unchanged** throughout
all test repairs. Final test source is `7b09ec575`.

These passes qualify the bounded Windows usability selection only. They do not
establish Mac/x64 viability for the new shared changes, physical recording or
hardware loopback, third-party plugin behavior, full reciprocal fixture saves,
or full parity completion. The shared preview boundary remains a host foundation
without a public UI/API gesture. No remote build was dispatched and no main merge
is included. The temporary integration branch remains the convergence branch;
resume the reviewed plan and outstanding cross-platform gates only on request.

## Historical consolidated checkpoint — compile failure, 10 October, 06:43 UTC

Checkpoint 05 ran once against frozen `85a90f095`, starting at 06:43:41 UTC
and ending at 06:43:54 UTC. Configuration succeeded; compilation stopped at
`TrackerDocument.cpp:1128`, error C2248: direct access to protected
`PlayState::m_nBufferCount`. Source hashes remained unchanged. The
[terminal receipt summary](integration-checkpoint-05-result.json) records exact
commands, timing, log hash and evidence scope. No manifest, native, actual-app or
remote gate ran; old executables cannot qualify this candidate.

The source repair adds a read-only `SamplesRemainingInTick()` accessor inside
the existing `OPENMPT_EDITOR_CORE` block and uses it in the audition path and
all three regression clock observations. The protected field stays protected;
there is no layout, tick advancement, persistence or plugin-state change.
`ReadNote(channel)` is already public under the native editor build, and its
single-channel path bypasses row/tick preparation and other voice updates.
The first-callback PCM, independent-envelope/pitch, clock and realtime assertions
remain intact. This repair is **unbuilt and unqualified**.

The next consolidated batch must include this repair plus `bc981bf99`'s shared
parameter validation on both platforms and P4's native live-parameter admission
repair. The latter rejects full/unavailable queues without stopping or committing
and retains consumed vendor notifications for a later coalesced retry; source
and prepared acceptance details are in [P4](P4-IMPLEMENTATION.md).
Add `parameter-edit-tests` / CTest
`parameter-edit` to the retained selection; preserve the audition/audio-bus and
API/history cases, including `document-controller-live-parameters` and
`test_recovery.RecoveryControllerTests.test_manual_vendor_editor_overlay_and_late_protection_guard`.
The next normal local build may start no earlier than
**2026-10-10 07:43:55.207390 UTC** (00:43:55 Pacific). Continue source work during
the interval; do not retry checkpoint 05 or reinterpret its failed build as a
partial runtime pass. P0b–P8 and final integration remain open.

## Historical checkpoint preparation — 10 October, 06:35 UTC

The [checkpoint-05 preparation](CHECKPOINT-05-PREPARATION.md) freezes and publishes
`85a90f095` for the combined pending P1–P4 source. It records the 54-target local
build, additional focused native/app cases and guarded Windows x64/Mac matrix
follow-up. At that preparation point nothing had run; the execution result above
supersedes that status. Previous partial/failed evidence is preserved.

## Standing build cadence (user instruction)

### Historical stopping checkpoint requested by the user

Source work stops after committing the shared parameter-preview host boundary
and its prepared cross-platform tests; see [P4](P4-IMPLEMENTATION.md). The working
implementation branch includes `bc981bf99` (shared validation), `b8f6932f1`
(audition compile repair), and `5ee3c9757` (atomic live admission and retained
vendor notifications), plus this preview checkpoint. These changes have not been
built or tested. The integration checkout remains at checkpoint 05's failed
`85a90f095`; no new checkpoint was launched, dispatched or scheduled.

Resume by reviewing the current implementation branch and its unqualified
changes, extending the consolidated selection with `parameter-edit-tests` and
`parameter-preview-tests`, and retaining the controller/vendor recovery cases
named above. Freeze one clean candidate for both platforms before building.
The last attempt's earliest next local build time remains 07:43:55.207390 UTC;
resuming later does not require a new arbitrary cooldown. Keep all prior failure
receipts intact. No phase closure, main merge or full-goal completion is claimed.

Current prepared feature details are in [P1](P1-IMPLEMENTATION.md),
[P2](P2-IMPLEMENTATION.md), [P3](P3-IMPLEMENTATION.md) and
[P4](P4-IMPLEMENTATION.md). P4 now includes the API-backed recorder duration
choice; it is source progress, not physical capture or phase qualification.

From this checkpoint onward, batch related code and test changes before building
or executing tests. Aim for at most one consolidated build cycle per hour. Do not
run a build for each owner, control, small edit or audit finding. Schedule required
platform/target builds together at a source-freeze checkpoint, reuse valid evidence,
and repeat only checks invalidated by changes or unresolved failures. Source audit,
implementation and fixture preparation continue between checkpoints. The cadence
does not waive necessary tests or authorize a narrower parity result.

## Consolidated checkpoint 04 — 9 October, 23:22 UTC

**23:42 UTC remote result update:** Windows x64 and Mac ARM64 are terminal; Intel
is still building. Windows passed 136/137 portable/worker and 29/29 native editor
tests; the actual-app group has 12 failure records over nine cases. Both Windows
audio services started but no endpoint exists, and a separate inspection-mode
recovery cleanup timeout remains unexplained. Mac passed 122/122 native and 34/35
interface groups. Both five-fixture roundtrip baselines passed; the known Windows
plugin-parameter no-op preservation difference remains. The
[reviewed CI record](p0b-ci-checkpoint-04.md) and [receipt](p0b-ci-checkpoint-04.json)
retain exact scope, artifacts and follow-up. A Mac parameter-drop fixture now
answers trim-catalog reads explicitly instead of counting them as mutations;
that correction is source-only. No new build/test cycle was started.

Frozen source `d39ec5548fc6224f5d395dbf02d6b0eca35b48ac` built locally on ARM64
in one configure/build cycle, **23:14:30–23:19:50 UTC**. Source hashes stayed
unchanged across that build. The log retains a C4244 int-to-float warning from the
existing `PluginChain.cpp:948` fill operation; the build succeeded.

Targeted native/model checks: **20/21 passed in 35.05 seconds**. The new shared
order-admission test, history allocation, envelope JSON guard, dock geometry,
private native controls and workspace departure/fixture cases passed. The
arrangement worker case failed at exhausted-order insertion: shared preflight
threw `std::invalid_argument` before the Windows candidate validator could map it
to API error `-32602`. The expected error and atomicity assertions are retained.
The adapter now performs the same validation-exception mapping around preflight;
this follow-up is source-only and is **not** covered by the completed build.

Actual application checks: **30/30 passed in 52.069 seconds**, no skips, using
owned private desktops and disposable files. They cover the existing shortcut,
typing, capture/recording, retained-editor, recovery, pattern and graph cases.
They are not physical-device, foreground visual or P1 qualification.

Receipts/logs: `bin/parity-evidence/p0b-closure-receipt-04.json`,
`p0b-closure-build-04.log`, `p0b-closure-native-04.log`, `p0b-closure-app-06.log`.
The 17 explicitly selected build targets include the app, shared order/history,
document/envelope workers, workspace harness and all private-GUI-helper consumers.
The 21-test selection includes its required scratch fixture. Unrelated suites
were not repeated locally; remote full gates retain their necessary scope.

At 23:22 UTC these exact jobs were confirmed live on the same frozen source:

* Windows x64: run **38003410537**, job **114066588740**, building.
* Mac ARM64: run **38003412848**, job **114066597302**, building.
* Mac Intel: that same run, job **114066597619**, building.

Do not restart those jobs because of the known follow-up; retain their independent
Mac interface/no-op and Windows environment results. No new normal local build
before **2026-10-10 00:19:51 UTC**. P0 remains open. P1 native compact/focus and
shared meter identity work is prepared separately in the mixer worktree, with P0
integrated into it; it has not been built or tested.

## Cross-platform correction batch — prepared, 9 October, 23:02 UTC

**23:09 UTC update:** the frozen Windows x64 → Intel Mac → Windows x64
exchange is complete. Both retained runs passed all seven files, with app-build
jobs skipped. Downloaded artifact hashes, report lineage and direct original-to-return
typed comparisons were verified; all seven returned files also match the original
Windows bytes. See [reciprocal checkpoint 03](reciprocal-checkpoint-03.json).
This evidence applies to built source `494d65563`, not the pending corrections.
The corrective batch additionally includes Mac unified-history checks for no-op
Redo preservation and uninterrupted advancing transport. Those checks are not yet
executed. P0b/P0c and the complete P1–P8 implementation scope remain open.

The [checkpoint 03 follow-up](p0b-ci-checkpoint-03.md#follow-up-inspection-and-corrective-batch-2302-utc)
records the terminal x64/Intel results, verified app archives and running
Windows→Intel retained exchange (38001876485 / 114061620308; no build requested).
It also documents the next uncompiled batch: collision-free envelope fixtures,
explicit owned wide-window setup, shared order no-op admission before transport
stop, and runner audio-service startup with diagnostics and unchanged required
assertions. These join the four Mac interface corrections below. No new local
build/test was run; P0 remains open. Separate P1 strip preparation is not included
in this frozen-app evidence and does not close these gates.

## Mac interface correction batch — prepared, 9 October, 22:24 UTC

The four failures from run **37997879633** are being handled together. No new
build or test run has been started. The source changes are **uncompiled and
unqualified**, and do not replace the recorded 31/35 result:

* The draft helper now uses a shared `pumpMainRunLoop` with an attached 5-ms
  timer and short run-loop turns. Plugin retry exhaustion schedules multiple
  consecutive dispatch callbacks; the prior one-shot deadline timer was
  insufficient in the observed run. The overall 0.08/0.12-second deadlines and
  expected edits/baseline/status remain unchanged. Native execution must determine
  whether this resolves the failure; no product retry semantics were changed.
* The queued graph handle fixture uses that same bounded pump. Its combined
  assertion is separated into exact-once, captured revision, chosen source and
  original gain checks, with the submitted payload in failure diagnostics.
  Current production code already captures the mutation before a pending load and
  reads sidechain gain in `selectSongConnection`; it was not declared missing or
  rewritten without evidence identifying a failing clause.
* The Back fixture previously counted **every request** as a write. Source shows
  that `showPort` calls `inspect`, which invokes `GraphTrimControls.context` and
  `graph.trim.get`. The fixture now answers only that explicit read for the
  captured destination in song scope, requires exactly one such read, and fails
  on every mutation or unexpected request. Original connection identity, consumed
  return state and every card position remain separate required assertions.
* `MixerEditor` gives the scrolling inspector 12 points of inset instead of 3,
  leaving room for rounded-button bezels beyond their alignment rectangles.
  The existing full-control-bounds and reachability checks remain intact; failures
  now identify the button title and inspector bounds. This candidate layout
  correction needs Mac execution before the geometry failure can be closed.

The only product edit in this batch is native Mac inspector spacing. No Windows,
shared editing, audio, API, serialization, plugin state or project identity code
changed. The Windows binaries qualified at `494d65563` remain the current Windows
candidate. Preserve the still-running x64/Intel jobs at that frozen commit. Keep
this new batch local until the existing archived-app exchange can use the remote
qualification checkout without encountering changed Mac product inputs.

## Consolidated native checkpoint — 9 October, 22:19 UTC

Frozen product/test commit: `494d6556367ee6ac9c592ae19cc7a858e6bc2e1b`.
One local ARM64 build ran **22:11:49–22:13:34 UTC**, after the hourly boundary,
for the app, workspace harness and three affected native fixture targets. It
passed without warnings; the source hashes stayed unchanged across the build.
Compiler and CMake cache hashes match the previous checkpoint. The next normal
local build is no earlier than **23:13:34 UTC**.

All **13/13 targeted native tests passed in 260.29 seconds**: ten workspace
groups plus parameter-automation dock, instrument short dock and graph workflow.
This confirms the three corrections on ARM64, including the temporary-snapshot
lifetime repair and the compact floating focus/geometry cases. All **30/30 actual
application integration cases passed in 53.933 seconds**, with no skips, covering
shortcuts, recording, MIDI/sample capture UI, recovery cleanup, musical typing,
pattern edits and retained editor hosts. Tests used owned private desktops and
disposable files. No musician process was present before the run. These are not
foreground visual or physical-device qualification claims.

Receipt: `bin/parity-evidence/p0b-closure-receipt-03.json`; logs:
`p0b-closure-build-03.log`, `p0b-closure-native-03.log`, `p0b-closure-app-05.log`.
The current ARM64 app SHA-256 is
`c32dca81899bc82fe00a487842b3b10cd84d27d86f84bd05ec435f4b191e443e`.
Unchanged worker/model evidence retains its earlier scope and source/compiler
checks; changed native targets were rebuilt and tested together.

Mac ARM interface-only run **37997879633**, job **114048444110**, compiled the
new harness successfully and completed all **35 groups: 31 passed, four failed**:

* `draft-plugin`: the deferred edit succeeds, but exhaustion does not restore the
  baseline within the existing wait; actual edits `[0.8]`, slider `0.2`, ordinary
  plugin status. The one-shot run-loop timer did not resolve the complete case.
* `signal-graph`: queued handle release during refresh does not meet the combined
  captured revision/source/existing sidechain gain assertion.
* `graph-ports`: Back does not meet the combined original-connection/view/no-write
  assertion. Diagnose the individual clauses before declaring a navigation bug.
* `core-layout`: a mixer inspector ActionButton is outside its asserted bounds
  at rectangle `(631, 300, 108, 32)`.

No assertion was removed or relaxed, and no group was retried. All failure logs
are retained in `bin/parity-evidence/macos-interface-03.log`. Artifact
**11648031958**, `screamseq-interface-macos-15`, includes the compiled harness,
source manifest and log; its advertised ZIP SHA-256 is
`e4574995745165c6bec50e9b15cf248f4ce6ea35b059a4cec6179fc830ab4973`.
The archive has not yet been downloaded locally; job-log evidence was inspected.
These four failures require one cohesive source/harness investigation before the
next native checkpoint; no Mac app rebuild was used for this result.

Windows x64 run **37997874323** / job **114048426317** and Intel Mac run
**37997877049** / job **114048437720** were confirmed live in their build steps
after this local gate. Both use the same frozen commit. Keep observing those
specific runs; do not restart them merely because another observation times out.
P0b/P0c remain incomplete. The seven-file prior ARM Mac reciprocal checkpoint is
still valid for its exact earlier product inputs; Intel exchange and P1–P8 remain
outstanding.

## Batched harness and CI corrections — 9 October, 22:05 UTC

No product build ran during this preparation. Native changes in `5561ca900`
remain uncompiled. The next normal local build remains no earlier than 22:11:49
UTC. P0b is still open; none of these tooling results establishes product parity.

The Mac interface harness now declares 34 independent checks plus its unchanged
core/layout body. `mac/test-interface.sh` compiles once; the Python driver runs
every group in its own process, retains all failures, and never retries crashes
or assertions. A group has a 120-second bound; discovery has a 15-second bound.
Five portable runner tests passed, covering continuation after failure/timeout,
invalid inventory, subset refusal, argument forwarding and completion status.
A source census against the preceding harness verified every existing check
exactly once and the core/layout body byte-for-byte unchanged. This is source
coverage evidence; the Swift changes still need native compilation and execution.

`EditorDraftInterfaceTests.wait` now attaches a one-shot timer at the original
deadline because an otherwise empty run loop can return immediately. Plugin retry
assertions retain their original deadlines and expectations and report actual
edit/slider/status values on failure. This is a harness-cause hypothesis pending
native execution, not a demonstrated plugin behavior fix.

Intel job **114027623926** in run **37991809860** is terminal/cancelled. Its build,
model/host tests, recovery/picker and library steps passed; cancellation occurred
during interface compilation, before app archival. The downloaded logs artifact
**11646694540** matches SHA-256
`3160e0de59a35853c426bf0bb1d3fcffde4b0c7d6d59a5174a8d2f23cf2834e7`
and contains no app archive. Local evidence is under
`bin/parity-evidence/macos-intel-gate-01`. The revised workflow archives the app
immediately after a successful build, bounds the full job at 60 minutes, and
allows a manual Intel-only build. A separate interface-only mode avoids another
unchanged Mac ARM application build. Both architectures remain mandatory for
normal main/PR qualification.

The Windows workflow now records audio service status and performs only
`audio.devices.get`, `audio.settings.get` and `transport.get` in an owned inspection
process before the required integration checks. It neither changes service/device
configuration nor skips failed tests. The new probe succeeded against the archived
x64 app on this ARM64 host (all three reads; executable unchanged), recorded at
`bin/parity-evidence/x64-audio-environment-01/report.json`. The CI machine's audio
failure cause is still unmeasured; local availability does not establish CI availability.

All three ScreamSeq workflow YAML files parse. Next consolidated checkpoint:
Windows ARM64 changed app/native targets, Windows x64 qualification, Mac ARM
interface-only qualification and the necessary Intel full build. Preserve the
existing exact seven-file ARM Mac reciprocal evidence; use the recovered Intel
archive for its still-outstanding reciprocal journey without another build.

## Completed ARM Mac reciprocal checkpoint — 9 October, 21:49 UTC

All **seven Windows x64 → Mac ARM64 → Windows x64** fixture journeys passed with
the archived apps built at `d703d86640c770e2a5735ea9a27dc57a011bf0e3`. No build ran.
The five originals and actual native F04 rendered/imported outputs each retained
their exact typed project tree through two Mac saves and two Windows return saves.
The returned files were also compared directly with the originating Windows
outputs. [reciprocal-checkpoint-01.json](reciprocal-checkpoint-01.json) pins executable,
receipt, project and report hashes and explicitly limits this checkpoint's scope.

Mac exchange run **37995384823** and Windows return **37995525893** succeeded. Both
normal build jobs were skipped, and only archived-app qualification ran. Source
inputs remained frozen; conformance tooling came from `f22b2d52d`. The downloaded
Mac exchange ZIP hashes to
`20a49430360e613b4b6a7789e01a5f5e63daf59644dc3f984b9121794b5a88c8`; the Windows return
ZIP hashes to `756b29e3e32781cd9efda73c57c488ebe6cdc16cb722aff0f336f8781dc8642e`.

The preceding Mac baseline run **37995159221** passed all five normal launch-based
fixture legs and completed all 21 recovery probes with their safety checks intact.
Its overall result remains failed: the actual no-op `order.edit(move,destination=0)`
created Undo and changed revision, contrary to the unchanged-document assertion.
Saved typed project data was unchanged. The assertion is **not relaxed**. This is
now a measured P3a defect; `TrackerSessionAPI.inc:797` dispatches through
`TrackerSession.mm:1397` to the shared order editor. The original failure and report
are retained. Mac advertises **70 reads and 162 writes**, matching all 232 schema
methods; actual inventory and report hash are now in `doc/api/platform-differences.json`.

Exact Mac codec observations are recorded in `doc/api/codec-observations.json`,
with their source/loader provenance. In particular Mac warns, protects the source,
and drops unknown root/native/track/node extension fields on canonical save, while
Windows preserves them. The corrupt core snapshot rejects without replacing the
current document; invalid ASCII is accepted/recovered with a warning on Mac and
rejected on Windows. These are P6 convergence findings, not permissions to normalize
or discard project data. Missing AU/VST3 opaque plugin identity/state checks passed.

The first retained run **37995029602** stopped before launching because the new
workflow's own root-level log was considered an untracked product input. The log
was moved under ignored `bin/`; the source-difference guard was not weakened.

### Next consolidated native correction batch — prepared, not built

Current local source changes address the three x64 native UI failures together:

* `GraphWorkflowWindowTests.cpp`: keep the snapshot alive before iterating its
  `controls` member. The prior range referenced a member of an already destroyed
  temporary under C++20. Runtime confirmation of the crash fix remains required.
* `InstrumentEnvelopeWindow.hpp`: when a short dock floats into a compact window,
  keep a focused inline point field visible by selecting its Points page. The test
  explicitly preserves a 440×500 floating size so a large local display cannot hide
  this transition, and continues to require the exact raw text, HWND focus and caret.
* `ParameterAutomationWindow.hpp`: reserve four additional DIPs before the ruler
  for classic Windows combo borders, retaining a 100-DIP curve at the minimum
  440×300 dock. Existing overlap and reachability assertions remain unchanged.

These changes are **uncompiled/unqualified**. Batch their build with any remaining
Mac interface and runner-diagnostic corrections; do not build before the standing
22:11:49 UTC local cadence boundary. The Mac plugin deferred-edit failure, native
x64 UI confirmation, runner audio/startup/cleanup issues, Intel Mac and P0c–P8 work
remain open. Reciprocal success alone does not close P0b or the full parity goal.

## Retained binaries and first cross-platform failures — 9 October, 21:42 UTC

No product build was run at this checkpoint. The latest local build remains the
21:10:15–21:11:49 UTC ARM64 build; normal rebuilding is no earlier than 22:11:49.
The first CI runs build PR merge commit `d703d86640c770e2a5735ea9a27dc57a011bf0e3`
from head `8709816852aefad865911b13041371a55f621f9e` and unchanged main `16cab100b`.

* Windows run **37991809865** completed: build and **136/136 portable/worker tests**
  passed; five originals, API baseline and all 21 codec observations passed.
  Native UI tests passed **26/29**. Failures: automation control 4205 overlaps its
  curve/ruler, graph-workflow child access violation, and instrument floating
  focus/short-body assertion. Three scratch tests originally had two errors from
  obsolete document-departure expectations. The 30-test integration group reported
  12 failures (including teardown failures); silent audio app startup, recording
  clock and recovery-browser cleanup require diagnosis. Silent-output mode still
  starts a WASAPI endpoint; it is not a device-independent clock. No absence of a
  runner endpoint has yet been measured, and these failures are not waived.
* Apple Silicon job **114027623669**, run **37991809860**: app build and **121/121
  CTests** passed, as did recovery/picker and library steps. The full interface
  harness failed at the deferred plugin-parameter edit in
  `EditorDraftInterfaceTests.swift:442`. This remains unresolved; no timeout or
  assertion was relaxed. Its conformance runner failed before the first fixture
  because Mac has no `document.open` method. Intel was still building at inspection.
* The downloaded Windows artifact ZIP hashes to
  `e4cd5cddd4bcc4bb1341080bd842ea11690d251cdf6e753b03e2546739012ac0`;
  its archived executable hashes to
  `6ab18fb83491f13603ef7c64f852bc5985b17bb5a7c6b33de0d8b038ececea9a`.
  The Apple Silicon artifact ZIP hashes to
  `da3a1f026370754a955683442e6b2b9d5dd41d724b58a4c1a51123f7fe0d7557`.
  Original logs and artifacts remain under `bin/parity-evidence/*-gate-01/`.

The new retained workflow and `reuse_native.py` verify archived executable, receipt,
source and input/report hashes before any launch. They refuse changed product/build
inputs, existing output paths, unsafe extraction and incomplete preceding legs.
They can execute baseline or seven-file reciprocal legs without configuration or
compilation. Both apps must name the same frozen source commit. Native render/import
outputs require the originating passing fixture test, not merely existing files.
See `editor/Tests/Conformance/README.md` for explicit dispatch inputs and provenance.

Mac no-edit loading now uses its normal positional launch path, one owned process
for each input/opened save. API/codec setup uses the existing private recovery API
with exact staged bytes, real replies and explicit provenance; rejected-load checks
remain in one session. These are distinct checks, not an invented `document.open`
alias. Ordinary Open-dialog and draft-admission coverage remains separately required.
The corrected Mac harness is **not yet runtime qualified**.

Local evidence without rebuilding:

* **21/21** Python conformance/tool-safety checks passed in 8.77 s. A subsequent
  focused runner group passed **3/3**, including the added Mac launch-chain and
  uncertain-save-stop regression. All three workflow files parse as YAML 1.2.
* The retained ARM64 app passed all five originals, API baseline and 21 codec
  observations with the revised loader (`p0c-retained-local-baseline-02`). The known
  plugin no-op defect remains visible and `parityComplete` remains false.
* Earlier actual seven-file Windows-only artifact exchange was successful; its
  chained first/reopened hashes are now verified by the exchange-input reader.
  This does not establish a Mac reciprocal pass.
* The scratch tests now assert refusal without draft loss, explicit Reload before
  replacement, old-owner retirement, and fresh reopened capture. All **3/3** passed
  against the **archived x64 CI binary running on this ARM64 Windows host**. The
  unchanged recovery-browser test also passed on that binary (**4/4, 18.83 s**, log
  `p0b-x64-app-corrections-01.log`). This resolves the obsolete scratch expectations;
  it does not explain the recovery CI failure or replace native x64 runner evidence.

P0b remains incomplete. The native UI failures, Mac interface failure, runner audio
qualification, reciprocal files and subsequent P0c–P8 phases remain required.

## Grouped ARM64 closure gate and draft PR — 9 October, 21:18 UTC

Candidate **8709816852aefad865911b13041371a55f621f9e** built successfully from
21:10:15 to 21:11:49 UTC, after the hourly cadence interval. The single invocation
built ScreamSeq, workspace-restore-tests and portable-tests; no warnings were found
in the retained log. All **13 targeted CTests passed in 256.95 s**: ten workspace
groups plus synthetic recording-device, sample-preview and parameter-provenance.
The three originally failing receipt/asset/command groups completed their remaining
assertions, and sample readback passed with its foreground/clipboard checks.

The source receipt records 1,897 inputs, unchanged compiler identities and an
unchanged CMake cache. The seven previously captured worker/envelope/native-owner
executables are byte-identical; their unchanged implementation/test/dependency
paths permit bounded reuse of successful earlier checks. The receipt keeps the
changed-path list and original failures; no failed result is reused as a pass.
Current registration evidence lists **165 CTests, zero unscheduled and zero repeated**
under the prepared CI groups, plus 18 separate projects. This is scheduling evidence,
not a claim that every standalone project has executed.

Actual-app qualification first failed at fixture setup because the caller omitted
TMPDIR; no product assertion ran. With the required owned temporary directory,
**25/26 passed, zero skipped, in 44.915 s**. The remaining recording-autosave case
received an explicit busy reply from a read immediately after MIDI injection.
It now uses the existing `wait_recording` helper's eight-second, read-only busy
poll and captures the returned take. No mutation is replayed, no deadline is enlarged
and every history/recovery assertion remains. That sole case passed in **1.059 s**
on the same executable. The Python-only correction is local pending the next push;
the active CI candidate still contains the original polling call.

The native F04 test retained `original.screamseq`, `rendered.screamseq`,
`imported.screamseq` and exact PCM/identity observations under
`bin/windows-parity-p0/parity-fixture-output/ScreamSeqRestore-6512-3886339994713/`.
Both edited files then passed exact typed two-save/reopen checks through the actual
Windows pipe in `bin/parity-evidence/p0b-windows-edited-01/`. These are the Windows
edited legs; Mac-and-return checks remain outstanding. Original files, executable,
foreground and clipboard were preserved by the owned runner.

Evidence: `bin/parity-evidence/p0b-closure-receipt-02.json`, build/native `-02` logs,
application `-02` setup failure, `-03` full run and `app-autosave-04` focused correction;
`p0c-ctest-registration-02.json` and `p0c-schedule-after-build-02.json` retain the census.
The conformance tools passed **13 tests in 6.578 s** before this build. The prior helper
result remains 17 passes and one separately configured historical-reference skip.

[Draft PR #4](https://github.com/rewbs/ScreamSeq/pull/4) targets main and contains
the frozen candidate. Main remains 16cab100; the PR is mergeable. Windows x64 run
**37991809865** and Mac Apple Silicon/Intel run **37991809860** are confirmed live;
both are still building at this checkpoint. Do not push each local fixture/docs fix
and restart those jobs. The next normal local build is no earlier than **22:11:49 UTC**.
P0b stays open for the cross-platform/reciprocal gate; P0c Mac baselines and P1–P8
remain outstanding. No foreground/device/release qualification is claimed.

## P0c codec recovery corpus, helper alignment and scheduling audit — 9 October

Twenty-one owned F04 derivatives completed through the actual Windows pipe on the
unchanged **5358a8ebc** binary. Unknown root/native/entity/node fields, reordered and
damaged identities, future versions, missing sections/duration, numeric boundaries,
Unicode, corrupt snapshot/encoding and unavailable AU/VST3 state are retained with
full requests, warnings, protection results and typed save differences in
`bin/parity-evidence/p0c-windows-codec-01/codec`. Original and derivative inputs stayed
unchanged; rejected loads preserved the prior document; every protected source refused
overwrite; canonical copies reopened; missing-provider plugin records stayed exact.
This is recovery evidence, not a blanket lossless or playable-project pass.

[Codec observations](../api/codec-observations.json) pin all 21 Windows classifications
and saved typed hashes. Unknown root fields preserved without warning; unknown native,
track and node fields preserved with source protection. Invalid identity layers and
missing command duration recovered with explicit loss warnings. Mac classifications
remain unset pending current native execution. The strengthened API corpus also
completed in this run; its exact saved no-op defect remains an unresolved P4b issue.

**F25 clarification:** the current shared `NativeNoteEffectSupported` requires
parameter zero for `CMD_NONE`, and `NativeSong::validate` uses it. Thus the injected
effect-zero/parameter-37 event is invalid model data. Windows skips it with a warning
and source protection. The encoder condition difference alone is not evidence of
losing a valid reachable musical edit. Missing non-nudge duration remains a separate
Mac/Windows recovery/defaulting question.

The legacy Python framing helper now accepts container 6 / metadata 17 while keeping
historical versions unchanged and rejecting newer/incorrect primitive types. The
first new all-five-fixture check exposed its obsolete requirement for nonempty timing
data. Current `SampleArchive.cpp` permits zero timing length in RSONGS2; the helper
now follows that framing rule while still rejecting empty module/sample sections,
bad length sums, truncation and trailing data. The corrected suite passed **17 tests**
in 5.242 s; **one separate September external-reference test was skipped** because
its opt-in file was not configured. All five original October projects were included.
Both attempts remain in `p0c-project-helper-01.log` and `-02.log`; no assertion was
removed to conceal a failure. Documentation now separates helper framing/repacking
from native recovery, strict canonical saves and playback.

The read-only scheduling audit found **165 configured CTests**, three outside the
prepared CI label union, zero duplicates and **18 separate CMake projects**. The
three are parameter provenance, synthetic MIDI/clock callback auditing, and sample
preview decoding. Source inspection confirms the registered latter two do not open
hardware (the preview executable's separate `--device` mode is not registered).
Their labels and portable target dependencies are now prepared for the grouped gate.
The old configuration's three missing selections remain visible in
`p0c-schedule-before-reconfigure-01.json`; only the next configure/build can supply
current registration evidence. The inventory also lists separate project targets and
Python declarations without treating matching names as execution proof.

CI now retains test-registration/schedule reports and executes helper/codec probes
from the same built apps. The codec ratchet was checked against retained outputs
without another application run. The final workflow/inventory changes and both Mac
baselines remain unqualified until the grouped gate. No native build ran in this slice;
P0b's next build remains no earlier than 21:10:15 UTC. Reciprocal file exchange and
P1–P8 remain outstanding.

The grouped CI candidate additionally retains executable archives, their hashes and
actual source/build-receipt identities independently of test success. Mac bundles
use tar to preserve modes and symlinks; Windows keeps the scanner and both attribution
directories with the app. This allows reciprocal or tooling-only follow-up checks
without recompiling the same product. Archive retention is not qualification. Both
workflows now follow main and PRs targeting main, removing the obsolete permanent
platform-branch trigger; temporary implementation branches still qualify through PRs.

The same pending build now retains the real F04 render/import recovery outputs and
exact sample PCM/identity observations in a unique build-owned directory, rather
than deleting them with the fixture. The conformance runner accepts explicit edited
files for the C6 Mac-and-return legs and labels that scope separately from all five
originals. Assertions and time limits are unchanged. This retention path, including
the new native fixture source, awaits the scheduled build/test checkpoint.

## P0c actual API baseline and grouped CI preparation — 9 October

The shared pipe/socket corpus now records 27 table-driven input cases plus rejected
request-ID reuse and one real edit/history/persistence sequence. The Windows run on
the same **5358a8ebc** executable matched the baseline: invalid whole-batch rejection,
dry-run/no-op preservation for pattern edits, exact successful replay, changed-content
ID rejection, one Undo/Redo and typed unrelated-state preservation through save/reopen.
All five original no-edit fixture checks also passed. This is actual private-pipe
evidence; Mac expectations still come from current source and require socket execution.

The live Windows catalogue advertises **72 reads and 162 writes**. Against the 232-method
shared schema it omits 13 method names and adds 15 others. These are advertisement
facts, not complete dispatcher or behavioral qualification. The checked-in
[platform difference inventory](../api/platform-differences.json) pins exact method
sets, assigns missing-name/contract questions to their domain phases and rejects new
unreviewed catalogue drift. Mac inventory remains explicitly unqualified rather than
being populated from the historical reference.

One probe corrected a source-only assumption: Windows `plugin.parameters.set` accepts
stable `plugin` identity. It also exposed a no-op defect on original F04: assigning the
already-current Enabled value changes revision, adds plugin Undo and replaces empty
`plugins[0].state` with a 75-byte payload. The runner retains the exact changed project
and typed diff before resetting the owned document for independent cases. This is
`API-PLUGIN-NOOP`, owned by P4b; **preservation failed and API parity remains false**.
The baseline ratchet pins this exact defect, including changed paths and payload hash;
it does not exempt no-op history or plugin state from the required fix.

Retained attempts: `bin/parity-evidence/p0c-windows-api-01` stopped at the first newly
observed no-op failure; `p0c-windows-api-02` completed the isolated corpus. Its API
report SHA-256 is `5f47b16d99a706289d072e9f13eca82063e2c244a451e1d686f6e994a1cd319c`.
Subsequent source hardening pins the payload diff, uses typed comparisons for every
edit/replay assertion and stops on busy/unknown internal errors; these later harness
changes await the grouped gate. Python syntax/data checks passed. No native rebuild ran.

CI source now builds each platform once, then runs independent checks from those
outputs even if another test stage fails. Windows adds the native-ui/preferences/recovery
label union, marks parameter-provenance portable, and schedules the locally qualified
closure application scenarios with explicit silent output. Mac adds the existing
sample-library binary and repaired full interface harness. Both retain conformance
reports, songs and build receipts. Workflows have not been dispatched or qualified;
the first Mac baseline deliberately remains incomplete until its native catalogue
and results are reviewed and pinned. No required assertion was made optional.

Still open in P0c: final harness execution, Mac inventory, codec recovery/differential
vectors, declared-versus-scheduled standalone test inventory, helper alignment and
reciprocal edited/no-edit fixture exchange. P0b's source repairs still await the next
hourly build checkpoint (not before 21:10:15 UTC).

## P0c typed corpus and no-edit Windows leg — 9 October, 20:28 UTC

Independent P0c tooling progressed without another native build. The
[conformance tools](../../editor/Tests/Conformance/README.md) pin all five supplied
projects and every original archive member. The comparator distinguishes bool/int/real,
exact integers, IEEE-754 bits, ordered arrays, unknown fields, stable IDs and opaque
bytes; it has no ignore list or normalization exceptions. Eleven focused comparator
checks passed; two additional runner safety checks passed, including no replay after
uncertain Save and refusal to overwrite existing evidence or launch a mismatched binary.

The actual Windows ARM64 app at **5358a8ebcaafae88c071cd7ed8fd27c227c747db**,
SHA-256 `39cd3cae8c118d54ccec70053b18d15a0a107d3e1012bbc9da96b999c44f75b8`,
passed F01–F05 original → Save As → reopen → second Save As with exact typed-tree
equality for both saves. The owned private-desktop run preserved the original inputs,
complete corpus, executable, clipboard and foreground window. Outputs and full RPC
receipts are retained in `bin/parity-evidence/p0c-windows-no-edit-01/{first,reopened}`
and `report.json`. This qualifies that existing binary, not the later unbuilt P0b
repairs. It is one Windows no-edit leg, not reciprocal Mac or edited-file qualification.
The retained report SHA-256 is
`e018e14ee69f82dff1c22568f974d7e9077e415ba340b7efb2e69a65ed6c7b5e`.

Prepared Mac harness repairs add the omitted `GraphTrimControls.swift` source and
separate the new `graph.trim.get` inspector read from the graph draft test's pending
write queue. The fixture additionally checks the captured graph/node and hidden empty
trim controls. Existing repeated-arrow, one-write and retained-draft assertions remain.
This diagnosis follows current source and the original reference failure; actual Mac
compilation/execution is pending. Branding compatibility prose now matches the existing
best-effort recovery/source-protection contract; no persisted identifier changed.

Logs: `p0c-typed-tree-01.log`, `p0c-roundtrip-safety-01.log`, and
`p0c-windows-no-edit-01.log` under `bin/parity-evidence`. P0c still requires API
contract vectors/inventory, codec divergence classification, declared-versus-scheduled
test coverage, helper alignment, Mac harness results and reciprocal fixture legs.
The complete P1–P8 implementation scope remains outstanding.

## Initial grouped P0b gate — 9 October, 20:06–20:17 UTC

Candidate `5358a8ebc` built successfully in one consolidated ARM64 invocation,
starting after the hourly checkpoint. The next normal build checkpoint is no earlier
than 21:10:15 UTC. Source repairs are batched until then; no per-failure rebuild.

- The 66-check native/worker gate passed 62 checks, including F04 and all three Parity
  WAVs, bank/master/catalogue result recovery, all 23 envelope scenarios, all controller
  checks, and native docking/ownership. The three failing receipt/unknown-asset/command
  groups stopped early; their later scenarios remain unexecuted.
- The fourth failure occurred after every sample-readback assertion passed: the observer
  detected a changed foreground window. The same binary's focused rerun passed in 68.15 s,
  including foreground/clipboard preservation. The initial failure remains recorded;
  its cause is not inferred as a product defect or user interaction from this evidence.
- All 26 selected actual application tests passed without skips in 44.919 s: recording,
  MIDI setup/review, sample recorder ownership, shortcut/text ownership, recovery, typing,
  note release and worker-wait input. Audio-dependent cases used explicit silent output;
  this does not qualify a physical input device or foreground appearance.

Source repairs prepared together: typed strings in the direct-render observation
adapter (MSVC warned about comparing literal arrays); unknown-render entry tests now
assert declined unverified observation plus unchanged song/exact owner instead of
expecting the old permanent exception; the real multisample fixture supplies the API's
required two files and asserts preview zones; MIDI comparison excludes only the changing
per-read hostTime and still compares all take/events/loss state. They are not rebuilt
or qualified yet. No production/API validation or safety assertion was relaxed.

Logs under `bin/parity-evidence`: `p0b-closure-build-01.log`,
`p0b-closure-native-worker-01.log`, `p0b-closure-app-01.log`, and
`p0b-closure-readback-recheck-01.log`. `p0b-closure-receipt.json` pins 1,884
source/dependency inputs, compiler/cache identities, nine executables and logs for the
initial candidate. Windows x64, affected Mac builds and reciprocal fixture exchange
remain unqualified. P0b is not complete; these scoped passes are not a parity percentage.

## Grouped closure candidate (source prepared; qualification pending)

C1–C5 are now prepared together for one consolidated build, rather than one build
per editor. This is source evidence only; none of these pending changes has been
compiled or executed yet.

- **C1:** Envelope bank retains capture-name-only drafts, raw generations and exact
  worker receipts through parent/child completion. Bank/catalogue reads stage before
  replacing fields. Unknown outcomes have guarded read-only acknowledgement; catalogue
  acknowledgement rechecks its independent revision. EnvelopeOperations prepares exact
  attribution before musical/file effects, with no-op/dry-run preservation.
- **C2:** Render options, multisample import, direct selection render and recorder Keep
  can review genuinely unknown outcomes without repeating writes. Current catalogues
  and take identity are observations, explicitly unverified. Newer raw inputs invalidate
  acknowledgement, remain retained, and require an explicit new target/rebase where
  appropriate. Sample-detail deleted-target observation was already implemented; no
  second implementation was added.
- **C3:** One Main command-result owner covers native sample commands, MIDI lifecycle
  (including transport Stop) and Save. Exact receipts survive failed native completion.
  Unknown Review checks original assets/clipboard, take or validated destination file
  plus recovery state; acknowledgement rechecks independent effects. Later raw fields
  remain untouched. API callers retain their existing structured outcomes. Main now
  has nine registrations; the earlier eight-owner census remains the prior checkpoint.
- **C4:** Single sample/instrument import and both instrument.create forms prepare exact
  results before Stop/commit, then publish after successful adoption. Mapped instrument
  creation adopts a validated prepared candidate. Tests cover precommit allocation/Stop
  refusal, publication loss, no-ops, initial mapping, exact history and stable identities.
- **C5:** Ten explicitly selected workspace groups share one executable and keep the
  existing private-child/CTest deadlines. The source-only mapping in
  [closure-test-groups.json](closure-test-groups.json) finds no lost scenarios and removes
  six duplicate calls. Existing envelope operation scenarios now also use the main
  build's current libraries and worker test selection; the standalone Python catalogue
  oracle remains a distinct check, not an implied pass.

New actual-Application fixtures cover bank/master/catalogue completion, native Main
sample/MIDI/Save outcomes, unknown asset observation and the supplied F04/three Parity
WAVs. Original fixtures live in `editor/Tests/Fixtures/Parity20261009` with archive and
member SHA-256 provenance. They are immutable inputs; all edited outputs use owned
scratch directories. F04 checks full chunked PCM and stable sample IDs through dry run,
committed render/import, lost completion, read-only Review, one Undo/Redo and save/reopen.
This is not reciprocal Mac qualification. Recorder callback cases are controlled native
owner checks, not physical capture evidence.

Remote main was rechecked at `16cab100b898f66726cfab6c2386887a9bcf5fd4`.
No running ScreamSeq process was present at the candidate inspection. Source formatting
checks passed; no build, executable test, app launch or device operation has yet run
for this grouped candidate. P0b remains incomplete until C6 qualifies it.

## Remaining P0b closure checklist

[The finite closure checkpoint](P0B-CLOSURE.md) records the current owner census,
confirmed result/draft gaps, exact source boundaries, grouped acceptance cases and
integration gate. It supersedes the open-ended “next owner audit” instructions in
older entries below. The verified inventory is 29 native tool classes (24 draft
summaries) and eight Main registrations. C1–C4 form one grouped implementation
candidate; C5 prepares non-overlapping test/CI scheduling; C6 qualifies the frozen
candidate. The checkpoint and standing cadence were prepared without any build,
test, application launch or device operation. No new product qualification is
claimed by this documentation checkpoint.

## Execution adjustment: close P0b in cohesive batches

The user asked to accelerate progress while P0b is still open. Stop treating each
newly inspected control as a separate build/qualification milestone. The remaining
P0b work will use three closure batches, governed by the five exit requirements in
`final-planning-review.md` rather than an expanding list of desirable refinements:

1. **One remaining-coverage audit.** Inventory the required Main/nested owners and
   side-effect domains once. Map each to current implementation and scoped existing
   evidence; identify concrete unmet exit requirements. Distinguish required draft,
   identity and uncertain-write safety from improvements already assigned to later
   phases. Current candidate requiring inspection is the main sample-panel edit
   path; do not assume every generic edit caller needs an identical new UI owner.
2. **One cohesive closure implementation batch.** Group the audit's confirmed gaps
   by common command/result boundary and reuse existing admission/receipt machinery.
   Prepare the related fixes and fault cases together before compiling the native
   application. Split only for a material dependency/risk reason, not per control.
   Rebuild once after the source freeze; repeat only invalidated checks or failures.
3. **One P0b integration gate.** Reuse valid exact-input evidence, run the necessary
   common-dispatch/workspace/recovery checks, the planned F04/Parity WAV cases, and
   affected Windows/Mac platform builds/checks. Integrate on shared main, then move
   to P0c and the P1/P2 feature batches. Do not postpone platform viability until P8.

This changes execution granularity, not acceptance criteria. No required safety,
platform, fixture or compatibility check is waived; no P0b completion percentage is
claimed before the coverage audit. New findings enter the fixed exit checklist or
their existing later phase instead of automatically starting another P0b mini-batch.
The just-completed sample readback batch is committed as `daddecd6c`; all four native
groups and nine targeted application cases passed. P0a is merged; P0b is not yet
closed or merged, and P0c–P8 remain outstanding.

## P0b.1 Atomic sample readback and navigation

`windows/App/SampleReadback.inc` now owns the sample inspector's staged metadata
and waveform reads, full Reload/From cursor, loop-only Reload, viewport and channel
changes. It is reused by post-mutation refresh in `SampleMutation.inc`; duplicate
waveform request/adoption code was removed. This completes the explicit Reload
read-ordering follow-up identified in the preceding sample mutation checkpoint.

Full Reload prepares the target context, stable identity, remembered region map,
metadata, validated loop draft, selected channel and bounded waveform before
adopting them or discarding the submitted drawing/raw fields. Loop-only Reload
prepares the same fallible reads while retaining other raw fields and drawing at
their original draft revision. A changed song revision or newer field generation
during either read rejects adoption, leaving that newer work intact. Invalid
metadata or a malformed/mismatched waveform also cannot clear the current view.
The shared waveform reader checks requested sample/range/channel/bin identity,
bounded peak count and finite values before adoption; empty views remain bounded
empty results. Pure region clamping is reused without publishing intermediate
state.

Viewport and channel changes stage their waveform first. Failed channel and
sample choices restore the old native combo selection so its label continues to
match the retained waveform/target. Successful explicit retry uses the current
request. All paths retain the existing native UI and shared worker operations;
there are no new wire methods, project-format changes, audio changes or Mac edits.
This is native presentation state, not a replacement musical model.

Validation used one incremental build of `workspace-restore-tests` and `ScreamSeq`
(Windows ARM64, parallelism 2), then bounded private-desktop checks:

- Four CTests passed in 90.94 seconds total: draft census 8.97, departure 5.32,
  sample readback 67.86, and previous sample result recovery 8.76. Each child keeps
  its existing 90-second bound; the readback group has a separate registration.
- The new readback group exercises 31 scenarios through the actual Application
  and native HWNDs. It covers Reload, From cursor and loop-only Reload at both
  metadata/waveform boundaries with thrown failures, malformed results, newer raw
  input and intervening document edits (24 cases); viewport/channel failure,
  malformed result and newer input (6); and sample selection failure/retry (1).
  Assertions compare retained target/revision, waveform/ranges, drawing, loops,
  raw text, song/history, focus and corrected combo selection. Successful retry
  proves the authorized discard/adoption; loop-only retry preserves other drafts.
  The previous mutation/chooser/unknown-outcome cases also passed against the
  shared reader. Private-process lifecycle, foreground and clipboard guards passed.
- Nine actual-application cases passed in 70.521 seconds, no skips: waveform
  zoom/cache/selection bounds; raw loop drafts and explicit Reload; drawing with
  Undo/Redo/save/reopen; all 14 processing operations compared with the shared API;
  retained drafts and guarded document replacement; history removal and reuse of
  the old slot by a different stable sample; joint loop Preview/Apply/storage;
  keyboard Apply/focus/caret retention; and captured replacement with chooser
  cancellation/stale guards/history/reopen.
- The document-replacement test now asserts refusal while a drawing is retained,
  explicit discard, retirement of the old inspector and fresh open with unchanged
  stable identity/PCM. The history test now actually reuses the removed slot,
  verifies historical Review never adopts that occupant, and verifies a subsequent
  Reload remains unavailable until explicit From cursor. These strengthen the
  obsolete pre-departure assumptions without weakening music/draft assertions.

Evidence: `bin/parity-evidence/p0b-sample-readback-receipt.json` pins 1,861 source
and dependency inputs, compiler/cache, executables, aggregate and detailed logs.
Build, native and application runs passed on their first executions. The only
source changes after the initial freeze were the two strengthened Python cases;
no product or native test input changed after compilation. No broad suite was
repeated.

P0b remains open. Next concrete owner-audit entry is
`windows/App/SampleEditor.inc::sampleCommand` (main-panel sample actions), which
currently calls `EditingView.inc::edit` and lacks this inspector's retained native
completion/Review owner. Inspect its range draft and pending clipboard read as
well as the final write before extending the pattern. Prepared commit receipts
inside remaining asset operations, other owners, integration on shared main and
P0c–P8 remain required. No Windows x64/Mac, foreground/accessibility, physical
device or supplied reciprocal-fixture qualification is claimed here. Unknown
sample-removal readback without a returned receipt remains separate from the
now-tested historical receipt/slot-reuse path.

## P0b.1 Sample editor mutation recovery and staged completion

The sample editor now routes non-preview operations through its own
`NativeWriteCompletion` and the Application's exact per-call worker ticket
(`windows/App/SampleMutation.inc`, `SampleDetailWindow.hpp`, `Main.cpp`). This
covers settings, replacement, processing, drawing, loop edits, clipboard
copy/cut/delete/paste/copy-to-new, Create instrument and native document Undo/Redo.
Preview remains read-only. The editor captures method, original document/revision,
stable sample identity/slot, request, draft generation and raw edit fields.

Post-write sample metadata and bounded waveform reads now stage locally before
adopting the refreshed context or clearing submitted fields/drawing. Each read is
followed by document/revision/generation checks. A worker return followed by a
native exception, failed read or newer raw edit leaves the exact receipt and
unapplied drafts retained. Review synchronizes and presents that historical
completion without repeating the write, reopening a chooser, retargeting the
inspector, rebasing its drafts or stealing a newer focus choice. Hide/reopen keeps
the same owner. Pending and unresolved outcomes participate in document departure;
conflicting controls and gestures remain disabled until resolution.

Without a receipt, Review reads the current stable sample catalogue and original
sample metadata, if that identity still exists. It additionally reads the private
clipboard for copy/cut/paste and the published document (including validated
instrument identities) for Create instrument and Undo/Redo. A missing original
sample is an observation, not permission to adopt its former slot's occupant.
Readback remains explicitly **unverified** and requires **Accept state** against
the same observed document/revision. Failed/malformed reads and stale
acknowledgements retain uncertainty. Neither Review nor acknowledgement writes or
claims that equal song revision proves clipboard success. Explicit Reload is
needed to adopt current audio after a historical/unknown completion review.

Replacement's chooser contributes pending ownership before submission and rejects
nested replacement, API Open, and changed captured context. Cancellation or a
chooser exception does not leave an unresolved mutation. The native labels are
**Review** and **Accept state** while unresolved, then return to **Reload** and
**From cursor**. No shared operation, public API, codec, plugin state, audio/device
integration or Mac source changed.

Validation (Windows ARM64, private desktops):

- Incremental `workspace-restore-tests` and `ScreamSeq` build passed. Initial
  compilation found a test-only helper mismatch; the shared
  `DocumentDraft::retained()` assertion corrected it.
- Three CTests passed in 23.20 seconds: draft census (9.05), departure (5.32), and
  the new sample-result group (8.79). The new group covers real replacement,
  pending chooser/nested/API refusal, exact result loss, newer raw text/focus,
  hide/reopen, unrelated sample preservation, single Undo/Redo, drawing failures
  at both metadata and waveform refresh, unknown clipboard and instrument
  outcomes, malformed/failed domain reads, stale acknowledgement and no replay.
  The private-process foreground/clipboard/lifecycle guards passed.
- Ten targeted actual-application cases passed across two runs: seven in the
  initial ten-case run (70.589 seconds), then the three affected save/reopen cases
  in a 14.285-second recheck on the **same binary**. The initial three errors came
  from the new helper reading the API envelope instead of its data. The corrected
  helper retains assertions for stable identity, slot, full settings and PCM.
  No product source changed after the successful build/native run.
- The application cases cover settings/no-op/invalid/stale drafts, explicit
  departure refusal/discard, captured replacement with chooser cancellation and
  stale guards, drawing and persistence, all 14 processing operations compared
  against the shared API, joint loop Preview/Apply/Undo/persistence, replaced
  clipboard review with raw option retention, Ctrl+Enter focus/caret retention,
  and minimum-size controls/F6. No skips; no assertion was removed or relaxed.

Evidence: `bin/parity-evidence/p0b-sample-mutation-receipt.json` freezes 1,859
source/dependency inputs, toolchain/cache, executable and retained log hashes.
It retains both initial failures and the exact recheck scope. Only the helper's
three affected cases were repeated; valid unrelated evidence was reused.

This is a targeted checkpoint, not completion of P0b. Remaining work includes
atomicity of explicit sample Reload/loop Reload (those older read paths still
clear drafts before a fallible waveform read), prepared commit receipts inside
remaining asset operations, the rest of the owner audit, and integration on main.
Unknown sample removal/slot reuse is guarded in source but not newly exercised by
this fault-injection group. Windows x64, Mac, foreground/UIA, physical devices,
all five supplied reciprocal fixtures and P0c–P8 remain open.

## P0b.1 Instrument import and creation recovery

`InstrumentEnvelopeWindow` now shares one native creation-result path for **Import
instrument** and **New from sample** (`windows/App/InstrumentCreation.inc`). It
captures the original song/revision, source sample list/identities, inspector
identity, request and generation before the chooser/write. The owner contributes
pending work even before it has loaded an instrument target. Cancellation,
chooser failure and stale pre-submission context leave no unresolved write.
Instrument, tool-field and nested envelope-bank drafts must be resolved before
starting a new creation.

Production Application wiring passes `documentOperationWithOutcome` and its exact
per-call worker receipt. A post-write native failure retains the original result;
reopening the inspector or invoking another creation cannot append another asset.
Reload becomes **Review result** and From cursor becomes **Use current song** while
unresolved; short docks use **Review** / **Accept**. Pending/uncertain work blocks
API replacement and departure and cannot be discarded. Hide and layout navigation
remain available. Mutating controls, canvas gestures and conflicting inspector
retargeting are disabled until Review/acknowledgement.

Successful immediate completion selects the created instrument and sound only
when the returned song revision and original draft generation still match. Review
of a returned completion presents its historical slot/result without retargeting,
reloading, replaying the write or clearing newer raw fields, selection or focus.
The existing APIs return an index for these operations; the native caller resolves
a stable identity for immediate selection only against that exact returned
revision, never against a later occupant of the slot.

An absent receipt triggers a synchronized document-snapshot read of instruments
and samples. Application supplies this read from its published view; `document.get`
is not a direct worker operation. Readback validates collections and identities,
labels the earlier effect **unverified**, retains the unresolved owner and requires
explicit acknowledgement before a fresh operation. Read failure/malformed state or
a changed observed revision cannot discharge that work. Acknowledgement does not
write, rebase a draft or imply that a particular instrument came from the uncertain
request. Untyped import decode errors take this same conservative Review path.

No shared musical operation, codec, public import API, plugin state, audio/device
integration or Mac product code changed. Both platforms retain their existing
musical import/creation semantics and chronological history. This is the native
caller batch; prepared commit receipts inside the remaining asset operations,
sample replacement and the rest of the P0b owner audit remain separate work.

The first native/actual-app validation exposed the incorrect direct-worker
`document.get` dispatch; it was corrected to Application snapshot readback. An
older API save/reopen test also expected a retired inspector to survive document
replacement. It now explicitly reopens that inspector and checks the same stable
instrument identity, slot and full saved contents. The chooser test now asserts
that API Open is refused while the original instrument chooser owns pending work,
and that corrupt imports require Review/acknowledgement rather than blind retry.
Original atomicity/history/content assertions are retained.

Validation (Release ARM64, `bin/windows-parity-p0`):

- Application, workspace harness and compact instrument harness built successfully.
- **4/4 targeted CTests passed, 54.34 s**: draft census, document departure,
  native receipts and compact instrument docking. New real-worker cases cover
  both Create and Import after lost completion, raw-draft/focus retention,
  read-only Review, one Undo/Redo, nested chooser/API replacement refusal,
  cancellation/failure/stale chooser, unknown outcomes, failed/malformed readback,
  explicit/stale acknowledgement and compact recovery labels.
- **5/5 actual-app tests passed, 28.694 s, no skips**: native SFZ import and sound
  selection, pattern preservation, Undo/Redo and native save/reopen; corrupt/
  canceled/stale chooser and API departure protection; instrument/bank draft
  retention; envelope validation/no-op/history/persistence; all envelope tools.
- `bin/parity-evidence/p0b-instrument-creation-receipt.json` records 1,857 source/
  dependency inputs, three executables, compiler/cache and all six build/test logs.
  The initial failed runs remain recorded alongside the corrected final runs.

Evidence remains limited to ARM64/private desktops. No broad engine suite was
repeated for this native-caller change. Mac/x64, foreground/UIA, physical devices,
supplied reciprocal fixture exchange and P0c–P8 remain open.

## P0b.1 Native plugin preset file-result recovery

Native rack preset Save/Load now captures its stable plugin ID, document/revision,
file path and inspected preset digest in an independent Main draft owner. Pending
work begins before plugin-editor flush and the native chooser. Cancellation and
pre-submission stale-context refusal release pending work; a submitted uncertain
result remains protected across inspector refresh and document-departure attempts.
The parameter/program draft owner remains independent.

`windows/App/PluginPresetIntegration.inc` sends the existing preset API through
`NativeWriteCompletion` and its per-call worker receipt. Save/Load becomes **Review
result** and **Use current state** while unresolved. Review of a known result
validates the returned file/path/digest or original plugin/load result, presents
that historical completion and performs no further write, chooser or selection
change. A later missing/changed file does not relabel the returned receipt.
Newer parameter text, target/revision, draft generation and focus remain intact.
Completed reports retire only after successful document replacement.

Without a receipt, Review reads the captured file through `plugin.preset.inspect`
or the original stable plugin through `plugin.state.get`. A current file or saved
baseline is explicitly **unverified** evidence of the earlier effect. Missing
files/plugins and failed/malformed reads keep the result unresolved. Successful
readback enables explicit **Use current state** acknowledgement, guarded by the
observed document/revision; acknowledgement also performs no write. The next
Save/Load requires a separate deliberate command. Neither equal song revision nor
readback equality is used to declare a file effect committed or rejected.

No preset format, plugin state transaction, routing, history, worker dispatch,
audio/device or Mac implementation changed. Legacy file magic/extensions and
physical-layout fingerprints remain compatible. `workspace` inspection includes
`pluginPresetAction` with captured intent, receipt/readback report and pending/
acknowledgement state. This completes the identified rack native preset caller,
not the full remaining P0b owner audit or other asset/file-effect callers.

Validation (Release ARM64, `bin/windows-parity-p0`):

- `workspace-restore-tests` and `ScreamSeq` built successfully.
- Final draft census passed (9.40 s); native receipt scenarios passed (25.08 s),
  including real Save/Load dropped completion, exact historical receipt, no replay,
  newer raw parameter/focus retention, one Undo/Redo, pending API Open refusal,
  missing original file/plugin, unverified readback, stale acknowledgement,
  explicit acknowledgement, cancellation, stale chooser and report retirement.
- Departure behavior assertions passed in the combined run, but its unchanged
  private-desktop guard observed a foreground-window change and failed the run.
  A separate recheck on the identical binary passed, including isolation (5.39 s).
- Actual application API/native-dialog checks: **5/5 passed, 6.510 s, no skips**.
  These cover preset tokens, binary/XML/Unicode, bounds, atomic overwrite/no
  history, file/song/class guards, malformed/vendor decode rejection, Undo/Redo,
  and native chooser Save/Load/cancel/stale behavior on disposable private desktops.
- Source/dependency, toolchain/cache, executable and all build/test log hashes are
  retained in `bin/parity-evidence/p0b-plugin-preset-receipt.json` (1,855 inputs).
  No broad engine suite was repeated for this native-caller-only change.

The no-ticket test intentionally drops a successful real operation's return outside
its worker receipt path; it proves domain readback/acknowledgement, not failure of
the production ticket. No installed third-party providers, physical devices,
foreground presentation/UIA, Mac/x64 builds or supplied reciprocal project fixtures
were qualified. Shared formats/worker code are unchanged. Those gates, other P0b
owners and P0c–P8 remain open.

The first C++ build exposed mixed string/JSON comparisons; explicit string extraction
fixed them. The first native receipt fixture reached an unsaved-song dialog before
the intended Close guard and hit its existing bounded timeout. Saving the disposable
fixture before that check isolates the retained-work guard without weakening it.
The initial actual-app run passed four of five cases, including the real native
Save/Load/cancel/stale dialogs. Its older exact preset-key assertion omitted the
already-persisted `audioLayout` field (introduced before this batch). The fixture
now retains exact key equality, checks the fingerprint against the saved plugin
baseline and returned summary, and additionally verifies old files without that
field retain the empty-layout read contract.

## P0b.1 Independent sample-library root and rescan recovery

Sample folder Add/Remove and Rescan now submit through the browser's retained
native completion path. The owner captures the exact method, root list/library
revision and browser fields, retains an unresolved result across hide/reopen and
song replacement, freezes conflicting browser actions, and changes Rescan to
**Review**. Stop preview and gain remain available. `Application::canClose` checks
this application-wide work both before and after message-pumping departure checks.
A folder chooser or unresolved library result blocks Close/session end; it is not
registered as a song draft and does not acquire musical Undo or document revision.

Review consumes the exact worker receipt, then reads current library state. With
no receipt, it reads and validates current state, labels that observation
**unverified**, and requires explicit **Reload** before a fresh library write.
Failed or malformed reads keep uncertainty. Review and Reload never start another
scan, change roots, reimport samples or attribute a later state to an old request.
The successful rescan receipt records **accepted/indexing**, not completed scan
work; current scan status is read separately. Configured source files remain intact.

`windows/Samples/Library.cpp::commitScan` prepares the status publication, independent
API response, optional native receipt and scan-queue storage before promoting
`roots.json`. After successful atomic promotion it moves/swaps the prepared state,
publishes the receipt and wakes the scanner. Accepted rescans retain the previous
immutable index while scanning. The standalone library worker now accepts an
optional per-invocation receipt/document identity; that original identity attributes
the request without making global preferences belong to a song. Recognized native
pre-commit failures carry NotCommitted. Pipe calls retain the established independent
API error envelope, response fields, replay behavior and library revision semantics.

The App callback routes only native library writes through this receipt-aware
worker path. No project format, shared musical model, Mac code, audio route or
plugin state changes. This batch finishes the identified root/rescan completion
path; it does not declare the complete P0b owner/entry-point audit finished.

Evidence: `bin/parity-evidence/p0b-sample-library-recovery-receipt.json`, 1,853
source/dependency inputs, both CMake caches, compiler/executable hashes and logs.

| Check | Result and scope |
|---|---|
| ARM64 standalone library, native-tool, workspace and app targets | Built successfully |
| Library worker | Final 1/1, 0.20 s; exact root/rescan receipts, locked-file rejection with no publication, independent revision/search, old-index visibility, cancellation by newer roots, source-file preservation, cache reopen/corruption and ephemeral settings |
| Native-tool group | 1/1, 4.91 s in the initial 4-entry native run (32.41 s total); root/rescan × returned/read-failed, ticket-with-lost-return and unknown outcomes; failed/malformed Review, frozen actions, hidden owners and replacement song. Its sources/binary are unchanged by the later worker-only API-envelope correction |
| Final draft census, departure and Application receipt groups | 3/3, 27.69 s; real root removal/rescan followed by dropped native completion, Close refusal, global owner surviving song replacement, Review without resubmission and unchanged music/history |
| Final actual application sample-library tests | 11/11, 7.656 s, no skips; discovery/guards/error envelope/replay, independent revisions, search/paging/tags/families, silent preview, persisted cache/reopen, batch Undo/save-reopen, retained drafts and minimum-size/source-file-preserving folder removal |

The first build selected the standalone library target in the main build tree;
its dedicated `windows/Tests/Samples` CMake project was then configured separately.
The first worker test compile required explicit string extraction in a JSON
comparison. Source review also caught the existing API assertion that independent
library errors have no added data; native outcome annotation was restricted to
receipt callers, preserving that assertion unchanged. The final worker/Application/
API checks passed after the correction. Initial logs and source freeze are retained.

The native-tool test binary is reused only for unchanged native-tool inputs. No
current full-workspace or full-app suite is claimed. Physical/live-audio preview,
foreground folder-chooser/UIA behavior, Windows x64, Mac integration, supplied
reciprocal fixtures and final qualification remain open. Further visual work should
also review how background scan diagnostics persist during search/status refresh;
this batch proves retained request outcomes, not every later scan-presentation state.
P0b's remaining owner/cache audit and all P0c–P8 work remain active.

## P0b.1 Direct file import reuses the retained native import owner

The main Import command, Ctrl+I and explicit raw-sample/mapped-instrument commands
now share the sample browser's captured `sample.importMany` submission and Review
path. `SampleEditor.inc` retains the native OS picker and its modal scope; it no
longer has a second untracked musical import/reveal implementation. The browser's
**Choose files** action uses the same chooser lifecycle. `ensureSampleBrowser`
constructs the native owner without showing it. Normal direct import does not open
the browser; repeating a command with an unresolved result raises its existing
**Review import** control, without opening another picker or resubmitting music.

The native owner captures document/revision, mapping option and generation before
the picker. It registers pending work during the picker, refuses nested import,
releases it on cancellation/failure, and rechecks the song before import. An API
`document.open(discard:true)` cannot replace the song beneath this pending owner.
An ordinary edit during the picker makes its eventual selection stale instead of
automatically rebasing it. Direct commands do not overwrite the browser's own
Create instruments choice. Their command-dispatch branch also avoids the generic
focus reset, preserving the sample range's raw text, identity, revision and focus.

This removes native orchestration duplication while retaining the existing shared
`Document::importSamples` transaction, API, prepared receipt, codec and Undo. A
tradeoff is lazy construction of the sample library/hidden native browser on the
first direct import; its existing background cache/index lifecycle is reused, with
no second recovery window or separate result state. The picker still belongs to
the initiating native window. App-wide folder/rescan work remains a separate audit.

Evidence: `bin/parity-evidence/p0b-direct-import-receipt.json`, 1,851 source/dependency
inputs plus final compiler/cache/executable hashes and five retained logs.

| Check | Result and scope |
|---|---|
| ARM64 native-tool, workspace and application builds | Passed |
| Native tools and Application receipt groups | Final 2/2, 15.00 s; library selection and direct chooser each cover known receipt, lost return and unknown result; repeated direct command does not choose again. Real Application cases cover cancellation/failure, pending admission, stale edits, mapping options, preserved draft/focus, post-commit failure and one Undo |
| Draft census and departure groups | 2/2 in the initial run, 9.09 s and 5.33 s. Product sources were unchanged by the subsequent fixture correction; these groups do not execute the corrected import fixture |
| Actual application sample-library and retained-shortcut checks | 8/8, 9.315 s, no skips; native library workflows, shared batch Undo/persistence and global shortcuts retaining editor focus/raw text |

The initial receipt fixture expected only one new instrument when importing into
a sample-only song. `editor/SampleImport.cpp` intentionally creates mappings for
all old sample slots first to preserve old notes. The corrected fixture asserts
the exact resulting count and every mapping, including the imported sound; the
product behavior was not changed to fit the initial assertion. Initial failure
logs and the original source freeze are retained. No assertions were removed.

The new Application fixture substitutes only the OS picker result; all command,
registry, worker, receipt, native controls and Undo paths are real. It does not
qualify foreground common-dialog interaction. Worker/codec semantics did not change,
so this batch does not rerun their broad suites. Windows x64, Mac integration,
physical/foreground accessibility, reciprocal fixtures and the final gate remain
open. P0b next audits independent library roots/rescans and their close/recovery
behavior; the full P0c–P8 objective remains unchanged.

## P0b.1 Sample-browser import receipts and composing context

The sample browser now submits `sample.importMany` through `NativeWriteCompletion`
and retains the original paths, instrument option, song/revision and generation.
Pending and uncertain imports register with document-departure admission. Hiding,
reopening or refreshing the browser cannot drop the receipt or submit the batch
again. **Review import** consumes an exact worker receipt without repeating import
or reveal; an old result never uses its sample slots to retarget today's editor.
If there is no receipt, Review synchronizes and reads the original song's current
samples/instruments, labels the observation **unverified**, and requires explicit
**Use current song** before a fresh import. Failed/malformed observations and a
changed original document retain uncertainty. Stop preview and raw gain entry
remain available while the import selection is frozen.

The worker prepares the full batch result before Stop and the atomic asset commit,
then publishes that preallocated receipt immediately after commit. Allocation or
Stop failure cannot create a success receipt; later publication/native callback
failure cannot lose the committed destinations. Unchanged-revision failures for
this operation carry the proven NotCommitted outcome. The API shape, project
codec, stable identities and one-transaction Undo semantics are unchanged.

Successful browser completion reuses the existing sample reveal helper. A sample
range draft keeps its raw text, captured identity and revision instead of being
cleared by import. The browser temporarily parks focus on its owner before
controls are disabled, restoring the original enabled control only if focus has
stayed there; an unresolved import offers focus on Review. Navigation to another
control or window during completion wins. The new Application fixture verifies
both retained focus and newer focus choices in the sample range and preview gain.

Evidence: `bin/parity-evidence/p0b-sample-browser-receipt-receipt.json`, with 1,851
source/dependency inputs, compiler/cache/executable hashes and retained logs.

| Check | Result and scope |
|---|---|
| ARM64 native-tool, controller, workspace and app targets | Built successfully; focus correction rebuilt affected native-tool/workspace/app targets |
| Worker asset/publication checks and setup dependencies | 4/4 passed within the initial 5-entry boundary run (5.05 s including native tools); exact receipt before failed publication, dry run, allocation/Stop refusal, exact PCM/identity Undo/Redo including allocator high-water mark. Worker sources and dependencies are unchanged by the later focus fix |
| Final native-tool and Application receipt groups | 2/2 passed, 11.55 s; known/unknown receipt, failed/malformed Review, hidden owners, no replay, departure refusal, raw sample draft and newer focus preservation |
| Draft census and departure groups | Both passed in the initial workspace run (9.28 s and 5.43 s); these are earlier scoped checks, not a final full-workspace pass |
| Final actual application sample-library checks | 7/7 passed, 5.254 s, no skips; batch Undo/persistence, native search/selection/import, gain/partial text, retained multisample drafts/rebase, minimum-size controls and folder removal preserving source files |

The initial Application assertion failed. Splitting it without removing any term
isolated a real focus loss caused by disabling the focused browser child; every
sample draft term already passed. The focus correction and the added newer-focus
cases then passed. An initial diagnostic build used a nonexistent target name;
the corrected `workspace-restore-tests` target built successfully. Initial logs
and the original source freeze are retained; no assertion was weakened.

This closes this browser batch-import path only. Direct file-dialog sample import
and independent sample-folder/root/rescan completion still require the P0b entry-
point audit. No Mac/x64, physical audio, foreground/UIA, supplied reciprocal fixture
or final integration qualification is claimed. P0b and the full P0c–P8 scope remain
active; prior planning findings retain their dated baseline.

## P0b.1 Plugin-library completion and application-wide draft retention

`PluginLibraryWindow` now submits rescans, browser preference changes and Add to
rack through `NativeWriteCompletion`, with a captured method/parameters, library
selection/revision, raw category, document and draft generation. Failed completion
keeps that request across hiding/reopening and changes Reload to **Review result**.
Another write, rescan or draft discard is refused while the result is unresolved.
Exact worker receipts are reviewed without repeating the operation. Category Apply
clears only its submitted generation; newer raw category text survives completion.

When no receipt exists, Review reads current library data with `rescan:false`, or
the original song's current rack after synchronizing its view. The report labels
this observation **unverified**, preserves submission and raw fields, and requires
explicit Reload before another write. Failed/malformed observations, unavailable
preferences, a missing preference target, or a changed original rack context retain
uncertainty. Observation never attributes current state to the earlier operation.

Library preferences, scans and category drafts remain application-wide: replacing
a song preserves their native owner. An unresolved Add to rack registers a
song-scoped pending/uncertain draft and blocks replacement. `Application::canClose`
checks global library work both before and after its message-pumping departure
checks; Close/session-end cannot silently discard it. Escape may hide an uncertain
library, but cannot discard it. A reviewed raw category can still be discarded
explicitly with Escape/Reload. Stale library-revision rejection now carries the
proven NotCommitted outcome at the locked, pre-write check in `PluginLibrary::set`.

The actual `plugin.add` API returns `slot` and `dryRun`, not a stable instance ID.
Its exact receipt proves the original insertion, but Review never uses that old
slot to select or inspect today's rack. A changed song context produces a historical
completion message. The external API response and preference storage schema remain
unchanged; the UI continues using the existing independent library revision.

Evidence: `bin/parity-evidence/p0b-library-completion-receipt.json`, 1,849 source/
dependency inputs, final compiler/cache/executable hashes and retained logs.

| Check | Result and scope |
|---|---|
| ARM64 native-tool, app and workspace targets | Final build passed |
| Native tool suite | 1/1 CTest entry, 3.57 s; six scan/preference/add × known/unknown cases, failed/malformed Review, scope separation, hidden/reopened state, newer raw input and no side-effect replay |
| Full workspace plus draft/departure/receipt groups | 4/4, 105.38 s; new real Application case drops preference and Add responses after actual worker success, preserves library across document replacement, refuses Close/departure appropriately, retains newer category and verifies a single plugin Undo |
| Actual application library tests | 5/5, 12.316 s, no skips; API filters/defaults/dry/no-op/replay/independent history, stale concurrent writers, locking/atomic failure/corruption, bounded preferences, native search/favorite/hide/category/add workflows, stale drafts, keyboard and minimum-size geometry |

The first native fixture build placed its header inside an anonymous namespace;
a second compile caught a string/JSON comparison requiring explicit extraction.
The first real-app run then exposed the incorrect Add result assumption described
above. Product/fixture handling was corrected to the current API and the complete
selected checks reran successfully. Initial failure logs and source freeze remain
available; no assertion was weakened or excluded.

No vendor/device behavior, project format, shared engine or Mac UI changed. Windows
x64, Mac integration, physical/foreground accessibility, supplied reciprocal
fixtures and the final cross-platform gate remain open. Installed-instrument and
live-audio optional library cases were not selected or claimed. P0b still requires
the remaining retained-owner/cache entry-point audit; P0c–P8 remain outstanding.

## P0b.1 Prepare asset receipts before the musical commit

The previous callback-loss receipt is now prepared **before** the asset commit.
`AssetOperations::PrepareImportCommit` builds a notification from the complete
result before Stop/transaction; `ImportCommit::committed() noexcept` publishes it
only after the single asset transaction returns successfully. Dry runs and failed
preparation/Stop do not publish. The operation layer exposes a narrow notification
interface rather than depending on native HWNDs, the API transport or a UI owner.

`DocumentController::prepareAssetCompletion` preallocates method, document,
post-transaction revision and result. Its revision uses the existing format and
the one-transaction asset contract: document revision advances once while identity,
sequence and plugin revision remain unchanged. The full render reply (including
selection/crop/latency fields) is retained; the recorder append hook receives the
validated take identity so the preallocated Keep reply includes it. Multisample
receipts retain the exact imported zones. Native ticket publication and controller
retention then require no allocation. Successful completion and failed view
publication reuse the same immutable receipt.

The real controller can now classify a failed Keep with an unchanged revision as
NotCommitted: its own append path consumes no take until the prepared import
succeeds. This inference is deliberately absent from the generic injected recorder
hook, whose failure can follow arbitrary host effects, and from start/stop/discard,
plugin, file and catalogue paths. The musical transaction, Undo implementation,
project format and external API response shape are unchanged.

This closes the previously recorded allocation window between a successful
render/multisample/Keep import and retention of its result. It is in-process recovery,
not durable crash replay. Unknown failures outside these prepared boundaries remain
subject to the wider P0b audit; no current-state sample/take inference is introduced.

Evidence: `bin/parity-evidence/p0b-prepared-asset-receipt-receipt.json` pins 1,847
source/dependency inputs and the final compiler/cache/executable/log hashes.

| Check | Result and scope |
|---|---|
| ARM64 app, workspace, controller and portable recorder targets | Final incremental build passed |
| Worker/recorder CTests | 7/7, 1.19 s, including two fixture setup entries. New real-asset cases cover dry run, preparation allocation failure, Stop refusal, exact Undo/Redo with retained allocator high-water mark, receipt availability before every failed view publication, and full render/multisample result identity |
| Workspace receipt/departure/draft groups | 3/3, 17.39 s; lost-callback Review, no duplicate render/Undo, raw drafts and departure admission |
| Actual application, private desktop | 5/5, 3.963 s, no skips; direct/options render history and reopen, multisample retained drafts/import transaction, recorder setup retention |

The initial new Undo assertion incorrectly expected the stable-ID allocator to
rewind. Inspection of `Document::undo` established its intentional high-water-mark
contract. The corrected fixture asserts the exact committed nextID after Undo,
all other restored native content, exact module bytes and exact Redo content/IDs.
The failing log and initial source freeze are retained; no product change or
unexplained identity normalization was used to make it pass.

Mac also compiles `SampleRecordingOperations` and this updated hook fixture in
`windows-sample-recording-tests` (`windows-sample-recording` CTest). That Mac target,
Windows x64, physical recording, foreground accessibility, supplied reciprocal
fixtures and the complete integration gate remain required. No Mac build was
available in this local run. No shared engine or Mac UI implementation changed.

**Next confirmed P0b work:** `PluginLibraryWindow::load/change/addPlugin` clears
pending state on exceptions without retaining scan/preference/insert outcomes.
Its app-wide library ownership must stay distinct from document replacement;
Review must not rescan or repeat a successful add/preferences write. Finish this
entry-point audit and remaining stable-owner checks before closing P0b.

## P0b.1 Native request receipts survive callback loss

Each retained native render, multisample import, recorder Keep and plugin reconnect
submission now allocates a separate `NativeCallReceipt` before queuing work.
`DocumentController::invokeCompleted` publishes its exact method, document,
revision and result on that ticket before Application completion callbacks run.
On failed view publication it publishes only the controller's own result, never a
receipt supplied by a nested callback. Native Review can recover this result even
when an outer callback drops the inline return value or misclassifies its failure
as NotCommitted. A later queued edit cannot relabel the original result.

The ticket is in-process and scoped to one invocation; it introduces no wire ID,
replay cache, persisted field or shared song-model change. `NativeWriteCompletion`
keeps raw fields/generation and blocks resubmission until Review finishes. Finishing
releases the ticket; a new submission receives a different one. Existing native
owners retain their current readback/reveal and newer-draft preservation policies.

This closes **lost native callback results when the worker owns an exact result**.
It does not close every unknown import/render/Keep outcome: an exception before
an operation produces its result, including result/identity allocation after a
commit, may still have no receipt. Keep that state unresolved; sample presence or
an absent take is not proof of the original write. The remaining P0b audit must
cover those commit/result boundaries and the other retained owner/cache paths.

ARM64 Release evidence is pinned in
`bin/parity-evidence/p0b-native-call-receipt-receipt.json`: 1,846 source/dependency
inputs, six executable hashes, compiler/cache/provider inputs and seven logs.

| Check | Result and scope |
|---|---|
| App, workspace, native-tool, controller and recorder builds | Passed; Windows native/session changes only |
| Native tool suite | 1/1 CTest entry, 3.25 s; exact receipts override downstream false refusal, no resubmit, no late-ticket crossover |
| Focused workspace groups | 3/3, 17.34 s; real worker render with dropped callback, later edit, readback and exact one-render Undo; draft census and Application departure |
| Worker/recorder selection | 7/7, 1.12 s, including two fixture preparation entries; publication failure retains the controller receipt, rejected imports have no receipt, subsequent edits do not relabel results, sampling/assets/recording regressions |
| Actual app on private desktop | 7/7, 10.599 s, no skips; direct/options render Undo and exact reopen, multisample import/draft guards, browser import transaction, recorder setup retention, native reconnect and real-effect recipe/Undo |

No physical recording was needed. No foreground/accessibility, Windows x64, Mac,
supplied reciprocal-fixture or final P0b/P8 gate is claimed. The full workspace
suite was not rerun: the receipt/departure/draft groups target this change, while
older broad evidence retains its original checkpoint. Production source did not
change between the build and those tests; the additional publication assertions
required only a focused controller-test rebuild.

## P0b.1 Plugin path and scan readback

`PluginPathWindow` now retains explicit path-scan and installed-rescan intent
through request/completion failures, including submitted parameters, raw fields
and generation. Scan uncertainty is independent of the song revision. The owner
blocks Verify, Reload, reconnect, another scan and draft discard until Review;
hidden/reopened windows retain this state and document departure refuses it.
Known typed preflight refusal can release scan intent, but a later presentation
error cannot classify a request that already returned.

Review uses `synchronizeView` and the captured rack instance or graph/node
identity's location read. Missing reconnect receipts use this same observation
path; known receipts continue through their existing exact-result review.
Failed, malformed, wrong-target, wrong-document or revision-raced reads retain
uncertainty. Successful observation stores an explicitly **unverified** outcome,
current location/candidates and captured submission fields. It does not invent
commit status, Undo history or plugin-state verification. All raw fields and
their baseline remain; explicit Reload is required before another operation.
Neither Review nor Reload rescans or reconnects. A removed target whose location
cannot be read remains unresolved rather than being mistaken for another plugin.

Native tests cover rack and graph targets for reconnect, path scan and installed
rescan; pending/uncertain departure guards; newer input; failed/malformed/identity-
mismatched/raced readback; hidden owners; retained submission; and explicit rebase.
The earlier scan-retention fixture now explicitly reviews and reloads before its
next reconnect, keeping its existing raw-field and generation assertions.

ARM64 Release evidence is pinned in
`bin/parity-evidence/p0b-plugin-path-readback-receipt.json`:

| Check | Result and scope |
|---|---|
| App/workspace/native-tool build | Passed; no shared engine, codec or Mac source changes |
| Full workspace, draft census, Application departure and native tool suites | 4/4 CTest entries, 103.08 s |
| Actual-app plugin paths | 6/6 cases, 14.770 s, no skips: missing-path scan review, stale/deleted targets, scan hashes/vendor-state/ports, graph recipe/history, native window behavior and real-effect recipe/Undo preservation |
| Additional cache-side-effect case | 1/1, 1.772 s: copied effect scan updates private cache, then fails the expected-class check at unchanged song revision; Review preserves changed cache, song, plugin state and raw path without repeating scan |

The provider fixture and two effect binaries/cache inputs match their retained
hashes. Tests use disposable projects/caches and private desktops; no physical
device or foreground accessibility qualification is claimed. The additional
case changed only test source after the main run; production inputs are unchanged.
**Remaining P0b:** unknown Keep/render/import result reconciliation, other scan/
cache entry-point audit, remaining stable-owner checks and cross-platform safety
qualification. P0c–P8 and reciprocal fixture/final integration gates remain open.

## P0b.1 Recorder lifecycle readback

`SampleRecordingWindow` now retains uncertain Record, Stop and Discard requests,
including method, captured document/revision, original take identity and submitted
parameters. Only an explicit typed `NotCommitted` refusal before the request
returns releases that intent immediately. A returned request followed by failed
native completion remains unresolved. The native census exposes pending versus
uncertain state, so a missing local take identity after failed Record cannot
permit document departure.

The native **Review current take** action performs only `sample.recording.get`.
It adopts the observed current state without claiming that the previous operation
succeeded: the old take may still be capturing, may be stopped, may be absent, or
may have been replaced by another API client. Review never repeats Record/Stop/
Discard or consumes the replacement take. Failed/malformed readback keeps the
unresolved request. Newer sample-name/output drafts remain unchanged. Subsequent
explicit actions use the observed take identity. While unresolved, unsafe writes,
setup discard and Close are refused; the UI explains that capture may still be
active. Review makes the observed Stop/Keep/Discard actions available again.

This is **take-state reconciliation**, not proof of a musical import. Keep's
result review remains separate, and an absent take does not prove that Keep
created a sample. Unknown Keep/render/import/reconnect results and scan/cache
reconciliation remain open P0b work. No shared musical operation, persistence
format, audio callback, capture device or Mac implementation changes here.

`RecordingLifecycleReviewTests.inc` uses real native HWNDs and deterministic
session responses. It covers failures after each lifecycle side effect, no
replay, departure refusal, newer setup, failed/malformed reads, explicit reopen,
active/stopped/absent take observations, an externally replaced take and typed
preflight refusal. The native tool suite passes (2.25 s). The actual-app
`test_sample_capture_ui.py` separately verifies setup retention, API departure
refusal, explicit discard and a fresh post-Open recorder without opening a
microphone. That case plus the nudge-departure regression pass (2/2, 1.038 s).
The full workspace, draft-census and Application-departure suites also pass
(3/3 CTest entries, 99.88 s) against the rebuilt Application fixture.
Source/toolchain/executable/log evidence is recorded in
`bin/parity-evidence/p0b-recording-lifecycle-receipt.json`. Native fault injection
does not establish hardware behavior, foreground accessibility or cross-platform
qualification; those gates and full P0b–P8 remain open.

## P0b.2 Application admission, retirement and refresh

`Application` now supplies `DocumentController`'s replacement-admission observer.
`DocumentDepartureIntegration.inc` coordinates final native draft review,
generation-bound consent, input protection, worker adoption, owner retirement and
forced refresh. Open and Recovery Restore use this path; Close and session-end
queries acquire the same final registry admission. API `discard:true` cannot
discard native raw drafts. Review and Cancel retain them; a changed generation
invalidates prior Discard consent. Pending or uncertain work refuses departure.

Main and its owned native trees are protected during the review prompt and final
admission. Queued shortcuts, raw control writes, API mutations, navigation,
deferred views and document-scoped timers cannot alter captured work while the
worker adopts and refreshes. Worker-to-main callbacks remain serviced. Report
count/selection/scroll/column updates now use explicit presentation setters;
owner-data reads and custom drawing remain available. Column width changes admit
only their matching stock header cascade, without exposing application callbacks
to a general recursive-write permission.

Failed admission/Stop releases the leases and keeps exact old owners and fields.
Successful adoption retires old HWNDs and C++ owners, clears captured workspace
targets, and keeps global recovery/library browsers and layout preferences.
Inspectors reopened afterward capture the new song. Discarded queued view intent
retains the existing “View target changed” explanation. Failed retirement/refresh
retains protection; document readback identifies the adopted model and context
marks `nativeRefreshPending`. F5 retries cleanup/refresh without repeating Open;
its key repeat/release cannot fall through to the normal transport shortcut.
Stop preserves the retry message. Canceled OS shutdown releases only its own
closing admission and retains drafts. Failed state inspection now fails closed.

ARM64 Release evidence is retained in
`bin/parity-evidence/p0b-application-departure-receipt.json`:

| Evidence | Scope/result |
|---|---|
| Integration and affected-target build logs | App, workspace fixture and 14 native control/editor targets built |
| `*-integration-targeted.log` | 2/2: Application departure and native input/report boundary, 7.50 s |
| `*-workspace-tests.log` | 6/6: full workspace, draft census, Arrangement, Matrix, MIDI recording and Recovery, 99.27 s |
| `*-affected-tests.log` | 9/9 remaining affected native control/editor targets, 26.08 s |
| `*-final-boundary-tests.log` | Final Application departure fixture passes, 5.45 s, after the queued-view feedback correction |
| `*-final-app-tests.log` | 27/27 actual-app cases, 37.183 s, no skips: API draft refusal/Cancel/Open, recovery, Close, queued views, layouts, shortcuts, text ownership and multisample retention |

The Application fixture exercises real owners/worker adoption, exact refusal
outcomes, late raw input, injected Stop/refresh failures, retry key lifetime,
canceled shutdown and the native recovery browser callback. Its modal choice and
fault boundaries are controlled; this is not foreground dialog or OS-shutdown
qualification. The pipe regression independently proves invalid nudge fields and
caret survive `document.open(discard:true)` refusal, then explicit editor Cancel
permits Open.

Retained failures explain the corrections: a const pointer cast and test enum
qualification caused compile failures; an initial TaskDialog import was not
available with the current common-controls configuration, so review uses the
existing native MessageBox convention; a blocked stock header cascade required
the narrow presentation permission; and one of the first 20 workflow cases found
the lost queued-view explanation. Assertions were retained. After that final
status-only correction, the Application fixture and expanded 27-case app run were
rerun. The earlier full workspace run remains evidence at its recorded source
snapshot, while unchanged native-control targets reuse their matching inputs.

**Still open:** P0b.1 unknown-without-receipt reconciliation (including reconnect,
scan/cache and recorder side effects); remaining owner/stable-target audits and
native modal/foreground qualification; Windows x64 and Mac shared-interface
gates; P0c–P8, reciprocal supplied fixtures and final integration. This checkpoint
activates Application departure protection; it does not complete P0b or parity.

## P0b.2 refresh setters and early nested owners

The Application admission audit found a concrete missing owner: `SampleLibraryWindow` creates `MultisampleImportWindow` before its own `finish`, which previously published the registry property. That early child could therefore remain outside the draft census. `NativeToolWindow` now propagates registry context during `WM_NCCREATE`, while summary registration still waits until controls are initialized. This changes no draft semantics or document state.

The global sample browser also handles post-adoption child retirement. It preserves its library search/preferences and callbacks, avoids reading a retired child's HWNDs in its snapshot, and creates a fresh import editor when the musician next reviews a family. Reopening captures the new song/revision and allocates a new registry owner identity. Pending and uncertain children still refuse departure through the existing registry policy.

`NestedDraftOwnerTests.inc` exercises the real browser/child constructors with a generated family and stubbed filesystem requests. It verifies configured/invalid hidden family intent enters the census, canceled admission preserves exact work, successful callback retirement destroys only the document child, and reopening uses a fresh owner/current song with clean fields. This is native-owner contract evidence, not an actual Application Open test.

379 explicit text, caret, combo and listbox setter calls across 46 native files now use `NativeInputGate::text/present`. A comparison against the previous commit verifies 44 files contain only argument-preserving setter substitutions; the other substantive/include changes are the helper, palette include, early registry propagation and sample browser lifecycle. No Windows API macro is redefined and no callback-wide permission is introduced. Additional gate tests prove selector/list rebuilds preserve item identity and selection while unsolicited raw changes remain blocked.

**Next integration requirements remain:** Application admission/retirement/refresh is not activated. Owner-data report controls still need explicit count/state/scroll refresh handling and a read-only notification policy (`LVN_GETDISPINFO`, custom drawing) before protection is enabled around their refresh. Audit stock report/header cascades without granting arbitrary recursive messages. Collect all owned roots, filter pre-dispatch shortcuts and API/deferred actions, retire C++ owner pointers, and provide a retained-lease refresh retry path. The preceding boundary tests are not proof of these host behaviors.

ARM64 Release qualification is recorded in `bin/parity-evidence/p0b-native-refresh-receipt.json`, with 1,839 frozen source/dependency inputs and compiler/cache/executable/log hashes:

| Evidence under `bin/parity-evidence/` | Scope/result |
|---|---|
| `p0b-native-refresh-boundary-rebuild.log` / `p0b-native-refresh-boundary-tests.log` | Native owner/input target built; focused boundary and nested-owner regression passed, 2.39 s |
| `p0b-native-refresh-build.log` | App, workspace and 13 affected native editor/control targets built |
| `p0b-native-refresh-native-tests.log` | 15/15 workspace/native UI entries passed; 122.40 s, including full workspace and separate draft census |
| `p0b-native-refresh-app-tests.log` | Seven actual-app cases passed; 4.313 s, no skips: configured multisample reopen, roots/stale/rebase, nudge/history/persistence, precise FX and retained workspace inspectors |

The first new fixture build omitted the thread-ID argument to `EnumThreadWindows`; the corrected fixture compiled and passed without a product change. The failed build log is retained. Tests used owned private desktops and disposable inspection documents, with no physical capture or system audio-default changes. This batch changes Windows native UI adapters only; shared musical behavior, persistence formats, Mac UI and audio/device code are unchanged. Final admission journeys, Windows x64/Mac qualification, reciprocal supplied fixtures, full P0b and P0c–P8 remain open.

## P0b.2 native input boundary

`NativeInputGate.hpp` adds a UI-thread RAII guard over explicit native root windows and their children. Install it after editor subclasses and before the final registry capture/admission. It intercepts input before stock controls or custom editor handlers receive it: queued/sent keyboard and mouse messages, focus-triggered edits, command/notification handlers, and text/selection/content setters for the application's standard edit, combo, button, listbox and report/header controls. Destroyed HWNDs unregister safely; failed construction removes only its own protection. Additional fully initialized trees require explicit `protect` before exposure or a message-pumping read.

Host refresh uses `NativeInputGate::present` for one exact presentation setter. Permission is consumed at the outer subclass before native processing; recursive messages and sibling writes cannot inherit it. The helper cannot authorize arbitrary commands, key input or callbacks. `NativeControls::text/select` now use that path, with their unchanged-value optimization intact. Direct `SetWindowTextW` and raw control messages remain blocked while protected. This is a native application coordination boundary, not a security boundary against arbitrary in-process code or direct model calls.

Real HWND tests in `NativeInputGateTests.inc` cover docked/floating roots, raw text and caret retention, queued characters, keyboard/focus/command handlers, combo/listbox/report/header state, one-shot recursive-send refusal, rollback after invalid roots, overlapping-lease rejection, new-tree registration, destruction during the lease, and resumed editing after release. An initial test incorrectly used the `SetWindowTextW` wrapper's boolean as proof of control acceptance; that wrapper returned success even when the subclass rejected its message. The corrected test checks exact raw text and handler counts, and separately checks the direct `WM_SETTEXT` return. Product behavior did not change for that correction; the failed log is retained.

**Integration remains open.** No Application departure path creates this guard yet. The next coherent batch must:

1. Collect Main plus all owned floating/native/global command surfaces, including nested owners, and install protection after their custom subclasses. Release typing/audition ownership and handle vendor editors/takes before final admission. Do not treat disabled parents as protection.
2. Filter queued shortcuts before `await` and the outer message loop call `handleKey`; guard API writes, audition, deferred actions and document-scoped timers before dispatch. Continue servicing worker-to-main callbacks to avoid deadlock. Native subclass tests alone do not prove these host entry points.
3. Migrate the remaining direct refresh setters (Main plugin/graph/effect/list controls and retained owners) to explicit presentation helpers; audit report notifications separately from paint/read requests. Never introduce a broad callback scope that permits arbitrary reentrant writes. No new editor subclass may be installed outside the guard after admission.
4. Acquire the guard before the registry's final re-read, pass the existing controller admission observer, and keep both leases through adoption, owner retirement, C++ owner-pointer cleanup and completed native refresh. Failed Stop/admission must preserve drafts and restore usable focus. Failed refresh must retain protection and provide a recoverable path; it must not silently reopen input against stale targets.
5. Exercise native/API Open, recovery, Close and bounded session end with actual Application owners. Native Review/Discard/Cancel must bind exact draft generations; API `discard:true` is not native-draft consent. New/Demo policy follows the planned command batch. Existing take guards remain authoritative and separately tested.

ARM64 Release qualification passed with 1,838 frozen source/dependency inputs:

| Evidence under `bin/parity-evidence/` | Scope/result |
|---|---|
| `p0b-native-input-gate-native-rebuild.log` | Native owner/input target built after the test correction |
| `p0b-native-input-gate-native-retest.log` | Native owner, result-retention and input-boundary regression passed; 2.03 s |
| `p0b-native-input-gate-app-build.log` | App and workspace targets built |
| `p0b-native-input-gate-workspace-tests.log` | Full workspace 85.37 s and separate draft census 9.04 s; 2/2 passed |
| `p0b-native-input-gate-app-tests.log` | Three actual-app cases: invalid/stale nudge, precise-note layout draft, retained inspectors/pin/Return; 1.535 s, no skips |

The tests used owned private desktops and disposable inspection documents. No physical capture or system audio-default change occurred. This Windows-only slice changes no shared model, project format, DSP or Mac UI. Its receipt is `bin/parity-evidence/p0b-native-input-gate-receipt.json`; results are bounded to its source/compiler/cache/executable/log hashes. Cross-platform P0b integration, reciprocal fixture saves and P0c–P8 remain outstanding.

## P0b.2 Main raw-owner retirement

`DocumentDrafts.inc` now supplies post-adoption cleanup for Main's seven registered owners: FX, nudge, sample range, Mixer, plugin parameters/programs, graph recipe and completed direct-render results. Cleanup clears captured document/target data, cached definitions, stale selections and gesture state without sending control messages or calling the worker. Pending/uncertain results remain non-discardable; completed cleanup runs once under the registry lease. Generations advance, while app-level unit/layout preferences and plugin discovery data remain. Application final admission is still not wired, so these callbacks are not yet complete Open/Close protection.

The nudge editor also advances its generation when opening a fresh draft. Previously cancel/reopen at the same document, cell and raw value could recreate an older generation. A real-HWND regression now proves prior discard consent is rejected after that sequence and after retirement/fresh initialization. The expanded census creates invalid raw drafts in every Main family, exercises rollback, verifies cleanup leaves native controls and song/history untouched, and reinitializes clean owners.

ARM64 Release app/workspace builds pass. Final evidence is pinned in `bin/parity-evidence/p0b-main-retirement-receipt.json` with 1,836 source hashes, native build inputs, compiler/cache/executable hashes and retained logs:

| Log | Result |
|---|---|
| `p0b-main-retirement-final-build.log` | App and workspace test target built |
| `p0b-main-retirement-census-final-tests.log` | Expanded raw-owner/retirement census passed; 8.98 s |
| `p0b-main-retirement-workspace-tests.log` | Full workspace regression passed; 85.27 s |
| `p0b-main-retirement-app-tests.log` | Four selected actual-app cases passed; one search-count assertion failed |
| `p0b-main-retirement-search-tests.log` | Corrected remaining case passed; 0.692 s, no skips |

Both intermediate failures remain in their original logs. The first new fixture retained an obsolete compact-tab selection after selecting another Main panel; it now supplies a valid layout without changing production validation. The existing FX test assumed “pitch slide” matched only one command, but the catalog descriptions also match legacy Portamento Up/Down. It now asserts all three exact display labels and explicitly selects native BL; all captured-target, stale-edit, Undo and save/reopen assertions remain. Only this Python test changed after the final native build; no rebuild was needed for it.

Next: wire the optional controller observer in Application, retire its C++ native-owner pointers safely, and gate document input/API writes before control mutation through adoption/retirement/refresh. Review/Discard/Cancel must cover exact captured owner generations; API Open must refuse unresolved native work without a modal prompt. Preserve both take guards and carry this policy through native Open, recovery, Close and bounded session end. Existing real-owner checks do not substitute for those end-to-end paths. Windows x64/Mac integration and P0c–P8 remain outstanding.

## P0b.2 post-adoption owner retirement foundation

The existing registry/native-window candidate now has bounded ARM64 qualification. `DocumentDraftRegistry::admitForReplacement` prepares cleanup and summary reads before acquiring its lease. Its move-only `AdmittedReplacement` retires document owners only after the caller observes adoption; dropping an unused admission leaves drafts intact. Clean document owners retire alongside dirty ones; global tools remain. Missing cleanup or failed summary reads refuse admission. Retirement skips nested owners already destroyed by their parent, retries only incomplete cleanup and retains its lease through native refresh.

`NativeToolWindow` registers no-throw HWND retirement, disables/hides/destroys the old window, unregisters on destruction and rejects reopening retired owners. The controller fixture observes the newly adopted document before retiring; Stop failure leaves drafts untouched. Real native-window checks cover dirty docked parents, nested children, hidden clean owners, rollback, no placement callback and global-tool survival.

Existing final-source build and execution logs were re-read; frozen source hashes matched. The additional separate draft census was run on the same executable without rebuilding. Evidence under `bin/parity-evidence/`:

| Log | Scope/result |
|---|---|
| `p0b-retirement-final-build.log` | ARM64 Release app, departure/controller and both native workspace targets |
| `p0b-retirement-final-tests.log` | 5/5: full workspace, native owner, controller scratch fixture/departure and portable departure; 88.18 s |
| `p0b-retirement-app-tests.log` | Two isolated actual-app layout/pin/focus retention cases; 1.363 s, no skips |
| `p0b-retirement-census-tests.log` | Separate raw-draft owner census 1/1; 6.16 s |

`p0b-retirement-native-receipt.json` pins source, compiler, cache, executables and logs. This does **not** activate final departure in Application: Main-owner retirement callbacks, C++ owner cleanup, the input/API lease and native/API/recovery/Close/session-end integration remain. No shared musical model, file format or DSP changed. Mac/x64 execution, actual departure journeys, reciprocal fixtures, foreground UIA and hardware remain open.

## P0b.1 plugin reconnect result retention

Continued from planning checkout `8d2997c2e`, completing the pre-existing seven-file reconnect candidate and adding worker/actual-pipe outcome assertions. This qualifies known-result retention, not unknown-result reconciliation or final departure protection.

- Rack and graph reconnect use the typed native write callback. A failed completion retains the submitted target, raw fields, generation and original worker result. Review refreshes the view without another reconnect or vendor probe. Later typing survives; an old result cannot relabel a newer document's saved path.
- Pending/uncertain reconnects appear in the native draft registry. Check, scan, reload and discard remain unavailable while the result is unresolved; hide/dock/float preserve it. Proven precommit refusal leaves the raw draft editable. A generic failure without a receipt remains unresolved and cannot trigger a blind retry.
- Path-set builds its reply before commit and classifies explicit preflight refusals, including missing targets, unsupported AU, module hash mismatch and rejected opaque vendor state. Scans mutate a separate cache and are not covered by this classification. Controller vendor-editor flushing occurs earlier; an unrelated flush revision change does not prove that reconnect succeeded. That boundary and unknown-without-receipt readback remain P0b.1 work.
- The actual application constructor and census fixtures now supply the typed callback. Native owner cases include rack/graph postcommit failure, failed Review, new document, later text, unknown outcomes and proven refusals. Actual-app tests retain exact recipes/opaque state, unrelated native/automation data, stable IDs, one-step history and save/reopen behavior.

One coherent ARM64 Release build produced the app, controller and both native workspace targets. `bin/parity-evidence/p0b-plugin-reconnect-receipt.json` freezes 1,836 source/dependency hashes, compiler/cache/executable hashes, provider caches and three module binaries; all matched after execution. Logs are retained separately:

| Evidence | Result |
|---|---|
| `p0b-plugin-reconnect-build.log` | All four requested targets built |
| `p0b-plugin-reconnect-native-tests.log` | 28/28: full workspace, native owner and all controller scenarios; 102.55 seconds |
| `p0b-plugin-reconnect-census-tests.log` | Raw-owner census group 1/1; 6.11 seconds |
| `p0b-plugin-reconnect-app-tests.log` | Five actual-pipe/native cases, no skips; 10.954 seconds |

Execution used owned private desktops, disposable songs and pinned provider/Contourtonist/OrbitCab modules. No musician or QA app remained at the final process check; no physical capture or system audio-default change occurred. This is Windows ARM64 evidence only. Windows x64/Mac P0b, full reciprocal fixtures, foreground accessibility and devices remain open. The next dependency is complete owner retirement plus final native/API/recovery/Close/session-end admission; no broad parity completion is claimed.

## P0b.1 native completion retention and direct render

Implementation continuation from planning commit `a2e14eccd`. This completes and qualifies the pre-existing known-result candidate and extends it to the one-click selection-render commands. It is a partial safety slice, not complete P0b.1, final departure admission or Windows parity.

- `CompletedCall` retains method/document/revision/result from the worker, before an interleaved later edit can change UI state. Controller postcommit publication errors and native completion failures preserve the original result; nested callbacks cannot replace it with a different receipt. The internal receipt is not serialized into the public API error.
- `NativeWriteCompletion` retains the submitted target, generation, raw fields and known result. Render/import/Keep owners expose Review after completion failure and do not repeat the write. Generic or unknown errors without a result remain retained. Typed NotCommitted/NoChange can release the submission; error numbers or unchanged song revision alone cannot prove rejection.
- `SampleRecordingWindow` now accepts the typed-write callback, resolving the earlier caller/constructor mismatch. Keep verifies its take identity, reads current take state without consuming a newer external take, preserves later name/output intent and avoids a stale sample reveal. Start/Stop/Discard have separate effects and are not declared reconciled by this change.
- The direct Sample/Instrument render actions in `SampleCaptureIntegration.inc` now use the same completion helper and enter `DocumentDrafts.inc` as pending/uncertain owners. Their command-palette and context-menu labels expose Review when needed. Switching to the other render command or Render options reviews the original result instead of starting another import. A newer document/revision prevents stale selection changes. The normal Render options owner also blocks a direct-command bypass while unresolved.
- `Workspace/NativeWriteCompletionTests.inc` exercises postcommit callback and preview-stop faults, mismatched identity, unknown outcomes, newer raw fields and externally replaced takes. `DocumentDraftCensusTests.inc` adds actual worker direct-render failure, registry review, failed read-only review, cross-command retry, exact result retention and one-step Undo/Redo. Actual-app cases cover both direct destinations and the retained options window with exact sample PCM after save/reopen.

Validation used dedicated ARM64 Release outputs and owned private desktops. `bin/parity-evidence/p0b-direct-result-receipt.json` records 1,836 matching frozen source/dependency hashes, seven executables, both CMake caches, compiler hashes and 22 retained logs. No musician app was present at preflight/final checks; no device capture or audio-default change occurred. The source changes are Windows adapters/owners/tests; no project version, shared DSP, Mac UI or device callback implementation changed.

| Final log under `bin/parity-evidence/` | Result and boundary |
|---|---|
| `p0b-direct-result-build.log`, `p0b-direct-result-api-build.log` | App, workspace, native tool, controller and sample-recording targets plus standalone API built successfully |
| `p0b-direct-result-census-tests.log`, `p0b-direct-result-census-detail.log` | Expanded real-owner census passed, 6.34 s; direct-render pending/uncertain retention, failed Review, cross-command retry, stale-selection protection and one Undo |
| `p0b-direct-result-native-tests.log`, `p0b-direct-result-native-detail.log` | 29/29 passed, 101.92 s: full workspace restore, native tool, 26 controller/fixture entries and sample recording |
| `p0b-direct-result-api-tests.log`, `p0b-direct-result-api-detail.log` | 2/2 passed, 4.43 s: actual protocol/outcome and bounded replay-cache cases |
| `p0b-direct-result-app-tests.log` | 8/8 passed, 5.302 s: direct Sample/Instrument and retained Render options, exact sample/PCM save/reopen, Undo/Redo, multisample import/rebase/retention, context and Activity/recorded points |

Earlier `p0b-native-result-*` logs are retained as intermediate evidence: native-tool behavioral cases initially reached a strict desktop-close failure, then passed after moving to the existing private-process harness; app setup initially lacked TMPDIR; an incorrect CTest filter correctly failed on zero tests. The final run above passed without relaxing assertions, cleanup checks or private-child deadlines. The planning receipt remains a historical read-only snapshot; it is not rewritten as implementation evidence.

**Remaining:** domain reconciliation/recovery when no authoritative result exists, uncertain Start/Stop/Discard and plugin repair, remaining owner audit, stable FX identity/unavailable nudge review, final Open/API/recovery/Close/session-end admission and its input lease. The new direct owner is visible to the registry, but Application still does not consult that registry for final departure. Windows x64/Mac gates, reciprocal supplied fixtures, foreground accessibility and physical devices remain open. Full P0c–P8 scope is unchanged.

## P0b.1 outcome boundaries — partial implementation

This continuation implements and qualifies the existing partial outcome candidate; it does **not** close P0b.1 or retained-work protection. The planning readback's hashes and “not executed” labels remain the earlier checkpoint, not the status of the code below.

- `editor/WriteOutcome.hpp` defines a portable value-only outcome. `ApiError` optionally carries it without changing public error numbers. Known worker postcommit publication failures report committed identity/revision; controller tests now assert typed data instead of searching message text.
- `SessionAdapter` preserves an outcome thrown by the write itself. Failures after the host returns (snapshot, envelope serialization or wire-size admission) become `unknown`, with no pre-write revision. A later callback's own typed refusal or foreign identity cannot be attributed to the original write. The native Application uses the same distinction and repairs presentation without replacing the original worker exception.
- The existing FIFO cache retains classified committed/unknown error responses as well as successes. Same-ID/same-content replay returns the retained response; changed content rejects. Bounds remain 64 entries/8 MiB, best effort and non-durable. Deliverable oversize-cache successes still return normally. Undeliverable write replies now become a small classified error before the pipe serialization boundary. Shared `ProtocolLimits.hpp` keeps adapter/transport byte limits aligned.
- [API guide](../../windows/Api/README.md#classified-write-outcomes) and [optional error-data schema](../../windows/Api/write-outcome.schema.json) document coverage and limits. The Python client already preserves error data and performs no send retries. No project format, history policy, audio/device path or Mac UI changed.

Qualification uses the dedicated Windows ARM64 Release app/controller/workspace directory and a separate small `bin/windows-parity-api` C++17 target. Actual named-pipe fault cases cover equal error codes/messages with different commit outcomes, authoritative worker identity, snapshot failures (including a nested typed refusal), invalid UTF-8, the 32 MiB reply bound, exact replay and side effects that leave the song revision unchanged. Domain readback must show exactly one effect. Native Application fault injection runs after the real worker edit and checks the retained new cell, readback, one Undo, nested-callback attribution and ordinary stale refusal.

Final qualification results and exact source/binary/log identities are recorded in `bin/parity-evidence/p0b-outcome-receipt.json`: 1,834 source/dependency hashes, both CMake caches, five executables and retained logs. All frozen inputs matched after the final checks.

| Final evidence | Result |
|---|---|
| `p0b-outcome-api-final-build.log`, `p0b-outcome-boundary-build.log` | Standalone API, controller, workspace and app built successfully |
| `p0b-outcome-api-final-tests.log` | 2/2 API targets, 4.67 s; detail log retains individual protocol/cache assertions |
| `p0b-outcome-controller-final-tests.log` | Full 26-entry controller inventory, 14.78 s, including scratch/fixture setup |
| `p0b-outcome-workspace-final-tests.log` | Full workspace restore and draft census, 2/2, 89.38 s; detail log includes actual worker/UI completion and one-Undo assertions |
| `p0b-outcome-app-final-tests.log` | Five actual-app/private-pipe scenarios, 2.559 s, no skips: context, Activity, recorded automation/history/save, sample import/history/persistence and independently revisioned library replay |

Intermediate evidence is retained: the first workspace compile found a test-local `Json` alias missing; the correction changed only test compilation. The first workspace suite and five app cases passed; later review tightened callback attribution in both adapters, requiring the final rebuild/recheck above. No assertions, private-child timeout or error checks were weakened. Qualification used owned private desktops and disposable documents; no system audio defaults or musician sessions were changed. These checks do not establish physical audio or foreground presentation.

**Next required work:** native owners must retain submitted generation/target/result and expose domain-specific reconciliation before uncertain imports, Keep, repair or render can be retried. Cached receipts alone do not prevent replay after eviction/nonretention/restart, and an unchanged song token cannot establish filesystem/catalogue/take outcome. Complete direct-render registration and every owner census; wire final replacement/Close/recovery/session-end admission. Mac and Windows x64 gates, reciprocal fixtures, foreground/accessibility and device qualification remain open. Do not merge this partial checkpoint as completed P0b.1 or mark the full parity goal achieved.

## P0b plugin repair, configured import and recorder setup

This continuation completes the three owner omissions identified by the latest review. It is branch work; it does not close F21 or qualify the final departure path.

- `PluginPathWindow` registers raw manual-path/candidate intent and pending reads, verification, scans, chooser and reconnect requests. Late fields and selections retain their captured revision; accepted-path baselines distinguish returning to an old path from accepting the submitted reconnect. Manual paths not submitted by Reconnect remain dirty. Explicit Discard releases local intent. `Application::openPluginPath` preserves hidden dirty/pending owners and distinguishes the document when reusing a clean owner.
- `MultisampleImportWindow::open` treats a configured family as retained intent immediately. Hiding is presentation, not a new draft generation. Check/Import keep pending ownership through completion callbacks; late raw text invalidates reviewed roots and survives successful import. Reopen raises the existing family; explicit Use current song plus Check is required for stale import. Discard rechecks generation after stopping preview.
- `SampleRecordingWindow` registers document-scoped name/output intent separately from the session's retained take. Idle endpoint/channel preferences do not create song drafts. Pending Start/Keep/Discard/read requests are protected, and attempted endpoint retargeting during a request restores the captured selection. Keep clears only its submitted generation; later name/output changes survive. Discard setup resets local fields without discarding a take; Discard take does not consume unrelated setup. Old-song setup cannot silently start/keep into a replacement song.

New real-HWND cases in `Workspace/DraftImportRetentionTests.inc` exercise pending census/refused discard, raw text and selector callbacks, Check versus commit, rejected writes, stale retry, late callback input, hidden/reopened owners and explicit discard. `DocumentDraftCensusTests.inc` additionally exercises the actual Application's hidden path-owner reuse and recorder registration. The small target links native `comdlg32` for the existing file chooser; production libraries and Mac/shared musical code are unchanged.

Qualification used the dedicated ARM64 Release `bin/windows-parity-p0` build and owned private desktops. No musician app was running at the preflight or final process check; no audio defaults changed. Logs under `bin/parity-evidence/`:

| Evidence | Result and boundary |
|---|---|
| `p0b-import-before-build.log`, `p0b-import-before-test.log` | New check reproduced omitted configured family intent before the repair |
| `p0b-owner-completion-tests.log` | Retained first post-edit failure: late plugin choice returning to the old baseline was misclassified clean; fixed without weakening the assertion |
| `p0b-owner-completion-final-build.log`, `p0b-owner-completion-final-tests.log` | Native tool target passed, 1.11 s |
| `p0b-owner-app-build.log` | App and workspace harness built successfully once the native owner changes were ready |
| `p0b-owner-integration-tests.log` | 4/4 CTests: full workspace restore 83.60 s, expanded draft census 4.01 s, native tools 1.05 s, sample-recording lifecycle 0.06 s; private child bound unchanged |
| `p0b-owner-app-tests.log` | Five sample-library scenarios and native rack reconnect passed. The graph-path scenario exposed an outdated foreign-path fixture setup; its failure remains retained |
| `p0b-owner-graph-path-tests.log` | File-based foreign-path setup passed backend state/history/persistence checks; native entry then exposed a stale button/deferred-load test sequence |
| `p0b-owner-path-final-tests.log` | Graph and rack native reconnect cases both passed, 7.531 s, after fixture/input readiness corrections; no product validation was relaxed |

The corrected graph-path case now asserts `graph.update` rejects a foreign Windows module path without mutation, then creates the imported project through a saved property tree. It still asserts recipe/rack/opaque-state preservation, Undo/Redo, save/reopen and the actual native graph target. Native commands wait for deferred view work, select the stable fixture node and verify selection before opening repair. The sample-library addition verifies a hidden family survives an external document edit before any field has been typed. Seven distinct application scenarios passed across the retained runs, with no skipped cases. Existing third-party/native provider fixtures were reused from the explicit test caches, not installed or modified globally.

`bin/parity-evidence/p0b-owner-completion-receipt.json` records source/dependency, executable, fixture/cache and log hashes. This is Windows ARM64 functional/native ownership evidence; it does not establish foreground accessibility, physical capture, x64, Mac P0b or reciprocal saves. No shared code changed in this slice, so the unchanged Mac baseline is reused only within its original P0a scope.

**Next:** introduce typed commit outcomes at the actual controller commit boundary and reconcile uncertain native writes; finish the remaining owner audit, including direct `SampleCaptureIntegration.inc::renderPatternSample` pending intent (not currently registered in `DocumentDrafts.inc`), Main FX identity and unavailable nudge review. Then wire Application final replacement/close/recovery admission with exact consent and the input/API lease. Existing numeric `-32003` errors cannot alone distinguish rejection from committed-but-unreported work. Do not treat these selected owner tests as complete departure protection.

## P0a integrated on shared main

[PR 3](https://github.com/rewbs/ScreamSeq/pull/3) merged as `16cab100b898f66726cfab6c2386887a9bcf5fd4` after all three candidate jobs passed. The actual tested PR merge was `6433dc0da9c1b779b1390de7a495af2c2353fd9e`; a local tree comparison found no content difference from the final main merge. Head was guarded at `6c3eea908fd3167ada681b2c44be551cb7eb94a3` during merge.

- Windows x64 [run 37923921386](https://github.com/rewbs/ScreamSeq/actions/runs/37923921386), job 113798216568: app build, 46 portable entries, 62 worker entries, three scratch app cases, three retained-editor CTests and five merged app scenarios passed.
- Mac [run 37923921268](https://github.com/rewbs/ScreamSeq/actions/runs/37923921268): Apple Silicon job 113798215876 and Intel job 113798216235 passed native builds, 120 CTests each and Swift recovery/picker checks. Their actual checkout lines identify `6433dc0`; CTest elapsed times were 118.25 and 228.86 seconds respectively.

These gates qualify the P0a integration, not P0b, reciprocal fixture compatibility, physical devices or full foreground/accessibility behavior. No duplicate workflow was dispatched. Source main now differs from the planning reference baseline; the plan's main-pinned findings remain historical until individually superseded by implementation evidence here.

## P0b native draft census and completion retention

The native registry now follows retained HWND/Main owner lifetimes, including hidden/reparented windows. Raw draft summaries include captured document, revision, target and generation; composite arrangement details retain independent identity/generation tuples. Formula children inherit their original parent identity; reference-only Formula search is excluded. Main owners register separately from `NativeToolWindow`. Song Timing includes the captured sequence; nudge Review reveals the captured cell without reloading or applying its raw strings.

Graph Trim and Pattern Selection Render requests expose pending state and retain newer input on completion. A new real-HWND regression reproduced loss of a trim link choice while Apply pumped messages. The repair increments generation for later selector edits and restores the displayed owner/port selection when an in-flight request prevents retargeting. Trim text, read refusal, stale retries and pending discard are covered; render Check/refusal/commit cases distinguish submitted fields from newer raw input. Those bounded callbacks qualify native completion ownership, not audio rendering.

Focused qualification uses `bin/windows-parity-p0` (ARM64 Release):

- `p0b-request-retention-before.log` retains the reproduced selector-loss failure; `p0b-request-retention-after.log` passes after the fix.
- Final app/workspace build: `p0b-census-final-build.log`.
- `p0b-census-final-tests.log`: workspace draft census, native-tool-window and pure departure targets pass, 2.49 seconds total.
- `p0b-census-final-app.log`: all 17 song-tools/graph-curve-host/precise-note-host app cases pass, 28.388 seconds, with the explicit arrangement fixture and silent-output prerequisites.
- `p0b-census-final-restore.log`: final retained workspace regression passed in 82.82 seconds, within the unchanged private-child bound.

`bin/parity-evidence/p0b-census-receipt.json` records the 1,830 source/dependency hashes, build configuration, executables and logs. Earlier census attempts remain retained; a missing arrangement-fixture setup failure and explicit silent-output skip are not relabelled as passes. The two new census cases use a separate invocation of the existing workspace executable, avoiding a second native-app translation unit and retaining the private child's 90-second bound.

**This is a foundation slice, not F21 closure.** `Application` still omits `DocumentReplacementAdmission`, and `protectUnsaved`/`canClose` do not authorize against the registry. Remaining P0b work includes complete owner classification (plugin path repair, recorder configuration, configured import intent, uncertain write outcomes), stable or sufficiently guarded Main FX identity, unavailable nudge-target review, and the final native input/API/adoption/retirement lease. Native Open/Close/API replacement/recovery and shutdown must then pass actual application checks. Current P0a Mac evidence does not qualify the changed departure header.

## P0b departure protocol foundation

The latest review was committed separately as `75ca12e30`. Implementation then added the shared value-token rules in `editor/DocumentDeparture.hpp`, UI-thread owner registration in `windows/App/DocumentDraftRegistry.hpp`, and a final replacement-admission observer in `DocumentController`. **The application does not yet pass an observer or register its native editors. This foundation is not complete raw-draft protection and does not close F21.** No product version, musical data, API schema, DSP or native device behavior changed.

- A token captures document/revision, owner incarnation, stable target, captured revision, raw-draft generation and dirty/pending/uncertain state. Presentation labels, clean navigation and owner iteration order are excluded. Pending or uncertain writes cannot be authorized by Discard. New work, owner recreation, failed owner reads, cancellation and a newer review invalidate old consent.
- The registry retains hidden owners, supports nested owners and raises an existing owner for Review. Registration handles follow native lifetimes; summary callbacks are UI-thread-only, read-only and cannot pump messages. A failed summary fails admission closed. The registry deliberately does not inspect arbitrary HWND text or classify every window as dirty.
- `DocumentController::installCandidate` now accepts an optional native observer shared by Open and recovery. Candidate parsing, cache construction and the recovery fingerprint check precede admission. Admission and Stop run in the same UI callback; the short lease spans worker adoption and view publication. Stop refusal calls `finish(false)` without changing the original view. Successful publication calls `finish(true)`. Initial construction has no departing document and does not invoke the observer. Existing callers remain compatible.

Local Release ARM64 evidence, built into the existing dedicated `bin/windows-parity-p0` directory with no running musician process found:

| Check | Result / log under `bin/parity-evidence/` |
|---|---|
| Token/registry target | Passed; `p0b-departure-tests.log` |
| New real-worker admission plus publication/recovery/recording regressions | 7 CTest entries passed including scratch/fixture setup, 1.55 s; `p0b-departure-controller-tests.log` |
| Full controller regression after the worker change | 26 CTest entries passed including setup, 14.71 s; `p0b-departure-controller-regression.log` |
| Final cancellation/failed-summary consent tightening | Both departure targets rebuilt; 3 focused entries passed including scratch setup, 0.46 s; `p0b-departure-final-build.log`, `p0b-departure-final-tests.log`. Worker implementation is unchanged from the broader regression; the new registry guard and cancellation assertions have this final focused result |

`p0b-departure-receipt.json` records 1,827 source/dependency hashes, CMake cache and log hashes. Final executables: `document-departure-tests.exe` SHA-256 `efcae9f727b3f8d73a0d3f53297b1c354bab8bbcf6a1adef5c31cdc6b5c326f6`; `document-controller-tests.exe` SHA-256 `06c6c32ddb7e33cb6dee23067d4fd2f416ec90e064f8f25148b0bc67dab2dca7`. No app rebuild or foreground/device test was needed for this foundation. The portable departure target is registered in both build definitions; Mac execution of this new source is still required at the cohesive P0b gate.

**Next implementation work:** register every owner family in the handoff census with exact raw-generation summaries, including nested formula/bank drafts and hidden Main-bottom fields. Add the native Review/Discard/Cancel departure flow and structured API refusal. Pass the observer from Application; keep document-scoped input deferred during admission while continuing worker service. On successful adoption, retain that lease until UI refresh/old-owner retirement completes; on failure or cancelled chooser, release without erasing drafts. Apply the same token policy to Close, bounded session end and recovery. Recheck both take types at final admission. Then build the app/workspace target once for the cohesive native integration and run the full owner census plus actual app Open/Close/recovery race checks. Tests of a fake registry owner do not substitute for those native registrations or raw-text retention checks.

P0a CI remains independent at `8afd4175a`: Apple Silicon job `113780223733` in run `37918435661` was observed successful; Windows job `113780223549` and Intel job `113780224030` were still live at the latest poll. No duplicate runs were started. Their eventual results qualify that isolated P0a source, not this P0b foundation. The previous planning turn made review progress; this resumed goal turn adds implementation and targeted execution evidence.

## P0a retained-editor qualification repairs

The existing x64 job at PR merge `29149338` passed build, portable/worker and scratch checks, then failed workspace and graph-curve native checks. Local diagnostics established two distinct issues:

- At 96 DPI, the Graph Curve shape combo occupied `[8,52,154,80]`, while the axes began at y=79. The real one-pixel overlap is repaired by measuring closed combo bounds before placing the axes. Requested dropdown heights cannot stand in for closed HWND geometry. Existing 100–120-DIP short canvas and exact 272-DIP floating canvas assertions remain intact. The native suite now exercises 96 DPI as well as the developer display's 192 DPI through a fixture-local thread context.
- `SetWindowPos` obeyed monitor-derived maximum tracking bounds. Even the local requested 1440×900-DIP client was actually 2880×1759 pixels at 192 DPI, rather than 2880×1800. The private restore fixture now temporarily establishes its exact requested frame maximum and asserts the resulting client size. It additionally reproduces a capped-small-desktop case and requires compact reflow. Production maximum/minimum sizing, focus, raw-field and independent-region assertions are unchanged.

The five previously unreached Python app cases exposed two fixture issues. A wheel message carried screen `(0,0)` and was correctly ignored outside the tracker; the test now verifies that rejection, then sends real tracker screen coordinates and retains both scroll-direction assertions. A graph read raced the curve's coalesced preview; the test now uses the existing bounded quiet/readiness observation before the read, without retrying any edit or weakening draft/target assertions.

Source commits on the safety branch: `13a5be2ab` (layout/native fixtures), `758cd1559` (app fixtures). Corresponding isolated P0a commits: `25a97241c`, `8afd4175a`, based on `4a5c8ed88`; later P0b cable/role changes are not included in that branch.

Local ARM64 Release evidence, no physical device or foreground claim:

| Check | Result / retained log under `bin/parity-evidence/` |
|---|---|
| 96-DPI graph failure before repair | Reproduced; `p0-layout-graph-before.log` includes exact rectangles |
| Original workspace geometry | Pass at larger local work area, with clipped client height recorded; `p0-layout-workspace-before.log` |
| Focused native/app rebuild | Passed; `p0-layout-build.log` |
| Graph Curve native suite, display DPI plus 96 DPI | Passed, 2.72 s; `p0-layout-graph-after.log` |
| Full workspace restore and Precise Notes native suites | Both passed, 83.33 s + 5.60 s; `p0-layout-retained-after.log` |
| Five app integration cases | Three passed initially; two diagnosed failures retained in `p0-layout-integration.log`. Both corrected cases passed, 4.799 s, in `p0-layout-integration-corrected.log`; unchanged successful cases were not rerun |

`p0-layout-receipt.json` records safety-branch source `758cd1559bb3e9be13093e1263476708e49d6081`, 1,823 tracked source/dependency input hashes, CMake cache and log hashes. Executable SHA-256: app `2e43cc3305c1239cb79d9a54e2afb3da509402c3987a84531ca48a8916ac18f2`; workspace `1b9f3dc5621e7b6c7989022472b3cad35761ffd96d9517990e8b67abd161ac10`; graph curve `381f04441e54dca99e673194b823d505d1701af97a39b2914aa0560b2caf4122`; precise notes `ecbad7f684264af8d31a79b7ca0b07762a9b1ec890ddc9389e198444296a9da0`.

Both jobs in existing Mac run `37913656837` completed successfully before the new push: app build, 120 CTests, Swift recovery and picker checks. Intel's native tests took 240.22 seconds. Their checkout remains PR merge `29149338` of `4a5c8ed88` into `ffe81aa4b`. The isolated P0a head `8afd4175a82eba3dc0331e6951b55e3ea891ae72` was then pushed to the existing draft PR 3, avoiding cancellation of that live evidence. New exact-candidate CI is still required; local safety-branch execution is not substituted for it. In particular, actual-app tests requesting large windows may expose the same runner geometry limit separately from the now-fixed in-process fixture. Do not skip their assertions if that occurs. No source migration, broad UI redesign or aggregate raw-draft registry is included in this correction.

New runs confirmed live for `8afd4175a`: [Windows 37918435648](https://github.com/rewbs/ScreamSeq/actions/runs/37918435648), [Mac 37918435661](https://github.com/rewbs/ScreamSeq/actions/runs/37918435661). Inspect those handles rather than starting duplicate runs. The managed `parity-ci-layout` worktree remains at the isolated PR head for any demonstrated follow-up; the main task checkout keeps the broader safety candidate and complete reviewed plan.

## Resumed P0a: exposed Windows worker failures

The original CI runs are terminal failures: both Mac jobs reached the already-repaired Swift expression failure, while Windows x64 compiled and passed 60 of 62 worker checks. The two failures were reproduced in the isolated local ARM64 build before editing:

- `document-controller-live-graph-publication`: a full prepared graph-control queue escaped as `std::runtime_error`, yielding the generic engine error instead of guarded `-32002`. The native preparation boundary now translates host preparation exceptions consistently with rack preparation. It does not alter queue budgets, publication ordering, audio, history or accepted model state. Existing full-queue, bypass, Undo/Redo and persistence assertions remain intact.
- `document-controller-live-native`: the test still expected a valid processor addition to be unsupported. The current shared implementation supports prepared live recipe topology. The replacement case explicitly refuses publication and asserts unchanged exact graph/view/history/transport, then accepts the same operation and verifies rendered transition, one Undo and one Redo with exact graph/stable-identity restoration. It does not remove rejection coverage.

After the repair, all four focused publication/native cases passed (plus their automatic scratch-directory setup), then all 25 `document-controller-*` CTest entries passed in 14.61 seconds. This covers worker/model behavior with the real prepared render chain, not a physical device or foreground UI. Only `document-controller-tests` was rebuilt; no musician process was present and no audio defaults were changed. One intermediate test compilation exposed a mixed-type `auto` declaration; it was corrected before the successful rebuild.

Evidence: `bin/parity-evidence/p0-exposed-worker-before.log`, `p0-exposed-worker-build.log`, `p0-exposed-worker-after.log`, `p0-controller-regression.log`. Current ARM64 Release `document-controller-tests.exe` SHA-256: `9efb29082d42cccc9566bf66bed1c1a5bd2d9fa178f112efe1f0bc142b42cb59`. The older hash below remains the earlier receipt. Both-platform CI must still qualify the final candidate before P0a integration.

## P0a candidate

Base: main `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. Initial implementation commit: `605c7c688e8fb5e3876b1b086ea6b4f1a1e17309`. [Draft PR 3](https://github.com/rewbs/ScreamSeq/pull/3) holds this batch.

- Windows has separate MIDI/sample retained-take checks and one aggregate guard. A second MIDI check covers message pumping during the sample read; close-error fallback also protects current MIDI state. UI guards do not silently rewrite the server's separate save/open contracts.
- The Mac recorder uses an explicitly stopped/joined monitor compatible with the supported libc++ instead of unavailable `jthread`/`stop_token`. Restart resets the stop request; disposal follows join; capture-limit/disconnection still stops the AudioUnit without polling from the UI.
- Native regression cases cover both retained take types, failure and reentrant MIDI creation, plus monitor restart, explicit/autonomous stop, join and destruction.

### Failures exposed after restoring compilation

1. **Workspace regression expectation:** `workspace-restore-tests` passed its new take guards, then expected raw `.375/.75` in a typed percent field. `NativePatternCommands.cpp` declares strength display units as percent, and production `PatternFields::text` presents `37.5/75`. The assertion now checks that display and additionally requires exact raw strength, offset and duration after reading the fields. No tolerance or behavior assertion was removed. Full workspace suite passed after this correction.
2. **Mac Swift compiler limit:** [Apple Silicon job 113750668968](https://github.com/rewbs/ScreamSeq/actions/runs/37909410789/job/113750668968) passed the recorder C++ compilation but failed at `WorkspaceIntegration.swift:110`, reporting “the compiler is unable to type-check this expression in reasonable time”. The additional-menu collection is now explicit typed accumulation in the same order, with the same visibility/content checks. This repair requires a new native CI build; it has no local Mac execution claim.

### Local Windows evidence

Dedicated directory: `bin/windows-parity-p0`, Release ARM64, Visual Studio 2022 / MSVC `19.44.35229.0`. The app, workspace, document-controller and sample-recording targets built successfully. Only the workspace target was rebuilt after its assertion correction. No system audio defaults or musician process were changed. Private-desktop lifecycle checks explicitly verified foreground and clipboard preservation.

| Check | Result | Retained local log |
|---|---|---|
| App/API recording and native MIDI review | 6 passed; initial 4 audio-dependent cases skipped | `bin/parity-evidence/p0-recording-app.log` |
| Those four cases with explicit silent output | 4 passed, no skips: timestamped chord/commit/history/reopen, stop draining, queue-loss retention, autosave/recovery | `bin/parity-evidence/p0-recording-silent.log` |
| Full native workspace suite after percent expectation correction | Passed in 80.75 seconds, including all new take-protection cases and existing retained-owner checks | `bin/parity-evidence/p0-workspace-native-pass.log` |
| Worker recovery, worker recording, sample recording | 3 tests passed, plus automatic scratch-directory fixture setup | `bin/parity-evidence/p0-worker-recording-pass.log` |
| Initial target build / workspace rebuild | Exit 0 / exit 0 | `bin/parity-evidence/p0-build.log`, `p0-workspace-rebuild.log` |

The initial CTest invocation used a relative output-log destination that was not retained where intended. The successful workspace `LastTest.log` was copied before another CTest run could overwrite it; the three cheap worker checks were repeated once with an absolute log destination to retain their actual output. The first failing workspace child log remains in the private fixture's reported temporary directory. These are qualified local results, not a whole-suite or hardware pass.

Executable SHA-256 identities:

| Binary | SHA-256 |
|---|---|
| `ScreamSeq.exe` | `78408717255f0c62f4077b9223c5a6a29af73f6f5da70d4074f2e203865ee225` |
| `workspace-restore-tests.exe` after assertion correction | `9769ea210547c959098f89a24da7f49864954ff6b0b9af9ffa5eb85823e2a838` |
| `document-controller-tests.exe` | `3f53c85fe446fab56530ef0ca0266e62dcba4ec196ad1af2dffdd99f51406fbc` |
| `sample-recording-tests.exe` | `43393dae07affa28847ee91e6ea00aaf5d52db0804ad37787d94607f35916e6e` |

### Remaining P0a gates

Windows x64 and both Mac CI workflows must complete successfully at the final candidate, including tests previously masked by compile failures. Any further failures need diagnosis and repair. Controlled recording tests do not establish physical device/disconnection behavior. The monitor's actual Mac lifecycle tests still need their native execution result. P0a is not merged or complete on the evidence above.

## Next batches

P0b covers retained draft protection and role/field-safe graph edits. P0c establishes typed fixture/API conformance, resolves the Mac interface harness failures and records accurate scheduled test coverage. The shared/native architecture, acceptance recipes, effort estimates and P1–P8 scope remain exactly those in the plan. New evidence updates this receipt; it must not silently convert an unexecuted matrix row to Pass.

### P0b cable safety slice in progress

Temporary branch `codex/windows-parity-safety` starts at P0a candidate `4a5c8ed881a3168b7804ed6ddad8a62ed771c757`. P0a CI runs `37913656837` (Mac) and `37913656606` (Windows) were confirmed live after the push; the safety branch does not invalidate those inputs. It must converge on main after the prerequisite qualifies.

`GraphCableEdits.hpp` now creates zero-depth modulation wires with an enabled peer's base/mode, or a catalogue-derived normalized manual base with explicit discrete mode. An all-disabled peer set does not impose a stale base on a new enabled cable. Same-target rewiring preserves a disabled cable's own settings. `GraphEditor.inc` copies both drag endpoints before reading the worker catalogue and rechecks captured draft generation, graph and document revision afterward. The new-wire form defaults to zero depth and a blank automatic Base; a typed base remains explicit. Selected-wire edits copy the original JSON before replacing displayed fields, preserving quantized/enabled and untouched data. Normal and staged first-open defaults agree. No project version, API schema, shared DSP or Mac source changed in this slice.

The graph fan-connection model test passes, including new baseline/discrete/disabled-peer cases and existing dry-run, no-op, Undo/Redo, metadata and unrelated-edge cases. Four actual-app private-desktop tests pass in 6.761 seconds: socket creation and neighbor preservation, new-wire form defaults, disabled quantized field edits with history/save/reopen, and audio fan-out/rewiring. These are native HWND/pipe checks, not foreground visual or hardware audio evidence. All 27 graph/workspace CTest entries then passed in 81.53 seconds (26 graph cases plus the full workspace suite). Four additional native graph-editor cases passed in 5.375 seconds, covering parameter transaction/history, cancelled/stale drafts and keyboard history, assignments, compact bounds and invalid-cycle refusal. Draft-registry and stage-role work remain outstanding; P0b is not complete.

Logs are `bin/parity-evidence/p0b-cables-build.log`, `p0b-cables-model.log`, `p0b-cables-ui.log`, `p0b-cables-neighbor-ui.log` and `p0b-cables-regression.log`. ARM64 Release hashes for this slice: app `953ad5f8bcd0de49a53997c322d55f3d1b194df83f9e26d7106bf5be5dbea9ff`; graph-document tests `7affa4d37119bf1515570841cd92de7cd8c200bb54239195d6798c2993b7ad6e`; workspace-restore tests `ea045d9be717f62477dac231928ca907861266fd6bd4fc87d9a5485191d856f0`. Native catalogue-read generation checks still need a deliberately pumped replacement/draft regression in the upcoming owner-safety batch.

### P0b command-stage protection

`SongRoutingCanvas::Node` now carries an explicit Row/Persistent/Ordinary/Instrument role and assignment/insert capabilities through projection and the workspace snapshot. Row/Persistent inspection displays the selected stage's recipe, with no misleading Ordinary amount/wet values. Both HWND enablement and the assignment/insert handlers reject attempts to change the Ordinary assignment or regular insert chain from those cards. The existing bus, Ordinary and sample-instrument assignment paths remain available. Persisted display/layout IDs are unchanged. Aggregate auxiliary routing and individual-copy observation remain separate existing contracts; this slice does not add exact-copy audio routes.

Four actual-app private-desktop cases passed in 19.691 seconds: role-specific inspection, all six assignment/insert mutation commands dispatched past disabled HWNDs without document/history changes, shared-recipe navigation, Ordinary clear/Undo, stage overview/filtering, instrument assignment/expansion, and insert-order/layout history/save/reopen. The full workspace regression also passed in 83.26 seconds. Its added catalogue case deliberately pumps a newer raw field edit during the real worker read and proves the stale completion refuses while retaining exact text, graph and song/history. This closes the raw-draft race check noted above; document replacement during the read still belongs to the remaining aggregate-owner admission work.

Build/test logs: `bin/parity-evidence/p0b-stage-build.log`, `p0b-stage-ui.log`, `p0b-stage-workspace.log`; detailed child assertions are retained in `p0b-stage-workspace-detail.log`. ARM64 Release app SHA-256 `6f449e3ab5f30360d2399108c8886f8f1ea74f7130088d26d14060e63970cf55`; workspace test SHA-256 `b51ad1974e22c8cd88dc826173fadf7acce573fce690946bed8a6bc41578538e`. Shared model/audio and Mac sources are unchanged; these results do not establish foreground rendering, physical device qualification, supplied-fixture reciprocal parity or completion of P0b. The P0a CI jobs were re-polled and remained live; they were not restarted. Next: aggregate raw-draft protection across native Close/Open, API replacement and recovery, with bounded shutdown admission and deferred discard.
