# P3 implementation checkpoint

This is source status, not runtime acceptance. P0–P2 qualification remains open;
P3 does not supersede those gates. The temporary `codex/windows-parity-workspace`
branch starts at P2 `a4a281f2f` and includes both native platform sources. The
implementation sequence still converges on shared main. P3a catalogue/UI,
remaining P3b transport/pattern tools and P3c file/song work are not complete.

## Live playback loop — prepared, unqualified

Windows already supports guarded `transport.play` regions in SessionAdapter and
DocumentController, and the shared Renderer already supplies an atomic `loop`
setter. Mac's `transport.loop` uses that setter without re-preparing its renderer.
This batch connects the corresponding Windows path rather than adding a second
transport model or a new device lifecycle.

`windows/Api/SessionAdapter.hpp` adds an opt-in SessionHost capability and setter.
Only capable hosts advertise/accept `transport.loop`. It requires expectedRevision
and an actual boolean enabled, rejects unknown fields, uses the normal write
replay cache and returns `{loop: enabled}`. Default standalone hosts remain
unsupported; their old catalogue and validation contracts remain unchanged.

`Application::setPlaybackLoop` refuses busy/replacement states before mutation.
For a running song it updates the existing renderer's atomic value, without
restarting WASAPI, flushing plugin edits, changing the playback epoch, replacing
the captured region, touching recording ownership or editing the document.
While stopped or independently auditioning, it changes the next song-play default.
An explicit loop setting passed to Play still overrides that default. Following
the Mac contract, transport.get.loop is current while region retains the original
play request. No native project metadata or preference schema is added.

Native command 586 is available from the palette and checked pattern context menu;
a compact on/off button sits below the existing typing selectors in the sidebar.
The native label carries state in text as well as the active appearance. Shortcut
activation preserves an existing native editor's focus/draft and uses the current
shortcut customization mechanism. No new default key overrides existing tracker,
text, formula or user bindings. Foreground DPI, tab order and accessibility still
require direct qualification as part of P3a.

## Prepared checks and bounded next gate

- `ApiTests.cpp`: capable/unsupported catalogues; absent/stale revision; strict
  boolean/unknown fields; busy atomic refusal; stopped/live semantics; unchanged
  context/region/history; and exact request replay without repeating the host call.
- `workspace-transport-control-tests`: actual Application/worker/native controls;
  retained invalid track draft and focus, stable document/transport generation,
  busy and departure-input refusal, and recording-take ownership. Registered in
  the existing workspace executable and its native-ui CI selection.
- `test_transport_loop.TransportLoopTests`: actual PID pipe, guards and native
  button plus a private, silent WASAPI case requiring an endpoint. It checks
  advancing frames, captured region, device/recording-clock generation, detached
  edit cursor, document history and the active take across loop toggles. No device
  default is changed, and absence of an endpoint is a failure, not a success/skip.
  Registered in the existing grouped Windows integration job.
- `editor/Tests/PlaybackRegionChecks.hpp`: the previous Mac region assertions
  are retained in a shared body; both wrappers run exact endpoints, tempo changes,
  cursor starts, selected and whole-song loop disable, and independent sample
  audition at 44.1/48/96 kHz with 1/17/128/4096-frame partitions. Added coverage
  enables an already-running non-looped region and disables it again. Windows
  adds the `playback-regions` target with its existing host C++ allocation/free
  probe; direct malloc/free, locks and drivers remain outside that probe. Mac
  retains GraphRealtimeAudit and its existing sanitizer exclusions.

No build or test has run for this batch. Before qualification, freeze the combined
source and use one grouped build checkpoint: Windows application, workspace target,
playback-region target and existing API standalone project; Mac app and playback-
region target because the shared test body moved. Run those targeted groups plus
the existing workspace-shortcut/native-control and recording application checks.
Run P1/P2 groups against the same candidate if integrating the dependent stack;
their earlier failed/unrun results cannot be relabeled as passing evidence.
Preserve all assertions. Broaden transport/recording/native-app validation if a
failure requires changes to shared rendering, device lifetime or main message
dispatch. No broad upstream PCM suite is justified solely by this atomic setter
adapter; a subsequent renderer change would invalidate that boundary.

The standing earliest next local build remains 2026-10-10 02:25:48.842926 UTC,
based on P1 checkpoint 02. Reuse current results only for unchanged relevant
inputs; the new shared region checks and P3 native/API paths are unqualified.

## Pattern and native-text Select All — prepared, unqualified

Mac `PatternView.swift` selects all rows/channels without moving its edit cursor.
Windows command 544 previously selected sample frames only. It now selects all
pattern cells when that canvas owns focus, with a pattern context-menu entry and
the existing customizable Ctrl+A binding. The cursor, follow flag and viewport
stay put. `context.get.selection` and drawing use an independent selection end;
copy, clear, transforms, sample rendering and note-track grouping consume those
same bounds. No new musical operation, project field or independent Undo exists.

`WorkspaceView.inc` owns selection state and its context revision. Ordinary
navigation, a fresh click (including at the existing cursor), graph-lane focus,
scratch Return and document replacement reset that state. Shift navigation and
group headers replace it using their own anchors. Appending columns preserves the
captured range; explicit Select All includes new columns, and shrink/Undo clamps
it and updates the context guard. Repeat Select All on unchanged bounds is a no-op.
The reset helper is deliberately distinct from the existing
`EditingView.inc::clearPatternSelection`, which deletes musical content.

Native text fields retain shortcut ownership. `NativeControls.hpp` handles the
translated Ctrl+A character for classic EDIT through ordinary EM_SETSEL; RichEdit
retains its native implementation. The send grants no presentation permission and
remains subject to the existing document-departure input lease. The sidebar's
typing/private-clipboard hints move below the new loop control to avoid overlap.

Prepared checks: `workspace-pattern-selection-tests` covers cursor/viewport/song
preservation, repeat no-op, grouping bounds, append/shrink, same-cursor reset,
departure refusal and edge-to-edge Delete with one Undo. The existing PID-pipe
workspace-shortcut suite gains real queued Ctrl+A, cursor navigation and retained
text ownership checks. `native-control-tests` adds single-line, multiline and
read-only text selection, unchanged text/Undo/focus and departure-lease refusal.
These checks have not run. Add them to the same next Windows build/checkpoint,
including existing shortcut, context-menu, grid/docking and native input-gate
groups because common selection/control handling changed. Mac source is unchanged
by this selection slice; the prior shared P2/P3 checks still require a Mac build.

## Native playback entry points — prepared, unqualified

