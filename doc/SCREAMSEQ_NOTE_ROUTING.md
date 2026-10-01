# Plugin-instrument note routing

The song graph has a second signal type: note/MIDI events. Audio routing stays
independent. A channel Notes output forwards only plugin-instrument events;
sample-instrument notes do not become MIDI. An instrument Notes output can route
the events generated for that plugin instrument. A plugin's Notes input is an
event destination, separate from its audio buses and sidechain ports.

An instrument's existing plugin assignment supplies its implicit default cable.
Adding cables is additive. Disconnecting that default cable suppresses delivery
without disabling note generation in the tracker. Muting or editing an implicit
cable materializes an explicit replacement in the same transaction. Suppressed
assignments persist until explicitly restored.

Removing an instrument's originally assigned plugin preserves its note generator
when explicit cables still route it to another plugin. The song retains that
instrument and its MIDI channel in `noteRouting.triggerSources`; it has no
implicit destination. Saving/reopening and Undo retain this source. Explicitly
unassigning it with `instrument.plugin.set {instrument, plugin:""}` restores its
sample mapping for future notes. Existing cables remain visible but inactive;
sample notes never become MIDI merely because those cables remain.

A new destination waits for the next note-on. It never receives synthetic held
notes. Removing, muting or repatching a cable sends the matching note-offs for
that cable's held generations. Two paths to the same destination/channel deliver
one note-on; either original path can retain that note. A path added after a note
started cannot acquire ownership of it. Undo restores routing for future notes,
without restarting old notes. Repeated pitches and new-note-action voice moves
retain their original source ownership.

MIDI channel 0 preserves the instrument's channel; 1–16 selects a destination
channel. Channel-global MIDI messages retain MIDI 1.0 semantics: for example,
a shared destination/channel pedal stays held until its last contributing pedal
owner releases it. Route removal releases a pedal when its last owner disappears.
Use separate destination MIDI channels where independent pedal behavior is needed.

## Agent API

`graph.get` includes `noteRouting.routes` and `noteRouting.suppressedAssignments`.
Stable native IDs identify channels, instruments and explicit cables; rack plugin
instance IDs identify destinations. All writes require `expectedRevision` and
support `dryRun`; successful musical edits participate in the unified Undo history.

- `graph.note.connect`: `sourceKind` (`channel` or `instrument`), `source`,
  `plugin`, optional `midiChannel`, `enabled`, `suppressAssignment`.
  The last option applies only to instrument sources. Result `route` is the new
  cable ID.
- `graph.note.update`: `id` and any changed source, plugin, channel or enabled
  fields. Preserves the cable identity.
- `graph.note.disconnect`: exactly one of `id` (explicit cable) or `instrument`
  (implicit assignment).
- `graph.note.restoreAssignment`: `instrument`, restoring the implicit cable.
- `graph.connections.remove`: a `connections` batch may contain
  `{kind:"note", route:"n…"}` or `{kind:"note", instrument:"n…"}` alongside
  audio/control cable identities. The whole cut is one atomic Undo transaction.
- `graph.note.activity`: no parameters. Reads actual accepted delivery counters
  from the prepared engine, without changing transport or starting a device.

Sub-tick pattern pitch curves follow these note routes, including channel
mapping and deduplication. Their MIDI events retain the exact sample offsets;
the host does not call a VST3 processor once per sample.

## Activity and cable geometry

Activity reports an `engine` identity, `sampleRate`, `requestedGeneration`,
`adoptedGeneration`, `pending`, `fresh`, and `routes`. Route counters include
accepted events, note-ons/offs, held notes/pedals, delivery failures, route-owned
releases and `lastFrame`. `token` identifies a delivery incarnation; `copy`
identifies its prepared plugin endpoint. Both are runtime identities, not saved
project IDs. `current` means requested membership; `member` means adopted
membership. Retired or pending entries cannot establish current silence.
`fresh` describes a coherent route-adoption snapshot, not a guarantee that
independent counters were read at an identical sample.

A removed cable can release its ownership without delivering another note-off
when another original cable still owns that destination's note. Counters expose
this distinction. This is host-delivery observation, not vendor-internal MIDI
processing or audio output metering.

Saved cable geometry accepts optional `connection` keys (`note:n…` or
`note-assignment:n…`) to distinguish different MIDI channel mappings between
the same sockets. Event sockets use port `4294967294`. Repatching preserves
reroute points; disconnect removes their geometry; Undo restores both.

## Implementation and qualification status

`editor/NoteRouting.*` contains the portable model, immutable delivery plan and
bounded audio-owned note ledger. Plans are prepared off the audio callback;
active voice ownership is reconciled at a render boundary. The ledger retains
silent generations until their note-off, preventing an old note-off from ending
a later retrigger. Capacity exhaustion is detected before sending any fanout.

The portable ownership tests and native AU/VST3 held-note routing renders have
passed, including callback partitions and the host realtime audit. Mac API,
history and save/reopen tests have also passed. Further telemetry, pitch-routing
and integrated UI/platform checks are in progress; the current qualification
record is under `doc/mac-native-qualification/2026-10-01-graph-completion/`.
