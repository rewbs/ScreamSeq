# Resonance: connected workspace and signal graphs

A tested development checkpoint is ready at:

`bin/mac-checkpoints/2026-09-21-connected-graph/Resonance.app`

The previous multisample checkpoint is untouched. No user song was overwritten,
no changes were committed or published, and no system audio route was changed.
This is a substantial implementation checkpoint, **not a claim that every part of
the requested graph/UI experience or sustained 60fps qualification is finished**.
The desktop locked partway through the work and computer control cannot unlock it.

## What is implemented

### Connected workspace

- Retained right, lower and secondary docks, plus detachable windows that can live
  on another display. Pattern editing remains accessible beside the tools.
- Independent Follow/Pin, Cursor and Return actions; target labels and focus outlines.
- Compose, Sound design and Pattern focus layouts; custom save/restore and retained
  panel instances. The main window initially uses the largest available screen.
- Pattern, graph, note inspector and pattern automation can coexist. Mixer,
  samples, instrument envelopes, plugin controls and graph commands occupy the
  same workspace rather than replacing the pattern.
- Cmd-K command palette, configurable single shortcuts and two-to-four-key
  sequences, prefix conflict checks, keyboard panel management and explicit Live
  keys mode. Normal note typing stays available outside a command prefix.
- Compact automation controls and sample/instrument panels. Waveform, loops and
  envelope editing take priority; less frequent tools use disclosure sections.
- Uncommitted sample/instrument fields survive background refreshes and hold their
  panel target. Applying one field preserves unrelated drafts. New documents
  retire the previous document's context.
- Workspace panels/layouts and shortcut configuration are exposed through the API.

### Shared graph and reusable library

- Named, numbered, document-owned subgraph definitions with stable identities,
  create/clone/rename/delete, saved canvas coordinates and document Undo/Redo.
- A shared audio/modulation canvas with directional audio edges, distinct dashed
  modulation edges, selection tracing, node/port dragging, zoom, fit and keyboard
  node navigation. Connection selectors provide a keyboard alternative to dragging.
- Song overview for channels, groups, sends, rack effects, native plugin instrument
  outputs, subgraph copies, sidechains and auxiliary outputs. Channel filters keep
  required dependencies, including nested sidechain groups.
- AU, VST3 and built-in effect recipes, with independent playback processors for
  each target bus and each row/persistent/ordinary role.
- Plugin parameter and port catalogs, native-unit parameter editing and recipe
  state capture. Custom interfaces edit a private library draft; Apply saves it
  in one Undo step. Stale recipes/documents cannot silently overwrite newer ones.
- Multi-input/output routing, parallel paths and per-path latency compensation.
  Feedback through a plugin's own delay is supported; implicit zero-delay audio
  or modulation cycles are rejected.
- Amount macros, LFO, random, envelope follower, note-triggered attack/release
  envelope and MIDI CC sources. Modulation targets stable plugin parameter IDs,
  with ascending/descending ranges and a separate base value.
- Live activity markers and actual stack-order numbers on subgraph copies;
  releasing tails are distinguished. `graph.get` exposes the same observation.
  Overview wires show configured routes; they do not rearrange nodes during play.

### Pattern graph activity

- Dedicated graph lanes alongside the pattern grid, sharing rows, scrolling and
  play position. They can target channels or summed groups.
- Row, Start, Stop, Clear, Amount and Wet commands, including initial Amount/Wet,
  optional tails and positions at 1/65,536 row resolution.
- Row graphs → persistent graphs → ordinary channel graph → existing inserts.
  Starting A then B gives A→B. Repeating an active Start updates it in place.
- Row copies expire on the next row. Stop cuts the named copy's audible output by
  default. Clear stops the persistent stack. Downstream processors retain audio
  already in their own buffers; cutting a graph does not reset other plugins.
- Inactive processors receive silence continuously, including their external
  inputs, while transport position advances. Bypass compensation keeps its place
  when a graph stops so remaining processors do not suddenly move in the chain.
- Lane/command edits, library recipes, routes and layouts are persisted in native
  metadata version 10 and accessible through the revision-checked API.

## Audio corrections found during qualification

1. VST3 ramps previously caused one-sample processing calls. The host now sends
   linear endpoints through bounded VST3 parameter queues inside larger buffers.
   Keeping those endpoints at double precision also preserves the existing
   bit-exact fixture comparison across callback sizes.
2. Delayed graph stops could replay stale wet padding. Each copy now continuously
   tracks its dry compensation. Stopping one graph also retains its position
   relative to other delayed processors. Tests cover both a single delayed copy
   and stopping stages in an A→B stack.
3. Repeated identical notes now have distinct native onset generations so note
   envelope sources retrigger without retriggering on every audio callback.
4. Graph reads that omit plugin state skip encoding those state blobs entirely.
   Graph-owned audio storage, including bypass and auxiliary buffers, is bounded
   before playback. Third-party private allocations remain outside that budget.

## Verification completed

