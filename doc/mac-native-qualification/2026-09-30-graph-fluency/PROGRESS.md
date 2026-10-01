# Graph interaction qualification — work in progress

This is an intermediate measurement record, not release qualification. The approved specification is `doc/SCREAMSEQ_GRAPH_INTERACTION_SPEC_2026-09-24.md`. Space retains transport, Tab retains navigation, and shared definition edits affect every use.

The baseline is a separately identified copy of the prior application. The fixture has 16 channels; its initial audible material is the four-channel demo, so it is **not** a 16-channel audio stress test. The private QA processes use BlackHole 2ch explicitly at 48 kHz / 512 frames. The musician's process and system default audio devices are untouched. Bundle identities and hashes are in the adjacent JSON records. The baseline PID was relaunched from 32258 to 39535 after an observed non-finite-meter crash; the executable was unchanged.

## Journey checkpoint

Counts come from `actions-before.json` and `actions-after.json`. A shortcut, text entry, click, double-click or drag each counts as one action. A Return that accepts a search or finishes exact-value entry is not a redundant Apply confirmation. Starting selection and working zoom are setup, not hidden actions within the journey. Where a baseline did not reach the requested end state, it is labelled blocked and is not treated as an action-count improvement.

| Journey | Before actions / panel switches / redundant confirms | Interim after actions / panel switches / redundant confirms | Observations / remaining verification |
|---|---|---|---|
| G01 Add reverb to selected channel 3 | 4 / 3 / 0 | 3 / 0 / 0 | Shift+A, search, Return; effect lands on captured channel. |
| G02 Add EQ between visible effects | 5 / 3 / 0 | 4 / 0 / 0 | Selected cable provides exact insertion point. |
| G03 Route kick to compressor detector | 8 / 0 / 0 | 1 / 0 / 0 | Baseline first drag hit Main; Undo, zoom and retry are included. The intended baseline was one drag. New nearest-socket hit testing reached Detector directly. |
| G04 Add LFO to cutoff and set depth | 9 / 0 / 0 | 4 / 0 / 0 | Socket → blank, type LFO, Return, depth-badge drag passed. Structural Add still stops playback; subsequent depth Undo/Redo is live. Screenshot 52; `logs/wire-gestures-ui.json`. |
| G05 Create send/return and raise send | 9 / 4 / 1 | 4 / 0 / 0 | Native re-walk passed with visible badge and retained numeric focus (stopped). Live bus creation passes rendered regression checks; the final native playback re-walk was interrupted by computer-control timeouts. Screenshot 55; `logs/send-focus-ui.json`. |
| G06 Trace a silent path | Blocked; 3 search actions | 3 / 0 / 0 | Starting with muted Track 2 selected: More, Trace silence, Mute toggle. Measured output returned during uninterrupted playback. See actions-after-scope.json and screenshots 06. |
| G07 Locate an overload | Blocked; 3 search actions | 7 / 0 / 0 | New diagnostic frames the measured Master output and exposes its level. Actual exact entry 24 → 0 passed; count includes one CUA slider drag with no value change, then exact entry. The intended exact-entry path is 6 actions. See actions-after-controls.json. |
| G08 Package selected chain for reuse | Blocked; 3 search actions | 6 / 0 / 0 | Song-rack grouping, direct naming and independent export passed during 16-voice playback; native Undo/Redo preserved the original rack/mixer. Screenshots 44–45 and `logs/song-group-direct-name-ui.json`. Earlier recipe-only and retry measurements remain below. |
| G09 Enter instrument graph, edit and return | 15 / 2 / 0 | 7 / 0 / 0 | From visible instrument copy: double-click, select filter, search Frequency, drag, More → Show in pattern. Channel 3 was restored; native Undo/Redo passed while 16 voices played. Baseline began with instrument navigation hidden, so start-state difference is explicit. Screenshots 48–49; `logs/instrument-primary-ui.json`. |
| G10 Repatch, compare bypass and Undo during playback | Blocked after 1 drag | 7 / 0 / 0 after cable selection; partial | Downstream reroute, two bypass comparisons and three chronological Undo steps passed with retained processors/graph copies. Cable selection required a popup retry. General changed-input repatching remains incomplete. |

Before/after PNGs retain the visible intermediate state for G01–G05. Baseline G06–G10 screenshots document the unavailable actions and stopped playback. `after/11-rack-parameter-controls.png` documents the inline inspector layout, not a passed drag test.

## Additional implementation checks

- Atomic recipe insert/detach/heal and cable cut: shared/session regressions passed. Serial-path ambiguity is rejected without changing the song. Song-level detach remains unfinished.
- Rack parameters now use stable plugin IDs and a validated live batch queue. Native history tests passed for a batch, invalid-batch rejection, no-op, gesture coalescing, Undo/Redo and stable identity after rack movement.
- Actual QA UI exact Drive entry changed 6 → 12 during uninterrupted playback; the socket read confirmed 12 and advancing callbacks with no fault. Slider click also changed the value. CUA slider drags did not change it; this remains unresolved rather than a passed gesture claim.
- AppKit tests passed for retained targets, queued distinct parameters, cable amount commit/cancel and retired inspector replies at the preceding checkpoint. Additional stale-read/Undo/conflict tests passed. Framing now has a far-boundary-node regression and an actual-UI screenshot.
- Mixer source capture is now separate from bus evaluation order. Native mixer, rack and signal-routing rendered regressions passed, including the existing callback allocation/free/lock audit. This is preparatory work; it does **not** establish live topology replacement.
- A signed-exponent error in meter decay produced infinity in the baseline. The fix and non-finite JSON response guard have targeted regression coverage.
- Windows source/schema counterparts were updated where applicable; Windows tests have not been executed on this Mac.

## Still required before completion

Finish all four approved slices, including live prepared-plan transitions and their latency/state tests, groups/frames/reroutes, signal diagnostics/scopes/listening, song-level modulation and provenance, complete command registration, actual UI re-walks and final performance qualification. The previous roughly 54 fps result remains a failure of the 60 fps requirement; no new presentation pass is claimed here.

## Signal observation and inline controls checkpoint

- Prepared host channel/rack ports publish finite peak/RMS, sample clocks, overload latches and latency metadata. Known-level, silent, non-finite and clear cases passed with the callback allocation/free/lock audit. Internal recipe ports are not yet observed.
- Actual overload navigation initially clipped Master. After the viewport fix, `after/07-overload-framed-inline.png` shows the complete node beside its level controls. `after/07-overload-corrected-inline.png` records the correction. `logs/inline-level-socket.json` confirms Master gain 0, playing transport and no recorded overruns at that checkpoint (48 kHz/512 frames, four audible demo channels). This is not full performance qualification.
- Clearing the corrected clip latch in the prior Graph Meters QA instance left no clipped host ports while playback continued.
- Inline bus controls retain the old target through gesture completion, carry the accepted revision into deferred selection and restore current document controls after a rejected preview. Focused AppKit tests passed. A committed text field briefly retained the “Draft held” caption; the source correction uses actual text dirtiness and awaits the next UI build check.
- New full-rate single-tap scope capture passed known-frequency/amplitude, opposite-polarity stereo, explicit silent frames, drop accounting, discontinuous-clock history and capture-stop tests. Hann FFT and min/max display reduction run off the callback. API and UI integration are still being qualified.

