# Explicit Windows VST3 location repair — 2026-09-21

Rack instances and graph plugin recipes can now reconnect a moved or Mac-origin
VST3 location to its matching installed Windows module. The native rack's
**Plugin location** page and graph plugin inspector open **Reconnect plugin…**;
both also have command-palette entries. This is an explicit Windows extension,
with no project-schema change and no automatic class or AU substitution.

## Identity, sound and history

The location API lists cached descriptors with the same VST3 class and
instrument/effect role. Class matching accepts saved lowercase hexadecimal IDs
while preserving the scanner's canonical uppercase cache invariant. Each
candidate carries its scan's SHA-256 token. Choosing a path requires a native
ARM64 binary, matching class/role, current content hash and exact canonical
cache identity. Changed modules require an explicit rescan and new token.

An actual changed path first loads a disposable processor with the original
saved state and enabled ports. Failure leaves the song untouched. Success
changes only the path, retaining the exact original opaque bytes, stable plugin
identity, instrument aliases, bypass, automation, ports, connections and unknown
metadata. Rack repairs use one plugin Undo step; graph recipes use one document
Undo step. A matching graph vendor-editor draft closes after a successful repair.
Same-location writes create no history.

Dry verification checks the scanned binary without decoding vendor state.
Inspection also instantiates no vendor. An explicit targeted scan uses the
existing isolated scanner and can update its cache without changing song history.
The saved path is fingerprinted once per read; UI painting uses retained data.
No continuous scan or status timer runs in the reconnect window.

## Native behavior

The window shows the saved path, matching module candidates and the selected
full path. It supports explicit bundle/module path entry, module-file Browse,
targeted Scan, Rescan installed, Verify module, Reconnect and Reload.

Stable rack or graph/node identity and document revision are captured when
loading. Close/reopen retains the target, selection and stale context. Reload
explicitly rebases. Song changes during a native file dialog reject its result
before updating the draft path or scanning. Cancellation has no song effect.

F6 switches candidate/manual-path fields, Ctrl+R reloads, Ctrl+Enter reconnects,
Enter scans from the path field and Escape closes. Native control bounds are
tested at the 700×480 logical minimum. Foreground visual quality, accessibility
and sustained presentation are not established by private-desktop checks.

## Evidence

The first six focused app tests passed with no skips in **14.265 seconds**.
They cover Contourtonist and OrbitCab repair, exact saved-state/metadata
conservation, Undo/Redo/reopen/no-op behavior, Surge XT aliases after rack moves,
graph ports/connections and history, changed DLL hashes, explicit targeted scan,
corrupt vendor state, native revision guards, focus, minimum geometry and file
dialog cancellation/staleness. Two earlier harness assertions were corrected:
the document rack summary intentionally omits paths, and F6 uses actual focus.

A separate silent WASAPI app reopened a repaired Contourtonist project. During
a 0.4-second observation frames advanced, playback stayed active, saved state and
identity matched, and there were no faults or callback overruns. This bounded
restart check is not sustained audio-capacity qualification.

An additional regression covers stale writes/scans, removed instances and
rejection of AU location repair while retaining all saved AU recipe bytes.
The final full application suite passed **150 tests**, with no failures or skips,
in **210.869 seconds**. All seven location regressions passed in that run.
Installed-effect/instrument caches, provider fixtures and live-audio checks were
enabled. The bounded Gainer, Contourtonist and OrbitCab parameter stages each
recorded zero callback overruns. The scripted graph fixture again produced
identical PCM across 44.1/48/96 kHz and 17/128/4096/8193-frame partitions, with one
second per render. This does not resolve the separate OrbitCab discrepancy.

The provider registry suite was freshly configured and rebuilt from current
registry, scanner, module and backend source: **11 CTests passed**, **3.57 seconds**.
Coverage includes cache capacity, identity, invariants, failed publication,
locking, concurrent replacement and module retargeting. Other historical CTest
reports are not relabeled as tests of this checkpoint.

Release ARM64 executable SHA-256:
`A9874BCE14044A24CFEB148C3293DFF0709BE39421B08AB17B6A50B1A831F338`.
Logs: `bin/windows-plugin-path-build.log`, `bin/windows-plugin-path-tests.log`,
`bin/windows-plugin-path-app-tests.log`, `bin/windows-plugin-path-registry.log`.
Retained evidence: `bin/windows-plugin-path-plugin-evidence/` and
`bin/windows-plugin-path-curve-evidence/`. Test-owned processes closed and the
private-desktop checks preserved foreground and clipboard state. Audio defaults
and the musician's project/preferences were unchanged.

The checkpoint package is `bin/windows-checkpoints/plugin-paths-20260921/`, with
source commit/tree, app/scanner/registry executables, dependency notices,
individual hashes, logs and bounded audio/PCM evidence. The prior library
checkpoint `ef017af02` remains preserved separately.

## Remaining work

Repair is explicitly limited to the same Windows VST3 class and role. Audio
Units remain preserved but unavailable. x64/bridging, cross-version vendor-state
compatibility and newly exported reciprocal Mac fixtures remain open. Actual
structural or opaque changes still stop before publication. The existing
OrbitCab partition discrepancy and Surge first-editor zoom-state stop remain.

Song routing overview, parameter automation and instrument envelope editors,
recording/recovery, persisted workspace, accessibility/keyboard parity and
foreground aesthetic/performance qualification remain in `PARITY_PLAN.md`.
Full parity is still active.
