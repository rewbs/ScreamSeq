# Quiet background checkpoint — 20 September 2026

G04 pattern-envelope tools and A09 source-format command discovery are implemented and automatically checked. Logs and offscreen views are preserved here; see [the ledger](../../RESONANCE_EXPANSION_PROGRESS.md) for exact scope and remaining work.

The application is built separately in `bin/mac-background/Resonance.app`, with a distinct bundle identity and reduced-priority two-worker build. The normal and copied stable app remain unchanged. No desktop control, visible app windows, physical audio or commercial plugin loading was used for this checkpoint.

43/43 selected background-safe regression suites passed, along with both API hosts, startup and offscreen native tests. Three automation-related sanitizer suites and the command catalog sanitizer check passed. Device/MIDI/stress and sustained 60 fps checks remain deferred. Offscreen AppKit controls can have blank system bezels/text; snapshots establish geometry and application drawing, not complete live native-control appearance.

A15 navigation was subsequently added and verified with both socket hosts and offscreen native tests. Its final logs and `BuildInfo-navigation.json` identify that build. No new engine/audio path was introduced by navigation, so the preceding 43-suite regression remains the audio baseline.


The note-track checkpoint adds A02 and the A06 named-track scope. See the
`resonance-note-tracks-*` logs and `BuildInfo-note-tracks.json`. All 44 safe
core suites and both socket/startup hosts passed. The combined verified-tests
log ends with an offscreen dialog layout failure; the delivery-interface log
records its corrected, passing state. Actual muted WAV export and three targeted
ASan/UBSan suites pass. `NoteTrackGrid*.png` uses the real Metal pipeline with no
window presentation. The native dialog snapshot retains the known blank system
control limitation. Hardware audio/MIDI, live interaction and sustained display
cadence remain deferred.

E10 now adds catalog search, filters, favorites, categories and hidden entries.
The plugin-library logs and manifest capture its targeted core/sanitizer checks,
both complete API suites, windowless startup and offscreen browser tests. The
4,096-entry load/search timing is diagnostic, not a display-cadence result.
Preset/program browsing and live UI checks remain open.

G06 last-touched learning is captured by the automation-target manifest/logs and
panel snapshot. The combined run passes 46/46 safe core suites, both socket hosts,
startup and offscreen interface checks; its targeted sanitizer run also passes.
A real local VST3 edit callback is tested without a plugin window or audio device.
Pattern-command mappings, live input/display and hardware performance remain open.

A01 scoped row insertion/deletion is covered by the row-tools logs. Its targeted
core suite includes 450 independent five-format/mask/count references; normal
and sanitizer runs, both complete socket hosts and final interface checks pass.
The initial interface failure exposed hidden-window content shrinking: the final
harness explicitly pins and checks viewport dimensions. The corrected snapshots
use those dimensions; this remains offscreen qualification, not live display QA.

E01 adds the virtualized mixer strip overview and stable selected-effect access.
The mixer-strips logs cover offscreen 240-bus recycling and gesture tests, both
API hosts, the signed build and windowless startup. The snapshots cover both
Strips and Routing. No new DSP path was introduced. Meter-loop timings are
diagnostics only; live interaction, hardware and display-cadence gates remain open.

E01 input balance adds the `prePan` API/control and native metadata v6 when
nonzero. The pre-balance logs contain independent stereo/insert/send/sidechain
references, exact block-independent and interrupted ramps, actual WAV output,
five-format persistence, 46/46 safe suites, both API hosts, and three passing
ASan/UBSan suites. The combined run ends with a test compile error; corrected
interface tests then identified a compressed heading, resolved by a scrolling
routing inspector. The delivery-interface log records the passing final state.
The input-balance snapshots were inspected; delivery build/startup/verification
logs and the manifest identify the final signed application. Original and frozen
executables are unchanged. Live display/hardware/commercial plugins remain open.

E10 native plugin preset files are captured by `resonance-plugin-presets-*`,
`BuildInfo-plugin-presets.json` and the two `*-presets.png` snapshots. The complete
run passes 48/48 safe suites, both API hosts, startup, picker/recovery and offscreen
UI checks. Both new preset suites pass ASan/UBSan. Round trips use built-in Gainer,
local VST3 effect/instrument and Apple LowPass AU; no commercial plugins, hardware
or native file dialogs were exercised. Factory/program browsing remains pending.

A12/A13 fractional tempo, musical timing and global groove are captured by the
song-timing logs/manifest and `SongTimingEditor.png`. The 50-suite safe regression,
both API hosts, startup, offscreen layouts and four targeted sanitizer suites pass.
The initial integration failure exposed duplicate knot emission at callback
boundaries; the corrected automation, extra and full regression logs pass. Native
projects preserve source-format timing losses with optional `RSONGS2` snapshots,
and module export rejects them. Live presentation/hardware and expanded plugin
transport qualification remain deferred.

E08 shared plugin instrument aliases and MIDI channels are captured by the
plugin-aliases logs/manifest and two alias snapshots. The full regression and
corrected test reruns cover all 51 safe suites; both API hosts, startup and
offscreen UI pass. AU/VST3 weighted audio, actual WAV/save/reopen, state/preset
retention and capacity boundaries pass, including targeted sanitizers. The first
run's two failures were test expectations (fixture output scale and newly valid
project v5), corrected in the targeted logs. Final capacity guards were rebuilt
and retested. Ordinary/frozen binaries remain unchanged. MIDI-controlled effects,
live UI, hardware and commercial plugin qualification remain pending.

G04 instrument-envelope tools are captured by the instrument-envelope logs,
manifest and two envelope-tools snapshots. All 53 safe suites, both API hosts,
startup and offscreen UI pass; six targeted sanitizer suites pass. The initial
roundtrip failure exposed legacy envelope losses now retained by optional
`RSENVS1` correction records. Native transforms preserve marker ownership,
report reattachments/rounding and match independent complete WAV references.
Compact control bounds/style issues were fixed and final snapshots inspected.
Live interaction, presentation and hardware gates remain deferred.

E10 AU factory presets and VST3 unit/program browsing form the stopped checkpoint.
The `resonance-plugin-programs-*` logs, `BuildInfo-plugin-programs.json`,
`resonance-plugin-programs-delivery-verification.json` and two `*-programs.png`
snapshots identify it. All 54 selected safe core suites, both API hosts, startup
and picker/recovery pass. The combined log ends on a Swift test-fixture bracket;
the corrected final interface log passes. Only that test source differed from
the package; the manifest was refreshed and the app re-signed. Four targeted
ASan/UBSan suites pass, including exact AU/VST3 program audio and guarded state
replacement. The signed frozen app is in
`bin/mac-checkpoints/2026-09-20-factory-programs/Resonance.app`. Earlier normal and
frozen executables are unchanged. See the stoppage report for remaining scope.
