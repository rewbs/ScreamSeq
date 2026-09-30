# Connected workspace and signal graphs

ScreamSeq keeps the pattern visible while tools occupy the right, lower and
secondary docks. A pin keeps each panel on its current target. Its overflow menu
contains Follow cursor, Inspect editing cursor and Return to opening row.
The panel outline marks keyboard focus; its target label identifies what it is
inspecting. Text drafts hold their target rather than following away mid-edit.
Compose shows pattern, note inspector, graph and pattern automation together.
Sound design and Pattern focus are alternate layouts. Panels can also float on
another display, and layouts retain the same editor instances.

Cmd-K opens the searchable command palette. Workspace commands also appear in the
menu. Set shortcut records one key; Set sequence records two to four keys,
confirmed with Return. Conflicting prefixes are rejected, and sequences time out
after 1.5 seconds. Agents can list/configure these through `workspace.commands.get`
and `workspace.shortcut.set`. Cmd-Option-G opens the graph; Cmd-Shift-G opens graph commands for the current
pattern row. Live keys is an explicit mode that keeps the musical keyboard active
while another panel has focus; disable it to type text normally.

## Building a reusable effect chain

1. Open the graph, use Add… (Shift+A), and find New reusable group. Give it a name and number in its
   inspector. The initial Input→Output connection passes audio unchanged.
2. Add… searches the cached plugin catalogue alongside groups and sources. An effect is inserted into the
   selected node's single main connection, or before Output when unambiguous.
   More complex routing can be connected manually.
3. Select an effect to see its searchable parameter controls. Parameter values use the plugin's native
   units. Named audio sockets and port menus come from its bus catalog. Hollow auxiliary
   sockets enable automatically when connected. AU and VST3 custom interfaces edit a separate library draft;
   Apply plugin settings saves that draft in one document Undo.
4. Add sources such as Automation, Amount, LFO, follower, note envelope, random or MIDI CC.
   Choose Modulation, its source, destination and stable parameter ID. The
   Modulate button beside a loaded parameter supplies that ID. Ranges use
   normalized values; descending ranges invert a source. Contributions add to a
   shared base and clamp to 0…1. Use the parameter menu to expose a named parameter socket, then drag a gold source socket to it. Drag a parameter socket to blank canvas to create a compatible source with zero initial depth. Audio uses separate green sockets for each supported bus.
5. Return to Song graph. Select a channel or group and assign an ordinary graph,
   with separate Amount and Wet values. Its independent copy appears before the
   channel's regular inserts. Open the copy to edit the shared definition.

Song graph also shows rack effects, plugin instrument sources, sends, plugin
sidechains, and subgraph external inputs/outputs. Connection controls distinguish
main output, send, graph sidechain, graph auxiliary, plugin sidechain and plugin
auxiliary. Main port is 0; auxiliary ports are 1…63. Graph Input/Output nodes expose
ports simply by connecting those ports inside the definition. Filters retain
sidechain dependencies, including nested groups. Dragged overview positions are
saved with the song. During playback, copies show their actual stack order and a
green activity marker; releasing tails are amber. The overview wires show the
configured routes, while these badges identify the currently sounding stack.

Audio routes and modulation dependencies must be acyclic. Plugin feedback effects
are supported; a wire forming a zero-delay graph cycle is rejected. Auxiliary
outputs can feed multiple buses (including channels), with delay compensation. Main
effect outputs also continue through their insert chain; extra output taps are
post-insert and pre-fader, with owner mute/solo applied.