`WorkspaceTransport.inc` maps native edit/selection state to the existing shared
renderer/controller's region contract. Command 587 is Play from edit cursor,
588 is Play selection or pattern, and 589 is its cursor variant. Their defaults
are Shift+Space, Ctrl+Space and Ctrl+Shift+Space. Existing native-control and
formula-local ownership still wins. Palette and pattern context-menu entries
expose all three. Ordinary native Play now starts from row zero of the selected
occurrence of the current pattern, falling back to its first occurrence, as in
Mac `main.swift::playbackSettings`. The API's empty Play request remains order zero.

Bounded Play captures selected inclusive rows as an exclusive end, or the whole
pattern if unselected. Its cursor variant uses the current row only when inside
the range. Windows keeps the existing Follow setting and detached edit position;
it does not force Follow on as Mac's completion currently does. This follows the
plan's independent edit/playback acceptance requirement. An unarranged pattern
can use bounded Play; unbounded cursor Play reports the missing occurrence rather
than silently playing a different pattern. Ordinary Play falls back to a selected
playable occurrence/first playable order. A sequence with no playable order
refuses before preparation; no synthetic order is inserted.

Captured targets contain document, sequence, pattern and occurrence identities.
The native-only resolver runs again after the existing plugin-editor flush, which
can pump input, and before WASAPI close/open. A moved occurrence resolves to its
new slot; deletion, reassignment, document/sequence replacement or invalidated row
bounds refuse. Later edit navigation does not redirect the originally requested
playback. UI choice/capture remains native; musical range execution, recording,
audio lifetime and DSP use the existing shared/native owners without a new path.

`WorkspaceShortcuts::Definition::introducedDefault` supports these additive
defaults without invalidating older customized profiles. On load only, an absent
new command yields to an explicitly stored exact/prefix binding as an empty
override. The file is not rewritten by loading. Subsequent explicit saves retain
that override, and explicit Reset can restore the default after its conflict is
removed. Explicit conflicts still fail atomically; the stored format stays v1.

Prepared checks extend the existing transport Application group with whole-pattern,
8–23 selection, inside/outside cursor, duplicate order, moved/removed/reassigned
occurrence, original capture after navigation, actual post-flush refusal, unarranged
pattern, range and departure cases. Actual-PID tests invoke the native commands on
a private silent WASAPI instance, asserting exact region, stable document, detached
context and viewport. They use real Shift-click selection, require an endpoint,
and do not change device defaults. The shortcut pure target covers old-profile
exact/prefix migration, unchanged disk bytes, unrelated bindings, save/reopen,
Reset and continued rejection of malformed/conflicting profiles. Existing Formula
and native shortcut ownership cases remain required with the new defaults.

No build or test has run for this batch. Add these cases to the same grouped P3
checkpoint, including workspace-shortcuts, native shortcut/Formula ownership and
transport/recording integration. Both-platform shared renderer checks from the
prior slice remain necessary; this native command mapping does not alter DSP or
the Mac application. The remaining timeline/ruler, full transform workbench,
command/UI foundation, File/song entry points and later phases remain open.

## Shared musical display rules — prepared, unqualified

F24 source drift is addressed by `editor/PatternDisplay.h`, a small C-compatible
header consumed directly by both C++ and Swift's Clang importer. It defines
legacy metric fallback (4/16), measure precedence even for non-divisible beat/bar
lengths, and bounded/truncated graph-lane hex display. F04's 70% Start is B2 and
35% Amount is 59; Wet uses its independent wet field. This does not quantize stored
doubles, change row units or approximate elapsed musical time. Non-finite display
values retain Mac's neutral zero fallback.

Windows `DocumentController` now publishes one `patternGridMetrics` map containing
both beat and measure lengths, resolving imported pattern signatures before song
defaults. Both grids and the existing beat-unit effect/Scratch adapters consume
that immutable map. Nothing queries the document worker while drawing. Each map
entry is conservatively charged before allocation/publication; controller cache
budget checks must be rerun. `workspace.get.gridTiming` and graph-lane `gridTiming`
expose the effective pair used by drawing. No serialized project field changes.

Mac retains its existing colors, spacing and grid behavior while `PatternModel`,
`PatternView` and graph command text consume these shared rules. The existing
bridge header imports the C header; the standalone interface-test compiler imports
that same lightweight header instead of requiring the application session bridge.
This is platform-independent display logic only; native drawing and controls stay
with their respective frontends. The Mac graph strip's existing visual treatment
is unchanged.

Prepared coverage includes a shared `pattern-display` target on both platforms,
Windows worker default/override, clone, retained snapshot, save/reopen and Undo
checks, actual Application projection/formatter checks under
`workspace-pattern-display-tests`, and Mac's existing pattern-grid interface group.
Add these plus controller budget/history tests and both native app builds to the
combined gate. Inspect F04 in both real UIs and inspect 3-row beats/10-row measures
at that gate; authored value/state checks do not establish pixel clarity or visual
parity. No build or test has run for this display slice.

The Windows root test project now includes `Tests/Api` as `api-tests`, preserving
its standalone entry point while adding the exact same ApiTests and
SessionCacheTests targets/tests to the main checkpoint. They remain separately
labelled pipe/cache tests with their existing time bounds. This avoids a second
configure/build cycle solely to validate the changed SessionAdapter; it does not
replace actual application pipe or native UI checks.

## First integration configuration observation

Prepared P1–P3 source was merged into temporary `codex/windows-parity-integration`
at `c21c6f4eea8edaee25bbbdd2220b145b3ede3680`, with a tree identical to P3
`7967a7095`. The existing P1 cache stayed in its original checkout. The first
configuration at 2026-10-10 02:26 UTC failed before compilation: the API dependency
preceded creation of portable-tests, and the new playback-region registration
duplicated the existing portable registration. Neither binaries nor tests ran.

The follow-up puts the API dependency after the portable include and replaces the
single existing playback-region registration with the native audit wrapper. Test
identity and all shared region assertions remain; the functional-only sanitizer
shim is no longer applied to that native audited target. This is a configuration
repair within the same grouped checkpoint, not evidence of a successful build.
The failed log/receipt remain under bin/parity-evidence/integration-build-01.log
and integration-checkpoint-01.json; compilation, runtime and Mac gates remain open.

## Grouped compilation checkpoint 01b — failed, correction batched

