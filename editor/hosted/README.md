# Shared hosted audio

`TrackerHosted` is a C++20 static library, **PUBLIC**-linked to `TrackerEditor`.
Include `editor/hosted/HostedAudio.hpp` for the rack/facade, or
`PluginTypes.hpp` for the unchanged project value objects. No Apple, WASAPI or
vendor SDK types appear in these public headers or `PluginBackend.hpp`.

## Source ownership

- `HostedAudio.cpp`: shared built-in dispatch, state/metadata facade.
- `PluginScheduling.cpp`: the existing AudioUnitHost automation/MIDI scheduler;
  only the former VST3 branch is expressed as a backend capability.
- `PluginChain.cpp`: the existing rack queue, mixer completion, dry compensation,
  graph/tail accounting and transport orchestration extracted from AudioUnitHost.
- Existing `mac/Audio/{NativeInstrument,NativeSignalGraph,PatternCommandRuntime,
  PatternPitchRuntime,PluginAssignments}.cpp` are compiled **unchanged**, once,
  into this shared target. Their paths are historical, not platform dependencies.
  There is no Windows copy of their scheduling, routing or musical algorithms.
- `mac/Audio/AudioUnitHost.hpp` is a compatibility forwarding header. On Mac it
  retains historical transitive AudioToolbox declarations; elsewhere its one
  compatibility constant is the persisted `aumu` FourCC, not an SDK type.
- `mac/Audio/AudioUnitHost.mm` now holds only a private `MacPluginBackend`, AU
  storage/callbacks/editor/state code and the Mac AU/VST3 factory. Existing
  `VST3Host.mm` remains unchanged. Mac CMake compiles these into TrackerHosted;
  TrackerPlugins forwards to that target instead of building another scheduler.
  That forwarding target is an interface, not an archive. Direct Swift compiler
  invocations in `mac/build.sh` and `mac/test-shutdown.sh` link `TrackerHosted`
  from the build's `hosted/` directory; they must not link a stale
  `libTrackerPlugins.a` left by an earlier build.

All existing OpenMPT/ScreamSeq attribution and repository licensing still apply.
This extraction does not change the project storage format or stable class IDs.

## Exact platform-provider contract

Define **one** `Tracker::platformPluginBackendFactory()` returning a persistent
`PluginBackendFactory&`. This is link-time injection, not mutable registration or
a process-global provider switch. Before `add_subdirectory(editor/hosted ...)`,
set `TRACKER_HOSTED_BACKEND_SOURCES` to the provider's source list; absent a
provider the library compiles `UnavailableBackend.cpp`.

Both `PluginChain` rack creation **and** `NativeSignalGraph::Instance` construct
the same `NativePlugin`. Its constructor calls this provider for each non-built-in
instance. Each graph target/role therefore receives a distinct backend; do not
cache/reuse a mutable processor by plugin class, recipe or rack slot. Built-ins
always construct the shared `editor/NativeEffects` implementation and never go
through the platform factory. There is intentionally no separate graph factory.

### Preparation and ownership

- `create(state, rate, offline)` runs on the stopped/control owner. Fully resolve
  identity, restore state, negotiate/activate buses and allocate all audio/event/
  parameter buffers before returning a non-null owned backend. Throw on missing,
  malformed or unsupported plugins; never return a silent/dry substitute.
- Preserve `descriptor`, instanceID, primary instrument, MIDI channel, aliases,
  auxiliary activation and opaque state. The facade restores ownership fields
  onto backend state; `PluginChain::states()` also restores rack bypass. Do not
  replace missing plugins by display name, path alone or transient slot index.
- Mac VST3 state is a binary plist dictionary with `component` and `controller`
  data streams, as implemented in `mac/Audio/VST3Host.mm`. A Windows provider must
  retain that representation, not invent a new opaque wrapper. AU state remains
  unavailable on Windows and must stay retained by the document owner.
- `buses()` returns stable logical mono/stereo port metadata. Physical native
  buses retain all channels as explicit pairs, with an odd final mono port.
  The first pair keeps its physical bus index; additional pairs follow the
  physical bus count in bus/channel order. `physicalBus`, `firstChannel` and
  `physicalChannels` identify the native mapping. Each direction has at most
  64 logical ports and each physical bus at most 64 channels. Main input may be
  absent for instruments; auxiliary activation arrays contain logical IDs 1–63.
- Prepare and activate every supported physical bus before rendering, including
  logical ports with no cables. `preparedAuxiliaryInputs/Outputs()` describe this
  immutable capacity; `active` describes requested musical membership. Missing
  inputs receive silence. Pair wires never sum or truncate hidden channels;
  explicit mono ports average stereo input and duplicate mono output.
- `PluginState.audioLayout` pins the physical/channel-pair mapping after capture.
  Its canonical signature includes inactive ports and excludes names/activation.
  A nonempty saved mismatch rejects before activation; empty legacy signatures
  remain readable. Include all plane, pointer and logical-output allocations in
  `preparedStorageBytes()` so retained graph plans enforce the shared budget.
