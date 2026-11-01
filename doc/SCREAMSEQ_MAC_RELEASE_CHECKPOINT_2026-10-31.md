# Mac release checkpoint — 31 October 2026

This is a usable checkpoint of PR #9, not a claim of complete Windows parity.
The dynamic left context pane is included. The branch includes integration main
`d1b52ade532d2d8fd9308a374ad61dd91ec7968c`; main was fetched again for this checkpoint.
The older [parity report](SCREAMSEQ_MAC_PARITY_CURRENT_MAIN_2026-10-28.md) records
historical batches. This document supersedes its remaining-work list.

## Included behavior

- Editor-specific left context pane, named layouts, categorized Command-K search,
  independent Graph Curve docking/pinning, and keyboard preferences.
- Retained sample drawing, numeric point staging, processing, crossfade, clipboard,
  replacement and instrument creation/import workflows with guarded completion.
- Sample-library batch imports, folder management and rescan recovery; latest search
  resumes after retained work without repeating an import.
- Plugin-instrument creation and assignment verify the captured stable identity,
  revision, routing and sample keymap. Unknown outcomes expose read-only Review
  and explicit Accept. Creation and assignment have separate Undo steps.
- Removed plugin-instrument targets can be explicitly discarded after a current
  catalogue read; this releases Save/history/departure admission without writing
  or selecting a replacement instrument. New trigger names are limited before
  creation to the engine's 31 UTF-8-byte storage capacity. Native snapshots now
  retain all accepted instrument-name bytes across Save/reopen and Undo, including
  Unicode, using the shared Mac/Windows archive codec.
- Recorder and selection-render input/outcome retention, guarded device settings,
  VST3 location repair, and retained song/mixer/note/automation edits.
- Clean-only Open/New/recovery admission protects known hidden editor drafts.

## Using this checkpoint

The production bundle uses the normal ScreamSeq name and `org.resonance.tracker`
identity. Build with `SCREAMSEQ_BUILD_DIR=<separate-directory>
SCREAMSEQ_BUILD_JOBS=2 RESONANCE_DEVELOPMENT_BUILD=0 bash mac/build.sh`.
This is an optimized arm64 build for macOS 14 or later, locally ad-hoc signed by
this repository's build recipe; it is not a notarized public distribution.

Save and quit an existing ScreamSeq session before switching to this production
bundle. Qualification uses a separately identified copy and disposable songs;
it does not replace a musician's running process or change system audio defaults.

When an import or assignment reports an uncertain outcome, use its Review action
and inspect the observed result before Accept. Review does not repeat a write.
For a removed plugin instrument, **Discard removed assignment** explicitly retires
that editor and its unsaved fields. Correct overlong trigger names before Create.

Projects needing the new instrument-name correction require an updated build on
either platform. Older readers reject that correction instead of loading a
truncated name. Files whose module representation already preserves their names
keep their previous archive encoding. Standalone module export refuses a lossy
name conversion; use the native project format to retain the full name. This
prevents future truncation; it cannot recover characters already lost in older saves.

## Remaining Windows parity work

These are follow-up tasks, not features promised by this checkpoint. The remaining
Windows commit inventory still needs source-by-source review; this list is not an
assertion that every other Windows change has been ported.

