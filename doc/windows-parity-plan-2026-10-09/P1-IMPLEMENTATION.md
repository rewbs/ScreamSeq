# P1 mixer preparation

This is preparatory work on temporary branch `codex/windows-parity-mixer` in the
already attached `parity-ci-layout/ScreamSeq` worktree. It starts from `3a05e5e28`
and is separate from the running P0 candidate `494d65563`. Integration still
depends on the P0 gates; neither P0 nor P1 is claimed complete. No builds/tests
were run while preparing this slice. Both platforms must continue to converge on
shared main; this branch is not a permanent Windows implementation line.

## Shared extraction prepared

`editor/MixerControlEdit.hpp` now owns these pure control-thread operations:

* `MixerControlPatch` and `applyMixerControls(candidate, busID, patch)` own the
  typed control candidate: stable-ID lookup, finite musical ranges, exact omitted
  values and no-op reporting. Validation finishes before any field assignment.
  Both native adapters use it for pre/post gain, pre/post pan, width, mute and
  solo. Native wire type/error validation remains in the adapters; timing,
  presentation metadata and structural routing remain in their existing paths.
* `classifyMixerControlEdit(before, after)` reports actual graph change and the
  existing API's control-only classification. Both native adapters previously
  duplicated the same field-reset comparison. Gain, pre/post pan, width, mute,
  solo, name and color remain eligible. Bus identity/order/count, timing, inserts,
  routes, detached chains and final output disconnection remain structural.
* `prepareMixerControlFrame(candidate, plan)` projects detached-chain roots and
  produces the existing `MixerControls` values in bus order, with the compiled
  plan's audibility. It validates plan size and indexed bus membership before
  returning a frame. It allocates only on the control owner, never in audio code.
  Native bounded publication and adoption remain the host's responsibility.

`windows/Session/MixerOperations.cpp` and `mac/Bridge/MixerAPI.inc` both consume
these helpers. The Mac translation unit includes the shared header through
`TrackerSession.mm`. Wire methods, numeric parsing, validation, preview/dry-run,
save formats, stable identities and plugin state are unchanged. The typed control
candidate is now connected to both adapters; native strips and gesture ownership
remain unfinished.

Mac control publication now follows the existing Windows ordering: prepare the
frame, stage document/history allocation, then publish in `Document::annotate`'s
admission callback. A rejected queue leaves the old model/history untouched. The
unchanged-final path explicitly publishes without annotation, so cancel can
restore the saved frame after a preview without a phantom Undo. Native audio
queues and routing preparation remain platform-owned. This source change is
unqualified; it is not evidence that every host failure path is atomic.

## Prepared checks and next work

`editor/Tests/MixerControlEditTests.cpp` is registered as `mixer-control-edit` in
both native CMake projects. Windows labels it portable, so its ordinary portable
CI selection includes it; configured inventory still needs verification at the
build checkpoint. Cases cover unchanged final values, each control/presentation
field, simultaneous control plus structural changes, bus reorder and identity,
detached roots, exact frame values/audibility, unchanged saved graph and rejected
incompatible plans. These checks are written, not executed.

The next prepared slice adds typed-patch boundary/NaN/infinity/omission/false/zero
and reordered-bus cases. `DocumentHistoryAllocationTests.cpp` now sweeps every
allocation during ordinary and coalesced annotation admission, checking that no
publication precedes successful staging, that rejection preserves a Redo branch,
and that retry/Undo/Redo retain the complete entry. It separately rejects the
publication callback in both annotation modes. These are shared-document checks,
not a substitute for native queue and PCM checks.

`windows/Tests/MixerOperationsTests.cpp` adds actual adapter preview/no-op-reset,
dry-run, queue-refusal, retry, exact values and single-Undo/Redo checks, including
an active but non-playing host. `mac/Tests/UnifiedHistoryTests.mm` adds the same
successful control values, invalid multi-field rejection and preview/reset/Redo
history checks through the real bridge with no device. Existing Windows
`MixerIntegrationTests.inc` remains the prepared-renderer/PCM oracle. A Mac
queue/PCM checkpoint is still required; bridge history tests do not establish
live playback correctness.

At 22:36 UTC on 9 October these edits have passed only source/diff inspection.
No build or test was run. The existing Windows x64 and Intel Mac P0 jobs were
confirmed live at 22:33 UTC and were not restarted or repurposed. The next normal
local consolidated build remains no earlier than 23:13:34 UTC; reaching that time
is not a reason to build an incomplete batch.

Continue P1 with native Windows strips and a shared pure gesture state, retaining
the existing Mixer Details owner and SongRoutingWindow. Capture stable bus and
document/revision, begin/preview/final/cancel and uncertain outcome explicitly.
Preview and Cancel add no history; successful final commits once. Reset also
needs a prepared but paused renderer. Preserve typing/focus, raw numeric text,
bounded visible HWND ownership and independent selection while meters update.

At the next necessary consolidated P1 checkpoint, run the new pure test and
existing mixer-document/live-parameter cases, then both native apps and affected
Mac mixer/history tests. Include native strips, workspace and actual pipe tests
once that UI is in the batch. Publication changes additionally require bounded
queue/failure and controlled PCM evidence. Do not rebuild or rerun the unrelated
P0 job merely because this separate preparation exists.