Click a wire **or its label** to show the **Selected connection** inspector at the
top of the sidebar. Double-click, Return, or right-click → Edit connection focuses
its settings. Changing its endpoints, ports, gain or modulation range commits
immediately without deleting neighboring connections. Drag the amount badge on
a selected adjustable wire for gain/depth; double-click it for exact entry.
**New…** switches to
creating a separate route. Main outputs and sends keep their owning source bus;
plugin routes keep their owning port. To replace a locked endpoint, remove the
route and create a new one. Wires within a channel's fixed insert order instead
offer **Open subgraph…** or **Open mixer / assignment…** so the editable owner is
one click away. Audio gain inside a
subgraph is a multiplier; song sends and sidechains use dB. Option-Tab selects
wires, Tab selects nodes, arrows move nodes and Delete removes the selection.
Plus/minus zoom; Arrange lays out dependencies; Fit shows the complete graph.
Escape cancels a drag without saving it. Connection menus support the same edits
from the keyboard. Float a crowded graph panel using its upper-right overflow menu.

## Patching and moving nodes

For **Channel 1 → Compressor**, drag Channel 1's Main out to the compressor's
**Main in**, or select those endpoints and ports then Connect. This moves the
compressor and its following inserts onto Channel 1. For **Channel 2 → detector**,
drag Channel 2's Main out to **Detector sidechain**, or pick that named input in
the connection form. The detector port enables at playback preparation. New built-in compressors use
Auto detector mode; for an existing compressor forced to Internal, select it and
click **Use connected detector (Auto)**. Third-party plugins may also require
their own External/Sidechain switch in the custom interface. Channel
2's existing output remains; delete that separate output wire if the key should
be inaudible. A hollow port means available but currently inactive.

A socket drag adds a cable; selecting a wire and dragging its round handles
reroutes that wire. Multiple sources into one input sum. Multiple destinations
share one processed output. **Option-drag** to an insert's Main in, or choose
**Mix into main** in the form, to sum an additional channel into its existing
main input instead of moving the chain. Main/auxiliary plugin output fan-out is
independent of its normal serial path. Mono and stereo buses are supported;
unsupported multichannel layouts are not offered as destinations.

Shift-click or Command-click toggles nodes in the selection; dragging empty
canvas draws a selection rectangle. Drag or use arrow keys to move the selection
together. Drop a selected effect or connected serial effect chain onto a
highlighted audio wire to insert it there. The old main path reconnects, while
auxiliary and modulation cables remain. Branched main paths are rejected with an
explanation rather than silently losing connections. In the song overview, drop
rack inserts onto another insert wire or a channel-output wire. Open a reusable
subgraph to patch its individual effects. Routing and position are one Undo.

## Signal inspection and processing groups

Select a host channel or rack processor to see its measured input/output levels.
More → Find next overload visits a latched measured overload; Clear overload
indicators resets the latch. Trace silence follows the selected path and reports
known mute/disconnection or measured input/output state. It cannot diagnose a
vendor's internal patch from a silent output alone. Unavailable measurements are
distinct from zero; stopped playback is labelled Stopped.

Hold Q over an observed audio wire for its waveform, or Shift+Q for spectrum.
The scope identifies the actual host-port tap; it is not yet an exact post-gain,
post-compensation measurement of every cable. Recipe-internal/control/event
telemetry remains incomplete. The scope reports dropped capture frames.

L or a card's headphone badge temporarily listens to that host output. Processing
and sidechains continue upstream and downstream; only the monitor mix changes.
Escape or Stop listening restores the mix. The main pattern workspace keeps a
Listening indicator visible even when the graph is closed. This session state
does not change saved solo, routing, Undo or the system audio device.

Marquee-select processors and press Control+G to package a processing group.
Type its name and Return. Double-click enters the boundary; the breadcrumb goes
back with view state retained. Real processor and parameter identities remain
unchanged. Groups can nest; moving a boundary moves its contents. More → Ungroup
retains the processors. Save to subgraph library makes an independent recipe copy
when the selected chain has a valid exposed boundary. Editing a reusable recipe
changes all its channel/instrument/pattern uses; the inspector states that scope.
These groups are distinct from a summing mixer group bus.

## Drawn automation inside a graph

