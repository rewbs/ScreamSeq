# Independent Windows dock regions

Implemented, 2026-10-07, in the isolated `independent-dock-regions` worktree,
on the integrated matrix checkpoint. Pattern, Graph, Instrument and Automation
can now occupy independent regions together. Candidate 8 passes all 48 native
targets, 33 focused actual-app cases and 37 source-matched visual reviews.
Full run 3 finishes 392/393 with one recording read-poll error. Its narrow fixture
correction now passes all 7 recording-module cases, with strict isolation.
Final focused run 7 passes 33/33 in 106.856 seconds with strict outer isolation.
Full run 4 passes 393/393 in 971.215 seconds, without failures or skips, with
strict outer isolation. The qualified application SHA256 is
`6344453CC9222ECB0F3D04FC8065FDD4FB93480FC3D6209627894478670C0074`.
Earlier failed runs and their exact evidence remain below. Checkpoint destination:
`bin/windows-checkpoints/independent-dock-regions-20261007/`; its manifest is the
authority for completed immutable packaging.

## Prepared and tested foundations

`WorkspaceRegions.hpp` supplies pure placement/preferences and bounded geometry
for right, bottom and secondary hosts, with a compact tab fallback. Version 2
editor preferences migrate the exact legacy three-field representation; a
missing editor member preserves current native placements. The catalogue's
version-1 envelope remains unchanged. This model does not store musical targets,
pins, origins or drafts, and does not resize the owner.

Instrument and parameter automation retain their HWNDs and expose useful docked
forms at 440×300 DIPs. Floating minima remain 440×500. Detail pages preserve raw
fields, selection and caret while keeping Apply reachable. First entry to the
short layout reveals a focused field's page. Fresh hidden initialization permits
layout preparation without showing or registering the editor. A narrow native
visibility hook resumes pending automation preview on show without a reload.

The four primitive CTest targets pass **4/4 in 5.11 seconds**:
`workspace-regions-tests`, `parameter-automation-dock-tests`,
`instrument-short-dock-window-tests`, and `native-tool-window-tests`.
The corrected build is warning-free. Evidence:

- `bin/windows-dock-regions-configure-2.log` and `-build-2.log`.
- `bin/windows-dock-regions-native-2.log` and `-native-2-details.log`.
- `bin/windows-dock-regions-native-2-isolation.json` and `-native-2-sha256.json`.
- Source snapshot path: `bin/windows-dock-regions-source-2-path.txt`.

The native cases exercise bounds, every retained page/action, text/caret/axes,
wide-to-short focus, curve mouse mapping, guarded Apply, hidden initialization,
and base-pointer show followed by ordinary preview timer delivery. The region
fixture covers 216 legacy migrations, strict V2 validation, all placement pairs,
fallback thresholds and immutable desired geometry. Private desktop teardown,
foreground, clipboard, musician PID 14312, source and executable guards pass.
This establishes bounded native interaction and geometry, not visual review or
actual integrated application workflows.

## Layout preparation integration

First-open Note, FX, Graph and Mixer caches now prepare local values. Restore
checks all candidates and a captured workspace guard before adopting any of them.
Existing caches and unfinished drafts are retained. Worker reads use a narrow
read-only boundary that does not refresh presentation; fresh hidden native
editors use that boundary only until adoption. A shared token belongs solely to
the staged objects, so existing live editors retain their usual callbacks.

The guard rejects changed document/revision, cursor, focus, layout, placement,
pins, origins, target selection or draft generations after a pumped wait. Failed
preparation discards local candidates without rolling back newer user actions.
This does not claim rollback of all possible Win32 placement/allocation failures
after adoption. A permanent Application-level failure/input fixture now covers
seven restoration groups; see candidate 5 below for its passing result.

The integrated application **compiles**, with no diagnostics in
`bin/windows-dock-regions-app-build-1.log`. SHA256:
`2827CAEA0E681D3EDE91687780284F88121BD5398315BE28D7307FB5E2E5BF8E`.
`-app-build-1-sources.json` and `-app-build-1-identity.json` record unchanged App
sources through compilation. This executable has not been launched or qualified.

## Earlier outcomes retained

Native run 1 passed Instrument and retained-window cases, but failed Regions and
Parameter. The region fixture prohibited the intended 6×6 intersection of two
resize hit areas. It now exempts only divider-versus-divider overlap and requires
that exact junction, preserving all body/header overlap and bounds assertions.
Production geometry did not change for this correction.

