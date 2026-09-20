# Resonance background development — 20 September 2026

Development stopped at the user’s request after the factory-program checkpoint. The A01–A15, G01–G06 and E01–E13 priorities remain incomplete. See the [stoppage report](RESONANCE_STOPPAGE_REPORT_2026-09-20.md) for the delivered build, verification and remaining work. The separate background build does not replace the ordinary application.

## Available in the background build

| Area | Delivered in this background run |
|---|---|
| A01 / A06 | Scoped row insertion/deletion, field masks, exact previews, loss guards and one-step Undo; native row menus use the same agent command. |
| A02 / A06 | Named polyphonic note tracks built from adjacent columns, shared mixer groups, creation/grouping/ungrouping, saved independent column mutes and whole-note-track tool scope. |
| A09 | Searchable source-format command picker, descriptions, effect-family colors, cursor help and agent command catalog. |
| A12 / A13 | Fractional BPM, musical timing, beat/bar lengths and saved global groove, with native preview/swing, shared agent commands, exact timing persistence and loss-safe module export. Dedicated tempo envelopes remain pending. |
| A15 | Explicit Follow state, separate edit/playhead feedback and revision-checked agent cursor navigation without changing the song. |
| G04 | Envelope copy, repeated/inserted paste, shift, flip, scale, ramps, sine and seeded humanization for pattern automation and instrument envelopes. Instrument tools preserve marker ownership, report reattachments/rounding, and retain source-format limits. |
| G06 | “Use last touched” selects a native, plugin-editor or API parameter edit by stable identity, retains drafts and loads an existing envelope. Agents use `automation.target.get`. Pattern-command bindings remain pending. |
| E10 | Cached searchable plugin browser, explicit rescan, format/kind/category filters, favorites and hidden entries. Preferences persist separately from songs. Native preset files and standard AU factory/VST3 unit programs can be browsed and loaded through the UI and API. Vendor preset-file interchange/proprietary browsers remain pending. |
| E08 | Shared AU/VST3 instrument aliases with independent MIDI channels, one processor/state/automation/output set, native preview/apply, stable agent ownership and Undo. MIDI-controlled effects remain pending. |
| E01 | Scrolling mixer strips with stereo meters, faders, pre gain, independent input/output balance, width and mute/solo. Routing/effect access keeps stable bus/plugin identities; generic controls open in the existing Plugins editor. |

Every new musical operation has an agent interface. Pattern/envelope edits retain document Undo and project persistence. Browser preferences and last-touched information deliberately do not create song edits: preferences have their own revision, while the touch target is session-local.

## Builds and background behavior

- Frozen latest checkpoint: `bin/mac-checkpoints/2026-09-20-factory-programs/Resonance.app`.
- New features: `bin/mac-background/Resonance.app`, displayed as **Resonance Background**.
- Ordinary app: `bin/mac-native/Resonance.app`, preserved.
- Frozen copy: `bin/mac-stable/Resonance.app`, preserved.
- Builds use two workers at reduced priority. Heavy checks run sequentially.
- No screen control, visible windows, physical audio/MIDI or commercial plugin loading was used for this run. Disposable windowless hosts, offscreen rendering, built-in effects and the local VST3 fixture provide isolation.

Aliases or non-default plugin MIDI channels use project envelope version 5. Older builds reject these projects; ordinary single-channel assignments retain version 4.

Envelope data that a source writer loses now uses optional `RSENVS1` snapshot corrections, including sampleless XM instruments and pan endpoint 64. Older builds reject the extension.

Timing that the source module cannot retain uses an optional `RSONGS2` snapshot, also rejected by older builds. Source-compatible timing keeps the older payload.

Grouped note tracks and saved column mutes use native metadata version 5; nonzero mixer input balance uses version 6, inside the existing project container. Older builds reject that metadata. Continue using the background build for projects saved with those features; featureless projects retain older metadata compatibility.

## Verification

The factory-program checkpoint passed **54/54 selected core suites**, both complete API hosts, actual windowless application startup, plugin-picker/recovery checks and offscreen native interface tests. Physical MIDI and stress suites were excluded.

