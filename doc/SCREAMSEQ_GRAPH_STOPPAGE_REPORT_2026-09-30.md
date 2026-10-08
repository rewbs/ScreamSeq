# ScreamSeq graph redesign — stopping-point report

30 September 2026. Work is paused at the user's request. This is a usable
development checkpoint, **not completion of the four-slice specification**.

Scope: the approved [graph interaction specification](SCREAMSEQ_GRAPH_INTERACTION_SPEC_2026-09-24.md).
Space still controls playback, Tab still navigates nodes, and edits to a reusable
definition affect all its uses. The earlier application features remain in place.
This report covers the graph redesign and its associated audio/UI work.

## Implemented and exercised

**Navigation and direct editing.** The graph opens at song level with implicit
channel routing visible. A searchable contextual Add menu (Shift+A) replaces
separate add paths. A selected channel, cable, group or dragged socket supplies
the destination context. Breadcrumbs, retained selection/viewport, channel filters,
distinct instance-role labels, and Show in pattern bridges make navigation more
predictable. Plugin double-click opens its interface. Parameter search, sliders
and exact values are available in the selected object's sidebar.

**Cables and amounts.** Named main/auxiliary/detector ports, compatible-port
highlighting, nearest-socket and nearest-wire hit testing, endpoint rerouting,
multi-input/output paths and direct gain/depth badges are implemented. Socket
drags add; endpoint handles replace one cable. Unrelated sends and detectors are
preserved. Dragging a parameter socket into empty space can create an LFO with
zero initial depth. Dragging an output into empty space can create a return with
its send initially off. The selected amount badge now avoids cards and stays
visible; exact numeric entry retains keyboard focus through refreshes.

**Processing groups.** Selected rack processors or recipe nodes can be packaged,
named directly, entered, nested, moved and ungrouped. Real processor identities
and external connections are retained. Valid chains can be exported as independent
library definitions. Grouping/export and their Undo/Redo were walked during
16-voice playback. This does not yet implement every group-editing action listed
in the specification; see the unfinished-work list.

**Diagnostics and monitoring.** Host mixer/rack input and output meters report
peak/RMS levels, sample clocks, overloads and available latency information.
Trace silence and Find next overload provide a route to the observed object.
Waveform/spectrum scopes use bounded requested-tap capture; temporary Listen here
has a persistent indicator and a one-action return to normal monitoring. Listening
does not rewrite mixer routing or system audio settings. Collapsed group taps
resolve to their real processor/port. Recipe-internal measurements remain incomplete.

**Parameter and curve interaction.** Rack and shared-recipe parameters have direct
searchable controls, stable target identities and gesture-coalesced Undo. Shared
graph control changes reach prepared channel/instrument copies during playback.
Graph envelope points now save on gesture completion or field commit; formula
typing saves after a short pause. An in-flight save cannot erase newer edits,
and conflicts retain the draft. Actual numeric edit, point drag, Undo and Redo
were verified with 16 voices playing. Parameter activity remains the bridge to
measured host-delivered values and existing automation sources.

**Audio preparation and a limited live-routing path.** Prepared mixer plans share
only equivalent processor dependencies, delay history and render-once wrappers.
Supported changes use a 10 ms sample-clock fade and retire old state off the audio
callback. Bypass continuously processes the plugin and fades to latency-aligned
dry audio (or silence for a source), retaining held-note state. Unchanged graph
copies can remain active through unrelated mixer repatching. New/removed/reordered
group and return buses are supported when source adapters, all processor input
dependencies and total latency remain unchanged. Per-plan meter maps and a bounded
append-only port catalogue preserve stable identities during concurrent audio/UI
observation. Broader structural changes still use the stopped path.

These changes use the shared model and revision-guarded APIs, chronological Undo,
and native persistence where applicable. Listening, scope selection and filters
are intentionally session/view state. Shared Windows model/codec/API counterparts
have been updated where needed; this is not a Windows-native UI qualification.

## Observed journey counts

Each cell is **actions / panel switches / redundant confirms**. A shortcut, text
entry, click, double-click or drag counts as one action. Accepting a search with
Return is not a redundant Apply. Setup and start-state differences are explicit.
The detailed ledgers and intermediate failures remain in the
[qualification record](mac-native-qualification/2026-09-30-graph-fluency/PROGRESS.md).