The first actual compilation of the integrated P1–P3 candidate
`c96bec8dbd00932bc89fdf9a27b6467d4502cf1b` ran on Windows ARM64 from
2026-10-10 02:28:01 to 02:28:34 UTC. Configuration succeeded. Compilation stopped
in `editor/TrackLayout.cpp`: both accesses to
`song.GetModSpecifications().channelsMax` require the complete
`OpenMPT::CModSpecifications` definition, which `Sndfile.h` only forward declares.
The accompanying `std::min` diagnostics follow from that incomplete type.

The source correction explicitly includes `soundlib/mod_specifications.h` and
`<algorithm>` in the shared implementation. No behavior or assertion changes.
It remains uncompiled. No immediate build retry or test execution was started;
the next normal local build is no earlier than **2026-10-10 03:28:34 UTC**
(20:28:34 Pacific on 9 October), after batching further source work.

The frozen integration checkout retains `bin/parity-evidence/integration-checkpoint-01b.json`
and `integration-build-01b.log`; log SHA-256 is
`f3bb7ee6203d112e111bc9de5d37f143890fa98654697bf587ac54a69f17ce9c`.
Source hashes remained unchanged across compilation. Existing executable hashes
in that failed-build receipt do not establish new app binaries: the shared library
failed before the app could link. Do not run those stale binaries as P2/P3 evidence.
All grouped native/application and cross-platform qualification gates remain open.

## Shared first-visit timeline and native ruler — prepared, unqualified

`editor/PatternTimeline.hpp/.cpp` now owns the previous Mac engine-row observation
algorithm. The typed result contains pattern, optional order and row/beat/optional
seconds entries. Explicit orders must contain the requested pattern; omitted
orders select its first current-sequence occurrence. Unarranged patterns retain
their rows with null times. `GetLength(eNoAdjust)` supplies first visits, including
flow, tempo/speed, groove and native timing after `NativeSong::prepareEffects`.
The engine's existing loop/complexity bounds remain. No fixed-BPM approximation,
playback seek, plugin preparation or persistent field was introduced.

The shared function runs on the document/control owner. It reuses the engine's
independent length-walk state and prepares only derived lookup data on that
owner's song, preserving the existing Mac implementation's ownership. This avoids
copying sample payloads into a second document for every ruler refresh; neither
platform's active renderer uses that document instance. Mac serializes the typed
result to its unchanged dictionary contract. Windows `TimelineOperations` and
`SessionAdapter` expose the matching read and catalogue entry, with strict request
validation and matching `-32602` errors. The existing public schema already covers
both requests; both API guides now describe the shared implementation.

`windows/App/WorkspaceRuler.inc` owns Windows view state and completion handling.
Native button/command 590, the palette/context menu and `workspace.ruler` select
rows, beats, pattern time or song time. The ruler uses 38/72/104-DIP gutters, with
the same member consumed by drawing, hit testing, scrolling and inline editors.
`workspace.get` exposes positionMode and a ruler diagnostic plus actual gutter
geometry. The button keeps native keyboard focus when cycling. No new default
shortcut conflicts with tracker, text or Formula input; customization uses the
existing command system. Mode is session view state, outside song/history, and
does not change cursor, selection, scroll or retained editor drafts.

Only one asynchronous query is outstanding. Its requested document, revision,
sequence, stable pattern, occurrence and mode are checked before adopting cached
labels. Edits, Undo, order selection and document replacement make old labels
unavailable immediately; late completions are discarded. Drawing never queries
the worker. Pending/unreachable times show `--:--.---`; failure retains a status
and diagnostic without a tight retry loop. The main message wait polls at 25 ms
only while that future exists, retaining its ordinary idle wait afterward.

Prepared checks (not executed):

- Shared `pattern-timeline` on both platforms: repeated occurrence absolute time,
  tempo transitions, current sequence, unarranged/mismatched orders, unreachable
  flow rows, native tempo, groove/signature, snapshot/identity/history purity.
- `document-controller-timeline`: actual worker validation including bool,
  fractional, null, out-of-range and unknown fields; unchanged view/history/Stop
  count; timing edit, Undo/Redo and native save/reopen.
- Existing `workspace-pattern-display-tests` now includes real Application ruler
  button focus/geometry, beat/time labels, stale asynchronous completion, repeated
  occurrence, viewport/selection retention, invalid modes and departure refusal.
- Existing `TransportLoopTests` now includes real PID-pipe catalogue/shape/errors,
  native ruler command and completion, unchanged document/context/viewport, plus
  timeline reads while the private silent WASAPI stream remains active.

At the next eligible consolidated checkpoint, add `pattern-timeline-tests` to
the integrated build targets and include `pattern-timeline`,
`document-controller-timeline`, `workspace-pattern-display-tests` and the existing
actual-PID transport class. Keep the full outstanding P1/P2 groups and cache,
history, shortcut, native-control, grid/docking/context-menu coverage because
gutter geometry and a common message wait changed. Mac must build the shared
source and run `pattern-timeline`, existing `playback-display`, `playback-regions`,
native timing and relevant pattern-grid interface checks. F04 and repeat/flow
fixtures still need both real UI inspections; screenshots and source assertions
do not prove visual clarity. No compiler, build, application or test was run for
this batch. The prior failed build's hourly cooldown is still respected.

## Retained Pattern Tools workbench — prepared, unqualified

`windows/App/PatternToolsWindow.hpp` and `PatternToolsIntegration.inc` add the
native command 591 to the palette and pattern context menu. The form uses the
existing `pattern.transform` operation; it does not create a second editing
engine. Its shared C-compatible `editor/PatternToolCatalog.h` supplies operation
names and option flags to both this form and `mac/App/PatternTools.swift`.
`NativePresentation.h` makes the display/catalogue headers available to the
Mac bridge and standalone interface harness. Native control/layout ownership,
JSON dictionaries, host validation, history and audio boundaries remain intact.

The 14 operations and five scopes include group identity, field masks, numeric
filters, interpolation curve, seed, remap/swap and explicit data-loss permission.
Opening captures document/revision, stable pattern/column, note track and the
selection. Navigation and reopening do not retarget it. Capture explicitly
adopts the current target and preserves settings. Any option or text change
invalidates Preview. Apply submits the exact prepared arguments/revision;
stale targets require a new capture/preview. Scope song includes unused patterns.
Native keyboard handling offers Tab, Ctrl+Enter, Escape and F6 back to Pattern.
The resizable window has a scrollable detailed change list and a minimum size;
foreground DPI, actual tab/focus behavior and accessibility remain unverified.

