# Live graph walkthrough — 1 October 2026

Independent snapshot e48c645bb plus source fingerprint 4d6082eb51b7bf8505f4da0ce4ba9e7cf16a976343bd5a7ce0c4a6ef9edb69cc; PID51911, private Graph journeys project. BlackHole2ch48k/512, no system audio changes. Floating1060×832pt graph. Original fixture Connected Circuit with8 channels, four sample instruments, existing reusable graphs. Screenshots in screenshots/. This is an interim ledger; failures remain failures until re-walked on the fixed build.

Initial setup: opened graph, floated it, chose Kick focus, Fit, selected Kick. Setup is excluded from counts. Space started playback, and it ended naturally without loop; API then explicitly enabled a looping64-row region. Graph work did not affect the musician session.

| Journey | Observed actions | Panel switches | Redundant confirms | Result |
|---|---:|---:|---:|---|
| 1 Add reverb to selected channel3 | 3: Shift+A, type reverb, Return | 0 | 0 | Apple AUMatrixReverb inserted, controls visible. Stopped start. |
| 2 Insert EQ between reverb and compressor | 4: select wire, Shift+A, type EQ, Return | 0 | 0 | Correct rack order; playback remained active. Compressor was created as fixture setup immediately beforehand using same cable-first picker. |
| 3 Sidechain input | 1 intended drag; 2 live attempts rejected; stop+drag then worked | 0 | 0 | Sub→Kick compressor detector1. Live activation failed, old transport preserved. UI rejection silently lost. Fixes in progress. Earlier own-bus experiment correctly rejected and is fixture exploration, not the intended cross-channel path. |
| 4 LFO to frequency | 3: drag card to host parameter, type0.1 in focused depth, Return | 0 | 0 | Live root modulation, stable param10, range0…0.1. Setup: LFO created, EQ selected, parameter searched beforehand (6 actions). Whole path including setup9. |
| 5 Quiet send/return | 5: drag output to empty canvas, type return, Return, type−12, Return | 0 | 0 | Silent send was selected/focused then enabled at−12dB. Initial separate Add-return exploration took3 but created only a bus; one reveal plus Undo reset it. New bus hidden by current channel focus found as separate regression. |
| 6 Diagnose mute | 3: More, Trace silence, uncheck Mute | 0 | 0 | Correctly identified Kick muted; framed the channel and repaired in same inspector. API fixture setup muted channel before walk. |

Return in a focused numeric field counts as an ordinary input commit, not an extra Apply/confirm. The UI tool connection briefly failed while adding the send return; the app stayed alive and reconnect verified one successful insertion; no duplicate action was sent.

| Further journey | Actions | Panel switches | Redundant confirms | Result |
|---|---:|---:|---:|---|
| 7 Find and fix overload | 10: More/Find next twice, click value/Select all/type0/Return, More/Clear | 0 | 0 | First detector input then upstream Sub output identified. Source reduced24→0dB; latched indicator cleared. Search from prior EQ incorrectly left the compressor list empty, and internal overload said CLIP; both fixed afterward, re-walk pending. |
| 8 Group/name/export | 6: marquee, Ctrl+G, type name, Return, More/Save | 1 graph depth | 0 | Punch contour group retains actual EQ/compressor identities, then independent recipe copy with main+sidechain boundary. Playback active. Setup moved cards apart for marquee (2 drags). |
| 9 Instrument graph edit/return | 9 intended: More/Instrument1, Return, processor click, value click/Select all/type/Return, Song | 2 graph depths | 0 | Live parameter edit rejected on this build after root LFO creation. Stop+same4 edit actions succeeded; returned to same song viewport and resumed. Fix/re-walk pending. Rejected initial4 actions and stop/retry/resume are additional observed cost, not a pass. |
| 10 Live repatch plus history | 3: Hats output→group Main input drag, Undo, Redo | 0 | 0 | EQ/compressor moved from Kick to Hats; Kick retains reverb. Stable processor IDs, sidechain and LFO binding preserved, playback continues, 0 overruns/faults/plugin failures. |