Add an **automation** source, select it, and choose a pattern in the curve editor.
Draw or drag points and change the selected point's outgoing curve. A pointer
gesture saves once when released; numeric fields save on Return or focus change.
Formula edits save after a short typing pause. No Apply step is required.
The curve repeats with that pattern in every independent copy of this definition.
Connect its gold output to any exposed plugin parameter. Multiple sources can
contribute to the same parameter with their own ranges and one shared base.

All nine curve types are available, including Scripted. Formulas use the same
bounded expression language as pattern automation: `start`, `end`, `t`, `beat`,
`beats`, `duration`, mathematical functions and deterministic noise. The final
scripted segment extends to pattern end. Missing/disabled curves output zero.
Points use 256 units per row. The host sends smooth sample-offset ramps between
bounded evaluations; step boundaries remain discrete. A plugin's own smoothing
can still affect the audible result.

Pinch/Option-scroll or +/− zoom the curve; ordinary scrolling pans. Tab selects
points; arrows adjust timing/value and Shift gives fine adjustment. Numeric Row
and % fields commit in place. Each completed save holds the original
source/pattern/revision while the rest of the workspace changes. Deleting the
last point removes this pattern's curve. Edits made during an in-flight save
follow it using the accepted revision. A rejected edit remains visible; More
contains Retry saving changes and Reload / discard pending changes. Pattern duplication copies curves; shortening/removing patterns
prunes out-of-range data.

## Sample instruments before channels

Select a sample instrument card in the song graph and assign a library definition,
Amount and Wet in the inspector. Double-click an assigned copy to enter its shared
definition; parameter controls identify its instrument context. Show in pattern
returns to the target channel. The overview shows
its independent copies feeding each raw note channel. Notes keep their original
channel processing after the instrument stage; older NNA voices retain their
instrument stage when a channel begins a different instrument.

The order is instrument graph → row graphs → persistent graphs → ordinary channel
graph → channel inserts. A shared instrument on two channels has two processor
histories. Plugin instruments continue to use their output bus graph because
one multitimbral plugin can combine its voices internally. Individual sample-zone
graphs and instrument graph-switching pattern commands remain separate features.
The graph budget is shared across channel and sample-instrument copies: 256
processors and 256 MB of host-owned graph audio storage, plus the existing core
adapter/output-route limits. Oversized configurations reject before playback.

## Pattern graph lanes

Graph commands have dedicated lanes alongside the pattern grid, aligned to the
same rows, scrolling and playhead. Lanes can target groups as well as channels.
Return or double-click opens the command inspector; Delete removes the selected
cell. A `~` marks a command offset inside its row.

| Command | Action |
| --- | --- |
| Row (`R`) | Apply a graph until the next row. |
| Start (`S`) | Add a persistent graph, or update its existing Amount/Wet. |
| Stop (`X`) | Stop that named graph's row and persistent copies. |
| Clear (`CLR`) | Stop all persistent copies on the target. |
| Amount (`A`) | Change that graph's Amount macro on active pattern copies. |
| Wet (`W`) | Change its wet/dry mix independently of Amount. |

Processing order is row graphs → persistent graphs → ordinary channel graph →
channel inserts. Starting A then B gives A→B. Repeating A updates A without moving
or duplicating it. Commands accept an initial Amount and Wet and a fractional-row
offset. The API uses 65,536 units per row and applies events at the next sample.

Library reuse creates independent processors for each bus and each row,
persistent and ordinary role. Inactive copies continue processing silence with
transport; their external inputs also receive silence. Stop cuts their output by
default. Optional tails mix back at that copy’s retained stage before the ordinary
graph. Processors downstream retain audio already in their own buffers. Bypassed
copies retain their compensation delay so a stop does not move remaining stages. Active copies sharing
an external output number contribute to the same auxiliary bus.

## Agent API

