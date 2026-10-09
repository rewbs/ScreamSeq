# P1 mixer preparation

This is preparatory work on temporary branch `codex/windows-parity-mixer` in the
already attached `parity-ci-layout/ScreamSeq` worktree. It starts from `3a05e5e28`
and is separate from the running P0 candidate `494d65563`. Integration still
depends on the P0 gates; neither P0 nor P1 is claimed complete. No builds/tests
were run while preparing this slice. Both platforms must continue to converge on
shared main; this branch is not a permanent Windows implementation line.

## Shared extraction prepared

`editor/MixerControlEdit.hpp` now owns two pure control-thread operations:

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
history, save formats, stable identities and plugin state are unchanged in this
mechanical slice. This is not yet the complete typed bus candidate or gesture
controller described by P1.

The existing Mac/Windows publication-order difference is deliberately still
visible: Windows durable edits publish in `Document::annotate`'s admission
callback; Mac currently publishes mixer controls before annotation. Resolve that
boundary with failure/history tests in the next cohesive P1 slice, rather than
claiming this extraction makes host publication atomic.

## Prepared checks and next work

`editor/Tests/MixerControlEditTests.cpp` is registered as `mixer-control-edit` in
both native CMake projects. Windows labels it portable, so its ordinary portable
CI selection includes it; configured inventory still needs verification at the
build checkpoint. Cases cover unchanged final values, each control/presentation
field, simultaneous control plus structural changes, bus reorder and identity,
detached roots, exact frame values/audibility, unchanged saved graph and rejected
incompatible plans. These checks are written, not executed.

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
