# ScreamSeq graph editor — interaction specification

**Approved for implementation, 30 September 2026.** Space retains playback; Tab retains node navigation; editing reusable groups updates all uses of their shared definition. Explicit independence remains available.

This is the specification requested before implementation. It defines the intended behavior, preserves existing power-user paths, and separates UI work from the audio/model changes required to make that behavior true. The next pass measures the ten journeys below against the current build, then implements and verifies four slices.

## 1. One graph, several depths

Open Graph at the song level. Even a new song shows its channels connected to Master; there is no routing-enable gate. The view represents the actual processing model, including disconnected routes. Opening it never creates an Undo entry or changes the song.

The header contains a breadcrumb, a compact **Add…** button, and view options in an overflow menu. A collapsible properties sidebar follows the selection. Search filters the current view; it does not change the graph. Channel focus is a filter chip, not a separate editing mode.

Example breadcrumb: **Song › Track 3 › Crunch [ordinary]**. Each level remembers selection, zoom and scroll. Clicking a breadcrumb goes back without losing those positions. A group exposes its real inputs, outputs and parameter ports. Double-click enters a group, while double-clicking a plugin opens its custom interface. The sidebar remains the normal home for quick parameter edits.

The processing order is drawn from left to right:

```text
Sample instrument processing ─┐
                             ├─ Channel audio → Row graphs → Persistent graphs
Other channel audio ─────────┘                  → Ordinary graph → Inserts
                                               → Fader / sends → Group / Master → Output

Pattern channel ── note events ── Plugin instrument ── audio outputs ── destination buses
Pattern FX / envelope / LFO ── control ── parameter socket on the relevant processor
```

This is intentionally two kinds of flow. Pattern channels send notes to instruments; instruments generate audio. A note-event cable must never imply that channel audio is being sent into an instrument. Sample voices retain their actual parent channel and instrument processing. Plugin-instrument outputs show their real bus assignments, which need not match the channel containing their notes.

Row, persistent and ordinary processing use separate, labeled stage containers. Persistent graphs display their actual active A→B order; inactive assigned copies remain visible but dim. Playback must not shuffle manually placed cards: dynamic stages update their internal order/rank while their containers remain stable. Opening a processor identifies the target channel, role and instance, not just its library name.

**Groups have distinct purposes:** a frame is only a labeled visual region; a processing group exposes a boundary around a chain; a reusable library definition creates independent processor instances when used. A mixer bus remains an audio summing/routing node. These must not be conflated.

Ctrl+G packages selected processors and their internal connections into a processing group, preserving boundary cables and stable parameter bindings. A default name allows immediate creation; rename inline. **Save to subgraph library…** makes reuse explicit and gives the group a name/number suitable for pattern commands. Existing named definitions remain available in Add. Grouping is one Undo transaction and does not reset processor state or alter the sound. Recursive definition references are rejected. Unsupported boundary configurations explain the issue before committing, rather than dropping cables.

## 2. Visual language and layout

Use restrained color plus shape, labels and line styles. Selection uses a clear outline; it does not replace the signal's color. Nodes retain readable names at working zoom and ports have screen-space hit areas that remain usable when zoomed out.

| Signal | Port / cable | Always available information |
|---|---|---|
| Audio | Teal circular socket, solid directed cable | Bus name and channel count: `Main · 2ch`, `Out 3 · 1ch`; exact channel mapping in inspector |
| Sidechain | Blue circular socket marked `SC`, solid cable | Named detector/auxiliary destination, count, pre/post-fader tap |
| Control/modulation | Amber diamond, dashed cable | `0…1` or `−1…+1`, current value, source name |
| Note/MIDI events | Violet square, dotted directed cable | Event kind/channel and activity pulses, not a fake audio level |
| Parameter target | Amber outlined diamond attached to named parameter | Name, unit, base value, effective value and modulation range |

An auxiliary audio bus is not automatically a sidechain: use the plugin's bus metadata and preserve its label. Where the vendor supplies no semantic label, show **Aux input 2**, not a guessed detector name.

