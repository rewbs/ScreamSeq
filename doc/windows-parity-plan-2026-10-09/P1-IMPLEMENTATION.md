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

## Details completion ownership — source only

Mixer Details enable/add/remove/apply now submits through the existing retained
native command-result owner. A completion/presentation failure retains the exact
receipt and original request; an unknown outcome blocks a second command and
document retirement. Reload becomes Review result while reconciliation is needed.
Known-result review retains newer raw Details fields. Unknown-result review reads
the current mixer and guards the observed document/revision/generation through
acknowledgment; it never resends the write. Strips cannot begin a competing gesture
while this command result is unresolved.

The existing workspace command-result fixture now includes completed Mixer Apply
with a later raw name and an unknown mixer result whose review is invalidated by
a concurrent bus edit. These are source-only additions to the same bounded
workspace command-result group; include that group at the P1 checkpoint.

## Native strip viewport and navigation — source only

Replaced the page-only binding policy with a native horizontal scrollbar and a
pool of visible strips plus one neighbor on either side. Existing focused and
captured gesture HWNDs are reserved before recycling; unchanged neighbor IDs keep
their HWNDs. A deleted focused bus remains unavailable until focus leaves. A
captured raw field can remain offscreen while scrolling without losing text,
selection or generation; the next key reveals that same field. Resize and
external reorder retain the focused bus in view. Pool capacity supports the
model's 240 buses without creating all song controls in an ordinary viewport.

Native sibling order follows musical bus order after recycling, preserving Tab
and Shift+Tab traversal. Tab can enter the strips from the root focus used by the
command palette. Strips and Details now have explicit palette/shortcut entries.
Wheel input over any strip child scrolls the viewport instead of adjusting a
trackbar with no end-track notification; partial wheel deltas accumulate. Normal
wheel scrolls vertically in short docks; Shift+wheel and horizontal wheel move
between buses. Partial neighboring strips now paint their own meter observations.
Workspace inspection exposes retained bus/control bindings and pool size for
actual-process qualification.

The existing native strip test now has a 240-bus fixture. Its new assertions cover
bounded allocations through a complete bidirectional traversal, unchanged neighbor
HWNDs, late Master, unique bus/position bindings, raw text/caret/generation retention,
keyboard reveal and Tab order, and partial wheel navigation with no musical write.
These tests have **not run**. Static diff inspection completed; no build/test cycle
was started. Next normal local build remains no earlier than 2026-10-10 00:19:51
UTC. Include native strips, workspace command/recovery and actual-app navigation
with the already-required P1 shared/native mixer checks in that checkpoint.

Still open: runtime viewport/input verification (including mixed DPI), actual UIA
and high-contrast treatment, Add Return and destination-aware routing/effect entry
points, naming/color ergonomics, F04 audible gesture/Undo/save checks and both
platform qualification. This supersedes the earlier page-only limitation, not
any unexecuted qualification gate.

## Bus creation, color and view dispatch — source only

Details now exposes Add Return alongside Add Group below its bus list. Both also
have palette entries. They use existing `mixer.bus.add`, select the returned
stable bus ID and reveal its name field. No implicit send is added. Structural
commands reject unfinished Details drafts even when invoked outside the disabled
button path. Names and a six-digit RGB color field use one existing guarded Apply
operation; invalid raw color remains owned by the same draft and revision.
Stored colors appear as a small strip accent, with names still providing identity.
No codec, API shape or native audio change is needed for these existing fields.

Strips/Details view commands now participate in deferred view opening, captured
target checks, lower-host reveal and native focus retention. A song-routing bus
inspection explicitly opens Details. Existing Main draft Review already selects
Details, and continues to do so.

