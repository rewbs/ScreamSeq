# Reviewed codec observations and remaining differences

This is a review of retained binaries built from
`d79321c7ccf59e7fdf0a4b2918d6906b41097ef4`, not new native execution.
Both Mac architectures have identical observations for all 21 cases. Original
reports still say `baselineMatched:false`; they have not been rewritten.
Artifact provenance is in [paired API review](PAIRED-API-REVIEW-2026-10-20.md).

## Exact Mac expectation changes

The 13 changed cases in `doc/api/codec-observations.json` were individually
classified. Admission, source-protection flags, input hashes and all unchanged
cases remain exactly as before. The Windows baseline is unchanged.

| Cases | Verified reason for changed hashes/warnings |
| --- | --- |
| unknown-root, unknown-native, unknown-track, unknown-node | Saved typed tree now equals the complete input exactly; the previous expectation discarded the extension. Difference count changes from one to zero. |
| duplicate-track-id, deleted-track-record | Original version/native/plugins/automation/sequence are added as exact inactive recovery data. The prior active saved tree is unchanged. |
| precise-zero-effect-parameter, missing-non-nudge-duration, missing-scratch-library | Same recovery decision and active tree; exact original sections are now retained. |
| boolean-next-id, fractional-next-id, unsafe-next-id | Same rejection/recovery of invalid identities, with more precise warnings and exact original-section retention. |
| invalid-ascii-string | Foundation's accepted model is still protected. The complete original binary is now retained exactly, plus an empty section archive; the prior active tree is unchanged. |

For the eight section-archive cases, removing only the new archive **in an
in-memory explanatory comparison** reproduces the previous expected typed hash
exactly. Every archived section is separately checked against the original
input. The invalid-ASCII case similarly reproduces its previous active hash
after removing only the two archive fields, and its raw original is byte-exact.
The real comparator still includes every archive, field and numeric type.
No assertions, cases or input fixtures were removed.

Review helper and derived receipt:
`bin/export-capture-20261020/review-codec-observations.py` and
`codec-observation-review.json`. The helper verifies all 63 original case
inputs/outputs and report hashes, original-fixture identity, source protection,
unchanged Windows observations, exact allowed expectation keys and paired Mac
results. It changes the baseline only after these assertions pass.

- Apple Silicon codec report SHA-256:
  `00b2f5e7b0fbec57a42e6cbde5de346ec56471a7018ba4dd9c619c3791050269`
- Intel codec report:
  `18796984125b5c4b46f2b62ffc3c85800f98485107291b0c442f512f62ef8cd9`
- Retained Windows codec report:
  `c6572a6580fa020c07231f43cffbc577df2320dc5a80f011bd1d8ec836a89bb6`
- Derived review:
  `ebbcb5870c4d665f88c1236c6469fb39d2b2702c13d1056f91b09fe480bf59ee`
- Baseline before/after:
  `3216e9f4e144e9e7dbe513bed25d6663ff6f434ef9bca797cc4a322cb35a7177` /
  `29f8e8742a6a5707fbce1f6c7d5dd1188cfb38cf4c7eb938b76160124fa7414b`

## Cross-platform differences are not waived

| Cases | Current evidence and next boundary |
| --- | --- |
| Four unknown-extension cases | Exact saved trees match. Mac additionally protects unknown-root sources; warning policy differs. `TrackerSession.mm::decodeProject` and Windows `NativeProjectRecovery.inc` own this policy difference. |
| reordered-tracks, supplementary-unicode, missing-au, missing-vst3 | Exact saved trees and observed classifications match. Missing-provider playback remains a separate gate. |
| future-container, future-native | Exact saved trees and protected-copy behavior match; warning wording differs. |
| duplicate/deleted identity and boolean/fractional/unsafe next ID | Both protect the source and retain inactive originals. Mac additionally materializes previously empty built-in state blobs; this already occurred in its historical expected active tree. Windows input states were empty and remained empty—no observed Windows opaque-state loss. Windows also archives diagnostic strings. This is not proof of equivalent recovered audio. |
| Three optional-record recovery cases above | Saved active trees match; only Windows's archived diagnostic strings differ. Both retain the original sections. |
| integer-real-next-id | Real musical gap: frozen Windows deactivates native music. The scoped [file-integer fix](FILE-INTEGER-RECOVERY-2026-10-20.md) is unbuilt; leave its old Windows baseline until actual qualification. |
| corrupt-required-module | Both reject and preserve the previously loaded document. |
| invalid-ascii-string | Windows rejects without replacing the document; Mac recovers a protected model and retains the exact original file. This is an intentional recorded difference, not parser convergence. |

This review removes obsolete loss expectations; it does not complete codec
parity. Next native qualification must execute the pending Mac opaque-value and
Windows integer fixes, retain new failures and inspect actual typed deltas before
changing further expectations. Broader fixture exchange remains the final
same-candidate gate.