The Parameter fixture stopped before creating UI because it required optional
common-control registration for intrinsic User32 controls. It now follows the
existing retained-window fixture and verifies each actual HWND. The cause of the
registration failure remains unattributed; timer and interaction gates were not
removed. An Instrument tuple's integer width literals were made explicit floats
to eliminate its narrowing warning, with unchanged values. The first build's
existing shared MixerRuntime warning is retained in that log. Run 1 logs, exact
binaries and source remain separate from run 2.

## Region integration, candidate 3

The application now connects the region solver, V2 preference migration, local
headers, shared Main editor body, independent native placements and compact
fallback. The Connected editors preset is available in the command palette,
layout manager and workspace API. Docking no longer enlarges the owner window.
Drawing, focus, input and musical-key release follow actual visible surfaces.

Candidate 3 builds the application and five native test targets without compiler
diagnostics. App SHA256:
`8264C245AD66E08DB64C48328B419625D0A11040B0DC841970ACAE8073C17019`.
`bin/windows-dock-regions-build-3.log` and the 5,228-file source-3 snapshot retain
the exact inputs. Earlier source and binaries remain separate.

Native run 3 passed four of five CTest targets. All seven functional groups in
the new restoration fixture passed, but its final private-desktop CloseDesktop
guard failed with Win32 170. It is a failed run. Owned HWND checks, outer
foreground/clipboard/musician checks and source/executable guards passed; the
remaining desktop resource holder is not identified. A fixture process boundary
is being added so the observer closes its desktop only after its owned process
exits, retaining all teardown gates.

Focused application run 1 ran 29 cases. The nine existing docking cases passed,
as did simultaneous four-region visibility, all short native pages and splitter
cancel/focus cycling. Two wheel cases used screen coordinate zero; one new case
tried a page-hidden Ramp button and another assumed a nonexistent Sample snapshot
field. These fixture assumptions are being corrected without removing behavior
or native bounds assertions. The run also failed its foreground guard: observed
HWND 591264/PID 21216 became null; neither was the owned runner. That change is
unattributed. Musician PID 14312 and source/executable hashes remained unchanged.
This is not a passing qualification run.

Source review additionally found a real compact-tab API issue: legacy Notes and
Samples focus/return requests did not reveal the pattern host. The candidate 4
source correction and regression are pending qualification.

## Candidate 4 verification

Candidate 4 compiles without diagnostics. All **5/5 native targets** pass,
including the seven Application restoration groups and the new owned-process
lifecycle boundary. The child exits before desktop closure; all owned-window,
desktop, foreground, clipboard, musician, executable and source guards pass.
See `bin/windows-dock-regions-native-4*.log` and `-isolation.json`.

The corrected actual-app run passes **30/30 cases in 63.104 seconds**, with strict
outer isolation and unchanged source/executable/musician guards. It includes
simultaneous native hosts, all short pages, minimum Main editor pages, dirty
HWND/raw field/caret preservation, five placements, saved restores, splitter
rollback, focus cycling and legacy inspector focus/Return. See
`bin/windows-dock-regions-focused-2-app-tests.log`, `-isolation.log` and
`-result.json`. App SHA256:
`556F3E0A8A8562F42F3414727D5571EE3F8F2624C5A04691926A3C377763BC5B`.
The source-4 snapshot contains 5,229 compiled inputs. These are functional and
native-bounds results; source-matched visual review is still outstanding.

A subsequent source review found that placement without focus could select the
Tracker compact tab while a Main canvas was focused. Candidate 5 preserves the
actually focused Main body when it remains available, while retaining explicit
native bottom/focus selection. This change is not covered by candidate 4 results.

## Candidate 5 verification

Candidate 5 preserves the focused Main canvas or native field during a placement
request without focus, including compact fallback caused by width or height.
Explicit native bottom placement and focus still select their requested host.
The regression checks Graph raw Unicode text, caret, logical/native focus,
toolbar exclusions and explicit native focus.

The complete default build succeeds. The full build log retains one existing
conversion warning in unchanged `mac/Tests/MixerRuntimeTests.cpp:189`; the new
application and restoration code compile without diagnostics. App SHA256:
`B49BF333EB6E3FDA93F1AC7DFD406824CDDDE3C9F74FAF9F8AD7F387F224E7BE`.
The source-5 snapshot records 5,229 compiled inputs.

All **48/48 native CTest targets pass in 67.38 seconds**, including the seven
restoration groups. Strict process/desktop, foreground, clipboard, musician,
source and executable checks pass. Evidence:
`bin/windows-dock-regions-native-5-full.log`, `-details.log`, `-isolation.json`
and `-sha256.json`, with the exact archived native executables.

