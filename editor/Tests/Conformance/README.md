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
This runner does not yet implement edited legs, provider fixtures, audio comparison,
or warning-classification/API differential vectors; these remain distinct gates.
