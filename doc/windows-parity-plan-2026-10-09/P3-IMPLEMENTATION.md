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
