# Clock/recovery CI review and exact parameter history repair

## Candidate and evidence

The qualified candidate is `28d757f65c66b970a5b57599984c60a51ac69f85`.
Both CI applications identify PR merge
`dc0479ec466015280566ba7b9341daf865c03730`; fetching that exact commit and
comparing its tree with the candidate produced no differences. The subsequent
snapshot assertion correction, recorded-clear changes and history repair below
are outside these binaries.

Windows run [39405368407](https://github.com/rewbs/ScreamSeq/actions/runs/39405368407),
job `118569879460`, completed with failures:

- Build succeeded; last build output was at **10:22:47.252 UTC**.
- Portable/worker/API: **156/157 registrations passed**, 81.30 seconds. The
  snapshot executable passed 48/49 internal cases; its sole failure is the
  already diagnosed NaN archive equality assertion. The byte-exact assertion
  correction remains unbuilt.
- Scratch: three actual-app private-desktop cases passed.
- Native editors/preferences/recovery: **48/48 passed**, 101.38 seconds.
- Integration: 84 cases, 13 failure records across 10 cases, two skips.
  Transport/recording failures could not establish a device/clock. Three
  audition cases also failed their cleanup assertions after their QA process
  exited; the shortcut playback case exited too. The retained audio probe
  reports an empty endpoint list, zero callbacks and an invalid recording clock.
  These remain failures with a missing-device environment; they do not qualify
  device behavior, and an empty endpoint probe alone does not prove every
  process-exit cause.
- F01–F05 exact save/reopen, all three duration-default derivatives, edited
  metadata, six-domain composing history and linked envelope-bank checks passed.
  All 28 API observations matched, with no preservation failures.
- All 21 codec safety observations completed; only the two reviewed Windows
  expectations below differed. The original report remains
  `baselineMatched:false` and the overall run remains failed.

The Windows executable SHA-256 is
`5401656f964672276b66ee65175bb9b5eb1515e7341b2e3bdef75aae54b3e0c8`.
Logs artifact `12258350335` was downloaded, verified against GitHub's digest
`f8c5aa6e9eed1f5407e007a90b75fada5e593b5bb6c8fdf30517a6f260fbe5c9`,
and extracted only after checking every resolved path stayed inside the
destination. Evidence is in `bin/capture-recovery-20261020/windows-ci/`.

Mac run [39405368725](https://github.com/rewbs/ScreamSeq/actions/runs/39405368725),
Apple Silicon job `118569880836`, completed with a composing-history failure:

- Build, 134 native tests, Swift recovery/picker, sample-library and 35 interface
  groups passed. All seven existing editing contracts passed.
- F01–F05, duration-default derivatives, 28 API observations, all 21 codec
  observations and edited metadata passed.
- Composing history failed at `undo-plugin`; envelope-bank qualification after
  that failure did not execute.

The Mac executable SHA-256 is
`2251833de7ff40f939886791e7cc8e82229c1afbb47b812a45c88a3cb9649f60`.
Logs artifact `12258002716`, verified ZIP SHA-256
`f90f9c121f0c9b31eec40e256c13ea746f404a7a55f63b90f25ee9dfa930e7cf`,
is retained in `bin/capture-recovery-20261020/mac-arm-ci/`.
Intel job **118569881392 was still building at the latest poll during this review**;
poll that exact handle, do not dispatch another build.

## Two Windows codec expectations reconciled

The ARM64 and x64 observations match exactly on all 21 cases. For the two
changed cases their complete saved typed trees also match exactly.

1. `integer-real-next-id` now retains native music, requires no loss warning or
   source protection, and matches the verified Mac observation. The only input
   difference is numeric representation canonicalization; the previous Windows
   result had 68 differences from loss/recovery handling.
2. `fractional-next-id` still rejects the identity layer, archives it and
   protects the source. Comparing complete historical/new saved trees changes
   only `/screamseqLoadRecovery/warnings/0`, from “Expected metadata integer” to
   “Metadata integer out of range”.

Only those two Windows expectations in `doc/api/codec-observations.json` were
updated. The other 19 cases, all Mac expectations, input identities, comparator,
rejection policy and historical result files remain unchanged. This does not
turn the failed historical run green.

Paired review receipt `codec-paired-review.json` SHA-256:
`8bc490fc9138720f3732e93f774c4048b7c4aba90345e1a530ec01079f8e8728`.
ARM64 codec report SHA-256:
`badab593641070ebd42afd4852ae78fbd0b961d84bb01016439c8c4071599c56`.
x64 codec report SHA-256:
`e4648548b1fa6d135c431485ed8634b500d0dcf82072348276c5f0a313a1e067`.

## Confirmed Mac Undo defect and prepared repair

The unchanged F04 composing check compares the complete saved tree after each
edit, Undo and Redo. The failed `undo-plugin` comparison has exactly one
difference: `/plugins/0/state`. Its original empty blob becomes a 75-byte
default Gainer state, SHA-256
`3796b14339cc9d2841696f21001eb9a9c9121968a239e3377971c0b7bcf6b993`.
The expected musical value is restored, but the saved representation is not.

`mac/Bridge/TrackerSession.mm::restoreEffectHistory` previously replayed the
historical scalar into the current pending manual vector. A later Save then
synthesized a state blob from the current cache, losing the captured empty
representation. The prepared repair stages the exact saved plugin cache and
pending manual vector, validates the entire rack's ordered identities and
non-state fields, and swaps those caches only after successful atomic scalar
publication. Redo captures the current exact caches symmetrically.

The live processor still receives the existing numeric parameter batch. It is
not replaced or stopped; its tail, automation, custom interface and modulation
owners remain intact. Allocation and validation precede publication; queue
refusal cannot adopt the staged cache or history entry. There is no format,
public API or Windows runtime change in this repair.

`mac/Tests/UnifiedHistoryTests.mm::exactParameterHistory` adds a stopped
regression with an imported empty Gainer blob, a second nonempty processor,
pattern automation, opaque typed extensions, interleaved note edits, saves
between Undo/Redo, pending values folded by a later save, and final save/reopen.
It compares full typed trees using `sameStoredValue`, including numeric types
and exact floating-point bits. The existing F04 comparator is unchanged.
The existing device history test covers scalar Undo/Redo during actual playback
after a Save with active root modulation.

**The repair and new native regression have not been built or run.** Source
review and `git diff --check` are the only checks on this C++/Objective-C++ edit.

## Next bounded gate

Keep the current Intel job running. The next coordinated native build must be
no earlier than **11:23 UTC**, extended again by Intel's eventual build
completion. Do not push while the build is active and accidentally replace it.
Batch this repair with the NaN assertion correction and recorded-clear work.

For Mac, run unified history (including the new exact-state case), parameter
activity/preview and the recorded-automation host targets, followed by the
actual-app editing contracts and full F04 composing/envelope-bank journeys.
The device history/capture paths need an available explicitly selected test
endpoint; their absence is not a pass. For Windows, rebuild the affected shared
recorded-automation, controller, snapshot and application targets together;
run their targeted native tests and recorded-clear UI/API contract.

Reuse the unchanged candidates' passing UI, codec, fixture and pure-model
evidence only where relevant inputs remain unchanged. Cache/history changes
invalidate the affected Mac Undo/Redo and saved-plugin checks. Complete the
new-candidate reciprocal fixture exchange, cross-platform integration gate and
remaining A01–A18 requirements before merging/claiming parity. Foreground
visual/DPI/accessibility and physical device/vendor checks remain separate.
