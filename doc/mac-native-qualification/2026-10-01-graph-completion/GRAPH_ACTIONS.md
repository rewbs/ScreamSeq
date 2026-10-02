# Graph actions — resumed implementation checkpoint

2 October 2026. This is an implementation/evidence ledger, not a completed native UI qualification.

## Editing semantics

- **Mute a modulation source:** `graph.source.mute {graph:null|ID,node,muted}`. The full weighted contribution, including a nonzero minimum, becomes zero. Its oscillator, random sequence, note gates and follower continue advancing. A reusable source affects all copies of that definition; a song source belongs to this song. Processor bypass and source mute remain different actions.
- **Make a reusable use independent:** `graph.makeIndependent {graph,target,scope:"channel"|"instrument"}` allocates a fresh definition and processor/source identities. A channel operation rebinds its ordinary assignment and every Row/Start/Stop/Amount command referring to the original definition, so command pairs remain intact. Other channels are unchanged. An instrument operation changes only its assignment.
- **Copy/cut/paste/duplicate recipe nodes:** `graph.selection.copy/cut/paste/duplicate`. Internal audio/control cables and nested groups follow their nodes; external cables do not. Cut, paste and duplicate each make one Undo entry. Paste needs an explicit source-to-destination `patternMap` for every copied pattern envelope; the UI supplies identity mappings within one document and asks for mappings between songs. It never binds unrelated patterns because their numeric IDs coincide. Copied curves become independent rather than acquiring implicit envelope-bank links.
- **Duplicate a rack processor:** `plugin.duplicate {plugin,position?}` creates a new stable plugin identity with the saved manual baseline, opaque state, bypass and physical bus settings. It copies no note assignments, automation or cables; effects begin detached. The effective value of a modulated parameter is not captured as the new editable baseline.
- **Recipe presets:** `graph.plugin.preset.save/load` reuse the normal native plugin preset format. Loading pins the inspected file revision, validates the descriptor and ports, and updates manual parameter bases while preserving routing and host bypass. Shared recipe copies receive the same change. Saving a preset is a file operation, not a song Undo entry.
- **Detach a branched recipe selection:** `graph.nodes.detach` accepts an optional `heal` boundary-edge choice. The UI asks which incoming/outgoing main path to reconnect when there is no unique choice. Only that path is healed; detach keeps other branches, and Delete and heal removes the selected processors and their remaining incident cables atomically. Gain on the healed path is the product of the chosen boundary gains. It never generates a fan-in/fan-out cross product.

All musical writes retain revision guards, validation, dry-run where offered, shared model semantics, persistence and unified Undo. Processor/source action entry points are in context menus and the complete graph command catalogue. Selection-based actions use stable identities and capture their document/revision context.

## Note-event graph UI

The Song graph has separate purple Notes sockets on channels, plugin-instrument sources and plugin instruments. These do not represent sample audio, physical plugin audio buses or modulation controls. Dragging or keyboard patching dispatches the note-routing API; audio/control-to-Notes connections are rejected visibly. A new route waits for the next note-on. Disconnecting or disabling a route releases only the notes owned by that route.

Existing plugin assignments appear as implicit cables. Changing one materializes an explicit replacement and suppresses its original assignment in one transaction; deleting it suppresses the implicit assignment so the cable does not immediately reappear. “Restore assigned instrument notes…” is available in the graph catalogue. Cable settings edit MIDI mapping and enabled state directly.

Distinct route IDs disambiguate cables sharing source/target sockets with different MIDI mappings, including selection/Back, in-flight gesture invalidation and saved reroute geometry. Mixed audio/note cable cutting uses one shared transaction. The event inspector reads `graph.note.activity`, displays exact adopted-route note/event/held ownership counters, and distinguishes stopped, unavailable, pending, stale and retired data. It never substitutes an audio meter for note activity.

## Evidence so far

- Shared `graph-editing` suite passed in the coordinated targeted builds; the latest additional note/branch cases still require the next complete test result to be recorded.
- Full copied-source AppKit suite passed at `/tmp/screamseq-graph-actions-interface.log`, snapshot `/tmp/screamseq-graph-actions-interface-26pz2l26`. This includes source mute, make-independent, clipboard/duplicate, recipe preset entry points and explicit branch-detach choices. It predates the new Notes UI.
- Full Notes UI/AppKit suite passed (exit 0) at `/tmp/screamseq-graph-notes-interface.log`, from the hash-recorded snapshot in `/tmp/screamseq-graph-notes-interface-snapshot.json`. It includes typed sockets, exact route identity, implicit replacement/restore, immediate MIDI controls, mixed-cable batch dispatch, duplicate dispatch, and current/adopted versus stale/retired event counters.
- Parent/runtime agents own rendered-audio, portable adapter, device and final native UI qualification. No musician session, device defaults or native app UI has been used by this workstream.