Nodes show a compact meter appropriate to their type. A collapsed node keeps its title, flags, connected ports and meter. Clipping uses a red peak badge separate from selection. A level above digital full scale is labeled **over 0 dBFS**, not proof that an internal floating-point path has already clipped. Output clipping and reported processor errors have distinct badges.

Channels stay in tracker order. Groups and returns appear downstream, and Master/output are to the right. New nodes occupy a nearby free location or the selected cable's insertion point. Only new/unplaced nodes are automatically laid out; existing positions persist through edits, refresh, save/reopen and library reuse. Explicit **Arrange selection** and **Arrange all** remain in the menu and ⌘K, with Undo for saved position changes. Automatic Fit occurs on the first opening of an unpositioned graph, never after each edit.

At 16+ channels, semantic zoom reduces card detail; it does not create unreadably small text. A focused channel keeps its upstream sidechain dependencies and downstream output path visible. Omitted branches terminate at labeled boundary stubs such as `7 other inputs`, with one-click reveal. Filter state and a clear-filter action are always visible. No necessary route should disappear silently.

## 3. Gesture and keyboard contract

Bindings apply only while the canvas owns keyboard focus. Text fields retain normal editing shortcuts. Explicit Live keys mode remains available; it must visibly explain when it owns musical keys. No global shortcut changes occur merely because a graph window is visible.

**Two shortcut choices require review:** Space already controls transport, and Tab already selects nodes. Until answered, retain both and use Shift+A to add, Return to enter/open, and Option+Up to leave a group. The requested alternate graph mapping remains an option, not a silent replacement.

| Intent | Pointer interaction | Keyboard / catalog action |
|---|---|---|
| Add anything | Add button or canvas context menu at pointer | Shift+A opens searchable Add; search, arrows, Return; Esc cancels |
| Select / multiselect | Click; Shift/⌘-click; marquee empty canvas | Tab/Shift+Tab next/previous node; ⌘A; selection commands in ⌘K |
| Select a cable | Click cable or its label | Option+Tab / Option+Shift+Tab; Return focuses cable properties |
| Move selection | Drag any selected card | Arrows nudge; Shift+arrows larger nudge; one transaction per gesture |
| Pan / zoom | Trackpad scroll or middle-drag; pinch | +/− zoom; Home fits graph; F frames selection; menu pan actions |
| Inspect / open | Single click selects; double-click plugin opens custom UI | Return opens plugin/group or focuses connection properties |
| Leave a group | Breadcrumb / Back | Option+Up; ⌘K → Parent graph; Tab alternate pending review |
| Connect | Drag from either endpoint to a compatible port | Port context → Connect to… searches compatible endpoints; Return commits |
| Add while connecting | Release a new cable on empty canvas | Compatible Add opens; choose node/port and auto-connect in one edit |
| Reroute | Select cable, drag its endpoint handle | Cable menu → Change source / destination |
| Disconnect | Select cable, Delete; cut stroke | Delete; Ctrl+right-drag cuts crossed cables; palette exposes a cut tool |
| Insert in a path | Drop selected node/serial chain onto highlighted cable | Cable → Insert…; or Move selected chain here |
| Pull out and heal | Option-drag node body | Detach and reconnect in context/⌘K |
| Delete and heal | Node context menu | Ctrl+X; ordinary Delete remains disconnect-and-delete; ⌘X retains Cut |
| Bypass | Small B flag on processor | M; label changes to Mute for a source without a dry path |
| Listen at output | Small headphone flag; Ctrl+Shift-click node | L toggles selected tap; global Stop listening action; Esc cancels active listen when no editor/gesture has priority |
| Collapse | Header disclosure triangle | H toggles selection; connected sockets remain reachable |
| Group / ungroup | Context → Group selection / Ungroup | Ctrl+G / Ctrl+Alt+G; aliases in ⌘K |
| Frame / annotate | Add → Frame or Comment | ⌘K → Frame selection; inline title/text editing |
| Reroute point | Cable context → Add reroute point, then drag | Same action in ⌘K; deleting point preserves the cable |
| Adjust cable amount | Drag selected cable's amount badge; double-click types exact value | Return to properties; arrows/fine modifier on focused value |
| Scope a cable | Hover for level; hold Q for temporary scope | Select cable, hold Q; Shift+Q spectrum; menu toggle for latch |
| Return to pattern | Node/source context → Show in pattern | ⌘K → Show in pattern; existing Return to opening row bridge |
| Undo / Redo | Edit menu | ⌘Z / ⇧⌘Z for all saved musical/graph edits |