The row-tools change passes its extended core suite with **450 independent five-format/mask/count references**, both complete API hosts, offscreen interface checks and ASan/UBSan. Targeted sanitizer checks also passed for note tracks, native metadata/mixing, automation transforms, command discovery, plugin preferences and learned targets.

The mixer update passes the full 46-suite core regression, both API hosts and startup. Its input-balance tests independently verify insert order, pre-fader sends/sidechains, actual WAV output, five-format persistence and interrupted five-millisecond ramps. Three targeted sanitizer suites pass. Compact Strips/Routing geometry and scrolling are checked offscreen; live display testing remains deferred.

Native preset tests pass for built-in effects, local VST3 effects/instruments and Apple LowPass AU, including state restoration, stale-file protection, preserved routing and Undo/Redo. Both new suites pass sanitizers. No commercial plugins or file dialogs were opened.

Timing checks cover independent groove clocks, odd beats, tempo/speed commands, pattern overrides and sequence defaults. The new VST3 automation test found and fixed a repeated-knot boundary bug; grooved envelopes now compare sample-for-sample across buffers. Four related sanitizer suites pass. Ordinary sample mixing retains a sub-1e-6 partition rounding bound.

Alias audio checks cover process-local AU and VST3 MIDI-channel weights, independent note-offs/mutes, one processor/output, exact buffer/rate references and complete save/reopen WAV equality. Four related sanitizer suites and the final capacity-boundary sanitizer checks pass.

Factory-program tests cover standard AU presets and multiple VST3 units, exact offline output at three rates/four buffer sizes, preserved routing/history/recall, changed or oversized catalogs and failed preparation. Four final ASan/UBSan suites pass. A test-fixture syntax correction was needed before the final offscreen suite passed; no production source change was needed.

Instrument-envelope tests cover all nine tools, marker ownership, malformed correction records, three-format persistence/history and complete WAV equality against independent references. Six related sanitizer suites pass.

Audio verification includes sample-exact grouped-versus-existing-native-mixer renders, mute and held-voice behavior, actual WAV export, sample-rate/buffer-size comparisons, mirrored-envelope timing and callback allocation/free/lock audits. A real VST3 editor callback is exercised through the local fixture without opening an editor window.

The offscreen harness now pins and asserts each requested viewport: an unpresented AppKit window had otherwise shrunk one content view to its fitting width while keeping the requested window frame. Updated row-tools and automation snapshots were inspected. Offscreen system controls can still have blank bezels/text.

**Still unqualified:** live input/custom-editor interaction, sustained presented 60 fps, combined hardware/audio load, and commercial-plugin compatibility under those conditions. No offscreen timing or short audio test is counted as passing those gates.

Evidence and build manifests: [background qualification folder](mac-native-qualification/2026-09-20-background/README.md). The [implementation ledger](RESONANCE_EXPANSION_PROGRESS.md) records each checkpoint and its precise verification scope.

## Remaining priority work

| Priority group | Main remaining work |
|---|---|
| Pattern editing, A01–A15 | Independent pan/delay and additional effect subcolumns; high-resolution note timing and accurate recording; mono/poly/chord entry and quantization; probabilistic/ghost-note behavior; dedicated tempo-envelope automation and broader transport qualification; metronome/count-in. Existing keyboard/clipboard and source-format effects still need complete live workflow qualification. |
| Automation, G01–G06 | Mixer/macro automation targets and recording qualification; arbitrary-row seek/chase and muted-clip behavior; explicit pattern-command mappings to stable plugin parameters. Live editing/performance checks remain open. |
| Mixing/plugins, E01–E13 | Multiband sends; separate hardware output pairs; plugin MIDI output routing; vendor preset-file interchange/proprietary browsers; auto-suspend and safe dynamic parameter/bus/latency changes; runtime plugin process isolation; broader real-plugin and sustained routing/PDC qualification. |

The other previously authorized arrangement, sampling, instrument and built-in-effect sections remain on the ledger. This checkpoint neither marks them complete nor removes them from scope.