| Area | Remaining work and acceptance evidence |
| --- | --- |
| Frame pacing | The final 60-second workload failed the existing presentation/geometry gates: 57.724 actual fps, 3.793% missed presentations and a 158.332 ms maximum presentation gap. Diagnose using the retained frame/UI-work/window-state timelines and pass the unchanged gates with the live edits, saves and audio workload enabled. |
| Intel CI verification | The original Intel job passed model/host checks but failed one recorded-point interface assertion using a 350 ms wait for a 300 ms callback. Test-only follow-up `066b8e6dc` keeps the main queue serviced and retains the target/value/revision assertions; all 49 groups and 20 consecutive repetitions pass locally. A standalone Intel rerun could not be started (workflow lookup rejected, then direct API connection failed). Remote verification remains open. |
| Native chooser keyboard | Isolate the computer-use timeout when sending Go to Folder in the library chooser on macOS 26.5.2. The app stayed responsive to API reads; Escape cancelled safely, and ordinary file-list navigation worked. Repeat keyboard and accessibility checks for both folder and file pickers. |
| Sample imports alongside drafts | Let library imports coexist with unrelated raw sample/instrument drafts while preserving stable target, captured revision, text, selection and focus. Current Mac admission can require finishing those drafts first. Test success, refusal, newer input and Undo. |
| Direct import commands | Port direct raw/mapped/default file-import commands, including hidden browser ownership and native chooser cancellation. Current new-sample import opens the browser. |
| Multisample import completion | Replace `MultisampleImportView`'s basic result handling with captured intent, strict receipts and catalogue/mapping readback; retain unknown results for Review/Accept without duplicate imports. Verify one-step Undo, exact key ranges and save/reopen. |
| Native editor ownership | Complete the census of hidden drafts, pending operations and IME composition across all editors and auxiliary windows. Add explicit discard/retirement paths and test panel close, Save, history, Open/New and Quit. Plugin-instrument removal is covered here; that does not complete the global audit. |
| Loop/settings recovery | Extend current reload-based failure handling with the Windows outcome-review/acknowledgement behavior while preserving original fields and targets. Exercise dropped replies, malformed receipts and newer edits. |
| Compact graph bus controls | Audit rejected final writes and preserve the rejected values/target instead of losing the gesture; test subsequent context changes and history. |
| Other plugin workflows | Audit the plugin-to-many-instruments window, presets, routing, reusable recipes, composition and auxiliary editor completion. The instrument-to-plugin editor completed here is only one of these workflows. |
| Navigation and accessibility | Continue instrument-envelope, precise-note, recorded-point source-link and keyboard/focus audits against Windows. |
| Runtime qualification | Complete long exports, continuous drawing and broader third-party plugin runs. The existing startup/workspace/crash-recovery suites pass; broader interactive coverage remains useful. Microphone capture, physical-device latency and speaker output remain unqualified by this checkpoint. |

Existing shared APIs, chronological Undo, stable IDs and native project persistence
must remain authoritative. Do not fork musical behavior into platform UI code.
Keep subsequent full builds at least one hour apart and perform computer-use
validation after each full build.

## Delivered production build

The packaged source commit is `59d7a6dc14d2d32ab0a1f50b7d33859163d89058`.
Build 18 started at 2026-11-01 02:06:50.579 UTC (31 October, 10:06:50.579 p.m.
New York), more than one hour after build 17, and finished at 02:09:53.714 UTC.
It is an optimized Release arm64 bundle, macOS 14+, with the normal production
identity. Deep strict signature verification passed; all 3,566 tracked files
were unchanged during the build. The post-build changes are test synchronization and release documentation only;
no compiled application source changed.

Local deliverables are under `bin/releases/mac-parity-checkpoint-2026-10-31/`:
`ScreamSeq.app`, `ScreamSeq-macOS-arm64.zip`, `BUILD.json` and this report.
`BUILD.json` records the source commit, executable and ZIP hashes, source
fingerprint, minimum OS and signing status. This is a locally ad-hoc signed
checkpoint, not a notarized public release or a completed Windows-parity claim.

## Validation record

- **142/142 CTest tests passed** against the rebuilt binaries in 173.94 seconds.
  This includes the new instrument-name snapshot/project regressions, exact audio,
  native model/host, routing, history, persistence and malformed-input tests.
- **49/49 native interface groups passed** after the final test-only follow-up.
  Both the revised mixer group and recorded-point/parity group also passed 20
  consecutive runs each, with no assertion retries. Whole-app
  Swift typechecking passed before the final builds, and the production compile
  passed after the shared archive change.
- **All six final-binary suites passed**: CTest, complete application socket API,
  library/multisample socket behavior, native startup, workspace and crash recovery.
  Recovery exercises the real timer, process crash/relaunch, exact song recall,
  corrupt fallback, write failure/retry and recording-take recovery.
- **Computer-use validation followed every full build.** Build 18's isolated QA
  bundle had all 44 file-backed Mach-O sections identical to production. With the
  Sample inspector active, a 32-byte name was refused without changing revision,
  assets or PCM; valid 31-byte ASCII and Unicode names created empty-keymap plugin
  instruments. A retained channel-2 field kept Save blocked until explicitly
  applied, then native Save succeeded. Native Open of a byte-for-byte saved copy
  created a new document identity and retained all ten samples, eleven instruments,
  stable asset IDs, full metadata, PCM, keymaps and both channel-1/channel-2 routes.
- Folder selection indexed the two fixture WAVs. Selecting a WAV enabled Open;
  Cancel preserved the exact song and browser state. Go to Folder attempts timed
  out in computer use; the app's main loop and API remained responsive. Escape
  cancelled safely, and ordinary file-list navigation and type-ahead completed
  the picker checks. The cause of the shortcut/control timeout remains unverified.
