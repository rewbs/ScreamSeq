# P4 implementation checkpoint

This records prepared source, not phase completion. P0–P3 qualification and the
remaining P4 asset/curve/manual-recording work stay open. Both native platforms
remain on the same temporary implementation branch and must converge on main.

## Recorder duration choice — prepared, unqualified

Parity matrix row 44 identified a fixed 60-second Windows recorder form. Current
source confirms that `SampleRecordingWindow::begin` hard-coded 60 while
`SampleRecordingOperations` already accepts `maxSeconds` through 300, and
`SampleCapture::Buffer::prepare` caps storage at 16,777,216 frames. Mac
`SampleRecording.swift` offers 60 and 300 seconds. No new capture service or
storage policy is needed for this UI gap.

The native Windows form now exposes a labelled New take limit combo with the
same 60/300-second choices and a 60-second default. It submits the selected value
to the existing revision-guarded `sample.recording.start`. Duration is capture
configuration, like endpoint/channel choice, rather than a musical edit or a
song-scoped raw name/output draft. It survives hide/reopen, Refresh and explicit
name/output setup discard for that retained owner; it is not newly persisted as
a user preference or project field. The existing API remains the automation path.

Start snapshots the choice before entering the pending worker boundary. Native
and programmatic selection changes are refused/restored while pending, while a
take is retained, or while a Keep/lifecycle outcome needs review. Invalid combo
indices preserve the last valid choice. The retained lifecycle request includes
the exact submitted duration; Review never repeats Start. An API-created take
does not overwrite the future recording choice. The take description continues
to show its actual `maxSeconds` returned by the capture service, which may be
shorter at high sample rates. Help text makes that limitation explicit. No frame
cap, allocation budget, device default, audio callback or sample payload changes.

The compact form puts the duration beside input channels, places permission
status below them, and reserves separate rows for take details, Review/Accept,
the meter and help. This removes the previous Review/meter overlap. Its floating
minimum is expressed as a 580×560-DIP client area so nonclient DPI metrics cannot
hide the final controls. Five input/destination fields have explicit native
accessible names. The background and meter use the existing Windows contrast
palette; clipping remains described numerically as well as by color.

Prepared checks extend `DraftImportRetentionTests.inc` and
`RecordingLifecycleReviewTests.inc` in `native-tool-window-tests`: default and
300-second request, invalid selection, global-configuration versus song-draft
classification, retained hide/reopen choice, attempted changes during Start,
retained-take lockout, unknown Start parameters and read-only Review. A fake
capture response reports 16,777,216 frames at 96 kHz and verifies that the form
displays the effective 174.76-second duration while retaining the requested 300.
These checks exercise the native form and request contract; they do not claim
300 seconds of physical microphone capture.

## Bounded qualification

Batch this with the pending consolidated Windows application/native UI build.
Run `native-tool-window-tests`, existing session sample-recording/capacity checks
and the affected actual-app recorder/departure scenarios against that same
freeze. Check actual window geometry, native keyboard combo operation and
accessible names at minimum size and 100/150/200% DPI; confirm every recovery
action, status and footer remains visible. Reuse unchanged capture-buffer and
Mac evidence only with matching relevant source/toolchain/fixture inputs. Mac
needs no new build solely for this Windows form change; shared changes elsewhere
in the consolidated batch still require their Mac gates.

Physical device permission, disconnect/reconnect, automatic limit stop, high-rate
memory behavior, retained Keep/Discard and reciprocal save/reopen remain P4/P7
qualification requirements. There was no microphone access, build or test run
for this source change. The native capture engine's existing strict limits and
the original test assertions are unchanged.

## Plugin parameter and program search — prepared, unqualified

`windows/App/PluginEditor.inc` previously populated the full parameter and factory
program catalogues into unfiltered native combos. The rack now adds native search
fields for parameter name/unit/ID and factory program name/group/ID. Matching is
ordinal Unicode case-insensitive. Search is presentation state: no API mutation,
Undo entry, project metadata or plugin-state change is introduced. The existing
parameter Apply and revision/catalogue-guarded program Load remain authoritative.

Filtered rows carry their original catalogue index as combo item data; command
handlers resolve that row through the catalogue to the existing stable ID. An
empty placeholder has an explicit invalid index. The current selection remains
visible with `[selected; outside search]` when it does not match. Selecting a new
matching item removes the obsolete retained row. Unavailable programs retain
their label and existing Load eligibility. Parameter selection while a raw draft
exists is still rejected and restores the correct filtered selection. Searching
never changes raw value text, captured revision/target or draft generation.

Both searches have explicit native accessible names and precede their results in
Tab order. Enter focuses results; Down focuses and opens the native dropdown;
Escape clears the query without discarding a parameter/program draft. Search
text survives detail refresh and retained workspace hide/reopen in the current
process. It is not a new saved preference. The extra compact row increases the
height needed to expose parameter/program controls from 132 to 164 DIPs, and the
instrument assignment row to 196 DIPs; minimum-size usability must be inspected.
No custom cross-platform widget or plugin audio/host change is involved.

`PluginSearchApplicationTests.inc` adds the private-desktop
`workspace-plugin-search-tests` group to the existing workspace executable and
CI native-UI selection. It prepares 200 deterministic parameters to distinguish
filtered row 1 from catalogue index 179/ID 1179, checks raw invalid-draft retention
and rejected selection, case matching, names, Tab/Enter/Escape behavior, program
IDs/catalogue revision, unavailable entries and empty placeholders. Synthetic
catalogue entries are never sent as musical writes. A real built-in Gain fixture
allows the check to assert unchanged document/plugin history throughout. Existing
actual-app plugin tests remain necessary to cover real Apply/Load behavior.

