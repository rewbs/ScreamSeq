# Current gesture candidate: CI review and retained follow-up

## Candidate and build boundary

Product candidate `db42b142196471f0a34fe67554c55dfc9de68439` and CI merge
`e4f36a1f1c84dbaa0995dd7ad74843e0f870d149` have identical Git trees.
Windows run `39418201506` and Mac run `39418201597` are terminal.
Their native builds finished at 12:19:05 UTC (x64), 12:11:19 (Mac ARM)
and 12:27:47 (Mac Intel). **No new native build before 13:27:48 UTC.**
This review and the follow-ups below started no builds.

| Platform | Observed result | Limit |
| --- | --- | --- |
| Windows x64 | 157/157 portable/worker registrations, 48/48 native editor groups and three scratch application cases passed. Typed conformance, edited metadata, composing history and envelope-bank journeys passed. | The 86-case application group was red: 14 failure records across 11 cases, including three teardown duplicates; two cases skipped. |
| Mac ARM | 133/134 native tests, 35/35 interface groups, sample library and recovery/picker checks passed. Eight application editing contracts and complete conformance passed. | `unified-history` failed at `Save/reopen exact parameter history`. Assertions after that point did not execute. |
| Mac Intel | Same 133/134 native and 35/35 interface result. All eight application contracts and conformance step passed. | Same exact `unified-history` failure. Native test time 195.20 seconds. |

Mac's actual composing-history journey now passes the previously failing exact
opaque-plugin-state Undo comparison. This is independent evidence from the
still-failing native history executable, not a reason to label that executable
green.

## Windows failures and follow-up

The ten device-dependent cases from CI passed on the **same x64 executable**
under Windows ARM64 emulation, using silent output and owned never-switched
private desktops. Endpoint availability was probed first; system defaults and
the musician's session were not changed. The eleven-case follow-up took 24.337
seconds: ten passed, while graph setup hit the explicit worker-busy admission
error. `retained-x64-check/report.json` preserves every result.

The distinct graph failure in CI was the bus selector returning `CB_ERR`
instead of retaining its captured target. Its test opens the separate retained
parameter inspector, then proceeds directly into an API mixer mutation and
assumes one graph timer message establishes a fresh bus catalogue.

The corrected test closes that separate inspector after its parameter
assertion, waits for the exact mixer revision and actual bus choices, and
retains all invalid-value, draft, assignment and clear assertions. It passed
against the unchanged CI executable in **2.385 seconds**. This is a targeted
follow-up, not a rerun or retroactive pass of the entire CI application group.

Source inspection also found a real rejection-path defect: the busy/pending/
drag gate restored only the node picker after native selection had changed.
Prepared product repair `restoreGraphTargetSelection` restores the captured
library, node, page, property, bus, wire or parameter selector before refusing
the action. The retained-command-result refusal uses the same helper.
No draft, musical edit, revision or history is adopted.

A deterministic native regression exercises seven selectors under all three
busy gates with retained raw text, selection, focus, generation and song/history
comparisons. **This product repair and C++ regression remain unbuilt.**
Run the existing advanced workspace group at the next coordinated build,
alongside the affected graph application case. Expand only if those expose
shared dispatch/layout changes.

## Mac history fixture repair

The failing fixture adds an unknown root extension deliberately. Current
`TrackerSession.mm::decodeProject` marks such a source protected, and
`validateRecoveredSavePath` refuses to overwrite it. The old test nevertheless
saved to that same path. The CI error is the test's generic Save/reopen message;
the protection diagnosis follows from the inspected fixture and source.

The prepared test now asserts protected-source status, same-path Save refusal,
unchanged revision/source bytes and complete edited tree, then saves a separate
copy, reopens it and compares the complete typed tree again. No assertion is
weakened and no product Save behavior changes. **The correction is unbuilt**;
both Mac native history tests, including their previously unreached assertions,
remain required at the next build.

## Reciprocal execution

The retained local Mac ARM → Windows ARM64 exchange passed all eight supplied
files: F01–F05, edited opaque metadata, mixed history and envelope bank. Each
passed two exact typed save/reopen comparisons. Source and binary hashes
remained unchanged. `mac-to-arm64/exchange.json` records this one-direction
result; it does not replace the ten-file x64 → both Macs → x64 gate.

