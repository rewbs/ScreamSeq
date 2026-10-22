# Scratch inspector refresh after external history

## Retained x64 CI replay, 22 October 14:43 UTC

Windows run39793062072 / job119873630506 finished with a failed merged-app
stage: 92 cases,15 failure records across12 unique playback-dependent cases,
and2 skips. The158 portable/worker and49 native registrations passed, as did
the separate four scratch cases,39 vendor-ownership cases and18 conformance
checks. The retained runner report explicitly lists no audio endpoints and an
inactive recording clock. This remains a failed CI job, not a waived assertion.

The retained artifact12434490236 was downloaded once and safely extracted.
ZIP SHA256: `505fb882c86e19de5bb9383207687f6176ca8126a60e629ea854ef5ee21a3d9d`.
Its x64 PE executable SHA256 is
`20636cb7562edcc7b3f047b2e7dca3ced144c8c383f95d0c7af42eb42914489d`.
Archive, executable and build-receipt hashes match their recorded identity.
Synthetic merge `b4ebe926f7918232d62abb509c815ac88f5710f9` and candidate96b
have the identical source tree `8a20baa0fb183e6c03aa9ca5d24053128441ea1a`.

One local replay used this unchanged CI executable, exact96b test source,
silent WASAPI and a never-switched private desktop. All12 affected methods
passed in36.817seconds in the original order, including the plugin recording
Keep/history/Redo/reopen/discard case that failed twice on the older1fb binary.
No assertion, timeout or test ordering changed. The process suite exited0 and
the executable hash remained unchanged. Receipts, original CI log/device report
and replay log are in `scratch-refresh/ci/windows-x64/` under the existing
`bin/mac-gesture-20261022/` evidence root. This is a successful bounded check of
the new candidate under Windows ARM64 x64 emulation, not proof of native x64
hardware, foreground input, sustained audio timing or absence of all races.
The earlier failures remain retained; no additional build was performed.

Mac ARM job119873630766 has successful model/interface/socket/vendor/library
and conformance steps in the fetched log; its top-level status still reported
in_progress at14:42UTC. Intel job119873630187 was still building. Do not mark
either job terminal without a later status. Final reciprocal edited-file checks
and the unbuilt scratch feedback/command palette fixes remain open.

## Foreground diagnosis and status correction, 22 October 14:16–14:22 UTC

The unchanged96b candidate passed all20 independently selected app cases,
including the original12-case ordering that reproduced the prior x64 recording
Redo timeout. `app-diagnostic.json` records this separately from the failed native
aggregate. It does not erase the90-second native timeout or qualify native x64.

Computer Use inspected owned PID44228 with a disposable F04 copy and silent
48kHz /480-frame output. Scratch drag changed the middle motion point from
0.5cycle /100% to0.5625 /71.984%. Main Undo and Redo refreshed the open curve
without Reload. The sampled immediate Redo display temporarily used straight
segments before the asynchronous preview restored smooth/step shapes; no claim
of frame-pacing or temporal flicker qualification follows from these images.
The Gainer slider applied5.856dB; Main Undo updated the still-open parameter
window to0. Dock search typing, Backspace, Tab/Shift+Tab and resumed typing
retained focus. Ctrl+Alt+P returned Pattern focus; Right detached editing.
Maximize/restore retained readable controls and search text. The private song
saved and the process exited normally; original F04 bytes were unchanged.

The scratch status still claimed a busy retained draft after an accepted edit.
Source inspection found two read-only paths that could overwrite successful
write feedback: the redundant catalogue read immediately after accepting the
complete guarded phrase, and a busy deferred curve-preview read. The new source
updates the existing catalogue entry from the accepted phrase, retaining its
unchanged linked-use metadata. Busy preview/clean-refresh reads retry quietly;
classified NotCommitted writes explicitly say they are waiting for automatic
retry. Unknown write outcomes still require review, and genuine preview
validation errors remain visible. Native admission coverage now asserts those
distinctions, no duplicate write, eventual two-lane preview and retained focus.
This correction is **unbuilt** and joins the split native group at the next gate.

The diagnostic foreground receipt is `scratch-refresh/foreground-check.json`,
with status `diagnostic-completed-needs-correction`. Audio measured364.376s,
20deadline overruns, zero device errors/starvation and failed realtime audit.
This is functional UI evidence, not a realtime pass. Floating-field keyboard
continuity remains bounded by the Computer Use target-window limitation and
the separate native focus/caret assertions. No production package or merge.

