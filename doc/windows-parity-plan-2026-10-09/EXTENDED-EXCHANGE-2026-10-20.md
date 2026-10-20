# Extend reciprocal validation to edited native projects

## Confirmed qualification gap and change

`editor/Tests/Conformance/reuse_native.py` previously exchanged only the five
pinned fixtures and the native rendered/imported sample outputs. The newer
opaque-metadata, mixed-history and envelope-bank journeys already emit a
verified `exchangeInput`, but the reciprocal runner did not consume it.
A seven-file exchange could therefore pass without proving that the other
platform preserved these newer edited files.

The opt-in `--extended-exchange` now requires five named legs, ten files total:

| Leg | Required files |
| --- | --- |
| `no-edit` | F01–F05 |
| `edited` | `rendered.screamseq`, `imported.screamseq` |
| `metadata` | `opaque-reopened.screamseq`, after opaque extensions, title/graph edits, rack move/remove and history |
| `history` | `reopened.screamseq`, after mixed note/sample/envelope/plugin/graph/send history |
| `envelope-bank` | `reopened.screamseq`, after linked/independent uses, master edits, unlink and history |

The Windows input leg requires the parent report's successful journey marker
and report hash, the expected journey format, original F04 hash, unchanged
inputs/outputs, exact final comparison, saved byte hash and typed fingerprint.
It rejects a clean or intermediate file substituted for the final opaque file.
It does not reconstruct or normalize an expected song.

Extended runs write `screamseq-retained-run-v2`. Return legs require every
named leg, its successful exit, matching report hash, same frozen product
source and original input fingerprints. Filename hashes are grouped by leg,
so the two different `reopened.screamseq` files cannot overwrite or substitute
for one another. The existing exact typed native save/reopen comparator runs
on every supplied file.

Legacy v1 evidence remains readable with the legacy mode. It cannot satisfy
an explicitly requested extended gate. A failed editing journey is a failure
to supply the new gate, not permission to omit that leg.

## Bounded checks executed

The eight `test_reuse_native.py` cases passed in 20.986 seconds. New coverage
includes a complete extended outward/return artifact, refusal of legacy
downgrade, ten failed/missing/tampered/uncompared input variations and refusal
to run without a preceding artifact. After adding the per-leg input binding,
the affected complete exchange test passed again in 6.103 seconds, including
rejection of swapped history/envelope-bank fingerprints.

Read-only admission was also exercised against real retained artifacts:

- `bin/capture-recovery-20261020/windows-ci` supplied all ten expected files.
  Its built source is `dc0479ec466015280566ba7b9341daf865c03730`
  (the preceding PR merge snapshot), executable
  `5401656f964672276b66ee65175bb9b5eb1515e7341b2e3bdef75aae54b3e0c8`.
- Both preceding Mac artifact directories were correctly refused for their
  incomplete composing-history journeys.

These are tooling/admission checks. No application was launched, no original
artifact changed, and no native product was built. They do not establish a new
reciprocal pass. The current candidate must still execute the exchange.

### Current Mac artifact admission

The completed Apple Silicon artifact at source
`e4f36a1f1c84dbaa0995dd7ad74843e0f870d149` now supplies all three successful
edited journeys. Its composing report is 21,341,767 bytes and its bank report
17,538,532 bytes. Their complete request/reply audits exceed the original
16-MiB small-receipt parser limit.

Only hash-verified journey reports now use an explicit **32-MiB** read bound;
other receipts retain 16 MiB. The checker still requires the full original
source, final-save byte/typed fingerprints, comparison and preservation evidence.
No assertions or reports are dropped. All eight tooling tests passed again
in 20.511 seconds, and all three real current Mac journey inputs passed
admission. The verified receipt is retained at
`bin/gesture-batch-20261020/mac-arm-conformance-verification.json`.

## Run with retained applications

Both native workflow dispatch forms now accept `extended_exchange: true`;
they forward it to the existing build-free reusable retained workflow.
Always supply `retained_app_run`, `fixture_run`, the exact `fixture_artifact`
and the intended `fixture_layout`. An empty retained app selector requests a
product build and is not an exchange invocation.

For the current candidate, once its required artifacts exist:

1. Use Mac app run `39418201597`, each architecture's exact logs/app artifact,
   and Windows fixture run `39418201506` / `screamseq-windows-logs`,
   layout `windows`, extended exchange enabled.
2. Use Windows app run `39418201506` and each resulting
   `screamseq-retained-macos-15` or `screamseq-retained-macos-15-intel` artifact,
   layout `exchange`, extended exchange enabled.
3. Preserve all report/identity hashes and compare final Windows trees with
   the originating ten files. Keep both Mac architectures distinct.

A temporary tooling branch may carry these workflow/Python/documentation-only
changes while the candidate PR build runs. The runner rejects product/build
source differences from the retained app. Do not push this update to the
active PR merely to make the workflow available and accidentally start or
replace its native build batch. Integrate the tooling with that same shared
main candidate afterward.

This closes a hole in the planned validation scope. Gesture controls/recovery,
foreground ergonomics, physical device/vendor qualification and the remaining
A01–A18 gates are still independent outstanding work.
# Protected launch follow-up

The first current-candidate extended exchange is run `39423914064`, using
retained Mac apps from `39418201597` and Windows outputs from `39418201506`.
Apple Silicon passed the five original, two render/import, composing-history
and envelope-bank files. Its opaque-metadata leg failed at launch identity,
before any Save: `Owned app did not open the requested launch document`.
Artifact `12263879560` retains that failure; it is not a successful exchange.

`mac/App/main.swift::refreshPattern` deliberately clears `documentURL` when
`requiresSaveAs` is true. The session retains `loadSourcePath`. The runner now
accepts that exact protected-source identity only when the context file is
empty, the protection flag is Boolean true, and the loaded source exactly
equals the requested path. It records the observed launch identity, keeps
the isolated PID/socket checks, and still compares both complete saved trees.
This changes qualification tooling, not file admission or source protection.

Four runner safety tests passed in 5.345 seconds, including nine normal/
protected/wrong-source identity cases, uncertain-save refusal and binary/
output collision guards. A new retained-app execution is required; the
tooling check alone does not close the failed metadata leg.

