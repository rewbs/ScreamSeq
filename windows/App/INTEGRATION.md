# Windows document-worker application integration

This is an editing integration, not a full Mac-parity release. The preserved
`bin/windows-checkpoints/workspace-preview/ScreamSeq.exe` remains a historical
artifact. Current upstream scope and qualification are documented in
`../PARITY_PLAN.md` and `../UPSTREAM_PLUGIN_QUALIFICATION.md`.

The current native format is container 6 / metadata 17. Historical native
wrappers reject before document replacement, matching upstream; module import
remains supported. Unified FX, note cuts and disconnected routes have shared
API/rendering and native editing controls. The reusable graph canvas is covered
in `../GRAPH_EDITOR_PROGRESS.md`; graph pattern curves and the shared modulation
sampling fix are covered in `../GRAPH_CURVES_PROGRESS.md`. The modeless envelope
bank and its two reuse levels are covered in `../ENVELOPE_BANK_PROGRESS.md`.
The modeless formula workbench and reference are covered in
`../FORMULA_WORKBENCH_PROGRESS.md`.
Native shared-plugin instrument assignments and MIDI channels are covered in
`../PLUGIN_ALIASES_PROGRESS.md`.
Song overview remains pending.

## Owner boundaries

`Session/DocumentController` constructs, owns and disposes the shared Document on
one serial worker. Native/module opening, complete validation, snapshot/cache
creation, serialization, atomic saving and renderer preparation run there. It
publishes `shared_ptr<const DocumentView>`; Main, drawing and input never read the
live Document. The only App snapshotData call outside this worker is the separate
non-GUI `--offline-test` harness.

Public interface:

- Constructor: `(inputPath, sessionIdentity, stopCallback, committedEditsCallback)`.
- `view()` returns the immutable current cache.
- `invoke(method, params)` returns `future<Json>`. Write params must still contain
  expectedRevision: it is checked against the worker's actual document revision
  and selected sequence before stripping it for the operation implementation.
- `prepare(rate, settings, loop, offline=false)` returns a borrowed
  `HostedProjectPlayback*` in a future. The worker owns, replaces and disposes it.
  The UI stops/joins the device and drops its playback/telemetry pointers before
  requesting another preparation. Worker shutdown disposes playback first.
- `service()` runs pending playback hooks on the UI owner, never on the worker.
- `refreshPlaybackLatencies()` rebuilds plugin/mixer/graph compensation on the
  worker, after the caller joins WASAPI. Plugin reactivation runs on its private
  STA. Main retains the renderer/position and honours Stop during the wait.

Main's guarded `await()` services hooks and native messages/drawing while work
runs. Reentrant document mutations reject with busy rather than queueing edits
against stale snapshots. Stop and navigation remain usable. Stop during renderer
preparation cancels the subsequent start. Device shutdown precedes renderer
replacement/disposal. Committed cell batches enqueue on the prepared renderer;
queue overflow stops playback without claiming that the document commit failed.

`NativeToolWindow` owns native child controls and a separate Direct2D surface for
modeless editors. Its input stays local to that owned window; painting consumes
cached geometry. `EnvelopeBankWindow` receives guarded request/context callbacks
from the source curve editor. The document worker retains all validation,
catalogue I/O, history and project ownership. The bank captures source identity
and draft generation independently of its own template draft, so navigation,
new text and stale external edits cannot redirect a completion. F6 switches
between the bank canvas and its native controls; Ctrl+S saves its song master.

`FormulaWorkbenchWindow` uses the system Rich Edit control with local multilevel
Undo, bounded text/snippet insertion and multiline selection-aware completion.
Ctrl+Space is local completion; Ctrl+Enter uses a validated draft; F6 moves between
code and reference. Preview requests capture complete parameters and the text
generation, run on the document worker and publish only to that generation.
Use rechecks the parent document, point, selection and draft generation, and
returns only a local point edit. Parent Apply/Save creates the document transaction.
Closing retains text; reopening raises the same draft, including a maximized
window. The independent reference window can open without an envelope target.
Rich Edit paint/focus/scroll notifications do not trigger layout or font resets;
font updates occur on window size/DPI layout, avoiding a timer-starving repaint
loop. Drawing uses retained preview samples, with no parser or worker call.

