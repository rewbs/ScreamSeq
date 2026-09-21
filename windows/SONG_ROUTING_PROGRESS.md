# Native song routing — 2026-09-21

The mixer toolbar and command palette now open a modeless song routing view.
The pattern editor and other tools remain accessible. The canvas connects track,
group and master buses, row/persistent/ordinary graph stages, explicit inserts,
default master inserts, plugin instruments, sends, sidechains and graph ports.
Selecting a stage can open its existing bus, plugin or reusable-graph editor.

## Musical editing and context

Connections use the existing shared mixer/graph API and document Undo. Main
outputs, sends, graph inputs/outputs and plugin inputs/outputs retain their
different semantics. Updates replace the selected route while retaining others;
explicit plugin disconnection retains an output record with an empty target.
Unassigned effects appear on the master, matching the actual processing plan.

Native controls support endpoint/port selection, gain, pre-fader and enabled
flags where applicable, dry verification, Connect, Update and Disconnect.
Dragging an output handle to a destination uses the selected route kind and
draft settings. The insert page appends unassigned effects, removes inserts and
changes their order. The ordinary-graph page assigns independent graph copies
to buses or sample instruments with Amount and Wet controls.

Bus filtering follows output/send paths and audio dependencies without pulling
unrelated sibling tracks in solely through the master. Sample-instrument graphs
are grouped by default; selecting an instrument expands independent channel
copies. Expanded node IDs follow the Mac presentation keys. The extra collapsed
key is presentation metadata and has no effect on audio or project compatibility.

The view captures document, revision, selection and drafts. Reload explicitly
refreshes it. Close/reopen retains a draft, and stale edits cannot retarget.
Opening another editor respects that editor's existing draft. Unavailable insert
references remain visible and reject Open instead of selecting a different rack
entry. Unknown song/plugin data continues through the existing model/persistence.

## Layout and performance

Node dragging and keyboard arrows produce a retained layout draft. Arrange
stages nodes by connections; Fit, pan and zoom change the local viewport.
Save layout uses `graph.layout.set`, shared Undo and native persistence. Layout
changes keep prepared audio running; structural route changes retain the current
validation-before-stop behavior.

Geometry, curves and hit-test bounds are cached outside painting. Rendering
culls offscreen nodes/wires and performs no plugin or document-worker queries.
The window has no continuous polling timer. Scene refresh is explicit.
Routing inventory reads and graph capacity checks now validate metadata and
opaque-data bounds without copying every saved vendor state. Playback continues
to decode the complete state by default. A 16 MiB regression checks omission,
identity conservation, source-data retention and malformed-state rejection.

F6 switches canvas/node picker; Ctrl+Enter applies, Home fits, plus/minus zoom,
Delete disconnects an editable wire, and Escape reloads a draft or closes a clean
window. Native controls are checked at the 1040×680 logical minimum. Separate
modeless tool windows do not complete the remaining dock/pin/workspace work.

## Evidence

Nine focused native app tests passed in **43.197 seconds** before the final
inventory optimization and unavailable-insert regression. They covered:

- Stage ordering, implicit/explicit inserts and dependency filtering.
- Send updates, dry runs, invalid routes, stale drafts and Close/reopen.
- Insert order, node dragging, keyboard focus, minimum control bounds and
  layout Undo/Redo/save/reopen.
- Sample-instrument and bus graph assignments, expanded copies and navigation.
- Graph sidechain/auxiliary ports, selected-wire preservation and socket dragging.
- Real provider sidechains and auxiliary instrument outputs/disconnection.
- Installed Surge XT output routing with exact sound and alias conservation.
- Owned silent WASAPI playback during layout saving, then deliberate routing stop.

Earlier harness failures used a nonexistent rack-summary field, requested an
auxiliary output from the effect fixture (its auxiliary outputs belong to the
instrument fixture), and treated a request-only `disconnected` flag as a read
field. They were corrected against the actual contracts. The first silent run
also exposed the strict audio-test process-exit rule after intentional Stop.
`--audio-test-allow-stop` now explicitly enables automated stop workflows while
leaving default audio qualification strict.

The final full application run exercised **160 tests** in **254.712 seconds**:
159 passed, none skipped, and one older library fixture failed while rewriting
its private preference file concurrently with a guarded app read. All ten
routing tests, including the unavailable-insert regression, passed. Fixture
injection now acquires the same bounded per-path mutex as real preference
writers. The complete seven-test library suite then passed in **15.165 seconds**.
No application code changed after the full run. All 160 discovered app cases
therefore passed across the full run and focused correction; the original error
remains in the log rather than being represented as one clean full run.

The rebuilt hosted-project and live-parameter targets passed **2 CTests** in
**0.43 seconds**, including the 16 MiB inventory regression. Their bounded C++
allocation checks do not establish a comprehensive vendor/Win32 realtime audit.

Release ARM64 executable SHA-256:
`423403C50A8D907CD1CD72F7C660CE07BEC6AF11CA82EE15A3D899BBBFC70BDC`.
During native layout editing the silent WASAPI frame count advanced from 27360
to 62400, with playback active and no faults or overruns. Native disconnection
then stopped playback. Bounded Gainer, Contourtonist and OrbitCab parameter stages
also recorded zero overruns. The scripted graph fixture retained zero PCM delta
across 44.1/48/96 kHz and 17/128/4096/8193-frame blocks, one second per render.
This does not resolve the separate OrbitCab partition discrepancy.

Logs are `bin/windows-song-routing-build.log`, `bin/windows-song-routing-tests.log`,
`bin/windows-song-routing-app-tests.log`, `bin/windows-song-routing-hosted-tests.log`
and `bin/windows-song-routing-library-tests.log`. Geometry/scene and silent-audio
evidence is in `bin/windows-song-routing-evidence/`; installed-plugin and curve
evidence is in `bin/windows-song-routing-plugin-evidence/` and
`bin/windows-song-routing-curve-evidence/`. Test-owned processes closed and the
private-desktop checks preserved foreground and clipboard state. System audio
defaults and the musician's project/preferences were unchanged.

The package is `bin/windows-checkpoints/song-routing-20260921/`, with source
commit/tree, individual file hashes, app/scanner/hosted-test executables, notices,
logs and evidence. The preceding location-repair checkpoint `9cb810708` remains
preserved. A fresh fetch still found upstream at `bcfe0f8a7`, with no new commits.
Private-desktop geometry and scene snapshots do not prove foreground visual
quality or sustained presentation.

## Remaining work

Graph command-lane controls, pattern-parameter automation, native instrument
envelopes/voice markers, inline formula completion, recording/recovery, sample
editing refinements, persisted workspace, accessibility and configurable
shortcuts remain. Live opaque/structural replacement, the OrbitCab partition
discrepancy and Surge's first-editor zoom-state stop remain unresolved.

Foreground aesthetic/60 Hz comparison, x64/bridging, reciprocal current Mac
project reopening and sustained loaded realtime/allocation/lock qualification
remain release gates. Full parity remains active in `PARITY_PLAN.md`.
