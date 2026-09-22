# Absolute song parameter automation — 2026-09-21

Windows now exposes `automation.get` and `automation.replaceLane` with the
existing Mac contract. The command palette's Absolute song parameter lane
entry and the pattern editor's Song automation button open a modeless native
editor. It also links back to the pattern curves and selected rack parameter.

## Contract and ownership

The existing project wrapper stores absolute points; the existing shared
PluginChain renders them. This change adds dispatch and native editing, without
a storage-format or DSP change. Frames use a fixed 48 kHz clock, bounded to
seven days. Values are native parameter units with step playback, rather than
pattern-relative normalized curve values.

Get is paginated at up to 4096 points and reads the preserved collection without
copying vendor state. Replacement validates the entire lane, parameter range,
unique timestamps, global 100,000-point bound and conflicts with enabled
pattern curves/commands before changing anything. Unrelated lanes and opaque
state remain intact. Plugin reorder/removal uses the existing identity remap.
Empty replacement deletes a lane but still requires an available parameter,
matching Mac behavior. Identical replacements preserve revision, Redo and
equal-time ordering in other lanes. Commits use one plugin-history transaction
and stop before publication; reads/no-ops/rejections preserve playback.

The editor captures document, stable plugin identity, numeric slot, parameter
and revision. Pagination rechecks source revision and draft generation. Close
retains unfinished fields and points. Reload refreshes the captured destination;
From rack explicitly captures the current song/rack target. Missing targets,
external edits and document replacement cannot silently redirect a draft.

Native controls include parameter search, last touched, seconds or exact frame
entry, native values, point index/previous/next selection, insertion/deletion,
clear, Undo/Redo, time/value zoom and pan. Double-click adds, drag moves, and
Escape cancels a drag or pending fields. F6 focuses canvas/parameter list;
Ctrl+Enter applies, Ctrl+R reloads, canvas arrows edit, Tab cycles points,
Insert/Delete edit nodes, Home fits, +/- zoom, Ctrl+Left/Right pan, and
Ctrl+Z/Y use plugin history. Ordinary text-control Undo remains local.

Drawing consumes a retained cache. Dense step transitions aggregate into
horizontal display columns while preserving each column's value extrema and
last value. The cache exposes at most 2049 point handles, with full point-index
navigation and exact underlying data retained. Painting never queries plugins
or the worker. Workspace inspection truncates its point preview to 4096 and
reports the total independently; the public paginated API supplies all points.

## Qualification

Four actual-app API tests pass, including all 100,000 points across paginated
reads, global capacity rejection, sorting, duplicate/type/range validation,
stale writes and replay, no-ops/Redo, conflicts in both directions, plugin
reorder/removal, Undo/Redo and native reopen.

The ARM64 Release executable is built in `bin/windows-parity`, with SHA-256
`A811D0F31D2D98A183B19F9A928A3D51F06263D2E2504C45542B2406526B19A2`.
The final focused checks pass for last-touched/cross-view navigation, silent
live read/no-op/rejection/apply behavior, and the pattern editor's new button
at minimum window size. The complete application suite passes all 199 tests
in 465.577 seconds, with zero failures or skips. Its canonical log is
`bin/windows-absolute-automation-app-tests.log`.

Native tests cover seconds/frame entry, point/index navigation, retained/stale
fields, missing plugins, document replacement, Undo/Redo, no-op, native reopen,
mouse/keyboard editing, zoom and minimum control bounds. A 100,000-point lane
loads, retains every point, edits the last one and saves it, while draw handles
stay at or below 2049 and cached segments remain bounded by viewport width.

The actual application renders saved native-editor fixtures at 44.1/48/96 kHz
with 17/128/4096/8193-frame blocks, one second per render. Gainer's lane uses
native dB values; the external VST3 gain fixture uses its native 0–1 range.
Their active and absent-lane controls both produce zero PCM partition delta,
finite output and unchanged documents. The maximum quarter-second energy
differences are 232.29337204559405 (Gainer) and 66.9123827662551 (VST3), establishing
an audible effect rather than merely successful storage. Exact plugin opaque
state survives native project save/reopen.

The short silent WASAPI case keeps playback active across reads, no-op
replacements and invalid duplicate-timestamp writes. Native Apply of a real
change stops transport and saves its value. Hardware output is muted after DSP;
no system route or volume is modified. Private-desktop ownership checks preserve
the foreground session and desktop clipboard.

The native last-touched test failed when its read sent null params; it now
sends an empty JSON object and passes. Read dispatch for absolute automation is
kept separate from formula previews, avoiding unrelated vendor-state flushes.
Logs and fixtures are under `bin/windows-absolute-automation-*`. The preserved
checkpoint is `bin/windows-checkpoints/absolute-automation-20260921/`, with
source/executable fingerprints and file hashes in its manifest. The process
inventory after qualification contains no remaining ScreamSeq QA processes.

## Remaining scope

These are step lanes with the shared existing absolute-time playback semantics.
The 100,000-point test qualifies storage and editor drawing bounds; it is not a
claim of playing all those changes in one audio block. Render fixtures and the
short silent device test do not establish long loaded realtime performance.
Private-desktop control/bounds checks do not establish foreground aesthetics
or sustained presentation. Native Mac runtime and reciprocal project reopening
remain unverified on this Windows host. Instrument import/audition, sample
workflows, MIDI/recording/recovery, saved/docked workspace, inline completion,
accessibility, x64, live opaque-state publication and OrbitCab's partition
failure remain in the full parity plan.
