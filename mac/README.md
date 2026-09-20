# ScreamSeq for macOS

A native tracker application built on the OpenMPT audio engine, with an AppKit interface, a Metal pattern editor, and direct Core Audio output. This is an independent derivative, not an official OpenMPT release.

## Build and run

Requires macOS 14 or newer, Xcode command-line tools and CMake. The current build has been exercised on an Apple M2 Pro with 32 GB RAM and macOS 26.5.2.

```sh
bash mac/build.sh
open bin/mac-native/ScreamSeq.app
```

The application is signed locally with an ad-hoc signature. Distribution signing and notarization require a developer identity; this build does not include one.

For development while using the Mac normally, run `bash mac/build-background.sh`
and `bash mac/test-background.sh`. This uses two compile workers at reduced
priority, writes to `bin/mac-background`, and gives the development app its own
bundle identity. The tests use disposable windowless sessions, offline audio and
offscreen editor images; they do not start device playback or control the desktop.
The normal `bin/mac-native/ScreamSeq.app` is left untouched. Live plugin-window,
MIDI-device and sustained display/audio checks still need a separate session.

## Working with songs

- **Patterns:** enter notes with Z–M and Q–U; choose octave and step above the grid. Arrow keys move between fields, Tab changes channel, Space plays/stops. Effects use the source format's command letters. Volume values are decimal; instrument and effect parameters are hexadecimal.
- **Command help:** Pattern → Command Picker… (Shift-Command-K) searches the current format's effect and volume commands, including extended subcommands. Choose a command, enter its value and apply to the displayed cell. **Use cursor** explicitly refreshes the target; stale edits reject. Pitch, volume, panning, timing and sound commands have distinct colors, and the grid footer explains the command at the edit cursor. Agents read the same catalog with `pattern.commands` and apply it with `pattern.apply`.
- **Independent editing:** turn Follow off to browse and edit while another pattern plays. Scrolling also turns it off, and the button immediately reflects that state. The footer labels the edit pattern/row/channel separately from the playback position above. Agents can read both through `context.get` and use `context.set` to navigate or toggle following with a check against concurrent cursor/selection changes; navigation leaves the song and transport intact.
- **Selection:** drag or use Shift with arrows. Command-C/V copies/pastes rectangular cells. Delete clears a selection. The Pattern menu provides transpose and row operations. Command-Z / Shift-Command-Z undo and redo module edits.
- **Pattern tools:** Pattern → Pattern Tools… (Shift-Command-T) provides interpolation, humanization, seeded randomization, scaling/fill, transpose, instrument remapping, reverse/rotate, expand/shrink and masked clearing. Choose selection/channel/pattern/all-pattern scope, preview the exact changes, then apply in one undo step. Expand/shrink and row insertion/deletion reject discarded data unless explicitly allowed. Row tools shift masked fields within the selected bounds and keep pattern length and separate plugin envelopes unchanged. Pattern → Insert Row protects nonempty data at the end; Delete Row explicitly removes the cursor row across all columns. Pattern → Mix Paste fills empty destination fields; Merge Paste skips empty source fields. The same commands are available to agents through `pattern.transform` and `pattern.paste`.
- **Orders:** use Arrange to see the complete order list, assign/reuse patterns, insert before/after, move or remove orders, and select a song sequence. The main strip shows a window around the selected order. Song Settings changes title, tempo, speed and channel count.
- **Samples:** import or replace WAV, AIFF, MP3, FLAC and formats supported by the embedded engine. Drag the waveform or enter exact frame bounds; click to place a paste cursor. Zoom to a selection or use +/−, Option-scroll or pinch; pan with arrows or horizontal/Shift-scroll. At individual-frame zoom, enable **Draw** and drag a waveform stroke; release applies one Undo step and Escape cancels. **Snap selection** and **Snap loop** find zero crossings or exact frame-grid boundaries; **Snap after selecting** applies this to each completed selection. Copy/Cut/Paste (Command-C/X/V with waveform focus) use the session's sample clipboard. Paste offers Insert, Overwrite, Mix and Replace, rate conversion, independent mix levels and a clipping preview. Deletion and structural paste adjust loops/cues with exact Undo/Redo. **Copy to new** creates a separate sample from the range and selected channels, retaining source settings and adjusted loops, then selects its new slot. Range/channel processing includes reverse, normalize, curved fades, gain, phase inversion, silence, trim, DC removal, smoothing and stereo operations. Loop crossfades offer linear/equal-power curves, preserved duration using pre-loop audio, or overlap with an explicit shorter loop period. PCM and loop geometry undo together. All these edits, drawing, crossfades and clipboard controls share the agent API. Edit rate, volume and pan; preview/apply normal and sustain forward/ping-pong/reverse loops together with compact undo through the native controls or `sample.loops.set`; native projects preserve exact sample data and settings.
- **Instruments:** create instruments from samples, import ITI/XI instruments, edit volume/pan/pitch/filter envelopes, sustain/loop ranges and the note-to-sample map, and set new-note/duplicate-note behavior. Double-click the envelope to add a node; drag nodes or edit their tick/value fields. Arrow keys adjust a selected node (Shift moves by four); Delete removes it. Sustain/loop node references adjust when a node is removed. Format-specific point limits are enforced. **Envelope tools…** opens copy/paste, repeated insertion, shift, flip, scale, ramp, sine and seeded humanization with exact previews. Loop/sustain/release markers follow moved nodes; replacement reattachments and integer rounding are reported. The clipboard works across instrument envelope windows; Apply saves one document Undo step.
- **MIDI:** select a CoreMIDI input or send from another app to `ScreamSeq MIDI In`. Arm recording in MIDI settings. Stopped transport records notes in steps; running transport records to the current row. MIDI timing is quantized to tracker rows.
- **Plugins:** add built-in effects or macOS AU/VST3 effects and instruments from the Plugins tab. Use the Mixer window for track/group/return inserts, sends, sidechains and independent instrument outputs; unassigned effects process Master in rack order. For an instrument plugin, create tracker instruments in the Instruments tab, then use the plugin’s **Instruments…** panel to assign one or more of them, each on MIDI channel 1–16. Aliases share one processor, sound state, automation and output routing; multitimbral plugins can give their channels different sounds. Preview checks the draft; Apply saves all assignments in one effect Undo step. Pattern notes, keyboard audition and incoming MIDI then play the plugin. Up to 64 devices share the rack. **Add built-in…** immediately lists Gainer, DC Offset, Stereo Expander, Digital Filter, EQ5, EQ10, Mixer EQ, Comb Filter, Distortion, Cabinet Simulator, LofiMat, Compressor, Bus Compressor, Gate and Maximizer without a plugin scan. Comb provides musical note/transpose tuning, signed feedback and smooth retuning. Distortion adds soft/hard clipping, folding and wrapping, a tone control and separate dry/wet levels; fixed 16× oversampling has automatically compensated latency. LofiMat adds bit-depth/rate reduction, seeded noise, smoothing and independent dry/wet levels. Filter/EQ controls support stereo, left/right and mid/side processing, logarithmic frequency/Q sliders and typed values.
- **Plugin discovery:** the Add plugin browser searches cached entries by name, format, instrument/effect and category. Favorites, custom categories and hidden entries persist per user. The catalog is cached across launches. Use **Rescan** in the picker after installing, removing or updating plugins. The first scan can take several seconds; reopening the picker reuses the inventory. Individual plugins are still validated before adding them.
- **Plugin interfaces:** **Open interface** opens the plugin’s own macOS window. Searchable native parameter controls remain available, with recycled rows for plugins containing thousands of parameters. Windows close cleanly and reopen across playback restarts. Plugins without a compatible custom interface use the native controls.
- **Pattern automation:** the envelope window edits pattern-relative plugin/built-in parameters with searchable targets, exact points, snapping and curved segments. **Use last touched** selects the last native/plugin-editor/API parameter change and loads any existing lane without discarding a draft or creating points. Agents use `automation.target.get` for the same stable target. Range tools copy points, repeat or insert a paste, shift/flip/scale, generate ramps/sines and humanize with a repeatable seed. Preview a tool, then Apply in one Undo step; Reload discards the preview. Copied points stay in the envelope editor's own clipboard. Envelopes repeat with the pattern. All tools also have agent API access.
- **Automation:** enable **Record automation** during playback and change a native parameter control or a plugin-provided control. Recorded values replay at their sample positions, including during WAV export and at other device sample rates. Initial parameter state remains separate from the recorded values. Clear automation and rack changes have their own bounded undo/redo history.
- **Saving:** Save, Save As and recovery use `.resonance` version 4 (version 5 for instrument aliases or a non-default plugin MIDI channel), containing the source-format module, exact native sample data, mappings and envelope corrections, stable song identities, arrangement annotations, mixer routing, automation, built-in/AU/VST3 state, bus activation, instrument assignments and selected sequence. Versions 1–4 remain readable. Older applications reject version 5. Export Module separately writes MOD/XM/S3M/IT/MPTM and rejects native feature/sample loss. Agents can use `document.save` and `document.exportModule`, including dry runs and explicit overwrite control. See [sample snapshots](SAMPLE_SNAPSHOTS.md) for scope and format limits.
- **Export:** export float32 stereo WAV at 48 kHz. Native effect latency is removed from the beginning, and effect tails are rendered. The one-hour export limit fails explicitly and retains the original destination file.
- **Recovery:** dirty documents receive three rotating recovery generations, normally every 30 seconds while no other document operation is running. File → Recover Last Session opens the newest available generation. Saving clears only that document's own recovery generations.
- **External applications and agents:** Automation → Enable Local API exposes the open song and cursor through a private local JSON interface. Read and patch patterns, samples, instruments, plugins and automation, with revision checks and undo. The bundled Python client includes a working crescendo drum-roll example. See [the automation guide](AUTOMATION.md).