Document identity changes only after a candidate opens successfully. Structural
operations stop playback only after operation-layer validation. Failed opens and
failed saves retain the previous path, baseline and document. GUI Open/close
protect dirty work with Save/Discard/Cancel; stale modal decisions reject.

## Review fixes: atomic, Unicode-safe, bounded views

Open builds the complete candidate `DocumentView`, checks its aggregate cache
budget and validates JSON text before stopping playback or replacing anything.
The entire `ProjectState` moves with its Document and generation (checked
no-throw swap); conversion/allocation failure keeps the old dirty model, path,
cursor and revision usable. Opaque AU class IDs retain their JSON type.
Title/instrument names are transcoded from `GetCharsetInternal()` only at the
view boundary; sequence names use Unicode-to-UTF-8, not JSON's wide-string array
conversion. No song bytes/charset are normalized for display.

Ordinary no-op/dry-run writes retain the exact view object. Patterns contain one
six-byte wire cell each; immutable unchanged pattern and 256-bin waveform
buffers are shared across edits. A one-cell edit checks/rebuilds its affected
pattern, not every project cell. Structural/history changes compare cells
explicitly, including otherwise-unused volume/parameter bytes. Display strings
are formatted only for the visible grid; there is no per-project string array.
Asset PCM mutations invalidate the edited sample's waveform. Imports, replacement
and history refresh all waveforms; unrelated pattern edits retain them.

The default cache budget is **64 MiB per complete view**, conservatively charging
all patterns, waveforms, maps, JSON catalog copies, instrument keyboards and text.
It is not a process working-set/OOM guarantee: shared engine/snapshot/history,
temporary serializers, a candidate plus the previous view, API response copies
and externally retained snapshots are separate. Growth operations reserve
conservative metadata/path headroom before mutation. Over-budget opens fail
without touching the current session. The worker retains retired views until
UI references are released, so final large-cache disposal runs off the UI thread.
Consumers must release views before destroying their controller.

A transient postcommit publication failure is retried before successful return.
Persistent failures explicitly say the operation **committed** and mark publication
pending; the next app snapshot read retries on the worker before offering a
revision guard. Completion-hook exceptions after a commit follow the same
recovery path. This prevents a permanently stale cache/guard after recoverable
failure; genuine sustained allocation exhaustion can still make reads fail.
Never infer an unchanged document from a failed completion or replay it blindly.

Native chooser/list selection notifications keep HWND focus. Only explicit
commands return to the pattern. Sample focus-gained/lost notifications are not
selection commands (otherwise Escape would immediately re-select the inspector).
The regression runs on a never-switched private desktop, sends only targeted
HWND messages, and checks `GetGUIThreadInfo`, foreground and clipboard sequence.

## Registered operation layers

DocumentOperations: `pattern.commands`, `sample.get`, `sample.waveform.get`,
`pattern.apply`, `history.undo`, `history.redo`, `document.patch`, `pattern.create`,
`order.edit`, `sequence.select`.

TimelineOperations: `document.timing.get/set`,
`automation.formula.reference/preview`.

PatternOperations: `pattern.effects.get/set`, `pattern.performance.get/set`,
`pattern.effect.set`, current `pattern.notes.get/set`, `pattern.transform` and `pattern.paste`.
The grid, FX inspector and precise-note dock use these guarded transactions. Sparse immutable FX/
note caches are charged before commit and reused across unrelated edits. See
`../PATTERN_FX_PROGRESS.md` for the earlier checkpoint and
`../PATTERN_NOTES_PROGRESS.md` for clipboard and precise-note editor evidence.