Option-drag **on a socket** keeps its existing add/sum-input meaning. Option-drag **on a node body** detaches. Context menus spell out both. Trackpad users get a palette-invoked cable-cut tool with a normal primary-button stroke; no special mouse is required.

Esc cancels the innermost active gesture/search/editor first. Canceling a cable drag or Add search restores the original route. Releasing an existing cable endpoint on empty canvas cancels the reroute; it does not silently delete a working connection. Delete and the cut tool are the explicit removal paths.

## 4. One searchable Add menu

The same search includes installed plugins, built-ins, instruments, modulators, existing automation sources, sends/returns, reusable groups, frames and reroute points. Results show type, compatible ports and destination context. Use the cached plugin catalogue; rescan stays in More/⌘K.

- **Channel selected:** choosing an effect inserts into that channel's normal insert path. The result shows `Track 3 › Inserts` before committing; adding a plugin does not unexpectedly change which pattern-command stage is active.
- **Cable selected:** choosing a compatible effect inserts between its exact endpoints. Sidechains and other outputs remain unchanged.
- **No selection:** place an unconnected node at the pointer; do not silently make it a Master effect.
- **Cable dragged to empty canvas:** search only compatible candidates, with a visible source/destination constraint chip. Choosing one creates and connects it as one transaction. If several ports match equally, a compact port choice is part of that search.
- **Inside a group:** add at that depth. The breadcrumb and search subtitle make the scope explicit.
- **Add return:** create a named return and its Master output. From an existing output-port drag, also create the send; the dry main route remains intact. Default send is off (`−∞ dB`) until the musician raises its cable amount, avoiding an unrequested level jump.

Show **Create LFO**, **Pattern envelope** and **MIDI controller** alongside effects instead of separate add controls. Modulating an ordinary rack effect must work in the song graph; it cannot require the musician to rebuild that effect inside a reusable recipe. This needs a model/API extension, not an invisible conversion that would reset IDs or presets.

## 5. Connection rules and safe direct editing

Highlight valid ports during a drag. Over an incompatible port, keep the preview uncommitted and show a specific reason beside it, such as `Note events cannot feed an audio input`, `6 channels → 2 requires a channel map`, or `This creates a feedback cycle`.

| From → To | Behavior |
|---|---|
| Audio → supported audio/sidechain | Connect or sum; preserve existing outgoing branches. Show tap and channels. |
| Audio → parameter | Offer **Insert envelope follower** with the final parameter target retained. Never connect audio samples directly as parameter values. |
| Control → continuous writable parameter | Connect, expose a named port, reveal depth next to the target. |
| Control → stepped writable parameter | Explicit quantized mode using the parameter's discrete values; no promise of smooth interpolation. |
| Any source → read-only parameter | Refuse with `Read-only parameter`; allow viewing activity. |
| Event → compatible instrument/event input | Route event stream; reconcile held notes when a route changes to prevent stuck notes. |
| Mismatched audio layouts | Offer explicit mapping/conversion where supported. Never silently discard extra channels. |
| Output → output, missing port, forbidden cycle | Refuse at drag time and revalidate atomically at commit. |

Socket drags add connections. Endpoint-handle drags replace only that cable. Multiple input/output edges remain supported. A multi-source input sums audio; replacing its source must be explicit.

Insertion, detachment and delete-with-heal operate on an unambiguous main path. A sidechain is never mistaken for the processor's predecessor. For a branched or multi-output selection, show the proposed reconnections first and require choosing the intended path in the inline picker. Do not invent a cross-product of neighbors or change levels. Cancel leaves every route unchanged.

