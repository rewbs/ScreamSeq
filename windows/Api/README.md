# Windows local API subset

`track.get {}` returns the shared note-track projection: stable column identities,
effective per-column mute, visual group membership, note-column indexes, group
bus names/colors/outputs, available destinations and the module's column limit.
The same projection is cached as `document.get.data.trackLayout`. Existing
`document.get.data.tracks` retains its original raw-column metadata contract.

`track.group {channels, name?, output?}`, `track.create {columns, name?, output?}`,
`track.ungroup {track}` and `track.column.set {column, mute}` require
`expectedRevision` and accept `dryRun`. Their parameter/result definitions are
shared with [the Mac API guide](../../mac/AUTOMATION.md). Group existing adjacent
columns or append new ones within the actual module limit; an explicit destination
is required when selected columns have different main outputs. Grouping retains
inserts, sends and unrelated routing. Ungroup removes visual membership while
retaining the group bus and all its processing/routes. Use `mixer.bus.set` for
group name/color and `song.annotate` for raw-column metadata.

Changed writes create one chronological Undo step and persist in the existing
native project format. DryRun and successful no-ops preserve revision/history.
Structural group/create edits use the existing safe transport-stop path. Pure
column-mute edits and their Undo/Redo prepare a whole-column frame for live
publication; a saturated queue or changed playback owner rejects before commit
with `-32002`, preserving the document, transport and history. Reads expose the
committed model; renderer adoption occurs at its next render boundary. These
methods do not enable full API parity. This P2 source integration still requires
the recorded native/pipe/audio and cross-platform qualification gates.

The native grid shows saved column names and colored group spans above its note
and effect headers. Click a span to select that group's columns at the current
row; click a column header to select that column. The native **Mute N / Unmute N**
button changes only the current note column. Mute and Ungroup are also registered
commands in the palette and pattern context menu, with configurable shortcuts.
Their uncertain results use the existing retained command Review flow, without
repeating a write. `workspace.get.data.trackHeaders` reports visible columns and
group rectangles in DIPs; `geometry.pattern.headerHeight` locates the first row
without assuming a fixed header size. Native create/group draft forms remain
pending in this checkpoint; the APIs support those edits now.

Parameter activity uses the shared prepared processor monitor: `parameter.activity.targets`,
`.parameters`, `.sources` and `.get` read actual host values and controlling sources.
`parameter.activity.watch` is a transient replay-cached write without a song revision
guard or musical Undo. Processor keys include the document identity; a replaced song
cannot reuse a previous song's monitor. Read points after a cursor, resetting it when
the token changes. No processor values are fabricated before playback preparation.

`automation.recorded.get` pages absolute 48 kHz automation by stable plugin identity
and parameter. `automation.recorded.edit` adds, moves, updates or removes one point
with `expectedRevision`, complete prevalidation, `dryRun`, one chronological Undo and
native project persistence. Enabled parameter envelopes and pattern commands exclude
new recorded points. See [parameter-activity.schema.json](parameter-activity.schema.json).

`workspace.input` changes the selected typing instrument and/or octave using both
`expectedRevision` and `expectedContext`; it changes context only, with no musical
Undo or transport stop. Other workspace operations keep their existing unguarded
presentation-only contracts.

This directory provides a private transport and a control-thread session adapter,
not full macOS API parity. The attached host determines document-operation support;
query the running instance's `api.describe` for its current method catalog. Do not
infer support from the standalone protocol fixture or the Mac schema. Navigation
and inspectors share GUI/API paths (see **Workspace subset** below).

`document.open` requires a current `expectedRevision`. Its `discard:true` applies
to unsaved song changes; it does not authorize discarding retained native editor
drafts. Those drafts produce `-32002` with `data.writeOutcome:"notCommitted"`
before replacement. Resolve them in their editor, or use the native Open review
prompt to review, discard the exact captured drafts, or keep editing. Recovery
Restore uses the same final admission check. A changed draft invalidates an
earlier native discard decision; pending or uncertain work must be resolved.

During document adoption and native refresh, mutating API and workspace actions
are refused with `-32002` / `notCommitted`. If refresh fails after adoption,
`document.get` reads the adopted song and `context.get` reports
`nativeRefreshPending:true`; remaining native context is the last displayed
context and must not be used as a new editing target. Input stays protected.
Press F5 in the app to retry native cleanup/refresh without reopening the file
or replaying the write. Stop remains available and preserves that recovery
message. A successful retry restores normal editing.

The native sample recorder retains an unresolved Record/Stop/Discard operation
in `workspace.get.data.sampleRecording.lifecycleReview`, including its method,
captured document/revision, take identity and submitted parameters. While present,
another lifecycle write, Keep, setup discard and Close are unavailable. The
native **Review current take** action reads `sample.recording.get` without
repeating the operation. Failed or malformed readback retains the review state.
Successful review adopts the observed current take (including a different take
created through the API, or no take), preserving the sample-name/output draft.
It does not claim whether the earlier operation succeeded. Subsequent explicit
actions use the observed take identity. An uncertain Keep still requires its
separate result review; absence of the old take does not prove sample import.

The native reconnect window similarly retains a failed explicit path scan or
installed-plugin rescan in `workspace.get.data.pluginPath.scanReview`. Review
result uses `synchronizeView` and the captured stable target's `plugin.path.get`
or `graph.plugin.path.get`; it never sends another reconnect or scan. A reconnect
with no retained result uses the same observation path. Successful readback
records `report.outcome:"unverified"`, the observed location/candidates and the
original submission. This is current-state evidence, not proof that the earlier
request committed or created Undo history. Raw path fields and their baseline
remain unchanged; `readbackNeedsReload:true` requires explicit Reload before
another Verify, reconnect or scan. Wrong-document, changed-revision, wrong-target,
malformed or failed reads retain the unresolved operation and departure guard.
Known reconnect receipts retain their existing exact-result review behavior.

`document.get` includes stable current-sequence order identities in
`orderMetadata: [{id: "n…", name, annotation, color}]`, aligned with every untrimmed entry in `orders`,
including End (`65535`), Skip (`65534`) and entries after End. Each existing
`sequences` entry also has a stable `id`, annotation and color; a nonempty native
name takes precedence over the engine sequence name. Patterns and tracks expose
the same metadata fields, with `index` identifying the engine slot. Use these identities to retain an
occurrence when repeated patterns are moved; numeric order indexes remain the
arguments to revision-guarded `order.edit` and `transport.play`.
`formatLimits` reports `patternRowsMin`, `patternRowsMax`, `patternsMax`,
`ordersMax`, `patternsRemaining` and `ordersRemaining` from the actual module
specifications. Pattern capacity includes reusable holes; order capacity counts
the complete untrimmed sequence used by create/insert validation.

The native Arrange and Tempo and groove windows use existing `pattern.create`,
`order.edit`, `sequence.select` and `document.timing.get` / `.set` operations.
Their read-only workspace snapshots are `arrangementWindow` and
`songTimingWindow`; `arrangementSelection` contains the selected occurrence
`id`, current `order` index and `sequenceID`, independently of cursor/playhead.
Timing Preview uses `dryRun:true`. Creation/timing drafts retain their original
revision and require explicit Reload after another edit or sequence change.
Changing timing creates one document Undo and stops playback; preview and no-op
leave playback and history intact. Existing pattern timing overrides remain in
force. Pattern duplication preserves exact source pattern timing overrides and
engine name/color alongside native musical metadata; changing its requested row
count truncates or extends cells while retaining those properties.

`arrangement.get {}` returns `{sequence, sequenceID, orders, sections}`. Every
untrimmed order has `{id, name, annotation, color, order, pattern}`, plus
`patternID` when it references a valid pattern. Each nonempty order name starts
a section: `{id, name, firstOrder, lastOrder, color}`. Its range ends before the
next named order or at the sequence's final slot. An unnamed prefix has no
implicit section; whitespace names count as nonempty, including on End/Skip.

`song.annotate` requires `expectedRevision`, a canonical `n…` entity `id`, and
one or more of `name`, `annotation`, `color`. It accepts patterns, tracks,
sequences and order occurrences, including orders in inactive sequences.
Omitted fields are retained. Names allow 256 UTF-16 code units, annotations
4096, and colors integer `0..16777215`. It returns the full entity metadata;
unknown fields and `dryRun` reject. An empty order name removes its section.
Changes create one document Undo and persist in the existing native format.
No-ops preserve revision and Redo; annotation edits and their Undo/Redo preserve
playback. Musical and structural history retain their existing stop behavior.

Arrange now has Orders, Section and Pattern details pages. Section edits the
captured order occurrence's name; Pattern details edits the captured pattern's
name and notes. Each has independent retained drafts, captured identity and
revision, explicit Reload, and stale-edit rejection. Selection changes do not
retarget unfinished text. `arrangementWindow.sectionDraft` and `.patternDraft`
report these contexts. Previous/Next section use strict earlier/later named
orders without wrapping or relocating playback; selecting End/Skip leaves the
pattern cursor unchanged. These actions and both editors appear in the
configurable command catalog. Color editing is available through the API;
there is no native color picker in this checkpoint.

`arrangement.matrix` and `arrangement.copyBlock` use the shared Mac request
schema and copy semantics in [AUTOMATION.md](../../mac/AUTOMATION.md). The read
returns full order/track entities and bounded block summaries, including precise
notes and native FX. Pages default to 64 orders and up to 16 tracks; maximums are
128 and 32. Counts/bins are unsigned 32-bit, and repeated occurrences retain
independent order identities. End/Skip slots have no blocks. The copy requires
`expectedRevision`; `dryRun` validates the complete prospective song and view
cache before any playback stop, ID allocation in the live song, or history edit.
`wouldChange` includes native-only changes even when `changedCells` is zero.
Changed Apply creates one document Undo; no-op preserves revision, playback and
Redo. Native project persistence retains exact cloned pattern timing and metadata.
Responses are charged as escaped JSON before appending each bounded page item,
leaving room for the transport envelope under 32 MiB. Query `api.describe` for
the live `arrangementMatrix` contract and available native interaction paths.