Draft registration includes hidden raw text, valid previews, pending operations
and unresolved results. Successful departure retires the HWND and owner through
the existing registry. NativeWriteCompletion preserves exact worker receipts,
original target and later typing. Review performs read-side synchronization,
never a repeat write. A missing receipt uses a separately labelled unverified
observation and explicit acknowledgement at its original document/revision.
The musician can inspect music through F6; acknowledgement makes no authorship
or Undo claim. Later writes still require capture and Preview.

`DocumentController::invokeOperation` now classifies rejected `pattern.transform`
as NotCommitted only when the worker revision/path/save state is unchanged and
the operation did not return. This method validates a complete candidate, stops
before commit, and performs one atomic document edit. The existing earlier
postcommit branch still reports committed outcomes. This classification is not
extended to arbitrary vendor, recording or filesystem calls.

Source inspection also found that Mac enabled Apply only for nonzero module-cell
changes, even when the engine reported native-only changes. The Mac form now
accepts `effectsChanged:true` and explains native-note/FX changes in its summary.
No musical transform semantics changed: row operations move precise notes and
native FX; existing numeric/transpose operations act on module cells on both
platforms. A future extension to numeric precise-note transforms must be an
explicit shared behavior change with its own fixtures, not an accidental port.

Prepared checks, not executed:

- `document-controller-pattern-transform` in the existing worker executable:
  native-note-only row reverse, exact fractional onset, dry-run purity, invalid
  field/unknown-key refusal, throwing Stop before commit, absent receipt and
  NotCommitted classification; one Undo/Redo, no-op preserving Redo and reopen.
- `workspace-pattern-tools-tests` in the existing workspace executable: F04-like
  rows 8–23 interpolation 4→64, exact API preview agreement, all 14 operations and
  five scopes, masks, detached cursor, stale/invalid/late previews, retained newer
  text, native-only Apply/Undo/Redo, minimum geometry, departure retirement,
  known receipt Review, unknown observation and stale acknowledgement.
- Actual PID `test_pattern_tools.PatternToolsAppTests`: native command, API
  agreement, captured column after cursor navigation, unrelated-column retention,
  Undo/Redo/save/reopen and invalid/stale draft retention. Added to the Windows
  integration workflow. Cases use private desktops and disposable songs.
- Existing Mac pattern-grid interface group: zero module changes plus native
  changes enable Apply, and Apply consumes the preview. Existing late preview,
  later typing and completion assertions remain required.

Include those groups in the next consolidated Windows gate with outstanding
P1/P2/P3 coverage. Build `pattern-tools-tests` and `pattern-timeline-tests` along
with the existing app/worker/workspace targets; shared `pattern-tools`, precise
note and native command checks protect musical behavior. Build Mac with the
changed Swift/C imports and run pattern-tools, native-note/command and pattern-grid
interface checks together. Reuse the same qualified binaries for supplied F04
Mac/Windows identical-argument preview comparison and reciprocal save/reopen.
The synthetic local checks do not replace the supplied fixture acceptance.

Only source review, diff whitespace checks and Python syntax parsing were done
for this batch. No compiler, app or test was invoked. The prior integration
build failed on TrackLayout includes; its source fix remains unqualified and its
old executable cannot validate this work. Next normal build remains no earlier
than **2026-10-10 03:28:34 UTC**, in the frozen integration checkout after a
deliberate source update. P0b/P0c qualification and the remaining P3–P8 scope stay
open; this source batch is not phase completion.

## Order destination API parity — prepared, unqualified

Windows `DocumentOperations.cpp::order.edit` now admits `move` with the required
`destination`, routed to existing shared `Document::orderEditChanges/editOrder`.
Destination means the final slot in the current sequence; it is not a pattern
number or a new identity. The existing candidate validation and preflight keep
exact no-ops before snapshots and Stop. Both directional moves preserve complete
occurrence metadata, repeated pattern uses and End/Skip slots. No shared model,
file representation or native audio code changed.

The Mac bridge already supports moves but previously ignored destination when
operation was remove. Its move-only guard now runs before that branch. This
rejects a meaningless formerly ignored parameter; valid remove/move requests are
unchanged. The shared schema now requires destination for move and forbids it
otherwise. This closes the order portion of F27 in prepared source only; plugin
move/gesture and other F27 contracts remain in their later batches.

Prepared regression additions reuse existing binaries/groups:

- Windows `document-controller-arrangement`: repeated-occurrence destination
  move, same-index no-op retaining Redo, absent/bool/fraction/range/misused
  destination rejection without Stop/history changes; arbitrary moves through
  sentinel slots in both directions, complete metadata, Undo/Redo and reopen.
- Actual PID `SongToolsUITests.test_repeated_occurrence_moves_undo_and_native_duplicate_persistence`:
  retains the existing native adjacent-move/duplicate checks and adds arbitrary
  API move, stable native arrangement selection, no-op/Redo and reopen. The case
  is explicitly included in the Windows CI application selection.
- Mac `native-song-tests` matrix/session checks: invalid move and remove
  parameters, occurrence metadata, same-index no-op, Undo/Redo and native reopen.
  Existing shared `order-edit-admission` continues protecting the common model.

Run these with the same consolidated Windows/Mac build as the Pattern Tools and
ruler changes. No additional build for this adapter correction. Schema JSON and
Python syntax parsing plus diff review are source checks only; the new acceptance
cases have not run. Drag-to-reorder and other native Arrangement ergonomics are
not claimed by this API change.

## Musical input navigation — prepared, unqualified

`MusicalTyping.inc::typingInputCommand` adds native commands 592–596 for octave
down/up, previous/next instrument or sample, and Use instrument at cursor. The
palette exposes all five through existing shortcut customization; the pattern
menu and Enter on an instrument cell expose the cursor action. No new default
global bindings interfere with native text editing or user shortcuts. Instrument
selection uses the existing guarded `workspace.input` path. Octave adjustment
retains the Windows chooser's existing 0–9 range; the API's existing Mac-compatible
0–8 range is unchanged. This is a pre-existing range difference, not a new API
parity claim or a reason to remove Windows' highest native octave.

The cursor action prefers the module cell's instrument, then the first nonzero
precise-note instrument in that row and column. Stepping includes empty slots
1–255. Empty cells and limit-clamped steps are no-ops. The commands return before
the generic command focus reset, and preserve selection, edit/playback position,
raw inspector drafts, song/history and already-held note destinations. They
refuse a busy/replacing document before changing input state. Only input-context
identity changes for a real adjustment.

