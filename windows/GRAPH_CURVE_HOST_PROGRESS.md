# Independent Windows Graph Curve editor

Implementation and qualification, 2026-10-07, on `codex/graph-curve-host`, based on
independent-docking commit `fe0b9fe6aa3c33f0f37ed587b4f5ea6c60cf384f`.
Candidate 7 passes **49/49 native targets**, **38/38 focused app cases** and
**399/399 complete app cases**, with strict source/executable/isolation guards.
All **21 source-matched views** pass geometry and original-resolution review at
192 DPI. Earlier failures and their exact inputs remain historical evidence.
Checkpoint destination: `bin/windows-checkpoints/graph-curve-host-20261007/`;
its manifest is the authority for completed immutable packaging.

The implementation gives Graph automation curves one retained native owner.
Pattern and routing can remain visible while a curve is edited. The Graph editing
preset shows Pattern, routing, Graph Curve and parameter Automation. Connected
retains its Instrument/Automation arrangement and hides the curve without
discarding it. There are still only four dock surfaces, and only one Main editor
can occupy its routing/editor surface.

The owner holds curve points, raw fields, selected point, viewport, timer and
pointer state, plus Bank, Formula and Guide children. It reuses the existing
shared graph curve, formula and envelope-bank APIs, document Undo and native
project storage. This phase adds no musical format or replacement mutation API.

Captured identity includes document, graph, node and stable pattern ID. Reload
stays on that target. Automatic Follow defers to retained edits; explicit Load
selection replaces local curve data only after a successful guarded read. Child
text survives recapture and stale source tokens reject Use. Routing and curve
drafts remain independent across document revision changes.

Editor configuration version 3 adds the third native panel. Exact version-2 and
legacy configurations migrate with Graph Curve hidden. Seven-field layouts keep
the complete current editor configuration. The catalogue/storage identifiers
stay unchanged. Restore stages missing editors before adoption; an existing
Guide-only owner is shown empty and reused by its first explicit curve opening.

Reviewed source drafts and their original bytes are retained under ignored
`bin/graph-curve-*`. The integration index fingerprints selected draft versions.
The native-owner fixture, staged-restore cases and actual-HWND test migrations
are integrated. Existing musical assertions and offline rendering checks remain
required.

Candidate 1's build stopped on a restore-fixture handler-name error; its exact
5,236-input snapshot, build log and two produced executables are retained.
Candidate 2 corrects that call and preserves a focused native panel through
compact fallback after unrelated `focus:false` placement. Explicit replacement
of its region still selects the requested panel. The complete ARM64 build passes
without source drift. App SHA256:
`43057F49D89E9FF5CFF22D9A4A673AD19D53480D342A81A5FE4D8E68CB7F466C`.

The first focused native run passes the owner (2.51 seconds) and regions
(0.10 seconds) targets. Restore fails after eight successful groups at the
pumped source-selection case. All recorded source,
executable, foreground, clipboard and musician-process guards pass. The failed
run and its private child log are retained under
`bin/windows-graph-curve-native-2-focused*`. The focused app run passes 33/38 in
137.979 seconds. Failures cover two requests made during pending work, retained
Formula expansion, Guide close focus and posted local-key text. Exact Python,
compiled-source and executable inputs are preserved with the failure evidence.

Candidate 3 is diagnostic-only. Its restore trace confirms that a source-picker
notification is rejected with -32002 while a required read is pending; the model
target and generation never change. The native combo had already changed before
the notification. Candidate 4 restores that native choice before rejection and
separately exercises permitted pattern navigation invalidating staged adoption.
It also preserves the retained Formula expansion affordance, returns Guide focus
through visible owners, waits for observed read-only idle before known pending
operations, and supplies valid key-up transition flags in the posted-key fixture.
Candidate 4 builds without source drift. Restore and regions pass; the new native
Guide-only focus group fails because GetParent does not traverse an overlapped
top-level owner's GW_OWNER relationship. The app run passes 36/38 in 139.130
seconds, including all five earlier failures. The remaining app failures are a
busy graph-node creation after opening the parameter Bank and a disabled Apply
control observed during pending preview. Exact inputs and failures are retained.

Candidate 5 distinguishes native parent and owner relationships for Guide focus;
its unchanged assertions also record actual window/focus state. Its complete
native suite passes 49/49 in 84.05 seconds. The first revised app readiness helper
contained a recursion error on absent child snapshots: 29/38 cases errored. That
harness failure and exact inputs remain preserved. The corrected bounded,
read-only readiness traversal passes 12 pure edge cases, then all 38 focused app
checks pass in 170.740 seconds without retries of editing actions. App SHA256:
`5A1568059E1D2B474D8E09900FDF72D3328D15C8186D8D7FD6D04201BCD64C44`.