The Mixer’s **Strips** view shows scrolling native faders, independent stereo
meters, input/output balance, width, pre gain and mute/solo for tracks, groups, returns and
Master. Only visible strips and their neighbors allocate controls. **Routing /
effects** opens the selected bus inspector. Its **Controls** button opens
searchable native parameters for the selected effect; **Open UI** opens its
custom interface. Fader gestures use the same smooth preview/one-step commit API
as agents. Input balance acts before insert effects; output balance acts after
them. Nonzero input balance requires native metadata v6; older builds reject
these projects. Centered input balance retains v4/v5 compatibility.

The Plugins editor’s **Save preset…** and **Load preset…** buttons reuse settings
across songs through native `.resonance-preset` files. Loading keeps the current
plugin’s routing, instrument/channel assignments and automation, stops playback, and supports
**Undo effect change**. Agents can inspect/save/load the same files with separate
file and song revision checks. Factory/program and vendor preset browsers remain
separate future work.

## Sample library

Open **File → Browse Samples… (⌘⇧L)** or **Samples → Browse…**. The first visit indexes `~/samples` if present. **Add folder…** adds other sample packs; the root menu scopes a search to one library. Removing a folder only removes its index entry, never its files. The persistent cache reopens without another disk walk; use **Rescan** after adding, moving or deleting files.

