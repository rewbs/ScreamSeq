# Export candidate: native CI results and follow-up repairs

Observed 20 October, through **09:02:42 UTC**. These runs qualify candidate
`54fa2babc96e63d0b10ebd8dedb8c438036f3b3f` against main through the GitHub merge
commit **`d79321c7ccf59e7fdf0a4b2918d6906b41097ef4`**, verified in retained
BuildInfo and app identity receipts. They do not qualify the subsequent clock,
snapshot-test or Mac repair commits. The draft PR remains gated.

## Apple Silicon Mac

Run [39393737331](https://github.com/rewbs/ScreamSeq/actions/runs/39393737331),
job 118531234979, completed with failure in conformance:

- Native app build passed, receipt timestamp 08:45:41 UTC.
- All 134 native model/host CTest registrations passed. Swift recovery/picker
  and sample-library steps passed.
- All 35 native interface groups passed. This is the application interface
  harness, not a live visual/DPI or physical audio qualification.
- All five original fixture save/reopen legs passed; duration defaults passed.
- Shared editing contracts: parameter preview, recipe parameter, rack dry-run,
  strict integer API, terminal request replay and module export passed.
  Recipe preview failed before its gesture assertions: stable-ID
  `plugin.state.get` returned -32602, “Unknown parameter or field.”
- API observations differ from the old baseline for two intentional dry-run
  improvements: bypass and parameter writes now validate successfully without
  changing the document. The baseline has not been rewritten to hide these
  differences; reconcile with corresponding Windows evidence.
- Codec safety passed, but its historical observation baseline differs.
- The edited-metadata clean leg reached save/reopen. The opaque leg failed
  loading `opaque-input.screamseq`: “Invalid or unsupported ScreamSeq project.”
  Its UID is 281474976710657. No successful opaque edit/history/reciprocal claim
  follows. Later composing/envelope checks did not execute in this aggregate.

App SHA-256:
`521d96aefb2131801f6c0db3ce60bb1cbbe148885ada139b952be83df600bc1a`.
Build receipt SHA-256:
`bc002b682c88e1a9b566a94350ade128a59fa16c061c4aa61c43a9098a524fa6`.
Logs artifact 12251328813 SHA-256:
`45a03b5b528f6cfd21cdc441cfb9901d537433e9b58da496bef749556f22ef24`.
Downloaded unchanged to the primary worktree's
`bin/export-capture-20261020/mac-arm-logs.zip`, extracted in `mac-arm-ci/`.
Original reports include request-by-request evidence and exact fixture hashes.
The separate retained app artifact is 12251318239; do not rebuild to retrieve it.

## Windows x64

Run [39393737358](https://github.com/rewbs/ScreamSeq/actions/runs/39393737358),
job 118531235136, completed with failure:

- Native app and test build passed.
- 155/156 portable/worker registrations passed. The only failure was
  `snapshot-tests`, at `titlesAndLegacy: legacy[6]==std::byte{'1'}`.
  Its source correction in `9c8cea6b2` is still unbuilt, as documented in the
  [ARM64 qualification](EXPORT-CAPTURE-QUALIFICATION-2026-10-20.md).
- Three owned-private-desktop scratch tests passed.
- All 48 native editor/preference/recovery registrations passed.
- Actual application suite ran 84 tests with 13 failure records and two skips.
  The ten distinct failing cases cover transport/recording/audition. The three
  audition cases also have teardown failure records. Environment inspection
  reports `audio.devices.get` with an empty device list. Retain the failures;
  this runner does not prove audio behavior or excuse missing device validation.
- Conformance stopped on the method inventory before the complete corpus:
  the exact Windows delta is one added write, `document.exportModule`, no
  removed writes and no changed reads. This newly implemented endpoint already
  has native worker/UI checks, ARM64 actual-app export/reopen evidence and the
  passing Mac actual-socket export contract. Updating this one inventory entry
  is justified; behavioral vector expectations remain unchanged.

App SHA-256:
`cbe2bee85f8ce5f2194f073351a9db32095693da761104b225a0c8958a70063d`.
Build receipt SHA-256:
`fb80c9098f5f6ae6a798d027bbdc1e718e5935c6717be9254f57d1cbd02fbb69`.
Logs/app artifact 12252002779 SHA-256:
`43a50bc4c5d21faf586de5e2732ab015adaff75cea8c6b488a2ef14df13101b2`.
Downloaded unchanged to `bin/export-capture-20261020/x64-logs.zip`,
extracted in `x64-ci/`. Retained `native-app.zip` SHA-256:
`5cde8c022d865a8122e3e8e21342604faabcbc73cdb5feaf117d1964b8843732`.
Reuse that binary for remaining applicable checks without recompilation.

## Intel Mac and next boundary

Job 118531235347 in run 39393737331 is confirmed live at 09:02:42 UTC,
running the full interface harness after successful build, native tests,
recovery/picker and sample-library steps. Poll the same job; do not restart it.
All three platform build steps are now complete. Until exact Intel completion
time is retained, **10:03 UTC is a conservative next coordinated build bound**.
No new build was performed during these investigations.

## Source repairs, unbuilt

- `c6c247fb6`: Mac's existing stable-ID resolver was hidden behind a slot-only
  field whitelist for `plugin.state.get`. Permit `plugin` there, update the
  shared schema/guide and add native identity/reorder/removal/invalid-input
  checks. The shared recipe-preview acceptance contract is unchanged.
- `0d7ff6630`: if Foundation rejects a binary plist, decode it through the
  existing shared typed codec and create a model-reading Foundation projection.
  Opaque UID/date/wide-number representations project to NSNull for validation,
  never narrowed numeric values or fake ordinary data. The original full typed
  tree remains the preservation source, including exact opaque payloads.
  Add wide-UID/integer coverage to native project recovery and projection tests.
  The opaque acceptance fixture and exact comparisons remain unchanged.
- Update only the Windows advertised method baseline to include the verified
  export method. No expected error/success result or preservation assertion is
  relaxed.

Next batch must run Mac property-tree/project-recovery, stable-target and
recipe-preview socket checks, then the unchanged edited-metadata acceptance.
Reconcile paired API/codec observations individually. Repeat only the affected
checks first; the final integration gate still needs complete same-candidate
cross-platform and reciprocal fixture legs. The new
[recording frame clock](RECORDING-FRAME-CLOCK-2026-10-20.md) has separate unbuilt
timing/realtime gates and does not inherit these older binary results.
