# Typed project conformance

This P0c tool compares native property trees without changing them. It supplements
native load/save and actual API checks; it does not prove a project is playable,
that a frontend exposes its state, or that audio/device behavior is equivalent.

From the repository root, with Python 3.10 or newer:

```sh
python editor/Tests/Conformance/project_tree.py verify-corpus
python -m unittest discover -s editor/Tests/Conformance -p 'test_*.py' -v
python editor/Tests/Conformance/project_tree.py compare original.screamseq windows-save.screamseq
python editor/Tests/Conformance/project_tree.py compare windows-save.screamseq mac-save.screamseq
```

Comparison returns 0 only for typed equality, 1 for differences and 2 for an input
or evidence error. JSON output identifies differing property paths. A bounded
diagnostic list never converts omitted differences to a pass. Dictionary order is
irrelevant; array order is significant. Booleans, integers and reals are distinct;
integers retain exact magnitude, and reals compare IEEE-754 binary64 bits (including
signed zero). Strings retain exact Unicode code points. Binary module/plugin/sample
payloads compare exact length plus SHA-256. Unknown fields and stable IDs participate.
The tool does not equate differently encoded opaque payloads or excuse allocator
changes. In particular, no-edit saves have no `nextID` exception.

This is decoded property-tree equality, not container-byte equality: key order and
the plist container's choice of integer/real storage width are not compared. The
decoded real value is compared by bits. Original corpus files additionally have
byte-for-byte archive-member checks, independent of typed digests. Original Mac
runtime claims in FIXTURES.md remain historical provenance, not current passes.

The checked-in golden manifest covers all five original supplied projects. Creation
uses `record-corpus --golden NEW_PATH`; it refuses to overwrite an existing manifest.
Verification and comparison never update a golden or project. Review provenance and
typed changes explicitly before replacing any golden. Do not regenerate fixtures to
silence a save/reopen discrepancy.

For future reciprocal gates, retain the source SHA, executable/toolchain/provider
identities and logs for each leg. Start with immutable F01–F05, use owned Save As
destinations on Windows and Mac, compare each saved tree, and reopen the final files
on both platforms. Edited legs need their own expected change vectors. The documented
arrangement Undo allocator high-water difference is a specific history contract to
assert, not a general comparison exception. This tool intentionally offers no ignore
list or tolerance option.

`native_roundtrip.py` runs the actual application, using an explicit executable,
expected executable SHA-256, built commit and retained build receipt. It does not
build, choose an ambient instance or infer the executable's source from the tooling
checkout. Supply identities from the build being qualified, even when the harness
checkout has moved ahead. The receipt is copied intact; its provenance must be
reviewed, not inferred from a caller-supplied commit string.

```sh
python editor/Tests/Conformance/native_roundtrip.py \
  --executable /absolute/path/to/ScreamSeq \
  --binary-sha256 FULL_EXECUTABLE_SHA256 \
  --binary-source-commit FULL_BUILT_COMMIT \
  --build-receipt /absolute/path/to/build-receipt.json \
  --output /absolute/path/to/new-leg-directory
```

Use `ScreamSeq.exe` on Windows, or the actual `Contents/MacOS/ScreamSeq` executable
inside a separate qualification bundle on Mac. Windows uses a never-switched private
desktop and the launched PID's pipe. Mac uses prohibited-activation automation-test
mode and only the owned temporary discovery directory. Neither path enables audio,
uses global input, or closes another process. The Windows harness also asserts
unchanged clipboard and foreground HWND. Mac execution remains a native CI gate.

Each leg loads all five immutable corpus projects, saves to `first/`, reopens that
save and saves to `reopened/`. `report.json` retains exact RPC requests/results,
typed differences, input/output/executable/tool hashes and process identity. Input
and original corpus preservation are checked even after a failure. A failed or
uncertain write stops the run without retry; equality failures continue across the
five projects to provide a complete report. Existing output directories are refused.

For the reciprocal leg, transfer the preceding leg's entire evidence directory and
pass its `first/` directory as `--input-directory`; use a new output directory and
the receiving platform's actual build identities. Link the receiving input hashes
to the preceding `firstSave.sha256` records. Returning the Mac `first/` to Windows
completes the no-edit round trip. Keep every intermediate report, including failures.
This runner does not yet implement reciprocal edited legs, provider audio fixtures
or audio comparison; these remain distinct gates. Optional codec classification
vectors are described below.