Frame movement and reroutes affect presentation only; they do not rebuild DSP. Grouping preserves exposed port identities, parameter automation, pattern bindings, instance ownership and processing order. Native format/API changes must be mirrored in shared storage semantics and documented for the Windows adapter.

Feedback is initially **blocked with a highlighted cycle and explanation**. This satisfies the brief's blocked-or-explicit-delay choice. An ordinary delay plugin does not make a scheduling cycle safe. An actual feedback-delay node may be added only with explicit block/sample scheduling and stability tests; it is not part of the first implementation slices.

## 6. Modulation and tracker context

All host-visible parameters can be exposed as named target ports; writable capability determines what can connect. The sidebar offers search, base-value editing, current effective value, and a modulation range ring/bar. Multiple sources get distinct small segments and a compact contribution list. Dragging one segment changes only that source's range. Dragging the main control changes the baseline. Exact units and normalized values are available in properties.

Dragging a modulator onto a host-owned parameter control creates the same edge as port-to-port patching. On first connection, depth starts at zero with the depth control focused; dragging sets an audible amount. This avoids a full-range jump on drop. Range endpoints, inversion and existing minimum/maximum/base semantics remain available. One continuous drag is one Undo step.

Custom plugin GUIs are a conditional enhancement. VST3 provides optional parameter-under-pointer lookup; a plugin without that capability cannot reliably reveal which knob is under a drag. Supported views get a temporary target overlay. Other views use **touch a knob → last-touched parameter → connect**, or the complete searchable host sidebar. AU/custom view support is verified per host interface, not inferred from a screenshot. No claim that every vendor knob can accept a native drag.

Pattern FX columns, automation envelopes and recorded automation appear as source/provenance nodes attached to their real targets. Selecting one shows pattern/order/row/column or recording time and provides **Edit source**. These are views of existing data, not duplicate automation lanes. A pattern FX binding that sets/slides a parameter retains its scheduling semantics; it is not silently converted to additive modulation. The target inspector explains overwrite/additive behavior and links to Parameter activity for the measured final value.

Selecting a channel in the grid highlights its relevant path without moving the graph viewport or stealing keyboard focus. Selecting its graph source highlights the channel; **Show in pattern** navigates explicitly. A pinned graph keeps its inspected context. A displayed group/parameter instance includes its channel and row/persistent/ordinary role so identical library names cannot send the musician to the wrong copy.

## 7. Signal visibility and listening

Meters are actual bounded observations from the audio engine. Audio cards show input/output peaks; events show activity; controls show current value. Silent paths dim after a short hold, while stopped transport displays **Stopped**, not a false diagnosis of silence. Measurement unavailable is distinct from measured zero.

Hovering a wire shows its exact tap, peak/RMS level or control/event value, channel count and latency. Holding Q shows a bounded waveform; Shift+Q shows a spectrum for audio. FFT work stays off the audio callback and scopes are captured only for requested taps. Scope and meter histories use the audio clock, with documented downsampling/drop counts.

Clip badges latch until cleared and identify the observed port. **Find next overload** visits measured overloads; **Trace silence** highlights disconnected/muted/bypassed paths and the last point with signal. Vendor-internal silence cannot be diagnosed from level alone: say `Input active; output silent` and link to the plugin, rather than invent a cause.

Latency labels show processor delay and route compensation separately in samples/ms. Zero is not substituted for an unknown or unavailable report. Bypass retains appropriate compensation. A dynamic latency change has a visible pending state while a safe plan is prepared.

**Listen here** is a temporary monitor tap, not saved mixer solo or a destructive reroute. It replaces the monitor mix with the selected node output; upstream processing and sidechains continue running. Downstream effects are excluded. It does not bypass an upstream mute implicitly. A persistent `Listening: Track 3 › Filter` indicator and one-click return prevent a forgotten audition state. It never changes the system audio device or route. Tap selection, gain and exit are smoothed; microphone/output routing is unaffected. This mode is session state, separate from song Undo/persistence, and has an API equivalent.

## 8. Editing while playback continues