| Journey | Before | Latest observed after | Qualification |
|---|---:|---:|---|
| Add reverb to selected channel 3 | 4 / 3 / 0 | 3 / 0 / 0 | Shift+A, search, Return. |
| Insert EQ at selected cable | 5 / 3 / 0 | 4 / 0 / 0 | Exact cable context retained. |
| Kick → compressor detector | 8 / 0 / 0 | 1 / 0 / 0 | Baseline includes a wrong-port attempt/retry; its intended path was also one drag. |
| Create LFO → cutoff and set depth | 9 / 0 / 0 | 4 / 0 / 0 | Subsequent depth edits are live; creating the source still stops playback. |
| Create send/return and raise send | 9 / 4 / 1 | 4 / 0 / 0 | Visible amount badge re-walk passed; final live-return check recorded below. |
| Find/fix a silent path | Blocked | 3 / 0 / 0 | Muted-channel fixture; playback continued. Does not diagnose vendor internals. |
| Locate/fix an overload | Blocked | 7 / 0 / 0 | Includes a failed slider attempt followed by exact entry; intended numeric path was 6. |
| Package/name/export a chain | Blocked | 6 / 0 / 0 | Song-rack path, live Undo/Redo verified. |
| Enter instrument graph, edit, return | 15 / 2 / 0 | 7 / 0 / 0 | After starts with the copy visible; baseline started with instrument navigation hidden. |
| Repatch, compare bypass and Undo live | Blocked after a drag | 7 / 0 / 0 after cable selection | Partial: retained-processor route plus bypass and three Undo actions. Tight-cable selection needed a popup retry. |

## Controls removed or relocated

| Previous friction | Current primary path | Retained exact/advanced path |
|---|---|---|
| Separate Add plugin/source controls | Contextual Add search / Shift+A | More, object menus and registered palette actions |
| Endpoint form plus Connect | Drag typed sockets or selected cable handles | More → Patch by keyboard; explicit endpoint/port settings |
| Separate edit-and-apply connection fields | Selected wire amount drag or immediate property edit | Numeric gain/depth/range fields |
| Graph-envelope Apply / Set point | Point gestures and committed fields save automatically | More contains ramp/delete/retry/reload-discard actions |
| Library/object forms ahead of parameter controls | Selected plugin's searchable parameter controls appear first | Contextual metadata/advanced sections |
| Anonymous repeated subgraph names | Channel, instrument and row/persistent/ordinary context | Breadcrumbs and instance inspector |
| Unexplained target-dependent graph commands | Target search when the command needs a target | Existing keyboard navigation and precise patcher retained |
| Persistent destructive solo for inspection | Temporary Listen here with visible monitor indicator | Listen gain, explicit Stop listening, normal mixer solo remains |

The complete command-catalogue requirement is **not finished**. Main graph actions
have stable IDs and legacy shortcut migration; remaining object/port/advanced
actions still need a complete registration/accessibility audit. Custom plugin
library editors still use an explicit draft commit.

## Validation at this checkpoint

- Final development build and strict bundle signature verification: **passed**.
- Native CTest suite: **82/82 passed**, 100.32 seconds. This includes the portable
  Windows graph-document test, not a Windows-native app run.
- Rebuilt AppKit interaction suite: **passed**.
- Full local API socket/client suite: **passed**.
- New concurrent port-registration and rendered live-bus/Undo regressions:
  **passed**, including the host realtime allocation/free/lock audit.
- `git diff --check`: **passed**.

Logs: [native suite](mac-native-qualification/2026-09-30-graph-fluency/logs/stoppage-ctest.log),
[AppKit](mac-native-qualification/2026-09-30-graph-fluency/logs/stoppage-interface.log),
[API socket](mac-native-qualification/2026-09-30-graph-fluency/logs/stoppage-socket.log),
[port catalogue/native mixer](mac-native-qualification/2026-09-30-graph-fluency/logs/port-catalogue-tests.log).

Final bundle: `bin/mac-graph-fluency/ScreamSeq.app`, built at
2026-09-30 09:13:58 UTC from base `d5d376fa0ced527cc901d8555076cadbf98d75d4` plus
local changes. Source fingerprint starts `1b7873395f9db5ea`; no fingerprinted
source changed between build and verification. Full hashes are in
[stoppage-build.json](mac-native-qualification/2026-09-30-graph-fluency/stoppage-build.json).