Application recovery uses the Mac-compatible `recovery.status`, `recovery.list`,
`recovery.save` and `recovery.restore` contracts, also described in
[`recovery.schema.json`](recovery.schema.json). `context.get.data.autosave` has
the same status: enabled, ten-second interval, ten retained generations per
session, last successful timestamp/copy, persistent error and saving flag.
Save and restore require `expectedRevision`; successful request-ID replay is
idempotent. Save captures immutable native bytes and writes them on a serial disk
worker, leaving revision, dirty state, original path, Undo and playback unchanged.
Its response revision identifies the captured song even if editing continues
while those bytes are written. Unchanged automatic snapshots are deduplicated,
including opaque manual plugin editor state; prepared playback automation is
never written into the saved baseline.

Restore accepts only a listed opaque ID. It durably protects the current unsaved
song under a separate session identity, then validates and opens the selected
copy as a new unsaved document with no file destination. Ordinary Save clears
only this session's copies, ordered after its pending writes. Storage is
`%LOCALAPPDATA%/org.resonance.tracker/Recovery`. Incomplete writes do not appear;
damaged metadata falls back to a generic title without hiding the song bytes.
The modeless recovery browser opens from the footer or File / Recover a song in
the command palette. Existing copies are offered on normal interactive startup.

Inspection/audio qualification never accesses that real recovery store. Explicit
`--recovery-test-directory ABSOLUTE_PATH` with `--automation` and inspection or
audio-test mode enables private fixture storage. The optional
`--recovery-test-write-delay-ms 0..2000` is restricted to inspection with that
private directory, to exercise concurrent editing and ordered Save cleanup.
Windows preserves unfinished recording takes and their compatibility metadata;
live MIDI input, take review and Finish/Discard are described in
[`../RECORDING_PROGRESS.md`](../RECORDING_PROGRESS.md).
Unapplied graph recipe editor drafts retain their explicit Apply semantics.

Windows output selection is available through `audio.devices.get`,
`audio.settings.get` and `audio.settings.set`; see
[`audio-settings.schema.json`](audio-settings.schema.json). Set requires
`expectedAudioRevision`, `endpoint` (opaque active output ID, or empty for System
default) and `periodFrames` (0/64/128/256/512). Optional `dryRun` validates without
applying. These session preferences have independent `audio:` revisions and
successful request-ID replay, with no document Undo or project mutation.
Replies use `changed:false` and report `playbackStopped` truthfully. A successful
changed Apply stops output; a failed validation or no-op preserves it. Inspection
does not open hardware. `checked` reports the latest validation probe; active
device fields report actual negotiation. See `../AUDIO_SETTINGS_PROGRESS.md`.

The application supports revision-guarded `transport.note` and `transport.panic`
using the shared renderer's preview queue. See `mac/AUTOMATION.md` for note,
sample/instrument, velocity and release semantics. Optional zero-based `channel`
selects pattern-channel routing for sample-backed previews; omit it for an
independent inspector preview and repeat the same context on release. Stopped audition prepares a
paused renderer; `transport.get` distinguishes `playing`, `audioActive` and
`audition`. Native piano input has independent held-key ownership and guards
against late releases from an earlier playback preparation. Windows responses
also expose a transient `epoch`; clients should not persist it. Previewing never
changes document history or native project data.
`workspace.get.audition` reports the retained target, revision, keyboard geometry
and held inputs. `sampleDetail.playbackFrames` reports bounded sample-voice
positions. `transport.get.presentation` contains `frames` (capped at 120000)
and `idleWaits`; these are presentation
diagnostics, not a sustained frame-rate qualification. See
`../AUDITION_PROGRESS.md` for evidence and limitations.

Windows `transport.get.faultDetails` is `null` without a prepared renderer,
otherwise `{renderer, chain, plugins}`. A failed rack plugin can report
`{slot, instanceID, reason, detail}`; `reason` is a static provider code such as
`vst3.restart-flags`, and `detail` is its restart bit mask, parameter ID, bus index
or SDK result as applicable. The first provider failure is retained. This is
transient diagnostic state, outside document history/persistence. An empty
`plugins` list does not rule out a graph/renderer/chain failure; it describes
only available rack-provider evidence. The audio-test host report includes the
same `audio.faultDetails`. No vendor calls or serialization occur on rendering.

The application now implements Mac-compatible `automation.get` and
`automation.replaceLane` for absolute song parameter points, plus stable-ID
`automation.recorded.get` and `automation.recorded.edit` point operations.
The latter take `plugin` and `parameter`; edit takes `frame` with `value`, an
optional `newFrame`, or `remove:true`, and supports `dryRun`. Timestamps are
48 kHz frames, values are native parameter units, and replacement uses plugin
Undo. During playback, point edits and their Undo/Redo prepare a bounded timeline
snapshot and publish it at the next render boundary. A failed preparation or full
publication queue preserves both playback and document history. The native Song automation window opens from the command palette or the
pattern-curve editor. `workspace.get.absoluteAutomation` reports captured target,
revision, draft, viewport and bounded draw geometry; its point preview truncates
at 4096 while `pointCount` reports the complete lane. Use paginated
`automation.get` to read every point. See `../ABSOLUTE_AUTOMATION_PROGRESS.md`.

`graph.note.connect/update/disconnect/restoreAssignment` edits plugin-instrument
note cables with stable source and destination identities. The write result's
`route` is the cable ID. An instrument connect can suppress its implicit
assignment in the same transaction; disconnecting an implicit assignment keeps
it suppressed until restored. `graph.get.noteRouting` persists the cables and
suppressed assignment IDs. Each plugin also exposes
`assignments:[{instrument,instrumentID,channel}]` alongside its instrument indexes.
New destinations receive the next note-on; removing a destination releases notes
owned by its cables. These APIs use expectedRevision, dryRun, Undo and native
project persistence, and reject unavailable or non-instrument destinations.

`workspace.get.sampleDetail` exposes the detailed sample editor's captured
identity/revision, draft points, selection, viewport, channels, bounded waveform
cache and latest operation report. Its native controls call the existing
`sample.*` methods with the same guarded document history and PCM semantics.
The dock's Detail button and command palette open this window. See
`../SAMPLE_DETAIL_PROGRESS.md`.

The retained sample pages add `page`, `loops`, `pastePreview` and `autoSnap` to
that read-only snapshot. `loops` includes independent raw normal/sustain frame
strings, enabled/direction flags, `dirty` and `stale`; invalid input remains
observable without becoming song data. Joint Preview/Apply uses
`sample.loops.set`. Paste Preview retains its exact options, target revision and
clipboard ID; Apply uses that reviewed clipboard or rejects a mismatch.
`pastePreview` describes a locally retained preview, not a guarantee that an
external clipboard or document change has not occurred. Snap selection and
either loop use `sample.snap.get`; only explicit Apply changes saved loops.
See `../SAMPLE_WORKFLOWS_PROGRESS.md` for scope and qualification.

The actual application now also registers `graph.*`, `mixer.*` and
`envelope.bank.*` / `envelope.catalogue.*` operations. Their fields match the
existing Mac schema. Graph recipe copies use the real rack's saved baseline;
envelope parameter targets resolve real plugin parameters and automation
conflicts. Mixer control-only writes and previews use the prepared audio queue;
queue refusal returns `-32002` without changing history or the saved mixer.
Routing changes validate before stopping playback. `workspace.get.mixerEditor`
reports the native dock's captured bus/revision and draft state.

`graph.plugin.get`, `graph.plugin.set` and `graph.plugin.editor.open/commit/close`
now follow the Mac recipe interface. These are independent effect instances;
changes belong to document history and never overwrite the rack baseline.
Parameter/port writes validate the whole candidate before publication. Compatible
recipe controls and topology prepare live updates without stopping playback.
Preparation failure (including a full graph-control queue) returns `-32002`
with its reason and preserves the accepted document, history and playback.
Editor open returns a token; commit requires that token, captured graph/node
and unchanged recipe. Closing the native window retains its uncommitted draft
until explicit API close or document replacement. Commit supports dry run.
`graph.plugin.bypass {graph,node,bypass,expectedRevision,dryRun?}` changes the
saved host-bypass flag for one recipe processor in all uses. Getter metadata
includes `bypass`; parameters and opaque preset data are preserved, as are
clone/group-export copies. A changed flag is one document Undo; identical values
and dry runs are no-ops. This action and its Undo/Redo publish to active prepared
copies. Busy queues or stale playback generations return `-32002` without stopping
transport or changing history; unsupported preparations also preserve playback.
`workspace.get.graphEditor` exposes the reusable canvas's captured revision,
draft flags, selection and retained hit-test geometry. The contextual workspace
panel API is unchanged.

The reusable Windows graph editor starts new modulation cables at zero depth.
Their base comes from an enabled contribution to the same target, or from the
target catalogue's normalized manual value. New discrete targets retain an
explicit quantized mode. In the new-wire form, an empty Base field means this
automatic baseline; entering a number remains an explicit normalized base edit.
Updating a selected cable preserves its saved quantized/enabled flags and all
fields not displayed by the form. These remain local drafts until Apply, which
uses the existing guarded `graph.update` transaction and document history.
Catalogue reads capture endpoint identities and reject a changed draft/revision
before adopting new wire defaults.

`workspace.get.songRouting.canvas.nodes` includes `stageRole` (`none`, `row`,
`persistent`, `ordinary`, `instrument`), `canEditInserts` and `canAssignGraph`.
Row/Persistent cards inspect their own shared recipe; activation remains in
pattern commands. They cannot change the bus's Ordinary assignment or regular
insert chain, including through keyboard or directly dispatched control actions.
Regular inserts are edited from the bus/effect owner. These presentation roles
do not rename persisted graph/layout identities or identify an aggregate audio
stage as an individual prepared processor copy.

The retained Graph Curve editor uses `graph.automation.get/set` and
`automation.formula.preview`. Graph's Pattern curve action opens that editor
while routing remains on its previous page. `workspace.get.graphCurve` reports
its captured graph/source/stable pattern identity/revision, retained point fields,
selection, viewport and preview status. Values use the shared 256 units per row and normalized 0..1
model; native fields display rows and percent. All nine curve types and scripted
expressions use the shared evaluator. Formula previews run on the worker and
painting consumes cached samples. Reload remains bound to the captured target;
Tools → Load selection deliberately replaces local edits only after a successful
guarded read. Automatic following never discards retained curve or child drafts.
The same native owner retains its Bank, Formula and Guide windows when moved or
hidden. Song overview remains outstanding. See
[Graph Curve implementation status](../GRAPH_CURVE_HOST_PROGRESS.md) for qualification.

