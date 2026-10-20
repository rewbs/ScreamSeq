# Shared contract candidate: Windows ARM64 qualification

## Frozen build

Candidate: `b3fe23e45a14763c1f34fe602f8340a49385e93a`, including Mac rack dry
runs, shared integer validation, terminal request replay, the corrected duration
test, the shared property codec/policy and Mac metadata ownership source.

One two-job VS2022 ARM64 build ran from **05:53:42.153 to 05:57:03.771 UTC on
20 October**. Configure and all ten requested targets succeeded. The previous
B158 output remains intact. The next build must not start before
**06:57:04 UTC / 23:57:04 Pacific on 19 October**.

Executable:
`C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq/bin/windows-parity-p1/SharedContracts-Release/ScreamSeq.exe`

SHA-256: `636657a5c4fad99673ca0f28c5f1e0185b30f4828af0e5b1e31e83b672e08297`.

Receipts and logs are in `bin/shared-contracts-20261020/`: `build.json`,
`BuildInfo.json`, `configure.log`, `build.log`, `checks.json`, `native.xml`,
`native.log`, `application.log` and the conformance subdirectories.
The qualification checkout remains frozen at B3; later source corrections are
not present in this executable.

## Executed results

**21 of 22 selected CTest registrations passed**, including the scratch setup,
in 89.55 seconds:

- The native command-result/UI completion group passed in 75.00 seconds.
- Shared integer validation, project preservation and binary codec passed.
  This includes the new portable metadata-owner tests, not Mac session tests.
- The corrected native-metadata test passed, retaining complete-model,
  duration-unit and malformed-command assertions.
- Controller sampling, assets, live parameters, pattern performance/transform/
  clipboard, graph recipes, unified history, recovery, recording, arrangement,
  annotations, tracks and timeline passed.
- `SessionCacheTests` passed, including terminal errors, busy retry, uncertain
  outcomes, retained-ID conflicts and exact cache accounting.
- `ApiTests` failed at **“Invalid input must reject before host.”** Its
  `workspace.input` invalid-case loop reused the literal request ID `invalid`
  for different payloads. The first terminal error is now retained, so later
  cases correctly produce an ID conflict. The test source now uses the existing
  fresh-ID helper for each independent validation case. The -32602 and
  no-host-mutation assertions remain intact. This correction is **unbuilt**.
  Later real-pipe ACL/boundary/serialization/completion groups in that executable
  were not reached; do not count them as passes.

**All ten actual-application plugin contract tests passed** in 18.627 seconds:
integral numeric representations/rejections and save/reopen; terminal request
replay and exact saved Undo baseline; rack dry runs; rack position preservation
and rejection/no-op/Redo; recipe manual edits, gesture boundaries and history;
and rack/recipe transient preview ownership, cancellation and persistence.
They used a disposable PID-pipe application on a private desktop.

The configured inventory has **201 registrations, zero unscheduled and zero
multiply scheduled tests**. `native-project-tests` compiled but was not
registered: the separate historical September reference path is unset. Do not
substitute F04 for its different hard-coded fixture assumptions. The current
October fixtures have the independent actual-application evidence below.

## File and API evidence

All **F01–F05 exact typed save/reopen checks passed** in
`conformance/report.json`. The API vector capture observed exactly three
expected changes from its still-pinned Windows baseline:

| Vector | Old observation | B3 observation |
| --- | --- | --- |
| `pattern-index-real` | -32602 | success |
| `plugin-slot-real` | -32602 | success |
| `rejected-request-id-reuse` | success | -32600 after the retained -32602 |

The API capture reports **zero preservation failures**. Its baseline remains
unmatched pending reviewed cross-platform execution; parity is not complete.

The codec run **did not complete**. `unknown-root` saved/reopened; the protected
overwrite probe for `unknown-native` then received -32003 with
`writeOutcome: unknown`. The destination guard correctly refused the write,
but its exception lacked a typed pre-commit outcome. The new conservative API
boundary therefore correctly refused to infer no side effect. The corpus
correctly stopped rather than proceeding after an uncertain result.
`conformance/codec/report.json` retains this failure; no comparison, refusal or
source-integrity assertion was weakened.

Because the codec stop prevented the duration stage, a separate bounded
`duration-only/` run used F04 and the existing three omitted-duration derivatives.
It passed both saves/reopens for parameter-set, scratch and scratch-stop, with
only the explicit permitted zero-duration materialization. All other typed
values matched exactly. That run completed at **06:04:02.490 UTC**.

Both conformance runs report unchanged executable, inputs and original corpus.
No QA application process remained after completion.

### Edited opaque metadata on the retained B3 executable

A later tooling-only check completed **06:14:37–06:14:49 UTC**, with no build.
Evidence is under
`bin/shared-contracts-20261020/edited-metadata-final-2/`. The runner's F04
no-edit leg and **18 paired clean/opaque saved stages passed**: baseline,
title, graph-node label, plugin move, plugin removal, Undo, Redo, Undo restoration
and reopen on each input. Root/native/track/node/plugin extensions retained
data, large integers, date, UID, Unicode, numeric type and negative zero.
Stable plugin identity followed movement, disappeared on deletion and returned
exactly with Undo. Title/label/move checks also constrain unrelated project data;
Undo/Redo require the complete corresponding prior tree. All input, output,
executable and original-corpus hashes remained unchanged.