All graph changes use the same revision-checked local API as the UI. Writes return
the new revision; stale writes fail without committing part of the operation.
`dryRun` validates document mutations without saving them. Graph history belongs
to the document Undo domain. Recipes, routes, lanes, commands and overview layout
are saved in native project metadata version 10 or newer. Automation sources and
instrument graph assignments require version 13; older applications reject those
projects rather than dropping their data.

| Methods | Purpose |
| --- | --- |
| `graph.get` | Library, assignments, routes, lanes, commands, layout, mixer/rack overview and read-only playback activity. `includeState:false` omits opaque recipe blobs. |
| `graph.create`, `.clone`, `.update`, `.remove` | Manage definitions with stable identities and unique numbers. |
| `graph.node.add`, `.remove` | Allocate/remove nodes. `insertAfter` atomically inserts an effect into a single main connection. |
| `graph.assign` | Ordinary bus assignment; null clears it. |
| `graph.instrument.assign` | Sample instrument assignment by instrument slot; the stored target is stable identity. Null clears it. |
| `graph.automation.get`, `.set` | Read/replace one source’s pattern curve; empty points remove it. |
| `graph.commands.set` | Upsert lane counts; replace the specified pattern's command collection when supplied. |
| `graph.routes.set` | Replace supplied external input/output route collections. |
| `graph.layout.set` | Merge saved node coordinates, or reset them. |
| `graph.plugin.get`, `.set` | Inspect/edit recipe parameter values and enabled buses. |
| `graph.plugin.editor.open`, `.commit`, `.close` | Open, save or discard a custom-interface draft. Commit rejects a changed recipe/document. |
| `graph.controller` | Send live MIDI CC values to prepared graph sources. |
| `mixer.plugin.route` | Route plugin instrument or effect auxiliary outputs. |

`graph.update` retains plugin state if a node's recipe omits `state`; this makes
reads with `includeState:false` safe for editing connections and positions.
New node identities must be allocated by `graph.node.add`, not invented by clients.
Parameter and bus catalogs are queried explicitly rather than loading every
plugin during graph navigation.

Example using an already-connected Python `Client`:

```python
revision = client.call("document.get")["revision"]
def write(method, **params):
    global revision
    reply = client.call(method, {"expectedRevision": revision, **params})
    revision = reply["revision"]
    return reply["data"]

write("mixer.enable")
graph = write("graph.create", name="Quiet hit")["graph"]
view = client.call("graph.get", {"includeState": False})["data"]
definition = next(d for d in view["library"] if d["id"] == graph)
input_node = next(n["id"] for n in definition["nodes"] if n["kind"] == "input")
node = write("graph.node.add", graph=graph, kind="plugin",
             plugin={"format": "Built-in", "classID": "resonance.gainer.v1"},
             insertAfter=input_node)["node"]
write("graph.plugin.set", graph=graph, node=node,
      parameters=[{"id": 1, "value": -6}])
target = next(b["id"] for b in view["mixer"]["buses"] if b["kind"] == "track")
# This replaces pattern 0's graph commands; read/merge existing commands first
# when augmenting a song that already contains them.
write("graph.commands.set", pattern=0,
      lanes=[{"target": target, "count": 1}],
      commands=[{"target": target, "graph": graph, "position": 0,
                 "kind": "row", "amount": 1, "wet": 1}])
```

## Qualification and current limits

Automated tests exercise hosted graphs and the full song renderer at 44.1, 48 and
96 kHz, callback partition invariance, sidechain/auxiliary routing, group command
stacks, revision guards, Undo, persistence and callback allocation/lock auditing.
Graph storage and processor counts are bounded before playback. Third-party
plugins' private memory cannot be bounded by this host budget.