This is an audio-engine acceptance criterion, not merely a label in the UI. Current graph mutations call stop for processing changes. That path must be replaced before claiming live reconnection.

Prepare and validate the next processing plan off the audio thread while the old plan plays. Publish it at a bounded boundary, transition affected routes with a short latency-aligned crossfade, and retire old state off the callback. Unchanged processors retain state and must not be advanced twice per block. New processors prepare before insertion; changed paths with incompatible state/latency require an explicit transition strategy and tests. Keep inactive graph copies clocked as today, including silence on their sidechains.

For unchanged correlated paths, avoid gain bumps from an inappropriate equal-power crossfade; qualify the chosen gain law and compensation with rendered signals. Start with a short configurable internal transition interval, tuning it through tests rather than promising an arbitrary duration makes all plugins click-free. Bypass has a smooth dry/wet transition and visible internal pass-through; an instrument/source with no dry signal fades to silence.

If preparation fails or exceeds the reserved processing/storage budget, retain the old audible graph and report the failure next to the attempted edit. No partial route, silent transport restart, or hidden cutoff. MIDI route changes deliver required note-offs without duplicating note-ons. Rapid edit/Undo/Redo sequences have revisioned pending plans; stale preparations cannot replace a newer graph. UI indicates **Preparing** until the committed graph is actually active.

The model, mixer, graph, API and Undo all consume the same authoritative transaction. UI echoes must not create a second edit. No callback allocations, plugin discovery, serialization, locks or UI calls. Third-party processing cost remains measurable and cannot be guaranteed by the host alone.

## 9. Context menus and command catalogue

| Object | Primary/context actions |
|---|---|
| Canvas | Add, paste, find node, filter current channel, frame selection, Fit, Arrange selection/all, parent graph, view options |
| Channel/bus | Add effect/send, rename, mute/solo, listen, show in pattern/mixer, focus path, show hidden dependencies |
| Plugin | Open interface, bypass, listen, parameters, automate/inspect parameter, preset save/load, duplicate, detach/heal, remove/heal, group, show owner |
| Group | Enter, bypass, listen, rename, expose ports, make independent, edit shared definition, save to library, ungroup, show uses/pattern commands |
| Port | Connect to, add compatible node, show/rename exposed port, channel mapping, disconnect selected/all, show source/targets |
| Cable | Gain/depth, change endpoints, pre/post tap where meaningful, mute connection, disconnect, insert node, add reroute, scope/spectrum, show source/target |
| Parameter | Expose port, connect modulation, automate, inspect effective value, edit sources, reset, exact-value entry |
| Frame/comment | Rename/edit text, color, collapse, move contents, remove frame while retaining contents |

These actions have stable command IDs and appear in ⌘K with context-aware names and configurable bindings. A command needing a target offers a target search instead of becoming an unexplained dead end. Genuine unsupported actions give the reason. The keyboard connection picker, explicit Arrange/Reload, precise numeric ranges, send tap controls, node cycling and independent panel pins are retained until any requested replacement is agreed.

An automated catalogue check ensures each registered graph action has a menu/keyboard path and exposes its reason when unavailable. The selected-object inspector uses progressive disclosure: common parameter/range controls first, advanced audio buses, source math and identity details behind sections.

## 10. Ten journeys and measurement plan

Start from a documented selection, zoom and fixture. Count clicks, double-clicks, drags, shortcuts and text-entry operations separately in an action ledger; also count panel switches, redundant confirmations, failed attempts and external information the musician had to remember. Record intended paths separately from observed retries. Save before/after screenshots for each slice during actual playback.

The numbers below are **design targets, not measurements**. The earlier interaction audit only supplies partial comparable baselines. No new graph journeys have been claimed as walked for this specification.