Prepared checks reuse `workspace-pattern-selection-tests`: Enter, module/precise
instrument selection, empty/maximum slots, octave boundaries, no-op context
identity, invalid retained workbench text, captured target/focus/selection/song
retention, busy refusal and departure admission. Existing actual-PID
`MainIntegrationTests.test_input_is_atomic_context_only_and_supports_empty_slots`
adds native-command stepping and cursor selection; it is explicitly selected in
Windows CI. Run both plus shortcut, audition/note-release and typing cases in
the same consolidated checkpoint as the other P3 changes. No renderer or shared
musical state changed, so this slice adds no separate audio build cycle. Keyboard
layout/display customization and the near-cursor FX picker remain outstanding.

Only source/diff review and Python syntax parsing are claimed for this slice;
the Windows build and actual native checks remain pending.

## Near-cursor FX chooser — prepared after checkpoint 02 freeze

This source batch is **outside** integration checkpoint 02's frozen `607154901`
candidate. Its build or test results cannot qualify the chooser.

`EffectPickerWindow.hpp` supplies Win32 search/list controls and native keyboard
navigation. `EffectPickerIntegration.inc` captures the single-cell target and
catalogue, places the modeless window near its screen position with monitor
clamping, and transfers a choice into the existing Pattern FX inspector. Command
597 is in the palette and pattern menu; local F4 works after native text/editor
ownership and configured shortcuts have had priority. Enter on an FX cell retains
its prior inspector behavior. No global F4 binding or parallel mutation path was
added. Search covers display code, name, description and format equivalents.

Choosing opens typed parameter fields and marks the selected command as a
retained draft; no musical write occurs until the existing inspector Apply.
Escape restores prior focus, while successful choice preserves the inspector's
new focus. The chooser compares document/revision, cursor, stable pattern/column,
FX/nudge generation and plugin-parameter context before selection. Stale input
cannot retarget a write. Explicit Use current cursor retains the query. Existing
dirty FX/nudge drafts are raised intact. Search is read-only and does not block
departure; the registered native owner is retired with its document.

Prepared checks, not executed: `workspace-effect-picker-tests` uses the actual
Application and native controls for F4/catalogue search, captured draft transfer,
Apply/Undo/Redo through the existing mutation path, stale cursor/newer invalid FX
refusal, explicit recapture retaining search, no matches, minimum geometry and
document retirement. `test_pattern_tools.EffectPickerAppTests` adds actual-PID
choice/Apply/history/save/reopen and stale/no-match cases, explicitly selected in
Windows CI. Reuse the workspace/app targets in the next eligible batch build and
run these plus native tool, pattern-field/performance, shortcut and departure
groups. Mac has no source change in this chooser slice; retain the pending Mac
gate for the earlier shared/catalogue work. Foreground placement, DPI, high
contrast, screen-reader behavior and keyboard ergonomics still require direct
qualification; private native tests do not establish those properties.

## Consolidated checkpoint 02 and build reuse

The ARM64 integration candidate `607154901ad29ec58043c7842098b114810d2314`
was frozen separately from subsequent work. Its consolidated build ran from
2026-10-10 03:29:40 to 03:36:12 UTC and exited 1. Source fingerprints were unchanged.
The app, workspace harness and document-worker targets linked, but
`native-audio-bus-tests` failed compiling `AudioBusTests.cpp`: inclusion of
`PlugInterface.h` after `using namespace Tracker` made `mpt` ambiguous in
`aligned_array.hpp` (Tracker imports OpenMPT). No runtime gate was run and no
phase is qualified by these partial outputs. Retained evidence in the integration
checkout: `bin/parity-evidence/integration-checkpoint-02.json` and
`integration-build-02.log`; log SHA-256
`03b52ff7ac33680346380f9673c78af78586945dbb7429f6ed952afca4645601`.

The next source batch moves the audio-bus test's engine dependencies above its
namespace directive, preserving the existing provider, ownership and allocation
assertions. It also introduces the Windows-only `ScreamSeqSession` static library
in `windows/CMakeLists.txt`. The same eleven session translation units previously
compiled separately for the app, workspace harness and worker tests now compile
once per configuration. Their dependency-provided definitions/options remain
identical; fault injection remains runtime-based. Native entry points, app
resources, scanner dependency, 8-MiB executable stacks and test registrations
remain on the existing targets. Isolated operation tests retain their narrower
source/link scope. Shared model sources and Mac build configuration are unchanged.

The refactor is prepared, not built. Its main risks are transitive link dependency
or compiler-option drift and missing registration after deriving workspace
sources from the app. At the next eligible consolidated build (not before
2026-10-10 04:36:12 UTC), compare the registered test inventory and qualify the
app, workspace and worker targets together, including the chooser outside the
previous freeze. Keep the full pending P1/P2/P3 and provider checks; this change
reduces compilation duplication, not coverage. Windows x64 and ARM64 builds are
required for the build-graph change. Earlier shared/catalogue edits still require
their pending Mac gates. No assertion or timeout has been relaxed.

### Bounded diagnostic of freshly linked targets

After the failed consolidated build, a separate diagnostic verified the exact
source, log and executable hashes and that workspace/worker executable write
times fell inside that build with successful target-link log entries. Only those
two fresh executables were admitted; later/unbuilt binaries were excluded. The
full test helper still refuses the failed build and was not bypassed.

At `607154901`, ten targeted groups plus their scratch-directory setup passed
in 21.76 seconds: workspace note tracks, Pattern Tools, transport controls,
selection/input navigation and pattern display/ruler; document-worker tracks,
timeline, pattern transforms, performance and clipboard. This is private-desktop
and worker evidence, not foreground, device, actual-pipe or whole-integration
acceptance. The newer chooser, build refactor and density controls are excluded.
The integration checkout retains `bin/parity-evidence/integration-diagnostic-02.json`
and `.log`; log SHA-256
`8fd527ccc6e59efde4a09fa6ad31d922cce203c59a29821c142caee130817087`.
Reuse these results only while their relevant source/build inputs remain valid.

## Pattern density — prepared after checkpoint 02

Native commands 598/599 adjust row height over 18/22/26/30/34 DIPs, with clamped
endpoints. They are discoverable in the pattern context menu and shortcut
palette without introducing conflicting default bindings. All existing pattern
and graph-lane drawing, hit tests, inline placement and visible-row calculations
use the same height. The public workspace geometry reports it. The change
preserves song/history, cursor, selection, viewport and raw inspector focus/drafts;
mouse capture, document departure and busy replacement refuse the adjustment.

