# Connected workspace and signal graphs

ScreamSeq keeps the pattern visible while tools occupy the right, lower and
secondary docks. Each panel has its own Follow/Pin, Cursor and Return actions.
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

1. Open the graph and choose New. Give the definition a name and number in its
   inspector. The initial Input→Output connection passes audio unchanged.
2. Add effect opens the cached plugin browser. An effect is inserted into the
   selected node's single main connection, or before Output when unambiguous.
   More complex routing can be connected manually.
3. Select an effect and Load controls. Parameter values use the plugin's native
   units. Auxiliary port numbers come from its bus catalog. Enable ports before
   connecting them. AU and VST3 custom interfaces edit a separate library draft;
   Apply plugin settings saves that draft in one document Undo.
4. Add sources such as Automation, Amount, LFO, follower, note envelope, random or MIDI CC.
   Choose Modulation, its source, destination and stable parameter ID. The
   Modulate button beside a loaded parameter supplies that ID. Ranges use
   normalized values; descending ranges invert a source. Contributions add to a
   shared base and clamp to 0…1. Click Modulate to expose a named parameter socket; drag a gold source socket to it. Audio uses separate green sockets for each enabled bus.
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
outputs from effects/subgraphs route to groups, returns or master buses. Main
outputs follow their insert chain.

Click any wire to inspect it. Update wire changes its endpoints, ports, gain or
modulation range without deleting neighboring connections. Audio gain inside a
subgraph is a multiplier; song sends and sidechains use dB. Option-Tab selects
wires, Tab selects nodes, arrows move nodes and Delete removes the selection.
Plus/minus zoom; Arrange lays out dependencies; Fit shows the complete graph.
Escape cancels a drag without saving it. Connection menus support the same edits
from the keyboard. Float a crowded graph panel using its Panel menu.

## Drawn automation inside a graph

Add an **automation** source, select it, and choose a pattern in the curve editor.
Draw or drag points, change the selected point's outgoing curve, and press Apply.
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
and % fields use Set point. Apply is one Undo transaction and holds the original
source/pattern/revision while the rest of the workspace changes. Clear all points
and Apply to remove this pattern's curve. Edits made during an in-flight save
remain pending. Pattern duplication copies curves; shortening/removing patterns
prunes out-of-range data.

## Sample instruments before channels

Choose a sample instrument in the graph toolbar and click Instrument graph.
Assign a library definition, Amount and Wet in the inspector. The overview shows
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

Structural/recipe/curve changes currently stop playback to prepare safe independent
copies. Names, numbers and canvas positions can change while playing. Live
structural replacement and automatic response to changing plugin latency/bus
layouts remain engine work. Delayed stop and reactivation/reordering transitions
are tested against a continuous reference: they switch immediately, keep plugin
histories advancing, and retain the total reserved compensation. They do not
promise a click-free crossfade for deliberately abrupt pattern commands.

See the current ScreamSeq delivery report for measured UI/audio qualification and
commercial-plugin limits. Historical locked-desktop reports are not current
performance evidence.
