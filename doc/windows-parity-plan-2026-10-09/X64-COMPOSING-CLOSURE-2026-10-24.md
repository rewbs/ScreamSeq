# Corrected Windows x64 composing acceptance

Observed 24 October 2026, 23:32 UTC. Source: `b0375de637dd940cce225a0aba15fab69ee2f50f`. This closes the changed-path x64 acceptance findings, not overall production integration or complete macOS parity.

## Executed evidence

Downloaded artifact `12579187759` from Windows run `40140424174`, verified outer SHA256 `96f356a3f037e4bd8aa37a04bb37a27988a3c9d82e4ad3fef945e6b3836d914a`, nested archive and executable hashes, build manifest, x64 PE machines and matching VST3 provider/cache. CI test-merge `79df3d591afbc60d65b620e3fc7a76c7ed59386a` has the expected main/head parents and exactly the candidate tree. Executable SHA256: `fa8af62c9b90700fac9599407cb7e70fc7872677aae078397330615029aa8023`.

The existing binary ran once on an isolated private desktop with a real silent WASAPI endpoint, under x64 emulation on ARM64. Four actual-app cases passed:

- Already-kept take Discard refusal, later-input preservation and explicit saved-state recovery.
- Normal native Record/Stop/Keep/history/reopen/Discard.
- Interleaved live precise-note and plugin-take chronological history.
- Complete layered sample/VST3 composing and recording, including six-domain history, stable assignment/routing, exact reopened PCM, independent sample/plugin contributions and six full-song WAV exports.

The last case passed the selection-render request that failed on the preceding x64 candidate. The fixture, exact equivalence, history and failure assertions were retained; no uncertain request was retried. Source, executable, provider and cache identities stayed fixed.

## Failed aggregate and its reconciliation

The prepared five-case replay also requested `RecoveryControllerTests.test_manual_vendor_editor_overlay_and_late_protection_guard`. Its controller executable is not included in the retained application archive, so it raised `FileNotFoundError` before execution. The aggregate therefore remains **failed (one error)**; `local-live-checks.json` and the failed log are not rewritten as a pass.

That exact controller case already executed successfully in current-source CI job `121010812681`, on its owned private desktop with the matching provider. Its retained `screamseq-vendor-ownership.log` records one test, OK; SHA256 `b9793e9d48bb6ebb9c67a5cf471be3097433f6d1eac5a259ff077bb295f10d0f`. This current-source pass supplies that requirement. No rebuild, rerun, substituted controller executable or weakened assertion is needed.

The Windows workflow remains failed: its runner reports no audio devices, and the integration run reports 23 failures, four errors and one skip across 125 cases. Accounting for duplicate/teardown diagnostics gives 23 distinct affected cases. `reconcile-x64.py` maps all 23 to either the four fresh actual-app passes or the prior 946 exact-x64 real-endpoint passes. It verifies prior log hashes, unchanged test-method ASTs and unchanged shared/audio/device sources. Only three runtime files changed since 946:

| Change | Required new coverage |
| --- | --- |
| Initial envelope-bank work-area clamp | Current native placement/retention group and prior fresh bank foreground check; no musical logic change. |
| Offline selection-render reply budget | Current actual pipe slow-completion/replay group, ordinary cancellation/admission assertions and the fresh complete layered app journey. |
| Already-kept Discard refusal | Current native recording group and fresh real-endpoint lifecycle/recovery cases. |

All 162 portable/worker tests and 53 native groups passed on the current x64 CI build. Vendor ownership, typed pipe, codec recovery, composing history and envelope-bank preservation steps also passed. The 19 remaining affected cases reuse valid earlier live evidence; no case is relabelled as successful merely because its CI failure looks environmental. The composite acceptance is recorded separately in `ci-x64/acceptance.json`, with the failed CI and local aggregate explicitly retained.

## Remaining delivery gate

Current Mac ARM passed 135 CTests, 36 interface groups and required lifecycle/socket/typed checks. Current Mac Intel is still running. The preceding Intel candidate passed and its native inputs are unchanged, but its current job must be observed rather than restarted. Reciprocal fixture reuse is valid only for unchanged codec/shared/model inputs.

No new product correction was identified by this replay. Final platform/review accounting, shared-main integration and packaging remain. Any main push must respect the coordinated hourly build gate (currently no earlier than 23:53:17 UTC, subject to later starts and active jobs). No new compilation was performed for this replay.

Evidence root: `bin/mac-gesture-20261022/render-reply/`; `ci-x64/verified-identity.json`, `local-live-checks.json`, `local-live.log`, `acceptance.json`, `composing-layered-instrument/recording-journey.json`, its audio/export reports, and `mac-arm-acceptance.json`. These ignored execution artifacts must be retained separately from this document.

Boundaries remain: x64 emulation is not physical x64 hardware certification; functional checks do not establish sustained realtime or temporal-flicker performance. Known dense-effects overruns, bounded floating-input observations and recovery safeguards remain documented in the goal report.