The native **Expand** action opens a retained multiline formula draft with local
completion/Undo and searchable reference. `workspace.get.formulaWorkbench` and
`formulaReference` expose source, selection target, pending/valid status,
completion/reference matches and preview samples. The envelope bank exposes
the same snapshots inside `envelopeBank`. Use changes only the captured local
point; the parent Apply/Save performs the existing guarded musical transaction.
There is no separate formula mutation API or history domain. A stale point or
newer parent draft retains the text and rejects Use.

`automation.formula.preview` accepts rows and rowsPerBeat through 65536 and a
span through rows×256, matching reusable bank templates. The shared envelope
validator checks geometry while the existing parser/evaluator validates and
previews expressions. Ordinary pattern-edit limits remain unchanged. Equivalent
Mac validation and the shared API schema use the same bounds.

The curve's **Bank…** button opens a modeless native envelope bank. It uses the
same guarded bank/catalogue operations for captured-curve templates, linked or
independent use, master edits, unlinking, removal, catalogue publication and
explicit replacement/import. `workspace.get.envelopeBank` exposes the window's
captured target/document/revision, catalogue revision, scope, selection, draft,
preview and parent-source guard. Closing the window retains an unsaved draft;
reopening raises it. Reload/discard is explicit. Catalogue copies remain outside
document Undo; importing and editing song templates use document history.
Source drafts are rechecked on completion, and unlinking retains pending source
points. See `../ENVELOPE_BANK_PROGRESS.md` for coverage and remaining native
parameter/instrument entry points.

The normal Windows envelope catalogue lives at
`%LOCALAPPDATA%/org.resonance.tracker/envelope-catalogue-v1.json`. Reading an absent
catalogue creates no file. Inspection/audio tests disable the normal catalogue;
inspection can opt into an absolute private file with
`--envelope-test-catalogue <path>`. Publication uses its own
`expectedCatalogueRevision` and does not create document Undo history.

The app must
explicitly opt in and publish its **exact pipe path containing its PID** to the
user; there is no global endpoint discovery and the client never guesses.

## Parent application integration (exact interfaces)

Compile `windows/Api/PipeServer.cpp`, include `windows/Api/` and
`include/nlohmann-json/include/`, link `advapi32`, use C++17 and `NOMINMAX`.
The parent application CMake compiles PipeServer.cpp and links advapi32.
`windows/App/ApiDispatch.hpp` implements the bounded control-thread handoff;
Main.cpp supplies the application's SessionHost and delegates document work to
its controller. The standalone API tests below qualify the adapter/transport,
not the application's full musical editing, persistence or DSP behavior.

```cpp
#include "PipeServer.hpp"
#include "SessionAdapter.hpp"
using namespace ScreamSeq::Api;

// Implement these on the Application's window/control thread:
struct ApplicationApiHost final : SessionHost {
  SessionSnapshot snapshot() override;
  PatternSnapshot pattern(unsigned index) override;
  void play(const Json &settings) override;
  void stop() override;
};
// Construct host and adapter ON that same owning control thread.
ApplicationApiHost host;
SessionAdapter adapter(host);
// PipeServer(name, handler, optionalIoTimeoutMs=5000).
// name example: L"\\\\.\\pipe\\ScreamSeq.Api." + std::to_wstring(GetCurrentProcessId())
// Handler type: std::function<nlohmann::json(const nlohmann::json &)>.
```

The pipe handler runs on a single background thread. **Do not** directly call
`adapter.handle(request)` there: the adapter returns `-32002` without touching
its host when called off its construction thread. Instead the parent handler
must put an owned JSON request into a bounded queue, `PostMessage(WM_APP+...)`,
and synchronously wait with a finite deadline for the owner's result. The
window procedure drains requests and invokes `adapter.handle(request)` there.
The returned JSON is already a complete JSON-RPC envelope; pass it unchanged
back to PipeServer. Never wait on the audio callback or access Document/GUI
concurrently. The adapter itself deliberately contains no HWND or app-field
assumptions.

A timed-out queued request must be marked cancelled and skipped if it has not
begun. If it has begun, its outcome is uncertain: do not report a safe-to-retry
busy result and then execute it later. Keep queue item storage alive until both
threads release it. For shutdown on the window thread, disable/cancel the queue
and wake all waiting handlers **before** calling `server.stop()`. Only then
destroy the host/adapter/window. A supplied handler must return in bounded time;
the transport can cancel its own I/O, not arbitrary user callback code.
`start()` throws for invalid local paths, security setup errors and collisions;
`stop()` is idempotent, and restart on the same server object is supported.

### Host snapshot contract

`SessionSnapshot` is an owned value containing:

- `std::string revision, documentId`: real session tokens, not the PID alone.
  Keep the revision stable for these read/transport methods; create fresh
  document identity/revision on a new document. Construct a new adapter on a
  new session to retire its request-ID cache.
- `Json document`: the `document.get.data` dictionary, passed through unchanged.
  Supply the same fields as `TrackerSessionAPI.inc` / `snapshot:` where available.
  For region validation it must contain `orders: [patternIndex,...]` and
  `patterns: [{index,rows,...},...]`. Do not falsely claim invented inventories,
  format limits, IDs, history, or loaded plugins. Full metadata parity remains
  a host integration task.
- `Json context`: `context.get.data`, passed through unchanged. The host owns
  cursor, selection, following and a separate contextRevision; `navigate` receives
  validated coordinates from `context.set` on the same control thread.
- `Json transport`: `transport.get.data`, passed through unchanged. Must have
  boolean `playing`; supply actual loop, region, order/pattern/row and renderer
  telemetry available in the app, not fake device metrics.

`pattern(index)` returns `PatternSnapshot { unsigned rows, channels;
vector<array<uint8_t,6>> cells; }`. All cells are row-major, in the order
`note, instrument, volumeCommand, volume, effect, parameter`. Read them from the
real document on its control thread. Invalid indices throw
`ApiError(-32602, "Pattern does not exist")`. The adapter supplies the normal
rectangle response, default row/channel pagination and 4096-cell bound.

`play(settings)` receives the validated original parameters **without**
expectedRevision: optional order, pattern, startRow, endRow, cursorRow, loop.
The defaults match the Mac contract: order/cursorRow 0, loop retains the current
loop setting, row bounds require a pattern and are half-open. The adapter
checks playable order, pattern existence, ranges, cursor and strict types
before entering the host. The host must honor every supplied setting or throw
`ApiError(-32003, "... not supported by this playback host")` **before starting
playback**; do not silently call plain Application::play() and ignore requested
regions/loops. Return only after actual acceptance, not just after posting an
unconfirmed play command. Propagate engine failure; do not report success.
`stop()` should stop playback/capture through Application::stop(). Neither
operation changes the document or its Undo history. Host callbacks must not
partially mutate on validation failure.

Hosts advertising `supportsPlaybackLoop()` also expose `transport.loop` with
`expectedRevision` and a required boolean `enabled`. ScreamSeq's Windows app
implements it through the shared renderer's atomic loop setter. It updates a
running song/pattern region without restarting WASAPI, replacing the region,
moving the edit cursor, changing history, or stopping/discarding a recording take.
While stopped, it sets the default for the next Play; an explicit `transport.play`
`loop` field overrides that default. `transport.get.loop` is the current setting;
`region` retains the parameters of the request that started playback. Busy or
document-replacement admission refuses before changing the loop setting.
Hosts without this capability do not advertise it and return unknown-method.

The Windows command palette and pattern context menu expose the loop toggle,
along with a native `Playback loop: on/off` button below the typing selectors.
The palette permits assigning a shortcut using the existing command ID scheme.
Native editor focus and raw drafts survive shortcut activation. This parity
slice adds no project field or saved preference format. Its new actual-app,
native-control and shared renderer checks require qualification before a runtime
parity claim; authored checks alone are not evidence of device behavior.

Native Play starts at row zero of the selected occurrence of the edited pattern,
falling back to its first occurrence. **Shift+Space** plays from the edit cursor;
**Ctrl+Space** plays the selected rows, or the whole pattern when unselected;
**Ctrl+Shift+Space** uses the cursor within that bounded range (its start when
the cursor is outside). Bounds include the selected last row, represented as an
exclusive `endRow` in `transport.get.region`. All these commands appear in the
palette and pattern context menu. They retain the edit cursor, selection, viewport
and Follow setting; native text/list controls and formula completion keep local
ownership of these keys.

Repeated orders use stable occurrence identity through preparation. A removed or
reassigned occurrence refuses before device work, while a moved occurrence is
resolved at its new index. Bounded Play also accepts a pattern absent from the
arrangement; unbounded cursor Play reports that condition instead of playing a
different pattern. Ordinary Play then uses the selected playable occurrence (or
first playable order). API `transport.play` retains its explicit parameters and
default order-zero behavior. No project migration accompanies these UI commands.

Existing customized shortcut profiles retain conflicting explicit bindings when
these defaults are introduced. Only the corresponding new command starts
unbound; other new defaults remain active. This is reflected as an empty override
in the palette/API, written only on the next explicit preference save. Clear the
old binding and Reset the new command to enable its default. Explicit conflicts
within a profile still reject the complete load.

## Workspace subset

`context.set` uses the existing shared schema fields `expectedRevision`,
`expectedContext`, and at least one of `pattern`, `row`, `channel`, `column`,
`following`. Read both tokens from one `context.get`. Booleans are not integers;
validate the complete request before moving. Columns 0–2 are note,
sample/instrument and volume. Effect lane `n` uses command/value columns
`3+2*n` and `4+2*n`; the channel's visible effect count bounds navigation.
Changing pattern defaults Follow off. A no-op retains the context
token. Navigation/selection never change song revision, Undo or transport.
`following` is the canonical Mac field; `follow` remains a read-only legacy alias.
Selection bounds are inclusive, matching Mac, and are part of the context token.