Search combines words across the sample name and every parent folder, regardless of nesting. For this library, `808 "bass drum"` finds all matching bass drum variations; `junos chords` finds chords across the Junos pack. Quotes keep phrases together, and `-maschine` excludes matching paths. Search ignores case and accents. The left pane treats folder names as inherited tags: selecting a pack/category includes all nested samples, and ⌘-click intersects tags. **Find a folder tag** narrows this list. **Clear filters** resets the query and tags.

Use ↑/↓ to move through results with **Auto-preview** enabled, Space to replay, and Escape or Stop to stop. Preview has its own volume, waveform, rate/channel/duration display, and player independent of the song transport and plugin rack. It uses the first 30 seconds, capped at 2,097,152 frames for high-rate files; long-file limits are shown. Source decoding uses the tracker sample engine, including its 8/16-bit internal sample representation. Preview does not normalize or change the source. The dedicated preview player currently follows the macOS default output.

⌘-click or Shift-click selects several files; **Load selected**, Return, or double-click appends new sample slots in one document Undo step. **Create an instrument for each sample** optionally makes the corresponding mappings. Loading validates the entire batch before editing: an unreadable file cannot leave a partial import. Batches are limited to 128 files and 256 MiB each of encoded and decoded audio, within the current module format's sample/instrument limits. Importing stops song playback, as other structural sample edits do. The existing **Replace…** action still replaces one slot. **Choose files…** also supports a multiple-file batch without adding a library folder.

