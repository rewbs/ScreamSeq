# Document operation layer (not exposed in the app)

`windows/Session/DocumentOperations.hpp/.cpp` owns wire validation and delegates
musical changes to `Tracker::Document`. It does not add another document model,
history, renderer, project codec, or plugin history.

## Integration contract

```cpp
namespace ScreamSeq {
using Json = nlohmann::json;
DocumentOperations(Tracker::Document &,
  std::function<void()> stopPlayback = {},
  std::function<void(const std::vector<Tracker::Edit>&)> publishEdits = {});
Json invoke(const std::string &method, const Json &params);
static std::vector<std::string> reads();
static std::vector<std::string> writes();
}
```

The constructor and methods above are members of `ScreamSeq::DocumentOperations`.
The host must validate and REMOVE `expectedRevision` before calling `invoke` on
the document control thread. Return values are Mac `result.data`, not JSON-RPC
or revision envelopes. Validation errors use `ScreamSeq::Api::ApiError(-32602)`;
unknown methods use `-32601`. Other core failures propagate for host mapping to
`-32003`. Recreate the operation object whenever loading replaces its Document.

The host revision must include document identity, Document revision and selected
sequence (and plugin revision when hosting exists). `sequence.select` changes
selection without changing Document revision/history, matching Mac navigation.
The core `Document::edit` replaces supplied `Edit::before` with the current cell;
it is NOT an optimistic per-cell guard. Stale request rejection belongs to the
host. An API cell patch cannot contain a `before` field.

Callbacks must not throw or mutate the Document. Cell publication happens after
committed edits, including cell Undo/Redo. The host should enqueue the batch, and
stop playback on queue overflow without claiming the committed edit failed.
Structural operations validate using a disposable real Document before invoking
the stop callback. Empty callbacks are appropriate only for offline use.
Native-only Undo/Redo also stops when metadata changes, because this interface
has no live column-mute/native-metadata publication hook.

## Implemented methods

Reads:
- `pattern.commands {}`: shared format-specific `patternCommands` catalog.
- `sample.get {sample}`: exact Mac metadata field set, no waveform calculation.
- `sample.waveform.get {sample,start?,end?,bins?,channels?}`: shared cached
  normalized min/max pairs, exclusive end, both/left/right, 1..16384 bins.
  `waveform.get` is deliberately not an alias: it is not the Mac method.

Writes:
- `pattern.apply {cells,dryRun?}`: 1..4096 distinct partial cell patches, one
  document Undo, format-aware shared validation, complete before/after changes.
- `history.undo/redo {domain:"document"}`: shared document history only.
  `domain:"plugins"` rejects explicitly.
- `document.patch {title?,tempo?,speed?,channels?}`: transaction, fractional tempo,
  UTF-8 validation/UTF-16 length limit matching NSString, core charset conversion.
- `pattern.create {rows,source?}`: shared create/duplicate, stable metadata clone,
  appends an order; returns `{pattern:index}`.
- `order.edit {order,operation,pattern?}`: shared before/after/assign/up/down/remove.
- `sequence.select {sequence}`: shared sequence selection, no new history entry.

Only `pattern.apply` accepts `dryRun`, as in the current Mac contract. Empty
patches, unchanged assignments/sequence selections, cell no-ops, dry runs and
empty history retain revision/history/transport. Unknown keys, booleans used as
numbers, fractional integers, invalid UTF-8/NUL and out-of-range values reject
before mutations or stops. Integral JSON floating values are accepted like Mac's
NSNumber validation. Even ignored optional `order.edit.pattern` values are type
checked, including for remove/up/down.

## Standalone ARM64 test build

Existing matching Release libraries are imported from
`bin/windows-workspace/Release`; override `SCREAMSEQ_ENGINE_LIB_DIR` if needed.
No root/application CMake files are changed. MSVC receives the core definitions
from `windows/CMakeLists.txt` and an 8 MiB stack reserve.

Git Bash, from the repository:

```sh
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
CTEST='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
"$CMAKE" -S windows/Tests/Editing -B bin/windows-editing-tests -G 'Visual Studio 17 2022' -A ARM64
"$CMAKE" --build bin/windows-editing-tests --config Release
"$CTEST" --test-dir bin/windows-editing-tests -C Release --output-on-failure -V
./bin/windows-editing-tests/Release/document-operations-tests.exe --snapshot-probe
```

The ordinary suite has 11 named groups against actual heap-owned Documents and
real PCM. Coverage includes batch atomicity, duplicate/unknown fields, Undo/Redo,
no-op/dry-run retention, all five editable formats' note/effect catalogs and
limits, fractional timing, short Unicode titles, UTF-16 bounds, native identities,
pattern-slot holes/full capacity, all order operations, sequence-specific tempo,
sample loop metadata, stereo/empty-range waveform queries, 4096-cell batches and
native automation-capacity rejection before stopping.

## Integration blockers / limits

Do not equate this suite with complete persistence or full API qualification.
No app, socket, renderer or audio device is started, and the app advertises none
of these methods yet. Plugin-host bus/processor-capacity checks still belong to
the future host integration; this layer cannot inspect hosted plugin instances.

`--snapshot-probe` now strictly asserts the shared snapshot fixes, with and
without DocumentOperations: the 100-byte MPT title remains 100 bytes after
Undo/Redo, the IT pattern hole stays unallocated, and actual native metadata
validates after Undo. The separate `../Snapshot` suite first reproduced both
failures and qualifies the shared RSCORE1 sample-archive correction across
snapshot/history/native persistence. See its README for wire format and limits.

Structural preflight now uses the restored shared snapshot directly. The manual
pattern/order/channel copy workaround has been removed; it cannot hide snapshot
loss or patch only a validation copy. Rebuild with the updated shared source
before running the probe. This does not itself enable application methods.