All eight Mac `sample.library.*` methods are supported by independent Windows
workers: `get`, `search`, `roots.set`, `rescan`, `inspect`, `preview`,
`preview.stop`, and `multisample.get`. Their contract follows
`../../mac/AUTOMATION.md` and the existing schema. Replies use `library:<uuid>`,
`changed:false`, `playbackStopped:false`, and no document ID. Use
`data.libraryRevision` for roots/rescan guards and optional search/family guards;
imports still need the song's `expectedRevision`. In-flight duplicate write IDs
reject busy; completed retained writes replay.

Normal settings live under `%LOCALAPPDATA%/org.resonance.tracker/SampleLibrary`.
Inspection/audio-test sessions use in-memory roots and preview without opening
hardware. An absolute `--sample-test-library <directory>` enables private
persisted fixtures only in those modes. `workspace.get.sampleLibrary` includes
service status, `pendingRequests`, preview counters and the browser's selection,
inspection and family draft. See `../SAMPLE_LIBRARY_PROGRESS.md`.

`workspace.get.pendingViewCommands` counts native view-opening requests retained
while workers settle, including a currently draining request. Captured document
or target changes discard those requests; returning focus to the pattern cancels
them. Edits are never replayed by this queue. See `../DEFERRED_VIEWS_PROGRESS.md`
and `../Tests/README.md` for qualification and the isolated suite runner.

`workspace.get` reports four panel IDs: `notes`, `samples`, `automation`, and
`instruments`. `locations`, `visible`, `pins`, `targets`, structured `inspection`,
`returnPoints`, and `focus` describe the retained GUI state. The legacy `right`
field identifies only the selected notes/sample inspector. `layout`,
`focusLayout`, DIP `geometry`, `dpi`, and `viewport` remain presentation state.
The automation and instrument entries in `inspection` contain the same editor
snapshots exposed as `parameterAutomation` and `instrumentEnvelope`.

`workspace.panel` accepts `panel`, `pinned`, `focus`, `follow`, `return`, and
`placement`; flags must be booleans. Notes/sample inspectors accept `right` and
`hide`. Pattern automation and instrument/envelope editors also accept `bottom`,
`secondary`, and `float`.
Unknown fields, panels and placements reject with `-32602`; a document operation
in progress rejects editor placement requests with `-32002`. See the
[workspace request schema](workspace.schema.json). Workspace operations require
neither song nor context tokens; supplying either rejects as an unknown field.
They do not edit music, create musical Undo or change the audio device route.

Notes/sample placement alone does not request keyboard focus. `focus:true`
selects and opens the inspector. Each retains its own location; hiding the
selected inspector selects the other only if it is placed at `right`. If both
are hidden, `right` is `""`; separate editors can still appear in `visible`.
`pinned:false` immediately resumes cursor inspection, and `follow:true` performs
the same unpin-and-inspect action. Their original return points remain intact.

Automation/instrument editors start hidden and prefer floating placement. The
`graphCurve` editor starts hidden and prefers the secondary region. Clean,
unfocused editors follow the cursor or selected Graph automation source while
idle and free of retained drafts. `placement:"right"`,
`"bottom"` or `"secondary"` opens and selects that editor in the requested region.
`placement:"float"` restores its
floating window; `placement:"hide"` keeps its native fields and drafts.
`focus:true` opens/selects and focuses the editor, using its last non-hidden
placement when necessary. Placement without focus retains the previous valid,
visible focus where possible. In compact mode, a focused Pattern, inspector or
Main editor can remain selected, leaving the newly placed native editor hidden.
An explicit bottom placement that replaces Main selects the native editor.
Close hides the same retained editor.

For these editors, `pinned:false` or `follow:true` requests a guarded refresh
from the cursor or selected routing source. Pending operations, raw fields, staged edits, dragging and
retained bank/formula drafts can defer the refresh. The request still unpins;
it does not discard the draft or silently retarget an Apply. Automatic follow
only visits visible, unpinned editors while document work is idle and keyboard
focus is outside that editor. Native Reload/From cursor remain explicit refresh
actions. `return:true` selects/focuses the tracker at the editor's original
opening position with playback-follow off. Its stable pattern identity survives
reordering; a replaced document or removed pattern rejects the return. Return
takes precedence over `focus:true` in a combined request.

`workspace.get.editorDock` reports `mode` (`none`, `regions`, or `tabs`), `active`
(the last selected native editor), `trackerVisible`, a DIP `rect`, each region's header/body and selection,
the selected compact tab, and the versioned presentation `configuration`.
Use `compactSelection`, region visibility and `visible` to identify the displayed
host; `active` alone does not identify the focused or visible compact surface.
The right, bottom and secondary regions share resizable boundaries. Editors in
different regions can remain visible together; editors assigned to the same
region share its selection. When the available width or height cannot fit their
minimum bodies, compact tabs show one selected surface. Resizing from regions to
tabs retains the actual focused host and updates `compactSelection`; desired
sizes and placements remain unchanged. Named-layout restoration keeps its saved
tab selection. The layout never enlarges the owner window. **Pattern focus**
temporarily hides docks while preserving placements. Floated editors remain
independent. The **Connected editors** preset opens Pattern, Graph, Instrument
and Automation in four regions where space permits. **Graph editing** instead
shows Pattern, routing, Graph Curve and parameter Automation; the Instrument
editor stays retained while hidden. Connected hides the retained Graph Curve
editor to restore its Instrument/Automation arrangement.

The existing **Automation…** and **Instrument…** actions open the retained
editors in their preferred placement, initially floating. The command palette's
**Dock automation beside the tracker** and **Dock instrument beside the tracker**
actions select the dock. **Open selected source pattern curve** and **Dock Graph
curve beside routing** open the retained curve editor. Ctrl+Alt+D inside each
native editor toggles dock/float.
Each region has a panel selector. Native editor regions offer local placement,
Pin/Following, Cursor and Return actions; Main offers its selector and Collapse.
Compact headers collect native editor actions in a More menu when needed.

`api.describe.revisionGuards` advertises required tokens per write method:
`transport.play`/`transport.stop` require `expectedRevision`, `context.set`
requires both `expectedRevision` and `expectedContext`, and workspace writes
accept neither. `api.describe.workspaceSubset` lists panels, inspector
`placements`, per-editor `editorPlacements`, and supported presets.

`workspace.layout` supports **Compose**, **Pattern focus**, **Sound design**, **Connected**,
**Save custom** and **Restore custom**, matching the shared names. Windows also
supports **Graph editing**, **Delete custom** and **Reload saved**. Custom actions accept optional
`savedName` (default `Custom`): 1–64 Unicode characters, no controls or surrounding
whitespace, up to 24 case-sensitive names. See [workspace schema](workspace.schema.json).
`workspace.get.savedLayouts` lists names; `lowerEditor`, `lowerVisible`, and
`geometry.lowerTabs` describe the retained lower dock. The **Layouts…** manager
and Ctrl+Alt+W expose the same operations; Ctrl+J collapses/reopens the dock.

Layouts save inspector visibility, dock sizes, the base preset, active lower
editor, automation/instrument/Graph Curve locations, each region selection and compact tab.
The `editors` member uses version 3; the named-layout catalogue remains version 1.
Exact version-2 and legacy two-editor configurations migrate with Graph Curve
hidden, while seven-field configurations leave
native presentation preferences unchanged. Restoring keeps current pins, inspected targets, return points, draft
text and song cursor; it never restores old musical targets or adds song Undo.
Older saved configurations without editor placement leave current editor
placements unchanged. Unopened editors are initialized if the saved arrangement
requires them; already-created editors retain their instances. An existing
Guide-only Graph Curve owner remains empty on restore and retains its Guide.
An explicit curve opening or Load selection initializes that same owner.
The selected lower editor can be reopened from its workspace command. A normal app stores layouts
in LocalAppData/org.resonance.tracker/workspace-layouts-v1.json; inspection and
audio qualification keep them in memory. Saves replace atomically. A concurrent
file edit rejects with -32001; **Refresh saved** / **Reload saved** reloads the
catalogue before retry. Invalid storage does not prevent startup.

Automation, instrument/envelope and Graph Curve editors support region/float placement.
The Main-owned Notes, Samples, FX, Plugins, Mixer and Graph share the bottom
region. Arbitrary panel docking and simultaneous Main-owned editors remain
unavailable. See [`GRAPH_CURVE_HOST_PROGRESS.md`](../GRAPH_CURVE_HOST_PROGRESS.md) for the
implementation scope and qualification status.

Use `SCREAMSEQ_TEST_EXE` pointing to a separate QA executable and run
`windows/Tests/run_isolated.py --log <absolute-log-path> --test test_workspace_docking`
with the configured Python runtime. The isolated runner protects the active
musician's desktop; native message/API checks and visual presentation evidence
remain separate. The compact editor suites are `test_parameter_compact_ui` and
`test_instrument_compact_ui`; baseline workspace/layout suites remain relevant.

### Commands, shortcuts and context menus

`workspace.commands.get` accepts `{}` and returns `data.commands`. Each entry
contains `id`, `name`, active `keys`, `defaults`, `customized`, and `contextHint`.
Treat IDs such as `windows.command.<integer>` as opaque Windows IDs obtained from
the current catalog. `contextHint` describes intrinsic local behavior such as
Enter on an FX cell; it is not another configurable global binding.

`workspace.shortcut.set` accepts exactly `command` (a catalog ID) and `keys`
(an array of zero to four strings). An empty array clears a binding. One stroke
sets a shortcut; two to four form a sequence, for example `["ctrl+alt+g", "r"]`.
The first custom stroke needs Ctrl or Alt to preserve note entry. Sending the
exact `defaults` array for that same command restores its trusted default,
including built-in unmodified bindings such as Space or F6. Canonical
strings are lowercase, with modifiers in `ctrl+alt+shift+` order, followed by
printable ASCII or a named Windows key such as `space`, `return`, `left`, or
`f6`. Printable keys use the active keyboard layout's unshifted identity plus
explicit modifiers: a shifted semicolon is `shift+;`, not `:`. The palette
recorder uses the same mapping as dispatch. Use `plus` for the plus key. See the
[workspace schema](workspace.schema.json) for names and aliases. Invalid keys,
duplicate modifiers, unknown IDs and any duplicate or prefix conflict reject
the complete change. Escape cannot continue a sequence. Windows-reserved
Ctrl+Alt+Delete, Ctrl+Escape, Alt+Tab and Alt+Escape combinations (also with extra
modifiers) reject in every position. A prefix expires after 1.5 seconds and
cancels on Escape, a mismatch, unmappable text input such as AltGr, or a
document, selection, focus or active-window change.

