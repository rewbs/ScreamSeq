# Export follow-up: current source audit

Scope: A15 and the file-workflow part of P6. This audit reads the frozen
`8c750f7d61966b3dc125ffaf49c34e8678697a25` candidate; it does not implement or
qualify export. Fix and qualify the current Mac build failure before expanding
the integration candidate.

## Confirmed boundaries

- Windows `DocumentController.cpp::operation` implements `document.save` only
  for `.screamseq`/`.resonance`. `document.exportModule` is absent from its
  dispatcher and actual-app inventory. `EditingView.inc::saveFile`,
  `WorkspaceCommands.inc` and `WorkspaceMenuBar.inc` have native Save/Save As
  but no module-export command. The module extension is not a second native-save
  choice.
- Shared `Document::validateModuleSampleExport` already serializes/reloads the
  source format and validates song structure, raw sample fidelity and timing.
  `Document::save(path, true)` uses these checks. `save(path)` defaults the
  preservation argument to false; a new export path must not use that default.
  Tests already cover these guards in `windows/Tests/Snapshot/SnapshotTests.cpp`.
- Mac `TrackerSessionAPI.inc` exposes guarded `document.exportModule` with
  `path`, `overwrite` and `dryRun`; it requires the current module-format
  extension. Its dry run rejects hosted plugins and native annotations before
  shared validation. Real writes go through `TrackerSession.mm::savePath`.
  The native File menu's `exportModule` uses an `NSSavePanel` and that same
  session method.
- `NativeSong::hasAnnotations` covers precise events, graph/performance data,
  automation, active/detached routing, note tracks/column mutes, envelopes,
  scratch gestures and entity annotations. This is a compatibility guard,
  not a detailed export-loss report.
- Windows `Project::writeProjectFile` already stages, flushes and publishes
  complete bytes with an explicit replace/no-replace flag. Its no-replace
  publication must be retained, including a destination created after
  admission. Do not implement export with an existence check followed by
  unconditional replacement.
- `saveNativeProject` updates path, saved revisions, metadata baselines and
  recovery state after publication. Export must not call it: an exported module
  is not a saved native project.
- `validateProjectSaveDestination` protects recovered source aliases, including
  filesystem equivalence and normalized paths. This must apply to exports too.
  Preserve conservative unknown outcomes after file publication; an unchanged
  song revision does not prove that no external file was written.

## Source-level concerns requiring runtime checks

Mac `savePath` unconditionally clears `_loadRecovery.protectSource` after either
native save or module export. This is observed code, not a reproduced failing
journey. Construct a recovered module-compatible input and determine whether a
successful separate module export can unlock the original native source. The
desired result is that export leaves native source protection and dirty/saved
identity unchanged.

The current module guard checks known native annotations/plugins, whereas the
new metadata owner can retain unfamiliar root/native extensions and inactive
recovery archives. Determine and explicitly report any data a module cannot
represent. Do not silently treat unknown stored data as safe to discard.

Windows selection rendering is an existing sample/instrument workflow, reachable
through `PatternSampleRenderWindow` and `sample.renderSelection`. It does not
establish a file-oriented full-song audio-export workflow. Mac exposes Export
Audio separately. Audit the full audio-export path, cancellation and destination
publication before counting that A15 requirement complete; do not relabel
selection-to-sample rendering as completion.

## Proposed cohesive implementation

1. Expose shared module-export preparation returning the **same validated
   bytes** that will be published. Reuse structure/sample/timing validators and
   report format and blocking reasons. Avoid validating one serialization and
   publishing an independently generated candidate. Keep filesystem publication
   and native dialogs in their platform adapters.
2. Add Windows `document.exportModule`, describe/schema/guide coverage and
   File / Export Module. Use a native file picker, a clear Export action and a
   useful incompatibility explanation with access to Save As. A blocked export
   should explain its limitation before the destination is written. There is
   no musical Undo entry for creating an external file.
3. Guard the captured revision across picker/worker boundaries; integrate take
   protection and retained command completion. Exact request replay must not
   write the destination twice. Failed or uncertain completions retain their
   result for inspection. Do not retire drafts, stop playback, update the
   project's path or mark the song saved merely because export completed.
4. Align Mac with the same preparation and recovery-protection contract. Keep
   AppKit dialogs and native exclusive publication. Treat detailed loss
   reporting as a shared policy, rather than inventing a Windows-only bypass
   for metadata that Mac rejects.
