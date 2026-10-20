# Recipe manual baselines and transient parameter previews

## Evidence and scope

Source work follows the rack-preview checkpoint and the separately frozen
Windows context-panel batch. This batch is **not built or runtime-qualified**.
Do not transfer the prior rack PCM/native passes to these changes.

The existing `ef24c31a` Windows ARM64 executable reproduced a recipe no-op defect:
reading a non-float-exact manual baseline from normalized modulation and setting
that same displayed value created a document edit. The corrected valid fixture
uses two contributions with the same base (`.123456789`), one enabled and one
disabled. The first draft fixture incorrectly used different bases and was
properly rejected; only the subsequent valid-fixture failure is defect evidence.
Logs are in `bin/parameter-preview-parity-20261019/recipe-noop-before-valid.log`.

Recipe slider tracking previously held text locally until release; unlike rack
sliders it did not audition the recipe's prepared copies. The new implementation
uses existing graph-control publication instead of writing document history for
every thumb movement.

## Shared/native boundaries

`editor/hosted/RecipeParameterEdits.hpp` centralizes validated manual edits for both
adapters. It validates the full batch, stages maps and contribution bases before
provider acceptance, preserves exact representational no-ops, widens float limits
before range subtraction, and changes no opaque state or routing.

`PluginChain::prepareGraphParameterPreview` prepares a candidate using that helper.
It rejects a stale accepted graph except for the explicitly previewed parameter,
and rejects replacement processors/latency repair in this path. A transient plan
does not overwrite accepted parameter-observation baselines. It uses the existing
bounded graph publication queue and control-owned retirement; no callback
allocation, native UI or device framework is introduced.

Windows `RecipeParameterPreviewOperations.inc` and Mac
`RecipeParameterPreviewAPI.inc` own revision/host/gesture checks. Only one rack or
recipe gesture may exist per session. Commit delegates to the ordinary recipe
edit, cancellation republishes the accepted graph, stopped-host cancellation
drains pending controls only when the callback is quiescent, and queue refusal
retains the owner. Unrelated accepted writes/save/history cancel first; recovery
snapshots serialize accepted state without committing the draft. Retired hosts
cannot receive later preview publication.

Windows `PluginParametersWindow.hpp` chooses the appropriate API family while
retaining its existing target, ten-parameter pages, failed-write review, deferred
release/Escape handling and raw field drafts. UI navigation does not retarget an
open parameter editor.

## Required bounded qualification

The source-only batch adds:

- `parameter-edit`: exact normalized bases, disabled contributions, invalid
  atomic batches/provider refusal, nonfinite and wide-range validation.
- `parameter-preview`: rendered recipe PCM at 44.1/48/96 kHz and blocks
  17/128/511; ordinary, Row, persistent and sample-instrument copies; accepted
  catalog preservation, cancellation, final commit, saturation, stale topology,
  unchanged clocks/opaque state and realtime allocation/free/lock audit.
- `document-controller-live-recipe-preview`: actual Windows worker, retained
  publication, full-queue refusal with NotCommitted outcome, restoration,
  no-op/release history, external edit, epoch retirement and save/reopen.
- `workspace-recipe-edit-tests`: real retained recipe inspector, slider
  tracking/release, Escape, pumped final-position/release, typed partial drafts,
  history, captured targets and document replacement.
- `recipe_parameter_contract.py` and `recipe_preview_contract.py`: shared real
  pipe/socket contracts; strict types/unknown keys, request-ID replay, cross-family
  ownership, no-op/Undo/Redo, exact saved trees. Windows adds actual reopen.
- Mac `parameter-activity`: native history and stale native edit interleaving,
  recovery without capturing preview, explicit native save/reopen and stable
  recipe target.

Build app, workspace/controller tests, parameter-edit and parameter-preview in
one cohesive future batch after the current context-panel qualification. Respect
one build per hour even after a failure. Run the targeted groups above plus
existing live rack/graph publication and unified plugin history; broaden on
failure, changed dispatch behavior or the final integration gate.

Validate all four schema request shapes and both dispatch inventories cheaply
before compilation. After the candidate runs, capture actual api.describe and
repeat F01–F05 typed save/reopen plus API/codec vectors once because this batch
changes shared manual editing. Reuse previous evidence only for unchanged exact
inputs. Source-updated inventory expectations are not native passes. Mac and x64
builds and reciprocal edited fixture round trips remain required integration
gates on shared main.

## Remaining risks

Cheap checks completed on 20 October UTC: all four changed/shared Python files
parse; 14 full-schema checks accept valid requests and reject unknown keys,
boolean parameter IDs and noncanonical node IDs; both source dispatch inventories
and the expected shared inventory contain all four methods; the schema fingerprint
matches; `git diff --check` passes. Schema receipt:
`bin/recipe-preview-20261020/schema-checks.json`. These are source/schema checks,
not native execution evidence.

Measure control-thread cost for dense recipe/copy counts; no responsive-slider
claim follows merely from correct prepared audio. Check coexistence with native
vendor-editor capture and delayed publication. Cancellation errors must remain
visible, without silently closing the owner. No physical audio or interactive
visual proof has been obtained. Computer use remains stopped after user Escape.

This batch does not change project versions, stable IDs, plugin opaque state,
OpenMPT attribution or platform-native audio/device hosting.