Both methods use the usual document result envelope. They require neither
`expectedRevision` nor `expectedContext`, and reject those extra fields.
Setting returns `data.command` and canonical `data.keys`; `changed:false`
describes unchanged song history even when the preference changes.
The operation leaves playback running (`playbackStopped:false`). Successful writes use
the adapter's existing request-ID replay. `workspace.get.shortcuts` reports
only the current sequence `pending` and `hint` presentation state.

Open **Commands** with its default Ctrl+K binding to search, select a command,
then **Set shortcut**, **Set sequence**, **Clear**, or **Reset default**.
Sequence recording accepts two to four strokes; Enter saves and Escape cancels.
Native text editing, open selectors, editor-local commands and musical note
releases retain priority. Custom global bindings do not replace those local
workflows. Normal sessions atomically save overrides in
`%LOCALAPPDATA%/org.resonance.tracker/workspace-shortcuts-v1.json`; inspection
and audio qualification keep them in memory. An absent file is not created by
reading it. Invalid or concurrently changed storage rejects publication and
retains live bindings; it does not alter song data. Use the palette's
**Workspace / Reload saved shortcuts** command to load the current preference
file before retrying a rejected write. This is a separate explicit action;
failed writes never overwrite another session's preferences.

Native context menus are available on the pattern grid, lower sample waveform,
graph canvas/nodes/wires, instrument envelope editor, and detailed sample
editor. They expose existing actions with current enabled/check states. The
instrument menu includes point editing, tools/bank, Apply and dock/float; sample
detail groups selection/view, drawing, processing, private clipboard, loops,
crossfade and settings. Native text fields retain their standard editing menus.
Main workspace menu shortcut labels reflect the current global bindings;
clearing a binding removes its hint. Keyboard context requests on an unsupported
or hidden canvas do not fall back to a different musical target.
Each application menu captures its musical target and rechecks document,
revision, selection and relevant draft state after the native modal loop.
Cancellation performs no musical operation; a changed target rejects the
chosen action. Existing shared API, Undo and native persistence paths perform
musical edits. See [command workflow progress](../WORKSPACE_COMMANDS_PROGRESS.md)
for current qualification and remaining scope.

## Wire and security

The base adapter catalog includes `api.describe`, `document.get`, `pattern.get`,
`context.get`, `transport.get`, `transport.play`, `transport.stop`, `context.set`,
`workspace.get`, `workspace.panel`, `workspace.layout`, `workspace.commands.get`
and `workspace.shortcut.set`. Attached document hosts
can enable additional operations; use the live catalog, not a hard-coded superset.
Unsupported methods return `-32601`. The adapter uses the existing
string-ID JSON-RPC envelope, revision guard (-32001), parameter errors (-32602),
busy (-32002), engine errors (-32003), document result envelope and transport
response dictionaries. Unknown envelope keys/batches/notifications are -32600.
Unbound adapters only describe capabilities; song requests return busy rather
than fabricated state.

### Classified write outcomes

A failure to deliver or present a result is separate from whether the operation
took effect. Classified errors retain the existing public error codes and may
add `error.data.writeOutcome`: `notCommitted`, `noChange`, `committed` or `unknown`.
See [write-outcome.schema.json](write-outcome.schema.json) for this optional data.
`documentId` and `revision` are included only when known at that boundary; a
post-write snapshot or serialization failure never advertises the pre-write
revision as current. `committed` means an effect occurred but completion failed;
`unknown` requires reconciliation. Neither permits blind resubmission.

The current worker classifies known committed publication failures. The adapter
classifies completion failures after a returned host write, including invalid
UTF-8 or a result exceeding the wire bound. The native application preserves a
classified worker exception through best-effort view repair. This is not yet
exhaustive classification of every host side effect. Absence of outcome data,
an ordinary `-32003`, or an unchanged song revision does **not** prove rejection.
Filesystem, library, take and device effects require their own domain readback.
Transport loss after sending can also leave the outcome unknown to the client.

### Bounded write replay

`api.describe.result.data.writeReplayCache` reports the policy and aggregate
occupancy (`retainedEntries`, `retainedSerializedBytes`), never cached IDs,
parameters, paths or response contents. Per SessionAdapter, retention is limited
to **64 write responses and 8 MiB (8,388,608 bytes)**, whichever binds first.
The byte charge is precisely `request.dump().size() + response.dump().size()`:
compact, sorted-object-key UTF-8 JSON envelopes, without newline delimiters or
original request whitespace. It is **not an 8 MiB heap/RSS guarantee**: retained
response JSON nodes, string capacities, ID copies, containers and allocator
overhead cost additional memory. Temporary snapshots, serialization buffers and
in-flight responses are outside this retention accounting.

Successful write-method results enter the cache, including successful no-ops
and `dryRun:true` previews. Classified `committed` and `unknown` write errors
also enter it. Proven `notCommitted`/`noChange`, unclassified validation/host
errors and all reads remain uncached. A retained request's ID, method and
parameters must match its canonical parsed JSON exactly; key order/whitespace
are irrelevant, but an integer changed
to a floating-point parameter is different. An exact retry returns the original
complete response without invoking the host or rechecking now-stale tokens.
Reusing that ID for a different write returns `-32600` and preserves the original
entry. Applying a dry-run proposal requires a new ID and current revision.

Insertions evict the oldest entries until both limits hold; a replay
does not refresh an entry's age. An entry whose charge alone exceeds 8 MiB is
not retained and does not evict older entries. Retention is best effort: a size
limit or allocation failure must not convert an already completed write into a rejection
with the pre-write revision. A deliverable success is returned even when it
cannot be cached. Undeliverable write results instead produce a small `-32003`
error with the original ID and `writeOutcome:"unknown"`, retained under the same
bounds. The transport still has a generic fallback for failures outside this
adapter (including malformed read replies); it makes no rejection claim.

This is a **bounded, session-local replay window**, not durable/global
idempotency. After eviction, skipped retention or adapter destruction, a request
is processed normally: document and context revision guards still reject stale
writes, but an unguarded workspace write or unchanged-revision transport/no-op
can run again. Never automatically resend an uncertain write. Read back current
state and rebase deliberately; do not weaken revision guards to force a retry.

Windows request/response byte bounds match the Mac server
(`mac/App/AutomationServer.swift`):
one client at a time, one newline-terminated UTF-8 request/reply per connection,
32 MiB request including newline, 32 MiB response, maximum JSON nesting 64,
5-second default read/write/drain deadlines (fixed per phase, not extended by
dribbled bytes). Connect wait is interruptible by the stop event. Pending I/O
uses OVERLAPPED and CancelIoEx; cancellation is drained before buffer lifetime
ends. There is no FlushFileBuffers, which could block indefinitely on a client
that refuses to read. After replying, the server briefly waits for client close
so DisconnectNamedPipe does not discard an unread reply, with the same bounded
and cancellable deadline.

The pipe has a protected DACL containing exactly one allow ACE for the current
process user's SID. It does not grant Everyone, Network, Administrators or
SYSTEM. PIPE_REJECT_REMOTE_CLIENTS rejects remote connections separately.
FILE_FLAG_FIRST_PIPE_INSTANCE plus one persistent max-instance=1 handle makes
startup collisions fail closed and prevents namespace gaps between clients.
This is a same-user boundary, not isolation from other processes of that user.
Remote rejection has been code-inspected, not tested from a remote machine.

## Python client

```powershell
python windows/Api/client.py --pipe '\\.\pipe\ScreamSeq.Api.1234' api.describe
python windows/Api/client.py --pipe '\\.\pipe\ScreamSeq.Api.1234' document.get
python windows/Api/client.py --pipe '\\.\pipe\ScreamSeq.Api.1234' pattern.get '{"pattern":0}'
```

`Client(pipe, timeout=5).call(method, params={}, request_id=None)` returns the
result dictionary and raises `ApiError` with code/data for protocol errors.
It uses standard-library ctypes with overlapped I/O and bounded cancellation.
Only pre-send busy connection establishment can wait/retry. There are **no
request send retries**, including for protocol busy. A failed send/read raises
`TransportError.may_have_been_sent`; inspect state before retrying an uncertain
transport write. The client also requests anonymous SQOS to prevent a pipe
server from impersonating its credentials. The shared Mac client is untouched.

## Reproducible isolated tests

```powershell
cmake -S windows/Tests/Api -B bin/windows-api-cache-fix -G 'Visual Studio 17 2022' -A ARM64
cmake --build bin/windows-api-cache-fix --config Release --parallel 2
ctest --test-dir bin/windows-api-cache-fix -C Release --output-on-failure
```

Built and executed with VS2022 MSVC 19.44.35229 Hostarm64/arm64. Native CTest
covers real pipe roundtrip, malformed/batch/oversize/deep JSON, actual live SID
DACL and protected flag, first-instance collision, stopped endpoint removal,
restart, stalled read/write shutdown, snapshot pagination/validation, revision
rejection, transport deduplication, owner-thread enforcement and the real-pipe
bounded serialization-error fallback with committed-state readback.
`SessionCacheTests` exercises the actual adapter with a protocol-only fake host:
80 successful 4096-cell pattern.apply-equivalent requests/change-list responses,
independent byte/count FIFO eviction, exact replay and accounting, the inclusive
8 MiB boundary and one byte over, UTF-8/escaping, oversize success preservation,
uncached failures, changed ID/method/parameter types, fresh reads, no-op/dry-run
retention, context guards and cache serialization failure. These are not tests of
Document validation, musical edits, history, persistence or audio.

The separate `python windows/Tests/Api/test_client.py -v` tests expect the original
`bin/windows-api/Debug/ApiTests.exe` build unless the test module's `EXE` is set by
an isolated runner. Python tests
launch a separate synthetic session host over a real pipe, exercise read/play/
stop, error codes, explicit local addressing and no uncertain-send retry.
Tests use disposable fixture data and never open an audio device.

