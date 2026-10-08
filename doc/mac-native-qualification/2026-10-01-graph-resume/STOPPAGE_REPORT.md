# ScreamSeq graph redesign — paused checkpoint

1 October 2026. Paused at the user's request. The approved four-slice graph
redesign is substantially implemented, but is **not complete or release-qualified**.
The strict 60 fps requirement remains unmet.

This report covers the graph-redesign work, including its shared audio/model and
API changes. Earlier tracker features are not a new audit of this checkpoint.
The checkout is based on `e48c645bb` on `codex/screamseq`. The resumed work is local
and uncommitted; no new commit or push was made during this resumed work.

## Implemented

| Area | What is now in the source |
|---|---|
| Navigation and layout | Song/group breadcrumbs; remembered views; focused channel paths with explicit hidden-branch links; stable placement; compact cards retaining connected ports and meters; labeled row, persistent and ordinary stages. Space remains playback and Tab remains node navigation. |
| Adding and patching | Contextual searchable Add; cable-first insertion; drag-to-add; quiet send/return creation; typed, named ports; multi-edge routing; sidechains; keyboard port search; compatible-node search; source/target navigation with Back. Precise numeric patching remains available. |
| Structure | Processing groups, reusable-library export, frames/comments and cable reroute points; detach/heal, delete/heal and cable cutting; routing-aware deletion and one chronological Undo transaction per edit. Reusable definition edits continue to affect all uses. |
| Modulation | Song-level sources can control ordinary rack parameters. Host parameter drops and exposed sockets create zero-depth connections; independent range handles; explicit follower conversion for audio-to-parameter connections; explicit quantization for stepped parameters. Manual base values stay separate from effective values. |
| Connections to patterns | Existing pattern envelopes, pattern commands and recorded automation can appear as reference sources. Their editing bridges address the exact parameter/cell and preserve a return path. A last-touched parameter command finds and exposes the stable plugin/parameter target. |
| Observation | Per-port levels and overload information, silence tracing, latency/freshness reporting, waveform/spectrum and temporary Listen. Root-route observation captures the actual post-gain/post-delay contribution, not an approximation from its source node. |
| Live audio | A qualified subset of rack add/remove/reorder/rewire continues playback with prepared transitions. Recipe controls, shared processor bypass and compatible opaque preset changes update all prepared copies. New prepared runtime support also permits supported reusable-source and modulation changes without recreating retained plugins. |
| Agent access and persistence | Shared model, guarded APIs, schema/docs, native persistence and unified history accompany musical changes. Portable Windows adapters/codecs track the shared semantics; Windows-native live publication and UI qualification remain separate work. |

The supported new live reusable-source subset is LFO, follower, random, MIDI
controller, Amount and pattern-envelope sources, their modulation connections,
and compatible follower taps. It preserves processor/boundary identities,
audible audio topology, assignments, roles, physical ports and compensation.
It does not make arbitrary graph topology changes live-safe.

Recent correctness fixes include stale parameter values after Undo, document Undo
swallowed by untouched numeric fields, lost cable releases during refresh, stale
edge indices, hidden newly added nodes, offscreen filtered views, incorrect
reference-wire endpoints and obsolete route telemetry. A delayed preset replacement
now warms its declared latency before fading; the reproducer's dry-path error fell
from 0.0270833 to zero. Live contribution histories use stable source identities
instead of vector positions.

## What was actually checked

The [journey ledger](JOURNEY_WALK.md) and [screenshot gallery](index.html) retain
the original failures and subsequent rechecks. A later test does not retroactively
qualify an older executable.

- All ten graph journeys were attempted in the native app. Rebuilt rechecks
  verified one-drag live sidechain activation, live instrument-recipe editing,
  live repatch/Undo/Redo, detach/heal, shared bypass, visible numeric Undo and
  exact automation-source navigation.
- The latest Ports UI walkthrough verified live song-follower creation and
  immediate document Undo from its untouched depth field, exact send scope and
  Listen, collapsed-card reopen, and last-touched target/return navigation.
  It ended after 37,279,744 frames with zero overruns at 48 kHz/512 frames.
  Builds ran concurrently; this was not a timing qualification.
- Earlier coherent full checkpoints passed all **89 CTest suites**, actual app
  socket tests and copied-source AppKit tests. The final integration results are
  recorded below.
- Six actual Core Audio loopback comparisons on the Polish checkpoint passed:
  dry, AU effect, VST3 effect/automation, AU instrument, VST3 instrument/automation,
  and fractional graph commands/copies. Captured versus offline PCM had zero
  maximum/RMS error and zero overruns. These results precede the newest runtime.
- Focused AU/VST3 fixtures cover callback partitions, independent processor copies,
  latency, fades, sidechains, queue/storage limits, stale/coalesced plans and host
  realtime allocation/free/lock auditing. Latest focused and sanitizer results
  are in [the audio checkpoint](LIVE_AUDIO_CHECKPOINT.md).
- Installed Battery 4 AU and VST3 passed scanner and hidden load/state/remove
  checks. This is not a commercial-plugin musical-playback or transition guarantee.

### Final integration boundary

The integrated C++/portable regression run passed **89/89** in 80.15 seconds
([log](logs/stoppage-ctest.log)); the final copied AppKit suite passed
([log](logs/stoppage-interface.log)). The latest audio source-runtime checks passed
**7/7 normal tests** and **5/5 ASan/UBSan tests** (leak detection disabled).
The rebuilt application passed the **actual app socket suite**
([log](logs/stoppage-socket.log)) and deep/strict code-signature verification.
All **856 source inputs** matched before and after compilation. Source fingerprint:
`97711e130ec79949ecce22b2b8bc65b30a0f45b14211a82dd84a10469f73d7a9`.
The full manifest and executable hash are in [stoppage-build.json](stoppage-build.json).
The rebuilt app is `bin/mac-graph-fluency/ScreamSeq.app`; it has not received the
remaining native UI walks or a new strict display-performance pass.

