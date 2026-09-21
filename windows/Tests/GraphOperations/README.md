# GraphOperations: document layer, not a host implementation

`windows/Session/GraphOperations.hpp/.cpp` implements the document-only graph
API against actual `Tracker::Document` / `NativeSong`. No DSP, UI, audio device,
plugin instantiation, or alternate song model is added. Mac authorities are
`mac/Bridge/SignalGraphAPI.inc`, `SignalGraphMetadata.inc`, and the API schema.

## Integration contract

```cpp
ScreamSeq::GraphOperations operations(document, stopPlayback, graphHostHooks);
auto data = operations.invoke(method, paramsWithoutExpectedRevision);
```

- Serialize calls on the document/control owner, never the audio callback.
- Caller must validate **and remove** `expectedRevision`, build the outer Mac
  `result` envelope, handle request deduplication, and own transport state.
  This class returns `result.data` only. Unknown methods throw
  `ScreamSeq::Api::ApiError(-32601, ...)`; invalid requests throw `-32602`.
- `stopPlayback` must not throw or mutate Document. It runs after complete
  candidate validation and before `Document::annotate`, only for actual mixer
  or graph-processing changes. Label/number/position and lane-display edits do
  not stop; errors, dry runs, and no-ops do not stop or alter history/revision.
- Recreate the operations object if loading replaces its referenced Document.
- Link `GraphOperations.cpp` and the existing `NativeMetadata.cpp` implementation
  (normally through the project library), plus the existing editor/core libs.
  Do not compile a second copy of NativeMetadata into the same executable.
- Use the shared document history. `undoChangesMixer` / `redoChangesMixer`
  already distinguish processing changes from graph labels/layout. The caller
  owns history transport policy; this layer does not dispatch `history.*`.
  Shared Undo deliberately retains the `nextID` high-water mark.

The implementation copies a candidate NativeSong, applies the operation, calls
shared `reconcileEnvelopeLinks` and `NativeSong::validate` (including graph DAG
and mixer routing validation), and commits one annotation. Complex JSON fields
reuse the existing complete metadata-14 codec with strict recursive API-key
validation in front; the persistence codec itself is unchanged. Omitted existing
recipe state is retained. Temporarily removing links only during codec parsing
allows the shared reconciler to prune deleted targets; restored links still
reject writes through a linked envelope. Clone allocates graph/node IDs through
`next.makeEntity` and remaps wires and bank uses.

Replacing supplied routes leaves the opposite route list unchanged. Commands
replace only one stable pattern's list. Automation replaces only one source's
curve for one stable pattern. Identical assignments and command replacements
retain collection order, history, and redo rather than creating reorder no-ops.
Commands use **65536 units/row**, automation **256 units/row**; rows/beat come
from the actual pattern override or song default.

## Truthful catalog

`GraphOperations::reads()`:

- `graph.get` (including `includeState`)
- `graph.automation.get`

`GraphOperations::writes()` (all accept `dryRun`):

- `graph.create`, `graph.clone`, `graph.update`, `graph.remove`
- `graph.node.add`, `graph.node.remove`
- `graph.assign`, `graph.instrument.assign`
- `graph.routes.set`, `graph.layout.set`, `graph.commands.set`
- `graph.automation.set`

`graph.node.add` supports independent plugin recipes without a host. Its `slot`
form requires the real baseline-state hook below; absent that hook it explicitly
rejects the slot and asks for a recipe. This is not plugin discovery/hosting.

**Not implemented or advertised:** `graph.controller`, `graph.plugin.get`,
`graph.plugin.set`, `graph.plugin.editor.open/commit/close`. These require real
prepared processors, isolated plugin instances, or UI ownership, not stubs.

## Exact host hooks (declared in GraphOperations.hpp)

`ScreamSeq::GraphHostHooks` contains:

