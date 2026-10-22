# Shared capture: Mac AU intake continuation

Status: source prepared, **unbuilt and unqualified**. This temporary feature
branch follows the Windows candidate; it is not a separate platform integration
branch. Both platforms continue toward shared main. The Windows `313893b9f`
candidate and its qualification checkout remain unchanged and available for the
pending Computer Use/global-focus check. No build or CI was started for this work.
The next coordinated build cannot start before **2026-10-22 07:57:09 UTC**.

## Observed boundary

`mac/Bridge/TrackerSession.mm::collectPluginEdits` still consumes the legacy
id/value overload; `acceptPluginParameter` records at current telemetry while
playing. It does not yet use the shared retained-take lifecycle or the seven new
Windows `automation.recording.*` operations. This is a shared architecture/API
convergence task; Windows already has public Record/Stop/review/Keep/Discard.
Recipe-copy recording is not a Mac reference feature either.

`MacPluginBackend` forwarded VST3 notification fences but returned Unsupported
for AU. Its AU observer held a separate 1,024-event array, read/write cursors and
loss counter. Closing the editor deleted that observer and added its unread
events to the loss count. Consequently already-delivered values/timestamps could
not be drained after close. The observer itself belongs to the main run loop;
audio rendering must not consume it or allocate its storage.

## Prepared implementation

- `editor/hosted/PluginEditQueue.hpp` now provides a capacity-parameterized
  implementation. `PluginEditQueue` remains an alias with its exact existing
  8,192-event VST3 budget; AU uses 1,024. Fence/cursor/order semantics are shared.
- `mac/Audio/AudioUnitEditIntake.hpp` retains original delivered events, a
  monotonic saturating overflow counter and fixed cuts. This is owned by the AU
  backend, independently of its editor/listener. Its prepared storage is included
  in the provider's storage accounting; no VST3 queue size changes.
- `mac/Audio/AudioUnitHost.mm` forwards actual AU callback values/timestamps to
  that intake, retains unread events across editor close/reopen, and captures
  fences on the native main owner using the existing host-time domain. Event
  registration errors now fail editor attachment explicitly rather than silently
  providing incomplete observation. No new public API or project format is added.
- The existing process-local `mac/Tests/FixtureAU.mm` has an opt-in Cocoa slider
  which uses `AUParameterSet`, exercising the real AU notification API. Default
  multi-bus/audio fixtures retain their previous no-editor behavior. The fixture
  is never installed or registered outside its process.
- `mac/Audio/PluginWindow.hpp` and the owned `RSPluginEditorWindow` in
  `AudioUnitHost.mm` now provide a native-main-owner input lease. It gates normal
  events, key equivalents and user close on that specific host window, retains
  each native control's previous enabled state, and restores those states without
  making a window key or replacing its first responder. Active field-editor
  drafts, tracking/modal run-loop modes, mouse drags and attached child/dialog
  windows refuse admission before control mutation. Enumeration is bounded at
  8,192 views. These conservative admission rules still need ergonomic testing.
- Both AU and `mac/Audio/VST3Host.mm` implement the existing backend lease
  interface: exact provider/token identity, no nested lease, no editor opening
  while protected, retryable cleanup and explicit host-close cleanup. This does
  not discard programmatic vendor notifications; the shared take owner must
  retain/recheck unexpected input before publishing a restored baseline.

The notification API choice follows Apple's [Audio Unit view/event guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnitView/TheAudioUnitView.html)
and [AUParameterListenerNotify reference](https://developer.apple.com/documentation/audiotoolbox/auparameterlistenernotify(_:_:_:)).
The test is intended to qualify actual delivery, not infer it from the API name.

## Required execution at the next checkpoint

1. Shared `recorded-automation` runs the complete queue ordering, fixed-prefix,
   overflow/wrap, allocation and concurrent producer/consumer checks at both
   capacities. The original 8,192-event assertions remain intact.
2. Mac `plugin-robustness` adds AU-budget overflow/loss retention, unknown original
   timestamps, fixed-cut suffix preservation, stale cuts and allocation/lock
   checks. This is a pure intake check, not proof of AU listener delivery.
3. `native-plugin-tests ... --ui` exercises actual AUParameterSet/listener delivery,
   original-time retention, fixed cut versus later input, close/reopen with unread
   events, preserved native value, exactly-once drain, and provider replacement.
   The existing VST3 custom-slider-to-audio test also runs. The ordinary CTest
   invocation lacks `--ui`; the Mac CI workflow now explicitly schedules this
   bounded 120-second owned-editor check from the same build, retaining its log.
   This is not a physical display or Core Audio loopback qualification.
   It also protects actual AU/VST3 fixture windows, checks native close refusal,
   unchanged values/geometry/focus, native control reenable, and original-time
   programmatic delivery while protected. The ordinary native-plugin test covers
   closed-editor leases for both providers: wrong/foreign/stale tokens, nested
   refusal, blocked opening, retryable release and explicit-close cleanup.
4. The shared queue header is a Windows provider input: rebuild the relevant
   Windows host/recorded-automation targets once, retain real VST3 notification,
   audible latest-value, overflow and callback-allocation assertions. Do not
   reuse the old binary as proof of this changed shared header.
5. Both native Mac architectures must compile and run the changed AU path.
   Existing native provider/PCM tests cover the storage-accounting change and
   unchanged audio/property behavior. API inventory/project format are unchanged;
   the final paired history/persistence/exchange gate still remains required.
6. `plugin-window-geometry` retains its existing geometry assertions and adds
   owned, offscreen native input checks: key dispatch and key equivalents before,
   during and after a lease; independent-window input; enabled/disabled controls;
   close notification; raw text/focus retention; and refusal of an active field
   editor or child window. This exercises AppKit directly, not OS-level input or
   Computer Use. Live keyboard/IME and real-vendor behavior remain separate gates.

Cheap checks so far are source/diff review only. No compilation, native test,
new AU runtime observation, or passing CI is claimed.

## Remaining work and limits

A fixed notification cut alone is not an exclusive native input lease. AU events not
yet delivered by Apple's listener when it is disposed are outside this queue's
retention guarantee. The newly prepared native AU/VST3 barriers are unqualified;
they protect the existing host window, not arbitrary programmatic vendor activity
or a separately created vendor top-level window. Late vendor windows/input must
be accounted for by the session's admission/recheck policy before enabling this
lifecycle publicly. Fixed release cuts, manual/vendor-state restoration, retained recovery and Mac session/API/UI
integration are still required before exposing the shared take there. Merely
adding the seven API names would not preserve these semantics.

In particular, the legacy Mac consumer still chooses record-versus-manual
behavior at poll time. This source is a dependency of the lifecycle migration,
not evidence that close/stop races, original-time musical placement or complete
Mac recording parity are solved. Keep that end-to-end migration and its tests
on this branch before claiming or releasing complete shared recording behavior.

The final migration should delegate capture/timing/validation/recovery to the
existing shared `RecordedGestureTake`, `RecordedGestureCapture` and recovery
types. AppKit input ownership and Core Audio/provider adapters remain native;
no cross-platform UI/audio framework, stable-ID rename, plugin-state rewrite
or format migration is proposed.