The standalone tests do not establish full API parity or qualify the running
application. Use its current method catalog and separate actual-app tests for
the integrated path. Remote-machine/cross-account behavior and endpoint discovery
are outside these tests. A method's logical payload limit is not necessarily
reachable through the transport: large base64 PCM, clipboard or collection
replacements must also fit the complete 32 MiB request. The exact-boundary test
accepts 32 MiB including newline, rejects one additional byte before dispatch,
and checks the next small request succeeds. Newline detection scans each received
chunk once rather than rescanning the accumulated request.

The application now registers AssetOperations through the host's explicit
additionalDocumentReads/additionalDocumentWrites catalogs. The worker retains
its private sample clipboard across requests and replaces it on document open.
Standalone test hosts still advertise only their implemented method sets.

## Pattern FX and precise notes

`pattern.effects.get/set`, their `pattern.performance.get/set` aliases,
`pattern.effect.set`, current `pattern.notes.get/set`, `pattern.transform` and `pattern.paste`
are integrated. Use the shared Mac schema for payloads and `pattern.commands`
for source-format IDs, masks and two-character display codes. FX columns are
0–7; cursor code/value fields are `3+2*column` and `4+2*column`. Commands and
precise notes use 65536 units per row. Replacement APIs preserve omitted
collections; single-cell clear uses `command:null`. Reads expose unresolved
bindings without silently retargeting them. Writes require `expectedRevision`.

The native FX and precise-note inspectors use these same transactions. A precise
row draft captures its target and revision, preserves unrelated events, and saves
through one `pattern.notes.set` transaction. `workspace.get.noteEditor` reports
its target, selected event, count, pending/stale status and owner-client DIP canvas
bounds. The sole native editor is `workspace.panel {panel:"preciseNotes"}`;
`notes` remains the read-only inspector with independent pin/origin. `preciseNotes`
and compatibility `noteEditor` snapshots describe the same retained HWND, stable
pattern/track IDs and raw fields; workspace polling omits the full row draft.
The retained native pages are Timeline, Hit, Tools and read-only Details; Details
shows captured timing and note-local effect guidance without changing the draft.
Command 107 and the lower Notes alias open that sole owner. Editors preferences
V4 strictly records all four native identities. Older bottom Notes selections
migrate to preciseNotes bottom; seven-field Notes aliases reuse an existing
placement and choose bottom only if hidden. Explicit V4 files use native
placement rather than a Main `notes` identity. No song format or pattern.notes
request semantics change.

The native clipboard publishes Mac's `ScreamSeq Pattern 2` Unicode text format,
including relative FX and only the referenced stable bindings. It also accepts
legacy `Resonance Pattern 1` text. `pattern.paste` supports overwrite, merge and
mix, field masks, explicit clipping, bounded preview and one Undo. The native
actions use clipping; API requests default to rejecting an oversized destination.
Ctrl+Shift+V mixes into empty fields; Merge Paste is in the command palette.
Parsing/serialization runs off the UI thread, bounded to 16 MiB and 262144 cells.
Clipboard source and destination are captured before asynchronous work.

## Plugin rack integration

The application also registers `PluginOperations`. Shared Mac method payloads
are used for discovery, add/remove/move/bypass, parameters, saved state, buses,
programs, sound presets and instrument aliases. Musical plugin writes require `expectedRevision`;
`history.undo`/`history.redo` with `domain:"plugins"` use an independent history.
The native rack uses these same transactions. The complete current inventory is
in `api.describe`. Browser preferences use their separate library revision.

Windows adds `plugin.editor.open` and `plugin.editor.close`, each accepting
`slot` and `expectedRevision`. Open/close alone retain the musical revision.
The controller owns separate saved-baseline instances on its worker; vendor UI
lives on the provider's private STA. It polls only open editors, debounces their
state changes, and forces capture before save/close/history. A vendor edit may
therefore make a captured expected revision stale; read and rebase. State reads
return the resulting revision. Validated `plugin.parameters.set` batches reach
the playing chain together at a render boundary and update the saved baseline
and plugin history independently from song automation. Dry runs, rejected writes
and successful no-ops publish nothing. A full/unavailable queue stops playback
before committing the complete baseline, rather than applying a batch prefix.

Native editor gestures use the same live path. Parameter polling runs on a 16 ms
timer while editors are open; full saved-state capture waits for a 400 ms gesture
boundary, with immediate capture for reads/save/close/history. Replaying the
gesture on a disposable saved-baseline instance must reproduce the complete
editor state before the edit is classified as parameter-only. Opaque preset/IR
changes, structural rack changes and plugin Undo/Redo still stop playback. API
parameter commits currently close baseline editor windows before refreshing
their saved state. State capture, validation, history and plugin construction
remain outside the audio callback.

`document.get` exposes `canUndoPlugins`, `canRedoPlugins`, `openPluginEditors`,
and rack slot/identity/bypass/instrument aliases. Removing a rack entry retains
unresolved native automation, routing and binding identities; absolute slot
automation is removed/remapped as on Mac. No target silently retargets.

`instrument.create` also accepts the shared `empty:true`, optional `name` and
`dryRun` fields. The new instrument has an empty sample keymap; sample-only
conversion preserves existing sample numbers and playback. Creation belongs to
document history. The native **New trigger** action then calls
`instrument.plugin.set` to assign it on channel 1 in plugin history, retaining
the created stable identity for retry if assignment fails.

Factory-program loads match the shared response fields: `plugin`, selected
`program`, `catalogRevision`, `validated`, `loaded` and `dryRun`. A dry run only
validates the catalog and selection; it does not load a disposable vendor
program. Actual loads recheck the catalog on the saved-baseline instance before
replacing opaque state. Native program selections retain their captured revision
until Load or Escape. The rack also exposes selective auxiliary-port controls;
`document.get.nativePlugins` includes `auxiliaryInputs` and `auxiliaryOutputs`.

`plugin.instruments.set` now returns the Mac contract: `wouldChange`, `dryRun`
and `routing` (the proposed get-shaped inventory/assignments). No-ops retain
revision/history and preserved opaque fields. `instrument.plugin.set` includes
`wouldChange` and validates capacity before either dry run or commit. Effect
plugins reject alias writes even for an empty list. The legacy `plugin.assign`
replaces the primary, retains other aliases, preserves a promoted alias's MIDI
channel and moves an instrument from its previous owner atomically; zero clears
all assignments. Whole-list `plugin.instruments.set` rejects ownership conflicts.
Shared duplicate/ownership/capacity failures use invalid parameters (`-32602`)
before playback or history changes, matching Mac.

The rack's Instrument assignments page and command palette open a modeless
native editor. `workspace.get.pluginInstruments` exposes its captured plugin,
document/revision, selection, draft, inventory and status. Preview/Apply use
the same API; changes use plugin Undo. Close retains the draft. Reload explicitly
refreshes it, and Discard/close releases a draft even after its source is removed.
Reopening a visible/dirty window preserves its captured plugin when rack
selection changes. See `../PLUGIN_ALIASES_PROGRESS.md` for evidence and limits.

`plugin.preset.inspect` accepts an absolute `.screamseq-preset` or legacy
`.resonance-preset` path. It returns `name`, `descriptor`, `stateBytes`,
`presetVersion` and the SHA-256 content token `presetRevision`. Binary and XML
property lists use the Mac wire schema, with a 16 MiB state limit and bounded
file/structure validation. No vendor is instantiated during inspection.

`plugin.preset.save` takes stable `plugin`, `path`, `name`, optional `overwrite`
and `dryRun`, plus `expectedRevision`. It atomically writes the saved sound
baseline and descriptor; routing and instrument assignments stay in the song.
The response adds `path` and `written` to the inspection fields. Saving has no
document-history effect. Dry save validates and encodes without writing a file.

`plugin.preset.load` takes stable `plugin`, `path`, `expectedPresetRevision`,
optional `dryRun`, and `expectedRevision`. It rechecks file content and plugin
class identity, independent of installation path/display name. A dry load
performs no vendor state decode. An actual load validates a disposable processor
before replacing only opaque sound state in one plugin Undo transaction. The
response is `{preset, plugin, loaded, dryRun}`. Invalid/stale loads leave the
song unchanged; an identical canonical state creates no history. Aliases, ports,
bypass, automation, routing and unknown plugin metadata are retained. State
loads use the existing stop-before-publication path. Native file dialogs capture
the document, revision and target before opening and reject stale results.

`plugin.library.get` accepts optional `format` (`AU`, `VST3`, `Built-in`), `kind`
(`effect`, `instrument`), `rescan`, `search`, `category`, `favoritesOnly` and
`includeHidden`. It returns decorated `plugins`, all discovered `categories`,
`libraryRevision`, `preferencesAvailable`, `warning`, `preferenceError` and
`totalPlugins`. Each row retains its descriptor and adds `catalogID`, `favorite`,
`hidden`, `customCategory` and effective `category`. Search ignores case and
diacritics. Default categories are Effects/Instruments.

`plugin.library.set` requires `expectedLibraryRevision`, `catalogID` and at least
one of `favorite`, `hidden` or `category`; `dryRun` is optional. It accepts no
`expectedRevision` and does not flush editors, stop playback, change document
revision or create Undo history. The response contains `libraryRevision`,
`wouldChange`, `written`, `catalogID` and complete normalized `preferences`.
Successful writes retain the usual exact-request replay behavior. Stale writers
fail with `-32001`; a held process lock fails with `-32002`. No-ops/dry runs
retain the file/revision; returning to defaults removes that customization.

Preferences are a bounded 2 MiB JSON file with up to 4096 customized entries.
VST3 library identity includes its normalized installation path and uppercase
class ID; built-ins use the effect ID and AU uses component codes. Display names
are excluded. This differs deliberately from portable preset matching.
Missing plugins do not cause stored preferences to be deleted. Corrupt, locked
or inaccessible preferences yield an empty revision and warning while catalog
rows remain available for insertion; the original file is retained.

Normal sessions use `%LOCALAPPDATA%/org.resonance.tracker/plugin-library-v1.json`.
Inspection/audio qualification sessions never use that file automatically;
`--plugin-test-library <absolute path>` explicitly selects a private test file.
The native Browse window caches catalog rows for local filtering, retains
category drafts across Close, and exposes its state in `workspace.get.pluginLibrary`.
See `../PLUGIN_LIBRARY_PROGRESS.md` for evidence and limitations.

