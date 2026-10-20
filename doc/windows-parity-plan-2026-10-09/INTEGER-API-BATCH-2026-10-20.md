# Shared API integer validation

## Evidence and scope

The retained Windows ARM64 app at `b1581c9c8` rejects `pattern.get` with
`pattern: 0.0`, while Mac's `Automation::integer` accepts integral-valued numbers.
The new shared actual-app contract reproduces that rejection in **0.487 s**,
before any mutation; `bin/integer-api-20261020/before.log` retains the exact
failure. Executable SHA-256:
`ebc103df3c5ea20a86f6c7ee09e13e3d4d9fe43a10918fb987489b5548e5793e`.
This is an expected pre-fix failure, not a pass of the remaining assertions.

Source also showed inconsistent Windows validation: document/assets/graph/
envelopes/tracks/mixer/pattern operations accepted integral real values, whereas
session pattern reads, plugins, preview identities and note recording required
integer JSON storage. Several helpers converted exact integers through double
before checking bounds. Mac used double for every NSNumber and then cast to
uint64; a rounded upper bound such as `double(UINT64_MAX) == 2^64` was unsafe.
The latter is a source finding, not a reproduced application crash.

## Prepared implementation

`editor/IntegerValidation.hpp` owns the platform-independent conversion and
range policy. Signed/unsigned integer inputs retain exact storage; negative
signed values reject. Floating inputs must be finite, integral and in
`[0, 2^64)` before casting; the converted integer is then checked against the
exact field bounds. The boolean overload is deleted.

`windows/Api/IntegerValidation.hpp` adapts nlohmann JSON without admitting
booleans/non-numbers. `mac/Bridge/AutomationValidation.hpp` adapts NSNumber,
checking CFBoolean first and distinguishing signed/unsigned storage from real
storage. Foundation and JSON dependencies stay out of the shared helper.

Windows session helpers now reuse this policy across document, asset, pattern,
timeline, track, mixer, graph, envelope, plugin and note-recording operations,
parameter monitoring, preview IDs and sample-render selection coordinates. Session
navigation/transport and sample-library integer fields also reuse it. Existing
field bounds, whole-batch validation, revisions, history and persistence remain
in their original owners.

This is not a replacement of file codecs or native UI readback validation.
Windows `HostedProject`, sample-index/preferences decoding, private driver
fixtures and platform-specific device settings retain their own contracts.
Mac's existing metadata code calls `Automation::integer`; consequently its
codec checks are required even though field limits and formats do not change.
Stable string identities, opaque state, native IDs and project versions are
unchanged.

Integral value equivalence does **not** change replay identity or caching:
retain the exact original request on retries. Windows's existing test that a
changed integer/real representation with the same retained ID rejects remains
valid. The separate rejected-request-ID discrepancy is still unresolved.

## Bounded validation and integration

**Source only; no new build performed.** The prior batch finished at
04:53:33 UTC, so the next build must not start before **05:53:34 UTC**.
Batch these changes with the pending metadata-test correction and Mac rack dry
runs rather than creating an extra build cycle.

Cheap checks: all three changed/shared Python modules parse, their hashes are
retained in `bin/integer-api-20261020/source-checks.json`, and `git diff --check`
passes. The pre-fix actual-app run above exited and left no QA app process.
No compiled helper, Foundation adapter or new success-path assertion has run.

Prepared checks:

- Shared `integer-validation`: exact signed/unsigned endpoints, negative zero,
  fractions/nonfinite values, exact values above binary64's consecutive range,
  UINT64_MAX, the real value below 2^64, rounded lower/upper bounds and inverted
  ranges.
- Mac `foundation-integer-validation`: actual NSJSONSerialization literals,
  Foundation booleans/non-numbers, NSNumber exact uint64 and safe real bounds.
  Both new tests are registered in CTest and the direct Mac test script;
  Windows's portable target/label includes the shared test.
- Windows `ApiTests`: parsed integer/real/exponent pattern reads, strict invalid
  values and exact JSON integer bounds; existing replay, busy and uncertain
  completion checks remain mandatory.
- `editor/Tests/integer_api_contract.py`, called by both actual-app suites:
  equivalent pattern/plugin coordinates, real descriptor/parameter IDs,
  duplicate numeric IDs, invalid later batch entry without partial mutation,
  preview ownership/cancel, no-op/Redo preservation, stale guard, one-step
  Undo/Redo and exact saved tree. Windows adds native reopen.

Next Windows build targets: app, integer-validation-tests, ApiTests,
SessionCacheTests, document-controller-tests and native-metadata-tests, plus
any targets required by other newly co-batched work. Run the new checks,
affected controller operation groups and the existing strict preview/recipe/
rack contracts. The previous UI geometry/gesture evidence remains reusable if
its inputs stay unchanged; a failure in dispatch or retained state triggers
those relevant UI groups.

Mac must execute both integer tests, automation/session/parameter-activity
groups and the shared socket contract. Re-run exact F01–F05, API, codec and
zero-duration captures on both platforms because request validation and the
Mac metadata adapter changed. Keep the current `API-INTEGER-TYPES` corpus
expectations until native evidence demonstrates the new behavior, then
reconcile those two cases and rerun the cheap capture on the same binary.
Do not turn a planned normalization into a claimed cross-platform pass.

No foreground computer-use, physical audio/device or visual claim is made.
