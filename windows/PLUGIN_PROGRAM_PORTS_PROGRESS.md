# Windows factory programs and audio ports — 2026-09-21

The native rack now has a keyboard-accessible detail-page selector for
Parameters, Factory programs and Audio ports. Pages share the selected stable
plugin identity and fit the existing compact dock. The command palette can
focus the selector. This continues `PARITY_PLAN.md`; full Mac parity remains
unfinished.

Factory programs show the vendor's group and name. Selection alone does not
load a program. **Load program** uses the captured document revision, plugin
identity and catalog revision. An external edit makes the draft stale instead
of applying it to a changed target. Escape discards the draft; successful load
is a plugin-history transaction. The saved-baseline probe rechecks the catalog
before loading, and only opaque state is replaced. Identity, bypass, aliases,
ports and automation remain intact. An empty standard catalog is displayed
explicitly; a plugin's own browser can still be opened through **Editor**.

Audio ports show direction, index, vendor name, channel count and active state.
Main and unsupported ports cannot be toggled. Enabling/disabling an auxiliary
port replaces only that direction's enabled-port collection and preserves its
other ports, the opposite direction and other rack entries. Port activation is
separate from mixer routing. Undo/Redo and project persistence use the existing
guarded `plugin.buses.set` operation. `document.get` now exposes both enabled
auxiliary-port arrays alongside the existing rack summary.

Program dry runs were corrected to match Mac: they validate catalog and
selection without constructing/loading a disposable vendor program. Program
load responses now contain the shared `plugin`, selected `program`,
`catalogRevision`, `validated`, `loaded` and `dryRun` fields. On actual apply,
the disposable baseline instance must expose the same catalog before any vendor
selection occurs. Vendor rejection leaves document, history and transport intact.

Qualification uses the real ARM64 VST3 provider fixture through the application
pipe and native controls. Its six programs, effect sidechain and instrument
outputs exercise stale program drafts, explicit load, independent Undo/Redo,
main-port rejection, selective changes to output ports 1/2/31, preservation of
another plugin's enabled sidechain, and exact saved-state/rack reopen.
The worker test additionally counts vendor selections: a dry run causes none,
a changed catalog is rejected before selection, a failing vendor load leaves
the view/transport unchanged, and a valid load commits normally.

The full Windows Python run discovered 67 tests: **66 passed**, with the
separate hardware opt-in skipped. The final ARM64 executable SHA-256 is
`e2d9a5d927589487e447e56ccc0ecb8da0a478c473bc1320011adb7843d297e2`.

Build and test logs are `bin/windows-program-ports-final-build.log`,
`bin/windows-program-ports-app-tests.log`, `bin/windows-program-dry-worker.log`
and `bin/windows-program-ports-regressions.log`. The checkpoint manifest records
the final source identity, executable and evidence hashes. Installed-plugin
audio evidence remains bounded to the preceding trigger/live-parameter builds;
these new UI controls do not establish a new hardware benchmark.

Remaining plugin work includes live opaque-state replacement, preserving open
editors across API parameter commits, native multi-instrument/MIDI alias editing,
presets/library, missing-plugin resolution, graph/mixer connections and broader
vendor qualification. The Surge first-open zoom-state stop and OrbitCab PCM
discrepancy remain documented failures. The desktop capture limitation reported
in `TRIGGER_INSTRUMENT_PROGRESS.md` still prevents new visual qualification.
