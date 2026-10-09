# CI checkpoint 04: reviewed results and remaining work

Frozen source: `d39ec5548fc6224f5d395dbf02d6b0eca35b48ac`. The
[receipt](p0b-ci-checkpoint-04.json) pins downloaded artifact hashes, executable
and report identities, log hashes and exact failed application case names. Both
downloaded archives passed SHA-256 and extraction-path checks. None of this
qualifies the later exception mapping or the separate P1 mixer candidate.

## Windows x64

[Run 38003410537](https://github.com/rewbs/ScreamSeq/actions/runs/38003410537),
job 114066588740, finished with failure. The build succeeded. Portable/worker
tests passed **136/137**; only `document-controller-arrangement` failed with
`The order list is full.` This matches the local ARM64 failure and the unbuilt
`6852f1544` exception-mapping correction. All **29/29** native editor/preference/
recovery cases and the three scratch application cases passed.

The selected application batch ran 30 tests with **12 failure records across
nine distinct cases** (three audition failures also failed teardown). The audio
probe proves that service startup succeeded for both AudioEndpointBuilder and
Audiosrv, but `audio.devices.get` still returned an empty device list. The probe
had no active endpoint, callbacks or recording clock. Audio-dependent startup and
recording-clock failures remain unqualified; starting services is insufficient on
this hosted Server image. Do not skip these checks, simulate their expected
results or call this physical-device evidence. The archived x64 binary remains
available for qualification on a device-capable Windows host without rebuilding.

A **separate** non-audio failure occurred in
`test_native_browser_save_reload_restore_and_file_save_cleanup`: after native
Save, the selected recovery file still existed at its eight-second deadline
(`test_recovery.py:234`). This test launches inspection mode, so the absent audio
endpoint is not an explanation. Inspect Save admission/status and cleanup queue
state with the retained binary; preserve the deletion assertion and original
deadline. The two other recovery application cases passed. Local ARM64 previously
passed this case on the same frozen source; that does not erase the x64 failure.

All five supplied same-platform fixture save/reopens passed. Actual pipe API
execution matched the pinned baseline, with no-op/unrelated file preservation and
edit/history/replay persistence passing. It still reports the known
`plugin-parameter-stable-identity` preservation failure (API-PLUGIN-NOOP); baseline
agreement is not API parity. The 22 conformance-tool safety cases and 18 project
helper cases passed. No reciprocal d39 exchange is claimed.

## Mac Apple Silicon

[Run 38003412848](https://github.com/rewbs/ScreamSeq/actions/runs/38003412848),
job 114066597302, finished with failure. The app build and all **122/122** native
tests passed, as did Swift recovery/picker and sample-library steps. Interface
coverage improved to **34/35** groups: the only failing group is `signal-graph`,
at the host parameter-drop exact-one zero-depth assertion. The previously failing
draft-plugin, graph-preview and pattern-grid groups passed. These results apply
only to their frozen inputs; P1 changes invalidate the relevant mixer evidence.

All five supplied fixture save/reopens and actual socket conformance completed.
The API baseline matched, with zero preservation failures and successful no-op,
unrelated-file and edit/history/replay preservation. Known contract differences
remain explicit. This is a same-platform roundtrip, not a reciprocal exchange.

Source inspection of `graphParameterDropChecks` found that its fake request
handler appends every request except `plugin.parameters.get` to `writes`.
`inspect()` also calls `GraphTrimControls.context`, which requests `graph.trim.get`.
The fixture therefore counts a catalog read as a mutation. The prepared follow-up
supplies explicit empty trim catalogs in both rack and recipe fixtures. It adds
an assertion that initial inspection submits no mutation and separates the
exact-one, stable-target and zero-depth predicates, also checking minimum zero
and continuous quantization mode. Unknown request methods still enter the failure
counter. This is an **unexecuted fixture correction**, not a verified product fix.

## Intel and next checkpoint

Mac Intel job **114066597619** in the same run was still authoritatively building
at 23:42:41 UTC. Continue observing that exact job; do not restart it. Its result
must be inspected before claiming the dual-architecture gate.

No new build or product test was launched for this review. Next normal local
build remains no earlier than **2026-10-10 00:19:51 UTC**. Batch the known Windows
error mapping, Mac fixture correction and ready P1 source before that checkpoint.
Run the affected arrangement, graph-interface and mixer cases plus the required
common command/owner regression group once against the frozen candidate. Keep
the Windows endpoint and recovery-cleanup issues visible until directly resolved.
