# Plugin API inventory correction

## Observed failure

Mac Apple Silicon job **119647128083**, run **39725114919**, is terminal
failure. Its app build, **134/134 native tests**, **35/35 interface groups**,
Swift recovery/picker and sample-library checks passed. The eight editing
contracts and five original F01–F05 no-edit save/reopen checks also passed.
The next API stage stopped with `Shared schema inventory changed`; later
codec, duration, edited-metadata, composing-history and bank gates did not run.
This is not a passing integration or reciprocal gate.

The artifact `screamseq-macos-macos-15-logs`, ID **12403702098**, was downloaded
and its ZIP hash verified against the upload log:
`47c78b25728301ec938d9af364bd41e0436312e69977825d20609616db546f47`.
The local evidence is under
`bin/plugin-control-review-20261022/ci/mac-arm-bcd/`.

Its compiled CI merge source is
`1f600420a3b59e9ec6b779c6a36e173ca73a69bf` for PR head `bcd10d8a8`;
executable SHA256 is
`a3552d072959f9d0e2055bc20457e2cc72bd8471157334bc31c2d68c0c9ebb31`.
The failed `parity-conformance/api/report.json` has SHA256
`c6f2c3f58071654e35180a16269bd3dd510ad9526d8af7acbe43ebe0bea10aa5`.
Its inventory was captured before the assertion and is valid bounded evidence;
its unexecuted vectors are not passes.

## Reviewed correction

The schema delta is exactly `automation.recovery.get` and
`automation.recovery.list`. The observed Mac read delta is those same two
methods; no Mac writes or removals changed. These are implemented by
`mac/Bridge/PluginRecoveryReviewAPI.inc` and advertised by
`mac/Bridge/TrackerSessionAPI.inc`.

Windows `Session/PluginOperations.cpp` additionally advertises two recording
reads (`get`, `events`) and five writes (`start`, `capture`, `stop`, `commit`,
`discard`) under `automation.recording`. Those seven methods remain
Windows-only, outside the shared schema. They are now explicitly recorded as
`API-NATIVE-PARAMETER-TAKE`; matching that expected difference cannot establish
parity. Windows actual-app inventory confirmation still requires the startup
correction build. The existing `document.exportModule` write was already in
the expected Windows write list, so its contradictory derived missing-method
entry is removed; no method expectation is changed by that correction.

`doc/api/platform-differences.json` updates only these reviewed expectations,
the schema hash, derived method sets and provenance. Existing vector outcomes
are unchanged. Thirteen new vectors exercise recovery read admission, strict
integer/type/range/unknown-key validation, stale/empty revisions, document
preservation, and the explicit Windows-only take-read difference. Actual
archive contents, original timestamps and save/reopen remain covered by the
separate native recovery tests and edited-metadata journey.

Three lightweight `test_api_vectors.py` checks now catch a stale schema hash or
method list, inconsistent derived platform deltas, and unreviewed runtime
inventory before any document access. They preserve the strict failure and
incomplete evidence receipt. All three passed locally in **3.554 seconds**;
no build or application launch was performed for that check.

Both full-build workflows run these cheap checks before compiling. The normal
native/socket/pipe gates remain intact. This does not relax the product-source
or identical-source requirements of retained reciprocal qualification.

## Next validation

Include this tooling correction with the already prepared menu/MIDI correction
at the next coordinated checkpoint, **not before 05:56:14 UTC**. Freeze and
update the prepared script's commit before launch. Do not rerun the terminal
`correction` build/check receipts. Windows x64 and Mac Intel jobs from the
preceding candidate were still running at this review; retain their own results
and do not restart them.

After the corrected candidate passes, require both native platform inventories,
all new actual-transport vectors and the remaining typed fixture journeys.
Then run the exact-source Windows → both Macs → Windows extended exchange on
the retained binaries without rebuilding. Preserve the musician's session,
and perform the separate Windows foreground/keyboard gate before packaging.

## Retained Apple Silicon result — no build

Run **39728770038**, job **119658717283**, passed at 05:35 UTC using the exact
Apple Silicon executable above. The full-build and interface-build jobs were
skipped; only the retained-app job ran. Temporary tooling branch
`codex/plugin-recovery-retained-review`, commit
`b208d2845c6206bc0ea0f98510efb4a44e4fa0ca`, starts at the exact compiled CI
merge source. Its only differences are the inventory, vectors, verifier tests
and this report. `reuse_native.source_delta` independently accepted that
tooling-only delta before dispatch. No product change or relaxed source guard
was used to reuse this binary.

The downloaded artifact `screamseq-retained-macos-15`, ID **12403414294**, matches
SHA256 `6ac37b25227a6fdadc759220221f2582437683894badcabdf117d99b70a07157`.
Local root: `bin/plugin-control-review-20261022/ci/mac-arm-retained-review/`.
Its `retained-result/retained-run.json` and `baseline/report.json` confirm:

- Five F01–F05 original fixture save/reopen checks passed, with unchanged
  source fixtures, original corpus and executable.
- **41 API cases** matched the reviewed expectations, with zero document
  preservation failures and exact history/replay/save checks. The seven
  Windows-only recording methods remain an explicit unresolved API difference.
- **21 codec cases** matched the retained baseline and all safety checks.
- Baseline report SHA256:
  `1e9073565008e085db01d70f6b7c296ca0d30768d1b8d68ac9c6b7e302031a0b`.
- API report SHA256:
  `cd8d6588be6a79b3a0b338e763931a89ef603effb1ede55e58ecf2f603bf4148`.
- Codec report SHA256:
  `f931bb9a8a811ead75acf668657cafd6731737663f45a41ff7372e089a1537d9`.

This default retained baseline does **not** run duration defaults, edited
metadata, composing history or envelope-bank journeys, nor reciprocal exchange.
Those gates remain open. It also is not Windows foreground qualification.
Four guard vectors in the next candidate use the valid recovery kind `gestures`
instead of `gesture`, isolating their intended revision/unknown-key checks;
that exact vector text still needs the next native run. Earlier outcomes and
preservation assertions are unchanged. Keep this receipt tied to its recorded
vector hash rather than claiming execution of subsequently edited test input.
