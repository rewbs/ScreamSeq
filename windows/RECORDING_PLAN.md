# Windows live recording implementation plan

Read-only source audit, 2026-10-07. This is a continuation plan, not a completion claim.

## Current boundary

Windows recovery is implemented: private immutable snapshots, atomic disk generations,
guarded APIs, startup/browser restore, unsaved recovery and source preservation.
See `App/RecoveryIntegration.inc`, `Project/RecoveryStore.cpp`,
`Session/RecoveryOperations.inc` and `App/RecoveryWindow.hpp`.
Imported `recoveryTake` data survives native open/save, including compatibility
provenance and unknown fields. It is not yet hydrated into a stopped, inspectable,
committable take. Live MIDI input and the six `recording.*` APIs are still absent.

The shared `editor/NoteRecording.cpp` is already compiled by Windows CMake.
`editor/RecordingClock.hpp` and `Tracker::Renderer::recordingTime` already provide
the shared timing model. Windows does not currently supply render host timestamps:
`Audio/WasapiDevice.hpp::RenderCallback` passes only frames, and
`Session/HostedProject.cpp::render` calls the renderer without `recordingTime`.

## Smallest complete workflow

Provide one retained native **MIDI & recording** window: source and Rescan, Arm,
selected sound, adjacent note-column count, timing grid, input adjustment, take
state/counts, Finish, Discard and a bounded event review list. Add corresponding
command-palette/menu actions and a visible recording/retained-take indicator.
Use native controls and the current document worker; retain drafts and focus
across asynchronous enumeration, reads and failed operations.

Match `mac/App/main.swift::midiSettings/handleMIDI` and
`mac/App/PreciseNotesIntegration.swift`: armed playback starts a take only after
successful Play; stopped armed MIDI performs explicit cursor step entry.
Stop drains pending input, closes held notes and attempts one guarded commit.
A failed finish remains stopped and reviewable until explicit retry or discard;
do not retry automatically every frame or silently rebase its revision.
Review should show pattern/track, position, note/release and velocity, plus losses,
with navigation to existing precise-note editors for explicit manual placement.

Mac typing currently edits ordinary cursor cells and auditions; it does not feed
the timestamped take (`PatternView.swift::keyDown`, `main.swift::audition`). Windows
`App/MusicalTyping.inc` has the same separate step-entry/audition behavior.
Preserve that default. If computer-keyboard live recording is included, expose an
explicit input mode, route accepted keys into the same take, and retain local text
ownership and physical-key release generations. Do not reinterpret Live keys
silently or mix keyboard/MIDI held-note identities without a defined source rule.

## Shared musical and API contract

- Reuse `Tracker::NoteRecording`, `PreciseNote`, stable native pattern/track IDs,
  65,536 units per row and the existing maximum of 65,536 events.
- Port `mac/Bridge/RecordingAPI.inc` and its `TrackerSessionAPI.inc` description:
  `recording.start/get/capture/stop/commit/discard`. Writes require current
  `expectedRevision`; operations after start require the opaque current take ID.
- Start pins document identity, base revision, distinct raw note columns and sound.
  Quantization is 0..65,536 units; positive `latencyMS` places input earlier,
  bounded to -500..500 ms. No implicit cursor retargeting.
- Capture accepts at most 1,024 events per batch. Parse decimal uint64 timestamps
  and all status/note/velocity fields before mutating anything; stable-sort by
  timestamp. Accept note on/off and CC120/123 for take capture. Report missing
  time, voice exhaustion and overflow; never substitute cursor position.
- Get returns bounded events, take/base revision, capture state, counters and
  decimal `hostTime`. Poll summaries for UI; do not serialize all events each frame.
- Stop closes held notes. Commit requires a stopped compatible take, validates
  the complete native candidate and all row edits, then applies one document Undo.
  Dry run retains the take. Actual successful commit/discard consumes it explicitly.
- Preserve Mac merge semantics: equal pattern/track/time/kind events use the latest
  value; `replaceRows` replaces precise notes in touched rows and clears only note,
  instrument and ordinary volume fields, retaining unrelated tracker commands.
  Prefer extracting this pure preparation from Mac `RecordingAPI.inc:34–57` into
  a shared helper used by both frontends if changing the merge implementation.
- Take lifecycle/capture changes do not manufacture musical revision/history edits.
  Preserve adapter request-ID replay rules, no-op/dry-run replies and whole-request
  rejection. A stale take remains inspectable and cannot be committed by rebasing.