- Scope-enabled QA (`after-scope.json`) displayed the actual Track 2 waveform and spectrum during playback. `logs/scope-live-socket.json` confirms 4096 contiguous frames, 256 waveform buckets, 2049 spectrum bins and an unmuted channel. `after/12-live-waveform.png` and `after/13-live-spectrum.png` record the UI. Closing cleared its port and history; a real Q keypress advanced capture generation 2 → 4 and finished with no selected tap.
- Opening a native menu paused the default-mode UI timer and overflowed the optional capture ring. Drops were reported, the subsequent waveform restarted at a contiguous interval, and audio continued. Source now uses a common-mode timer; actual menu-open requalification is pending. Low-level waveform scaling and human-readable tap names are also being rechecked in the next build.
- The full current socket suite passed with the scope methods, revision/type/unknown-port checks and no song changes from capture. An earlier socket invocation against the previous automation host failed the schema catalogue check; it was rerun only after rebuilding that host.
- Obsolete private QA processes were retired to avoid contaminating later performance measurements; their bundles/screenshots remain. Exact PIDs are recorded in `retired-qa-processes.json`. The musician's application was not closed.

## Scope continuity and temporary listening checkpoint

- The common-mode timer passed the actual native-menu hold check in Graph Flow:
  capture continued with zero dropped frames, playback active and zero overruns.
  `logs/scope-menu-continuity.json` records it; `after/14-live-waveform-scaled.png`
  shows the readable auto-scaled waveform and human tap name.
- `SignalListen` adds a temporary host-output monitor, independent of song buses,
  save data and Undo. Fixed buffers collect two taps during a linear 5 ms fade;
  rapid selections finish the current fade and consume the latest request.
  Sample-clock ramps are identical at callback sizes 64, 511 and 4096 across
  44.1/48/96 kHz. The audio audit passed allocation/free/lock checks.
- Actual rendered native playback at blocks 17/512/4096 keeps a downstream
  compressor and detector processing during channel listening. Restoring the
  normal mix matches an untouched reference exactly, including processor state.
  Native mixer, mixer-runtime and signal-graph-session tests passed (3/3).
- New `graph.listen.get/set` operations are in the schema, guide and both platform
  API adapters. Current full socket tests passed validation, stale-revision,
  unknown/input-port rejection, gain bounds and unchanged document/history cases.
  Windows source is updated but not compiled/tested on this Mac.
- AppKit tests passed queued Stop, stale revision recovery, external API selection
  visibility, and independent gain. The actual Graph Listen build (`after-listen.json`)
  passed L-to-listen, exact −6 dB monitor gain, closing the graph while retaining
  the main-window indicator, and one-click restore from the pattern workspace.
  `logs/listen-live-socket.json` and `logs/listen-restored-socket.json` show unchanged
  song revision, advancing audio, no plugin fault and zero observed overruns.
  Screenshots: `after/15-listen-channel.png`, `after/16-listen-pattern-indicator.png`.
- The first headphone symbols were too dark; the source now caches explicitly
  tinted symbols. That visual correction awaits the next actual build check.
  Current scope/listening ports are host mixer/rack ports, not recipe internals.
  Latency-aligned topology transitions and the final 60 fps gate remain unfinished.
- Graph Scope (59067) and Graph Flow (61819) were stopped and retired after the
  newer Graph Listen process was launched. Graph Listen (65379) is stopped with
  no monitor override; the musician's app/device defaults were untouched.


## Continuous bypass checkpoint

- Prepared host bypass uses a 5 ms linear fade to latency-aligned dry audio;
  instruments/auxiliary outputs fade to silence. Processors, sidechains,
  automation and MIDI continue to run. New scratch/delay storage is counted in
  the graph/rack 256 MiB budget. The zero-latency enabled path skips unnecessary
  bypass scratch writes.
- Mixer/native-mixer, AU factory-program/instrument, latency, rack, multi-bus,
  session and graph-session regressions passed (8/8 at the first checkpoint).
  Rendered reference tests include 17/512/4096-frame callbacks, 44.1/48/96 kHz,
  exact latency-aligned gain and retained downstream compressor state. Instrument
  tests verify a held note survives bypass and a note-off while bypassed does not
  reappear on restore. Callback allocation/free/lock audits pass.
- `after-bypass.json` identifies the separately launched Graph Bypass QA app,
  PID 70266. Actual M, Undo, Redo and right-click Toggle bypass all succeeded
  during BlackHole playback. The distortion retained 90 samples of latency,
  callback counts advanced without resetting, and 0 overruns/faults were
  observed. Evidence is in `logs/bypass-*-live.json` and
  `after/17-live-plugin-bypass.png`. This is a bypass comparison, **not** the
  complete G10 live-repatch journey.
- Fresh socket tests and AppKit tests passed. An initial socket invocation used
  an older automation host while `build.sh` was still compiling Swift; its no-op
  failure is superseded by `/tmp/screamseq-bypass-socket-current.log`.
- The headphone icon contrast fix is visible in the actual Graph Bypass app.
  The first selected-parameter read hit a transient document-busy error; source
  now retries bounded reads with target/generation guards. The host checkbox is
  now labelled Bypass to distinguish it from a plugin's own Enabled parameter.
  These latest UI refinements await the next actual build check.
- Bypass history now explicitly records its stable target as a delta. This keeps
  save/state-capture consolidation of manual parameters from turning bypass Undo
  into a processor rebuild. Requalification of this save/Undo case and interrupted
  VST3 fades is in progress.
- Windows session/playback callbacks mirror live bypass and bypass-only history;
  no Windows-native build/run is claimed. Live topology publication, nested
  groups, song-wide modulation and the final 60 fps gate remain incomplete.

## Live-edit preparation and command-discovery checkpoint

- The refined bypass build passed the complete socket and AppKit suites. Actual
  save → Undo → Redo retained Drive = 9, advanced callbacks without reset, kept
  playback active and reported zero observed overruns/faults. See
  `logs/bypass-save-undo-live.json` and `after/18-live-bypass-refined.png`.
- `RealtimeTransition` now retains outgoing/current/pending plans separately.
  The render consumer acknowledges retirement only after its final use; the
  control producer alone reclaims storage. A 100,000-revision concurrent test
  passes the allocation/free/lock audit and ThreadSanitizer. Stale preparations
  retain caller ownership and cannot replace a newer pending revision.