## Still in progress in this workstream

True processing-group boundary bypass (including ambiguous dry mappings), retained loose multi-effect chains and fixed rack-cable cut semantics, and explicit per-copy recipe audio/control observation need their shared runtime contracts and integration. Existing isolated-processor detach does not imply loose-chain support. Host/bus observation is not evidence of an exact reusable-copy signal. These remain explicit work, not hidden limitations presented as completed actions.

## Group and physical-port checkpoint (2 October, later pass)

Recipe groups now retain a real boundary bypass flag and explicit dry mappings.
`graph.group.boundary` returns stable entering audio-edge identities and outgoing
sockets; `graph.group.bypass` commits one complete map and mode change. The UI's
Bypass action uses this boundary rather than toggling each enclosed processor.
Ambiguous paths use a cancellable sequence of choices with no partial write;
“Group dry paths…” can edit the map without changing its current bypass mode.
Saved maps survive cloning and library export with remapped identities. An
exported outer wrapper is retained whenever its bypass or explicit map carries
processing semantics. Missing surviving input mappings are rejected, not guessed.

The recipe runtime now has rendered/audited tests for warm processors, nested
latent boundaries, dry substitution only on outgoing cables, internal controls,
full outward modulation suppression and callback-partition equivalence. Song
(root) group wrappers remain in progress; their active preparation is explicitly
rejected until that runtime exists. Root-source group membership and wider
loose-chain/cut semantics are also still in progress.

Plugins and recipes pin `audioLayout`, the canonical logical-stereo-slice to
physical-bus/channel signature. Presets, duplication, export and native storage
preserve it. A known incompatible vendor layout is rejected before publishing
routing. UI slice labels describe physical channel ranges without replacing
stable logical port IDs. Creating a rack plugin now pins the fingerprint before
history/save, avoiding save/reopen adding metadata absent from the original read.

Evidence: `/tmp/screamseq-group-boundary-build.log`; nine targeted suites passed
across `/tmp/screamseq-group-boundary-tests.log` (eight initial passes) and
`/tmp/screamseq-group-windows-tests.log` (the final Windows graph adapter pass).
They cover `signal-group-bypass`, `signal-group-runtime`, `graph-editing`,
`signal-graph-session`, `windows-graph-document`, `windows-native-metadata`,
`native-signal-graph`, `graph-signal-observation` and `mixer-runtime`.
The Windows group fixtures were updated to use the actual prepared-publication
hook because groups now prepare runtime boundaries; stop counts remain unchanged.
The earlier n-channel/Notes AppKit snapshot passed at
`/tmp/screamseq-layout-notes-interface.log`. The new group/copy-observation UI
suite is being rerun by the parent; no native visual result is claimed here.

### Song source membership and automatic Master placement

Song processing groups now accept existing `source:nID` modulation sources along
with rack effects. Group movement retains source coordinates; source removal
prunes group membership and envelope links. Mixed source/effect Delete sends one
`plugin.remove` transaction with optional `sources`, including dry-run and one
Undo. Source-only groups use `graph.song.source.remove`. Mixed-control export
currently rejects with a scope-preservation explanation instead of silently
omitting its controls.

A Master without a user-saved position now remains to the right of its actual
upstream audio path when Add creates another processor. Explicitly positioned
Master and processor cards remain unchanged.

Evidence: `graph-editing` and `signal-group-bypass` passed in
`/tmp/screamseq-source-group-tests.log`. The full copied AppKit suite passed in
`/tmp/screamseq-source-groups-interface.log`; manifest
`/tmp/screamseq-source-groups-interface-snapshot.json`. Native/Windows adapter
checks and actual UI rewalk are pending at this writing. This interface snapshot
predates the next broader recipe detach changes.

## Detached-chain and exact cable-cut checkpoint (source written; verification pending)