| Journey / fixed starting point | Current evidence / baseline to measure | Proposed path and target actions |
|---|---|---|
| 1. Add reverb to channel 3, channel visible and selected | Earlier J03 was a longer add/tweak/bypass journey: 10 actions; do not reuse that as this baseline | Shift+A, type reverb, Return: **3**, 0 switches, 0 Apply |
| 2. Insert EQ between two visible effects | Existing serial insertion drag works; measure add-and-insert together | Select cable, Shift+A, type EQ, Return: **4**, 0 switches |
| 3. Kick sidechains the bass compressor, both visible | Previous patch-only journey measured **1 drag**, after compressor setup | Drag kick output to Detector: **1**; preserve dry kick/main bass path |
| 4. LFO → cutoff with depth, filter selected and cutoff visible | Earlier LFO-creation-only walk was 8 actions; full target/depth journey unmeasured | Drag cutoff socket to blank, type LFO, Return, drag depth: **4**, 0 switches |
| 5. Create a send/return, source channel visible | Measure current mixer form and graph bridge | Drag output to blank, type return, Return, raise send gain: **4**; rename is a separate edit |
| 6. Find why bass is silent during playback | No measured graph diagnostic path yet | Select bass, invoke Trace silence, open reported cause: target **3–5**, outcome depends on cause |
| 7. Find an overload during playback | Bus meters exist; per-port diagnostics missing | Invoke Find next overload, inspect/read port, adjust: target **3–5**, never claim automatic vendor diagnosis |
| 8. Package a chain for reuse, processors visible | Library definitions exist; in-place grouping/export path missing | Marquee, Ctrl+G, rename, context menu, Save to library, accept name: target **6**; count full naming dialog if more actions are required |
| 9. Enter instrument group, edit, return to pattern | Separate library/instrument navigation currently required | Double-click group, parameter drag, Show in pattern: **3–4**; shared-edit scope pending decision |
| 10. Repatch a playing path, compare bypass, Undo | Structural edits currently stop playback: baseline **blocked for uninterrupted journey** | Drag endpoint, M, M, ⌘Z: **4** after selection; require audio continuity and restored topology |

Use a small musical fixture and a dense fixture with at least 16 channels, multiple instruments, two returns, a group bus, sidechains, stacked pattern graphs and overlapping modulation sources. Include long/duplicate names, disconnected nodes and mono/stereo/multichannel capability reporting. Label unsupported cases honestly. Test at the actual available display size, both docked and floated; neither a fitted unreadable overview nor offscreen unit snapshots qualify legibility.

## 11. Implementation slices and release checks

| Slice | Deliverable | Checks before moving on |
|---|---|---|
| 1 — Navigation and Add | Breadcrumbs, stable viewport/layout, single contextual search, target selection, panel bridges, shortcut decisions, full action registry | Actual small/dense playback walks; search focus/Esc; original keyboard paths; screenshot readability; no graph mutation on view/filter changes |
| 2 — Connections and structure | Typed ports/validation, compatible add-on-drag, insert/detach/heal/cut, group boundaries, source/output maps, mixer sync; safe live plan transition | Graph/API invalid/stale/no-op cases; one Undo per gesture; save/reopen; branch preservation; cycle rejection; sample/MIDI continuity, latency and rapid-Undo transition tests |
| 3 — Visibility and listening | Per-port meters/overload state, silence tracing, scope/spectrum, latency overlays, temporary Listen here | Bounded telemetry/audio audit; known-level/overload/silence fixtures; tap correctness; monitor restore; actual 16+ channel display/performance run |
| 4 — Modulation | Song-level targets/sources, parameter ports/ranges, knob drops, optional vendor lookup, pattern/recorded source bridges, contribution inspection | Stable target identity; base/range semantics; stepped/read-only parameters; overlapping sources; original automation preservation; source navigation; audio partition/timing tests |

Build in a new QA directory and launch with a unique bundle identity and disposable song. Preserve the musician's current process and system audio defaults. Use explicit BlackHole output for unattended playback tests. Each slice keeps screenshots, source/executable hashes, fixture details and action logs. Test actual API sockets as well as model/AppKit handlers.