- Shared mixer dependency comparison uses exact interned expressions rather
  than collision-prone hashes. It accounts for main/sidechain sources, summing
  order, gain, mute, pre/post taps and delay compensation; instrument outputs
  retain stable source identity when destinations change. Targeted tests pass
  reordered processors, sidechains, latency, explicit resets and unrelated paths.
- These are preparation components, **not enabled live topology replacement**.
  The executor still needs retained delay/fader histories, one render of shared
  processors, separate prepared affected copies, warm-up/latency transitions and
  host/UI/API publication integration. Structural edits still stop playback.
- Command-discovery source changes in progress: stable graph command IDs and
  target search for bypass, plugin interfaces, groups, modulation sources and
  observed listening/scope ports. Native verification is pending.

- Final command-discovery UI checks passed in `after-commands-cold.json`
  (PID 80223). The first actual walk exposed an unloaded hidden graph and a
  duplicate bypass command; both were fixed, covered by regressions, rebuilt
  and re-walked. Explicit command IDs avoid AppKit's automatic selector IDs.
- Actual cold-start ⌘K → bypass → target search → filter → Escape retained
  revision `0:0:0` and returned focus to the pattern. A configured modified
  shortcut opened the same picker; target selection and Undo during BlackHole
  playback advanced callbacks without restarting, with zero observed overruns
  or faults. The LFO command offered an existing shared group or a new group;
  cancel preserved the song. See `actions-after-commands.json`,
  `logs/graph-commands-live.json` and screenshots 19–20.
- This is a partial command registry: the main graph action menu now has stable
  IDs, but remaining object/port/frame actions and all target bridges still need
  completion. Existing custom bindings under former menu-path IDs should be
  migrated before final delivery. Recipe scopes and song-wide modulation remain
  unavailable. Full live topology execution is still not implemented.

## Retained mixer history and dense-fixture checkpoint

- Shared processor rendering now has AU/VST3 reference tests: same main audio,
  auxiliary buses and detector inputs across two plans, one vendor DSP advance,
  44.1/48/96 kHz and 17/128/512/4096-frame blocks. Delayed VST3 output remains
  exact after the outgoing plan stops rendering. Logs: `screamseq-render-once-*`.
- `MixerTransitionReuse` extends the exact dependency classifier to direct,
  instrument, edge and sidechain delay histories and smoothed bus controls.
  Prepared runtimes can retain compatible delay storage; a per-chunk cache keeps
  it from advancing twice. Ramp records transfer at the render boundary without
  copying large delay buffers or changing ownership on the audio thread.
- New `mixer-transition` tests compare both plans against uninterrupted PCM,
  including active fader/pre-pan ramps, nonzero PDC, a detector with two sources,
  an auxiliary output and processor retirement. All samples match exactly at
  44.1/48/96 kHz and 1/17/512/4096 frames. Concurrent prepare/cancel passes TSAN;
  the same suite passes ASAN/UBSAN. Invalid mappings fail before changing history.
  All seven targeted mixer, native-mixer, multi-bus and latency tests pass.
- Delay-cache storage is included in the mixer compensation budget. The full
  live executor still must budget combined pending/old/new plans and prepare
  affected processor copies, warm/fade differing latencies, dispatch controls
  and MIDI, and publish model/Undo revisions. Structural editing still stops;
  the retained-history work alone does **not** enable live cable edits.
- `after-history.json` identifies the actual rebuilt QA bundle, PID 85720.
  `dense-graph-fixture.screamseq` has 16 sounding channels, two sample instruments
  used by the pattern, two returns, one group bus, a sidechain, ordinary and
  sample-instrument graphs, two overlapping LFOs, and row/persistent stacks.
  Its hash and explicit 48 kHz / 512-frame BlackHole route are in
  `dense-fixture.json`; preparation operations are retained in `logs/`.
- Actual playback showed all 16 nonzero track meters and 16 voices, with 27,644
  advancing callbacks and zero observed overruns/faults at the checkpoint.
  This is bounded live audio evidence, **not** the strict presentation gate.
- The dense walk found ambiguous duplicate filter names and an oversized sparse
  filtered view (`before/21-dense-filter-spacing.png`). Source now disambiguates
  duplicate names, keeps separate provisional layouts for channel filters,
  preserves explicit musician positions, and restores default placement on Undo.
  A new song clears coincidentally matching provisional IDs. AppKit tests pass;
  the final native re-walk of these layout changes is pending the next build.
- A deferred graph command is cancelled if the preceding mutation fails, so it
  cannot unexpectedly run after an unrelated later refresh. AppKit coverage passes.
- The older Graph Commands Cold QA app (80223) was stopped and retired. The
  musician's process and system audio defaults remain untouched.

- The final native layout re-walk passed in `after-history-layout.json`, PID
  88193: duplicate channel names are distinguished in the filter and canvas;
  filtering Track 4 followed by Home fits all 12 relevant nodes on screen.
  Screenshots 21–22 record the result. One AX canvas click failed because its
  document centre was offscreen; a visible canvas click plus Home completed it.
  Fit is an overview; exact port/role details still need zooming in.
- `logs/history-layout-live.json` records 16 voices, 9,590 callbacks, zero
  overruns/faults and no song revision changes from filtering/floating/Fit.
  The older build's longer checkpoint covers 281.408 seconds between reads,
  max callback 5,022.208 µs at a 10,666.667 µs buffer period, zero overruns.
- Both current AppKit and full application builds completed successfully. The
  Graph History process (85720) was stopped from its graph with Space and
  retired. Graph History Layout (88193) is the current private test instance.
  These observations do not complete G10 or the strict presentation gate.

## Native live-routing executor checkpoint

- `editor/MixerTransition.*` now owns prepared old/new mixers and evaluates them
  on one chunk clock. It warms incoming latency history from actual sources,
  applies a 10 ms linear fade, and retires plans on the control thread. The
  combined host audio/control/cache budget is checked before publication, with
  overflow-safe rejection. Failed candidates retain the old plan and do not
  acknowledge the failed revision as rendered. Fixed source mappings survive
  graph evaluation order. A quiescent-only handoff supports the existing stopped
  dynamic-latency refresh path.
- The actual shared `PluginChain` now uses this executor and `RenderOnce` wrappers
  for normal native mixer playback. Instrument sources still run once; their
  audio is distributed to both plans. Recipe instances use the same prepared
  clock/cache path. Only the incoming plan emits duplicated-node observations,
  avoiding two scope blocks per chunk. The native pipeline tests pass.
- Mac mixer API and document Undo/Redo enable a **conservative subset** of live
  edits: same bus identities/order, same source adapters, unchanged processor
  dependency expressions and unchanged total latency. A dry track can reroute
  through another dry track while an unaffected VST3 remains running. Preparation
  precedes document mutation; publication allocates nothing. Busy edits reject
  without changing the revision. Changed plugin inputs, new buses, recipe copies,
  auxiliary activation and latency changes still use stopped preparation.
