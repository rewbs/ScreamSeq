# Clock and file recovery candidate

## Current result

Candidate `28d757f65c66b970a5b57599984c60a51ac69f85` built successfully on
Windows ARM64. **19/20 selected CTest registrations passed**; the snapshot
registration passed 48/49 internal cases and failed its new malformed-NaN
archive assertion. All 21 actual-application codec safety cases passed, and
four additional integral-real project derivatives passed exact canonical
save/reopen comparisons. This is bounded evidence, not completed parity.

The qualification checkout is
`codex/windows-parity-integration`, at the exact candidate with a clean tree.
It includes main `07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`.
The candidate was pushed once to `codex/plugin-edit-parity`; [PR #5](https://github.com/rewbs/ScreamSeq/pull/5)
remains draft. Newer local test/documentation changes are not part of this binary.

## Build and targeted native checks

One configure/build invocation, two workers, **10:04:38–10:08:46 UTC**.
Output is the separate `bin/windows-parity-p1/ClockRecovery-Release` directory
in the qualification checkout; the preceding ExportCapture executable remains.
Executable SHA-256:
`f2a00c3b6707b286cb6f45c57b4ce4b7524c69fa9204592f80b5a1d4c09dc0f8`.
BuildInfo SHA-256:
`6abc195ea001faa5d54b854d4025100c3aa9cc11d77d156b5713626b12472c9c`.

Checks ran once at 10:09:15–10:09:42 UTC, 26.58 seconds. Passing groups cover
native metadata, native audio buses, vendor edit notifications, parameter
preview, recorded automation/frame clocks, live parameters, hosted projects,
typed project preservation, binary plist, Windows save, controller recording,
recovery/live publication/module export, and API tests. Required scratch setup
registrations are included in the 20-registration count.

The notification check establishes original callback timestamps through a
delayed drain, compatibility of the old overload, exact 8,192-entry order and
loss reporting, invalid-value refusal, audible/fault PCM and host C++ callback
allocation/free checks. It uses the redistributable VST3 fixture, not physical
audio hardware or a commercial vendor. The recording checks cover the shared
clock and Windows callback adapter; Windows manual-gesture ingestion and the
Mac poll-time replacement remain unimplemented.

### Snapshot failure and prepared correction

`projectIntegerRecovery` passed strict admission, complete canonical save/reopen,
opaque state, unknown-real preservation and earlier malformed values. It then
failed the archive equality assertion for the NaN case. The retained malformed
input's last plugin `type` is confirmed NaN; its SHA-256 is
`c7fd6718a0cfc74bb1881ff7182da27bb2f3986908b74f936a1387bb66d40e47`.
The bundled JSON comparator uses ordinary floating-point equality, under which
NaN differs from itself.

The prepared correction in
`windows/Tests/Snapshot/ProjectIntegerRecoveryChecks.inc` compares encoded plist
bytes of both complete plugin arrays. It therefore requires the rejected value's
representation to survive, rather than skipping NaN or relaxing preservation.
**This corrected assertion is unbuilt and unexecuted.** Later malformed recorded
frame assertions in that case were not reached. The original failure remains
in `native.log` and `native.xml`; do not count it as passed.

## Actual application checks

Both probes used owned, never-switched private desktops and inspection mode.
They do not establish foreground ergonomics, DPI/UIA, physical input or audible
device behavior. Owned processes exited; no musician process was replaced.

The first run completed all 21 codec observations and safety checks. Its new
four-case stage then failed during fixture preparation: `read_project` returns
a duplicate-key-rejecting dictionary, and the probe tried to replace a key
directly. No four-case application request had been issued.

The follow-up used the existing recursive `editable_copy` helper. It reused the
completed codec report with verified source/executable hashes instead of rerunning
it, then passed these F04 derivatives:

| Derivative | Required result |
| --- | --- |
| First plugin `type` represented as an integral real | Full canonical tree, including every plugin and opaque state, unchanged |
| First plugin `instrument` represented as an integral real | Same complete preservation |
| Selected `sequence` represented as an integral real | Exact selected sequence and complete tree |
| Recorded point frame represented as `0.0` | Exact canonical recorded point and complete tree |

Each opens without warnings/source protection, saves, reopens, resaves and
compares the complete typed tree to the expected canonical fixture. Both the
supplied original and each derivative remain unchanged. Follow-up report SHA-256:
`221bff70578a4453fbbb19c9dae0d343e5c655f2c94ed7f1f76c5987edaef8b1`.
These exact new derivatives have not yet run on Mac.

### Codec baseline differences: reviewed locally, not updated

Nineteen of 21 Windows observations are unchanged. The two differences are:

- `integer-real-next-id`: now retains the native musical sections, canonicalizes
  only that field, and needs no loss warnings/source protection. Its observation
  exactly matches the already verified Mac baseline. The old Windows result
  had 68 typed differences; the new input-to-save comparison has one.
- `fractional-next-id`: still rejects the identity layer, preserves its archive
  and protects the source. Comparing old and new complete saved trees finds
  only `/screamseqLoadRecovery/warnings/0`: “Expected metadata integer” becomes
  “Metadata integer out of range”. No musical data or admission policy changes.

The actual report still says `baselineMatched:false`; no expectation was
updated. Pair with the new x64 observations before accepting a baseline change.
Codec report SHA-256:
`badab593641070ebd42afd4852ae78fbd0b961d84bb01016439c8c4071599c56`.
Local comparison receipt SHA-256:
`16852fb0872c1077ba5aa3d93057aeb1d06b2116beb6e98c0e36a0ac2cc065ab`.

## Retained evidence and next gate

Local evidence is under `bin/capture-recovery-20261020/`: `build.json`,
`BuildInfo.json`, configure/build logs, `checks.json`, native XML/log, initial
`actual/`, `actual-followup/`, both harnesses/logs, the failed snapshot input
and `codec-local-review.json`. Earlier reports/scripts remain unmodified.

At **10:13:12 UTC**, all three new CI jobs were still building:

| Run | Job | Platform |
| --- | --- | --- |
| 39405368407 | 118569879460 | Windows x64 |
| 39405368725 | 118569880836 | Mac Apple Silicon |
| 39405368725 | 118569881392 | Mac Intel |

Poll these handles; do not redispatch. Collect native results, actual API/codec
and edited-file contracts, and exact build-completion timestamps. Confirm Mac
stable-ID reads and opaque UID preservation from this binary. Perform the final
reciprocal fixture gate using retained binaries after failures are resolved.
The previous successful exchange qualifies the preceding candidate only.

No new local build before **11:09 UTC**; later CI completion extends the
coordinated boundary. Group the snapshot correction with any further proven
failures. Reuse unchanged passing evidence and keep device/foreground gates open.