The focused actual-app run passes **31/31 cases in 74.160 seconds**. All source,
Python fixture, executable and outer isolation checks pass. See
`bin/windows-dock-regions-focused-3-app-tests.log`, `-isolation.log`,
`-result.json`, `-test-sources.json` and the archived application executable.

## Candidate 5 rendering review

All **37 source-matched views at 192 DPI** pass bounds, intersection, retained
state and original-resolution visual review. They cover the current-work-area
Connected four-pane workspace, six Graph pages, four short Automation pages,
five short Instrument pages plus envelope options, all six Main editors at the
900×620-DIP outer minimum, remaining compact Plugin/Graph detail pages, the
900×620-DIP client variant, and invalid raw drafts across resize and selection.
The independent reviews found no blocking visual issue. Bounded status ellipsis
and retained graph viewport clipping after resize are intentional; Fit remains
available and no view state is reset to manufacture the capture.

Evidence is `bin/ui-capture/evidence-regions-candidate5-run1/`, including
`capture-evidence.json`, `visual-review-record.json`, all BMP/PNG originals,
source/link fingerprints and scratch reproduction inputs. The sibling
`evidence-regions-candidate5-run1-runner/` records strict outer success with
unchanged foreground, clipboard, musician PID, production executable and helpers.
The initial scratch-only compile failure and exact failing helper sources remain
in `bin/ui-capture/regions-build-failed-1/`; the corrected scratch compile has no
diagnostics. Production source was unchanged.

These images use the production Direct2D renderer plus retained native-control
composition in a source-matched instrumented process on a private desktop.
They verify those renderings, not foreground presentation or physical-display
frame timing. No audio device was opened by this capture process.

## Regression findings and corrected qualification

Full candidate-5 run 1 completed **387/391 passing in 940.477 seconds**, with four
failures and no skips. `bin/windows-dock-regions-full-1-app-tests.log` retains
all traces; `-result.json` records exit 1 with unchanged compiled source, Python
fixtures, executable and musician process. The isolated runner reports no
separate foreground/clipboard guard failure. This is a failed qualification.

The graph keyboard failure and bank test's pre-Unlink `dirty` failure follow
the missing `graphCurve` focus mapping: mouse release or preview layout can
redirect focus to Pattern or Graph. The sample loop Ctrl+Enter and Arrangement
sequence failures have a distinct cause: Main layout assigns focus while a
pending native tool has intentionally cleared it, preventing the tool's guarded
field restoration. The two latter cases reproduce alone, **2/2 failing in
3.435 seconds**, in `bin/windows-dock-regions-tool-focus-repro-1-*`.

Two new actual-app cases also reproduce the source-review findings on the same
unchanged candidate-5 executable. The curve case loses logical focus on mouse
release. The Notes/Samples case shows inspector keys changing a separate Main
draft/selection and F6 from an Effects field selecting the wrong region.
`bin/windows-dock-regions-focus6-repro-1-*` records **2 cases / 6 failures in
8.715 seconds**, with clean source/test/executable/musician fingerprints. These
are regression demonstrations, not passing qualification.

Candidate 6 distinguishes canvas intent from region placement, preserves
temporary null focus in unrelated pending tools, and prohibits hidden routing
canvas key handling on the curve page. Its targeted run passes **5/6 cases in
41.331 seconds**, including all four failures from the full run and the new
Notes/Samples ownership case. The expanded Graph case exposes a separate resize
failure: regions-to-tabs fallback can hide the focused Graph field when the
stored compact preference still names Pattern. This failed run and exact binary
remain in `bin/windows-dock-regions-focus6-regression-1-*`.

Candidate 7 remembers actual visible Main control membership after layout and
carries the focused host into compact tabs only during window resizing. It does
not move keyboard focus or replace drafts, and ordinary named-layout restoration
keeps its explicit configuration. Its full default build succeeds without
diagnostics or drift across 5,229 compiled inputs. The first targeted run passes
the six repaired/new cases; its seventh case fails an old assertion that shrinking
away from focused Graph automatically selects Automation. That test now requires
Graph focus to remain, then explicitly selects Automation. The original failed
expectation remains in `bin/windows-dock-regions-focus7-regression-1-*` (**6/7 in
45.030 seconds**). Both native editor fields and a chooser outside Main also have
immediate post-resize assertions. Native editor-local F6 remains local; the region
command and F6 originating in Main are separate tested behaviors.

