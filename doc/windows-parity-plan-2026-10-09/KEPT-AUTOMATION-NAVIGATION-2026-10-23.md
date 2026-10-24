# Keep a take, then refine its automation

## Observed gap and implemented source

The Windows take window previously cleared its target list on successful Keep,
with no direct route from that completed workflow to the existing song automation
editor. The editor itself already supports native point edits, automatic saves,
Undo and persistence; this batch connects those existing capabilities.

`ParameterRecordingWindow.hpp` now captures the selected stable plugin/parameter
when submitting Keep (or the first captured target when reviewing input delivery).
Only a matching, successfully reconciled Keep completion that released the take
enables **Song automation…**. Unknown outcomes, pending writes, retained native
cleanup and an active take cannot enable the action. A newly observed take clears
the previous shortcut. Empty takes have no target to open. This is navigation,
never a replay of original input, another Keep, or a musical edit.

`ParameterRecordingIntegration.inc` delegates to `inspectParameterSource`, which
validates the original plugin and parameter and opens the existing absolute song
automation window. That path rejects an existing point draft instead of replacing
it, and checks document/revision/cursor continuity. It does not change transport.
The new button follows input details in Tab order, fits beside pagination at the
650-DIP minimum width, and retains the native Windows window convention (`…`).

## Required qualification — not yet executed

The `plugin-search` native group now checks disabled navigation during capture and
uncertain Keep, late receipt reconciliation, no musical write during navigation,
clearing on a new externally observed take, and selecting the second of two
captured parameters. Existing assertions remain unchanged.

`test_plugin_recording.py` adds the route to its complete native recording journey:
keep, open the exact lane, require actual recorded points, preserve music/history,
preserve a detached pattern cursor and the running playback epoch, then continue
Undo/Redo, Discard and save/reopen. The layered/sample and cross-domain variants
inherit this check when next required; the cheapest new gate is the basic native
record/keep/history/reopen/discard case.

Combine this with the pending keyboard-fixture correction in one checkpoint.
Build app/scanner plus the existing workspace/precise native targets once; run
`workspace-menu-bar-tests`, `precise-note-window-tests`, `workspace-plugin-search-tests`
and `workspace-source-navigation-tests`, the seven precise host/live cases, and
the basic plugin recording journey. Check the actual CTest inventory before
execution. Reuse unchanged Mac, shared renderer, independent-layer PCM and
reciprocal persistence evidence. No extra broad suite is justified by navigation
alone. A failure, changed source dependency or unexpected musical write broadens
only the affected checks. Fresh isolated Computer Use must check the new action,
minimum-size layout, exact lane, keyboard focus and uninterrupted silent playback.

Current source review and Python AST parsing passed. Compilation, native/app
execution and fresh foreground validation remain outstanding. No new package or
merge follows from source-only evidence.

## Larger Mac live-restoration work

Source inspection confirms `AudioDevice::restoreStoppedPluginBaselines` invokes
`PluginChain::state`, and `VST3Plugin::state` flushes pending edits with a zero-frame
process call. Neither may be reused concurrently with rendering. Existing opaque
repair also changes native notification providers and requires closed editors.
Removing those guards is not a live-restoration implementation.

Steinberg explicitly permits UI-thread component state reads while processing:
[IComponent API](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/master/vst/ivstcomponent.h)
and [processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html).
This supports investigating a separate non-flushing provider snapshot interface,
without calling current host state capture or touching audio-owned queue fields.
The intended next architecture is to canonicalize an observed opaque snapshot in
a disposable private vendor against accepted manual baselines, verify original
provider/input cuts, and release only after preview cancellation is adopted.
This remains a proposal, not verified behavior. AU threading guarantees, snapshot
coherence, queue adoption, vendor callbacks that pump input and failure retention
need explicit native tests before either platform's safety guards change.

## Qualified Windows navigation checkpoint

The `cc5553aab` candidate passed all four selected native groups. Its basic
recording app journey initially failed an overbroad comparison: `context.get`
contains live playback telemetry, whose row correctly advanced while the editor
was detached. The Python correction preserves exact comparisons of every other
context field and adds increasing-frame, unchanged-region/epoch and continued
playback assertions. The corrected case passed without rebuilding; prior failure
and test diff are retained alongside the composite qualification receipt. The
remaining seven precise host/live cases passed in the original run. See
`bin/mac-gesture-20261022/kept-navigation/qualified-checks.json` and the
[precise-note report](PRECISE-NOTE-LIVE-2026-10-23.md).

Fresh Computer Use on PID43732 opened recording from Playback, started a take,
hid its window, opened Plugins → Params, and dragged Gainer's Balance slider.
Recording review showed two captured original notifications with no retained
delivery failures or loss. Stop left playback running. Keep identified
`Gainer / Balance` and enabled Song automation, which opened parameter2 with two
points. Read-only snapshots prove exact music and editing-context preservation,
increasing frames and unchanged epoch6 across navigation. The recording window
was narrowed and shortened to its minimum; its target, details and new action
remained readable. It was then closed, the disposable song saved, and the QA
process exited normally. A first border drag did not resize; subsequent right
and bottom border drags did. One stale accessibility index was refreshed before
the visible Stop control was clicked; no action was blindly repeated.

Foreground floating-field typing continuity was not claimed: the helper can
reactivate Main. Precise-field keyboard focus was freshly checked and the native
keyboard assertions passed. Screenshots are not temporal-flicker evidence. Mac
primitive CI and the larger live-restoration work remain separate gates. The
usable Windows package contains this navigation batch and the precise-note work,
with executable SHA-256 `940df0bfe7263f1d60527a50ec679740674b5b6d2b80fdc05b117654c1f3ba33`.