Two source-matched private-desktop capture runs preserve 21 scenes each at the
observed 192 DPI. Production Direct2D readback and same-process native control
composition expose missing selected-tab emphasis, automatic black Formula text,
stale child action presentation and focus accents using retained Main intent.
Native RichEdit format queries confirm automatic black text both before and
after printing, with the text selection and scroll restored. Both runs preserve
source/executable/foreground/clipboard/musician guards. Their geometry validator
flags the visible Formula completion popup overlapping its own editor; the new
validator accepts only this named, contained, state-verified overlay while still
rejecting every unexpected intersection. These are diagnostic captures, not a
final visual qualification.

Candidate 6 builds with explicit Formula text color after typography updates,
selected Curve tabs, stale captured-source labels/actions and actual native focus
presentation. All 49 native targets pass in 82.47 seconds, including exact
text/caret/scroll/Undo and retained child state. App SHA256:
`48F9C98BB03DA235827987EFD908C7DC7A8D89F563F7853A233A4744A5FFB6D2`.
The focused app run passes 37/38 in 172.551 seconds; the remaining existing
assertion shows that opening a standalone Guide incorrectly reports Graph Curve
focus while its owner is hidden. The assertion remains unchanged. All 21
candidate-6 diagnostic views have clean
geometry and original-pixel review; they do not override that functional failure.

Candidate 7 restricts logical focus presentation to visible owners. The standalone
Guide preserves its opening Pattern context while its Curve owner is hidden;
the same child reports Curve presentation focus when its owner becomes visible.
Actual input ownership and retained Main focus intent stay unchanged. The ARM64
build passes against 5,236 exact compiled inputs without drift. App SHA256:
`4AC1575B2783FEDDCD97D0E306A9440307E3652A0DF72D83A91F987A4401C20B`.
All **49 native targets pass in 82.73 seconds**, and all **38 focused app cases
pass in 171.064 seconds**, with successful source, executable and isolation
guards. The existing standalone Guide assertion passes unchanged.

Fresh candidate-7 capture evidence preserves 21 views and all source/executable/
foreground/clipboard/musician guards. All 21 PNGs have exact RGB identity to the
candidate-6 original-resolution review at the unchanged observed 192 DPI.
The separate nine-image reviewer independently verifies those pixels and native
Formula before/after formatting. All geometry checks and the combined review pass.
This transfers visual observations only; candidate-6 functional failure remains
historical failure.

Full app run 1 passes **395/399 in 1,021.623 seconds**. Three workspace assertions
still expect the pre-Graph-Curve inventory or placement dictionaries; the Rich
Edit shortcut fixture still looks for curve controls in Main. Exact failed
Python inputs and results remain preserved, with no compiled-source, Python,
executable or musician-process drift. The run is failed, not qualified.
The correction preserves exact dictionary equality, adds the new panel/preset,
and addresses the retained Curve HWND and Formula page through owned PID/class
discovery. All keyboard/music/focus/clipboard assertions stay unchanged; readiness
is bounded and read-only, without repeating edits. These **four corrected cases
pass in 5.750 seconds** with all outer guards. Application source and executable
remain candidate 7. Focused run 7 passes **38/38 in 172.670 seconds**, with all
source/executable/isolation guards, against the exact corrected Python input set.
Complete app run 2 passes **398/399 in 1,016.020 seconds**. All four migrated
fixtures pass. The remaining error is the first sample audition `transport.note`,
rejected as `Document worker busy / note not queued`; its historical trace does
not identify the active worker operation. Source, Python, executable and musician
identity guards remain unchanged, and the failed run is preserved.

Sample Detail's first-show reflow can defer a waveform read. The corrected fixture
waits for matching sample/document/revision, waveform viewport/bin/peak cache,
and idle editor/document state before issuing the original first note once.
Polling has an eight-second loop deadline; each read retains the client's
20-second timeout, so eight seconds is not a hard wall-clock cap. Errors propagate;
no musical write is retried and all original assertions remain. Twenty-five pure helper checks pass. The actual
audition case then **passes in 1.845 seconds** with strict outer success on the
unchanged candidate-7 binary. Focused run 8 passes **38/38 in 171.953 seconds**
with strict outer success and unchanged compiled source, Python inputs, executable
and musician identity. Complete run 3 **passes 399/399 in 1,017.774 seconds**, with no failures or
skips and strict outer success against this exact Python set. Source, Python,
executable and musician identity remain unchanged. This final run includes all
Graph Curve cases and the corrected workspace, shortcut and audition fixtures.
No foreground presentation, sustained performance, physical audio, other-DPI or
Mac runtime evidence is claimed here.
