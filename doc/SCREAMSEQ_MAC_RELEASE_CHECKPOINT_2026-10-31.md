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
  creation to the engine's 31 UTF-8-byte storage capacity.
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

## Remaining Windows parity work

These are follow-up tasks, not features promised by this checkpoint. The remaining
Windows commit inventory still needs source-by-source review; this list is not an
assertion that every other Windows change has been ported.

| Area | Remaining work and acceptance evidence |
| --- | --- |
| Sample imports alongside drafts | Let library imports coexist with unrelated raw sample/instrument drafts while preserving stable target, captured revision, text, selection and focus. Current Mac admission can require finishing those drafts first. Test success, refusal, newer input and Undo. |
| Direct import commands | Port direct raw/mapped/default file-import commands, including hidden browser ownership and native chooser cancellation. Current new-sample import opens the browser. |
| Multisample import completion | Replace `MultisampleImportView`'s basic result handling with captured intent, strict receipts and catalogue/mapping readback; retain unknown results for Review/Accept without duplicate imports. Verify one-step Undo, exact key ranges and save/reopen. |
| Native editor ownership | Complete the census of hidden drafts, pending operations and IME composition across all editors and auxiliary windows. Add explicit discard/retirement paths and test panel close, Save, history, Open/New and Quit. Plugin-instrument removal is covered here; that does not complete the global audit. |
| Loop/settings recovery | Extend current reload-based failure handling with the Windows outcome-review/acknowledgement behavior while preserving original fields and targets. Exercise dropped replies, malformed receipts and newer edits. |
| Compact graph bus controls | Audit rejected final writes and preserve the rejected values/target instead of losing the gesture; test subsequent context changes and history. |
| Other plugin workflows | Audit the plugin-to-many-instruments window, presets, routing, reusable recipes, composition and auxiliary editor completion. The instrument-to-plugin editor completed here is only one of these workflows. |
| Navigation and accessibility | Continue instrument-envelope, precise-note, recorded-point source-link and keyboard/focus audits against Windows. |
| Runtime qualification | Complete long exports, continuous drawing, sustained presentation/frame pacing, broader startup/workspace/recovery and third-party plugin runs. Microphone capture, physical-device latency and speaker output remain unqualified by this checkpoint. |

Existing shared APIs, chronological Undo, stable IDs and native project persistence
must remain authoritative. Do not fork musical behavior into platform UI code.
Keep subsequent full builds at least one hour apart and perform computer-use
validation after each full build.

## Validation record

The final production bundle must include the inspector-read ordering follow-up:
plugin-instrument results wait for existing inspector reads to finish before
checking admission. Packaged build 16 exposed a false preflight refusal when the
Sample inspector was active; the identical captured input succeeded with Pattern
focus. That source fix passes whole-app typechecking and awaits the next hourly
build for the exact desktop regression.

Build 16 was optimized Release with the normal production identity, from
`6a0dc130ec7dc175f16eb5a969e0236b00bf6977`. It started at 2026-11-01 00:06:48.996 UTC
(31 October, 8:06:48.996 p.m. New York), finished at 00:09:54 UTC, and passed deep
signature verification with all 3,565 tracked files unchanged. It is an intermediate
qualification build, not the final delivery because of the read-ordering issue.

- All 49 native interface groups passed; whole-app Swift typechecking passed.
- The packaged complete application socket suite and sample-library/multisample
  socket suite passed, including exact mapping, atomic imports, history and native
  save/relaunch.
- Desktop raw batch import appended two exact-PCM samples without changing existing
  samples or instruments. One native Undo restored original metadata and PCM.
- Desktop mapped import appended two samples and two instruments, each with all
  128 notes mapped to its sample. One Undo removed all four assets; Redo restored
  their exact identities, metadata and PCM. Library rescan kept the search text and
  focus, displayed the correct filtered results, and left the song revision unchanged.
- A 32-byte trigger name was refused with no revision or asset change. An exact
  31-byte name created an empty 128-note keymap and a MIDI-channel-1 assignment
  after the Pattern-focus workaround. External Undo of assignment then creation,
  read-only Reload, and **Discard removed assignment** released native Save.
- The first library folder/file pickers kept Open disabled despite valid selections.
  Cancel preserved the song. A minimal AppKit picker worked; the application's main
  project picker then reopened the disposable song, after which folder selection
  and indexing worked. This initial stall is recorded without attributing a cause.
  The final build must repeat first-launch picker checks without concurrent test apps.
- CTest initially passed 141/142: `midi` failed “Receive all five MIDI events.”
  The isolated follow-up result must be recorded separately, not replace this result.

Final release identity and follow-up observations will be appended here. Local logs and disposable fixtures live
under `bin/mac-parity-current/evidence/` and are intentionally excluded from Git.