Custom workspace layouts persist optional `patternRowHeight`, validated before
any editor preparation/adoption. Absence means the prior 18-DIP default, and
default saved layouts keep their old shape. Nondefault new layouts may be
rejected by older readers; existing layouts and all project files are unchanged.
Mac already offers these row heights; no shared musical or Mac code changes.
This does not complete configurable note-key mappings, font scaling, UIA or the
remaining P3a/P3c work.

Prepared checks extend the existing workspace pattern-display group: every size,
limits, native coordinate hits, focus/raw-draft retention, disk save/reload,
legacy/invalid layouts, mouse-capture and departure guards. The already-scheduled
`TransportLoopTests` adds actual-PID native commands and save/restore purity.
Run these with the existing saved-layout, workspace and shortcut tests at the
next consolidated build. Only source review, diff checks and Python parsing are
claimed here; compilation, foreground DPI and rendered legibility remain pending.

## New and Open Demo — prepared after checkpoint 02

Commands 624/625 expose New song and Open demo song in the native File command
catalogue. Ctrl+N is a newly introduced default, using the existing shortcut
migration rule so a saved custom binding keeps priority. Native New/Demo call
the existing unsaved-song/take guard, capture the revision, review raw drafts,
then invoke the same worker operation as the API. Cancellation returns before
generic focus reset. Success focuses the pattern after adoption.

`document.new {expectedRevision, demo?, discard?}` is a native Windows extension,
advertised only by hosts listing it in additional writes. The schema and guide
are in `windows/Api/`. It constructs `Tracker::Document` or `Document::demo()`
exactly as Mac New/Demo do, creates fresh `ProjectState`, and passes through
`DocumentController::installCandidate`: full view/assets/plugins/recorder staging,
late native draft admission, Stop, one adoption, owner retirement and refresh.
No parallel replacement lifecycle or musical constructor was introduced. The
response is allocated before adoption. Document/revision identity is fresh;
history and path are empty. Retained MIDI or microphone takes reject replacement,
and `discard:true` cannot authorize raw native draft loss. Consent is cleared on
completion or failure. Existing post-adoption refresh recovery and request replay
prevent treating an uncertain result as permission to replace the song again.

Prepared controller checks extend `document-controller-departure`: New/Demo
admission and Stop rollback, malformed/missing/stale input, dirty plugin-state
preservation, candidate failure before Stop, shared blank/demo contents, fresh
identity/history, stable-ID save/reopen and retained MIDI refusal. Workspace
departure checks add both native commands, raw-draft cancel/focus, late edits,
failed Stop, retirement, MIDI retention and Ctrl+N preference migration. The
actual-PID `MainIntegrationTests.test_new_and_demo_identity_validation_replay_and_save_reopen`
is explicitly selected in Windows CI and checks catalogue, parameter rejection,
song-discard guard, exact replay, native commands and persistence.

No build or test execution is claimed for this slice. Batch it with the pending
chooser, density controls and build refactor. Run API/cache, worker/native
departure, recording/recovery, shortcut and actual-PID creation cases. Existing
recording and microphone guard suites remain necessary; synthetic native checks
do not establish physical capture/device behavior. Mac source and project wire
format are unchanged; its pending earlier shared-code gate is still required.
P3c title/channel controls and retained load report remain outstanding.

## Retained load report — prepared after checkpoint 02

`DocumentLoadReportWindow.hpp` and `DocumentLoadReportIntegration.inc` present the
loader's existing warnings, issues, original source path, current path,
source-protection and editable status in a modeless native text view. Command 626
is both in the File catalogue and beside the file controls, with a recovery
caption while Save a copy is required. Read-only text uses standard selection,
scrolling and keyboard operation. Unchanged refreshes avoid `WM_SETTEXT`, keeping
selection/scroll/focus. Closing restores prior focus, and reopening retains the
same report. Warnings survive Save As because the existing ProjectState already
retains them; no parallel acknowledgement or source-protection policy is added.

Ordinary tracker-module import now records its original `loadSourcePath`, just as
native project import already does. The field remains runtime provenance, not a
new serialized project field. Newly created/saved songs do not invent a source
file. Save a copy posts a captured document identity to the main window and runs
the existing Save As only after its report callback has returned. This avoids
holding the tool's callback on the stack during a nested chooser or worker pump.
Replacement retires the read-only owner and cancels queued actions before native
refresh, preventing an old report from targeting the new song.

Prepared `workspace-departure-tests` coverage opens a real future-version native
fixture through the production loader, checks warning/source text and selectable
read-only controls, unchanged-refresh selection/focus, protected-source rejection,
Save As retaining warnings, minimum control bounds, close/reopen and queued-save
retirement. The actual-PID
`MainIntegrationTests.test_retained_load_report_preserves_source_warnings_after_save_copy`
is explicitly selected in Windows CI and checks the same loader/pipe/native
report/save/replacement path with exact preservation of the original file bytes.
It does not operate a foreground file chooser or qualify Narrator/DPI rendering.

Source/diff review and Python syntax parsing only; build/runtime qualification is
pending. Include these cases in the next consolidated Windows batch with native
departure, save/recovery, import/persistence and existing source-protection tests.
No codec, Mac UI or musical edit semantics changed. Retain the P0/P8 reciprocal
fixture and earlier shared-code Mac gates. P3c title/channel controls and the
remaining P3a accessibility/visual work are still open.

## Song title and channel count — prepared after checkpoint 02

`SongPropertiesWindow.hpp` / `SongPropertiesIntegration.inc` add command 627 to
the Song catalogue and the sidebar channel-count summary. This retained native
form calls existing `document.patch`; no alternate title conversion, channel
resizing, metadata reconciliation, history or codec was added. The controller
snapshot now publishes format channel minimum/maximum alongside its existing
format limits. Invalid raw channel text and out-of-format counts are rejected
locally; the existing worker candidate validation remains authoritative for
capacity and native reference integrity before structural Stop/mutation.

The form captures document/revision, submits only changed fields and preserves
tempo/meter/groove. It warns that reducing the count removes trailing channels
and their music. Refresh/reopen never overwrite fields; explicit Use current
revision retains typed intent, whereas Reload values discards it. Ctrl+Enter,
Tab, F6 and Escape provide native keyboard operation. The existing document
draft registry owns hidden drafts and replacement retirement; the existing
`NativeWriteCompletion` retains known or unknown outcomes without replay and
protects later typing by generation. Unknown readback requires explicit
acceptance at the observed revision. This reuses P0b's native ownership and
receipt boundaries instead of introducing another command/history controller.

