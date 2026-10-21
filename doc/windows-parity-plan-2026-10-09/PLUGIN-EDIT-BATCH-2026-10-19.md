# Plugin editing continuation — 19 October 2026

## Baseline and status

The broad parity goal resumed after all local branches were integrated and pushed
to main at `07424c0c675c57caf7828ea23b6c7a4a13a5cf4b`. See the
[main integration record](../MAIN_INTEGRATION_2026-10-19.md) for its bounded
Windows evidence. The original planning matrix remains a historical inventory;
its statements about features absent from main must be reconciled with that merge.

This batch is **compiled and partially qualified, with one discovered recovery
fix prepared but not yet compiled**. The qualification follow-up below takes
precedence over the preparation notes.
It is on temporary branch `codex/plugin-edit-parity`, based on shared main. It
does not close P4, P5, the reciprocal-file gate or overall Windows parity.

The user requires at most one build per hour. The latest local build finished
`2026-10-20T00:41:19.518934Z`; this batch must not build before
`2026-10-20T01:42:00Z` (18:42 Pacific on 19 October). The single build ran after
that gate, from 01:42:08 to 01:44:52 UTC. The next build must not start before
02:45 UTC (19:45 Pacific). A failed check does not authorize an immediate rebuild.

## Observed gaps and prepared changes

| Plan item | Observed on main | Prepared implementation and ownership |
| --- | --- | --- |
| P4b / API-PLUGIN-POSITION | Windows `plugin.move` only swaps adjacent entries; Mac accepts a final position. | `editor/RackMove.hpp` owns the final-position permutation and recorded-automation slot remapping. Both native adapters use it. Windows accepts position or direction and either stable plugin identity or slot, retaining dry-run validation, no-op history and existing publication. Mac also accepts the already-supported stable identity resolver for this operation. |
| P4b / rack ergonomics | Up/Down require repeated actions for distant moves. | Native **Move to…** button and command palette entry list final positions. Captured document, revision, selected plugin and draft generation are rechecked after the native popup loop. Compact racks retain Move to while hiding the two redundant adjacent-move buttons. |
| P5 / API-GRAPH-GESTURE | Mac accepts a graph-parameter gesture token; Windows rejects it. | Windows now supplies the existing guarded `Document::annotate` merge sequence for one captured graph/node/parameter/token. Failed publication cannot advance ownership. Both adapters also require the previous document revision, preventing a reused token from merging back across Undo/Redo. |
| P5 / recipe parameters | The song-routing rack inspector has pages of ten controls, while a reusable graph node exposes only the older single-value form. | **All parameters…** opens the same retained `PluginParametersWindow`, with an explicit graph/node target and the existing graph API. Target selection, drafts, automatic typed saves, result review and document retirement reuse the existing owner. Rack state and recipe state remain independent. |
| P5 / completed recipe edits | A live, silent QA review of the main binary confirmed an enabled Apply on a clean graph. Source confirms that node moves, cable edits and Set property only stage changes. | Completed drags, keyboard position steps, Set property and wire actions now save through a guarded retained result owner. Backspace also removes the selected cable. Save edit is a disabled fallback while clean; Review result handles a lost completion without repeating the write. Invalid/stale definitions remain captured. Typing alone and vendor editor drafts retain their explicit completion boundary. |

The schema, API guides and source architecture map are updated with these
contracts. The two corresponding API vectors now require success on Windows,
with unchanged-document assertions for their no-op/dry-run inputs. Earlier
failure receipts remain historical evidence; modifying expectations is not a
runtime pass. No codec, plugin-state representation, audio callback, attribution,
device default or third-party framework changes are involved.

Recipe parameter metadata now retains the same numeric `unit` discriminator on
both adapters, so the common Windows field presenter can choose an actual native
checkbox for a Boolean parameter. The API regression compares recipe/rack IDs,
units, labels, choices, steps and writability explicitly.

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
7. `test_graph_editor.GraphEditorTests`, the two graph-specific context-menu
   cases, and `GraphCurveHostTests.test_routing_and_curve_keep_independent_raw_drafts_and_targets`:
   completed gestures save once, Escape cancels, a stale in-flight drag preserves
   the document, invalid cycles remain drafts, cable attributes/neighbors persist,
   Backspace cuts only its selected cable, and retained curve drafts stay independent.
   Native command-result tests additionally inject known and unknown graph
   completion failures and a revision change during acknowledgement.

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
These are baseline-main results, not evidence about the unbuilt feature branch.
No rerun or extra build was dispatched.

- Mac ARM64 compiled; 127/128 model/host tests and all 35 interface groups passed.
  `plugin-aliases` failed its plugin-only exported PCM assertion. Source inspection
  shows that its demo-derived instruments still map samples, now intentionally
  audible alongside plugins. The fixture now explicitly clears and verifies the
  three sample keymaps before alias measurements. Exact PCM assertions remain;
  separate shared AU/VST3 layered-stem checks remain intact. This correction
  still requires a Mac run. Intel was still compiling at the last inspection.
- Windows x64 compiled and passed the registered native/model/editor batches.
  The integration Python batch reported 16 failures and three errors (including
  secondary teardown failures after audio startup refusal). Six non-audio cases
  had stale fixture assumptions: expecting a clean saved song after a mutation,
  expecting a visible Apply in an auto-saving mixer, using valid text to test
  draft blocking, failing to allow overwrite of the test's own earlier save,
  and reading newly reopened mixer strip bindings before the normal timer update.
  All six corrected cases pass against the unchanged main ARM64 executable
  (8.954 seconds; `bin/plugin-edit-parity-20261019/baseline-fixture-corrections.log`).
  They retain exact state/history/identity assertions and still need x64 confirmation.