All10 journeys were attempted. Journeys3 and9 remain live failures in this coherent snapshot and must be rerun after integration. Broad AU/VST activation, performance, commercial plugin/audio evidence remain separate acceptance gates. The pass discovered and triggered fixes for lost release during loading, disappearing error feedback, stale edge-index gestures, new-node filter exclusion, stale parameter search, float-overload terminology, stale success error, and recipe edits following root-source changes. It does not certify those newer fixes until rebuilt and walked.

## Coherent recheck: sidechain and instrument editing

The next independent build uses source fingerprint `846ebff854aacd455b2ed2308b11e06b371b8cb0381be151be873cc5e68b6678` and signed executable SHA256 `6dc51ca942f8cb34cba3c66c89795146a7357a6ff4324d0ff2f6c7136db949ce`. Its full CTest suite passed 89/89, its actual socket suite passed, and its copied-source AppKit interface suite passed. These results apply to this checkpoint, before the later source-range controls and detector/tail refinements.

The private fixture was copied from the saved first walkthrough. Setup ungrouped the exported EQ/compressor pair, removed its detector cable, selected BlackHole 2ch at 48 kHz/512 frames, and looped pattern 0. The following are new actual UI observations, not reinterpretations of the original failures.

| Recheck | Actual actions | Switches | Extra confirms | Result |
|---|---:|---:|---:|---|
| J03: activate unused compressor detector during playback | 1 drag: Sub output to Compressor Detector input | 0 | 0 | First attempt succeeded; main paths and send retained. `j03-recheck.json` records continued playback, matching requested/rendered plan 2, and zero overruns/faults/plugin failures. |
| J09: edit instrument recipe after creating another song LFO | 9: More, Instrument 1, Return, select trim, click value, Select All, type −9, Return, Song | 2 graph depths | 0 | Gain changed immediately during playback, with no retry. `j09-recheck.json` confirms the saved −9 dB and continuing transport at frame 5,076,480. |
| Manual versus effective parameter value | Search Band 1 frequency; edit base 80→100 Hz, Return | 0 | 0 | Manual base stays distinct from effective snapshot 519.7 Hz. The edit succeeds during playback. However, both ⌘Z and Edit → Undo then do nothing; this newly discovered history regression is still being repaired. |

Screenshots `j03-recheck-before/after.png`, `j09-recheck-before/edit/after.png`, and `parameter-manual-effective.png` record these checks. The final API sample before shutdown records 20,228,608 rendered frames, zero overruns, no fault/plugin failure. The QA project was stopped, saved and quit normally; foreground process 71196 exited 0. No commercial-plugin or strict display-performance pass is implied.


## Controls and automation-source recheck

The next signed private app uses source fingerprint
`5ce7bc2bc7d9d895808e8ad2b36e4eb84761b600fd6adf3f1b1ae8f44d9d973a`
and executable SHA256
`f9e1d9b6ac5ae4e74746865690ddf45039c59ad34857df47afa611d40da06827`.
Full build, 89/89 CTest, socket and copied AppKit interface checks passed.
This checkpoint precedes discrete recipe modulation, incremental graph drawing,
source auto-reveal, and the subsequent manual-field refresh repair.