Add `--api-vectors` to execute the shared pipe/socket API corpus after the five
no-edit legs. It uses an owned F04 document, validates the advertised method
inventory against `doc/api/platform-differences.json`, records exact response/error
envelopes and runs the common edit/history/persistence sequence. The additional
`api/report.json` distinguishes **baselineMatched** from **parityComplete**. Exit 0
means the no-edit legs and reviewed evidence baseline matched; it does not declare
API parity. Known differences, including preservation failures, remain release work.
The first current Mac inventory must be reviewed and pinned before its baseline can
match; historical reference data is not silently substituted for that native run.

The table covers boolean/integer/real distinctions, bounds, invalid fields, missing
and stale guards, UTF-16/NUL limits, plugin identity, absolute moves, dry-run/gesture
fields and rejected request-ID reuse. The edit sequence additionally requires whole
batch rejection, no-op history preservation, exact successful replay, rejection of
different content under a retained ID, single Undo/Redo, unrelated native/plugin
state preservation and actual save/reopen. Revision tokens remain opaque. A transport,
busy or uncertain internal error stops the run; it is not retried by this harness.
The existing Mac client retains its bounded same-ID protocol-busy retry behavior.

An observed no-op defect retains its changed project and full typed difference before
the runner explicitly reloads the immutable fixture in the owned app. This permits
independent probes to complete without declaring the defect fixed. A recorded defect
matches only its exact expected response, changed document paths and typed saved
payload difference. There is no general permission to ignore revisions, Undo entries,
plugin bytes or unknown fields. Update both vectors and the owned difference entry
when a domain implementation fixes it. Broader codec recovery vectors, raw malformed
wire inputs, cache bounds and full musical behavior remain separate coverage.

`--codec-vectors` additionally generates 21 owned F04 derivatives covering unknown
root/native/entity/node fields, reordered/duplicate/deleted identities, future
versions, absent sections/duration, numeric types/ranges, Unicode/invalid encoding,
required snapshot corruption and unavailable AU/VST3 state. It retains each input,
saved copy, actual load warnings, source-protection decision and exact typed changes.
Rejected Open must preserve the loaded document; a protected source must refuse
overwrite and remain byte-identical; canonical copies must reopen. Missing-provider
files must preserve their exact plugin records. No provider audio is started.

`doc/api/codec-observations.json` pins the reviewed Windows classifications and
saved typed hashes; Mac is unqualified until current native observations are reviewed.
`safetyPassed`, `baselineMatched` and `parityComplete` are separate fields. A recovered
file may have intentional, warned loss; an exact baseline match does not excuse an
unresolved platform discrepancy or qualify an edited reciprocal leg. The original
21-case report can be checked against `codec_vectors.observations(report, directory)`
without rerunning the application when its inputs are unchanged.

The precise-note effect-zero/parameter-nonzero vector is invalid in the current
shared model: `NativeNoteEffectSupported(CMD_NONE, parameter)` requires zero.
Windows recovery drops the incompatible event and protects its source. Preserve
this case as malformed-input evidence; differing encoder conditions alone do not
establish loss of a valid reachable edit.

`schedule_inventory.py --ctest-json REGISTRATIONS.json --platform windows|macos
--output NEW_REPORT.json` consumes `ctest --show-only=json-v1` without building or
running tests. It requires every configured CTest to be selected exactly once by
the workflow's declared groups. Separate CMake projects, matching executable names,
Python method declarations, explicit unittest selections and non-CTest workflow
commands are listed separately. Those source declarations are not runtime passes;
inherited cases and runtime skips are not inferred. Regenerate registrations after
CMake changes at the next build checkpoint, rather than calling a stale configuration
current evidence. CI retains registration and scheduling reports alongside results.

CI also retains each built native app (Windows zip / Mac tar.gz), attribution and
an identity manifest containing the actual built commit, executable hash, archive
hash and build-receipt hash. These are candidate binaries, not passing qualification
claims. Use those exact archived apps and retained `first/` songs for reciprocal
checks or tooling-only corrections; do not rebuild unchanged product inputs merely
to review a baseline. Preserve Mac executable permissions and bundle symlinks when
extracting its tar archive. Compare all identity hashes before launching an archive.