Added unexecuted workspace command/recovery cases for opening from Pattern focus,
busy/deferred activation and stale target refusal, stable Return creation with
Undo/Redo, preservation of existing routes, invalid color/raw-draft protection,
Strips/Details switching, combined name/color Undo/Redo and native save/reopen.
The new color field is included in initial-layout preparation and adoption. Its
normal keyboard and mixed-DPI presentation still require the next native gate.
Because common command dispatch changed, that checkpoint must also include the
workspace/departure/shortcut/typing/recovery application group, in addition to P1
native strip/model/PCM checks. No build or test was run for these changes.

The bounded P1 feature slices below are prepared but unqualified. Physical
UI/accessibility and reciprocal
Mac fixture checks remain open; prepared source does not close them.

## Destination-aware effect browser — source only

Mixer Details and the command palette now expose **Add effect to selected bus**
(`mixerAddEffect`, 422). They open the existing retained `PluginLibraryWindow`
with the captured document and stable bus identity. The browser displays the bus
name/ID, restricts the list to effects, and provides **Rack destination** (3216)
to explicitly return to ordinary rack insertion. Existing category drafts,
pending operations, uncertain results and unacknowledged readback prevent
retargeting. Reopening the ordinary browser preserves the visible destination.

Insertion reads current `mixer.get` to verify the captured bus still exists,
rechecks document/revision, selection and generation after the message-pumping
read, and submits existing `plugin.add {descriptor,target,expectedRevision}`.
Windows `PluginOperations.cpp` already stages plugin creation and insertion as
one validated commit; no API, codec or audio integration change was needed.
The pending read prevents recursive submission. Deleted buses and replacement
documents require an explicit new destination; they never fall back to Master.
The browser's unknown-result review now records routing alongside the rack,
without resending or attributing a merely matching current state to the request.

Prepared acceptance cases (not executed):

* Native library completion fixtures cover captured bus insertion, exact and
  unknown completion, routing readback, and refusal to retarget unresolved work.
* Admission fixtures cover deleted destination, revision changes and newer raw
  category input during reads, plus recursive Add and destination-change refusal.
* Real workspace/worker fixture exercises the Mixer command, effect filtering,
  lost completion after atomic insertion, preservation of every other bus, one
  Undo/Redo, stable plugin and bus IDs through save/reopen, document-replacement
  and deleted-bus refusal, and explicit return to rack destination.
* Existing invalid Mixer draft case now also refuses the effect-browser action.

Next consolidated Windows checkpoint must include `NativeToolWindowTests` and
workspace restore/application library and command cases, alongside the already
pending strip/model cases. Full foreground layout, keyboard, UIA and mixed-DPI
acceptance remains open. Only `git diff --check` was used during preparation;
no build or product test was started. The normal local build remains gated until
2026-10-10 00:19:51 UTC or later, and should include the next ready P1 slices.

## Instrument Route Here — source only

Details and the palette now expose **Route instrument here…** (423). The native
popup groups outputs by stable plugin identity and displays actual port names,
current destinations, implicit Master versus unrouted auxiliary outputs, and
unsupported/unavailable states. Supported inactive outputs are explicitly marked
as enabling on playback, matching existing API admission. An empty catalog never
fabricates a main output. Native menus provide Windows keyboard/DPI/theme behavior.

`windows/App/MixerInstrumentRouting.inc` owns the presentation and captured menu
intent. The existing `mixer.instrument.route {plugin,output,target}` transaction
moves only the chosen output to this bus. Its existing multiple destinations are
replaced, as the menu's “one output only to” heading states; other outputs and
plugins are preserved. The full Routing editor remains the branch-editing path.
The action rechecks document, revision, destination and draft generation after
the native menu loop and uses the existing retained Mixer command receipt and
Review path. Cancellation performs no write. Mixer pending ownership protects
the menu interval; raw drafts and strip gestures block entry.

Both Route Here and Add Effect now reject stale Mixer context before opening.
They no longer refresh and potentially choose a first bus when the originally
selected bus disappears. Explicit Reload may choose a current selection, which
the musician can inspect before invoking either action.