- `pendingLatency()` runs on the control owner and returns a stable notification
  serial, latency samples and tail seconds without resetting the processor.
  `acknowledgeLatency(serial)` is callback-safe and follows prepared PDC adoption;
  acknowledging an older serial never clears a newer notification. Unsupported
  I/O/reload restart requests remain separate failures.
- `latency()`/`tail()` are seconds, not frames. Validate finite/nonnegative latency
  and the existing two-second host limit before returning. Preserve existing tail
  conventions and program IDs. Program load/state/editor/discovery/destruction
  are control-thread operations; provider-specific UI/COM ownership stays private.

### Render-facing methods

- `process(stereo, frames, position, inputs, inputOffset, transport)` consumes and
  replaces interleaved stereo float PCM. `frames` is at most 4096, `position` is
  the absolute frame of this **slice**. The main pointer is already advanced.
  `inputs` always has 64 entries: 0 is unused, null means silence, enabled auxiliary
  pointers reference the **whole facade block**; read at `inputOffset * 2`.
- `auxiliaryOutput(bus)` returns interleaved stereo for the most recent slice,
  starting at **zero**, valid until the next process call. The facade copies it
  into its preallocated whole-block output at the appropriate offset. Never apply
  the input offset a second time to backend auxiliary output storage.
- Parameter values use the domain advertised by `parameters()`: AU/built-ins
  retain native units (dB, Hz, etc.); existing VST3 metadata is normalized 0..1.
  Keep stable parameter IDs, writable/continuous flags and steps. No shared
  normalization or conversion to a guessed plain-value range is performed.
- `supportsSampleOffsetParameters()` must be true for VST3. `parameter(id,double,
  offset)` queues endpoints relative to the **next slice**, preserving double
  precision until the vendor queue. Same-ID/same-offset writes replace the prior
  point. Consume/reset bounded queues per process call; report capacity failure.
  The shared scheduler sends ramp starts at 0 and ends at count-1. It splits on
  musical events/endings, **not** into one-sample VST3 process calls. False means
  immediate-parameter semantics (AU/built-ins retain their previous behavior).
- `midi(status,a,b)` applies/queues MIDI at the start of the next slice; the common
  scheduler already split at precise event positions. Implement note-off, channel
  ownership and all-notes/all-sound-off correctly. Do not add a second note clock.
- `transport(t)` publishes current host time even before parameter/MIDI calls;
  it is called by the facade setter and after each processed slice advances time.
  This preserves AU host-callback timing between process calls. The process
  argument supplies the slice's same clock. Neither operation may marshal to UI.
- The noexcept processing/parameter/MIDI/transport methods must not throw. Failure
  returns false and the existing chain fail/silence behavior is retained. Backend
  destruction, allocations, locks, discovery, serialization and UI are forbidden
  on the render path. **These Windows functional tests are not a realtime audit**;
  do not claim malloc/free/lock freedom, sanitizer coverage or vendor-private safety.

## Integration lifecycle

Define TrackerEditor first, then `add_subdirectory(editor/hosted <binary-dir>)`.
Link consumers to TrackerHosted. Do not compile the historical portable host
sources into another target too. The Windows main CMake/application integration
is intentionally left to its owner.

Prepare `PluginChain`, then a heap-owned `Renderer`, on the control thread. Always
call `chain.attachInstruments(renderer, &native)` and
`chain.attachMusicalAutomation(renderer, native)`, **even with an empty rack**:
the song can contain a mixer, sample-instrument graph, commands or precise notes.
At render time, sync transport, render the engine, then process the chain. End
notes when the song finishes and continue the existing tail path as appropriate.

**Stop/join processing, reset/destroy Renderer before PluginChain.** Engine
adapters borrow chain references and the chain holds a renderer backpointer for
tails. Keep the native snapshot alive during preparation; never rebuild/attach,
replace chains, inspect serialized state or destroy them on the callback. Live
structural replacement/publication is not implemented by this extraction.

## Qualification and Mac review

Both native frontends coexist in the same source tree and consume this target.
Mac supplies AU/VST3 hosting; Windows supplies its native VST3 backend through
`windows/Plugins/Provider.cmake`. AU instances remain unavailable on Windows and
their identities/state must be preserved. Plugin binary locations are platform
specific even when a VST3 class and its state are portable.

See `windows/Tests/Hosted/README.md` for the original extraction's offline tests,
and `windows/UPSTREAM_PLUGIN_QUALIFICATION.md` and subsequent Windows progress
reports for installed VST3 lifecycle evidence. The extraction report predates
the Windows vendor loader; it is not a current built-ins-only limitation.

Mac cannot be built/run on this Windows host. Before merge, compile Mac CMake and
run existing AU/VST3 fixtures, especially bus activation/output splitting, state,
program/editor lifetime and transport host callbacks. Review the private backend
storage move, the per-slice auxiliary copy, double parameter forwarding and the
TrackerPlugins forwarding target. Source integration is not Mac runtime
qualification: a real Mac-to-Windows-to-Mac project round trip is still required.
