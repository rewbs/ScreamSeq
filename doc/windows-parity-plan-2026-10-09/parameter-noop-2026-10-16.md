# Windows saved-parameter no-op — 16 October 2026

The original F04 fixture has an empty saved state for its first built-in plugin.
Assigning its already-current Enabled parameter previously captured 75 bytes of
default state, advanced the revision and created an Undo entry. The new real
PID-pipe regression fails against the identified d11 x64 app at `wouldChange`.

Source checkpoint `adce8a486a554c41078b7f126c2caee5e6c59eea` compares requested parameters with the
disposable processor's saved manual baseline at API float precision. It validates
the complete batch before returning an unchanged result; invalid or duplicate
later entries still reject. Only changed entries are applied/published. An
unchanged batch never captures opaque state or records a parameter touch. The
existing Mac API already skips unchanged manual values. No schema, project
format, shared DSP or Mac source changed.

## Executed qualification

One consolidated ARM64 Release build ran **04:49:32–04:50:12 UTC**, after the prior
03:49:31 completion. App and worker targets built without compiler warnings.
All **1,919 captured inputs** stayed unchanged across build and tests.

| Check | Result |
|---|---|
| Native CTest entries | **6/6** across the initial run and the missing-target repair; includes one scratch setup and five test entries |
| Actual app PID-pipe/private-desktop cases | **5/5**, 7.351 seconds, no skips |
| Original F01–F05 typed save/reopen legs | **5/5**, original inputs unchanged |
| Actual API corpus | **28/28 baseline matched; zero preservation failures** |

The initial native run had five passing entries and `live-parameter-tests` **Not
Run**, because its target was omitted from the build list. A 13-second build of
that missing target and its focused 0.33-second CTest run passed. No product
source/executable or assertion changed; the initial failure and both repair logs
remain retained. This is a corrected setup failure, not a first-run 6/6 claim.

The worker regression starts from an imported empty state and verifies no change
to saved bytes, immutable view/history, publication count or transport stop count;
it also checks invalid later entries and no-op after Undo retaining Redo. Existing
live batch, queue overflow, failed publication, chronological history, parameter
activity and save/reopen tests remain. The application regression uses the exact
original F04 and verifies cold/dry no-op, duplicate/invalid rejection, unchanged
saved property trees, a real edit and Undo/no-op/Redo. Neighboring app tests cover
native parameter drafts and short silent WASAPI live playback.

ARM64 executable SHA-256: `74226eaa92656b43cfccaef088819795e926231bf95a696ebf9daf5dd48f416e`.
Worker SHA-256: `851e1bc533d3829896764c8cb6811d388c8e09052d0632656aecad16014a2843`.
The missing hosted test SHA-256 and every log/receipt hash are in
[the receipt](parameter-noop-2026-10-16.json). Raw evidence is retained under
`C:\Users\P14\Documents\Codex\2026-10-15\task\windows-parameter-noop-20261016`.
The conformance runner retained its exact build-receipt snapshot alongside its
reports. Documentation changes following qualification do not change binaries.

## Remaining gates

`API-PLUGIN-NOOP` is corrected and qualified on Windows ARM64. The stricter vector
now requires document/history preservation on both platforms. The corpus still
records six other known API contracts: graph gesture, integer types, order move,
plugin dry-run, plugin position and rejected-request-ID replay. Overall parity
remains false; existing codec differences are not normalized or waived.

No current-fix Windows x64 build, current-candidate reciprocal exchange,
foreground computer-use screenshot, sustained visual/audio qualification or
physical device validation is claimed. Native computer-use tools remain
unavailable. The short silent host test is functional evidence only. The
musician's PID 5032 and the separate integration worktree were preserved.

The [read-only integration audit](integration-audit-2026-10-16.md) identifies
already-implemented P1–P5 work, source overlap, historical evidence and acceptance
gates. That branch was not merged. The fix/evidence remain local pending the
parent's outstanding publication approval; no publication retry was attempted.
