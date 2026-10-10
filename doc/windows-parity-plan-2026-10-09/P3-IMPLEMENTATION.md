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
