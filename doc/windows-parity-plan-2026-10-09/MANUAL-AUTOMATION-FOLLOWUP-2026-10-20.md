# Manual automation recording: remaining A13/P4b work

**Current-source/evidence update (20 October):** the frame-clock and original-time
notification infrastructure is in built candidate `28d757f65`. Windows x64
`plugin-edit-notification` and `recorded-automation-tests` passed; Mac Apple
Silicon `native-plugins`, `recorded-automation` and `plugin-robustness` passed.
These are bounded infrastructure checks. Windows gesture capture and the Mac
poll-time consumer replacement remain unimplemented. The newer
[`automation.recorded.clear` batch](RECORDED-CLEAR-BATCH-2026-10-20.md) is
implemented in source but unbuilt. The historical source-only checkpoints
below describe their original date/state.

Latest source-only slice:
[original-time vendor notifications and loss counters](PLUGIN-EDIT-TIMESTAMPS-2026-10-20.md).
Consumers are not switched to capture from those timestamps yet; the new checks
remain unbuilt.

Newer source-only follow-up:
[shared absolute frame clock and native adapters](RECORDING-FRAME-CLOCK-2026-10-20.md).
This does not complete event ingestion or Windows capture; its tests are unbuilt.

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
- At the original audit, `WindowsVST3.cpp::Impl::Edit` carried only ID/value.
  The current timestamped `popEdit(PluginEdit&)` preserves callback time and
  exposes loss counts, but `flushEditors` still uses the legacy ID/value reader.
  Recording at its later worker poll would still include scheduling delay.
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

`clearAutomation` is an existing native Mac method. The new source exposes
`automation.recorded.clear` on both APIs; gesture arm/capture semantics still
need work on both platforms. The current Mac telemetry sampling is not evidence of
timestamp-accurate vendor callback capture; measure it before promising that.

## Current adapter boundaries rechecked

The follow-up source audit at `fc5d586ad` confirms four integration obligations
before adding a Windows Record toggle:

1. `windows/Session/PluginOperations.cpp::flushEditors` consumes each vendor
   event once, coalesces per target for live delivery, and later serializes the
   editor's opaque state into the manual baseline. Recording must intercept
   original events before coalescing and must not run the ordinary opaque
   capture for a recorded gesture. Its pending live-publication retry behavior
   must remain intact.
2. `PluginOperations::overlayRecoveryState` currently reads retained editor
   state without consuming its events. A recorded gesture needs a separate
   recoverable take representation; simply adding an armed flag to
   `flushEditors` would still let recovery serialize movement as manual state.
3. `windows/App/PluginParametersWindow.hpp::previewSlider` and
   `Session/ParameterPreviewOperations.inc` retain a single transient value.
   Release calls the ordinary manual edit. Recording needs the original native
   control timestamps and accepted sequence, with cancellation/release and
   queued-completion ownership retained. The worker's request time is not a
   substitute for the original UI event time.
4. Mac `collectPluginEdits` drains up to 128 legacy ID/value events per slot;
   `acceptPluginParameter` uses current telemetry only while playing. A delayed
   Stop drain therefore needs the retained original playback timeline and
   explicit capture ownership, rather than relying on the current playing flag.

Both native adapters already expose the absolute mapping:
Windows `Audio/PresentationClock.hpp::RecordingTimeline::absoluteFrames`
and Mac `AudioDevice::recordingFrameTimeline()`. Bind it to the original
document, playback and provider incarnation; do not infer the provider's
incarnation from a stable project ID or subtract loss counters across a vendor
replacement. Keep the existing independent MIDI-note take separate. Its
[ten-case device follow-up](DEVICE-FOLLOWUP-2026-10-20.md) does not qualify
plugin gesture recording.

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

## Capture admission slice (20 October, source only)

Further source inspection found a concrete shared-boundary mismatch.
`TrackerSession::collectPluginEdits` may drain 128 notifications in one poll;
`acceptPluginParameter` previously appended every one using the current
telemetry frame. Multiple edits of one parameter in one frame could therefore
produce duplicate `(slot,id,frame)` points. Meanwhile
`editor/hosted/RecordedAutomation.cpp::prepareRecordedAutomation` explicitly
rejects those duplicates. Mac's existing control-period timestamping also does
not establish original vendor-event timing.

`editor/hosted/RecordedCapture.hpp` now prepares a destination without mutating
the song: last accepted value at the same target/frame replaces that point;
an identical value is a no-op; a new frame appends subject to the existing
100,000-point limit. Replacement is allowed at capacity. Ambiguous already-saved
duplicates at the edited time reject with a lane-edit instruction; unrelated
records/order remain untouched. The owner resolves stable identity and catalogue/
conflict guards before preparation, reserves append storage before host
publication, and adopts on the same serialized owner. There is no callback
allocation or new history/format owner.

The helper also converts integer device frames to canonical 48 kHz by quotient/
remainder arithmetic, avoiding floating rounding and overflow at ordinary
integer rates. Fractional Core Audio rates remain supported with checked
conversion. Invalid rates, non-finite values and times outside the existing
one-week bound reject before narrowing.

The Mac capture adapter uses this preparation before its existing queue/history
admission, retains the independent manual baseline, and replaces/appends only
after the live parameter queue accepts. Identical captured values add no musical
history. This fixes same-frame capture admission; **it still samples telemetry
at collection time and does not close the timestamping gap**. Windows manual
capture is still absent; its future adapter should consume this same helper.

Prepared checks in the existing shared `RecordedAutomationTests.cpp` cover
44.1/48/96-kHz and fractional rates, invalid/overflow input, exact capacity,
same-frame replacement/no-op, preservation of other targets/times and rejection
of ambiguous old duplicates. A coalesced gesture also enters the real shared
timeline and is rendered under the existing callback audit. Both native CMake
projects already schedule this target. These checks and the Mac adapter have
not yet been compiled or executed; Mac gesture history/opaque-state and provider
recording gates remain necessary.

### More precise clock integration boundary

- `editor/TrackerDocument.cpp::nativePrepareMix` already publishes host-time
  coverage for MIDI positions through `RecordingClock`. It has both
  `renderOffset_` and the renderer's cumulative `frames_`; these provide the
  source for an absolute-frame mapping alongside the musical-position mapping.
  Do not infer cumulative frames from row position, especially across loops.
- Windows `Session/HostedProject.cpp::render` supplies the WASAPI presentation
  origin and device generation, bounds valid coverage only after successful
  processing, quarantines failed/discontinuous clocks and publishes Stop's final
  bound through `Audio/PresentationClock.hpp::RecordingTimeline`.
- Mac `Audio/AudioDevice.mm::callback` supplies `mHostTime` to the same renderer,
  but has no equivalent retained absolute-frame/generation capture view. Add
  explicit lifecycle bounds before relying on this path for delayed events.
- Windows VST3 `performEdit` runs on its required STA owner, publishing separate
  8,192-entry UI/audio queues. Its UI record must acquire the original native
  host timestamp at acceptance. Preserve the audio queue's current delivery and
  queue-full failure; do not substitute a worker timestamp in `flushEditors`.
- Windows `flushEditors` currently folds consumed records into maps and captures
  opaque state after 400 ms. Capture must retain timestamped events before that
  fold, keep failed-publication work pending and distinguish unrelated opaque
  preset changes from parameter-only gestures. Existing live manual delivery
  must continue while capture is armed.

These findings refine the next implementation slice; they do not authorize
calling clock accuracy, Windows capture or A13 complete.
