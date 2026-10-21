# Windows file integer representation recovery

Status: **source implemented, unbuilt/unexecuted**. This is a current codec
compatibility defect demonstrated by the same-source native reports, not an
inference from a dated progress checklist.

The `integer-real-next-id` F04 derivative uses an exactly integral real for
`native.nextID`. Mac retained all native music and saved a canonical integer.
Windows rejected the representation as “Expected metadata integer,” deactivated
the native sections, replaced active identities and emitted 68 typed changes.
Its original inactive metadata remained recoverable, but the active musical
behavior was not equivalent.

`windows/Project/NativeMetadata.cpp` now uses the existing shared
`Tracker::boundedUnsignedInteger(double, low, high)` **inside file recovery
only**. The same bounded conversion already backs Mac numeric validation and
the reconciled API numeric contract. It checks finite/integral/nonnegative values
and exact bounds before narrowing. Boolean/non-number inputs remain invalid.

`NativeMetadataRecovery.inc` activates a thread-local scoped import guard,
restoring its previous state on every return/exception. This is on the document
owner, never the audio callback. The canonical encoder normalizes only understood
fields; original unknown property values retain their typed preservation path.
Strict API/model decoding and canonical-save validation still require integer
storage. No project version, stable-ID allocation, plugin-state or audio changes.

`NativeRecoveryChecks.inc` adds a rich-model regression with integral-real
`nextID`, pattern index and track color. It requires the complete model and
canonical metadata to match exactly, without lost music or spurious recovery
protection. The canonical result must pass strict decoding; the original real
representation must still fail strict decoding after recovery. Boolean, negative,
fractional, unsafe/nonfinite inputs still trigger loss diagnostics and protect
their source, and must not leak permissive validation into later operations.

Include this in the next cohesive build, no earlier than the coordinated hourly
gate. Run native-metadata tests on Windows and the Mac-hosted Windows adapter,
then rerun the unchanged actual-app codec vector and compare the active canonical
tree to Mac. Recheck F04 opaque edited metadata and source protection because
the file-recovery path changed. Retained 54fa binaries cannot qualify this fix.
Do not update the Windows codec observation baseline until that execution proves
the intended difference and retains all malformed-input checks.

## Additional project-record defect confirmed without rebuilding

Four new probes used the unchanged retained CI x64 executable on a never-switched
private desktop (owned PID 30432, subsequently closed). Source, executable and
BuildInfo hashes were rechecked. `project-integer-probes/report.json` under
`bin/export-capture-20261020/` has SHA-256
`ef77367626729cbe23fd8bbb26bde3a8fcaba864956d692c38fcb1c3eb8dd242`.
`probe-project-integers.py` retains the exact inputs, requests and saved outputs.
This runtime evidence belongs to frozen merge `d79321c7c`, not the new source.

| F04 derivative | Frozen Windows result |
| --- | --- |
| First plugin `type: 0.0` | Dropped that plugin: 15 rack records became 14. |
| First plugin `instrument: 0.0` | Dropped that plugin: 15 became 14. |
| Selected sequence stored as integral real | Defaulted the selection and required a protected copy. |
| Recorded frame stored as `0.0` | Dropped the sole recorded point. |

Mac source uses bounded numeric conversion for plugin descriptors, assignments,
buses and absolute points; new paired native execution is still required for
these exact derivatives.

The Windows file-open adapter now canonicalizes only understood project integer
fields before the existing strict validators: sequence, plugin descriptor IDs,
assignment/bus fields, recorded slot/ID/frame, and recovery-take counters/events.
Integral current container/metadata versions avoid an unnecessary compatibility
warning. Invalid/nonfinite/fractional/negative/out-of-range fields still follow
the existing per-record recovery, archive and source-protection paths. This
uses `editor/IntegerValidation.hpp`; there is no global permissive decoder, opaque
state conversion or API relaxation.

`Snapshot/ProjectIntegerRecoveryChecks.inc` adds a normal-build check using F04,
an unavailable synth with opaque state and aliases, recorded automation and a
recovery take. It requires exact canonical encoded-tree equality, complete native
and core preservation, unknown real values remaining real, save/reopen, strict
captured-byte rejection per field, strict writer rejection, and protected
malformed-value recovery retaining unrelated plugins. Tests remain **unbuilt**.
Include `snapshot-tests` and the actual four probes in the next coordinated
batch, together with the native metadata checks. Do not rerun unchanged broad
suites just to qualify this file-only repair.
