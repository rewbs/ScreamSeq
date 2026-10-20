# Plugin editing continuation — 19 October 2026

## Baseline and status

The broad parity goal resumed after all local branches were integrated and pushed
to main at `07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`. See the
[main integration record](../MAIN_INTEGRATION_2026-10-19.md) for its bounded
Windows evidence. The original planning matrix remains a historical inventory;
its statements about features absent from main must be reconciled with that merge.

This batch is **source prepared, pending compilation and native qualification**.
It is on temporary branch `codex/plugin-edit-parity`, based on shared main. It
does not close P4, P5, the reciprocal-file gate or overall Windows parity.

The user requires at most one build per hour. The latest local build finished
`2026-10-20T00:41:19.518934Z`; this batch must not build before
`2026-10-20T01:42:00Z` (18:42 Pacific on 19 October). No build has run for this
batch. Source review, fixture preparation and interpreter-only checks proceed
before that gate. A failed compile does not authorize an immediate extra build.

## Observed gaps and prepared changes

| Plan item | Observed on main | Prepared implementation and ownership |
| --- | --- | --- |
| P4b / API-PLUGIN-POSITION | Windows `plugin.move` only swaps adjacent entries; Mac accepts a final position. | `editor/RackMove.hpp` owns the final-position permutation and recorded-automation slot remapping. Both native adapters use it. Windows accepts position or direction and either stable plugin identity or slot, retaining dry-run validation, no-op history and existing publication. Mac also accepts the already-supported stable identity resolver for this operation. |
| P4b / rack ergonomics | Up/Down require repeated actions for distant moves. | Native **Move to…** button and command palette entry list final positions. Captured document, revision, selected plugin and draft generation are rechecked after the native popup loop. Compact racks retain Move to while hiding the two redundant adjacent-move buttons. |
| P5 / API-GRAPH-GESTURE | Mac accepts a graph-parameter gesture token; Windows rejects it. | Windows now supplies the existing guarded `Document::annotate` merge sequence for one captured graph/node/parameter/token. Failed publication cannot advance ownership. Both adapters also require the previous document revision, preventing a reused token from merging back across Undo/Redo. |
| P5 / recipe parameters | The song-routing rack inspector has pages of ten controls, while a reusable graph node exposes only the older single-value form. | **All parameters…** opens the same retained `PluginParametersWindow`, with an explicit graph/node target and the existing graph API. Target selection, drafts, automatic typed saves, result review and document retirement reuse the existing owner. Rack state and recipe state remain independent. |

The schema, API guides and source architecture map are updated with these
contracts. The two corresponding API vectors now require success on Windows,
with unchanged-document assertions for their no-op/dry-run inputs. Earlier
failure receipts remain historical evidence; modifying expectations is not a
runtime pass. No codec, plugin-state representation, audio callback, attribution,
device default or third-party framework changes are involved.

Graph gesture writes are durable edits sharing history, **not** the separate
transient-preview protocol. The paged sliders still commit on release. Audible
intermediate preview, cancellation ownership and the shared preview API remain
P4 work; this batch does not advertise those as implemented.

## Checks already executed

- Two new real-PID fixtures against the unchanged main executable reproduce the
  missing position and graph-gesture capabilities as `Unknown plugin field`.
  Log: local `bin/plugin-edit-parity-20261019/baseline-red.log`.
- The 22 existing conformance helper tests pass. They cover receipt/fixture
  safety and exact typed comparison, not the changed native implementation.
  Log: local `bin/plugin-edit-parity-source-checks.log`.
- New Python test syntax, JSON parsing, the schema's exclusive selector structure
  and `git diff --check` pass. The optional `jsonschema` package is unavailable;
  no full JSON Schema validator pass is claimed.
- No musician ScreamSeq process was running when the private test instances were
  launched. The two owned instances were closed by their fixture cleanup.

## Bounded qualification batch

Reuse the clean qualification checkout's existing ARM64 CMake cache at
`C:/Users/P14/.codex/worktrees/parity-ci-layout/ScreamSeq/bin/windows-parity-p1`.
Fast-forward its temporary integration branch to the frozen feature commit only
after checking it is clean. Keep `MainIntegration-Release` intact and use a new
runtime output directory for the candidate. Record commit, source/receipt hashes,
start/end time, commands and final executable hash. Configure once, then build
`ScreamSeq`, `workspace-restore-tests`, `document-controller-tests` and
`rack-move-tests` together with two workers. Do not run a second full build.

Required checks for that candidate:

1. `rack-move`: exhaustive source/destination permutations for rack sizes 1–64,
   preserved relative order, positional automation identity, inverse and invalid
   bounds. The same target is registered in both platform builds.
2. `document-controller-live-graph-publication`: existing live-control/bypass
   checks plus grouped writes, queue saturation/refusal, one Undo, Redo excluding
   the rejected value, and a reused token after Undo/Redo. Transport stays active.
3. Existing `document-controller-graph-recipes`,
   `document-controller-unified-plugin-history`,
   `document-controller-live-rack-publication` and
   `document-controller-live-parameters`: shared PCM/state, atomic publication,
   stable rack ownership and chronological history protection.
4. `workspace-plugin-search-tests` and `workspace-command-result-tests`: native
   rack position selection and stale/draft protection; recipe parameter entry,
   autosave submission, independent rack state, Undo/Redo, retained invalid text,
   reopen and retired owners; existing paged-rack and result-review cases remain.
5. `test_plugin_edit_parity.PluginEditParityTests`: four actual application
   cases covering multi-position moves, dry-run/no-op/replay/stale rejection,
   exact saved opaque state and recorded automation, save/reopen, recipe gesture
   grouping and history boundaries. Existing absolute-automation reorder and
   plugin move/remove/reopen cases supplement this focused selection.
6. Configured CTest scheduling audit: the new portable target and four Python
   cases must be included in the existing CI batches without duplicate runs.

Broaden only for a failure implicating another owner or a further product change.
No full native/application suite is required merely because the build directory
has a new output name. Unchanged engine/codec evidence can be reused within its
documented scope; changed app/controller tests cannot inherit earlier passes.

Mac validation needs the native `rack-move`, `signal-graph-session` and
`unified-history` tests plus the ordinary build/integration gate. The modified
Mac graph regression explicitly reuses a gesture token after Undo/Redo; the rack
test uses a stable identity for a nonadjacent move. Native UI, host and device
integration remain on their respective platforms.

## Current integration risk

The main push's existing CI runs are Windows `39349699781` and Mac `39349699645`.
At the last inspection the Mac ARM job had compiled successfully but its native
model/host step failed; the remaining job steps were still running, and GitHub
had not yet published its downloadable job log. Inspect that failure before
requesting another cross-platform run. No rerun or extra build was dispatched.
This is a baseline-main failure, not evidence about the unbuilt feature branch.
