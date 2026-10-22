# Reconcile the current API evidence

Read-only comparison of the three retained actual-transport reports at CI merge
`4c40b3e0b792f73953b71d2727dd8580548e696e` (tree equivalent to PR head
`7154b9f323def1c448a7948eb27b6933c6a40021`) finds **39 identical observed
outcomes across Windows x64, Mac ARM and Mac Intel**, out of 41 selected cases.
Every case matches its reviewed platform expectation and passes its preservation
assertion. All three reports also pass no-op persistence, unrelated persistence
and edit/history/replay persistence. This does not qualify the newer AU/input
lease source, all API methods, native UI or audio behavior.

The two differing cases are `parameter-take-read` and
`parameter-take-read-unknown-field`. Mac returns method-not-found; Windows has
the retained take API and its strict field validation. The seven Windows-only
recording methods still need the complete shared/Mac lifecycle integration.
These two probes are not coverage of all seven methods.

The old reports label integer handling, plugin dry runs and rejected-request
replay as open differences even though their selected actual outcomes now agree.
The already-prepared `api_vectors.py::contract_labels` correction classifies
equal reviewed contracts as historical regressions, preserving the vectors and
all runtime/preservation assertions. Do not infer complete API parity from this
reporting correction, or rewrite the original runtime reports.

## Traceable inputs

All paths below are beneath `bin/plugin-control-review-20261022/ci/`.

| Report | SHA-256 |
| --- | --- |
| `windows-x64-7154/extracted/bin/windows-ci/parity-conformance/api/report.json` | `5b1be25cf544d6f4df4808e9f9b8c8eb018463d7b4ec37a7ffef9f542b8e5afb` |
| `mac-arm-7154/extracted/bin/mac-ci/parity-conformance/api/report.json` | `bb7eb0fc65755cd1cce8cee34543864656ae2a7c909841dc9fd6f52d660157f1` |
| `mac-intel-7154/extracted/bin/mac-ci/parity-conformance/api/report.json` | `3e0d15eaf5a7f66c475685b7bc5bd314e30fb64b52c2194f70853a023429e80a` |

`bin/mac-gesture-20261022/compare-retained-api.py` checks unique case identities,
all outcomes and preservation flags, then emits
`retained-api-comparison.json`. Its first admission rejected unequal raw vector
hashes. Inspection established that the exact same pinned Git blob is checked
out as LF on Mac and CRLF on Windows. Admission now requires one of those two
exact byte encodings of the same unchanged Git blob; it does not accept arbitrary
JSON normalization, a changed vector corpus or a different expected outcome.

## Next checkpoint

The AU intake/input-lease batch remains source-only. The prepared
`bin/mac-gesture-20261022/build.ps1` enforces the hourly gate, exact frozen commit,
clean qualification checkout and a new output directory. It includes the app,
workspace probe, document-controller helper, native edit-notification and shared
recorded-automation targets in one build. `check.ps1` selects seven native
registrations and six actual-app cases covering recording, clear/history/reopen,
parameter preview and vendor restoration protection. Both scripts parse; neither
has run. The preceding `313893b9f` binaries and receipts remain intact.

Both native Mac architectures must execute the changed providers, shared queue
tests and owned-editor lifecycle check. Old Mac binaries cannot qualify changed
AU/input ownership merely because the project codec is unchanged. The final
ten-file reciprocal exchange and visible Windows UI/focus checks remain required.
Computer Use input remains stopped after the earlier access denials, pending the
user's unlock reply. No production package or merge is claimed.
