# Windows mixer and graph integration — 2026-09-21

Full parity remains active. A fresh fetch found no commits beyond upstream
`bcfe0f8a7`; the remote default is `codex/screamseq`, with no `main` branch.
The previous pattern checkpoint is `6ffeb9645`.

## Implemented

- The app now registers the existing graph and envelope operation layers and
  the Mac-compatible mixer operation family. Graph reads expose actual rack
  identities/assignments and prepared playback activity. Clone-from-rack copies
  the saved plugin baseline and enabled auxiliary ports into an independent
  recipe. Omitted recipe state survives graph updates.
- Mixer preflight resolves real effect/instrument identities and bus catalogs,
  checks adapter capacity and compiles routes before mutation. Disconnected
  main/plugin outputs, sends, sidechains, group removal and insert transfer use
  the shared native model. Undo and native persistence remain shared.
- Control-only writes/previews publish a complete bounded batch on the UI owner.
  History allocation happens before publication; queue refusal rolls back the
  staged Undo entry, leaving document/revision/history unchanged. MSVC's map move
  construction can allocate, so the implementation does not assume otherwise.
  Structural routing changes stop playback after validation.
- The native mixer dock retains a selected bus and captured revision. It exposes
  gain, pre-gain, balance, pre-balance, width, timing, name, mute/solo, output and
  disconnect, group creation/removal, Reload and Apply. Drafts survive selection
  attempts, navigation and stale revisions. Completion checks generation before
  refreshing fields. Native controls and the command palette provide keyboard
  entry; drawing uses retained state and timer-published meters.
- Envelope banks resolve actual plugin parameters and conflicts. The separately
  revisioned JSON catalogue uses the retained `org.resonance.tracker` namespace.
  Read-only listing creates no disk file. Inspection accepts only an explicitly
  configured private catalogue; normal catalogue persistence is disabled there
  and in audio qualification processes.

## Qualification

Release ARM64 executable: `bin/windows-parity/Release/ScreamSeq.exe`.
SHA-256: `20D1F476EA9F34DC241689E46169859C5EF4881C0CF4B8178F32C79B2BF0583B`.

Seven new actual-app tests cover graph recipe baseline copying, omitted-state
preservation, history/reopen, mixer disconnection/sends, validation and preview,
real VST3 sidechains/output routes, linked graph/parameter envelope propagation,
private catalogue revisions, native draft/focus/selection workflows and control
geometry at a 900×620 scaled window. The live test runs WASAPI silently, verifies
gain/pan/preview leave playback active, then verifies topology mutation stops it.
This short functional test is not a sustained performance benchmark.

The worker `--mixer-integration <absolute existing directory>` passed:
single UI producer, dry-run and queue-refusal conservation, preview cancellation,
Undo/Redo, validation-before-stop, sends/disconnected routing and native reopen.
At 44.1/48/96 kHz, detached demo tracks render silence and unity sends restore the
connected reference within the unchanged `1e-6` PCM bound (128-frame blocks).

Evidence:

- `bin/windows-mixer-qualified-build-tests.log`
- `bin/windows-mixer-qualified-app-tests.log`
- `bin/windows-mixer-hosted-build.log`
- `bin/windows-mixer-hosted-tests.log`
- `bin/windows-mixer-worker.log`
- `bin/windows-mixer-app-rerun.log`
- `bin/windows-mixer-plugin-live.log`
- `windows/Tests/test_graph_mixer_app.py` and `MixerIntegrationTests.inc`

All 29 CTests passed (26 portable cases in 16.89 seconds, three hosted/project
cases in 0.61 seconds). The 92-case app run took 96.131 seconds: 90 passed, one
optional live-plugin test skipped, and one old test failed because it still
expected `graph.create` to be unsupported. That assertion was replaced by graph
creation/readback and stale-revision rejection. The affected API test and all
seven new integration tests then passed together (8/8, 11.071 seconds). No runtime
code changed after the full run, and the binary hash above is unchanged.
The optional live-plugin test was then explicitly enabled and passed separately
(24.137 seconds), exercising the built-in Gainer, Contourtonist and OrbitCab with
five-second playback dwells, parameter changes, editor lifecycle, saved baseline
reopen and zero asserted callback overruns. Thus all 92 discovered app cases
passed across the full run and the targeted correction/opt-in reruns. Logs retain
the original failure and skip rather than disguising them as one clean run.

Installed Contourtonist, OrbitCab and Surge XT binaries retain their previously
recorded SHA-256 values. The full app regression includes their existing lifecycle
tests. Their provenance and prior measured limitations remain in
`UPSTREAM_PLUGIN_QUALIFICATION.md`, `LIVE_PLUGIN_PARAMETERS.md` and
`TRIGGER_INSTRUMENT_PROGRESS.md`.

## Still outstanding

The graph canvas and graph-plugin editors, automation/formula and instrument
envelope/bank UI, and native insert/send/sidechain controls need implementation.
The current lower dock switches editors; independent simultaneous/floating docks
remain necessary. A private-desktop geometry/control test does not qualify
foreground appearance, accessibility or sustained 60 fps. No such claim is made.
Live structural/opaque-state replacement, the OrbitCab partition discrepancy,
Surge's first-open opaque zoom change, MIDI/recording, fresh reciprocal Mac
format-17 reopen, x64 and comprehensive long-session realtime auditing remain.
