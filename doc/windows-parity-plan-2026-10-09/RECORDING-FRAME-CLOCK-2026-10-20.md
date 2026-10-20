# Recorded parameter event-time mapping

Status: **implemented in source, unbuilt and unexecuted**. This continues A13/P4b;
it does not yet enable Windows manual recording or replace Mac's poll-time
parameter ingestion. Parent `9c8cea6b2` includes the separately unbuilt historical
snapshot fixture correction. Frozen qualification candidate `54fa2babc` does not
contain this clock.

## Boundary and ownership

`editor/RecordingFrameClock.hpp` publishes actual presentation intervals with
their absolute renderer-frame origins, device rates and stream generations.
One audio writer uses a fixed 4096-slot atomic ring; control readers convert the
original event timestamp to canonical 48 kHz frames. Versioned atomic payloads
prevent torn reads when a slow reader overlaps ring replacement. No fallback
uses the time of polling. Memory is allocated and retired off the callback.
Retention is bounded in callback chunks, not guaranteed seconds.

`RecordingFrameTimeline` adds lifecycle bounds. Stop retains earlier coverage
and trims its exclusive end when supplied a presentation stop time. Natural
completion retains the already-queued musical bound. A new increasing generation
cannot expose prior-generation coverage. Close before the first valid callback
prevents late publication of that stopped generation. Invalidating a failed
timeline is terminal: a replacement playback must own a new timeline. Clock
boundary regression, backwards musical frames and an unannounced rate change
invalidate coverage. Forward holes remain unmapped. Correlation boundaries may
overlap slightly; newest retained presentation intervals win those overlaps.
Actual host-time quantization/correlation error remains an adapter qualification
requirement, not a promise of sub-tick accuracy.

Publish is audio-writer-only; control-thread close/invalidate requires a
quiescent writer. Readers can retain the timeline after device/renderer teardown.
Generation values are never reused for a different stream on a retained object.
No project field, schema, musical edit or history representation changes here.

Windows `HostedProjectPlayback::render` samples renderer frames before each
chunk, publishes only the successfully rendered musical portion after plugin
processing succeeds, and excludes sample auditions. `RecordingTimeline` retains
the new absolute clock beside the existing MIDI musical-position clock;
`locateFrame` also checks the existing outer stream bounds. Processor failure
and offline latency invalidation retire absolute coverage. WASAPI presentation
correlation remains in `PresentationClock`.

Mac `AudioDevice` prepares a new retained clock for each playback. Its callback
publishes successful musical frames against Core Audio host timestamps, excluding
preview/isolated-sample playback and tail-only audio. Renderer faults, processor
failure and failed device startup invalidate it. Stop and natural completion
retain the appropriate bounds. `recordingFrameTimeline()` is the control-owner
handoff for the future timestamped input queue. No callback ownership is moved
into shared code; Core Audio and WASAPI stay native.

## Added checks, not executed

`editor/Tests/RecordingFrameClockTests.inc` runs inside the existing
`recorded-automation-tests` executable on both platforms:

- Exact controlled 44.1/48/96-kHz mappings and whole/split callback agreement.
- Fractional rate, invalid intervals/rates, overflow and canonical week bound.
- Delayed lookup, missing intervals, wrong/retired generation, restart, explicit
  Stop, Stop before initial mapping and natural completion.
- Terminal invalidation, regressed boundaries/frame origin and changed rate.
- Ring eviction and concurrent writer/reader overwrites with exact accepted
  answers; unavailable overwritten answers remain explicitly unavailable.
- Existing platform realtime allocation/free audit; Mac also audits locks.
  Windows audit remains host C++ allocations/frees, not vendor/malloc/locks.

`windows/Tests/RecordingControllerTests.inc` adds actual prepared-renderer
assertions for absolute origin, stale generation, exclusive final bounds and
delayed lookup after Stop, alongside the existing MIDI take assertions.

## Next coordinated gate and remaining implementation

Do not build this slice separately during cooldown. Combine it with related
capture ingestion/ownership changes where reviewable. The last local build ended
08:33:39 UTC; local lower bound is 09:33:40 UTC, extended as necessary by the
current coordinated remote build boundary. First run the shared recorded
automation executable and Windows recording-controller group after the next
native build. Both Mac architectures must compile the callback adapter and run
the shared suite. Expand to timing/hosted-render/device tests for any correlation
failure; preserve strict existing PCM and timestamp assertions.

Outstanding A13 work remains: original timestamps in vendor/native event queues,
bounded loss reporting, shared capture owner/conflict admission, public arm/status/
clear operations on both adapters, native Windows controls, chronological gesture
history, Stop/cancel/departure behavior, and F04 conflict/gesture/Undo/save/reopen
and reciprocal PCM checks. Recipe-copy capture scope must be explicit.
Core Audio clock discontinuity/device-loss handling needs native runtime evidence.
Source and controlled fixtures alone cannot qualify vendor timing, audible device
behavior, or foreground ergonomics.