- New `mixer-publication` tests cover continuous reference PCM, sample-clock
  fades, nonzero latency warmup, cancellation/failure, source mapping, storage
  overflow, stopped handoffs and 100 concurrent publications. They pass at
  44.1/48/96 kHz with 1/17/512/4096-frame test chunks. TSAN and ASAN/UBSAN pass.
  `native-mixer` compares a real VST3 host reroute and its reverse against two
  continuously running references at 17/512/4096 frames, with zero audited host
  allocations/frees/locks and unchanged source transport. Twelve relevant CTest
  cases pass, including native recipes, multi-bus, latency and graph sessions.
- Full app build passes. `after-live-router.json` identifies PID 94492 and the
  isolated `ScreamSeq Live Router.app` bundle. During actual UI playback on
  explicit BlackHole 2ch (48 kHz, 512 frames), Track 1's destination handle was
  dragged from Master to Track 2. Playback kept advancing through the drag,
  Cmd-Z, Cmd-Shift-Z and Save, with zero observed overruns. A feedback-cycle
  attempt rejected without a revision change or stopping playback. Reopen
  preserved the routing. Screenshots 23–24 and `logs/live-routing-ui.json`
  record the checks; the gesture required one cable click and one drag, no
  confirm or panel switch. This is a checkpoint, not a replacement for all ten
  journey measurements.
- The same rebuilt host plays the dense fixture with 16 voices and 16 nonzero
  track meters. The second read reached 14,899 callbacks with zero overruns or
  faults, max 4,455.291 µs and p99.9 1,930 µs versus a 10,666.667 µs period.
  Reads span 117.013 seconds. Screenshot 25 shows the Track 4 filtered path.
  The prior History Layout app (88193) was stopped and retired; only the new
  QA app remains. Device defaults and musician sessions were not changed.
- General live repatching remains unfinished: affected processor copies need
  their own automation/MIDI and custom-editor ownership; reusable graph copies
  need note-membership/command-state preparation; latency changes need aligned
  output transitions. The current warmup is conservative (whole plan latency),
  and can be shortened when every nonzero delay/processor history is retained.
  Exposing requested/rendered/pending state in UI/API also remains. Windows
  build sources include the shared executor, but its API integration was not
  enabled or run on this Mac. Strict 60 fps, remaining graph/model/UI slices and
  the complete journey/report deliverables are still open.

## Dense instance identity and document context

- Subgraph role labels now remain visible at semantic overview zoom. Row,
  Persistent, Ordinary, and Instrument I1 can be distinguished without opening
  identical library cards. Accessibility descriptions include the role.
- Opening another song clears graph/channel selection, breadcrumbs, filters,
  provisional views, gestures and cached port exposure. Draft envelope requests
  from the previous song cannot update the new inspector. This prevents stable
  IDs that happen to match in two documents from silently carrying context.
- Full app and AppKit interface builds pass. The isolated Graph Roles bundle
  (PID 98093; `after-roles.json`) was checked on the actual native desktop.
  Screenshot 26 shows all twelve Track 4 path nodes fitted with role labels;
  screenshot 27 verifies that opening a different song restores All channels
  and a ten-node song graph. No musical edit was needed for either view check.
- The stale Mixer footer claiming that every routing edit stops playback was
  removed. The conservative live-routing limits above remain unchanged.

## Dense presentation diagnostic (not passed)

- Added an opt-in existing-graph qualification workload. It loops a supplied
  16+ channel song, exercises note editing/Undo/save, retains its routing and
  plugins, and records the number of nonzero track meters. The old generated
  graph setup now finds its own definition by ID instead of assuming library[0].
- First 60-second run (`logs/dense-performance-first.json`) failed: 43.85 mean
  presented fps, 892 missed periods, 270.83 ms maximum presentation interval,
  GPU p99 5.00 ms. Host geometry CPU p99 was 0.135 ms; audio had zero overruns,
  p99.9 2.11 ms at 48 kHz / 512 frames, and 16 nonzero track meters.
- Screenshot 28 exposed a setup/interaction defect: a dock could report its
  graph selected/visible while the actual lower pane remained hidden. Thus this
  result is not a completed graph-canvas benchmark. The current source fixes
  selecting a hidden docking destination and adds its regression check; a rerun
  is required. The strict presentation thresholds have not been weakened.

## Routing status and refresh profiling

- `graph.signal.get` and Mac `transport.get` now distinguish requested, rendered
  and failed routing plans. The graph displays pending/failure feedback, and
  pending latency separately. Counters belong to a prepared playback engine,
  not document history. Windows graph telemetry has matching source fields;
  Windows live-edit integration is still not enabled or verified here.
- Publication tests now also reorder source buses while comparing the audible
  result to an uninterrupted reference across callback sizes/rates. Targeted
  mixer/publication tests and the publication ThreadSanitizer run pass.
- The corrected visible-dock 90-second diagnostic still failed presentation:
  42.70 mean presented fps, 1,438 missed periods, 275 ms maximum interval, and
  two audio overruns. A three-second sampling profiler ran during this test;
  this is diagnostic evidence, not an idle qualification result. The actual
  graph is visible in screenshot 29; sixteen track meters remained nonzero.
- Profiling found repeated AppKit display/layout work, graph popup/breadcrumb
  reconstruction after unrelated note edits, and precise-note command menu
  reconstruction on cursor movement. Source now preserves unchanged graph
  views and command menus and only changes focus borders when focus changes.
  Revision guards still advance and actual graph changes still rebuild. The
  AppKit regression suite passes. A fader refresh shortcut was rejected by
  conflict-recovery coverage and removed before qualification.
- The refreshed build passed the full socket suite, including stopped routing
  status shape and unchanged history. Its 60-second run improved to 52.25
  presented fps with zero audio overruns and sixteen sounding channels, but
  still failed the strict frame/presentation/GPU gates. Screenshot 30 exposed
  an inadequately short canvas and a Fit request lost while the initial graph
  load was pending; it is not a satisfactory dense-canvas test.
- The next workload gives the graph a larger dock, waits for its initial read,
  then fits it before measuring. It requires at least 180 points of viewport
  height and eight displayed cards, and reports both values. Animated meter,
  graph, command-lane and precise-note canvases now have separate backing
  layers so their redraws do not share their surrounding control surface.
- Quiet run with the enlarged dock: 51.57 presented fps, 75 ms worst interval,
  nine displayed cards in a 202-point canvas, sixteen sounding channels, zero
  audio overruns. The next profile identified trace-history rasterization on
  every moving playhead and numerous small Core Graphics stroke operations.
- Trace playheads now use independent shape layers. Trace strokes are batched
  by source style; graph grid dots are drawn as one path. Unchanged captured-pass
  menus and source lists remain mounted. AppKit coverage verifies playhead
  positioning at zoom, out-of-view clipping and no history repaint for playhead
  movement. Actual visual verification of these cursor layers remains pending.