Extended run `39423914064` used tooling `6008e3c36` with retained native apps.
Apple Silicon passed nine files but stopped the opaque-metadata leg at launch
identity before Save. Both jobs are terminal failures. The runner assumed
`context.file` remains populated after a protected import; Mac intentionally
clears it to require Save a copy. The exact-source/protection repair and its
four passing tooling tests are described in [extended exchange](EXTENDED-EXCHANGE-2026-10-20.md).

A new **no-build** exchange was accepted at 12:44:16 UTC using tooling
`29bf7dfea191c887e5fdb71adadde96446463a98`, the same retained Mac app run and
Windows input artifact: run **39424598212**. Apple Silicon job `118633854501`
passed all ten files, including opaque metadata, at 12:45:48 UTC. Its artifact
is `12265183078` (`screamseq-retained-macos-15`), ZIP SHA
`865a3101a0bc3f08c3b54f9258498a40b8fea550666cd5babccee2f0d9bfa84f`.
Intel job `118633854135` also passed all ten files at 12:46:19 UTC.
The overall Mac exchange run is terminal success.

The Windows x64 return from this successful ARM artifact was dispatched at
12:46:49 UTC, again with no build; receipt
`windows-arm-return-dispatch.json` identifies its exact inputs and run
**39424938001**. The Intel return was accepted at 12:47:55 UTC as run
**39425081819**, with receipt `windows-intel-return-dispatch.json`.
**Both returns are terminal success**, with all ten files passed on each.
ARM return job `118635001897` finished its last file at 12:48:03 UTC;
Intel return job `118635497933` at 12:49:07 UTC. Do not poll/restart them.

The downloaded final artifacts were then checked locally without launching an
app: complete v2 manifest/leg/source/input lineage validation, every archived
output hash, and direct typed-tree comparison of all twenty final files to
their originating Windows output. All passed. Receipt:
`reciprocal-verification.json`; verifier: `verify-reciprocal.py`.

| Return | Artifact | Archive SHA-256 | Retained manifest SHA-256 |
| --- | --- | --- | --- |
| Through Mac ARM | `12265378061` | `713a1dd502eb0a5664c0793e9f44b60f85dd3de4e2943daf9e3bcbf5281f5247` | `0d1ef63e014f6cad235e3f3446e5b2074f9803c8e1d9dbcf89480c943500abbf` |
| Through Mac Intel | `12264934013` | `59900090c2424fc04b3088e3949cd5f41f6363a9acfe8d735ac8b1bf9a7eea5a` | `2a383b41be3023825f4b9f9a967b8d2161fdd8ed211f675b2995b01c47496b69` |

This closes the ten-file reciprocal persistence gate for **db42/e4f36 only**.
It does not qualify the newer graph correction, history fixture repair or
recorded-take owner, and is not completion of the full parity inventory.

## Traceable artifacts

Local evidence root: `bin/gesture-batch-20261020/` (ignored, not shipped).

- Windows artifact `12264581784`, archive SHA
  `453b5080e649036b0ccc13e94b1ea1eae5aec418119cfcdd6fb2b28be01ccd95`;
  extracted to `windows-ci/`. Executable SHA
  `399b80a980c8ca0f9d5aae9a7e79d80a9c623625c2878ffad29d45bcab2aabf3`.
  Parent conformance report SHA
  `0f6c6b63ae7fe3a701ffce3bb734d177d92d68ae06154bac86bc681d46e9b921`.
- Mac ARM artifact `12263114488`, archive SHA
  `71720d81649a2006eff3ed1294d198a4329a201e26c1c207ccce572224132cfa`;
  extracted to `mac-arm-ci/`. App/BuildInfo identities separately checked.
- Failed ARM exchange artifact `12263879560`, SHA
  `5daa7f2c03bb53cfa6147f2a290b7acedc337f9009a8d0096ba56026f6b9aeaa`,
  extracted to `mac-arm-exchange/`, including failed launch/process receipts.
- `retained-x64-check.log`, `retained-x64-check/report.json`,
  `graph-form-followup.log`, `protected-launch-tests.log`,
  `mac-protected-exchange-dispatch.json` and `mac-to-arm64/exchange.json`.

Archive and executable identities were checked before use. No foreground UI,
DPI, accessibility, physical audibility or complete parity claim follows from
these isolated checks. Native arming/intake/recovery for recorded plugin
gestures and shared-main integration remain outstanding.