5. Implement full-song audio export as a separate bounded batch after inspecting
   its existing renderer/provider paths. Its rate/range/tail/cancel rules and
   actual output audio need independent qualification.

## Bounded acceptance

- Module-compatible MOD/XM/S3M/IT/MPTM fixtures: dry run writes nothing, actual
  export preserves the validated module state and raw samples/timing on reopen.
  Reuse existing adversarial snapshot cases for incompatible sample/timing and
  title/structure loss.
- F01–F05 remain native-save fixtures. Their native data must not silently
  disappear through export; check the applicable refusal/report, then exact
  native save/reopen after that refusal.
- Native/opaque extensions, missing plugins, recorded takes and source-protected
  aliases: retain exact original bytes, song state, saved identity and history.
  A cancelled picker or failed export must not consume Redo.
- Wrong extension, relative/missing parent path, existing target without replace,
  a target appearing after preflight, write/flush/publication failure and
  post-publication reply loss: assert the actual destination state and typed
  completion outcome. Never infer file outcome solely from document revision.
- A real export followed by a retry with the same request ID returns its retained
  outcome; conflicting request-ID reuse rejects. Both API and native command
  entry paths require coverage.
- First use existing portable snapshot/validation tests, then one coordinated
  build for changed native adapters and targeted actual-app file cases. Shared
  preparation changes require both platforms. Reuse unchanged DSP evidence;
  this module-export batch does not itself change audio processing.

Relative effort: **M** for shared validated preparation, Windows API/native
command and Mac consistency; separate **L** estimate for full-song audio export
until its source and cancellation boundaries are audited. No user decision is
needed to preserve the existing lossless-export policy.

## Isolated implementation in progress

Temporary branch `codex/module-export-followup` uses the existing
`windows-parity-workspace/ScreamSeq` checkout, starting at `951048a02`.
It is separate from the frozen PR candidate and qualification checkout.

The first prepared change adds `Document::moduleExportData()`, returning the exact
module bytes that passed structure, sample and timing validation. The existing
validation-only method and `save(path, preserveSamples)` share the same private
preparation; the legacy save argument/default and native filesystem code remain
unchanged. Snapshot checks now inspect the returned bytes, reload their musical
state, retain history/state, and reject dormant pattern-cell loss.

This is **unbuilt source**, not a user-facing export feature. The Windows
endpoint/command, shared metadata/loss report, Mac recovery-protection change
and actual-app acceptance remain to implement. The modified Windows snapshot
cases currently belong to the separate `windows/Tests/Snapshot` CMake project;
they must be explicitly scheduled with matching engine inputs at qualification,
alongside Mac sample-archive/session and new actual endpoint checks. Do not
mistake a default app build for execution of this standalone target.

Continue this branch while current CI results are collected, then reconcile
with the corrected integration branch before its own frozen, bounded build
batch. Preserve the one-main integration policy; this is a temporary feature
branch, not a platform fork.

### Endpoint and native-command source batch (20 October, unbuilt)

The follow-up now wires `document.exportModule` through Windows
`SessionAdapter.hpp` and `DocumentController.cpp`, retaining ordinary revision
guards and terminal request caching. Read-only admission produces an explicit
noncommit receipt; file publication remains outside that block and outside the
unchanged-revision rejection allowlist. The exact `moduleExportData()` bytes go
to `ProjectIO.hpp`'s exclusive/replace publisher. The operation does not flush
plugins, cancel parameter previews, stop playback or establish a native save
baseline.

`editor/project/ModuleExport.hpp` supplies one loss policy for native annotations,
hosted plugins, absolute automation, uncommitted takes and opaque extensions or
recovery archives. Windows checks the current metadata merge; Mac checks its
metadata owner's merge against the current known encoder. Unknown data is
rejected, never stripped to make export pass. Module byte structure/sample/timing
validation remains in the existing shared Document preparation.

Windows File / Export module uses `ModuleExportDialog.hpp` and the native
`IFileSaveDialog` with an explicit Export button. `EditingView.inc` captures the
document/revision across the picker. `NativeCommandRecovery.inc` recognizes
export destinations, records their hash and decoded summary during Review,
checks the file again before acknowledging uncertain work, and never repeats
publication automatically. Native shortcut dispatch preserves retained-editor
focus. The API guide, describe response and shared schema document the contract.

Mac's native menu now uses the guarded export API with the pre-picker revision.
The application adapter does not collect/commit plugin gestures for this method.
`savePath` clears recovery source protection only after a native project save,
and checks the module extension on direct module saves too. API preflight
rejects incompatible session layers before reserving a staging file.

