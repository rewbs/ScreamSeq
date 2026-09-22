# Windows local API subset

This directory provides a private transport and a control-thread session adapter,
not full macOS API parity. The attached host determines document-operation support;
query the running instance's `api.describe` for its current method catalog. Do not
infer support from the standalone protocol fixture or the Mac schema. Navigation
and inspectors share GUI/API paths (see **Workspace subset** below).

The application now implements Mac-compatible `automation.get` and
`automation.replaceLane` for absolute song parameter points. Timestamps are
48 kHz frames, values are native parameter units, and replacement uses plugin
Undo. The native Song automation window opens from the command palette or the
pattern-curve editor. `workspace.get.absoluteAutomation` reports captured target,
revision, draft, viewport and bounded draw geometry; its point preview truncates
at 4096 while `pointCount` reports the complete lane. Use paginated
`automation.get` to read every point. See `../ABSOLUTE_AUTOMATION_PROGRESS.md`.

`workspace.get.sampleDetail` exposes the detailed sample editor's captured
identity/revision, draft points, selection, viewport, channels, bounded waveform
cache and latest operation report. Its native controls call the existing
`sample.*` methods with the same guarded document history and PCM semantics.
The dock's Detail button and command palette open this window. See
`../SAMPLE_DETAIL_PROGRESS.md`.

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
Parameter/port writes validate the whole candidate before stopping playback.
Editor open returns a token; commit requires that token, captured graph/node
and unchanged recipe. Closing the native window retains its uncommitted draft
until explicit API close or document replacement. Commit supports dry run.
`workspace.get.graphEditor` exposes the reusable canvas's captured revision,
draft flags, selection and retained hit-test geometry. The contextual workspace
panel API is unchanged.

The Graph dock's Pattern curve page uses `graph.automation.get/set` and
`automation.formula.preview`. `workspace.get.graphCurve` reports its captured
graph/source/pattern/revision, retained point fields, selection, viewport and
preview status. Values use the shared 256 units per row and normalized 0..1
model; native fields display rows and percent. All nine curve types and scripted
expressions use the shared evaluator. Formula previews run on the worker and
painting consumes cached samples. Song overview remains outstanding.

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

## Workspace subset

`context.set` uses the existing shared schema fields `expectedRevision`,
`expectedContext`, and at least one of `pattern`, `row`, `channel`, `column`,
`following`. Read both tokens from one `context.get`. Booleans are not integers;
validate the complete request before moving. Current Windows columns are 0–4
(note, sample/instrument, volume, effect, parameter). Extra effect lanes are not
implemented. Changing pattern defaults Follow off. A no-op retains the context
token. Navigation/selection never change song revision, Undo or transport.
`following` is the canonical Mac field; `follow` remains a read-only legacy alias.
Selection bounds are inclusive, matching Mac, and are part of the context token.

`workspace.get` reports retained `notes`/`samples` panels, `locations`, `right`, `visible`,
`pins`, `targets`, `focus`, and `focusLayout`. Windows extensions include `layout`,
structured `inspection`, `returnPoints`, DIP `geometry`, `dpi`, and `viewport`.
These are actual GUI state, not a second musical model.

`workspace.panel` accepts the existing schema: `panel`, `pinned`, `focus`,
`follow`, `return`, `placement`. Only `notes`/`samples` and `right`/`hide`
placement are supported; other placements/panels reject atomically with -32602.
Placement alone does not select a different panel or steal focus; `focus:true`
explicitly selects and opens it. Each panel retains its own `right`/`hide`
location, including inactive panels. Hiding the selected panel selects the other
only if it is placed at `right`, and returns hidden-panel focus to the pattern.
If both panels are hidden, `right` is `""` and `visible` is empty. Presets/open
actions can reopen their selected panel without resetting the other's location.
`pinned:false` immediately resumes following the current edit cursor without
replacing the original return point. `follow:true` is the Cursor action: unpin
and inspect the edit cursor. Return
moves to that panel's original opening position with playback-follow off, then
focuses the pattern. Hidden/tabbed panels keep independent targets and pins.
Read-only inspectors follow cursor changes without taking focus; there are no
editable drafts yet. Workspace operations, like Mac, do not require song/context
tokens; supplying either token rejects as an unknown parameter. They cannot
edit music. Agent focus requests change in-app focus only.