GraphOperations, MixerOperations and EnvelopeOperations are now dispatched by
the actual app and advertised by `api.describe`. Graphs resolve real rack IDs,
saved recipes, assignments and prepared playback activity. Candidate mixer bus
counts are checked against plugin adapter capacity. Envelope targets use real
plugin parameter/conflict hooks; inspection catalogue storage is explicitly
private. See `../MIXER_GRAPH_PROGRESS.md` for current evidence and limits.

The native mixer dock has a captured, revision-guarded bus draft, compact numeric
controls, main-output selection (including disconnect), mute/solo, group creation
and removal, reload, Apply, keyboard access and stereo meters. Unfinished fields
survive navigation and stale revisions; async completion checks draft generation.
It reuses the same API operations as external clients. Painting reads retained
UI state, with meter snapshots collected by a UI timer. The reusable graph dock
adds cached nodes, ports and wires, socket dragging, pan/zoom, definition/property
drafts and bus assignments. Its plugin controls and native VST3 draft editors
operate on independent graph recipes with document Undo. Painting never queries
the worker or plugins. The Pattern curve page retains a separate captured source
and pattern draft, with native point fields, dragging, snap, zoom, all nine curve
types and worker-evaluated formula previews. Generation checks preserve newer
edits during requests. Song overview, expanded formula/bank editors,
insert/send/sidechain controls and simultaneous independent lower docks remain.

Mixer gain/balance/width/mute/solo updates publish one bounded control batch on
the single UI producer. The shared Document prepares Undo storage before that
publication and rolls it back if the queue refuses. Successful previews are
transient; a no-op bus write restores saved controls without creating history.
Validated topology changes stop/join playback before committing. This is not
live structural graph replacement.

The precise-note dock keeps a captured row draft with row/beat offsets, snap,
velocity, note-local effects, off/cut, duplicates and retriggers. Check validates;
Apply merges untouched pattern events and creates one document Undo step. A stale
draft stays visible and cannot overwrite newer music. Use target explicitly
reloads the notes inspector's pinned/follow target. In-flight completions preserve
newer fields and navigation focus. The hit list is virtual; dense canvas rows
draw 256 cached bins plus the selected event. Sparse grid lookup uses binary
search and displays a precise-hit marker. Delete on a note with precise hits
clears that row's note events and ordinary note fields together, preserving FX.

Pattern clipboard text matches Mac's Pattern 2 payload and legacy Pattern 1
import. Stable parameter bindings remap by target, and pasted FX expand the
destination's column count atomically. The native clipboard uses Unicode text;
inspection uses private text and never changes the desktop clipboard. Copy and
parse tasks use immutable inputs off the UI thread. Paste retains its captured
destination and revision even if the cursor moves while parsing.

Controller file operations: `document.save`, plus explicit Windows extension
`document.open`. Save requires absolute UTF-8 paths and `.screamseq`/`.resonance`,
explicit overwrite for existing files, and validates dry runs without publication
or baseline changes. Open requires expectedRevision and explicit discard when
unsaved. Successful-write request-ID deduplication and private PID pipe transport
remain in the existing adapter/dispatch layers.

AssetOperations is registered and retained on the worker, including imports,
PCM, processing, loop settings, drawing, private clipboard and instrument edits.
Import validation checks actual preserved plugin assignments, adapter capacity
and cache growth. Instrument replacement rejects plugin-owned slots, including
integer-valued JSON numbers such as 1.0. Failed validation precedes playback stop.
GraphOperations and EnvelopeOperations are registered app endpoints with real
rack, baseline, parameter and conflict hooks, as described above.

## Native UI

