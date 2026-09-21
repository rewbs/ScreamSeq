# Windows plugin sound presets — 2026-09-21

The rack's **Presets** page and command palette now offer native Save/Load
dialogs. New files use `.screamseq-preset`; legacy `.resonance-preset` files are
accepted case-insensitively. The implementation follows the current Mac
`PluginPreset` and session API contracts. Reciprocal execution on Mac remains a
separate qualification gate.

## File and transaction behavior

Presets contain a name, plugin descriptor and opaque sound state. Binary saves
retain the compatibility magic `Resonance plugin preset`, version 1. Reads accept
both branded magic strings and binary/XML property lists, including Unicode
text and UTF-8/16/32 input. State is limited to 16 MiB and the total file to that
limit plus 65536 bytes. Dictionary fields, class identities, string sizes,
Unicode, XML entities, structure and data are validated before use. Custom DTD
entities are rejected; no external DTD is fetched. XML uses the repository's
pugixml with an additional strict character/entity validation layer. Its MIT
notice is included under `THIRD-PARTY-NOTICES/pugixml-LICENSE.md` beside the app.

`plugin.preset.inspect` returns metadata and a SHA-256 token of the exact file
bytes. `plugin.preset.save` requires the document revision and saves the current
baseline atomically through a flushed staging file. It refuses accidental
overwrite; failed publication retains the original and removes staging data.
Saving does not create song history. Dry save validates/encodes without writing.

`plugin.preset.load` requires the document revision, stable plugin ID and
`expectedPresetRevision`. VST3 identity is its case-insensitive class ID, not its
installation path or display name. A dry load checks the file and identity
without invoking a vendor decoder. Actual loading constructs a disposable
processor and obtains canonical sound state before publishing. Only the state
field changes, preserving bypass, instance ID, aliases, ports, automation,
routing and unknown plugin metadata. A change creates one plugin Undo step;
identical canonical state creates none. Loading does not publish a last-touched
automation target. Invalid/stale requests leave the document/history unchanged.

Native dialogs capture the document, revision and plugin before opening, then
recheck after selection and inspection. Changing the song while selecting a file
rejects the result. Cancel is a no-op. Loads use the existing stop-before-
publication path; this checkpoint does not add live opaque-state publication.
Parsing, hashing, serialization and vendor construction occur on the document
worker, outside the audio callback and painting.

## Qualification

Eight focused actual-app tests cover binary/XML interoperability using Python
plist fixtures, exact content hashes, Unicode/encoding/entity rejection, 16 MiB
boundaries, atomic overwrite failures and cleanup, dry runs, stale file/document
guards, plugin mismatch, stable identity after rack movement, no-op history,
native Save/Load/Cancel dialogs and exact Undo/Redo/save/reopen behavior.

Installed ARM64 Contourtonist, OrbitCab and Surge XT sounds were saved, changed,
loaded, undone/redone, reopened and removed/restored. Preset descriptors were
rewritten with a Mac installation path and lowercase class ID to verify portable
identity. Surge's two instrument aliases on channels 1 and 9 were retained. The
provider fixture separately verifies an enabled auxiliary input and existing
absolute automation. Saved plugin records and native song metadata are compared
exactly, including an unknown future plugin field.

Focused run: **8 passed**, no skips, **13.413 seconds**. The complete suite also
includes subsequent explicit failed-publication and stale native-load checks.
Build log: `bin/windows-preset-build.log`.
Focused log: `bin/windows-preset-tests.log`.
Full application log: `bin/windows-preset-app-tests.log`.
Executable SHA-256:
`CC5CBCE3CEE4A3D441052AF11E73B9F2444199D43B2DB03EAF6FC4D1D617D8FB`.

The complete application suite passed **136 tests**, no failures or skips, in
**189.013 seconds** against this executable, with installed-plugin caches,
provider fixtures and live parameter checks enabled. Gainer, Contourtonist and
OrbitCab's bounded parameter stages recorded zero callback overruns. These are
short fixtures, not sustained capacity qualification. The scripted graph fixture
again produced zero PCM partition delta at 44.1/48/96 kHz with
17/128/4096/8193-frame blocks and one second per render.

Evidence: `bin/windows-preset-plugin-evidence/` and
`bin/windows-preset-curve-evidence/`. Owned test processes closed; foreground and
clipboard preservation checks passed, without changing system audio defaults.
Foreground visual/60 Hz qualification remains unavailable, not implicitly
established by these native-control tests.

The preserved package is `bin/windows-checkpoints/plugin-presets-20260921/`.
Its manifest records source commit/tree, evidence and individual artifact hashes,
including the VST3 and pugixml notices.
The previous alias checkpoint `03896d68b` remains preserved separately. Shared
DSP is unchanged from `d619ae439`; earlier CTests are historical evidence, not
new runs against this checkpoint.

## Remaining parity work

Library search/categories/favorites/hidden entries, missing-plugin resolution,
live opaque-state/structural publication and the OrbitCab partition discrepancy
remain. Surge's first-editor opaque zoom-state change still stops playback.
Song overview, parameter automation/instrument envelopes, recording/recovery,
persisted workspace and accessibility/shortcut parity remain. Foreground visual
and 60 Hz qualification, x64, reciprocal Mac reopen and sustained loaded
realtime audits remain open in `PARITY_PLAN.md`. Full parity is still active.