Prepared checks: `MixerInstrumentApplicationTests.inc`, invoked by the existing
workspace command-result group, covers menu identity/port labels, unavailable
outputs, native label escaping, cancellation, changed destination, newer raw
draft, stale context and an actual document edit during menu selection. It uses
synthetic catalog data only for menu/admission cases; it does not claim hosted
instrument execution. `MixerOperationsTests.cpp` extends the existing instrument
fan-out fixture with actual Route Here mutation, dry run, one-step Undo/Redo,
no-op revision/history, invalid identity/output rejection, unrelated output
preservation and metadata roundtrip. Full native save/reopen with a hosted
multi-output instrument and audible routing remains required at the P1 gate.

No build/test was started for this slice. At 2026-10-10 00:00:07 UTC the existing
Intel job 114066597619 had completed its build successfully and was executing
native model/host tests at frozen d39; it does not qualify this P1 source.

## Declared routing ports and retained routing outcomes — source only

`SongRoutingPorts.inc` gives the retained Song Routing owner native input/output
selectors (3837/3838) populated by `plugin.buses.get {plugin:<stable ID>}`. Rows
show actual logical port indices, names, channel counts, unsupported layouts and
inactive ports that enable on playback. There is no invented input 1 or default
auxiliary output. New plugin sidechain, output and cable routes require an explicit
catalog choice; existing selected wires recover their saved port if available.
Unavailable saved ports remain visible as unavailable and their connections may
still be removed. Graph-stage numeric ports remain unchanged for the later P5
graph-catalog work.

Catalog reads are scoped to document/revision/plugin/draft generation. A read
that pumps newer raw input cannot replace that draft. Empty, unavailable,
unsupported, wrong-identity or malformed catalogs cannot submit a connection.
Fresh generic plugin socket gestures now retain a route draft until declared
ports are chosen; they cannot guess a destination input. Compatible explicitly
selected port drafts retain their choice. The existing exact-edge merge helpers
continue to preserve other sidechain sources and output branches.

Discovery also exposed an unsafe completion boundary in this existing owner.
`SongRoutingWindow` now accepts the actual typed write callback from Application
and uses `NativeWriteCompletion` for every musical/layout/history write. Exact
receipts are retained through presentation failures; uncertain writes block
resubmission and document departure. **Review result** reads current graph state
without repeating a write. Unknown observation is labeled unverified and requires
explicit Reload before another write. Newer raw drafts survive exact/unknown
Review; changed-generation, malformed and failed observations retain the pending
result. Reload itself now stages its document/revision/generation before adopting
readback. A retired routing owner is recreated on the next open after successful
document replacement.

Prepared tests in `SongRoutingApplicationTests.inc` run in the existing workspace
command-result group. They use the actual Built-in Gain input catalog and worker
for explicit port selection, sidechain commit, lost exact completion, newer raw
gain retention, departure refusal, no resubmission, single Undo/Redo, native
save/reopen and owner recreation. Separate transport-boundary cases exercise
unknown outcomes, failed/malformed/changed-generation Review and mandatory Reload.
Catalog variants cover unavailable, unsupported and empty inputs and newer input
during catalog reads. The actual-app `test_song_routing.py` helper now chooses
named catalog rows rather than writing hidden numeric controls; its existing
provider-backed sidechain/auxiliary, branch-preservation and PCM assertions remain.

No build or test was run for this preparation. The next consolidated checkpoint
must include the workspace command-result group and the routing application suite,
with provider/instrument fixture caches supplied for its hosted cases; absent
fixture/device prerequisites do not qualify those cases. Native popup/dropdown
presentation, keyboard traversal, UIA and mixed-DPI checks remain open. No Mac,
shared musical operation, project format or audio/device implementation changed
in this slice. At 00:07:44 UTC the original Intel d39 job had passed its native,
Swift recovery/picker and sample-library steps and was running the interface
harness; final conformance and job disposition were still pending.