- The trace build's quiet run reached 54.02 presented fps, a 50 ms worst
  interval and zero audio overruns. It still fails deadline and GPU gates.
  `dense-trace-build.json` and `logs/dense-performance-trace.json` identify it.
- Synchronous precise-note captures no longer disable/re-enable every native
  control in one call stack. Asynchronous reads still disable the prior target;
  a cached catalogue still resets an empty row to no effect. Regressions pass
  for the asynchronous guard; the added blank-row regression is running with
  the latest build. Display-link lead, render-queue wait and submission deadline
  measurements are being added; they do not relax the existing pass criteria.

## Native trace verification and remaining frame investigation

- Latest AppKit regressions pass, including the cached catalogue's blank-row
  reset. Full app build and actual socket suite pass. Screenshots 31 and 32
  verify the blue editing cursor, yellow moving playback cursor, zoom alignment
  and hiding cursors outside the visible interval in a native floating panel.
- Both live API endpoints report the same stable routing plan during 16-voice
  playback (`logs/routing-status-live.json`); the QA app explicitly uses
  BlackHole 2ch at 48 kHz / 512 frames. Space stopped playback from the graph.
- Quiet deadline-instrumented test: 53.71 presented fps, 50 ms worst interval,
  nine visible cards / 202-point canvas, sixteen sounding channels, zero audio
  overruns. Every measured submission preceded its Metal display-link deadline:
  slow-end p99 lateness −11.52 ms, p99 render queue wait 0.155 ms. This still
  **fails** the strict presentation/GPU criteria. On-time submissions alone do
  not rule out dropped display-link callbacks; cadence and buffer-starvation
  counters are being added to distinguish that case.
- Native overview inspection still exposes an unsatisfactory short default
  bottom dock and a Fit view that clips the tall graph at minimum zoom. These
  are open layout issues; a successful enlarged test harness is not proof that
  the default workspace is usable.

## Shortcut consistency and nested boundaries

- Graph canvas defaults yield to explicit shortcut removal, remapping or command
  sequences. Reset restores shipped unmodified defaults. Remapped graph actions
  only consume keys when the graph canvas owns focus; text fields and other
  panels retain their bindings. Exact historical graph menu-path preferences
  migrate to stable IDs without guessing matches from other panels. AppKit
  regression coverage passes for all of these cases.
- Reusable definitions now store nested processing boundaries independently of
  flat DSP nodes, audio wires and modulation wires. Core validation rejects
  duplicate membership, missing members, cycles and mixed-depth packaging.
  Grouping preserves every original node/parameter identity and processor state;
  `sameSignalProcessing` excludes boundary metadata.
- New revision-guarded group create/update/remove API operations share document
  Undo, dry-run/no-op behavior and persistence. Mac session and shared model
  tests pass for nested grouping, rejection, one-step Undo/Redo, save/reopen and
  clone identity remapping. Windows metadata codecs mirror these fields; Windows
  execution has not been checked on this Mac.
- The Mac canvas projects collapsed group ports onto their real endpoints,
  exposes external dependencies when entered, remembers depth navigation, moves
  member positions as one edit, and supports Ctrl+G / Ctrl+Option+G. AppKit tests
  verify that boundary gain and modulation edits still target the original node
  and parameter, and Add targets the opened depth. Native playback UI and socket
  verification are the next checks.
- This grouping slice currently operates **inside reusable definitions**. Song
  rack packaging and export of a selected boundary into a new library definition
  remain unfinished; this is not yet a completed G08 journey.

### Nested processing-boundary native walk (QA PID 21744)

The full build, AppKit suite, eight graph/mixer/history CTests and actual private-socket suite passed before this walk. The new socket coverage exercises grouping, nested membership, deletion, revision guards, no-op/dry run and unified Undo. The actual native UI then packaged three processors/modulators in `Filter movement`, renamed the boundary `Animated filter`, entered it by double-click, packaged the LFO into `Primary motion` with Ctrl+G, entered the nested boundary, returned with Option+Up, ungrouped with Ctrl+Option+G and restored it with Cmd+Z. The parent viewport and selected nested card were restored. API observations after creation and Undo both show playback active, 16 voices, no fault, and stable routing plan 1 (requested/rendered), with zero failed plans. Screenshots 33–34 and `logs/groups-ui-after-{create,undo}.json` retain the evidence. Both manual QA processes (21744 and the previously stopped 15526) were retired before the next performance run.

Observed action ledger: enter library = More / choose Filter movement (2 clicks); package = marquee / Ctrl+G (first immediate attempt did not act) / More / Group selection / name entry / Return (1 drag, 2 clicks, 1 shortcut retry, text+commit); enter = one double-click; nested package = select LFO / Ctrl+G / name entry / Return; enter = double-click; parent = Option+Up; ungroup = Ctrl+Option+G; Undo = Cmd+Z. The first grouping shortcut was repeated separately later and worked; batch-after-drag timing is retained as an observed retry, not omitted from the count. These are partial G08 measurements: export and song-rack boundaries still require separate walks.

### Dense performance, button-layer and display-link cadence instrumentation

`logs/dense-performance-groups.json` and `dense-groups-build.json` retain the 60-second run from PID 22858. Strict qualification **failed**: 53.619 mean presented fps, 3,215 presented frames, p99 display-link interval 35.818 ms, 44.398 ms worst callback interval, p99 GPU 6.586 ms (worst 10.800 ms), 529 missed presentation periods. There were zero geometry-buffer starvations, zero missed submission deadlines and zero audio overruns; audio p99.9 was 2,120 µs with 16 voices. Main-thread geometry p99 was 0.095 ms and render-queue wait p99 0.045 ms. This establishes gaps in callback delivery, not their root cause, and does not attribute failure to another application. The 60 fps gate remains unchanged.

### Explicit boundary → library export

Implemented `graph.group.export` and its native More/context/command-catalog action. The shared extraction code preserves enclosed processors, state, modulation and nested membership, remaps every copied identity, and exposes distinct incoming cables/output taps as recipe audio ports. External modulation dependencies and absent output boundaries fail atomically with an explanation. Saving an unused recipe now leaves active processing alone; changes to an assigned recipe still invalidate processing. Group movement through the API now moves descendants, and Arrange uses the same group-aware UI movement path. Schema and API documentation updated.

Validation: full app build, AppKit suite, eight relevant CTests and actual private-socket suite passed. Native QA PID 25680 (`after-group-export.json`) played the 16-voice dense fixture on BlackHole 2ch / 512 frames while grouping/exporting `Animated filter` as library entry 2, arranging it, undoing layout, undoing export, and redoing export. Both `logs/group-export-ui.json` and `logs/group-export-ui-redo.json` show playing=true, voices=16, fault=false and unchanged stable routing plan 1. Original definition n26 and copied definition n41 coexist. Screenshot 35 shows the explicitly arranged new entry. QA was stopped and retired afterward.

