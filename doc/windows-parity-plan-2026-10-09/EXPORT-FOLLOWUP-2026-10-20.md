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