Open/Save/Save As and Undo/Redo buttons and palette actions use the same worker
operations as the pipe. Ordinary tracker note typing, instrument/effect parameter
hex fields, volume/effect catalog entry, Delete, rectangular copy/paste, Ctrl-Z/Y,
octave/step and pattern/order chooser controls are implemented. The sample
sidebar is a bounded native scrolling list, not one button per sample. Drawing
remains a virtual grid. Catalog cache revisions avoid resetting native selection
on every musical edit. Inspector pins/Return, keyboard focus and cursor/playback
state remain separate. Removed Return targets reject safely after Undo.

The sample editor draws bounded live voice cursors from shared atomic telemetry;
overlapping voices and sample loops use actual positions. Stopped transport
publishes `audioActive:false` and no `voicePositions`. Envelope cursors and
audition UI remain open work.

`--vst3-test-cache <absolute path>` selects an isolated registry for inspection,
offline-hosted or audio qualification mode. It does not scan automatically.
The native plugin rack provides discovery/rescan, add/remove/reorder/bypass,
editor windows, parameter drafts, instrument assignment and independent plugin
Undo/Redo. **New trigger** creates an empty tracker instrument and assigns it
through the shared guarded API, retaining a stable retry target if assignment
fails. Parameter batches and proven parameter-only native editor gestures reach
the playing chain atomically. Opaque state changes still stop playback, including
Surge's first editor open when it serializes its initial zoom. See
`../TRIGGER_INSTRUMENT_PROGRESS.md` for installed-instrument evidence and limits.
The rack detail selector also exposes guarded factory-program loading and
selective auxiliary-port activation. Program selections retain captured target
and revisions; port changes preserve unrelated ports. These are musical edits
with plugin Undo/Redo and native save/reopen, not mixer-routing assignments.
The **Presets** page and command palette expose native Save/Load dialogs for
Mac-compatible `.screamseq-preset` and legacy `.resonance-preset` files. Captured
document/revision/plugin identity and file content hashes guard asynchronous
selection. A sound load preserves aliases, ports, bypass, routing, automation
and plugin metadata and uses plugin Undo. Saving writes atomically without
changing the song. Both paths use the guarded API; see
`../PLUGIN_PRESETS_PROGRESS.md`. The bundled pugixml MIT notice is copied beside
the app under `THIRD-PARTY-NOTICES/` and must be included in packaged builds.
**Browse…** opens a retained native plugin library with search, kind/format/
category filters, favorites, hidden entries and custom categories. Rows use
separate name, format/kind and category columns. Typing filters cached rows;
Reload/Rescan explicitly refresh discovery and preferences. A disappearing
selected row is deselected, never silently retargeted. Hidden entries also leave
the rack's quick picker. Enter adds the selected row, F6 switches search/list,
Ctrl+F focuses search and Ctrl+R reloads. Category drafts survive Close; Escape
discards a draft. Unavailable preferences leave insertion usable. See
`../PLUGIN_LIBRARY_PROGRESS.md` for native/API and isolated live-audio evidence.

The Samples inspector opens a waveform editor in the lower dock. Dragging or
keyboard/range fields select audio; Reverse, Normalize, Fade, Trim and normal or
sustain loop controls use guarded worker operations. Both loops support enable,
disable and forward/ping-pong/reverse direction. In short inspectors these
controls remain available through the command palette. Copy/cut/paste use the song's sample clipboard.
Selections follow stable sample IDs. Drafts capture target and document revision;
an external edit or changed sample cannot silently retarget Apply. Escape cancels
a drag or field draft. New document identities clear selections and drafts.
Native controls remain keyboard focusable, with dark list/combo drawing and a
dark title bar. Drawing connects short samples as well as min/max bins.

DirectWrite layouts are retained in a 4096-entry cache. Stopped, unchanged views
exclude the ready swapchain from their wait set, avoiding an idle render loop.
Document reads with the same view do not invalidate the layout. Playback and
input still schedule frames, and worker waits respect swapchain readiness.

## Hosted playback integration