- Earlier desktop checks on this checkpoint's unchanged editing code covered raw
  and mapped batch imports, exact PCM, one-step Undo, stable-ID Redo, retained search
  through Rescan, removed plugin-instrument target discard and release of Save.
- Owned QA processes were stopped. The musician's QA10 process was preserved.
  System defaults still select MacBook Pro Microphone for input and MacBook Pro
  Speakers for output/system output; the workload selected BlackHole only in its
  own engine.

### Playback and presentation limits

The final 60-second combined VST3/graph audio workload used 48 kHz and 512-frame
buffers, with the Pattern editor visible. It completed 299 live edits, 14
Undo/Redo cycles and three saves. Its 5,625 audio callbacks had **zero overruns,
zero buffer starvations, no fault and no plugin failure**; callback p99.9 was
3,360 µs and maximum was 3,488.833 µs. These are virtual-device/fixture-plugin
results, not physical-device or third-party-plugin qualification.

**The presentation gate failed.** Actual timestamps were available in this final
run: the canonical 58-second window measured 57.724 fps, 132/3,480 missed
presentations (3.793%), a maximum presentation gap of 158.332 ms and maximum geometry
snapshot age of 44.142 ms. The failures were stale geometry beyond two periods,
missed deadlines above 0.1%, and a stall beyond two 60 Hz periods. The window was
visible, active and unoccluded. No sustained-frame-pacing pass is claimed, and the
failure is not dismissed as the earlier build-16 missing-timestamp symptom.
The graph was active in audio; its separate canvas was not visible in this run.

### Fixes found during qualification

Build 16 exposed a false plugin-creation preflight refusal with the Sample
inspector active. Delivery now waits for outstanding inspector reads; builds 17
and 18 passed the exact desktop case. Build 17 then exposed 31-byte instrument
names reloading as 25 bytes through the legacy module base. The shared archive
correction fixed that; build 18 passed native Save/Open for ASCII and Unicode.

The initial local CTest run passed 141/142 because a synthetic CoreMIDI packet
could have the same host-clock timestamp as the input discard boundary. The
fixture now uses one later tick and explicit channel status bytes, matching
Apple's MIDIPacket contract; production MIDI code and assertion timeouts were
unchanged. It passed 20 isolated runs and the subsequent complete suites.

Both older Mac CI interface runs failed a fixed 30 ms test wait. The mixer test
now waits for the actual deferred callback within a bounded second, additionally
asserting exactly one write, its method and captured revision. Production mixer
timing is unchanged.

The later Intel run passed that mixer test but failed another fixed wait in the
recorded-point test (350 ms for a 300 ms callback). Test-only commit `066b8e6dc`
keeps the headless harness's main queue serviced until the expected write arrives
within one second. It retains the exact two-write count, relocated frame, newer
value and acknowledged revision assertions, and actively services the rejection
check's unchanged 350 ms interval. All 49 groups and 20 consecutive parity-group
runs pass locally. This follow-up does not modify the packaged application.

### Remote checks and evidence

At the packaged source commit, Windows x64 and macOS Apple Silicon CI passed.
Intel CI passed its model/host checks but finished 48/49 in the interface suite,
as described above. A standalone Intel interface rerun of the test-only fix was
not started: GitHub rejected the workflow-name lookup, and the direct workflow-ID
dispatch could not connect to its API. Remote verification of that fix remains open.
The test/documentation follow-up commits use `[skip ci]` to avoid another full
application build for unchanged application code. All 21 existing PR review
threads are resolved. Greptile has not completed a new review after the fixes,
so a clean current-head Greptile review remains unavailable. Check PR #9 for later
remote status; local test success does not replace a missing remote review.

Local evidence is intentionally excluded from Git under
`bin/mac-parity-current/evidence/`: `build-18-identity.json`,
`build-18-source-hashes.json`, `release18-tests.json`, `release18-*.log`,
`qa18-native-reopen-result.json`, the before/after `qa18-*.json` captures,
`interface-release-checkpoint-final3-all.log`, `interface-mixer-final-repeat.log`,
`interface-recorded-point-final-repeat.log`, `ci-59d-macos-failures.log`,
`ui-build18-vst3-graph-60s.json`, `ui-build18-traces/`, the chooser process sample,
and before/after system-audio inventories. The timelines preserve the failed
presentation run for follow-up rather than replacing it with a passing rerun.