- `std::function<std::vector<GraphRackRecord>()> rack`
- `std::vector<GraphRackRecord> cachedRack`
- `std::function<std::vector<Tracker::SignalActivity>()> activity`
- `std::vector<Tracker::SignalActivity> cachedActivity`
- `std::function<GraphRackClone(uint32_t)> cloneRackSlot`

Callbacks supersede their respective caches. Default empty caches describe an
actual **offline empty rack / no playback** fixture; an application with plugins
or playback must supply its real records, not rely on those defaults. Callbacks
must be read-only with respect to Document and reflect the same serialized host
state. Typed `GraphRackRecord` carries the actual descriptor dictionary, instance
`id`, zero-based `slot`, `bypass`, and assigned tracker `instruments`. Those
assignments are used to reject sample-voice graph assignment to plugin instruments.

`GraphRackClone` carries the complete `Tracker::GraphPluginRecipe`, an
`instrument` descriptor flag, and assigned `instruments`. The clone callback
must return the requested real slot's **baseline opaque state and enabled
auxiliary inputs/outputs**, not a transient automated playback state, and throw
`ApiError` for absent/unavailable slots. Instrument descriptors, assigned
instruments, and AU music-device type are rejected. No real application hook has
been connected by this component; tests exercise explicit in-memory fixtures.

## Reproduce on the configured Windows ARM64 toolchain

From the checkout root in Git Bash (native paths, not `/c/...`):

```sh
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
TOOLS='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
"$TOOLS/cmake.exe" -S windows/Tests/GraphOperations -B bin/windows-graph-operations -A ARM64
"$TOOLS/cmake.exe" --build bin/windows-graph-operations --config Release --parallel 3
"$TOOLS/ctest.exe" --test-dir bin/windows-graph-operations -C Release --output-on-failure
bin/windows-graph-operations/Release/graph-operations-tests.exe --catalog
```

Standalone CMake imports matching `bin/windows-editor/Release` core libraries;
set `SCREAMSEQ_ENGINE_LIB_DIR` for another matching build. It compiles the actual
Document consumer, GraphOperations, and NativeMetadata, uses the repository's
nlohmann header (manifest: 3.12.0 plus its recorded upstream patch), and reserves
an 8 MiB test stack. Documents are heap-owned. Checks throw in Release/NDEBUG;
no assert-only tests, GUI, audio device, package install, or root-CMake edit.

Verified: **8/8 CTest scenario groups passed** in ARM64 MSVC Release. Initial
missing-method tests failed before their implementations; identity-exhaustion
regression failed with the shared runtime error before adding API range guarding.

- Create/get/dry-run and actual shared Undo/Redo with monotonic identities.
- Create/connect/modulate, implicit feedback rejection, clone ID/wire remapping,
  omitted opaque state, kind/identity replacement rejection, node/graph deletion.
- Per-pattern curves, scripted formula validation, beat signatures, fractional-row
  positions, exact row bounds, linked-use rejection, clone/pruning of bank links.
- Assignments, instrument stable IDs, mixer/aux-route validation, unrelated route
  preservation, lane/layout behavior, per-pattern command replacement and no-ops.
- Hook fixture data, baseline opaque bytes/aux ports, sample-vs-plugin assignment
  rejection, unavailable host methods, strict types/unknown fields/IDs/base64.
- All write dry runs preserve revision/history/redo; no-op edits preserve redo.
- Callback ordering: validated changes stop before annotation, while failed and
  cosmetic edits do not; unrelated cells and native annotations survive.
- Metadata-14 encode/decode plus real snapshot-backed Document restoration.

The existing shared `signal-graph-tests.exe` also passed its DAG, audio-routing,
delay, multiport, modulation, and macro tests; that is separate shared-engine
functional evidence, not a new host or hardware qualification.

Pending outside this component: app/session routing and advertised schema subset,
real host hook wiring, prepared renderer publication, plugin/controller/editor
methods, actual NativeProject container integration/save/reopen and Mac-app
interchange. The metadata/model roundtrip here does not claim those integrations.
