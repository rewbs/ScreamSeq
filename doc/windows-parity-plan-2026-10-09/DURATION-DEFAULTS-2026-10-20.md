# Preserve zero-duration commands on Windows file load

Status: **loss reproduced; correction implemented in source, not yet built or
qualified**. This addresses a bounded part of F25/P6. It does not resolve
unknown-field preservation, allocator policy, general codec convergence or the
cross-platform integration gates.

## Evidence and corrected scope

The existing `missing-non-nudge-duration` codec vector removes duration from a
PitchSlide. Both platforms reject that invalid command through recovery. The
existing baseline already documents that result. Likewise, effect zero with a
nonzero parameter is invalid under shared `NativeNoteEffectSupported`; differing
encoder conditions are not evidence of losing a valid precise-note edit.

There is nevertheless a real optional-default difference. Mac
`mac/Bridge/PatternPerformanceMetadata.inc::decodePatternPerformance` supplies
zero for absent row-unit duration. Windows
`windows/Project/NativeMetadataAutomation.inc::decodePerformance` previously did
so only for NF/NR. Shared validation permits zero for instantaneous parameter and
pitch changes, note cut, tracker effects, and native operations with absent or
optional duration. Required-duration operations still require positive duration.

A disposable process on a never-switched private desktop loaded three owned F04
derivatives with only an existing zero-duration field removed:

| F04 command | Index | Windows result before correction |
| --- | ---: | --- |
| Parameter set | 2 | Dropped; 9 commands became 8 |
| Scratch | 3 | Dropped; 9 commands became 8 |
| Scratch stop | 4 | Dropped; 9 commands became 8 |

Each load reported its skipped command and required Save as. The saved copy
lacked the command. The original corpus and every load input remained unchanged.
No audio was started. No computer-use interaction occurred.

Evidence: `bin/codec-duration-20261020/{probe.py,before.log,before.json}`.
Report SHA-256:
`442a2e96ce01fcc70b4c286e9c20921c63728bbcb3a393f902fa0af0754c22c5`.
Existing ARM64 executable source:
`e79133155d62c5eff0ceaf15beba6f987b96256f`, executable SHA-256
`4c01af17b6e9f0fc4112e9f61442b90868b988fb6baf17d1dc8a336609367dc0`.
The private process exited. These observations prove the old Windows loss;
Mac's handling of these three new derivatives is currently source evidence,
not a new native Mac execution.

## Correction and ownership

The Windows file decoder now uses the same absent-duration zero default as Mac.
Explicit null, boolean, negative, fractional and out-of-range values remain
subject to its existing strict integer validator. Shared native command
validation and `NativeMetadataIntegrity.inc` still reject a slide or native
operation that requires positive duration. NF/NR beat-duration validation and
legacy row-to-beat recovery remain unchanged.

No storage version, identity, opaque plugin state, API editing contract, Undo
operation or DSP timing changes. The canonical encoder still writes duration.
An untouched preserved file tree may retain its optional omission; either wire
representation must decode to the same musical command.

## Bounded qualification

Add `native-metadata-tests` to the already prepared recipe/sample build. Its
source coverage now removes duration independently from every command in the
rich model and every native operation: valid zero defaults preserve the whole
model; required durations still reject. Recovery must preserve the entire rich
model without warnings, source protection or loss. Existing exhaustive
wrong-type/null/range tests remain enabled.

Run the existing F01–F05 and API/codec conformance capture, adding
`--duration-defaults` to `editor/Tests/Conformance/native_roundtrip.py`.
The new shared `duration_defaults.py` checks the three exact F04 derivatives
through two saves and reopens. Every saved property tree must equal either the
original omitted-field input or F04 with that exact integer-zero field present.
It does not strip or normalize unrelated values. Unexpected warnings,
protection, lost commands, changed identities, opaque state or PCM fail the
check. It preserves originals and receipts. On Mac the existing fixture loader
uses private recovery; ordinary native Open remains a separate integration gate.

The new verifier's acceptance/rejection checks and existing roundtrip/fixture
loader safety tests passed: **8 Python tests, 16.551 seconds**, retained in
`bin/codec-duration-20261020/verifier.log`. This is evidence about the verifier,
not a native decoder pass.

The existing codec observation baseline is unchanged, including its invalid
missing-slide vector. Broader validation is triggered by any baseline drift,
unexpected warning, unsafe save, changed unrelated field or failure of native
metadata tests. The next candidate must also satisfy the recipe/sample gates;
this correction grants no exception to the one-build-per-hour limit. Repeat
the new duration capture on Mac and Windows x64 in the final integration gate,
including reciprocal saves of the resulting files.

Both platform CI workflows now explicitly request `--duration-defaults` in their
existing conformance step. Their existing Python discovery also includes the
new verifier safety tests. This is scheduling source, not a CI execution.
