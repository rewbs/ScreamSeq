# Original-time plugin edit notifications

Status: **source implemented; native builds and tests pending**. This is another
A13 infrastructure slice after the shared frame clock. It does not enable
Windows gesture recording and does not replace Mac's existing poll-time capture.
The frozen `54fa2babc` / `d79321c7c` qualification binaries contain none of it.

## Shared contract and native implementation

`editor/hosted/PluginTypes.hpp::PluginEdit` carries parameter ID, catalogue-domain
value and an original native host timestamp. The value is not renormalized.
Timestamp zero explicitly means unavailable. This is transient host data, not a
project-format field or a new musical operation.

`PluginBackend`, `NativePlugin`, `PluginChain` and Mac `AudioDevice` expose the
timestamped overload alongside the existing `(id,value)` reader. Legacy backend
implementations default to zero time; existing consumers retain their behavior.
The chain checks its publication queue capacity before popping a vendor event,
then performs the same existing audible/manual bookkeeping. It does not replace
a timestamp with drain time. The shared frame timeline can subsequently map the
native time, once a capture owner binds the correct retained playback epoch.

- Windows VST3 stamps accepted `performEdit` callbacks using the same QPC-derived
  100 ns domain as WASAPI presentation correlation. The separate UI/audio
  8,192-entry queues retain their capacities, order and failure policies. Only
  the UI notification carries a timestamp. Overflow, invalid values and
  unknown/nonwritable parameters increment a control-readable atomic loss
  counter. The existing first-failure diagnostic remains authoritative:
  callbacks after terminal failure are rejected without exhaustive loss counts.
- Mac VST3 stamps accepted callbacks with `mach_absolute_time()`. Its existing
  8,192-entry UI queue, latest-value audible coalescing, normalization and loss
  policy remain. The timestamp is retained while notifications wait to drain.
- Mac AU preserves the event listener's supplied host time, including zero.
  Its main-run-loop-owned 1,024-entry queue now counts overflow. Closing the
  editor accumulates both overflow and queued notifications discarded with the
  listener; reopening does not reset the backend's loss count. This reports an
  existing loss boundary rather than inventing captured points on closure.

Loss counters belong to a **backend incarnation**, not the stable project plugin
ID or retained facade. An opaque vendor replacement starts a new counter. The
future capture owner must retire/bind that epoch explicitly; subtracting counts
across replacements would be incorrect. AU readers stay on the existing main
run loop; no cross-thread safety is claimed for its listener fields.

`windows/Plugins/Provider.cmake` now supplies the small `ScreamSeqClock` static
library so both the main native build and standalone provider qualification link
the clock. The main project adds its normal build options to that target.
There is no UI/audio framework dependency and no persistence/schema change.

## Prepared checks and bounded next gate

No build, CTest run or runtime execution occurred for this source slice.
`git diff --check` is the completed whitespace check, not compilation evidence.
Respect the existing **10:03 UTC** earliest coordinated build boundary and batch
this with the pending clock, snapshot, Mac stable-ID/opaque-data, and Windows
integer-recovery fixes.

1. New root Windows worker target `plugin-edit-notification-tests` uses the real
   VST3 fixture without hardware or visible windows. It checks original callback
   time through the shared rack, a delayed drain, legacy overload compatibility,
   exact 8,192-entry capacity/order, observable overflow, invalid-value rejection,
   preserved first-failure reasons, accepted/fault PCM and host C++ allocation/
   free auditing. CTest name: `plugin-edit-notification`; the grouped worker
   target and CI pick it up. Direct malloc, locks and vendor allocations remain
   outside this Windows audit.
2. Existing standalone ARM64 `vst3-native` and native `vst3-Editor` checks now
   retain callback-time assertions across delayed reads and close/reopen.
   They are additional provider targets, not implicitly covered by root CTest.
   The editor check requires an owned desktop; do not use the musician's desktop.
3. Mac `plugin-robustness-tests` preserves its existing 40,003 accepted-event
   burst, exact loss arithmetic, normalized values, latest-value PCM and
   allocation/free/lock audit, adding bounded original timestamps and monotonic
   queue order across delayed drain. `native-plugin-tests` adds timestamp/loss
   assertions through the real custom-editor slider and shared facade.
4. Both Mac architectures must compile the AU/VST3 adapters and run the shared
   frame-clock tests plus the focused native provider checks. AU original-time
   quality, listener coalescing and overflow/closure require a native AU event
   fixture; ordinary Apple editor attachment alone does not qualify those cases.

Reuse the already completed frozen fixture exchange for its exact inputs; it
does not replace the final same-candidate cross-platform gate. A timestamp,
ordering, queue or audio failure expands validation to the related provider
lifecycle and recorded-automation/render suites. Do not relax existing
assertions to obtain a pass.

## Remaining integration

Current consumers still use the legacy overload: Windows
`PluginOperations::flushEditors` coalesces manual values; Mac
`TrackerSession::collectPluginEdits` still samples telemetry for recording.
Both must later retain original events before coalescing, bind stable target and
playback/vendor generations, report missing clock coverage/loss, and preserve
accepted music on publication failure. Do not enable a Record toggle ahead of
the owner, unified history, API and lifecycle work described in
[manual automation follow-up](MANUAL-AUTOMATION-FOLLOWUP-2026-10-20.md).

Known native policy difference remains deliberate in this bounded change:
Windows queue overflow faults the instance; Mac UI overflow leaves the latest
coalesced audible value active. Timestamp parity is not evidence of equivalent
failure recovery or end-to-end recording. Any convergence requires separate
review of existing failure contracts and tests.
