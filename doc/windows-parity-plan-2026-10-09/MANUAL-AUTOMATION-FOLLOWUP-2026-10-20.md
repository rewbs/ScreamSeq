# Manual automation recording: remaining A13/P4b work

Source audit at candidate `8c750f7d61966b3dc125ffaf49c34e8678697a25`.
This is not new implementation or a runtime pass. Keep the current CI repair
ahead of this larger shared/native change.

## Current behavior and missing boundary

Windows already has `automation.recorded.get/edit`,
`AbsoluteAutomationWindow`, parameter provenance/activity and live publication
of edited recorded points. `PluginOperations::commit/history` distinguishes
recorded-only changes and uses its prepared publication callback;
`DocumentController` supplies that callback. Existing
`LiveGraphPublicationTests.inc::liveRecordedAutomationTests` exercises edits,
queue refusal and history. Do not duplicate or remove this live path.

The missing path is **capturing manual gestures while playing**:

- `PluginParametersWindow` drives transient
  `plugin.parameters.preview.set/commit/cancel`. Its worker adapter commits only
  the final accepted manual value through `plugin.parameters.set`.
- `PluginOperations::flushEditors` consumes vendor edits, coalesces the current
  values by stable plugin/parameter, publishes them live and captures vendor
  state after the gesture settles. It passes the existing recorded-automation
  collection unchanged to `commit`. Coalescing is appropriate for current
  controls but cannot reconstruct a recorded gesture.
- `WindowsVST3.cpp::Impl::Edit` currently carries only parameter ID and value;
  the audio/UI queues each have 8,192 entries. `popEdit` returns no event time.
  Recording at a later worker poll would include worker scheduling delay.
- The inspected Windows rack parameter controls, plugin owner, preview adapter
  and public method inventory contain no gesture-record arm/capture path.
  The existing MIDI note take and microphone recorder are separate owners.

Mac `PatternAutomation.addRecordingControls` exposes “Record gestures” and a
clear-all command. `main.swift` passes the record flag to native parameter edits;
`AutomationIntegration.swift` passes it to vendor edit collection.
`TrackerSession::acceptPluginParameter`:

- captures only when armed and playing;
- rejects conflicts with enabled pattern lanes and parameter commands;
- bounds the existing collection at 100,000 points;
- records the current audio telemetry frame converted to canonical 48 kHz;
- keeps `_manualParameters` separate during recording;
- groups a native gesture or debounced vendor gesture in chronological history.

`clearAutomation` is an existing native Mac method. The shared public API
inventory does not expose gesture arm/clear semantics, so API parity needs work
on both platforms. The current Mac telemetry sampling is not evidence of
timestamp-accurate vendor callback capture; measure it before promising that.

## Proposed shared/native boundary

Introduce a bounded, platform-independent recorded-gesture candidate/owner in
`editor/`, alongside `ParameterPreviewOwner` and the existing automation
validation/publication types. It should accept stable plugin/parameter identity,
document revision, playback generation, canonical frame and native-unit value.
It should not own native windows, an audio device, vendor code or filesystem
state.

The shared candidate owns conflict validation, capacity/overflow, duplicate
frame policy, identity validation and the complete before/after point set.
Adapters retain native input timestamps, clock conversion, host publication,
opaque plugin state and the unified history commit. Reuse the current
`RecordedAutomationPlan` publication and recorded-only history paths.

Do not equate `RecordingClock` with an absolute-frame clock: its published
locations are sequence/order/pattern and precise-row position. Windows
`HostedProjectPlayback::render` already tracks callback generation and host-time
coverage, but a recorded parameter event needs a coherent absolute-frame mapping.
Inspect that mapping and the Mac telemetry path before selecting or extending
the shared clock representation. Playback restart/seek/device reset must retire
old mappings; a missing mapping must be reported rather than replaced with “now”.

Retain the current independent unmodulated manual baseline. Recording should not
serialize a transient/effective modulation value into plugin state. Stable IDs
must be resolved again when a pending point is published; rack reordering must
not retarget captured events.

## Proposed implementation slices

1. **Shared preparation and clock contract.** Define frame conversion, same-frame
   event order, stale generation, conflict and capacity behavior with controlled
   timestamps. Reuse existing canonical 48-kHz persistence. Do not add a new
   project version merely for an in-memory capture owner.
2. **Both host adapters and public controls.** Add a narrow arm/status/clear API
   after naming its exact contract in the schema/guide. Arming is transient
   session state; accepted captured music belongs in normal chronological
   history. Clearing recorded music is one reversible edit. Keep explicit
   current document/target guards and terminal request replay.
3. **Native and vendor gesture ingestion.** Windows native sliders and vendor
   queues must supply accepted events without using the worker's poll time as
   their original event time. Preserve the existing queue's live-audio delivery
   and bounded producer/consumer ownership. Upgrade Mac ingestion where the
   same timing evidence requires it.
4. **Connected native UI.** Expose “Record gestures” beside the parameter/
   automation controls, a visible armed/recording state, a route to captured
   points, and a clearly named Clear recorded automation action. Do not create
   a second Apply requirement for normal finished gestures.
5. **Lifecycle and history.** Specify and check release, cancellation, Stop,
   queue refusal, document departure, vendor closure and device loss before
   integrating UI. Do not discard a captured point set or redo branch on an
   uncertain completion. Gesture batching must not merge unrelated subsequent
   edits, and recoverable work must participate in departure protection.

The precise arm API names and whether Stop finalizes an unfinished gesture are
implementation decisions to resolve against the existing native gesture/Stop
contracts, not invitations to silently alter those contracts.

## Required evidence and bounded validation

- Controlled 44.1/48/96-kHz callback mappings, delayed worker polls, equal-frame
  events, seeks/restarts, stale generation and missing coverage; assert canonical
  positions, monotonicity/order and the existing strict error limits.
- F04's enabled Gainer lane must block conflicting capture before points/history
  change. Disable it in a disposable derivative to record a known gesture, then
  check the independent manual value, exact point times/values, one Undo/Redo,
  clear-all Undo, save/reopen and reciprocal playback.
- Native-control and redistributable vendor fixtures, including queue saturation,
  read-only/disappearing parameter, reordering/removal, closure and opaque-state
  changes. Record expected event loss/overflow explicitly; unavailable provider
  cases are skips.
- Reuse current recorded-edit publication and snapshot tests where inputs are
  unchanged. Run cheap shared candidate/clock checks first, then one coordinated
  native build batch with targeted history, actual endpoint and offline
  128/511-frame partition checks. Callback/queue changes require the realtime
  skill audit and controlled audio evidence; a stopped point-editor pass is
  insufficient.
- Foreground controls, physical audio/MIDI and the complete A13/A18 journeys
  remain separate gates. No current Windows pass establishes manual capture.

Relative effort: **L**, with shared preparation/timing, host ingestion and native
controls kept as reviewable slices that converge on main.