## Windows timing and device requirements

Use one documented Windows host clock, preferably QPC converted to 100 ns units.
Obtain correlated device position/frequency and QPC from `IAudioClock` to calculate
the presentation origin of each submitted buffer. Account for the initial silent
buffer primed by `WasapiDevice.cpp::stream`. Reject inaccurate/invalid mappings;
handle startup zero, starvation, clock regression, Stop, restart and endpoint
changes with explicit stream generations. Keep plugin latency handling separate.

`HostedProjectPlayback::render` slices callbacks at 4,096 frames while shared
`Renderer::render` resets `renderOffset_` on every call. Advance the host origin
for **every slice**, including callbacks larger than 4,096 frames. Clock history
must remain valid for delayed control-thread input lookup without stale renderer
pointers during preparation, shutdown or latency maintenance.

A native WinMM adapter is a small compatible first implementation. Discover and
connect off the UI/audio threads. Retain driver timestamps, anchor their epoch to
the advertised host clock, and account for millisecond precision and rollover.
WinMM ordinals and display names are not stable identities: use opaque device
interface names, revalidate on connect, and never reconnect to a different device
after enumeration changes. Offer disconnected input; do not promise a virtual
Windows MIDI port without an installed provider.

Callbacks only enqueue bounded data: no allocations, locks, UI, logging or driver
lifecycle calls. Reject late callbacks from retired connection generations.
On loss/overflow, quarantine the affected pending batch and release held voices;
do not panic and immediately replay old note-ons without matching releases.
Forward ordinary MIDI CC to the existing `PluginChain::graphController` path,
matching Mac input routing; only supported note/control events enter the take.
Device preferences are separate from song Undo. Any new MIDI settings APIs should
be explicit Windows extensions, following the existing audio-settings service.

## Recovery and session lifecycle

Extend `Session/RecoveryOperations.inc` to copy the live take, close held notes in
that copy at a valid clock position, and overlay the existing `recoveryTake`
compatible/events/counters dictionary. Never mutate or stop the live take while
capturing recovery or overlay live plugin state into its baseline.
`recoveryTimer` must consider an unfinished take even when song dirty/revision are
unchanged; snapshot fingerprints must change when take events change.

Native open/restore must validate the whole candidate and hydrate a fresh stopped
take ID. `compatible:false` stays noncommittable; Undo or matching geometry must
not silently revive compatibility. Keep imported opaque fields and provenance.
Commit/discard removes the retained take wrapper so later saves cannot resurrect
it. Normal Save/Open/close and recovery replacement must protect an unfinished
take through explicit Finish/Discard, matching Mac `TrackerSession.mm` and
`RecoveryIntegration.swift`; automatic recovery remains available while recording.

## Ownership and gates

1. Audio/device: `Audio/WasapiDevice.*`, `Session/HostedProject.*`, new
   `Audio/MidiInput.*`; correlated clock, input lifecycle, bounded queues/tests.
2. Document: new `Session/RecordingOperations.*`, `DocumentController.*`,
   `RecoveryOperations.inc`; API transactions, take state and immutable snapshots.
3. UI/root integration: new `App/MidiRecordingWindow.hpp` and
   `App/RecordingIntegration.inc`, Main/commands/context/transport hooks.
4. API/qualification: `Api/SessionAdapter.hpp`, schemas, native and isolated actual
   application tests, source/executable hashes and native visual evidence.

Port Mac `PreciseNoteTests.mm::recordingTest`, `test_automation.py:1314–1331`,
`RecoverySessionTests.mm` and `test_recovery.py` take cases. Add full-batch rejection,
replay, stale/repeated takes, same-revision autosave, failed finish retention,
capture-versus-Stop ordering, source/focus changes, queue overflow, device reorder,
clock expiry/discontinuity, delayed delivery and >4,096-frame callback cases.
Render onset/release fixtures at 44.1/48/96 kHz and varying partitions; qualify
sample and plugin instruments, one Undo/Redo and exact save/reopen. Run the host
allocation/free/lock audit. Use private desktops/stores and explicit silent owned
fixtures; physical MIDI timing/hotplug and reciprocal Mac take reopen remain
separate release gates when hardware or a Mac runtime is unavailable.

Primary Windows clock/device contracts:
[IAudioClock::GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition),
[MIM_DATA timestamps](https://learn.microsoft.com/en-us/windows/win32/multimedia/mim-data),
[opaque device interfaces](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/obtaining-a-device-interface-name).