- Ordered multi-effect detach preserves internal main order, processor identities, control sources, auxiliary input/output branches, and the old chain's explicitly healed serial neighbors. It creates a document-owned detached chain; its silent scheduler root is never a user bus.
- Recipe detach supports a connected audio/control selection with an explicit ingress/egress healing pair. Unchosen boundary branches remain connected. Ambiguous default inference still rejects rather than multiplying cross-connections.
- Cutting an insert cable records only its target main-input disconnection. It leaves processor ownership, other cables and clocks intact. The final Master cable has a separate disconnection flag; raw Master observations remain available.
- API and native UI source includes exact `insert`/`master-output` batch cut identities, multi-effect `mixer.inserts.detach`, detached-chain insertion targets, and `mixer.bus.set.mainOutputConnected`. Context/command catalog exposes reconnect and terminal-cut actions. Codecs/Undo include the new state on both platforms.
- These statements describe the intended, written implementation. Shared model/API/interface regression runs and hosted PCM qualification must pass before this slice is reported complete. Runtime projection/masking is owned by the audio agent.

### Exact root routing: current scope and qualification boundary

The source implementation now has a dedicated `MixerPluginConnection` route;
it no longer substitutes a bus route for a plugin-to-plugin cable. The route is
stable source/output/target/input plus gain and enabled state, independent of
serial ownership. Its initial target scope is effect processors. The audio
workstream is compiling the segmented DAG/PDC implementation; final claims
require the resulting portable, native-host and live-history tests. See the
following source/UI checkpoint for evidence that has already passed.

Mandatory root-routing gaps still requiring a scheduler/API extension:

- Rack plugin output → a reusable-copy auxiliary input, and reusable-copy
  auxiliary output → rack plugin input. Existing `graph.routes.set` routes
  these copy boundaries through bus identities; the new rack connection must
  not pretend a copy is a rack instance. The UI retains an explicit explanation.
- Audio-input destinations on plugin instruments. They currently render in
  the source phase; accepting such a wire requires incorporating the generator
  into dependency scheduling, port activation and PDC. The new write rejects
  these targets without changing the document or transport.
- Audio-derived modulation into a plugin instrument when its follower depends
  on audio rendered later in the mixer. This requires the same generalized
  source/dependency scheduling; ordinary independent control sources are a
  separate already-supported case and should not be conflated with it.

These are musical routing capabilities, not clipboard/UI discoverability limits.
They remain in the full-graph completion assessment even when the bounded
rack-effect route checkpoint passes. Validation, note ownership, exact per-copy
telemetry and audition source identity must remain shared across the extension.

### Exact root plugin-to-plugin audio — source checkpoint

The new shared `MixerPluginConnection` stores stable source/output/target/input, gain and enabled state independently of serial ownership. Both adapters validate actual physical logical-slice catalogs and support atomic upsert/replace, persistence, Undo and exact mixed cable removal. The Mac graph projects the real sockets, supplies keyboard/socket equivalents, edits gain/enable/endpoints in place and retains the original main move-vs-Option-sum distinction. Instrument audio-input destinations explicitly reject until the source scheduler supports them; this is still a mandatory remaining graph capability. Runtime/compiler/PDC and exact observation integration belong to the audio workstream and are not qualified by this model/UI checkpoint.

The copied AppKit suite preceding this slice passed at `/tmp/screamseq-chain-interface.log` with `/tmp/screamseq-chain-interface-snapshot.json`. It includes detached chains, exact Insert/Master cuts, broad recipe detach, visible-name search, effective-value refresh, panel Back and exact-copy observation. Direct-route UI tests are running separately; do not infer their result from that pass.

Action scope retained intentionally: graph clipboard is the reusable-recipe clipboard; song rack duplication provides a fresh independent processor, while mixed song-control groups do not silently convert into library recipes. Root-group export also rejects cut or direct-plugin boundaries that its current contiguous-chain exporter cannot preserve. These cases provide explicit reasons and retain the original document. This is an export scope limit, not a claim that the source graph cannot contain those connections.

Direct-route UI qualification: the complete copied AppKit suite passed, exit 0, `/tmp/screamseq-direct-interface.log`; manifest `/tmp/screamseq-direct-interface-snapshot.json`. All live `mac/App/*.swift` and Swift interface-test hashes matched at completion. It exercises physical slice selection, gain/enable, both-endpoint replacement preserving prior identity/controls, Option-sum vs main chain ownership, keyboard parity, self-cycle rejection, semantic cut and unavailable exact telemetry. Native rendering and direct-route API/codec tests remain pending the segmented runtime checkpoint.