Windows extensions `plugin.path.get/scan/set` repair a VST3 instance's saved
installation path using stable `plugin` identity. `graph.plugin.path.get/scan/set`
use `graph` and `node` instead. They do not change the portable project schema.

`get` returns the target, `descriptor`, `moduleVerified`, `reason` and matching
scanned `candidates`, each containing `descriptor` and `moduleSHA256`. It checks
the current binary against the scan cache; verification is not a vendor-state
load. Candidates match the exact VST3 class and instrument/effect role. Library
visibility preferences do not hide repair candidates. Location inspection does
not instantiate a vendor; the normal session boundary can first capture pending
edits from an already open vendor editor.

`scan` requires `expectedRevision` and an absolute VST3 bundle or module `path`.
It explicitly scans that path in the isolated scanner, verifies the matching
class and returns fresh location information. It can update the scan cache but
does not change the song or its history. A stale document is rejected before
scanning. A module containing a different class may be cached but cannot repair
this target.

`set` requires `expectedRevision`, `path`, `expectedModuleSHA256` from `get` or
`scan`, and optional `dryRun`. It rechecks canonical path, class, role, native
architecture and actual binary hash. A dry call performs no vendor-state decode.
A changed actual location must successfully load the saved state and ports into
a disposable processor before modifying the song. Only the path changes: exact
opaque bytes, identity, bypass, aliases, automation and unknown metadata survive.
Rack repairs use plugin Undo; graph repairs use document Undo, preserving the
entire graph recipe and connections. A matching graph editor draft is closed
after a successful change. Structural publication currently stops playback.

The response includes the target, canonical `path`, `moduleSHA256`,
`wouldChange`, `dryRun` and `reconnected`. A no-op creates no history. Stale song
guards use `-32001`; invalid/mismatched modules and unsupported formats use
`-32602`. Removed targets are rejected. AU state is preserved without conversion
or substitution. See `../PLUGIN_PATH_PROGRESS.md` for native controls and tests.

Discovery reads the cache. Explicit `rescan:true` scans installed VST3 roots
through the isolated scanner; failures are reported with module paths while
successful scans remain available. The exact class/path/architecture/hash guard
still applies when loading. No automatic substitution or rescanning occurs on
project open. Unknown plugin/project fields survive save and plugin history.

The native song routing window uses the existing `mixer.bus.set`,
`mixer.sends.set`, `mixer.sidechains.set`, `mixer.plugin.route`, `graph.routes.set`,
`graph.assign`, `graph.instrument.assign` and `graph.layout.set` contracts.
`workspace.get.songRouting` reports its document/revision, stale/pending flags,
route/layout drafts, selected node/wire, filter/page, status and retained canvas
geometry. Node keys match Mac layout identities; collapsed sample-instrument
groups also retain an `instrument-graph:<instrument>:all` presentation key.
No musical schema or routing semantics change. See `../SONG_ROUTING_PROGRESS.md`.

For silent workflow qualification, `--audio-test-allow-stop` requires both an
audio-test mode and `--automation`. It keeps the owned QA process available after
an intentional stop, so tests can observe post-edit state. Default audio-test
runs still fail when playback stops. The flag does not change normal sessions,
audio fault detection, routing validation or callback behavior.

The native pattern graph command editor uses the existing `graph.commands.set`
and `mixer.enable` operations; timing stays at 65536 units per row.
`workspace.get.graphCommands` reports its captured document/revision,
stable pattern/bus targets, row/lane, draft/pending/stale flags and status.
`workspace.get.graphLanes` reports the row-aligned strip bounds, lane identities,
selected/first lane and visible formatted commands. These are Windows UI
observations, not a new musical command schema. See `../GRAPH_COMMANDS_PROGRESS.md`.

`workspace.get.liveKeyboard` now reports the native Live keys mode. The
Windows-only `musicalTyping` observation contains the selected sound's `sample`
kind flag, `slot`, stable `id`, module `noteMin`/`noteMax`, and `held` physical
inputs with captured sound/pitch and pending/started flags. These are transient
UI fields, outside document persistence and Undo. Native pattern typing uses
the existing `pattern.apply`; explicit audition uses `transport.note` and
`transport.panic`. See `../MUSICAL_TYPING_PROGRESS.md`.

Sample detail settings/replacement and native multi-file imports use
`sample.patch`, `sample.import`, `sample.importMany` and `instrument.create`.
Omitted `pan` now preserves inherited panning rather than enabling a sample
override. Explicit `pan` still enables the override. `workspace.get.status`
reports the main window's status text for native workflow diagnostics.
Omitted rate, volume and loop fields also retain their exact stored values,
including legacy relative tuning and sub-unit volume precision.
See `../SAMPLE_SETTINGS_PROGRESS.md` for the retained-draft/file-chooser behavior.

Native instrument import uses `instrument.import` with a captured revision and
new slot. The keymap list stages the existing `instrument.patch.values.mapping`;
it does not introduce different musical editing semantics. Windows-only
`workspace.get.instrumentEnvelope.mappingFields`, `mappingDirty` and
`selectedKey` expose unfinished range fields, staged mapping state and the
native list selection. See `../INSTRUMENT_IMPORT_PROGRESS.md`.

## MIDI input and retained recording takes

`recording.get/start/capture/stop/commit/discard` use shared precise-note recording;
see `recording.schema.json`. Writes require current `expectedRevision`, plus an
opaque `take` ID for operations on an existing take. Start pins the base revision,
distinct zero-based raw `channels`, `instrument` (1–255), optional `quantization`
(0–65536 row units) and `latencyMS` (−500 through +500; positive places input earlier).

Capture validates its entire array of at most 1024 events before changing the
take, then sorts equal timestamps stably. Events supply decimal uint64
`timestamp`, MIDI `status`, `note` and `velocity`. Windows host time is QPC
converted to 100 ns units. Note-on/off and CC120/123 are accepted. The audio
presentation clock accounts for primed silence and each renderer slice. Missing
or expired mappings increase `missingTime`; no cursor time is substituted.
A take binds its first valid stream generation and cannot join a restarted
stream. `transport.get.recordingClock` reports validity, generation,
discontinuities and host ticks per second.

Get returns `take`, `capturing`, `compatible`, `baseRevision`, `eventCount`,
`events`, `missingTime`, `exhaustedVoices`, `overflow`, `inputError` and current
`hostTime`. Events contain stable native `patternID` and `track`, row-unit
`position`, core `note`, `instrument` and `velocity`. The compact
`workspace.get.recording` summary omits event payloads. A stopped compatible take
commits with optional `replaceRows` and `dryRun`. Commit validates the complete
candidate and applies one document Undo. Dry runs, invalid requests and stale
commits retain the take. Take lifecycle alone does not advance musical revision.
Successful commit/discard consumes the take and any imported recovery wrapper.

API `transport.stop` ends capture and retains the take. Native Stop/Space also
attempts Finish. Lost input, exhausted voices, overflow or an incompatible base
retain the stopped take for review. Explicit Finish can accept a compatible
partial take; it cannot rebase a stale one. Save/Open/close require Finish or
Discard first. Recovery snapshots copy and close held notes without changing
the live take. Restore deliberately ends old capture before protecting/replacing
the song, excludes input during that boundary and hydrates a fresh stopped take.
Failed restore leaves the old take stopped and available. Optional
`recoveryTake.inputError` preserves loss reasons; older files remain supported.

Windows-only `midi.devices.get` and `midi.settings.get/set` use a separate
`expectedMidiRevision` guard; see `midi.schema.json`. They expose opaque device
interface IDs, connection state and input-loss/timestamp counters. Preferences
are `source` (empty disconnects), `armed`, `channelsCount`, `quantization` and
`latencyMS`; they are session state outside musical Undo. No-ops preserve their
revision. Dry runs validate without changing input, takes or music. Device calls
run on a control worker. WinMM callbacks queue bounded raw messages; driver
milliseconds map to host time with precision and anchor uncertainty exposed.
Source loss releases input holds and retains the take. Input during Finish,
Discard or song replacement cannot later become a cursor step edit.

The native MIDI & recording window and command palette expose input selection,
Arm, timing, adjacent columns, review, Finish, Discard and precise-note navigation.
Armed stopped MIDI enters cursor notes; armed playback starts a take after Play.
The main Sound selector is authoritative. Computer keyboard step entry and Live
keys keep their existing behavior. MIDI and keyboard holds for the same current
sound/pitch share audition voice ownership.

Qualification only: `--midi-test-input` requires automated inspection or explicit
audio-test mode. It substitutes an owned input adapter and advertises
`midi.test.inject`, guarded by settings revision and connection generation.
Injected driver messages traverse the production queue and timestamp conversion.
Its optional `after` barrier invokes actual `apiStop` or `nativeStop` before timer
servicing to test pending batches. Normal sessions never expose this method.
This fixture does not qualify physical MIDI drivers, hotplug or hardware latency.

Graph interaction parity (30 September 2026): `graph.node.add` supports `insertEdge` or `connect`; modulation Add initializes zero depth and a shared target base. `graph.nodes.detach` preserves internal/sidechain connections and heals a unique serial Main path, with optional `remove` and saved `positions`. These changes use the shared graph validation and one transaction. Rack parameter/bus/bypass APIs accept a persistent `plugin` ID instead of `slot` (exactly one). The new GraphOperations regression scenario is `cableInsertionAndDetachment`; this Mac checkout has not executed the Windows binary.

### Shared song processing groups

`graph.song.group.create/update/remove/export` mirrors the Mac API in
`mac/AUTOMATION.md`. `graph.get.groups` persists nested presentation boundaries
around `plugin:<instanceID>` rack members. Grouping, moving and ungrouping keep
rack ownership, real mixer cables, stable parameter targets and DSP unchanged.
`graph.layout.set.groups` batches boundary movement with ordinary node positions.
Export uses the host's saved baseline-state hook and creates a fresh independent
library recipe from consecutive enabled effects on one bus. Missing/bypassed
members and unsupported topology fail before editing. All operations support
revision guards, strict input, dry runs, no-op history and Undo/Redo. The portable
adapter and codecs are tested on macOS; this does not claim native Windows UI
interaction or device qualification.


