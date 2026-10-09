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

## Native strips candidate — source only, 22:50 UTC

`editor/MixerGesture.hpp` holds one captured document/revision/bus/control,
unrounded saved value, latest desired value, last accepted preview and draft
generation. It has no native handles or transport. `MixerControlEditTests.cpp`
now also covers coalesced updates, stale contexts, raw invalid-text generations,
exact baselines and boolean values. Neither the shared state nor these added
cases has been compiled or executed.

`windows/App/MixerStripsWindow.hpp` implements the first native strip owner:
stock vertical gain and horizontal balance trackbars, exact numeric gain,
mute/solo, stereo meter drawing and Details navigation. A page contains up to
16 strips; controls are allocated as needed and retained in a bounded pool.
Page bindings freeze during a gesture, and a focused control cannot be rebound
to another bus. Previous/Next and wheel navigation reach later buses. The
pool/page policy still needs comparison with the planned visible-neighbor
scrolling ergonomics; it is not yet the final qualified interaction.

`MixerEditor.inc` now exposes Strips/Details modes while retaining the original
Details draft and SongRoutingWindow. Competing Details writes and strip gestures
are admitted separately, and both participate in document departure. Strip
review reveals the correct parent view; Details review selects Details.
`DocumentDepartureIntegration.inc` releases the retired strip owner after
adoption. Main's read-only Show/mode commands precede write admission.

The strip owner coalesces previews on the UI service tick, sends one durable
final through `NativeWriteCompletion`, and retains uncertain results without
repeating them. Cancel reads current saved controls and uses their current
revision for a preview reset, including when playback is paused. Capture loss
and hiding a live slider schedule a reset after the native notification stack,
independently of the visible-only meter timer. Invalid numeric text stays in
its HWND until Enter or Cancel. Clicking an unmoved rounded slider thumb does
not commit its rounded display value.

`NativeToolWindow.hpp` gained default-no-op scroll/capture hooks and an overridable
draft-review action. Existing owners keep their previous behavior by default.
`NativeInputGate.hpp` recognizes trackbar presentation mutations; the strip
uses the explicit presentation path. These common native-owner changes trigger
the native UI/departure suite at the consolidated checkpoint, not only the new
strip test.

`windows/Tests/MixerStripsWindowTests.cpp` is registered as
`mixer-strips-window-tests` (private desktop, 60 seconds). It drives actual
trackbar notifications and edit HWNDs against a controlled API boundary: preview
coalescing, one final, untouched rounding, external-revision cancellation,
invalid raw retention across hide/show, capture loss, and uncertain-result
review/acknowledgment without a second durable call. This does not replace real
pipe, renderer, reciprocal-save, accessibility or visual evidence.

Still required before claiming P1 usable: compile this candidate; inspect and
finish compact/short layout and overflow behavior, focus/caret under external
reorder, wheel and keyboard gesture termination, high contrast/UIA labels and
meter adoption identity; update the actual-app/workspace fixtures and commands;
qualify A02/F04 including a late Master and retained Details; complete the
cross-platform and reciprocal gates. No build/test was started for this slice.

At the next necessary consolidated P1 checkpoint, run the new pure test and
existing mixer-document/live-parameter cases, then both native apps and affected
Mac mixer/history tests. Include native strips, workspace and actual pipe tests
once that UI is in the batch. Publication changes additionally require bounded
queue/failure and controlled PCM evidence. Do not rebuild or rerun the unrelated
P0 job merely because this separate preparation exists.

## Compact strips and stable focus — source only, 23:12 UTC

The candidate now freezes the entire displayed set while a strip owns focus or
a gesture. External reorder cannot rebind neighboring controls to duplicate the
focused bus. A removed captured identity remains labelled unavailable with writes
disabled; a retained raw gesture keeps its own field until explicit reconciliation.
Focused text/caret is not reformatted by background refresh. A narrow resize shifts
the visible subset to retain the focused strip, and Ctrl+Page Up/Down provides
explicit keyboard page navigation.

The default 226-DIP dock now separates Balance from Mute/Solo. Shorter docks use
a native vertical scrollbar over a minimum 205-DIP content area, including
keyboard-focus reveal. Meter and control geometry share the same offsets. Result
review/acknowledgment replaces disabled navigation in the toolbar, keeping both
actions accessible at 280 DIPs. A visible Strips workspace recreates its native
owner after document adoption instead of requiring another Mixer command.

Added private-HWND checks cover these geometry, focus, reorder, deletion and
uncertain-result cases. They have not run. Remaining P1 qualification includes
true continuous visible-neighbor scrolling versus the current bounded page pool,
actual accessibility/high-contrast behavior, meter source/adoption identity,
native wheel/keyboard termination, actual-app document adoption and F04/PCM/API/
reciprocal gates. This slice is not a P1 completion claim. No build/test was run.

## Shared meter identity — source only, 23:20 UTC

Source inspection found both adapters pairing the host's retained-plan meter
array with the editor's current bus order. `MixerMeterReading` now carries stable
bus IDs with values and a fresh flag. `PluginChain::identifiedMixerMeters` runs on
the existing control owner, waits for a settled routing plan, reads that plan's
IDs and atomic meters together, and rejects changed/pending/failed generations.
No additional callback allocation, synchronization or DSP work was introduced.
The legacy positional reader remains for low-level device tests; native UI/API
identity assignment now consumes the identified reader.

Windows worker feedback, API meter rows, Details and strips use these identities.
Mac AudioDevice/session does likewise, including the implicit mixer. The JSON
meter row shape is unchanged; pending/unavailable observations have no rows.
Both native strip displays distinguish unavailable readings (an em dash) from
measured silence. Mac's existing unchanged-level label optimization is preserved,
with availability included in its change detection.

New candidate checks cover deliberately reordered meter rows, pending native
publication withholding and post-adoption identities, native Mac device identity
availability, and missing-versus-silent Mac meter labels. All remain unexecuted.
The P0 build at `d39ec5548` is separate and does not qualify these changes. The
next P1 checkpoint requires both platform builds, mixer operations/integration,
Mac mixer/draft interface groups, device-backed meters and existing queue/PCM
checks. UI meter readings are bounded peak observations, not a claim of exact
sample-synchronous stereo snapshots or physical output calibration.

## Complete strip controls — source only, 23:26 UTC

Windows strips now include exact numeric pre-gain, pre-balance and width, using
the same shared gesture/revision and typed completion paths as post gain/pan.
The stock horizontal width slider displays percent while sending the saved 0–2
ratio; balance captions describe their native 0-left/100-center/200-right range.
Per-bus trackbar captions include the bus name. Secondary controls remain in the
same retained strip, reachable through the native vertical viewport in short
docks. Strip HWND IDs now use a 16-wide internal stride to keep controls unique.

Explicit commit/cancel readback now refreshes a focused field deliberately;
ordinary background refresh still leaves focus/caret alone. This corrects the
case where Cancel restored the model but left invalid raw text visibly present.
New candidate HWND checks cover each added control, independent pre/post fields,
exact pre-gain precision, invalid value retention/cancel, keyboard scrolling and
a late Master. The older focus/reorder cases were updated for the internal IDs.
No test was weakened, executed, or rebuilt in this slice.
