# Recipe, sample and context-panel candidate qualification

## Exact candidate

Windows ARM64 source `b1581c9c8bc7d776d637d8c6ac094e8c3db7780a` built
successfully from **04:50:00 through 04:53:33 UTC on 20 October**, with two jobs.
The qualification worktree remained frozen at that commit. Its seven targets
were ScreamSeq, workspace-restore-tests, document-controller-tests,
parameter-edit-tests, parameter-preview-tests, rack-move-tests and
native-metadata-tests.

Executable:
`bin/windows-parity-p1/RecipeSample-Release/ScreamSeq.exe` in the
`parity-ci-layout/ScreamSeq` worktree. SHA-256:
`ebc103df3c5ea20a86f6c7ee09e13e3d4d9fe43a10918fb987489b5548e5793e`.
Receipts, scripts and logs are in the primary worktree's
`bin/recipe-sample-20261020/`. No subsequent build before **05:53:34 UTC**
(10:53:34 p.m. Pacific on 19 October). The musician's existing executable was
not replaced.

The later Mac rack dry-run commit `02815039c` is outside this candidate.
Do not attribute its source or changed schema to these results.

## Native and actual-application results

**16 of 17 native groups passed in 193.05 seconds.**

| Area | Result |
| --- | --- |
| Context panel | Passed, 1.12 s. Corrected region/compact geometry fixture, visibility, focus, saved/legacy/malformed layouts and retained drafts. |
| Command results, parameter preview and recipe edits | All passed; 74.41, 4.91 and 4.82 s respectively. |
| Sample readback and mutation results | Passed; 77.14 and 20.19 s. Includes release strokes, loop handles, cancellation, keyboard nudges, history and retained failures. |
| Shared rack move and parameter edits | Both passed. |
| Shared parameter preview | Passed, including the documented recipe-copy PCM matrix and realtime allocation audit. |
| Worker recipe/rack/graph publication, live parameters, graph recipes and unified history | All passed. The required scratch fixture setup also passed. |
| Native metadata | Failed after 54 checks: the new omitted-duration test accessed a deliberately absent `duration` key. Later assertions in this executable did not run. |

The metadata failure is a test defect, not a green group. The existing
`windows/Project/NativePatternJSON.hpp::append` deliberately removes the row-unit
`duration` field for NF/NR and emits `durationBeats`. The test added in `49e1f3fb0`
assumed every encoded command had the row-unit field. The recovery test repeated
that assumption.

The follow-up source correction reads the original typed command's duration,
explicitly asserts the canonical NF/NR unit distinction, and retains exact
whole-model comparison, required-duration rejection and recovery assertions.
It changes only those tests. **It has not been rebuilt or executed.** Keep the
original `native.log`/`native.xml` failure; rerun this target/group at the next
permitted build rather than repeating already valid UI/audio groups.

**Actual application: 21 passed, 2 skipped, 61.130 seconds.** These were
PID-scoped pipe/native-message checks on a private desktop:

- Plugin edit/recipe manual and transient preview contracts, atomic rejection,
  no-op, exact bases, ownership, one-step Undo/Redo and save/reopen.
- Context panel width reclamation at 1600×900 and 1000×720 DIP, retained native
  notes window, target, raw fields, selection, focus and document.
- Graph socket dragging, explicit rewiring, cancellation/stale guards,
  modulation fields, parameter editing, independent routing/curve drafts.
- Sample stroke cancellation, release commit, channel/interpolation/history,
  loop handles and keyboard operation, save/reopen, dense strokes and exact
  4096-point numeric batch limit.

The two skips require a native provider fixture or installed ARM64 effects.
They are not passes. `application.log` contains every case and skip reason;
`checks.json` records the native failure and successful application invocation.
The registration audit found **199 registered, zero unscheduled, zero repeated**
tests. This is scheduling evidence, not execution of all 199.

## Persistence and compatibility

`conformance.ps1` completed successfully on the same executable:

- F01–F05 each passed exact typed save/reopen checks.
- API vectors matched the pinned baseline with zero preservation failures.
  `API-INTEGER-TYPES`, `API-PLUGIN-DRY-RUN` and `API-REPLAY-ERROR` remain recorded
  differences; parity is not complete.
- Codec vectors matched the existing baseline. No expectations were relaxed.
- All three F04 zero-duration derivatives—parameter-set, scratch and
  scratch-stop—passed two saves/reopens. Each saved tree differs only by the
  permitted explicit integer-zero duration; full typed comparison rejects
  unrelated changes. Commands, identities and payloads remain preserved.

`conformance/report.json` is passed with `parityComplete: false`; subordinate
API, codec and duration reports retain exact inputs, output hashes and
comparisons. This actual-app decoder evidence is independent of the failed C++
test harness; it does not turn that harness green.

## Remaining gates and evidence reuse

The sidebar's earlier failing native layout fixture is now passed on this
candidate, and its actual-app width/focus check also passed. Recipe preview and
sample gestures have the bounded Windows ARM64 evidence above. These supersede
their earlier “source only” status for this platform and scope.

Mac and Windows x64 execution, the later Mac rack dry-run change, reciprocal
edited-file checks and the final shared-main integration gate remain open.
Computer use remains stopped after the user's Escape. Native-message tests do
not establish visual hierarchy, DPI/readability, Narrator output, physical
pointer feel or audible device behavior. No broad parity or foreground visual
claim follows from this candidate.

Preserve this app/output directory for test-only reuse. The next native build
may include the metadata-test correction with other cohesive pending work; it
must not precede the hourly gate. Reuse these results only where relevant source,
test, fixture, binary and platform inputs are unchanged. Any interface or
behavioral change invalidates the affected evidence.