Fixed-topology parameter baselines, modulation ranges, source settings, audio-edge
gains and graph curves publish to prepared copies while playback continues.
Bypass fades to latency-aligned dry audio (silence for sources), retaining processor
state. Mixer reroutes with unchanged processor dependencies, note membership,
adapters and total latency also transition live, including songs containing
ordinary, pattern and sample-instrument graphs. General structural edits and
dynamic latency/bus changes still require further live-plan work and can stop
playback. Names, numbers and canvas positions do not rebuild audio.
Delayed stop and reactivation/reordering transitions
are tested against a continuous reference: they switch immediately, keep plugin
histories advancing, and retain the total reserved compensation. They do not
promise a click-free crossfade for deliberately abrupt pattern commands.

See the current ScreamSeq delivery report for measured UI/audio qualification and
commercial-plugin limits. Historical locked-desktop reports are not current
performance evidence.


### Cable gestures and moving rack chains

Drag either an input or output socket to a matching socket. Socket drags add another source or destination, retaining existing wires. In the song view, Option-drag an insert Main input to sum another channel; a normal main-input drag moves the chain. Select a wire to expose round handles just outside both nodes, then drag a handle to reroute that endpoint. Escape or dropping in empty space cancels; use Delete on a selected route to disconnect it.

In the song graph, drag a channel output onto a rack effect's main input to move that effect **and all subsequent inserts in its chain** onto the channel. For example, Track 8 → Distortion moves Master’s Distortion → Compressor chain onto Track 8; Track 8's output still feeds Master. Dragging Distortion's existing input back to Track 8's output performs the same atomic edit. A processor output inserts the moved chain immediately after that processor. Plugin identities/settings and automation survive, with one document Undo. Sidechains and auxiliary routes are validated, so a move introducing feedback is rejected unchanged.

Main is a final sink at the right of the default layout. If it has processing, **Master input** represents the summing stage before those effects, and **Master** represents the final output after them. Saved manual positions are retained; Arrange places visible nodes in flow order.

Filter nodes with the text field (name, type or ID), the type menu, and channel focus. Clear filters restores the complete song view. Only wires with both endpoints visible are shown. An in-progress connection form remains visible while filtering so typed values are retained. Filters do not edit audio routing.

Reusable graph internals support free audio/modulation rewiring with their port types and cycle validation. The song overview still represents bus ownership: moving a rack chain is distinct from changing a bus output, and pattern-controlled/ordinary subgraph copies are assigned through their assignment controls. An owned main-output/send/aux route cannot transfer to a different owner merely by editing its endpoint; those operations explain the constraint in the status line. Double-click a reusable copy to edit its internal graph.

### Sidechains, additional ports and group editing

For a compressor on Track 1 controlled by Track 2, connect **Track 1 / Main out → Compressor / Main in**, then **Track 2 / Main out → Compressor / Detector sidechain**. The first gesture moves the rack chain onto Track 1. The second adds an independent detector feed. The same named sockets are available in the From/To port menus; auxiliary ports activate automatically when wired. Track 2's normal audible output remains connected. Delete that separate output wire if the track should act only as a silent detector source.

New built-in Compressor, Gate and Bus Compressor instances default to **Auto (use connected sidechain)**. For an existing instance, select its graph node and click **Use connected detector (Auto)**. Third-party plugins can also require their own external-detector switch; that remains a plugin setting.

Supported plugin buses appear individually with their native names. Hollow sockets are available but inactive; a connection activates them on the playback copy. Outputs can feed multiple destinations and several sources can sum into one input. The song view uses channel/bus sources for insert sidechains; route a plugin output through a bus to use it as a detector source. Reusable graphs allow direct internal plugin-to-plugin wiring. Audio buses currently support mono/stereo, not surround layouts. Master remains the final sink; move its effects onto a track or return before branching them downstream.

**Shift-click** or **Command-click** toggles nodes in the selection. Drag a rectangle from blank canvas to select a group; arrows or a node drag move it together. Drop a selected effect, or a contiguous serial chain of effects, onto a highlighted audio wire to insert it there. The old main path reconnects automatically; internal chain order is preserved. Position and routing changes make one Undo step. A branched or disconnected selection cannot be inserted as a serial chain and is rejected without changing the song.