| Recheck | Observed actions | Switches | Extra confirms | Result |
|---|---:|---:|---:|---|
| Numeric parameter Undo | Edit 100→120 Hz, Return, ⌘Z | 0 | 0 | Backend returns to 100 Hz while playback continues, but the field still displays 120. Source repair and interface regression passed afterward; rebuilt native recheck remains required. |
| Range endpoint | Drag one source endpoint; ⌘Z | 0 | 0 | One continuous drag changes one range; one Undo restores it. |
| Two ranges on one target | One endpoint drag | 0 | 0 | Slow sweep maximum changes to 0.196721; Fast motion remains −0.01…+0.01. See controls-two-ranges-recheck.json. |
| Existing envelope → editor → Back | From selected parameter: menu, Show sources; More/Find/type/Return to locate offscreen card; Edit source; Back | 2 | 0 | Exact EQ5 Band 1 frequency, pattern 0 and two points at rows 16/32; transport stays active. Required extra source navigation is a discovered friction issue, fixed in later source but not certified by this run. |
| Existing recorded points → editor → Back | Same source discovery; Edit source; Back | 2 | 0 | Exact parameter 11, two recorded points at 1/2 seconds. Playback continues. Raw UUID/name fallback and misleading stop-on-edit help are repaired in subsequent source. |
| Pattern FX source → cell → details → Back | Same source discovery; Edit source, Return to sole command, Return to details, close, Back | 2 plus details window | 0 | Exact pattern 0, row 16, channel 1, FX 2. Details show PL, binding 255, EQ5 Band 1 Q, target 40%, duration 16 rows. No edit performed. |

The envelope, recorded and pattern sources were seeded through the guarded API
as fixture setup, not counted as UI creation journeys. An attempted recorded-point
edit during playback was correctly rejected without mutation; setup then explicitly
stopped the private transport, added the points and resumed. Recorded-point edits
remain a stopped-transport capability, not a live-edit pass.

Final stopped transport sample records 13,421,056 rendered frames, zero overruns,
no host fault or plugin failure (controls-final-transport.json). The project was
saved and the private app quit normally, exit 0. Sources and first observed failures
remain recorded rather than overwritten by later test outcomes.

## Polish checkpoint: visible history, source navigation and live detach

Frozen source `/tmp/screamseq-graph-polish-20261001` has fingerprint
`3416926cde21748d01565b0309ad9508a2a279a4292c04a62708b525df201956`.
The uniquely signed visible QA executable has SHA256
`289af6724f35561d95b3365e1fa97086f766cd65f4ab8295a28385ba61049fbe`.
All 89 CTest checks and the actual socket suite passed, including the new
single-processor detach operation. Native compilation passed; packaging initially
failed because the archive lacks Git metadata. Its manifest was then generated
from the independently verified 368-file overlay manifest, and the completed
bundle passed deep/strict signature verification. See `polish-build.json`.

| Recheck | Observed actions after selecting the object | Switches | Extra confirms | Result |
|---|---|---:|---:|---|
| Visible numeric Undo | Click base, select all, type 120, Return, ⌘Z | 0 | 0 | Model and visible field both return to 100 Hz. This closes the stale-field failure. Initial check stopped. |
| Envelope source discovery | Parameter menu, Show existing sources | 0 | 0 | Source is selected and framed automatically; no separate Find operation. Text wraps. Edit source opens EQ5 Band 1 frequency in pattern 0; Back restores the graph. |
| Compact processor | H on selected EQ5 | 0 | 0 | Connected audio and modulation sockets remain reachable, live input/output meters retained. |
| Detach/heal and Undo | More, Detach and reconnect, ⌘Z | 0 | 0 | EQ5 is clocked silently while Hats feeds Compressor directly. Undo restores EQ5 before Compressor; sidechain and modulation survive. Playback stays active, requested/rendered plans agree, zero overruns. |
| Recorded source bridge | Parameter menu, Show existing sources, Edit source | 1 | 0 | Named EQ5 / Band 1 gain, stable parameter ID 11 in tooltip, original two points. Playback continues. Footer accurately says stopped playback is required for point edits. |
| Sole pattern command bridge | Parameter menu, Show existing sources, Edit source | 1 | 0 | Jumps directly to pattern 0, row 16, channel 1, FX2 without a one-item chooser. Pattern cursor selects PL; status describes binding 255, 40% and 16-row duration. |
| Duplicate instrument-copy search | More, Find node | 0 | 0 | Each Instrument character copy identifies channel and instrument, with current activity; no indistinguishable duplicate names. |

