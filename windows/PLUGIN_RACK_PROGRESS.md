# Windows native rack continuation — 2026-09-21

Full Mac parity remains the active goal. This checkpoint connects the plugin
provider to the application; it does not complete the parity plan.

## Implemented

`Session/PluginOperations` owns baseline/editor instances on the document worker.
VST3 controller and HWND calls retain the provider's private STA ownership.
Playback preparation uses distinct instances so automation cannot be serialized
into the song's saved baseline. Rack writes validate before changing history,
revision or transport. Plugin Undo/Redo is independent from document Undo/Redo,
bounded by entry count and estimated state bytes, and cleared on project open.

The native lower dock now provides the rack, plugin discovery/rescan, add,
remove, bypass, reorder, editor windows, parameter selection/value entry,
instrument assignment and plugin Undo/Redo. Controls preserve plugin identity,
list scroll, selection and unfinished parameter drafts. Enter applies a value;
Escape discards a draft. A stale draft cannot write to a changed song/target.
The sample editor remains available through Sound design / Sample & loops.

Shared plugin API operations cover parameters, base64 state, programs, buses
and instrument aliases. Explicit Windows editor operations have a schema in
`Api/windows-extensions.schema.json`. Discovery reports failed module paths and
keeps successful scans. All loading still checks exact class/path/ARM64/hash.

Only open editor instances are polled. State changes are debounced into plugin
history and forced before save/close/history operations. Editor-generated edits
update the last-touched automation target. Read responses include the resulting
revision when a pending gesture is captured. Background inspector reads defer
pending API dispatch instead of producing random busy errors while idle.

Project dirty/saved tracking includes plugin revisions. Unknown plugin fields
and opaque states survive rack history and save/reopen. Removing a rack entry
retains unresolved native automation/routing/binding targets, matching Mac;
absolute slot automation is removed/remapped. Empty document Undo remains a no-op.

## Qualification and findings

The ARM64 application and document controller build successfully. The complete
Windows Python application/qualification run passes **63 tests**, including
the seven rack integration cases with the two installed effects enabled.
Native project retention/edit/save/reopen tests also pass. Logs are
`bin/windows-parity-app-tests.log`, `bin/windows-parity-native-tests.log` and
`bin/windows-parity-build.log`; the packaged checkpoint carries copies and hashes.

The application tests exercise built-ins, atomic invalid parameter batches,
dry runs/no-ops, stale revisions, independent histories, identity through moves,
base64 rejection, native control editing, stale drafts, aliases, unavailable AU
state and native reference preservation. Installed Contourtonist 0.2.2 and
OrbitCab 2.5.0 exercise parameter edits, state restore, save/reopen, native editor
open/close, removal with an editor open and Undo restoring the same instance ID.

Real visible UI inspection added Gainer and OrbitCab through the rack and
opened OrbitCab's custom editor. A high-pass filter click produced parameter
936500484 on the correct rack identity, source `editor`, and a new saved plugin
revision. The disposable project and screenshots are under
`bin/windows-plugin-qualification/app-rack-ui/`; that directory records the
exact executable used. A toolbar text overlap observed there was corrected.

OrbitCab deliberately changes `eqOn` to follow preamp power when its editor
opens. This is confirmed in the publisher's
[v2.5.0 editor source](https://github.com/darwinscat/orbitcab/blob/v2.5.0/src/PluginEditor.cpp#L1431).
A dedicated application regression verifies that this vendor-generated change
is captured and undoable. Generic lifecycle edits use a continuous parameter;
this does not conceal the separate EQ behavior. Source and before/after state
evidence are retained under the plugin qualification directory.

The existing native-project tests use the explicitly synthetic current-format
wrapper described in `UPSTREAM_PLUGIN_QUALIFICATION.md`. Their historical test
names do not establish a fresh Mac export or reciprocal Mac build/reopen.

## Remaining work

- Parameter/state commits currently stop playback. Add safe live parameter
  propagation and gesture history without baking automation or allowing a
  partial batch to reach the callback. Opaque preset/IR changes need prepared
  replacement behavior. Long playback and allocation/free/lock audits remain.
- Native program/bus/alias controls, plugin presets, favorites/categories,
  missing-plugin resolution, a real instrument and its trigger workflow.
- OrbitCab's previously measured offline partition difference remains open;
  the native editor/lifecycle results do not waive the audio comparison gate.
- Full pattern-performance/precise-note, graph/mixer/automation/envelope,
  recording, MIDI, persisted/floating workspaces and the final parity audit
  remain as listed in `PARITY_PLAN.md`.