- Windows pipe conformance stopped on an outdated advertised-method inventory.
  An actual main-binary probe found two added reads (`pattern.timeline.get`,
  `track.get`) and eight added writes (`document.new`, four track edits,
  `transport.loop`, `workspace.keyboard.set`, `workspace.ruler`), with no removals.
  The reviewed inventory is updated explicitly. The same probe confirms that
  `order.edit` with destination equal to source preserves the full document and
  revision; the obsolete rejection vector is replaced by success plus unchanged
  document assertions. Mac ARM64 CI passed that no-op vector already.
- The remaining Windows live-audio failures could not start a WASAPI endpoint
  on the hosted runner, despite the existing audio-service startup/probe step.
  Required tests are not disabled and no synthetic clock is substituted for
  device evidence. Preserve that failure until an endpoint-equipped run qualifies
  the transport/recording/audition cases. This remains an integration limitation.

The foreground review used only the task-owned main executable with
`--inspection --automation --seconds 900`, PID 30320, at 1285×761. It confirmed
actual presentation and clean-graph Apply discoverability, not sound, frame
pacing or the unbuilt changes. Its disposable modified demo was discarded and
the owned process closed; no musician session was present or modified.

Follow-up: the exact x64 binary from run `39349699781` was downloaded with its
artifact, archive, executable and build-receipt hashes verified. Its hosted
audio report explicitly contains an empty device list. On this endpoint-equipped
ARM64 host, that x64 binary passes all 16 targeted cases under x64 emulation,
without a rebuild. This includes the six corrected non-audio fixtures and ten
live-audio cases. Two further fixture corrections were necessary: use invalid
`3e` rather than auto-saving valid `3.` when testing retained raw text, and compare
the detached editing context exactly while separately asserting that the live
playhead remains in its requested pattern/range. Assertions for transport,
document/history, focus, Undo and persistence remain. See
[the exact evidence record](MAIN-CI-FOLLOWUP-2026-10-19.json). Hosted CI is still
red; these baseline passes do not qualify the new graph/plugin implementation.

## Qualification follow-up

The frozen candidate is `1d6c2a21106411c3885169cfd5689dd369e1ceab`, built once
for Windows ARM64. Its executable SHA-256 is
`3b58452a58ddc5ea35ba2c720a126578b080b14b729b1c293812265bf652f76e`.
The [qualification record](PLUGIN-EDIT-QUALIFICATION-2026-10-19.json) pins the
build receipt, logs, test source and conformance reports.

- All nine selected CTest groups passed, including the scratch fixture dependency
  automatically selected by CTest. The configured schedule has 195 registrations,
  zero unscheduled entries and zero repeated registrations.
- Four new plugin API cases passed, including final positions, exact saved state,
  automation remapping, gesture grouping and reused-token history boundaries.
- The 19 application cases initially produced 14 passes, three failures/errors
  and two optional provider skips. Two fixture corrections pass on the same
  executable: bounded retries of read-only observations while the retained
  parameter editor refreshes, and using the visible **Set point** action to
  validate an unselected raw curve point. Writes and native actions are never
  retried. All exact state/identity/history assertions remain.
- One real product defect remains in this candidate. A rejected invalid
  `graph.update` leaves the document unchanged but lacks a known rejection
  outcome. The new retained command owner therefore preserves an unknown result;
  Reload opens its acknowledgement dialog. Source now classifies this operation
  at the controller's prepared-document boundary, after its committed/returned
  branches, and adds a native regression requiring direct Reload with no unknown
  result. `Document::annotate` validates and stages history before publication;
  publication refusal rolls back that history entry. This fix and regression are
  **uncompiled**. The failing real-PID test remains a required next-build check.
- All five original supplied fixtures pass exact typed save/reopen/save
  comparisons. All 28 API vectors match their recorded expectations, and all 21
  codec vectors match the baseline with safety checks passing. Existing recorded
  platform differences remain; this is not a claim of complete API/codec parity
  or a reciprocal edited-fixture gate.
- Main's Intel Mac run has now also completed: 127/128 model tests and all 35
  interface groups pass, with the same alias PCM fixture failure as Apple Silicon.
  Neither result qualifies this candidate's Mac changes.

The next eligible build is **2026-10-20 02:45 UTC / 19 October 19:45 Pacific**.
Build the app and workspace/controller tests together after batching further
necessary fixes. Rerun command-result recovery, the invalid-cycle real-PID case,
and controller graph/receipt tests affected by the outcome change. Reuse other
evidence only after reviewing the changed inputs. Do not merge this candidate
to main as fully qualified before this recovery fix and Mac changes are checked.

## Next dependency after this batch

The next parameter usability step is transient audible slider preview, followed
by one durable commit or cancellation. Discovery confirms that
`editor/hosted/PluginChain.cpp` already owns `previewParameters`,
`hasParameterPreview` and `cancelParameterPreviews`, declared in `HostedAudio.hpp`;
`editor/Tests/ParameterPreviewTests.cpp` checks baseline preservation, cancellation
against a newer accepted value and queue saturation. Neither native frontend
currently calls those preview methods. Windows vendor-editor polling in
`PluginOperations.cpp` is a different path: it delivers live edits and later
captures opaque state, and must not be mistaken for cancellable UI preview.

The next batch should expose an owner with stable document/plugin/parameter
identities and revision/generation guards through the controller/API, then wire
`PluginParametersWindow` tracking/release/Escape to it. Recipe instances need
their corresponding graph-control publication owner, not a positional rack
shortcut. Queue refusal must retain ownership for safe cancellation; save,
document replacement, Undo and external edits need explicit interaction tests.
Reuse the existing shared primitive and tests rather than introduce a second
audio-state representation. Do not advertise the durable graph `gesture` token
as this preview protocol.