The source bridge walk revealed another visual correctness issue: retaining a
reference while changing the explicitly exposed parameter can remove its target
socket, causing a fallback endpoint. The stable source/editor target stays correct.
An exact-socket retention repair and regression were started after this checkpoint;
they are not implicitly covered by this executable.

Screenshots are prefixed `polish-` in `screenshots/`. BlackHole was selected only
inside this QA process, at 48 kHz / 512 frames. The final stopped transport sample
contains 17,880,576 rendered frames, zero overruns/faults, max callback 4,078 µs and
p99.9 3,670 µs. The project was saved and PID 97837 quit normally with exit 0.
`polish-detach-ui.json` retains topology and live transport before/after Undo.
These checks do not certify strict presentation timing or the later exact-route
observers/stage-container source changes.


## Observation checkpoint

Coherent production source manifest `cd5bde1e8db02b985a4483784dc8c1597666551f10a15cd64558a91d600485ed` built successfully, passed **89/89 CTest** (55.95 s) and the actual private socket suite. All 845 editor/mac/windows source hashes matched before and after compilation. `observation-build.json` records the copied signed bundle (executable `ac3a1d3f8726c2b0fe482cac381a265c5f5ed56df5e462cbd69e69cea4f2c530`).

The native floating graph visibly separates the Pulse channel's **Row**, **Persistent** and **Ordinary** processing stages. During playback, it displays the audible order and dims inactive copies while preserving positions. See `screenshots/observation-live-stages.png`. The private app used BlackHole 2ch / 512 frames and exited normally. A subsequent AX query reopened that unique QA bundle without inspection flags; it showed recovery choices, no restore was performed, and it was immediately quit. No musician process was touched.

An actual viewport defect was found: floating the docked graph then selecting Pulse could leave all nodes offscreen until Home/Fit. This is an unresolved observation for this executable; the next source checkpoint adds a bounded empty-viewport recovery. Source-socket retention after changing exposed parameters, discrete recipe-mode UI, and exact cable scope/listen are still awaiting native walkthrough on a newer build.


## Follower checkpoint: shared bypass and audio-to-parameter conversion

The coherent executable `7309e4b13947c4ad1a7cad65e4b0cab0b5ac5ccda9f3d4cef4279e00c1fcd91c` passed the full build, **89/89 CTest** (57.58 s), and actual socket suite, including guarded atomic follower creation. The copied AppKit interface suite passed. `follower-build.json` embeds source fingerprint `33abffce47cad57ce364717de513884b75124ac999188a435aec19ba743fa601`; only Mac/Windows API documentation changed during the compile interval, with no compiled source changes. Later latency-warmup and keyboard-port changes are not included.

| Recheck | Observed actions | Switches | Extra confirms | Result |
|---|---|---:|---:|---|
| Empty floating viewport recovery | Float graph, filter Pulse | 0 | 0 | Relevant stage cards remain visible at working zoom without Home/Fit. |
| Reusable processor bypass and Undo | Enter Instrument character, select trim, M, ⌘Z | 1 depth | 0 | Shared recipe bypass changes then restores; −9 dB manual baseline retained, playback continues, zero overruns. |
| Audio → continuous parameter | Expose Gain, drag Input output to Gain, Return | 0 | 0 beyond explicit converter choice | While playing, edit rejects visibly and preserves transport/topology. Stopped retry creates follower + input tap + zero-depth modulation in one transaction, with −9 dB-derived base 0.725. |
| Follower Undo | ⌘Z in freshly focused untouched maximum field; click canvas, ⌘Z | 0 | 0 | First shortcut is swallowed by field editor (new regression). Canvas Undo removes follower and both wires in one history step. Fix is subsequent source. |
| Audio → discrete parameter | Expose Left polarity, drag Input output to parameter, Return, Return | 0 | Explicit discrete-mode choice | Chooser identifies two values; selected connection displays checked, disabled “Discrete values (required)” with summation/rounding explanation. API retains quantized=true and zero depth. |