## 14:07 checkpoint and bounded group correction

Frozen source `96b6971e56f0c3f297a713ac7254afad87c46099` built successfully once
from 14:07:38 to 14:09:29 UTC. ARM64 Release executable SHA256 is
`945bb24bcf3b6596eac146284a77d3053f64b7951b5743d2aef7aa045e2bd554`.
PR #6 now has that exact head; Windows run39793062072 and Mac run39793062128
were dispatched alongside the local build. Next coordinated build must start
no earlier than **15:07:58 UTC**; inspect `scratch-refresh/checkpoint.json`.

Departure and parameter-preview native groups passed. Within command-results,
both scratch write-admission and external-refresh cases passed, as did each
logged case before the final song-routing catalogue refusal. The outer child
hit its unchanged90-second bound during that last case. The preceding 1fb group
took87.77 seconds and the new scratch case took2.845 seconds; the aggregate
no longer has adequate room. Original native.xml/log/checks.json retain failure.

Source now moves both scratch cases to `workspace-scratch-editor-tests`, retaining
every assertion and the90-second child/120-second CTest bounds. This scheduling
change is unbuilt; do not call the timed-out group a pass. Independent app
diagnosis runs the original12-case order plus eight affected scratch/rack/review
cases, with all original assertions. Its separate `app-diagnostic.json` does not
replace the failed native receipt or authorize packaging. Foreground diagnosis
may use this built binary after app tests finish; production packaging remains
gated on complete qualification.

The second ordered run of the prior x64 binary reproduced the recording Redo
timeout. Its document revision matched the expected revision, while a visible,
clean, non-pending parameter inspector stayed one revision behind. No Reload
error or unknown write was present. New workspace-only refresh counters in96b
distinguish delivered timers, attempted reads and busy deferrals on recurrence;
they add no audio-thread work or song data. Preserve both old failure logs.

## Parameter refresh follow-up, 22 October — source only

The same batch now protects automatic plugin-parameter reads with a short native
input lease instead of disabling their focused controls. Search/value fields keep
their HWND and caret selection; a focused Reload button also remains enabled.
The read still guards document/revision/generation, and a revision that changes
during a background read retries the read without forcing a permanent Reload.
Dirty/uncertain controls retain their previous protection. Explicit Reload and
durable writes retain their existing admission rules. This is a source-confirmed
focus hazard, not an explanation proven for the intermittent timeout below.

`applicationParameterRefreshFocus` covers focused numeric/search/button controls,
reentrant input rejection, busy reads and changing-context reads. The existing
parameter-preview group executes it. These new assertions are unbuilt. Add the
two actual-app rack/search cases and all three plugin-recording cases to the four
scratch cases at the next checkpoint (nine cases total). No additional build.

Current Windows x64 CI run39784093856 passed158 portable/worker and49 native
UI/preferences/recovery registrations. Its audio diagnostic returned devices=[];
the merged92-case app group retained15 failures (12 unique playback-dependent
methods) and2 skips. The exact CI binary, SHA256
`f33c65d4f96520f1ba4df9fc9e66bbf7d4e81d277242e4163728c1e2aa62698a`, was downloaded
and hash/PE-verified locally. Synthetic merge d10e53f1 has exactly the1fb source
tree. Local silent-WASAPI execution passed11/12 affected cases; the plugin take
Keep/Undo/Redo case timed out in the fixture's inspector idle wait after Redo.
One focused diagnostic and three bounded repeats passed unchanged assertions.
The original failure is unresolved, not erased by those passes. Improved timeout
diagnostics retain expected/current document and full workspace state for the
next occurrence. Receipts/logs: `focus-followup/ci/windows-x64/`. These runs used
the unchanged x64 CI executable under ARM64 emulation, with the653 fixture-only
long-path helpers; they do not qualify native x64 hardware or realtime behavior.

## Observed on 22 October, 13:18–13:28 UTC

The 13:07 coordinated checkpoint built source
`1fbfebd7346013926b908250390b8dd0b5af31a7` once. ARM64 Release executable SHA256:
`9cdd7271e4a26102f9561f07533eb966e4a0a89d0735023a678eda8322f7f56c`.
Receipts are in `bin/mac-gesture-20261022/focus-followup/`.