The initial build hit a Clang test-fixture compiler crash. Its incremental retry
compiled the application successfully, then packaging failed because the disk
was full. Only 1,783 regenerable object files from this task's older snapshots
and sanitizer build were removed (about 704 MiB of logical data); all source,
executables, archives, songs and evidence were retained. The unchanged remaining
packaging steps then succeeded. At handoff, disk space was about 1.5 GiB.
The failure logs and [cleanup inventory](stoppage-object-cleanup.json) are retained;
this was not an uninterrupted successful invocation of `mac/build.sh`.

### Measured journey paths

These are actions / panel or graph-depth switches / redundant confirms. Return
ending numeric entry is part of editing, not a separate Apply. Most new graph
journeys did not have a comparable successful numerical baseline; none is invented.
The older, broader UI before/after audit is in
[the September journey report](../../SCREAMSEQ_INTERACTION_JOURNEYS_2026-09-24.md).

| Journey | Earlier boundary | Latest observed successful path |
|---|---|---|
| Add reverb to selected channel 3 | Full add/tweak/bypass baseline is not comparable | 3 / 0 / 0 |
| Insert EQ on a selected path | Unmeasured | 4 / 0 / 0, playback active |
| Connect a detector sidechain | Live first-use activation rejected | 1 / 0 / 0, playback active |
| LFO → parameter and set depth | Earlier count omitted setup | 9 / 0 / 0 including setup; 3 after source/parameter setup |
| Quiet send and return | Unmeasured | 5 / 0 / 0 |
| Diagnose and repair mute | Unmeasured | 3 / 0 / 0 |
| Find and repair overload | Unmeasured | 10 / 0 / 0; subsequent presentation fixes need rewalk |
| Group, name and export a chain | Unmeasured | 6 / 1 / 0 |
| Instrument graph edit and return | Live edit rejected after root-source creation | 9 / 2 / 0, playback active |
| Repatch with history | Uninterrupted path was blocked | 3 / 0 / 0 for move, Undo, Redo; bypass was verified separately |

### Controls relocated or simplified

| Previous friction | Current home/path |
|---|---|
| Persistent From/To/Connect form | Socket dragging and searchable port context actions; exact form remains under More/⌘K → Advanced numeric patching |
| Separate graph Apply steps | Values and cable gains commit on change; unified Undo restores them |
| Repeated node selection/Open/Remove controls | Direct canvas selection, double-click/Return, Delete and contextual menu/catalog |
| Equal-weight rows of secondary actions | More, object context menus and ⌘K; stable command IDs retain keyboard access |
| Remember parameter identity while moving panels | Parameter → automation/source/activity bridges and last-touched lookup, with return navigation |
| Filtering that silently drops dependencies | Labeled hidden-branch links and one-action reveal |

## What remains

1. **Pass the unchanged 60 fps gate.** Latest clean 60-second docked and floating
   runs still fail: 30/3,462 and 17/3,473 missed presentations respectively.
   Maximum gaps were 41.666 ms and 33.333 ms; snapshot delays also exceeded the
   limit. Both had 16 sounding channels, nine readable cards and zero audio
   overruns. Caching reduced graph activity-update medians from about 6–7 ms to
   0.3–0.5 ms, but did not resolve presentation stalls. A passive trace isolated
   an uninstrumented main-thread interval; the next profiler experiment was
   cancelled for this pause. See [performance findings](FRAME_PERFORMANCE_FINDINGS.md).
2. **Finish note/MIDI routing semantics and the held-note ledger.** Event sockets
   do not yet constitute a general editable note-routing engine. Two questions
   remain open: whether a new destination receives only plugin-instrument notes
   or layers all channel notes, and whether it starts currently held notes or
   waits for the next note. Route removal must send the required note-offs.
3. **Extend live structural editing carefully.** Arbitrary recipe processor/audio
   topology changes, new instrument adapters/assignments, newly watched note-envelope
   sources, incompatible latency transitions and inactive VST3 bus activation
   remain outside the accepted live subset. Rejection preserves the current
   audible graph. Arbitrary N-channel audio layouts also remain unsupported.
4. **Complete deeper observation and remaining graph actions.** Exact scope/control/
   event activity inside each reusable processor copy; group bypass/source mute;
   remaining preset, duplicate, clipboard and make-independent entry points;
   broader detach/cut branch handling and complete context/catalog coverage.
5. **Rewalk the newest UI on the integrated executable.** Human socket labels,
   cached invalid-drag feedback, Main-output gain cleanup, last-touched filter/
   inspector fix, keyboard sidechain commit, reference-socket retention after
   exposure changes, and live reusable-source creation/Undo need fresh native
   checks. Automated coverage is not a substitute for those walks.
6. **Complete final platform/robustness qualification.** New-runtime loopback,
   commercial-plugin transitions, preparation-failure model/history recovery,
   and Windows-native execution/publication remain open. Recorded automation
   point edits currently require stopped playback. Recheck every final journey
   and publish one consolidated release result after these gaps are closed.

## Handoff

The final native Ports session was saved, stopped and quit normally. The next
performance capture was cancelled before launch. No musician process, bundle,
project or system audio default was replaced. Builds remain under
`bin/mac-graph-fluency`; uniquely identified QA bundles and private fixtures remain
under its `qualification/` directory for reproducible follow-up.

Resume with the integration results above and the linked evidence, not an older
dated completion claim. Keep the timing thresholds unchanged. The first useful
next step is the remaining native rewalk of the final integrated build, followed
by the targeted frame-stall investigation.