Prepared `workspace-song-properties-tests` shares the existing workspace
executable, with no extra compilation of Main. Cases cover invalid/raw/range
input, stale drafts, retained focus, atomic title/count Apply, stable channel
identities through one Undo/Redo and save/reopen, hidden departure protection,
minimum native control bounds, Escape and committed/unknown completion failures.
The actual-pipe
`SongToolsUITests.test_song_properties_native_patch_history_shrink_and_reopen`
is explicitly selected in Windows CI: native Apply preserves unrelated cells
and timing; removing a populated trailing channel and undoing restores its
cells and identity; equivalent numeric text is a no-op retaining Redo; native
save/reopen preserves the restored data and retires the previous owner.

Qualification is pending: only source/diff inspection and Python syntax parsing
are appropriate during the build cooldown. The next consolidated Windows gate
must include the new workspace group, actual-pipe test, existing document patch,
track/mixer capacity, departure and persistence checks. Windows x64 and ARM64
remain separate requirements. This slice changes no shared/Mac source or project
format; earlier shared-code Mac gates and reciprocal fixture checks remain open.
P3c's planned entry points are now prepared in source, not qualified complete.
P3a accessibility/visual checks and P3b's remaining interaction work remain open.

Source layout inspection also found that stacking the two new entry buttons
under Live Loop would clip Settings at the 620-DIP frame minimum and overlap the
sidebar help. Settings now shares the channel-count summary row; Report shares
the Save As row, with compact Report/Warnings/Recovery captions and full names in
the command catalogue. The retained OpenMPT credit is placed on the bottom row
so it no longer overlaps Live Loop at the minimum frame height. Foreground text
legibility and mixed-DPI qualification remain pending.

## Musical keyboard preferences — prepared after checkpoint 02

The inspected Mac `KeyboardSettings.swift` permits note-row customization and
Space/Return transport. Windows previously duplicated fixed rows in Main's
`MusicalTyping.inc` and `AuditionWindow.hpp`. `MusicalKeyMap.hpp` now owns their
shared Windows mapping/validation; Main, connected sample/instrument/precise-note
typing and native audition consume it. Physical translation stays in the native
host, including unshifted punctuation and the extra ISO key. The unchanged
Windows default has an optional top C on I; the UI supports the reference's
12+12 arrangement without silently removing that established key.