`api.describe.revisionGuards` advertises required tokens per write method:
`transport.play`/`transport.stop` require `expectedRevision`, `context.set`
requires both `expectedRevision` and `expectedContext`, and workspace writes
accept neither. `api.describe.workspaceSubset` lists the supported panels,
placements and presets rather than advertising arbitrary Mac docking/layouts.

`workspace.layout` supports **Compose**, **Pattern focus**, **Sound design**.
Save/Restore custom, arbitrary docking and floating explicitly reject rather
than succeeding as no-ops. Layout sizes and panel state are session-local.
Keyboard and native button actions call the same host operations. API calls do
not raise the process or change the system audio route.

Reproduce with `SCREAMSEQ_TEST_EXE` set to the separate QA executable:
`python -B windows/Tests/test_workspace.py -v`. Tests exercise real HWND buttons,
command-palette text/Enter, drag selection, divider resize, pins/return, strict
validation and stale guards through the actual per-process pipe. Mouse-message
tests are DPI-aware; computer-use inspection is a separate visual check.

## Wire and security

The base adapter catalog includes `api.describe`, `document.get`, `pattern.get`,
`context.get`, `transport.get`, `transport.play`, `transport.stop`, `context.set`,
`workspace.get`, `workspace.panel` and `workspace.layout`. Attached document hosts
can enable additional operations; use the live catalog, not a hard-coded superset.
Unsupported methods return `-32601`. The adapter uses the existing
string-ID JSON-RPC envelope, revision guard (-32001), parameter errors (-32602),
busy (-32002), engine errors (-32003), document result envelope and transport
response dictionaries. Unknown envelope keys/batches/notifications are -32600.
Unbound adapters only describe capabilities; song requests return busy rather
than fabricated state.

### Bounded successful-write replay

`api.describe.result.data.writeReplayCache` reports the policy and aggregate
occupancy (`retainedEntries`, `retainedSerializedBytes`), never cached IDs,
parameters, paths or response contents. Per SessionAdapter, retention is limited
to **64 successful writes and 8 MiB (8,388,608 bytes)**, whichever binds first.
The byte charge is precisely `request.dump().size() + response.dump().size()`:
compact, sorted-object-key UTF-8 JSON envelopes, without newline delimiters or
original request whitespace. It is **not an 8 MiB heap/RSS guarantee**: retained
response JSON nodes, string capacities, ID copies, containers and allocator
overhead cost additional memory. Temporary snapshots, serialization buffers and
in-flight responses are outside this retention accounting.

Only successful write-method results enter the cache, including successful
no-ops and `dryRun:true` previews. Validation/host errors and all reads remain
uncached. A retained request's ID, method and parameters must match its canonical
parsed JSON exactly; key order/whitespace are irrelevant, but an integer changed
to a floating-point parameter is different. An exact retry returns the original
complete response without invoking the host or rechecking now-stale tokens.
Reusing that ID for a different write returns `-32600` and preserves the original
entry. Applying a dry-run proposal requires a new ID and current revision.

Successful insertions evict the oldest entries until both limits hold; a replay
does not refresh an entry's age. An entry whose charge alone exceeds 8 MiB is
not retained and does not evict older entries. Retention is best effort: size,
serialization or allocation failure must not convert an already completed write
into a rejection with the pre-write revision. The original success is returned
even when it cannot be cached. A host result that cannot serialize still reaches
the transport's existing bounded `-32003` fallback (currently with null ID), not
a claim that the write was unchanged; inspect state to determine the outcome.

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
its target, selected event, count, pending/stale status and logical canvas bounds.

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