`graph.signal.get` now exposes exact adopted mixer-route contributions alongside
physical audio ports. A route reading carries
`route:{kind,source,target,plugin,input,output,tap,gainDB,preFader}`; unused IDs
are empty strings and unused numbers zero. `compensation` is its route delay.
The captured contribution is after route gain/delay and before destination
summing. Serial inserts use `tap:"main-path"` before auxiliary Main-in summing;
Master's processor output uses `tap:"pre-master-fader"`. Other routes use
`tap:"post-gain"`. Plugin/recipe auxiliary outputs do not imply a bus-fader tap.
Use the returned opaque key for Scope/Listen and keep route observations out of
physical socket indices. Missing or ambiguous route observations are unavailable,
not the source's output meter. Gain and tap metadata describe the adopted plan,
including while another plan is preparing. Shared host tests cover route PCM;
the Windows JSON adapter remains subject to a native Windows execution check.
Reusable recipe internals are outside this initial route-observation slice.

`graph.note.activity {}` reads actual prepared note delivery counters without a
revision or musical edit. It reports the playback `engine` identity, device
`sampleRate`, accepted `requestedGeneration`, audio `adoptedGeneration`, `pending`,
`available`, `active`, and `fresh`. An unavailable host has no engine or route
records. Only previously adopted route incarnations appear in `routes`:
`token` identifies the cable/destination incarnation, `copy` identifies the
prepared destination, `route` is a native ID (null for an implicit assignment),
and `sourceKind`, `source`, `plugin`, `midiChannel`, `implicit` identify its cable.
`current` means the latest accepted plan contains it; `member` means the audio
plan contains it. During pending adoption these can differ. Retired records
retain their counters until their plans and held ownership retire, then disappear.
A replacement token starts a new counter history; missing data is never silence.

Each record exposes accepted `events`, `noteOns`, `noteOffs`, `failures`,
`routingReleases`, `lastFrame`, `heldNotes`, and `heldPedals`. `lastFrame` uses the
engine event/sub-block frame, including scheduled controller frame offsets.
Removing one of several owners increments `routingReleases` and releases its
ownership without inventing a physical note-off. Use event-count deltas for
activity pulses. These are note/MIDI counters, not audio-level meters. Snapshots
are control-thread reads of lock-free counters; `fresh:false` means an adoption
crossed the read and membership should be refreshed. Up to 4,096 retained route
incarnations are bounded and reclaimed on the control producer.

`plugin.duplicate {plugin, position?:{x,y}, dryRun?}` copies the saved manual
preset, bypass and auxiliary configuration into a fresh plugin identity. The
result includes `slot`, `plugin`, `detached`, and `dryRun`. It copies no instrument
assignments, recorded points, or song cables; effect copies start detached. The
rack and optional canvas position join one unified history operation and persist.

### Pattern time ruler

The native pattern menu and command palette expose **Increase row height** and
**Decrease row height** (commands 598/599). Heights are 18, 22, 26, 30 and 34 DIPs;
the endpoints clamp. Bind either through the existing shortcut editor. Pattern
and graph-lane drawing, hits, inline fields and visible-row calculations share
the height. `workspace.get.geometry.pattern.rowHeight` exposes its current value.
Changing height retains the song/history, playback, edit cursor, selection,
viewport and retained inspector drafts. A captured mouse gesture or document
operation must finish first. This changes row spacing, not font size.

Custom saved layouts retain a nondefault height in optional local
`patternRowHeight`; a missing field restores the historical 18-DIP default.
The height is never written to project files. A malformed height refuses the
entire restore before preparing or changing editors. Older app versions may
reject layouts containing the new field; their existing saved layouts still
load, and the new default layout shape remains unchanged.

`pattern.timeline.get {pattern, order?}` is a document-worker read advertised by
`api.describe`. It uses the same shared engine walk as Mac, in the current
sequence. Omit `order` for the first occurrence, or specify the zero-based order
containing the requested pattern. `positions` contains `row`, zero-based `beat`,
`patternSeconds` and `songSeconds`. Times describe first visits, account for
tempo/speed, groove, native timing and flow, and remain null for unreachable rows
or unarranged patterns. Pattern time starts at the first reached row. Invalid
patterns, mismatched orders, booleans/fractions and extra parameters reject with
`-32602`. Reads do not edit music, advance revisions, stop playback or add history.
The shared schema already defines this Mac-compatible request.

`workspace.ruler {mode}` accepts `rows`, `beats`, `patternTime` or `songTime`.
Like Mac, it is session view state and accepts no revision tokens; the result is
`{mode}` with `changed:false`. `workspace.get.positionMode` reports the choice.
The native pattern header button and command palette cycle the same modes; its
command is customizable without reserving another default shortcut. Mode changes
preserve the edit cursor, selection, scroll and focus, and do not modify projects.

`workspace.get.ruler` additionally reports `mode`, selected `order`, `pending`,
`ready`, `error` and `gutterWidth`. Pattern geometry exposes `gutterWidth` as well
as `headerHeight` for clients doing hit testing. The native display uses the
selected order when it contains the edited pattern, otherwise its first
occurrence. One asynchronous worker read supplies cached labels. Document,
revision, sequence, pattern, occurrence and mode changes invalidate the result;
late completions cannot fill a different view. Pending, failed and unreachable
times display `--:--.---`, not invented zeroes. Failed queries retain a diagnostic
and do not retry continuously. Switching mode or changing the source allows a
fresh query. No device/renderer work happens in the ruler or during drawing.

### Retained Pattern tools

The native **Pattern / Tools: preview transforms** command (591) opens a retained
Win32 workbench over the existing `pattern.transform` API. It exposes all 14
operations, selection/column/pattern/song/note-track scopes, field masks, numeric
filters, curves, seeds and explicit permission for destructive row edits. The
Mac and Windows forms use `editor/PatternToolCatalog.h` for operation names and
applicable options; musical validation remains in the shared PatternTools engine.
There is no new transform method or project representation.

Opening captures the document, revision, pattern, column/note track and selection.
Navigation and reopening retain that target. **Use current selection** explicitly
captures a new target while retaining raw settings. Preview must succeed for the
current settings before Apply; changing any setting invalidates it. A revision
change requires recapture and a new preview. Apply uses the exact preview request
with `dryRun:false`, preserves the edit cursor, and creates one Undo operation.
Both module-cell changes and native-only row changes enable Apply. Numeric
operations retain the shared engine's module-cell behavior; row operations also
transform precise notes and the selected native FX layer.

`workspace.get.patternTools` reports `visible`, `captured`, `draft`, `preview`,
`prepared`, `dirty`, `pending`, `completed`, `generation`, `stale`, `applyEnabled`,
`completion`, `observation` and `status` while the owner exists; an absent owner
reports only `visible:false`. These diagnostics are session state, not a portable
project or a replacement mutation API. Raw incomplete fields, previews and
unresolved results participate in the document-departure guard, including when
the window is hidden. Successful document replacement retires the owner.

A fallible native completion retains its worker receipt and later raw text.
**Review result** never repeats a write. Without an exact receipt, Review
synchronizes the original document and offers an explicitly unverified state
observation; **Accept observed state** requires its unchanged revision. Inspect
the affected music using **Pattern / F6** before accepting it. New operations
still require explicit capture and Preview. Tab/Shift+Tab use native navigation,
Ctrl+Enter applies a prepared preview, and Escape hides the retained window.

### Move an order occurrence

`order.edit {expectedRevision, order, operation:"move", destination}` moves the
selected occurrence to the zero-based final index in the current sequence. It
uses the shared document operation, carries the occurrence's stable identity and
section metadata, and preserves intermediate occurrences, including repeated
patterns and End/Skip slots. The move is one Undo step and persists in native
projects. Native arrangement selection follows its stable occurrence identity.
Moving to the same index is a no-op: no Stop, new revision or lost Redo.

`destination` is required for move and rejected for other operations. Invalid,
boolean, fractional and out-of-range destinations are rejected before Stop or
mutation. Mac now also rejects an unused destination on remove; previously that
branch bypassed the existing move-only parameter check. The shared schema
expresses the same conditional requirement. Existing before/after/assign/up/down
and remove operations retain their meanings.

### Musical input navigation

The native command palette exposes **Lower/Raise input octave**, **Previous/Next
input instrument or sample**, and **Use instrument at edit cursor** (commands
592–596). Each can receive a shortcut through the existing customization UI;
no new global default overrides a text editor or existing shortcut. Enter on a
pattern instrument cell also uses its instrument. If that cell is empty, the
first precise note in that row/column with a nonzero instrument supplies it;
an empty result leaves input unchanged. The pattern context menu exposes the
same action.

These commands change future note input only. They preserve the edit cursor,
selection, focus, raw drafts, song revision and history. Already held notes keep
their original destinations for release. Instrument stepping includes empty
slots 1–255 and stops at either boundary; octave stepping retains the native
chooser's existing 0–9 range. Boundary no-ops do not revise the input context.
Busy/replacing documents refuse changes. The existing `workspace.input` API
remains the guarded API for instrument and octave setup, with its existing
Mac-compatible octave range 0–8; this batch does not expand that wire contract.

### Near-cursor effect chooser

**Choose effect near cursor** (597), the pattern context menu, and local F4 in
the pattern grid open a native searchable list beside the edited cell. Code,
name, description and source-format equivalents are searchable. Arrow keys
choose results, Enter or double-click opens their typed values in the existing
Pattern FX inspector, and Escape returns to the prior focus. Choosing does not
write music: the inspector retains the chosen command until Apply or explicit
discard. Its existing `pattern.effect.set`/binding, Undo and persistence paths
remain authoritative.

The chooser captures document/revision, cursor, stable pattern/column identities,
FX/nudge draft generation and selected plugin parameter context. A changed
target cannot silently receive a stale choice; **Use current cursor** explicitly
captures it and preserves search. An existing dirty FX or nudge editor is raised
instead of overwritten. Empty searches and dismissal never mutate the song.
The window is resizable, constrained to its monitor work area, and retired on
document replacement. Read-only search does not become an unsaved musical draft.
`workspace.get.effectPicker` exposes `visible`, `captured`, `search`, `matches`,
`selected` (catalogue index) and `current` while the owner exists.