Candidate 7 then passes **33/33 focused actual-app cases in 106.351 seconds**,
with strict outer success. Its native run passes **47/48 in 61.13 seconds**;
the restore fixture explicitly focuses visible Pattern before shrinking but
still expects Graph to replace it. All preceding atomic-restore groups pass.
The fixture is being updated to require focused Pattern retention, explicit
Automation selection and authoritative saved Graph restoration. Original native
output, child log, exact binaries and unchanged source/isolation fingerprints
remain in `bin/windows-dock-regions-native-7-full*`. This native run is failed
qualification, even though the failure is an outdated expectation.

Candidate 8 changes only that native fixture from candidate 7. Its full default
build succeeds without diagnostics; the application remains byte-identical at
`6344453CC9222ECB0F3D04FC8065FDD4FB93480FC3D6209627894478670C0074`.
All **48/48 native tests pass in 63.53 seconds**, including both explicit
selection and saved-layout authority. Foreground, clipboard, musician process,
executables and all 5,229 compiled inputs remain unchanged during the run.
Evidence is `bin/windows-dock-regions-build-8*`,
`bin/windows-dock-regions-native-8-full*` and the source snapshot named by
`bin/windows-dock-regions-source-8-path.txt`.

Candidate 8's focused run 5 passes **33/33 in 106.665 seconds**, with strict
outer success and no source, fixture, executable or musician-session drift.
`bin/windows-dock-regions-focused-5-*` records exact source and application identity.

Its **37 final views at 192 DPI** pass bounds, intersection, retained-state and
original-resolution review, including all short native pages, minimum-size Main
pages, Graph/Plugin details and retained invalid fields. Evidence is
`bin/ui-capture/evidence-regions-candidate8-run1/` and its sibling `-runner/`.
The capture matches 230 production source files and 21 linked inputs; all eight
behavioral assertions and isolation guards pass. The scratch executable is
separate from the production application. The review record identifies the full
27-view Main and 10-view native reviews plus five independently rechecked views.
These are source-matched renderer/native-control compositions on a private
desktop, not physical-display presentation evidence.

Full run 2 finishes **390/393 in 982.042 seconds**, with two failures, one error
and no skips. Compiled source, all Python fixtures, executable and musician
process remain unchanged. The sample-audition failure expects the inspector's
Tab to enter a separate Main field (line 411); the context-menu focus subtest
reads an invalid HMENU after its focus request (WinError 1401); the recovery
vendor case times out before observing the protective-write phase (line 91).
The original three-case rerun reproduces the first two and passes recovery:
**1/3 in 6.298 seconds**. No production cause is inferred for the recovery
timeout. Exact binaries, output and guards are in
`bin/windows-dock-regions-full-2-*` and `bin/windows-dock-regions-full2-repro-1-*`;
all 61 original Python inputs are archived in
`bin/windows-dock-regions-full-2-python-sources/`. These are failed qualification
runs and remain preserved while the focused diagnostics are completed.

The unchanged menu case was instrumented once in an ignored observer under the
same strict private runner. Before focus changed, the captured native popup and
menu were valid. Immediately after `workspace.panel(notes, focus:true)`, both
were destroyed, no owned popup remained, and the same context-menu serial was
inactive with selected command 0, no dispatch and no rejection. Document and
musical-context hashes were identical before and after. The original test then
dereferenced the destroyed menu and retained its original WinError 1401. Evidence:
`bin/windows-dock-regions-menu-focus-probe-1-*`. This proves native cancellation
for that run; the fixture must distinguish it from selecting a stale action.
The observer did not reopen, retry or suppress the original error.

The recovery diagnostic passes **2/2 in 6.338 seconds**, with strict isolation
and unchanged source, diagnostic inputs, application and musician process. One
case posts the real autosave timer and observes saving before and after one
Restore command while its native button is disabled. The command is ignored;
autosave completes without restoring, changing the document or changing either
vendor editor's enable ownership. This demonstrates the possible unavailable
interval, not the cause of the historical timeout. The other case uses the
proposed readiness/explicit-copy selection and retains every original protective
failure assertion. Evidence: `bin/windows-dock-regions-recovery-vendor-probe-1-*`.