The new shared `edited_metadata.py` compares exact expected output without
stripping unfamiliar fields. Its first diagnostic run exposed Python plist
float interning in the **probe generator**, not a product loss: negative zero
had replaced unrelated positive-zero values before loading. The corrected
generator verifies its complete intended typed input before native admission.
A subsequent tooling-only failure involved editing the strict reader's
duplicate-key-guarded dictionary; oracle copies now use mutable dictionaries
without changing typed values. Both failed reports remain retained alongside
the final passing run. The three existing roundtrip safety tests also passed.

Both platform workflows now request `--edited-metadata`; Mac execution remains
pending. The retained `edited-metadata/opaque-reopened.screamseq` is a concrete
edited input for the next reciprocal exchange. This Windows private-desktop
check does not complete reciprocal editing, Mac Open/UI behavior, or the earlier
codec failure. It does not qualify the unbuilt file-outcome correction.

### Cross-domain stopped composing journey

`composing-history-final/` records **06:39:39–06:39:56 UTC** on the same retained
B3 executable. F04's no-edit leg and **21 saved checkpoints passed**: six musical
edits across precise notes, sample loop, independent instrument pan envelope,
manual plugin parameter, recipe audio-edge gain and mixer send; six chronological
Undo steps; six Redo steps; a real history fork and final reopen. Every dry run
and interleaved no-op preserved the current document/history. A stale captured
note replacement rejected without mutation. Exact saved-tree and PCM checks
retain unrelated data. Inputs, previous outputs, executable and original corpus
were unchanged; the QA process exited. Both workflows now request the shared
`--composing-history` check, with Mac execution still pending.

Diagnostic runs retained under `composing-history-1/` through `-3/` corrected
the oracle's precise-note replacement order and real-valued send gain type.
The exact comparator was not normalized or relaxed. F04's linked volume
envelope also correctly refused a direct transform; this journey uses its
independent pan envelope, while A08 linked-master editing remains open.

That linked-envelope refusal exposed an additional outcome-classification gap:
the error occurred while validating a private document clone, yet the API
reported `writeOutcome: unknown`. The new private-PID regression reproduces
the failure on B3 in **0.696 seconds**; receipt/log are under
`bin/asset-admission-20261020/old-binary.*`. This is a reproduced failure,
not a pass. The prepared `AssetOperations.cpp::preflight` correction annotates
errors arising inside that read/clone-only helper as `NotCommitted`, preserving
validation codes. Actual document publication and playback callbacks remain
outside the catch. Controller and application regressions require exact
bank/PCM/view/Redo preservation and a successful independent edit afterward.
**This correction and its new assertions remain unbuilt/unqualified.**

The stopped journey is bounded A01 evidence, not the full A01/A08/A18 gate,
foreground inspection, live audio or reciprocal editing. Its concrete final
file is `composing-history-final/composing-history/reopened.screamseq`.

## Prepared corrections after B3

Alongside the fresh-ID test correction, `DocumentController.cpp` now prepares:

1. An explicit `NotCommitted` receipt around **read-only Save admission only**:
   field/path/type checks, existing-destination checks and source protection.
   Serialization, staging, atomic replacement and result construction remain
   outside that catch. Failures there are not declared safe merely because the
   musical revision is unchanged.
2. Open joins New's existing document-replacement rejection boundary. Open
   reads/stages a candidate before the ownership swap; generation/path changes
   and prior editor-flush musical changes remain covered by the existing
   committed-outcome check. Failed candidate decoding/admission can now return
   a definite rejection without weakening generic host-failure handling.

Controller tests now require explicit rejected outcomes for protected-source,
existing-destination, invalid-parent/extension saves and dirty/corrupt/pre-view
Open failures. They retain exact original bytes, complete current view, playback
stop count and absent completion receipt checks, then require a successful copy.
Existing post-commit/uncertain completion checks remain unchanged.
**These source corrections and assertions have not compiled or executed.**

## Next bounded gate

Co-batch the corrections at the next permitted build, then run `ApiTests`
through its previously unreached real-pipe groups, targeted controller recovery
and publication tests, native command-completion checks and the actual codec
corpus. Include controller assets and the private-PID linked-envelope regression
for the newly reproduced pure-preflight outcome gap. Reuse B3's unchanged integer, cache, codec-helper, metadata, fixture and
plugin-contract evidence where the relevant inputs remain identical. A changed
outcome boundary requires actual file/API checks; a source diff is not enough.

Mac still needs both architectures' app/automation/shutdown links and targeted
Foundation, recovery, opaque-property, recording and plugin-state execution.
Current x64, reciprocal **edited** F01–F05 exchanges and final shared-main
integration remain open. Do not merge an unqualified correction merely to make
the report green.

No live computer-use, foreground visual/DPI/accessibility, physical input,
vendor lifecycle or audible qualification is claimed. Computer use remains
stopped after the user's Escape.
