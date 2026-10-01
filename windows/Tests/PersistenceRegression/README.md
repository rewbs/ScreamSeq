# Native persistence regressions

## Behavior and interfaces

`NativeProject.cpp` now promotes a changed historical metadata tree in two
steps: canonicalize **all** known pre-edit branches against the decoded v14
baseline, retaining dictionary extensions and keyed array ownership; then apply
the ordinary identity-aware three-way edit. This includes unchanged mixer,
automation, performance and signal branches, not just `prePan`. The promotion
walk distinguishes decoder-sorted maps (including numeric performance binding
IDs, graph lanes and layout keys) from order-preserving vectors. Identity
selection uses the known schema, so opaque fields named `id` cannot shadow a
sequence's real `info.id` or masquerade as vector-element identities.
Unknown-bearing ambiguous edited arrays still fail closed. Unedited metadata, including a
cell-only edit, keeps its original historical typed tree.

Before any file staging/replacement, the final merged metadata is decoded,
compared to the intended model (including canonical scalar types/float bits),
and restored through `Document::restoreNative` against a heap-owned Document
constructed from the **exact output snapshot** and selected sequence. Invalid
metadata, a different intended model, or a mismatched snapshot cannot publish.
No changes were needed in NativeMetadata or BinaryPlist.

`ProjectState` adds `std::optional<RecoveryOrigin> recoveryOrigin`, containing
`revision` and `sequence`. Loading a retained take establishes its origin;
saving never changes that origin. Serialization may change `compatible` from
true to false when revision/sequence no longer match, but never the reverse.
Counters, events and unknown take/event fields are retained. Undo changes the
Document revision and cannot revive a stale take. Failed publication leaves
ProjectState and the destination unchanged.

### Hooks required by future recording/plugin owners

- On a **real new capture**, replace the take and assign its `RecoveryOrigin`
  from the Document revision and selected sequence at **capture start**, not
  `savedRevision` or the revision at stop/save. Captures may start after unsaved
  edits. An absent origin cannot authorize compatibility.
- Call `invalidateRecoveryTake(state)` for musical changes outside
  `Document::revision`: future rack/plugin mutations, external musical state,
  and sequence changes that might be switched back before serialization.
  This only changes compatibility to false; it retains events and origin.
- A future recording commit/read API must check current origin as well as the
  stored compatibility flag. `preserved` is saved-source storage, not a live
  compatibility view. Use the same revision/sequence guard; do not rebase an old
  take when the saved baseline changes.
- Actual recording, plugin operations, and their app integration are outside
  this patch. Mac source inspection (`TrackerSession.mm` recovered revision
  setup; `RecordingAPI.inc` commit/replaceRows) motivates the compatibility
  contract. No Mac commit/reopen execution is claimed.

## Reproduce

From the Windows checkout in Git Bash, use a **new** scratch output directory:

```bash
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
python -B windows/Tests/PersistenceRegression/run_tests.py \
  --output "$TMPDIR/persistence-qualification-new" \
  --reference '<read-only actual Mac reference.screamseq>'
```

The runner validates the pinned reference SHA-256, copies it for tests, verifies
native Windows ARM64 and the executable PE machine, builds with MSVC and an
8 MiB test stack, and records commands, logs, source/library/executable hashes
and environment values. It compiles current Project sources plus
TrackerDocument/SampleArchive freshly; remaining dependencies come from the
explicit, hash-recorded `bin/windows-snapshot-fix/Release` libraries. This is not
a claim of rebuilding every shared engine/import source. Documents stay on the
heap. MSBuild emits MSB8029 because the intentionally isolated build directory
is under the mandated scratch TEMP directory.

Historical versions 1–13 are **generated**, nonempty reductions of the rich
model in `ProjectNative/NativeMetadataTests.cpp`, not Mac-produced historical
fixtures. Coverage includes every feature gate, omitted nested defaults,
reordered decoder-sorted maps, opaque extensions in every dictionary, stable
entity reorder, exact unedited trees, cell-only edits, promotion, second save,
final validation rejection and ambiguous-array rejection. The separate actual
Mac v14 path compares the entire typed tree with independent Python plistlib,
including boolean/integer distinctions and floating-point bits, checks opaque
sections, and rechecks original/copy hashes. Existing metadata/plist,
preservation, native snapshot+PCM/edit/Undo and atomic I/O tests also run.

## Executed evidence

Evidence outside Git: Hermes scratch `persistence-fix/`.

- `red-results.json`: initial promotion failed after successful save with
  `Missing required metadata field`; initial recovery regression failed because
  edited/reopened compatibility remained true.
- `first-validation.json`: final-tree guard failed before implementation:
  valid-but-wrong preserved model was published.
- `unknown-id-red.json` and `shadow-id-red.json`: additional failing checks for
  opaque `id` fields confusing promotion identity or being lost during a
  sequence edit; known-schema identity selection fixes both.
- `final-qa2/test-results.json` and individual logs: native ARM64 build and all
  invocations passed. Historical promotion: 10,526 checks, versions 1–13.
  Recovery lifecycle: 78 checks (unchanged/no-op, edit→save→save, Undo,
  failed save, already false, sequence, capture after unsaved edit, missing
  origin, external invalidation). Validation: 14 checks. Existing CTest 3/3.
  Actual Mac metadata: 2,111 checks; actual Mac full-tree, native-project and
  atomic I/O tests passed. Source/reference hashes remained unchanged.

No UI, audio, global state, clipboard, network, commits, or external plugin
execution was used. Test success does not qualify Mac recording commit, audible
playback, missing-plugin resolution, or unrelated concurrent owner changes.