Prepared (not executed) regressions:

- `document-controller-module-export` in the normal Windows worker schedule:
  five normalized imported source formats, dry run/no file, reopen, dirty/native
  state and Redo retention, wrong/relative/missing/existing destinations, strict
  flags, stale requests, explicit replacement, opaque native/root data,
  inactive archives, and recovered-source protection after successful export.
- `applicationModuleExport` in `workspace-command-result-tests`: native menu
  availability/dispatch, cancelled and stale picker results, successful export
  without native save/history changes, and destination readback. The fixture
  replaces only the OS picker; it is not a rendered-dialog test.
- Existing Mac `sample-project-tests` now checks five-format export with pending
  Redo. `project-recovery-tests` adds compatible recovered export followed by
  protected original-save refusal and opaque export refusal in dry/real modes.
- The previous standalone Snapshot target still needs explicit scheduling.

No build or test execution has qualified these additions. Before integration:
add actual pipe/socket request-replay and post-publication native-completion
fault coverage; inspect failed/uncertain publication outcomes and source aliases;
run the bounded native/worker/Mac batches on a frozen candidate. Review the real
Windows dialog and Mac sheet when foreground inspection is authorized. Full-song
audio export and manual automation capture remain separate open requirements.

### Retry, failure and schedule batch (20 October, unbuilt)

The next source batch adds `editor/Tests/module_export_contract.py` to the
fresh-process editing-contract runner used by Mac CI and to the Windows
actual-pipe application suite. It prepares dirty work with pending Redo, checks
dry/invalid/stale admission, then publishes both exclusive and replacement
exports. An owned external writer changes the destination before exact request-ID
replay: the retained result must return without overwriting that change. A
retained rejection must remain rejected after the destination is removed.
Conflicting ID reuse rejects. Full document readback, native file/dirty fields
where exposed, Redo, chronological Undo and an exact typed saved baseline are
checked. The common runner additionally saves/reopens the final native tree.

`ModuleExportApplicationTests.inc` now injects a native completion failure after
real publication and separately drops every receipt route after publication.
Re-entering Export reviews the known result; an uncertain-result acknowledgement
rechecks the destination and refuses if it changes during the prompt. Subsequent
Review records the current observation as unverified, never a fabricated success.
Both paths preserve externally replaced bytes, native path/dirty state and song
history, without repeating the operation.

`ModuleExportControllerTests.inc` also exercises a real Windows filesystem
replacement refusal using an owned read handle without delete sharing. It checks
unchanged bytes/state, absence of a success receipt and staging cleanup. A
format-matching hard-link alias of a recovered source must reject in dry and
real modes, preserving both links' bytes and the protected session.

The complete 48-case `SnapshotTests.cpp` is now registered as `snapshot-tests`
in the normal Windows CMake worker schedule, linking that same build's
`ScreamSeqProject`/`TrackerEditor` libraries. It has an owned scratch fixture and
the bundled F04 input. Historical version-4/5 writer expectations are replaced
with actual legacy-container recovery, strict writer rejection, source protection
and version-6/metadata-17 save-copy checks; Unicode titles, exact core/native
state and PCM assertions remain. The old unavailable Mac fixture is replaced by
F04's declared seven samples, seven instruments, eight precise notes and one
bank entry. A missing reference now fails rather than printing SKIP then PASS.
The standalone entry point gets the same scratch/reference setup; it is no
longer required for this batch's snapshot qualification.

Only static checks have run for this batch: changed Python sources parse, the
API schema parses, and `git diff --check` passes. No native compilation, new
runtime test or foreground interaction has run. This is prepared coverage, not
passing evidence. Keep the next coordinated build no earlier than
**08:28:51 UTC** on 20 October.

Next qualification: freeze the merged candidate; build both native platforms
once with the pending Mac compiler fix and Windows menu fixtures. On Windows run
`snapshot-tests`, `document-controller-module-export`,
`workspace-command-result-tests`, affected menu/departure/song-properties groups,
and `PluginEditParityTests.test_module_export_contract_and_reopen`. On Mac run
sample-project/project-recovery tests and the fresh socket export contract.
Retain original failures, actual output files and exact hashes. Shared Document
preparation also requires both platforms' existing sample/persistence gates;
broaden on a preservation or dispatch failure. Reciprocal fixtures, foreground
dialogs and the remaining parity journeys remain separate requirements.