Partial G08 after measurement, fixed start inside the visible source definition: marquee (1 drag), Ctrl+G (1 shortcut), name (1 text operation), Return (1 commit), More (1 click), Save to subgraph library (1 click) = 6 actions, 0 panel switches, 0 redundant Apply/confirmation dialogs. Export opens the copy and selects its inherited name; no extra naming dialog is required. This completes reuse from an existing recipe boundary, **not yet packaging arbitrary song-rack processors**, so full G08 remains partial.

## Renderer lifecycle and cross-platform group API checkpoint

- The dedicated Metal render-loop prototype initially presented zero frames. A
  minimal native harness presented normally; app diagnostics then showed the
  pattern view had nonzero bounds while its CAMetalLayer drawable remained
  zero-sized. MTKView's cached drawableSize was insufficient with its own draw
  loop paused. The view now publishes actual layer backing dimensions on frame,
  layout and backing-scale changes. Native presentation requalification is pending.
- The presenter owns its three buffers independently of later window attachments,
  and stops without a main-thread GPU wait. Snapshot age is measured separately
  from cadence so repeating stale geometry cannot count as responsive rendering.
  AppKit checks pass; strict dense performance remains unqualified.
- Windows' document adapter now provides group create/update/remove/export,
  nested identity remapping and parent-aware node insertion. Its codec was already
  preserving boundaries. A platform-neutral test target links the actual Windows
  adapter and shared model on macOS; all 11 scenario groups pass, including group
  export, dry run, Undo/Redo, exact metadata roundtrip and unchanged playback for
  presentation/unused-library edits. This does not qualify Windows UI/device code.
- Running that adapter exposed two dangling JSON temporaries in optional insertion
  and detachment positions. Keeping the positions array alive fixes the failures.
  The API string validator now reuses the codec's UTF-8/UTF-16-length validation.
  Evidence: /tmp/screamseq-windows-graph-tests.log and
  /tmp/screamseq-render-layer-interface.log.

- Actual renderer recovery is confirmed in screenshot 36. The first visible
  run, including CUA inspection, averaged 56.08 fps with 217 unpresented drawables.
  A subsequent quiet run averaged 59.87 fps, with 3.83 ms p99 GPU time, five
  unpresented drawables, 16 sounding tracks and zero audio overruns. It still
  failed the unchanged deadline-count gate and the added geometry-age gate
  (maximum 50 ms); neither is a 60 fps qualification pass. Evidence is in
  `logs/dense-performance-render-thread*.json` with matching build records.
- An explicit present-at-time experiment was rejected by QuartzCore during the
  real shutdown test (`CAMetalDrawableInvalidOperation`). It was removed: a
  CAMetalDisplayLink drawable already owns its presentation timing. The next
  build uses native display-rate callbacks, capped at 120 Hz, and refreshes the
  immutable geometry snapshot after each main UI tick. Shutdown and quiet
  presentation tests are pending for that build.
- The graph/shared mixer transition and portable Windows document-adapter subset
  passed 5/5 CTests at this checkpoint. Full live topology and live recipe
  parameter editing remain unfinished: GraphPluginAPI still stops processing
  on a changed recipe, including parameter-only changes. These cannot be claimed
  as uninterrupted journeys until the audio/control publication path is extended.

- Native-rate follow-up: actual Quit passed, but the quiet dense run failed the strict presentation gates. It averaged 78.51 presented fps on the 120 Hz display, with 2,379 unpresented drawables, a 91.67 ms worst presentation interval and 70.57 ms maximum snapshot age. Audio retained 16 sounding tracks and zero overruns. Higher callback frequency did not solve frame stability. Evidence: `logs/dense-performance-native-cadence.json` and matching build record.

## Live recipe controls implementation checkpoint

Shared plugin recipes now store explicit native-unit parameter overrides beside
opaque preset state. Both platform codecs retain them. The Mac host prepares one
complete control snapshot for all independent ordinary/row/persistent/instrument
copies, validates against immutable catalogs, and applies it at the audio-block
boundary. Undo that removes an override restores its original preset value.
Editing a modulated knob also updates its normalized base. No active plugin state
is serialized or processor replaced by these control edits.

The same publication now carries prepared source settings, modulation ranges,
scripted pattern curves and cable gain for unchanged topology. Runtime pointers
refer to producer-owned immutable plans; retirement and script destruction remain
off the audio callback. Node/port/enablement/preset changes remain outside this
path and still need the broader transition implementation.

Parameter-only shared/model/session/Windows-codec/native-host checks passed,
including PCM at 1/17/128/256-sample partitions, all active/inactive copies,
modulated baselines, no-op/history/roundtrip, latest-wins publication and queue
pressure. Native mixer comparisons at 17/512/4096 samples preserve the held-note
source clock and report zero host allocations/frees/locks. The expanded controls
checks and full app build are running; native UI verification is still pending.

### Native controls walk, QA PID 44213

The dense song played on explicit BlackHole 2ch at 512 frames. From the shared
`Filter movement` definition, changing Frequency from 1000 to 800 Hz, Undo,
Redo, and changing the LFO rate to 0.25 cycles/beat all retained playing=true,
16 voices, stable routing plan 1 and zero overruns. Logs are
`live-controls-ui-{parameter,undo,lfo}.json`, with the exact bundle in
`live-controls-ui-build.json`. Screenshot 37 follows an explicit Arrange action.
The copy was stopped and exited through native Quit/Discard; the musician's
session and original fixture were untouched.

The walk found a real inspector regression: Undo restored 1000 Hz in the engine
and API while the field still displayed 800. The same-target inspector now
reloads when the document revision changes, while protecting an active field
edit. The AppKit regression suite passes; repeating native Undo on the rebuilt
bundle remains pending.

Action ledger from the visible song graph: More / library entry (2 clicks),
panel More / Float (2 clicks to gain usable working space), canvas focus / Home /
Tab×3 (5 navigation actions), parameter popup / Frequency / Return (3 actions),
value focus / select-all / type / Return (4 edit actions). One first attempt used
an uncommitted popup selection, leaving focus on the canvas; the retry is retained
as friction, not counted as a successful edit. The normal parameter edit needs no
Apply. Source rate editing then uses Tab to source / focus value / select-all /
type / Return. This is still too form-heavy and the default bottom graph is too
shallow; the full journey's interaction design is not qualified yet.

Expanded controls tests passed 4/4. A real 601-parameter VST3 fixture verifies the fix for sending unchanged parameters unnecessarily. The callback
now compares audio-owned baseline cells and sends only changed IDs. Complete
snapshots still support superseded edits; a producer-side limit of 128 changed
parameters per processor per publication reserves capacity for three pending
snapshots plus modulation. Oversized edits reject before document commit.