The three reviewed fixture corrections pass together **3/3 in 6.666 seconds**
with strict isolation (`bin/windows-dock-regions-full2-corrected-1-*`). Inspector
Tab now explicitly stays out of Main; the test selects Main Samples before its
native-field phase and verifies the held musical key survives that transition.
The menu case checks cancellation without dereferencing a dead handle, retaining
the live-menu stale-action rejection path. Recovery waits for loaded/native/API
readiness, selects the original stable copy ID through the native list and sends
one Restore; all original protection and vendor-enable assertions remain. No
production source or application bytes changed for these fixture corrections.

Focused run 6 passes **33/33 in 106.720 seconds** with the three corrected
full-run-2 fixtures, with strict outer success and no source, fixture, executable
or musician-session drift. Evidence is `bin/windows-dock-regions-focused-6-*`.
Full run 3 finishes **392/393 in 976.740 seconds**, with one error and no skips.
Its corrected audition, menu and recovery cases pass, but
`test_timestamped_chord_api_stop_dry_run_one_undo_reopen_and_native_review`
receives API error -32002 while polling `recording.get` for its first two events
(line 169). A worker operation is busy at that read boundary; the test's generic
wait does not handle that existing worker-busy response. MIDI capture is one
source-confirmed path to the guard, but the historical trace does not identify
which in-flight operation held it. The original
single case passes on its isolated rerun, **1/1 in 1.301 seconds**. No mutation
was retried. Full output and guards are in
`bin/windows-dock-regions-full-3-*`; its 61 exact Python inputs are preserved in
`bin/windows-dock-regions-full-3-python-sources/`. The original case rerun is
`bin/windows-dock-regions-full3-recording-repro-1-*`. Source, fixtures, executable
and musician process are unchanged for both runs. Full run 3 remains failed
qualification.

The reviewed fixture correction changes only that method's three convergence
polls: exact event counts 2 and 4, then an empty take after one native Finish
command. A dedicated `recording.get` helper uses an eight-second total deadline,
a fresh read client limited to the lesser of 0.5 seconds and the remaining time,
and at most 16 diagnostic observations. Only an API -32002 from the read is
retried. Other API errors, transport failures and predicate errors propagate.
The generic wait/read/take helpers remain unchanged; no MIDI injection, native
command or mutation is retried. Exact counts, loss checks, revision and musical
state, one Undo, Redo, native review and save/reopen assertions are retained.
Production source and application bytes did not change.

The six deterministic fake-clock/client checks pass **6/6 in 0.001 seconds**
(`bin/windows-dock-regions-recording-poll-contract-2.log`). They cover busy-only
read retries, the total deadline, exact-count convergence and propagation of
nonbusy, transport and predicate errors. The retained `-contract-1.log` records
an import failure caused by missing `PYTHONPATH` before any check ran; it is not a
passing run or an application failure.

The corrected actual-app recording module passes **7/7 in 7.163 seconds**, with
no failures or skips, strict outer success and unchanged compiled sources,
Python inputs, executable and musician process throughout the run. Evidence is
`bin/windows-dock-regions-recording-corrected-1-app-tests.log`, `-isolation.log`,
`-result.json`, `-test-sources.json` and the archived executable. It uses source 8
and the same application SHA256
`6344453CC9222ECB0F3D04FC8065FDD4FB93480FC3D6209627894478670C0074`.
Final focused run 7 passes **33/33 in 106.856 seconds** and full run 4 passes
**393/393 in 971.215 seconds**, with no failures or skips. Both use the corrected
Python inventory, source snapshot 8 and the same candidate-8 executable. Strict
outer checks pass: compiled source and Python inputs do not drift, executable
hashes match, and the musician process is unchanged. Evidence is
`bin/windows-dock-regions-focused-7-*` and `bin/windows-dock-regions-full-4-*`;
all 61 exact full-run Python inputs are preserved in
`bin/windows-dock-regions-full-4-python-sources/`.

Combined with candidate 8's **48/48 native tests in 63.53 seconds** and the
**37 reviewed source-matched views**, these runs qualify this bounded docking
checkpoint. Functional checks cover desired split sizes and retained editing
state across fallback, local selection, placement and saved-layout restoration.
The immutable package must retain the earlier failed runs and exact evidence;
no historical result is upgraded to a pass.

The display was re-queried at 00:12:03 UTC on 2026-10-08 (17:12:03 local on
2026-10-07): one 2880×1800 display, 192 DPI, with a 2880×1704-pixel
(1440×852-DIP) work area. Graph's
pattern-curve page still replaces the routing canvas; a separate graph-curve host
is later work. Full parity, foreground/multiple-scale presentation, accessibility,
Mac runtime and device/long-session qualification remain open.