The final isolated app opened the 16-channel fixture successfully. Native UI
control then repeatedly timed out after selecting BlackHole 2ch / 512 frames and
applying audio settings. The API remained responsive and a diagnostic sample
showed the main run loop active. Therefore **the newest live-return UI re-walk
was not completed**; no new gesture or uninterrupted-playback claim is made for
this build. The immediately preceding four-action stopped send walk and the new
rendered-audio tests remain valid, separate evidence. This limitation is recorded
in [stoppage-ui-limitation.json](mac-native-qualification/2026-09-30-graph-fluency/logs/stoppage-ui-limitation.json).

Earlier rendered evidence includes AU/VST3 gain, sidechain, auxiliary, latency,
bypass, held notes, listening restore, mixer publication and graph-control cases.
Host callback allocation/free/lock audits pass for the covered paths. Transition
ownership/history suites have previous ThreadSanitizer and ASAN/UBSAN passes.
The newest port-catalogue change has concurrent-reader and realtime-audit coverage;
it has not received a new sanitizer run.

**The strict 60 fps requirement is not met.** A previous quiet dense run reached
59.57 mean presented fps but still had a 50 ms stall and stale geometry. The latest
60 Hz experiment measured 55.91 fps, a 75 ms maximum interval and zero audio
overruns at 48 kHz/512 frames. It displayed only six cards, so it also failed the
eight-card workload requirement. A 120 Hz scheduling experiment did not solve
this and was reverted. These are failures, not a performance pass; thresholds
were not weakened. The final bundle has not received a new strict display run.

## Remaining work, in recommended order

1. **Complete live structural editing.** Prepare independent affected processors
   and graph copies, their automation/MIDI/custom-editor ownership, changed source
   adapters, auxiliary activation and differing-latency transitions. Preserve
   held notes and state through add/remove/reorder and rapid Undo. Today new
   plugins, new modulation sources and many changed-input routes still stop.
2. **Finish song-level modulation.** Ordinary rack parameters must accept graph
   sources directly, without rebuilding the rack effect inside a recipe. Complete
   stable song-level targets, explicit stepped-parameter quantization, read-only
   rejection and pattern/recorded-automation provenance/bridges.
3. **Complete structural UI semantics.** Finish song-level detach/heal/cut,
   deletion and adding inside groups; maintain memberships/routes/bindings through
   plugin removal and history. Resolve stable identity for an implicit Master
   before first materialization. Add visual frames/comments/reroute points,
   unconnected song-level Add and nested reusable-definition references.
4. **Finish dense readability and diagnostics.** Focused views need labeled hidden
   branch stubs with one-click reveal. Complete recipe/control/event observation,
   exact post-gain/compensation cable taps and latency overlays. Existing host-port
   meters must not be described as measurements of every internal graph wire.
5. **Meet presentation performance.** Diagnose drawable/compositor cadence and
   occasional stale geometry, fix the dense test's minimum visible-card setup,
   then pass the unchanged sustained 60 fps gate in docked and floating layouts.
6. **Finish discovery and platform qualification.** Register all graph actions,
   audit keyboard/menus, re-walk all journeys on one final build, run final loopback
   and commercial-plugin continuity cases, and compile/run the Windows host/UI.
   The portable Windows document regression is only part of that work.
7. **Optional vendor GUI enhancement.** Query supported parameter-under-pointer
   interfaces, otherwise offer a reliable last-touched parameter bridge. Do not
   promise arbitrary knob drops into every AU/VST custom editor.

No new product decision is needed to resume these agreed tasks. The current
choices preserve playback/navigation keys and shared-definition edits. Any future
simplification that removes a power-user path should still be reviewed with the
user first.

## Screenshots and handoff

The [screenshot gallery](mac-native-qualification/2026-09-30-graph-fluency/index.html)
contains actual native UI captures, including groups, scopes, role labels,
instrument parameters, direct depth/send editing and automatic graph envelopes.
Each image is a checkpoint; it does not establish a global performance or feature
completion claim. Raw before/after images, action ledgers, logs and per-build
fingerprints are retained in the same directory.

Work remains local on `codex/screamseq`; no commit or push was requested for this
checkpoint. The existing working tree contains substantial earlier work and has
been preserved. The development bundle is separate from the musician's installed
app. All disposable QA processes are closed. The final process (PID 91524) was stopped
through its API and terminated by verified PID after native UI control became
unavailable; this is not claimed as a native-Quit test. No musical edits were made
in that final disposable session. No system audio defaults have been changed.