| Check | Result |
| --- | --- |
| Complete CTest regression suite | 67/67 passed, including CoreMIDI and dense concurrent edit/audition stress |
| Native interface suite | Passed, including graph lanes, routing context, draft preservation, keyboard sequences and activity display |
| Actual application API tests | Passed: socket protocol, strict validation, revision guards, Undo, discovery/schema parity and existing workflows |
| Actual application workspace/startup | Passed: follow/pin/return/layouts, shortcut API, and 127-channel document startup |
| Address/undefined-behaviour sanitizers | 6/6 focused graph/routing/ramp tests passed; leak detection disabled for system/plugin runtime objects |
| Stock OpenMPT fidelity | 30 render comparisons at 44.1/48/96 kHz, all bit-exact; no intercepted callback allocations, releases or locks |
| Virtual Core Audio loopback | Dry, AU effect/instrument and VST3 effect/instrument cases matched reference PCM exactly |
| Sustained graph loopback | 60 seconds, 48 kHz/512 frames, 2,880,000 stereo frames, exact PCM match, no timestamp discontinuities and zero callback overruns |
| Graph tutorial export | Project reopened and rendered successfully; finite audio with peak 0.0656 and no clipping |

The loopback uses the installed **BlackHole 2ch** device directly. It does not
record the microphone, change the default output, or play through the speakers.
It provides meaningful background testing even while the desktop is locked. It
is not a measurement of physical DAC latency or speaker sound.

Sixteen simultaneously modulated graph copies, 1,000 callbacks per case, 128-frame
buffers (offline processing measurements, not a commercial-plugin capacity claim):

| Processor | p99 at 48 kHz | p99 at 96 kHz |
| --- | ---: | ---: |
| Built-in gain | 0.172 ms | 0.158 ms |
| VST3 gain fixture | 0.057 ms | 0.063 ms |
| Apple AU low-pass | 0.476 ms | 0.495 ms |
| Available callback time | 2.667 ms | 1.333 ms |

All six cases had zero intercepted realtime allocations/releases/locks. Peak
process memory across the benchmark was about 89 MB. Test logs and measurements
are copied into the checkpoint's `evidence` directory.

## What remains

- **Unlock-dependent qualification:** final live visual inspection of the complete
  workspace, commercial AU/VST3 custom interfaces, real editing/playback workflows,
  sustained 60fps presentation, combined screen/audio soak and physical listening.
  Earlier workspace foundations were inspected live; subsequent graph/compact UI
  changes have automated and offscreen checks only. Some offscreen native controls
  render as white blocks while locked, so those images cannot qualify appearance.
- **Hot graph edits:** structural changes, recipe changes, assignments and command
  changes currently stop playback to prepare independent instances safely. Names,
  numbers and layout changes preserve playback. Seamless graph replacement remains
  a significant next engine step.
- **Direct sample-instrument graph assignment:** current graph inputs cover mixer
  channel/group audio and native plugin instrument outputs. A sample instrument's
  own pre-channel graph is not implemented yet. Instrument-level pattern graph
  switching was explicitly deferred in the discussion.
- **Graph-owned automation curves:** graph modulation sources and Amount/Wet pattern
  commands work, but the existing drawn pattern-automation editor still addresses
  rack parameters. Binding it directly to graph-owned parameters/macros remains.
- **More complex latency transitions:** fixed compensation, bypass, tails and
  delayed A→B stops are tested. Reordering several already-active latency-bearing
  chains still needs a defined transition policy and broader qualification.
- **Further UI consolidation:** specialist tools retain some separate nonmodal
  windows. The major musical editors have workspace panels; the complete keyboard,
  focus and small-window audit must be completed on the unlocked desktop.

## Questions queued for morning

1. If a Row command names a graph already active persistently on the same channel,
   should it temporarily override that instance or add a second copy for that row?
   The current implementation uses separate row/persistent copies and stacks them.
   Repeating Start within the persistent stack updates the existing copy.
2. Should the reusable library also have a global preset collection shared across
   songs? Currently definitions and plugin state travel inside each project.

No answers were needed to complete the current checkpoint. The desktop must be
unlocked manually before the remaining live UI qualification can proceed.

## Trying the checkpoint

Open the checkpoint application and load `Connected-Circuit.resonance` from the
same directory. Cmd-Option-G opens the graph; Cmd-Shift-G opens the current row's
graph command inspector. The sample project has three definitions: Liquid comb,
Crunch and Listening trim. Its lead demonstrates row-only effects, persistent
stacking, a half-row Amount change, Stop and Clear; the kick uses an independent
copy. An ordinary lead graph and master trim remain in the regular signal path.

`GRAPH_WORKFLOW.md` in the checkpoint explains controls and API usage. The tutorial
was generated with `mac/Tools/create_graph_demo.py` through the public local API,
and `Connected-Circuit.wav` is its offline render. It uses only bundled effects.

App executable SHA-256:
`0e720d55508502c633e8f52e77922a632c3d7c1b65e2308be00ff5407418198b`