`WorkspaceShortcuts` extends its existing profile with optional `noteKeys`, so
mapping plus transport selection use one validation/conflict/atomic-file commit
and the same unchanged-file guard and cross-session mutex. All other shortcut
edits preserve the map. Old profiles load defaults without rewrite; default maps
omit the field. Old executable readers reject the extension safely rather than
discard it. Return is allowed only for definitions explicitly permitting it
(Main's Play/Stop), preserving the existing Ctrl/Alt rule for other custom
bindings. Keyboard changes affect future notes; held-key target/pitch and release
ownership remain untouched. Mapped keys yield from global unmodified bindings
only in the pattern note column; local editor and text ownership remain native.

Command 628 opens `KeyboardSettingsWindow.hpp`, with retained fields, atomic
Apply, explicit Reload, defaults in draft and a keep-custom-binding option.
This global preference owner survives song replacement and does not masquerade
as a musical draft. `workspace.keyboard.set` plus `workspace.get.keyboard` expose
the same contract with `expectedKeyboard` configuration guarding. Describe,
schema and guide are updated. No project metadata, musical history, plugin state
or Mac/shared model code changes.

Prepared checks: the existing shortcut executable covers legacy profiles,
case normalization, punctuation/ISO positions, invalid/duplicate rows, transport
conflicts, no-op disk preservation, reload failures and concurrent writers.
`workspace-keyboard-settings-tests` shares the existing native workspace binary
and covers raw/stale field retention, focus, bounds, mapped F versus Follow,
held-note release across a mapping change, global draft lifetime, and a
deterministic audition host's note-on/off parameters. The latter is input-owner
evidence, not audio-device qualification. The actual-PID
`WorkspaceShortcutTests.test_keyboard_preferences_atomic_guard_replay_and_native_entry`
is selected by the existing CI class invocation and covers pipe validation,
exact request replay, native form retention and mapped physical key entry.

Only source/diff review, Python syntax and JSON parsing are completed. Queue
shortcut/session-adapter, the new native group, actual-pipe shortcut tests and
existing musical-typing/audition/local-focus cases for the next consolidated
Windows validation. The common native physical-key helper warrants the existing
retained-editor keyboard suite; a failure there broadens ownership inspection,
not assertion relaxation. Hardware/foreground keyboard layouts, accessibility
and mixed-DPI evidence remain required. Existing Mac and reciprocal gates remain
open, with no new shared-source build requirement introduced by this slice.

## Native activation and DPI foundation — prepared after checkpoint 03 freeze

Source inspection found that `Resources/ScreamSeq.rc` contained only the icon;
the app relied on a runtime PerMonitorV2 call and had no Common Controls v6
manifest dependency. `Resources/ScreamSeq.manifest` now supplies the native
activation context and initial DPI policy. `ScreamSeqNativeUiOptions` carries
that manifest input to the app, renderer probe and HWND test executables through
CMake. Engine, session, scanner, plugin and device targets do not inherit an
executable UI policy. The existing runtime DPI call remains a fallback.

This selects Windows' themed native controls and per-monitor layout behavior;
it does not introduce a custom macOS-like widget layer. The fragment retains
asInvoker privilege and does not alter filesystem paths, text encoding, project
storage, plugin identities or audio integration. CMake's Windows generator owns
manifest merging and embedding rather than a second manually assigned resource.

The native-control fixture now checks the initial PerMonitorV2 context before
the runtime fallback, verifies the Common Controls activation-context entry,
and requires the loaded library's reported major version to be at least six.
These are prepared assertions, not runtime evidence. Existing control pixel,
focus and geometry checks remain unchanged. If themed controls expose a sizing
or focus defect, fix the relevant layout or interaction rather than relaxing
the assertion.

This change is intentionally outside checkpoint 03's frozen source. Its next
cohesive Windows UI gate must inspect generated manifest inputs and the actual
app's embedded manifest, build the app and affected native harnesses on ARM64
and x64, and run retained-editor, native-control, command palette and workspace
geometry/focus checks. Foreground Windows theme, high contrast, Narrator and
mixed-DPI movement remain separate required observations. No Mac/shared source
changes require an extra Mac build for this slice; outstanding integration and
reciprocal project gates are unchanged.

Implementation references: [Windows process DPI policy](https://learn.microsoft.com/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process),
[application manifests](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests)
and [CMake's Windows MSVC generator support](https://github.com/Kitware/CMake/blob/master/Modules/Platform/Windows-MSVC.cmake).

## Consolidated checkpoint 03 — app links; workspace fixture compile failed

One Windows ARM64 configure/build ran on frozen integration commit
`c1c4f3387b60320fc2e31f08d3c6e34f029a9be6` from 2026-10-10 04:36:24 to
04:38:07 UTC. It began more than one hour after checkpoint 02 finished, used the
existing cache with two build jobs and selected 53 targets. The source receipt
confirms unchanged inputs throughout. The shared `ScreamSeqSession` library and
`ScreamSeq.exe` linked successfully. Workspace compilation then failed with two
C2664 diagnostics: `SongPropertiesApplicationTests.inc` passed temporary
`std::wstring` values to the existing `rawDraftText(HWND,const wchar_t *)`
fixture helper. Both calls now pass `.c_str()` for the synchronous helper call;
the fixture expectations and production song-properties code are unchanged.

Retained evidence in the integration checkout:
`bin/parity-evidence/integration-checkpoint-03.json` and
`bin/parity-evidence/integration-build-03.log`. Build exit is 1. Log SHA-256 is
`1e07d2a7032b90e5451d39d943e0fa207fb56aa840cd0aa3126dc86708174279`;
the linked app SHA-256 is
`cae08087af5d0e0819ec74eebc6d1355ac23f22a103eec695104f6c34e673ef9`.
This is app compile/link evidence, not a runtime pass or completion of all
selected targets. The build stopped before reaching the prior audio-bus target,
so its prepared include fix is not newly qualified either.

The complete native/application test helper was not run against the incomplete
build. No immediate rebuild was started. The next normal local build is no
earlier than 2026-10-10 05:38:07 UTC (22:38:07 Pacific on October 9). Batch these
fixture corrections with the prepared native activation/DPI foundation and
other reviewed changes for that checkpoint. Reuse the linked app evidence only
for its exact source; the manifest changes invalidate executable/UI evidence
for the next candidate. Preserve every pending native, actual-pipe, x64, Mac and
reciprocal integration gate, with no relaxed assertions.

## Shared Windows control appearance — prepared during checkpoint 03 cooldown

Observed source gap: `NativeControls::comboItem` and `NativeReportList` honored
high contrast, but Main's and `NativeToolWindow`'s field colors were unconditional
dark RGB values. Main and retained owners also duplicated button rendering;
retained actions lacked pressed feedback. The common native button renderer now
uses normal, pressed, hover, disabled and retained-active state. Existing
`NativeControls::active` page state becomes visible through this renderer.
Windows focus-cue suppression is respected, DPI controls content/focus insets,
and drawing restores its DC state. Main's single-line native lists use the same
system selection/text roles in high contrast. Native controls still own input,
accessible native semantics, selection, type-ahead and click dispatch.

`NativeControls.hpp` owns the semantic field/action palette and drawing helpers;
`WorkspaceView.inc` and `NativeToolWindow.hpp` consume them. Top-level appearance
messages now reach registered native controls and nested retained owners on the
same UI thread. A forwarding guard prevents recursive enumeration. Unregistered
vendor children are not explicitly sent appearance messages. These paths repaint
without rebuilding layout, loading data or moving focus. Main and native tool
frames request normal system chrome when high contrast is active, both on
creation and on a subsequent appearance message. No system preference is changed.

Prepared native-control checks cover normal/pressed/hover/active/disabled colors,
visible and suppressed keyboard focus, system list/field roles and DC isolation.
The system-color branch is selected explicitly within a drawing fixture; this
does not constitute foreground high-contrast evidence. The retained native-tool
fixture sends appearance notifications through nested docked owners and requires
unchanged raw text, caret, selection, Undo, focus, layout count and input release
count. Existing theme/list/geometry assertions remain intact.

No build or test was run for this slice. Batch its validation with the activation
manifest and checkpoint 03 fixture fixes: Windows app/workspace, native-control,
native-tool, graph-curve/workflow, precise-note, short instrument dock, parameter
automation, mixer, arrangement/matrix, recording/recovery and palette harnesses;
then existing actual-pipe focus/draft/departure checks. This shared native-control
change warrants the broader retained-UI gate on both Windows architectures.
Failure broadens inspection of the relevant message/layout path, not tolerances.
Mac/shared musical code and storage are unchanged; retain the outstanding Mac
and reciprocal gates without an extra Mac build just for these Windows helpers.

Still open: custom canvas color semantics, rich formula text, specialized
multi-column/multi-line owner-drawn lists, the accessibility root and virtual
canvas elements, actual Narrator/keyboard-layout checks and foreground mixed-DPI
captures. This is a common-control foundation, not a claim that P3a or application
accessibility is complete.

## Formula and specialized list appearance — prepared in the same UI batch

`NativeRichText.hpp` now provides an explicitly plain-text-only color operation.
It updates RichEdit's single default character format and background without
selection replacement or history clearing. `FormulaWorkbenchWindow` applies it
after font changes and, through its retained RichEdit subclass, after native
appearance notifications. Applying it after the control's own message handling
prevents RichEdit's default theme handling from restoring an unreadable automatic
foreground. High contrast uses the current Windows Window/WindowText pair.
Ordinary mode retains Consolas and the existing dark formula surface.

The existing graph-curve typography fixture now explicitly exercises system
colors without altering OS preferences, forwards all three theme/color/settings
messages through the actual owner/control path, and verifies foreground,
background, code font, full draft snapshot, caret, scroll, focus and the original
single text Undo/Redo. These checks are prepared, not executed. Musical Apply
must remain uncalled. The helper rejects rich-text mode rather than flattening
a document's independent character formats.

Formula reference rows, plugin-library columns and precise-note hit rows now
consume a common selected/disabled/system list palette. Secondary text uses the
same system foreground as primary text in high contrast, retaining readability
on arbitrary user highlight colors. Native item geometry, selection, accessible
item strings and type-ahead are preserved. Formula snippets and plugin labels
draw ampersands literally. Shared saved-DC lifetime protects early-return and
exception paths; focus suppression is respected. The Instrument envelope page
indicator now uses system highlight in high contrast.

This extends the pending UI batch rather than creating another build cycle.
In addition to the previous gate, retain graph-curve formula typography,
precise-note hit-list, instrument short-dock and plugin-library draft/completion
checks. Source/diff inspection only has occurred. Actual Windows high-contrast,
Narrator, custom canvas semantics and cross-platform integration gates remain
open. No API, shared music, codec, native plugin state or Mac source changed.