Live-edit qualification includes steady sine/impulse/noise fixtures, rewire/bypass discontinuity measurements, latency-bearing plugins, sidechain isolation, held MIDI notes, tails, transport continuity, callback partition invariance, preparation failures and realtime allocation/free/lock audit. Reuse the previous strict presentation gate without weakening it: the last pass averaged about 54 fps in its stress fixture and did **not** qualify as 60 fps. A new implementation must investigate/measure this, not inherit a pass.

## 12. Current-code gaps and decisions for review

Source inspection, not a completion claim:

- `SignalGraphEditor` already requests implicit routing, supports port drag, has live selected-wire fields and hides the keyboard form by default. Preserve that work; current empty/enabled wording is not the whole implementation.
- `SignalCanvas` has audio/control as a Boolean distinction, node/cable selection, multi-node drag, insertion and saved positions. It lacks the richer type/metadata model, meters, bypass flags, frames and reroute geometry described here.
- `SignalGraph` stores flat reusable definitions, audio edges, modulation, assignments and layout; true nested boundaries/event routes and song-level modulation into rack parameters require shared model/runtime work.
- `SignalGraphAPI.inc` stops audio for processing mutations. Layout-only changes avoid this. Seamless topology changes need prepared-plan ownership/retirement and transition qualification.
- `SignalRuntime` currently uses stereo buffers per port; AU/VST3 auxiliary routing accepts mono/stereo. **Multiple buses are not arbitrary multichannel audio.** Show real catalog counts and unsupported layouts immediately; arbitrary N-channel routing requires extending buffers, mappings, compensation and both host adapters before it is advertised as supported.
- Recipe modulation currently accepts continuous writable parameters; stepped parameters require explicit quantization support. Existing MIDI-controller sources do not constitute a general note-event patching engine.
- Host custom-editor parameter hit testing is optional. A host-owned searchable parameter surface and last-touched bridge are the guaranteed routes.
- Mixer and reusable graph plans remain separate internal representations today. A common graph projection/edit service must reflect both without duplicating ownership; a cosmetic unified canvas alone will not remove routing restrictions.
- Some architecture/skill prose still mentions split Undo and obsolete Apply controls. Current source and the previous pass's chronological-history tests take precedence; update those documents with implementation, preserving distinct revision tokens where the API still uses them.

**Questions before changing established behavior:**

1. **Space:** retain global transport in the graph (recommended), or make plain Space open Add when the canvas has focus? Shift+A works in either case.
2. **Tab:** retain node cycling with Return to enter and Option+Up to leave (recommended for continuity), or adopt Tab to enter/exit groups and move node cycling to another binding? The alternate Add-on-Tab behavior is incompatible with using the same Tab for groups without a context rule.
3. **Editing reused groups:** when entering a group used on several channels, should edits continue to change its shared definition/all uses, or default to making the selected use independent? Both explicit actions remain available. The breadcrumb and inspector must show the scope before any edit. This also determines the remaining custom-editor template boundary from the previous pass; no silent behavior change is assumed.

All other listed removals are relocations: the exact keyboard patcher, Arrange, Reload, full port settings and preset/library actions remain reachable through context menus and ⌘K. Catalogue publication remains explicit, as previously requested.

## References

The familiar detach, mute, collapse, group and cut vocabulary draws on [Blender's node editing manual](https://docs.blender.org/manual/en/4.2/interface/controls/nodes/editing.html), adapted to ScreamSeq's musical-key and transport requirements rather than copied wholesale.

Custom-editor target lookup is conditional because [VST3 IParameterFinder](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IParameterFinder.html) is an optional plugin interface. Its presence must be queried and verified per view.

Current local implementation: `mac/App/SignalCanvas.swift`, `SignalGraphEditor.swift`, `SignalSongOverview.swift`, `SignalGraphPatching.swift`; `editor/SignalGraph.hpp`, `SignalRuntime.hpp`, `MixerRuntime.hpp`; `mac/Bridge/SignalGraphAPI.inc`; `mac/Audio/NativeSignalGraph.cpp`, `AudioUnitHost.mm`, `VST3Host.mm`. Previous actual-UI evidence is in `SCREAMSEQ_INTERACTION_JOURNEYS_2026-09-24.md` and its screenshot gallery.