Add the new named group to the next consolidated local checkpoint selection;
checkpoint 04 is frozen and must not be rewritten. Build the Windows application
and workspace harness once with the other pending changes, then run this group,
existing plugin draft/departure tests and the actual-PID plugin scenarios against
that freeze. Inspect native dropdown keyboard behavior, query retention, minimum
height and 100/150/200% DPI. Broaden to plugin worker/persistence gates if request,
revision, identity or state code changes. No additional Mac build is needed solely
for these Windows presentation changes; shared pending changes retain their Mac
gates. This section records source inspection and prepared checks only: no build
or test was run for the search batch.

## Native discrete parameter values — prepared, unqualified

The rack's parameter value row now interprets the existing catalog through
`windows/App/PluginParameterField.hpp`. Named choices use a native dropdown;
Boolean unit 2 uses a native checkbox with catalog labels such as Off/On or
Normal/Inverted. Other fields retain numeric entry and Apply. The value column
expands for labels without adding another row. Native accessible names identify
the parameter and manual-value role; Enter/Space operate the checkbox. The
retained-draft Review action focuses the visible numeric/program control or Apply
when a discrete candidate is retained.

Presentation reads `manualValue` when present, with legacy `value` fallback.
Nonfinite, nonnumeric, inverted, inconsistent choice or fractional discrete
metadata cannot enable editing or become an unchecked integer conversion.
Read-only controls remain read-only. Choice IDs retain the API's zero-based
numeric contract; labels are never used as persisted identities. Ranges and
units are available in the parameter status. No audio/device/codec change or new
public API field is introduced.

A discrete selection stages its exact raw value and submits the existing
`plugin.parameters.set` once using the persistent plugin identity. The draft
captures the displayed catalogue revision/target, preventing a newly arrived
revision from silently legitimizing old controls. A stale/failed edit retains its
candidate and generation for Review/Apply/Escape. A second discrete change cannot
replace that draft; busy/read-only/invalid requests restore the existing control
presentation. Numeric raw forms retain their existing explicit Apply semantics.
These changes do not yet implement slider audition, gesture history grouping,
manual automation capture, activity navigation or dynamics telemetry; P4a/P4b
remain open. In particular, the current Windows parameter API accepts no preview
or gesture token. Slider work must establish a bounded preview/final/cancel
contract or equivalent shared history boundary, not call the existing durable
write for every thumb movement and claim one-gesture Undo.

The existing `workspace-plugin-search-tests` group now also runs
`applicationPluginValueControls`: invalid/untrusted metadata, manual versus
effective value, real Gainer Enabled keyboard interaction, unchanged-value Redo
preservation, real Digital Filter Shape names/selection, one-step Undo/Redo,
unrelated plugin-state preservation, captured stale revision with retained
candidate, and exact state/identity after native save/reopen. Its single existing
workspace executable and private-desktop timeout remain unchanged. Existing
preset-receipt, departure-census and actual-PID numeric-draft fixtures now select
a real numeric parameter explicitly; none of their retention/history assertions
was removed or weakened.

At the next consolidated freeze, include the new group plus the existing
preset/departure/actual-PID plugin gates and shared plugin/history tests already
required by the build batch. Inspect native checkbox/combo focus, dropdown
operation, label width, contrast and 100/150/200% DPI. The metadata interpreter is
Windows presentation code; plugin validation/history remain shared host/session
responsibilities. Mac build requirements come from the other pending shared
changes, not these native controls alone. No build or test was run for this slice.

## Parameter navigation — prepared, unqualified

The selected rack parameter now has Automation and Activity buttons, searchable
commands and native Tools/Automation menu entries. The actions pass stable plugin
and parameter identities explicitly, avoiding an unrelated pattern-cursor target.
The menu's captured context now includes the rack target and draft generation, so
an open menu cannot silently act on a different rack selection. Missing targets
have a disabled reason. Navigation itself does not save, discard or apply a rack
parameter draft.

`Application::openParameterAutomation` gains an explicit-target mode for these
actions. It uses the existing strict `ParameterAutomationWindow::openSourceAt`
read path, preserves the single native owner, and leaves the editing cursor in
place. A clean unpinned owner may open the requested target. Another pinned or
dirty target is refused; opening the same target raises its retained draft without
reloading it. Existing generic open/raise behavior and source-navigation callers
retain their defaults. Native pins and raw fields are not silently overridden.

`ParameterActivityWindow::openSourceAt` adds corresponding exact observation
navigation. Its staged reads reject an unprepared processor or missing parameter
before installing a watch; there is no fallback to a different first parameter.
Recorded-point drafts and frozen traces prevent retargeting, while the same target
can be raised intact. The ordinary global Activity command still opens its chooser.
Existing live observation and recorded-point services provide the data; this work
adds no telemetry/audio provider or physical-device claim.

The parameter action row is visible at 196 DIPs; an instrument assignment row now
needs 228 DIPs. Parameter status follows the final visible row, avoiding button
and status overlap. This is another explicit minimum-size/DPI qualification item.
The prepared workspace group checks exact target navigation, one retained HWND,
independent pin refusal, raw curve-draft preservation, same-target reopening,
unchanged musical history/cursor and menu availability. Existing
`parameter-activity-window-tests` now checks strict prepared/missing targets,
draft preservation and frozen-trace preservation. Keep the existing actual-PID
activity/provenance cases in the next consolidated gate. No build or test was run
for these changes; P4a remains open for slider gestures and other listed work.