Indexing and searching run off the UI thread, and result rows are reused and paginated. The index excludes hidden metadata, `__MACOSX`, and symlinks. Supported filename extensions are candidates; a corrupt or unsupported file reports a decoding error. The library, metadata, preview and bulk import are also available through the [local API](AUTOMATION.md#sample-library-and-bulk-import).

### Multi-sample instruments from filenames

Selecting a note-named sample now offers **Import as one instrument…** when its
folder contains related recordings at other notes. Detection uses the full
indexed family, including files outside the current search or page. The review
shows every filename, its detected note, proposed tracker root and playable key
range. Rename the instrument or adjust **Octave offset**, optionally **Check
import**, then **Import one instrument**. Space or Preview selected auditions a
source file. Cancel leaves the song unchanged.

Leading sequence numbers are ignored for family names. Matching numeric
prefixes can also suggest an octave convention: the 109-file Clav Junos family
labels numbered key 0 as C−2, so its suggested +2 offset maps it to tracker
C-0…C-9. The dialog makes that conversion visible. Detection supports note
tokens with sharps/flats and negative octaves, retains velocity/round-robin
suffixes, and keeps different folders and file types separate. Ambiguous names
are ignored; duplicate roots or notes outside the supported range must be
resolved before import. This is filename inference, not audio pitch analysis.

Each supplied root plays its own sample at recorded pitch. Missing keys between
roots use the nearest recording with semitone transposition (ties choose the
lower root). Keys outside the reviewed range are unmapped. The complete import
is one Undo step, with exact native save/recall, and shares the
[`sample.library.multisample.get` / `instrument.importMultisample` agent workflow](AUTOMATION.md#filename-based-multi-sample-instruments).

## Compatibility boundaries

The editable formats are MOD, XM, S3M, IT and MPTM. Format-specific note, command, row, channel and sample limits are enforced. Legacy formats are preview-only. The import report identifies missing external samples and embedded tracker plug-ins that this host cannot run. External sample paths are resolved during import, including Windows-style relative separators; successfully loaded samples are embedded in subsequent native saves.

The native plug-in host supports macOS AU and VST3 effects and instruments, and built-in effects, with up to 64 devices in total. A stereo routing graph provides track/group/return inserts, pre/post sends, sidechains and auxiliary instrument outputs. Instruments use tracker-instrument assignments. Mixer buses and assigned plugin instances together have a 250-slot capacity; additional instrument aliases and effects use no extra adapter slots. MIDI-controlled effects and plugin MIDI-output routing remain pending. Custom interfaces support AU Cocoa views and VST3 NSViews. Apple effects, DLSMusicDevice and a deterministic VST3 test bundle have automated coverage. Other installed plugins are individually probed before use, but a probe is not full qualification. Runtime processing is in-process, so a faulty plugin can crash the application. A render failure or non-finite sample stops playback. Hardware output pairs, AUv3 view-controller interfaces, VST2, Windows DLL hosting and automatic Windows plug-in migration are not implemented. Auxiliary plugin buses support mono/stereo layouts; surround layouts are listed but unavailable.

Effect-chain history is separate from module undo. Structural sample/instrument/order/effect-chain changes stop transport before replacing engine assets. Existing song sequences can be selected in Arrange and are honored by playback and export; creating new sequences is not yet exposed. This is an active development build, not a claim of full OpenMPT parity or broad hardware qualification.

## Automated checks

```sh
bash mac/test.sh
# Also exercise the real output device (plays the built-in demo):
bash mac/test.sh --device
# Add a one-minute real-device stress run with quiet, dense audio:
bash mac/test.sh --soak
# Optional longer audio-only workload, in seconds:
bash mac/test.sh --soak 300
# Optional actual output/capture comparison through an already installed virtual device:
bin/mac-native/loopback-tests "BlackHole 2ch"
# Memory/undefined-behavior checks (not performance measurements):
bash mac/sanitize.sh
# Visible combined UI/audio workloads; keep the app visible and Mac unlocked:
bash mac/ui-test.sh 600
bash mac/ui-test.sh 1800 --no-build
```

The tests cover atomic pattern edits, grouped undo, sample processing and rollback, pattern/order/sequence persistence, effect-chain undo, sample PCM preservation, channel/format limits, bounded edit queues, audition, panic, looping, module/project save/reopen, AU state recall, buffer-independent automation, live/offline AU equivalence, latency/tail export, malformed projects, missing plugins, recovery rotation, real CoreMIDI loopback/disconnect and bounded concurrent MIDI input. Native interface tests cover keyboard transport/remapping, note release, deferred edits, envelope-node operations and compact layouts with long names.

`test_audio.py` runs the editable renderer and an independently built stock libopenmpt renderer in separate processes, requiring exact equality of every finite float sample and the frame count for each fixture at 44.1, 48 and 96 kHz (up to 30 seconds per fixture). A test-only dyld interposer verifies zero intercepted malloc/calloc/realloc/free/posix_memalign or pthread mutex/read-write lock calls during each core render. The three qualified Apple effects also pass allocation/lock checks, including first render. It first proves its own allocation/free/lock instrumentation works. This does not prove the absence of every possible system call, allocator or lock primitive in arbitrary unqualified plug-ins.

`device-tests` measures repeated Core Audio start/stop and native AU processing at the device's negotiated rate and buffer size. Its overrun metric compares callback execution time with the buffer deadline; it is not a hardware loopback recording or a complete device xrun counter.

`loopback-tests "BlackHole 2ch"` records only that explicitly named virtual device, leaving the system default route and its existing rate/buffer configuration unchanged. Dry and AULowpass playback each matched 96,000 captured stereo frames exactly at 48 kHz / 512 frames, with no input timestamp gaps or output callback overruns. This checks the live Core Audio route; it does not measure a physical DAC, speakers, or microphone-to-output latency. The test fails if the device is absent, the format is unsupported, capture is silent/truncated, samples differ, or timestamps are discontinuous.

`ui-test.sh` creates a dense 127-channel fixture and runs the visible app with scrolling, selection, editing, undo/redo and project saving during actual Core Audio/AU playback. It checks real drawable presentation intervals, CPU/GPU work, main-thread/drawable waits, audio deadlines and p99.9 callback time. It waits for stable visibility, temporarily keeps its test window above ordinary windows and prevents idle display sleep, fails if the window becomes hidden, and writes a process/source-identified report to `bin/mac-native/qualification/ui-<seconds>s.json`. Only its own test instance is stopped afterward. Lightweight progress is written off the main thread to `/tmp/resonance-ui-progress.json`; the final report explicitly states whether measurement started. A visibility timeout is a failed prerequisite, not a performance pass.

A one-minute visible workload passed; the subsequent ten-minute run failed after the display locked and also recorded one audio deadline overrun. A later attempt ended after 46.6 seconds when its window became hidden; two further attempts could not establish stable visibility. Screen-capture tools also became unavailable. The corrected long run is still pending a visible desktop. **The final sustained 60 fps and 30-minute combined audio gates have not passed.** See `COMPATIBILITY.md` for the measured scope.

## Structure

- `editor/`: portable editable document, command history, serialization and rendering.
- `mac/Audio/`: Core Audio, bounded CoreMIDI input, AU/VST3 effects and instruments, and offline export.
- `mac/Bridge/`: Objective-C++ interface and versioned project persistence.
- `mac/App/`: native interface, Metal grid, asset editors and effect controls.
- `mac/Plugins/`: isolated discovery/validation executable.
- `mac/Tests/`: fidelity, real-time instrumentation, engine, session, plug-in and device tests.

The original BSD license and bundled dependency notices must accompany redistributed builds. Upstream's contribution policy does not accept AI-assisted contributions; this work is maintained locally as a derivative.

## Package

`bash mac/package.sh` produces an ad-hoc-signed application ZIP in `bin/mac-native/distribution/`, including the user guide and third-party notices. It is a local development distribution, not a notarized public release.

## Native plugin verification

The VST3 interface sources are vendored at pinned revisions under `mac/ThirdParty/vst3`, with their MIT notices. No SDK download or Windows compatibility layer is needed to build.

```sh
bin/mac-native/native-plugin-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
# Custom-window lifecycle and gesture recording; requires a visible desktop and the existing virtual device:
bin/mac-native/native-plugin-tests "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3" --ui --loopback-record
# Captures only the named virtual audio route, preserving default audio routing and its existing settings:
bin/mac-native/loopback-tests 'BlackHole 2ch' "$PWD/bin/mac-native/test-plugins/ResonanceFixture.vst3"
# Visible dense tracker workload with AU + VST3, recording automation during playback:
bash mac/ui-test.sh 60 --no-build --vst3
```

The fixture is built into the test directory and is never installed in the user’s plugin folders. Tests cover processor/controller state, exact automation timing, tracker notes and releases, audition, latency alignment, custom interfaces, project reopening, and WAV fidelity. The regular test and sanitizer scripts include the native plugin tests. See `COMPATIBILITY.md` for measured coverage and current limits.

The latest plugin run passes all seven CTest suites, memory/undefined-behavior checks, custom-editor gesture recording and all five virtual audio paths. The VST3 visible 60-second workload could not begin because the Mac was locked; it is not a performance pass. Logs and the failed prerequisite report are preserved in `doc/mac-native-qualification/2026-09-19-plugins/`.

Battery 4 AU/VST3 loading and cached discovery are covered by the [latest qualification](../doc/mac-native-qualification/2026-09-19-battery/README.md). To repeat the optional commercial-plugin probes without windows or device output, run `python3 mac/Tests/test_battery.py`.

The Arrange window now supports named song sections, previous/next section
navigation, pattern names and shared pattern notes. Section markers follow their
order when it moves. These details share the document's Undo/Redo history and
are saved in version 4 `.resonance` projects. Track names and colors are available
through the same agent API. Module-only saves reject native metadata loss.

Use `bash mac/inspect.sh` to prepare a development copy with a separate bundle
identity. Launch its executable with `--inspection --automation` to keep test
windows and recovery files separate from an ordinary editing session.

Pattern → **New Note Track…** appends empty polyphonic note columns; **Group
Selected Columns…** groups an existing adjacent selection under a named shared
mixer group. The grid shows the group above its columns. Click a column header
to mute it independently; mutes persist in native projects and support Undo.
**Ungroup Current Track** keeps notes, routing and effects. Shared processing is
edited in the Mixer. Plugin output routes remain explicit in the Mixer. These
native features use metadata v5 and are unavailable in older builds.

**Add plugin…** opens a searchable table with format, effect/instrument, category,
favorites and hidden-entry filters. Select a plugin to add it or save its browser
preferences. **Reload list** uses the cache; **Rescan installed plugins** is
explicit. **Show hidden** lets you restore a hidden plugin. Preferences persist
across songs and rescans without changing the current song's Undo history.
The built-in browser uses the same controls and never scans installed plugins.

Pattern → **Tempo and Groove** edits fractional BPM, ticks per row, musical beat/bar lengths and global groove, with preview, two-row swing and document Undo. Its agent commands are `document.timing.get` / `.set`; legacy `document.patch` also accepts fractional tempo. Musical timing is opt-in; existing imported timing and per-pattern overrides remain intact. Native snapshots retain settings that the original module format cannot store, and module export rejects such losses. Dedicated tempo envelopes and expanded plugin transport qualification remain pending.

Autosave protects unsaved editable songs every 10 seconds while the document is
available, retaining ten recovery copies per session. Click the footer's
**Autosave** status or use **File → Recover a Song…** to choose a dated copy.
Recoveries open as unsaved songs; use Save As to keep them. Unfinished recording
takes are included and reopen stopped for review. Write failures remain visible
in the footer without interrupting work with repeated dialogs. Run
`RESONANCE_BUILD_DIR="$PWD/bin/mac-background" python3 mac/Tests/test_recovery.py`
to qualify the real timer, abrupt process death, restart, recovery, and error
handling without visible windows, hardware audio or real user recovery files.

**Pattern → Precise Notes…** (Command–Shift–N, or Return on a note) delays notes
within the selected row. Select the note, enter **Row offset**, and click **Apply**;
the visible fields are saved directly. `0` means row start, `0.5` halfway through
the row, and `0.75` three quarters. For example, at 125 BPM with six ticks per row
in classic timing, `0.5` delays the note by 60 ms. It changes when the note starts,
not where playback starts inside the sample. **Add** creates another event;
**Check edit** validates the draft without playing audio. Leave **Replace the
ordinary note in this row** enabled when moving an existing note. The `~` marker
identifies precise events, including ones at offset zero.

Agents use the same `pattern.notes.get` / `pattern.notes.set` commands. Positions
are absolute within the pattern, in 65,536 units per row: halfway through row 3
is `3 * 65536 + 32768 = 229376`. When replacing an ordinary onset, include
`clearRows: [{"row": 3, "channel": 0}]` and preserve the other returned events.
The edit retains effects and is one document Undo step. Native project save,
recovery and audio export retain precise timing; legacy module export cannot.

The connected workspace, reusable audio/modulation graphs, dedicated pattern graph
lanes and their agent API are described in [GRAPH_WORKFLOW.md](GRAPH_WORKFLOW.md).
