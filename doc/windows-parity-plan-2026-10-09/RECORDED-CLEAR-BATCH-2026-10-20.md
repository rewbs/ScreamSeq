# Clear recorded automation without changing the manual sound

**Prepared source only. No additional native build or runtime execution.**
This is one A13/P4b workflow slice; it does not implement Windows gesture
capture, arming or original-time event ingestion.

## Observed problem

Windows exposes recorded-point editing and selected-lane clearing, but no
equivalent to Mac's clear-all action/API. Mac's `clearAutomation:` obtains
`AudioDevice::pluginStates()` before folding manual edits. That method stops
the device and captures vendor state, which may contain the current automated
value. Clearing also creates history when the collection is already empty.

A further source boundary matters for freshly captured gestures: the live
parameter catalogue can contain their last value even when the independent
saved manual state is unchanged. Merely publishing an empty timeline does not
restore a target that has never entered a scheduled timeline.

## Implementation

- Both platforms advertise `automation.recorded.clear(expectedRevision,
  dryRun?)`, returning `removed`, `wouldChange`, `dryRun`. It clears all recorded
  parameter lanes in one chronological Undo. An empty collection is a no-op.
  Invalid/stale requests, dry runs and empty clears preserve transient previews.
- Windows `AbsoluteAutomation.inc` uses the existing recorded-only commit and
  publication owner, retaining every opaque rack record. The native Song
  parameter automation window adds **Clear all recordings** next to **Clear
  lane**, with ordinary keyboard/button behavior and Undo. It refuses stale
  targets and retained point/drag drafts; no Apply/confirmation step is added.
- Mac's existing native action and the API share `clearAutomation:`. Stopped
  clearing uses the saved manual rack and existing history path. Live clearing
  prepares accepted manual values for affected parameters from independent
  saved plugin instances, then publishes those values and the empty timeline
  together. All allocation/history capacity precedes publication.
- Shared `RecordedAutomationPlan` accepts optional explicit manual baselines.
  Preparation checks bounded distinct targets, native ranges and zero frame.
  Targets enter the plan even without an earlier scheduled point. Publication
  guards rack identity, recorded revision and the manual queue fence before
  updating the accepted control catalogue. An intervening manual edit refuses
  the prepared restoration. Callback adoption uses existing preallocated
  timeline data and preserves ordering with earlier/later manual batches.

No file-format, identity, native device integration or vendor framework changes.
UI, session history, saved-state ownership and device lifetime remain native.
The shared host owns only validation and prepared render publication.

Tradeoff: live Mac clearing creates temporary baseline plugin instances for
affected slots on the control owner. This avoids querying automated live state
or replacing playing processors, but vendor instantiation cost remains a
qualification concern. Preparation failure retains the recording and playback.
A future capture owner should retain accepted manual parameter values directly
and can remove this extra probing without changing the public contract.

## Bounded next validation

Include this change with the already prepared snapshot assertion correction and
any proven failures from candidate `28d757f65`. Do not start a separate build
during the hourly cooldown.

1. `recorded-automation-tests`: 44.1/48/96-kHz restoration after an unscheduled
   capture, exact manual catalogue/PCM, stale manual fence, invalid/duplicate
   targets, full queue, later manual precedence and existing callback audit.
2. Windows `document-controller-live-recorded-publication`: dry/no-op clear,
   full-queue refusal, baseline preservation, live one-step Undo/Redo and native
   save/reopen of both restored and cleared timelines.
3. Mac `parameter-activity-tests`: native/API clear, pending manual values,
   dry/no-op, saved baseline and history. Existing optional
   `NativePluginTests --loopback-record` additionally clears a fresh vendor
   gesture without stopping playback, restores the independent manual value,
   and undoes it live. Equipment-dependent execution remains explicit.
4. `recorded_clear_contract.py` loads F04 through the owned platform fixture
   loader, adds two independent lanes, and checks dry/invalid/stale requests,
   transient ownership, request replay, empty no-op/Redo, full typed project
   preservation, one Undo/Redo and reopen. Mac's existing conformance runner
   includes it as the eighth contract; Windows has a dedicated wrapper in its
   already scheduled parity class. Fixture setup via Mac private recovery is
   distinguished from ordinary native Open.
5. The new Windows private-desktop UI case checks field/stale draft retention,
   all-lane clearing, saved plugin state and one Undo/Redo. It is explicitly
   scheduled in the next workflow. Existing minimum-window checks cover the
   added native button; foreground/DPI/accessibility gates remain separate.

Source checks passed: four Python ASTs, JSON parsing/schema shape, exact
one-method additions to both expected inventories, unchanged prior schema
entries and observed API contracts, and `git diff --check`. The local runtime
has no `jsonschema` package; full schema validation was not run.

The inventory additions are explicitly marked source-reviewed/runtime-unqualified.
They are expected new API surface, not rewritten historical observations.
Current source still requires both native builds, the targeted tests above and
the final cross-platform fixture gate. Broaden only for failures or evidence of
shared queue/history regressions.
