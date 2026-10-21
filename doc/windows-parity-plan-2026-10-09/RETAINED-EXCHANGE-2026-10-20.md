# Retained cross-platform fixture exchange

**Passed for the frozen candidate. No builds performed.** All participating
executables were built from merge commit
`d79321c7ccf59e7fdf0a4b2918d6906b41097ef4`, corresponding to PR head
`54fa2babc96e63d0b10ebd8dedb8c438036f3b3f`. This does not qualify newer local
clock, recovery or Mac adapter repairs.

## Proven chain

1. Windows x64 CI run 39393737358 produced and reopened F01–F05. Its native
   `workspace-parity-fixture-tests` produced one rendered and one imported F04
   derivative, with the originating native test log and asset hashes retained.
   Those individual fixture legs passed even though other CI gates failed.
2. No-build [exchange run 39399717354](https://github.com/rewbs/ScreamSeq/actions/runs/39399717354)
   used the Mac apps from run 39393737331 and the exact Windows logs artifact.
   Both Apple Silicon job 118551112695 and Intel job 118551112822 passed all
   five no-edit fixtures and both edited derivatives through save/reopen.
   Native build and interface-build jobs were explicitly skipped.
3. The retained Windows x64 executable reopened and saved the Mac outputs on an
   owned, never-switched private desktop. Both no-edit and edited return legs
   passed. The x64 executable ran on Windows ARM64; no native-x64 hardware or
   performance claim is made.

The seven first-save files from Apple Silicon and Intel are **byte-identical**.
The existing reuse helper validated both Mac identities, preceding-leg report
hashes, immutable input/output hashes and exact fixture sets. The Windows return
result is reused for the identical Intel inputs; no redundant return run was
needed. `exchange-return-review.json` records this derivation explicitly.

This proves exact typed project preservation for the specified Windows-to-Mac-
to-Windows chain, including the original five fixtures and the native
render/import derivatives. It does not cover arbitrary project migrations,
opaque malformed metadata, live vendor state, physical audio, UI presentation,
or the complete A01–A18 journeys.

## Retained identities and evidence

Binary/BuildInfo hashes are listed in
[native CI evidence](EXPORT-CROSS-PLATFORM-2026-10-20.md) and
[paired API review](PAIRED-API-REVIEW-2026-10-20.md).
The reuse helper checked every executable/archive/receipt before launch and
checked source differences. Local tooling branch `codex/export-retained-qualification`
at `1edc14926` differs from the built source only in the reviewed API inventory.
No application source changes were qualified by an older binary.

| Evidence | SHA-256 |
| --- | --- |
| Apple Silicon artifact 12255146244 | `5123787c6fc2402521d1ba3c6c0d4d798758249c78842d94c400f7a360bbf0cb` |
| Intel artifact 12253677883 | `de8e9ac72f739c909861fb6b7b8df674262c9181981b38a14d4508095424e58c` |
| Apple Silicon retained-run report | `5fb40bec288c76501a8190499eb80a9e5f1570123e2db643d8653f9519148c9a` |
| Intel retained-run report | `2aa5aade473535d4648de9edbffd864b722d5c1a0d535447a730e6ce126be5c5` |
| Windows return retained-run report | `97238ec94aa6c290a6e80aea24443337d2ca9e157a302bc6b01d2b3ffcda1d42` |

Primary worktree evidence root: `bin/export-capture-20261020/`.
Original archives: `mac-arm-exchange.zip`, `mac-intel-exchange.zip`.
Extracted reports: `mac-arm-exchange/retained-result/`,
`mac-intel-exchange/retained-result/`, and `windows-return-arm/`.
Each contains separate `no-edit/` and `edited/` reports and saved outputs.
The dispatch receipt is `retained-exchange-dispatch.json`; submitted once,
HTTP 204, exact unchanged branch/head verified before dispatch. Both jobs are
terminal success. Local owned apps exited.

## Remaining integration boundary

Retain these files for reuse where binary, source, fixture and test inputs remain
applicable. The final shared-main candidate still needs its applicable build,
targeted regressions and cross-platform integration gate. Do not claim this
older frozen candidate is the current local source or merge the draft merely
because the fixture exchange passed. The next coordinated build remains no
earlier than **10:03 UTC**.