### Controls refresh and instrument walk

QA PID 48989 confirmed Frequency 1000 → 800 → Undo displays 1000 in the
inspector while all 16 voices continue with zero overruns. Screenshot 38 and
`logs/live-controls-refresh-undo.json` record the result. The taller default dock
provides substantially more canvas but still needs final dense legibility checks.
G09 from the library: More, instrument I1, Return enters the correct instrument
breadcrumb; Tab×4 reaches LFO; focus/select-all/type/Return changes rate to .25;
canvas focus/Option-Up returns with the instrument selected. This is 13 actions,
0 panel switches, 0 redundant confirms; it is still an interim form-heavy path.
Screenshot 39 and `logs/instrument-live-source-ui.json` confirm uninterrupted
playback. Native Quit/Discard of the disposable fixture exited 0.

The quiet 60 Hz run still fails: mean 59.57 fps, 13 unpresented drawables, worst
interval 50 ms, maximum snapshot age 54.96 ms, p99 GPU 3.57 ms, zero audio
overruns and 16 voices. `logs/dense-performance-controls-refresh.json` records
this failure. No performance pass is claimed.

### Direct shared-plugin inspector

The reusable-plugin sidebar now reuses the rack's searchable inline sliders,
exact-value fields and per-parameter source/activity menus. Graph parameter
updates carry optional unique gesture tokens; metadata history coalesces only
consecutive edits of the same graph/node/parameter with the same token. The
native history/session/mixer subset passed 3/3, and the AppKit suite passed.
The first AppKit run had an assertion assuming Swift Int instead of bridged
UInt32 parameter IDs; the corrected stable-ID assertion passed.

Actual QA PID 53038 verified search and exact Frequency editing during 16-voice
playback, and clicking the slider track changed the value. Native CUA drags
from the visibly measured thumb did not change it; this remains a failed gesture
check, not a pass. Screenshot 40 and `logs/inline-shared-parameters-ui.json`
record the state. Native Quit/Discard exited 0.

ParameterSlider now handles drag events through the normal application event
loop instead of NSSlider's nested tracking loop, with one begin/end gesture and
Escape restoration. Its AppKit intermediate-value/gesture test passes. This
correction still needs the rebuilt native UI check.

Investigation also found that graph.plugin.get could report a preset value when
a modulation edge supplied a different base. The API now reports that base in
native units, and live publication updates the control-owned Parameter activity
catalog, including Undo. No-op baseline writes are covered. These changes and
the full build are currently being qualified.

### Direct gesture verification and song-group implementation

The rebuilt native QA app (PID 57014, uniquely identified disposable bundle)
passed a real slider-thumb drag: shared Filter Frequency changed from 1000 to
8578 Hz, Command-Z restored 1000 and Command-Shift-Z restored 8578. All 16 voices
continued and the API reported zero overruns. Screenshot 41 and
`logs/direct-parameter-drag-ui.json` retain this evidence. Native Quit/Discard
exited 0. A subsequent accessibility lookup accidentally relaunched the owned
QA bundle without its inspection flags; that process was quit as well. Do not
query an app object after its final Quit, as lookup may launch it.

The full socket suite passed before adding song processing groups. New shared
song-group model/codec/API work now supports nesting, boundary movement,
ungrouping and independent rack-chain export. The first test run found an
incomplete plugin descriptor in the Mac fixture and a dangling temporary JSON
reference in the Windows adapter. Both were fixed; the targeted model/session/
Windows-document/unified-history subset passes 4/4. These APIs still need the
rebuilt socket and actual UI checks. New interface tests exercise the real
sidechain identity through a collapsed group; qualification is in progress.

The diagnostic sample run is retained as `logs/dense-performance-profiled.json`
and is deliberately labelled invasive, not a quiet performance pass. The main
thread sample spent roughly 72.5% waiting; Core Animation/native button drawing
and repeated context refresh remain investigation targets. The prior quiet
60 Hz presentation failure remains open.


### Song-group native walk and routing-export correction

The first song-rack G08 walk (PID 64391) successfully grouped EQ5 and Distortion,
entered the boundary, and saved an independent library copy during 16-voice
playback. Export Undo/Redo preserved the original plugins and mixer exactly;
save and native Quit completed. Screenshots 42–43 and
`logs/song-groups-live-ui.json` retain the result. That walk took 9 actions
because immediate grouping collided with an inspector read and rename did not
receive focus; it is not the final fluent-path measurement. The full socket
suite passed with the song-group catalogue (`logs/song-groups-socket.log`).

The graph request adapter now retries only definite busy rejections, with
captured revision/target, bounded attempts, document cancellation and no stale
rebase. Edits begun during a graph read are retained rather than dropped. An
initial native re-walk (PID 67595) passed immediate grouping but exposed selection
cleanup cancelling its rename callback. The returned group is now selected before
projection hides the original children; the new AppKit regression covers this
exact sequence. That disposable process exited 0 through native Quit/Discard.

Shared rack-group export now preserves auxiliary ports inferred from enabled
routing and gives extra main-input fan-in to a downstream insert its own graph
boundary. Disabled/disconnected inferred ports are excluded. A rendered fixture
checks that the extra main input bypasses earlier inserts and the detector input
reaches its actual target. External song gain/tap routes remain unchanged.
The model/session/portable Windows-document/history subset passed 4/4, and the
AppKit suite passed. The final native re-walk (PID 69605) passed in six actions: marquee, Ctrl+G, type name, Return, More, Save to subgraph library. There were no panel switches or redundant Apply presses. Native Undo/Redo preserved the original plugins/mixer and kept 16 voices playing with zero reported overruns. Screenshots 44–45 and `logs/song-group-direct-name-ui.json` retain evidence and the exact build fingerprint. The private project was saved and native Quit exited cleanly.

### Group diagnostics and inspector continuity

Collapsed song groups now resolve meter/scope/listen socket aliases to the real
processor and bus number. Serial groups prefer their unique real Main output;
parallel main outputs require a choice. Overload navigation opens a hidden
processor's immediate nested owner. Silence tracing traverses the ungrouped
projection, and search includes hidden child names. Plugin-port reads retry only
definite busy rejection and report other failures without caching empty success.
AppKit regressions pass (`logs/group-diagnostics-interface.log`).

Native QA PID 72581 visibly showed the Tone pair group's input/output meters and
its headphone badge selected the final Distortion output, confirmed by API.
Escape restored normal monitoring. Sixteen sounding voices and zero reported
audio overruns continued. Screenshot 46 and `logs/group-diagnostics-ui.json`
retain the evidence. The private fixture was saved and native Quit completed.

