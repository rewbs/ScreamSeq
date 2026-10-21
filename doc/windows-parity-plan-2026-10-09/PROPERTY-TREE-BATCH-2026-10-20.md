# Shared typed project preservation foundation

**Later execution:** [B3 qualification](SHARED-CONTRACT-QUALIFICATION-2026-10-20.md)
passes the shared binary codec and preservation/metadata-owner tests on Windows
ARM64. Mac's bridge/session tests and reciprocal edited-file gates remain open.

## Status and observed ownership gap

**Foundation checkpoint `5f278e7c5`, unbuilt at this checkpoint.**
The subsequent [Mac session ownership source batch](MAC-PRESERVATION-BATCH-2026-10-20.md)
implements the integration described below; it also remains unqualified.
The following observations describe the pre-integration session.
Mac's current `TrackerSession.mm` constructs a fresh root in
`projectDataForRecovery:`. `decodeProject` and `ProjectLoadRecovery.inc` report
unknown root/native fields as ignored/lossy; `_loadRecoveryArchives` retains
only explicitly named recovery fragments. An unrelated native edit and save can
therefore drop a future field. The current codec corpus records that difference.

The Windows codec already has an exact typed representation, bounded parsing,
opaque values, stable-identity merge and ambiguous-edit refusal. Moving that
implementation provides a common foundation instead of inventing another
Foundation-only merge or using lossy textual JSON.

## Prepared shared/native boundary

- `editor/project/BinaryPlist.{hpp,cpp}` now owns the portable codec formerly
  under `windows/Project/`. Its executable logic is unchanged. It preserves
  binary data, signed integer values, large unsigned integers, floating bits,
  dates, UID bytes, wide integers and float32 NaN payloads within explicit
  budgets. Unsupported/corrupt representations still reject.
- `editor/project/ProjectPreservation.hpp` owns the existing exact stored-value
  comparator, canonical promotion and three-way merge. The one new behavior
  recognizes known `instanceID` fields for plugin arrays, including when an
  original plugin has an unrelated opaque `id` field. Unknown-bearing arrays
  without a stable identity still refuse ambiguous edits.
- Windows forwarding headers preserve existing includes and namespaces.
  Main, provider and standalone test CMake inputs point to the shared source.
  Container admission, filesystem publication, source protection and document
  ownership remain in `windows/Project/`.
- `mac/Bridge/NativePropertyTree.hpp` converts original NSData directly to the
  typed tree and back. Only the newly encoded **known model** passes through
  Foundation property-list serialization. Unknown original values never go
  through JSON text. Native file publication remains a session responsibility.

The codec and preservation tests now live in `editor/Tests/` and are scheduled
on both platforms. Windows retains the standalone `ProjectNative` test entry
point and gains a main-build `binary-plist-tests` target. Mac adds a Foundation
bridge test and the direct test script entries.

## Qualification and limits

Source equivalence checks confirm unchanged codec and codec-test bodies except
comments/include paths; receipts are in
`bin/property-tree-20261020/source-checks.json`. Old CMake/script references to
the moved implementation/test paths were searched and updated.
`git diff --check` passes. These checks are not compilation evidence.

The next permitted build is **05:53:34 UTC on 20 October**. Co-batch
`binary-plist-tests`, `project-preservation-tests` and existing native project/
metadata checks with the already prepared API changes. The codec test includes
its existing resource/malformed-object/typed-value checks and 6,000 deterministic
mutations; do not reduce them. The preservation test adds plugin reorder,
state replacement, incidental opaque IDs and deletion without resurrection.

Mac additionally runs `native-property-tree`: typed binary/date/UID/wide-integer/
NaN/Unicode/signed-zero preservation, indexed entity reorder, known edits,
encode/reopen and unchanged captured input. These are currently unexecuted.
They establish only adapter/merge behavior, not native session compatibility.

No project version, storage identifier, plugin identity, audio/UI framework or
OpenMPT attribution changes. Existing source protection and exact comparison
assertions remain required.

## Required Mac session integration next

1. Capture the original typed root and a canonical **unedited** baseline in the
   staged load candidate before replacing `_document`. Commit/reset them with
   successful Open/New/recovery replacement, never on a failed load. Avoid
   duplicating the embedded module in a long-lived baseline; it is always
   regenerated from the current document.
2. Extract the current known property-tree construction from
   `projectDataForRecovery:` so staged loading and save use one encoder. Plugin
   path hints, opaque state, manual baselines, assignments, sequence and take
   provenance must remain owned by their current session/controller logic.
3. Canonicalize accepted unedited metadata before merging. Legacy NF/NR
   `duration` migration must not reappear as an unknown active field. Recovery
   that drops invalid entries needs its accepted tree and explicit inactive
   archives; blindly merging the original invalid array is unsafe.
4. Merge the current known tree against the captured original/baseline, then
   strictly validate the actual merged musical data and references before
   atomic publication. Saving a copy may clear source protection only after
   successful publication. Undo/Redo must not replace the captured source.
5. Preserve Mac's existing XML-plist import behavior deliberately. The shared
   raw codec accepts binary v0 only. A known XML import may use Foundation's
   typed parse; a failed binary decode must not silently fall back through a
   conversion that discards unsupported original values.
6. Treat parser/output budgets and unsupported opaque types as explicit
   admission/preservation decisions. Do not turn previously recoverable
   projects into destructive saves or silently omit extensions to fit a limit.
   Keep the original file protected when lossless merging is not available.

Acceptance must execute ordinary native Open/Save plus recovery, failed load,
failed save, unrelated edit, entity/plugin reorder/removal, no-op, Undo/Redo,
unavailable plugin states and typed opaque leaves. Update unknown-field warning
classification only after preservation is active and tested. Finish with
Mac→Windows→edit→Mac and Windows→Mac→edit→Windows on exact candidate binaries,
including the F01–F05 corpus and the existing unknown-extension derivatives.
The current codec baseline remains unchanged until that work executes.
