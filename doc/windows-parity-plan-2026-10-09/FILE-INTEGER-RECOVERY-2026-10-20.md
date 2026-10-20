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