Six native groups passed, including the repaired scratch focus/caret admission
case and deep recovery storage. The original 23 app/controller cases produced
21 passes and two Python long-path fixture errors. Commit `653b2697a` corrects
only fixture access/cleanup; those same two cases then passed against the
unchanged executable. `qualification.json` combines the exact 21+2 results,
without replacing the failed original logs. This is not a second build.

Fresh Computer Use controlled disposable PID 38636, with a copy of F04 and
silent 48 kHz / 480-frame output. It observed:

- Tools → Pattern → Scratch phrases opens the paired-curve window. Dragging
  the middle motion point changed it from 0.5 cycle / 100% to 0.5625 / 71.725%,
  retained the rendered curve and reported that linked uses were saved.
- Ctrl+Z addressed to Main consumed that one history entry, but the still-open
  scratch window continued to show the old curve and saved status. This is a
  clean stale inspector, not evidence that musical Undo failed.
- Arrow keys selected EQ10 in the native rack list. Its dock search retained
  `frequency`, Backspace produced `frequenc`, and Tab / Shift+Tab followed by
  typing `y` restored `frequency`. Ctrl+A / Backspace cleared the field.
- Params exposed ten controls on page 1 of 5. The output gain slider applied
  5.1264 dB during silent playback. Main Undo consumed the entry; Reload values
  showed 0 dB. A screenshot immediately after reopening showed the old value;
  it did not test the existing 250 ms parameter refresh timer. Do not classify
  this observation as a confirmed plugin timer failure.
- Maximize/restore kept the native workspace readable. Ctrl+Alt+P followed by
  Right changed Pattern's active field from Note to Instrument. The private
  song saved, the process exited normally, and original F04 bytes were unchanged.

Computer Use treats floating tools as related windows and key injection targets
Main. Native focus/caret tests supply the scratch typing evidence; this session
does not establish direct floating-field typing continuity. Screenshots do not
qualify temporal flicker. The loaded silent session reported 35 deadline
overruns in 429.317 seconds, zero device errors/starvation indicators, and
`realtimeAuditPassed:false`; it is functional UI evidence, not a realtime pass.
No production package is approved by this foreground receipt yet.

## Reproduced baseline and source correction — unbuilt

The new actual-app case
`ScratchEditorTests.test_main_history_refreshes_clean_curve_without_retargeting_or_losing_draft`
fails on the unchanged 1fb executable: after an external curve mutation, the
inspector remains `stale:true`, `dirty:false` with its original points for five
seconds. `scratch-refresh-baseline-ready.log` preserves that failure. An earlier
setup-only attempt lacked TMPDIR; its log is retained and is not product evidence.

`Main.cpp::refreshDocument` now notifies the scratch owner. Its event-scheduled
refresh reads only a clean same-document phrase after the worker returns. It
preserves lane and viewport, keeps point selection only when the same cycle
position still exists, and does not create history. A deleted phrase becomes an
empty selection rather than silently choosing another phrase. It revalidates
the original stable pattern/track/cell and command; cursor navigation cannot
redirect it. Changed captured commands still require explicit recapture.

Raw field edits, active drags, queued saves, unknown write outcomes and retained
formula work keep their captured revision and contents. Focused text fields
defer clean refresh; no refresh steals focus or disables their HWND. Escaping
an unfinished point schedules the deferred read. Hidden tools stop their timers.
No song data, API mutation, audio processing or project format changes.

## Next bounded checkpoint

No build before **2026-10-22 14:07:23 UTC**, including PR-triggered CI. The local
build started 13:07:18 UTC; paired Mac/Windows runs started 13:07:22. Inspect
receipts before execution. Current PR remains 1fb; backup branches need no CI.

Build ScreamSeq and workspace-restore-tests together once. Run native
workspace-command-result-tests (new scratch refresh/focus assertions),
workspace-parameter-preview-tests (new explicit clean timer assertion), and
workspace-departure-tests. Run all four actual-app scratch cases, including
the previously failing baseline. Reuse unchanged recovery and recording results
from this checkpoint; broaden only for a failure or another affected subsystem.

Fresh foreground checks must repeat scratch drag → Main Undo/Redo with the
inspector visible, a clean external edit, raw invalid text retention, and normal
keyboard navigation. Check plugin Undo after the existing refresh interval,
not just immediately after reopening. Preserve the private process and original
fixture discipline; never run private-desktop input concurrently with Computer
Use. Package only after these gates pass, retaining scanner/runtime/notices,
exact source/hash and documented limitations. Mac/Windows CI and final native
audio/reciprocal integration requirements remain separate merge gates.