WASAPI now uses the worker-prepared shared renderer and PluginChain for ordinary
and hosted songs. The common render method splits requests above 4096 frames,
applies pending controls, publishes transport, renders, then processes the chain.
Processor failure silences the complete callback and stops playback on the UI
thread. No preparation, plugin state I/O or destruction occurs in that callback.
Unavailable AU or unresolved VST3 recipes fail preparation; there is no dry
substitution. The VST3 scanner and SDK notices are built beside the application.
Plugin discovery/editor/state-editing UI remains pending.

`--offline-hosted-test --project <copy> --report <json>` exercises the same
preparation/render path at three rates and four partitions without a device.
`--audio-test-silent --seconds <duration> --project <copy> --report <json>` runs
the actual device callback and mutes only after DSP; it changes no system volume
or route. Normal playback retains the explicitly labeled -20 dB monitor gain.

See `../RESUME_PROGRESS.md` for current source/build evidence and open gates.

Inspection disables audio, not editing. Its pattern clipboard is process-private
and explicitly labeled; final tests assert the desktop clipboard sequence is
unchanged. An earlier exploratory OLE clipboard-backup approach failed on
restoration and was removed; it is not evidence of reliable arbitrary-format
clipboard restoration. File dialogs suppress recent-document additions.

## Historical editor-review-fix verification

Built current shared source with:

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-editor-review-fix -Target ScreamSeq
powershell.exe -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-editor-review-fix -Target document-controller-tests
```

With SCREAMSEQ_TEST_EXE pointing to that Release/ScreamSeq.exe and
SCREAMSEQ_REFERENCE_PROJECT pointing to the supplied reference:

- `python -B windows/Tests/test_editor_app.py -v`: 20 passed, including the real
  worker failure-injection/cache tests. Worker fixtures are generated on demand.
- `python -B windows/Tests/test_workspace.py -v`: 16 passed.
- `test_app_api.py`, `test_native_project_app.py`, `test_app.py`: each passed.
- Python API client: 4 passed against the freshly built explicit test host.
- Fresh ARM64 `windows/Tests/Api` CTest: 2 passed (including SessionCacheTests).
- Fresh Editing and Timeline CTests: 1 passed each, linked against the current
  review-fix engine libraries, not checkpoint/imported stale libraries.

All fixture runs explicitly set TMPDIR/TEMP/TMP to the approved scratch root.
Clipboard sequence and original reference/checkpoint executable hashes stayed
unchanged. Review evidence is in the task scratch `editor-review-fix-evidence/`
(logs, runner, results, executable/source hashes); it is not a release package.
Regressions were observed red before their corresponding fixes: split worker/view
open, independent legacy text serialization, Unicode sequence arrays, native
focus loss (including Escape/list kill-focus), postcommit publication failure,
and large-project dry-run/no-op/reuse/budget checks. The normal large cache
fixture is 32 × 1024 × 64 cells; a 200-pattern fixture exercises actual-app budget
rejection while preserving the dirty source, cursor and usable worker. These are
functional checks, not 60-fps or whole-process memory qualification.

The editor suite copies the actual Mac fixture, edits cells, Undo/Redo, saves and
launches another app to read back changed cells and native counts. It also covers
native dialog Save As/Open/Cancel/Discard, MOD filename/format, stale/invalid and
no-op/dry-run guards, request-ID deduplication, failed atomic replacement with a
locked destination, clipboard rectangles, retained inspectors/list selection,
timeline save/reopen and 100-byte titles through structural Undo/save/reopen.
A real outstanding structural worker job is observed while Stop and Down are
handled in under the test's 500-ms bound. This is not a latency benchmark.

At that historical checkpoint, hosted playback was blocked and no audio
hardware was opened. Precise-note/curve/graph UI, VST3 frontend, MIDI,
recovery, accessibility and complete keyboard customization remain separate
work. No current Mac binary reopen or sustained presentation qualification is
claimed. All task-owned QA processes were closed.