G09 remains incomplete: entering the instrument graph worked, but the fixture's
saved nodes overlapped (explicit Arrange and Home corrected the fixture).
Selecting its filter put library metadata and object forms above its parameter
controls, below the dock's fold. Two scrolling attempts overshot. Screenshot 47
records the friction; this is not a passed after-journey. The inspected instrument
copy also incorrectly exposed ordinary channel-assignment controls. Source now
puts parameter controls first, hides library metadata while inspecting an object,
and resolves instrument copies to their real assignment. The AppKit suite passes for these inspector corrections (`logs/inspector-primary-interface.log`); the rebuilt native re-walk is pending. The first compile was invalidated by a concurrent source edit, then rerun with the sources held steady.

### Instrument journey native verification

QA PID 76086 passed G09 in seven actions, zero panel switches and zero Apply presses: double-click the instrument graph, select Digital Filter, click/search Frequency, drag, More, Show in pattern. The baseline begins with hidden instrument navigation while this fixed start has the copy visible; do not treat the raw reduction as a perfectly matched start. Frequency changed 1000 → 5523.3989 Hz; a single native Undo restored the exact graph and Redo restored the value. The pattern bridge focused channel 3. All checkpoints retained 16 voices and zero overruns. Screenshots 48–49 and `logs/instrument-primary-ui.json` include the build fingerprint and full API checks. An initial CUA coordinate-scale mistake selected a grid cell before the graph walk; no musical data changed. The private fixture was saved and native Quit exited 0.

### Retaining graph copies during unrelated live routing

The native host no longer rejects every mixer route edit merely because reusable or sample-instrument graphs exist. It projects the unchanged signal/sample sources into the candidate mixer, checks the latest published controls and immutable note-envelope membership, then retains the existing processor wrappers and delay histories. Changed processor inputs, topology, auxiliary activation, adapters or total latency remain outside this path. A rendered VST3 fixture with ordinary, row, persistent and sample-instrument copies passes at 17/512/4096 frames, including route reversal and zero realtime allocations/frees/locks. The native mixer test and five transition/graph-routing regressions pass; logs are retained. Actual UI verification of this extension is pending.

### Retained-route native verification

QA PID 78949 passed the retained-processor subset of G10: Track 3 output moved from Rhythm group to Master, Distortion bypassed/restored, then three chronological Undo actions restored the original route and bypass state. Redo restored the route. All checkpoints retained 16 voices, advancing frames and zero audio overruns. Graph recipes/assignments/sample-instrument copies and the existing send/detector branches remained unchanged. Seven core actions followed cable selection; the tight converging cables required a popup to disambiguate after a wrong cable selection and an accidental socket Add. Screenshots 50–51 and `logs/retained-routing-ui.json` record the full result, including setup friction. The private fixture was saved; native Quit exited 0. General changed-input live repatching and strict presentation qualification remain open.

### Cable selection and modulation/send gestures

Nearest-curve hit testing replaces storage-order selection for dense fan-in; converging and short-cable AppKit regressions pass (`logs/nearest-wire-interface.log`). QA PID 81740 passed G04 in four actions and G05 in six, with screenshots 52–53 and `logs/wire-gestures-ui.json`. Zero-depth creation preserves existing modulators, and depth Undo/Redo remains live after transport restart. Structural source/return creation still stops playback. G05 exposed an offscreen badge and floating-point text noise; source corrections reveal only the new amount control and preserve gesture precision. Their rebuilt native check is pending. The private fixture was saved and native Quit exited 0.

Graph-envelope auto-save is now in implementation: pointer gestures save once on release, numeric fields on completion and formula typing after a short pause. Pending writes retain target/revision; only definite busy rejections are retried. Conflicts preserve the draft. Apply and Set point are removed; advanced ramp/delete/retry/discard remain in More and keyboard access. Interface qualification is running; no native pass is claimed yet.


### Automatic envelope edits and visible send amounts

QA PID 85064 verified graph envelope edits during 16-voice playback: numeric
entry saved point 0 / 0.375, dragging saved row 8 / 0.5 as one edit, Undo restored
the numeric point, and Redo restored the drag. All checkpoints reported playing,
advancing frames and zero overruns. Screenshot 54 and
`logs/automatic-envelope-ui.json` retain exact state and executable identity.
The private fixture was saved and native Quit exited 0.

QA PID 87856 passed G05 in four actions with no panel switch or redundant Apply:
drag Track 3 output to blank, type return, Return, drag the visible amount badge
to −12 dB. The gain field also retained its field editor through asynchronous
refresh. Screenshot 55 and `logs/send-focus-ui.json` record this stopped check.
The badge now avoids cards, stays within the canvas and paints over cards when
space is constrained. The private app exited 0.

The graph-envelope AppKit test initially failed a fixed-delay assertion under
load, then passed on retry. Positive expected writes now use bounded polling
instead of timing their arrival to a fixed fraction of a second. The final
rebuilt AppKit suite passes (`logs/stoppage-interface.log`).

### Presentation experiments remain failures

The quiet 60 Hz snapshot-deduplication run (`logs/dense-performance-dedup.json`)
measured 55.91 presented fps, 75 ms maximum interval, GPU p99 4.37 ms, zero
audio overruns and 16 voices. It also displayed only six cards, below the
eight-card workload requirement. The 120 Hz native-rate experiment measured
55.93 presented fps and an 83.33 ms maximum interval; it also failed and has
been reverted. These runs do not qualify the complete dense UI, and the final
build has no new strict 60 fps pass. No thresholds were relaxed.

### Live return buses and concurrent telemetry

Prepared routing now permits group/return bus addition, removal and reordering
when source adapters, every processor input dependency, graph note membership
and total latency remain unchanged. Each mixer plan owns its bus-to-meter map.
The bounded port catalogue publishes immutable identities and meter slots
together, so control preparation cannot resize storage under UI/audio readers.
Concurrent registration/telemetry and rendered new-bus/Undo checks pass; actual
VST3 plus ordinary/row/persistent/sample-instrument copies match continuously
rendered references through 10 ms fades at 17/512/4096-frame blocks, with no
audited realtime allocations, frees or locks. The final native UI check and
full-suite result will be recorded in the stoppage report.


### User-requested stopping point

Work is paused at the user's request. The final build, signature, AppKit suite,
full API socket suite and 82/82 native CTest cases pass. See
`../../SCREAMSEQ_GRAPH_STOPPAGE_REPORT_2026-09-30.md` for the consolidated report,
control relocation list, journey comparison and prioritized unfinished work.
`index.html` is a curated gallery of actual native screenshots.

The final unique QA app (PID 91524) launched the 16-channel fixture, but CUA
repeatedly returned `timeoutReached` after audio settings closed. Its API remained
responsive and the sampled main loop was active. The live-return gesture was not
performed; no final native live-return pass is claimed. The app was stopped via
API and terminated by verified PID. No musical edits were made in that final
private session. All owned test processes are closed. No new strict 60 fps run
was performed; earlier presentation failures remain unresolved.
