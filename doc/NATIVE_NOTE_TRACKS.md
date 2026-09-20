# Native note tracks

Implementation slice for A02, using the already-qualified OpenMPT voice path and
native mixer. Legacy songs retain their existing channel layout and playback.

- Each note column keeps its existing stable native channel identity and pattern
  cells. A named note track groups adjacent columns beneath a shared mixer group
  bus. Shared inserts therefore process their actual summed audio. This is a
  native project feature; source-format effect semantics remain unchanged.
- The commands group existing adjacent columns, append a new track
  with empty columns, and ungroup without deleting notes or changing routing.
  Existing insert/send/sidechain identities stay intact. A group uses the common
  previous destination of its columns unless an explicit destination is chosen.
- Independent column mutes are native metadata overrides of imported channel
  settings. They must apply equally to live playback and export, survive saving
  and document Undo, and release only the selected column's plugin notes. Sample
  NNA voices belonging to that column need the same mute treatment.
- OpenMPT's existing format channel limits and the native mixer/adapter budget
  remain enforced before mutation. Structural layout changes stop transport;
  mute changes use bounded atomic controls. Previews and no-ops change neither
  transport nor history. A column keeps its raw API `channel` coordinate; track
  reads/document snapshots expose the mapping for agents.
- Native metadata version 5 holds the optional grouping/mute data. Files
  without these features continue to emit version 4 metadata. Older builds must
  reject newer metadata rather than silently discard it.
- The grid shows a shared track heading above its note-column headings.
  Existing editing, selection, clipboard and effect rendering continue to use
  the same virtualized cells. The native track controls and agents use the
  same validated commands.

Qualification requires independent summed-signal/mute checks, held/NNA sample
voices, plugin note ownership, rate/buffer partition checks, callback allocation
and lock auditing, graph capacity/cycle checks, history/persistence across the
five editable formats, API race/no-op checks and offscreen UI layout/input tests.
Live UI/audio-device tests stay deferred during quiet background development.

Track reordering/deletion, expanded per-note fields and sub-row timing remain
separate dependent work (B07/A03/A04). Grouping must not imply that those event
model changes already exist.


The grouped note-track scope is also available to `pattern.transform` and the
native Pattern Tools panel. It resolves stable group IDs into existing channel
ranges, retaining the same validation, masks, previews and one-step Undo.

Offline qualification now covers actual exported WAV data, shared VST3 note
ownership (including NNA and same-pitch notes), imported mute overrides,
three-rate sample identity and real-time allocation/free/lock auditing. Native
controls and the actual Metal grid can be rendered offscreen without desktop
control. Live presentation timing remains a separate gate.