Screenshots use `follower-` prefixes. The private app used BlackHole 2ch at 48 kHz/512. Stopped telemetry records 5,746,176 frames, zero overruns/faults, maximum callback 4,343.916 µs and p99.9 3,840 µs. The discrete follower was undone from canvas, the private fixture saved, and PID 14131 quit normally. Exact-cable scope/listen, native parameter-socket retention after switching exposure, and reopen of collapsed cards remain to be walked. These results do not certify seamless recipe topology edits or strict presentation performance.


## Ports checkpoint: keyboard patching, untouched-field Undo and exact monitoring

This is a **UI-only** checkpoint: frozen Swift/header snapshot
`/var/folders/_l/wc099tqn2kvfk697dlrd02hr0000gn/T/screamseq-port-actions-final-4b3imsro`
linked against independently copied Follower audio archives. Composite fingerprint
`865bff3c9c3e95e3a2407dacf6a7364d59236712e2bcc841aab7895fae48dbca`,
signed executable SHA256 `40aec7edb3937a6a38f48cd26be388e962e94a8dd8cabcab750299dbb75b82d1`.
The optimized UI build, copied AppKit suite and deep/strict signature checks passed.
Later prepared reusable-source runtime, latency warmup, cable-preview diagnostics,
human socket labels and Main-output gain fixes are not in this executable.

| Recheck | Observed actions | Switches | Extra confirms | Result |
|---|---|---:|---:|---|
| Keyboard socket target | Sub output context menu → Connect to → search Band 1 frequency → Return → Insert follower → Return | 0 | Explicit converter choice | Named compressor Detector/Main ports were separately offered. Choosing EQ parameter 10 created one song follower with zero initial depth while playback continued. Raw UUID-heavy labels were a presentation problem; subsequent source humanizes them. |
| Undo from untouched numeric field | ⌘Z in the newly auto-focused maximum field | 0 | 0 | Removes the follower, input tap and modulation together; transport continues. This closes the swallowed document-Undo failure. |
| Exact selected-send scope | Select Sub→Return Send, More→Scope selected signal | 0 | 0 | Waveform names the send, captures `route/send/2:n33:n470:/0/0` after its −12 dB gain, 4,096 frames / 85.3 ms; zero dropped/invalid capture samples. |
| Exact selected-send Listen | L with canvas focused; Stop listening | 0 | 0 | Persistent banner identifies Sub→Return send. API confirms the exact route; Stop listening returns to normal monitoring without changing song history. |
| Last-touched target and Back | Native frequency edit 100→120 Hz; enter Instrument character; ⌘K search Show last touched/Return; ⌘K search Back from last touched/Return | 2 graph depths | 0 | Resolves EQ5 stable plugin identity/parameter 10, exposes and frames the correct socket; Back restores recipe depth and view. Native walk found stale Sub filter text after clearing the filter and a hidden inspector; both have a subsequent source regression/fix. |
| Compact-card reopen | Open saved private fixture | 0 | 0 | EQ5 remains collapsed with connected sockets and live meters. |

An initial scope attempt cleared selection with an extra Escape and correctly opened
a signal chooser; it is not counted as a successful selected-cable interaction.
The later exact-scope and Listen captures show the actual requested route.
Screenshots use the `ports-` prefix; API evidence is in `ports-*-ui.json`.

The test parameter was undone to its 100 Hz manual base. Listen was stopped, the
private fixture saved, transport stopped, and PID 22312 quit normally with exit 0.
Final telemetry records **37,279,744 frames, zero overruns/faults**, maximum callback
7,218 µs and p99.9 2,970 µs at BlackHole 2ch / 48 kHz / 512 frames. Compilations ran
concurrently: these are functional observations, not an idle performance pass.
Keyboard sidechain commit, exact source-socket retention after switching exposed
parameters, and the later live reusable-source changes still need native rewalks.
