# Paired API evidence and retained Windows qualification

This reviews **existing binaries**, all built from
`d79321c7ccf59e7fdf0a4b2918d6906b41097ef4`. No new build was run.
The [native CI report](EXPORT-CROSS-PLATFORM-2026-10-20.md) retains the original
failures and artifact provenance. New clock, Mac state-read/property projection,
snapshot-fixture and Windows file-recovery changes remain unbuilt.

## Intel Mac completed

Run 39393737331, job 118531235347 completed. BuildInfo timestamp:
08:57:42.714752 UTC. All **134 native registrations** and **35 interface groups**
passed. Conformance reproduced Apple Silicon's recipe-preview state-read failure
and opaque UID load failure, plus the same two historical dry-run expectations.
The local source repairs still require execution on both architectures.

Intel app SHA-256:
`dd262c19471eede96292c7a3801f3fde8985ac2093b50c170ffae2c26c048d5a`.
Receipt SHA-256:
`a8977e472d99abfe8c32fc300ca949a6a5a910cfb06e5ebb95008143bcd73e2a`.
Artifact 12253590831 SHA-256:
`e52e3bd6ff272d0e5373e9d9b9ff2ff99db777c4e78f49e6e3361e78a161c74c`.
Original logs/outputs are in
`bin/export-capture-20261020/mac-intel-logs.zip` and `mac-intel-ci/`.
Keep the conservative next coordinated build bound **10:03 UTC**.

## Reviewed API convergence

The same 28 bounded API probes have **identical observed outcomes** on Windows
x64, Mac Apple Silicon and Mac Intel. All three reports have zero preservation
failures, exact no-op/unrelated saved-state comparisons and successful single
Undo/Redo/replay/save/reopen checks. Windows used its unchanged CI x64 executable
on local Windows ARM64, on a never-switched private desktop. This is not
native-x64-hardware performance or foreground evidence.

Only these historical expectations changed:

| Case | Previous expectation | Required/current result |
| --- | --- | --- |
| Windows pattern index `0.0` | -32602 | Success |
| Windows plugin slot `0.0` | -32602 | Success |
| Mac bypass dry-run | -32602 | Success without document/saved-state mutation |
| Mac parameter dry-run | -32602 | Success without document/saved-state mutation |
| Windows different request reusing a rejected request ID | Success | -32600; original rejection -32602, unchanged document |

These are intentional implemented contracts, not waived failures. Boolean,
fractional, unsafe numeric, unknown-field, stale-revision and malformed-batch
rejections remain unchanged. Exact successful replay and history checks remain.
All request inputs and assertions are retained; only the five outdated outcome
expectations in `api-vectors.json` changed. The strict state-read schema repair
is a separate unbuilt change, not proven by this review.

Original API report SHA-256 values:

- Windows: `2a2595090a68b3b4ab6fcaf2a288f6f068865c067c0e158402473d64136c95af`
- Apple Silicon: `ea9743f62e67fae2e8ba7e2b5a723b264618bf8b30ad4187959a45cccfd2fde3`
- Intel: `cffdd3740d75c292539a611a981c847ea3626d5772fcf27770e7a75bbb20ecb0`

`bin/export-capture-20261020/review-paired-api.py` verified the exact allowed
vector delta, source/binary preservation, all outcomes, complete case sets and
unchanged original reports. `paired-api-review.json` records the derived review;
it does not rewrite `baselineMatched:false` in the historical reports or claim
a new execution. Current vector SHA-256:
`3db8bd1aa21885802cac20548e850b80a904f56f4ff324323b2cdfef62a09019`.

## Additional checks on retained x64

Reused managed checkout `windows-parity-tracks/ScreamSeq` on temporary branch
`codex/export-retained-qualification`, commit `1edc14926`. It differs from the
built merge commit only in the reviewed `document.exportModule` inventory entry.
The reuse helper verified that source boundary, archive, executable and receipt
before launching. No unbuilt product repair entered these runs.

- All five F01–F05 save/reopen legs passed.
- All 21 codec safety cases completed; Windows observation baseline matched.
- All seven shared editing contracts passed: parameter preview, recipe parameter,
  recipe preview, rack dry-run, strict integer API, terminal request replay and
  module export, each with final exact save/reopen comparisons.
- Edited metadata passed clean and opaque legs through title/node-label edits,
  rack move/removal, Undo/Redo and reopen.
- Composing history passed six-domain chronological Undo/Redo and persistence.
- Envelope bank passed linked propagation, independent copies, unlink,
  rejection/history and reopen.

Receipts are under `bin/export-capture-20261020/x64-retained-baseline/` and
`x64-retained-contracts/`. Contract aggregate SHA-256:
`bd886fdf04f8f6dbf3a3be5e313ed2d1b290b6f9dc75b70135768a087a5a35a9`.
All owned processes exited; the executable and receipt remained unchanged.
No foreground/UIA/Narrator, audible hardware or whole-parity claim follows.

## Codec differences remain explicit

Both Mac architectures produced identical 21-case codec observations. Much of
their historical drift reflects preserving previously discarded extensions and
archiving lossy original metadata. The Mac codec baseline is **not updated**
without completing case-by-case classification.

One concrete Windows gap is now source-corrected: integral-real native identity
metadata caused optional music to become inactive, while Mac retained it. See
[file-integer recovery](FILE-INTEGER-RECOVERY-2026-10-20.md).
Warning wording, source-protection policy, invalid-ASCII recovery and inactive
metadata representations still differ. Do not treat matched observations as
codec convergence, and do not normalize away typed differences.

Reciprocal Windows-to-Mac-to-Windows fixtures and edited render/import outputs
remain separate gates. Their no-build workflow can reuse the exact CI artifacts;
do not rerun native builds to obtain those files.
