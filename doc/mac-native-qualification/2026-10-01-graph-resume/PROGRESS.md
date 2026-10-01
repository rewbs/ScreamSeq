# Graph redesign resumed — 1 October 2026

The user resumed the approved four-slice graph interaction goal. Before edits,
the clean local checkpoint `64636f95b` was fast-forwarded to `e48c645bb`, including
the merged Mac/shared/Windows review fixes. The previous stopping-point report
remains historical evidence, not a claim about this build.

## Work in progress

- Song processing-group additions carry their parent boundary and choose the
  existing group's insertion context when unambiguous.
- Rack deletion removes routing, layout and empty processing groups together;
  stable musical automation and pattern bindings remain as unresolved targets.
  Batch deletion uses stable plugin identities and a single chronological Undo.
- Focused graph traversal follows the chosen path instead of expanding all
  channels feeding a downstream summing bus. Hidden branches have read-only,
  clickable boundary links; search/category filters use the same affordance.
- Display qualification is gaining correlated callback/GPU/presentation timing
  and an explicitly readable dense viewport. The unchanged 60 fps gate has not
  yet been passed.
- The shared transition executor is being extended to handle changed effect
  catalogs while keeping instrument adapters stable. This foundation alone does
  not enable all live topology edits in the application host.

## Initial verification

The first focused run passed five suites: pattern performance, mixer publication,
musical automation, signal graph session and unified history. The history test
includes group add/delete/Undo/Redo/persistence and atomic invalid-batch rejection.
See `logs/initial-targeted.log`. Full-build, native UI, final audio and performance
results will be recorded after the corresponding checks actually run.

All application qualification uses a separate bundle and private fixture copy.
No musician session or system audio defaults are replaced.

## Native UI walkthrough and broader tests

The first isolated app (PID 71802) ran the private 16-channel fixture using
BlackHole 2ch at 512 frames. The native walkthrough verified:

- Focusing Track 3 showed ten nodes and three hidden-branch links. Clicking the
  compressor's “1 other input” link revealed the omitted input in one action;
  both song revision and graph data remained identical.
- Grouping EQ and Distortion retained their identities. Inside that group,
  Shift+A, a search and Return added Gainer as its third member (three actions).
- Delete followed by one Undo restored the exact group, mixer and plugin data.
- The private fixture was saved, and native Command+Q exited cleanly.

The initial app was an intermediate build: source changed during its build, so
its manifest is not evidence for a coherent final-source qualification. Its
screenshots and action log document only the observed interactions. Adding the
plugin stopped playback; live insertion remains unfinished.

The reveal also exposed an automatic-layout collision with a saved Return card.
The source fix reserves saved/current positions before placing a newly visible
card. Regression coverage also verifies sample-instrument source ports have no
fictional audio input. The full interface suite passed after these fixes; native
UI reinspection on a rebuilt app is pending.

The broader native/portable CTest run passed **84/84**, in 230.90 seconds. This
includes the portable Windows graph-document test, not a Windows-native app run.
A later full application build correctly rejected a Swift input modified during
compilation; it did not produce a new qualified application. Another coherent
build is required after the pending shared-history repair.

## Coherent UI recheck and measured performance

A subsequent coherent application build succeeded with source fingerprint
`6dec897ca3d83c375fbab441f3408caa76b341aaec672e898d53baf96841ce78`.
`coherent-build.json` records the full manifest. The separate QA app (PID 16055)
verified the focused-path reveal without the earlier Return-card overlap,
unchanged document/graph data after reveal, and Space starting playback while
the graph owned focus. It used BlackHole 2ch, then stopped and exited normally.
Screenshots `after/04-coherent-graph-dock.png` through
`after/06-coherent-revealed-main-input.png` and `logs/coherent-ui.json` capture
this build. Later source additions are not covered by that executable.

A valid isolated 60-second docked stress run showed nine fully visible cards
at 0.9 zoom, a 200-point pattern strip and 16 sounding channels. It had no audio
overruns; p99 rendering CPU/GPU were 0.151/0.604 ms. The unchanged presentation
gate still **failed**: 59.88 fps, 29 missed presentations, maximum gap 50 ms.
The frame trace includes callback, commit, GPU completion, target and actual
presentation times. Many irregularities were 25/8.33 ms pairs despite timely GPU
completion. See `logs/docked-60s-iteration1*.json` (or the correspondingly named
root evidence files). A separate native screenshot confirms readable cards;
production port labels now have a 9-point screen-space floor, awaiting final
build verification.

A controlled latency-1 experiment did not improve the result: 59.58 fps,
39 missed presentations, maximum gap 175 ms, no audio overruns. The longest gap
had timely GPU completion followed by delayed presentation/display callbacks.
Production retains latency 2. No timing gate was relaxed, and neither experiment
qualifies the complete graph redesign as 60 fps.

## Additional verified foundations and pending integration

- Shared Undo/Redo allocation-failure coverage passed 98 injected failures,
  preserving document and history on failure. An additional publication-before-
  commit hook is now being implemented for live grouped rack Undo.
- Native live-rack rendering tests passed add/remove/reorder and retained plugin
  state/parameter targeting at 17, 512 and 4096 frame buffers, with no host
  realtime allocations, frees or locks. Active-session grouped Undo and failure
  cases still need their next run; arbitrary differing-latency transitions remain
  outside the currently implemented live subset.
- The song-level modulation evaluator passed callback-partition, envelope/script,
  precise step/random, overlapping-source, quantization, scoped note-envelope,
  actual stereo follower, stale-buffer and realtime-audit tests.
- Both project codecs now carry song-level sources, stable rack-parameter edges
  and root-envelope bank links. Native-song storage tests passed. Portable
  Windows metadata coverage has been extended but its new cases are pending.
- New source/edge APIs, `graph:null` envelope/bank addressing, schema and guarded
  session tests are in progress. The schema examples/invalid-field checks pass;
  actual API/session, host rendering and full UI integration are not yet claimed.
- Atomic song cable cuts and group delete/heal passed targeted native session,
  portable graph and interface tests before the latest modulation extension.
  Mixed audio/modulation cuts have since gained an atomic shared representation;
  those new cases await rebuild.

The final application build, full regression run, ten complete journeys,
commercial-plugin checks, floating-window timing and final performance/audio
qualification remain open. This is a work log, not a release-completion report.


## Later integration checks

The copied-source native interface suite passed after adding the song-source
activity bridge and parameter command catalogue. The catalogue resolves stable
processor/parameter identities, distinguishes duplicate names and full-width
parameter IDs, reports read-only targets, ignores retired replies, and focuses
an exact value after its asynchronous catalogue arrives. Disabled contextual
palette actions no longer execute merely because their action selector exists.
Evidence: `/tmp/screamseq-catalog-interface.log`. This snapshot predates the new
presentation-frame and detached-effect tests.

Hosted song modulation now has rendered coverage over AU/VST effects and
instruments, parameter ramps, recorded/editor baselines, scoped note envelopes
and precise retriggers. The native-mixer, precise-ramp and musical-automation
suites passed after adding a single routing-plan publication for linked graph
sources and ordinary parameter envelopes. A newer standalone lane edit cannot
be overwritten by an older prepared compound update. Additional API/device
coverage of the envelope-bank caller is still pending.

Actual silent CoreAudio checks passed first-source creation, depth change,
Undo/Redo and continued callbacks with an implicit saved mixer. A further API
fix preserves implicit routing even when a source explicitly references a
channel or Master; its new persistence/history tests await the next build.

The next implementation batch adds visual frames/comments, semantic cable
reroute positions and unconnected rack effects. Shared storage and Mac/Windows
request semantics are being kept aligned. Detached effects must never fall
back onto Master; connecting one assigns it to a real channel in one Undo.
These changes are work in progress until their model, API, rendering and actual
UI checks complete.

Display profiling has separated application work from presentation stalls.
Main-thread layout/state restoration explains some stalls, while other long
presentation gaps occur with timely main-loop and GPU work and delayed display
callbacks. Narrow no-change control guards are implemented. Experimental
refresh/latency and window-restoration settings remain in private candidates;
production settings and strict timing gates are unchanged. See
`FRAME_PERFORMANCE_FINDINGS.md` for measured failures and source fingerprints.

## Subsequent integration checkpoint

The complete copied-source AppKit interface suite passed with visual regions,
reroute points, root parameter commands, detached-effect Add and the new
modulator-to-parameter-control drop. The gesture test's initial failure was a
fixture coordinate that remained inside the resized canvas; after using the
actual visible boundary, it verifies restored source geometry and no extra
layout Undo. Actual parameter-row hit tests cover filtering and hidden surfaces.
Evidence: `/tmp/screamseq-control-drop-interface-final.log`. A native application
walkthrough still needs the next coherent executable.

The next native checkpoint passed six suites: native mixer, mixer publication,
song modulation, signal graph session, unified history and portable Windows
graph document. It includes current PCM follower tap switching with retained
attack/release state, source-clock partition coverage and staged bus telemetry
publication. Earlier silent CoreAudio checks also verified continued callbacks
through detached effect Add/insert/Undo/Redo and a linked song-source/parameter
envelope-bank edit. Combined recipe/parameter-bank publication is the next case.

A new portable Windows mixer-adapter test passed on macOS. It exercises actual
Windows request handling for atomic quiet return/send creation, saved placement,
Undo/Redo, invalid requests and a detached effect returning to implicit routing.
This also covers a dangling JSON temporary found in its moved-insert position
loop. The Windows-native controller integration test remains unexecuted here.

The latest fixed-60 docked run recorded continuous foreground/key/visible window
state and a held activity token. Background throttling is ruled out for that
run. Maximum presentation interval was 33.333 ms, snapshot-to-main delay 30.899 ms
and there were no audio overruns, but 13 misses across 3473 samples still fail
the unchanged strict missed-presentation ratio. Average cadence alone is not a
qualification pass.

## Ten-journey integration pass

The next coherent snapshot (`4d6082eb51b7bf8505f4da0ce4ba9e7cf16a976343bd5a7ce0c4a6ef9edb69cc`)
built successfully. Its complete CTest run passed 85/88; failures were native
sample round-trip PCM, updated graph latency and portable presentation metadata.
Subsequent targeted fixes retain the original assertions: unannotated sample
exports keep upstream integer summing, updated graph delay reaches the combined
latency plan, and project decoding tolerates unknown presentation fields while
API mutations remain strict. The final combined full run is still pending.

All ten graph journeys were attempted in the actual separate application
(PID 51911, BlackHole 2ch, 48 kHz/512). The detailed action ledger and screenshots
are in `JOURNEY_WALK.md` and `index.html`. Live effect insertion, root modulation,
quiet send creation, diagnostics, processing-group export and whole-chain
repatch/Undo/Redo were observed. Sidechain first-use activation and a sample
instrument recipe edit after root-source creation failed during playback. The
old audible plan remained active. These failures drove the next engine fixes;
they are not retrospectively passes. The QA project was saved, stopped and
closed normally with Command-Q; its foreground process exited successfully.

The walkthrough also exposed lost cable releases during refresh, disappearing
backend errors, stale gesture edge indices, hidden newly added buses, stale
parameter searches, misleading floating-point overload wording and old errors
remaining after a successful retry. The combined copied-source native interface
suite passed with these fixes (`/tmp/screamseq-graph-feedback-final.log`). Later
baseline/effective-value and refresh guards require their next coherent run.

Existing automation sources now have a shared bounded read projection and lazy
Mac graph cards. Their “Sets base” wires are read-only references, with exact
editing/Back bridges. The host's delivered value must remain separate from the
editable manual baseline; that UI/API correction is being integrated. Nonempty
socket assertions now cover envelope IDs/position units, precise FX positions
and durations, recorded 48-kHz frame spans, and source updates through Undo/Redo.

Opaque recipe preset replacements have targeted AU/VST and device evidence,
including hidden vendor state and all prepared graph copies. The previous
ASan/UBSan checkpoint passed four suites without findings; newer detector and
baseline edits are not covered by that older sanitizer result. No global
completion, commercial-plugin continuity or strict 60 fps claim follows from
these bounded checks.

## Full suite and live regression rechecks

The next coherent source fingerprint
`846ebff854aacd455b2ed2308b11e06b371b8cb0381be151be873cc5e68b6678`
built and passed all **89/89 CTest suites**, the complete actual socket suite,
and the copied-source native interface suite. The earlier three failing
assertions remain intact. Logs are retained with this evidence checkpoint.

The actual application rechecks now pass J03 (one drag enables an unused
compressor detector during playback) and J09 (instrument recipe gain changes
after adding a root LFO). The app used a private fixture and explicit BlackHole
48 kHz/512-frame output; the last transport sample had rendered 20,228,608
frames with zero overruns or faults. See the appended `JOURNEY_WALK.md` ledger
and before/after captures. This closes those two specific observed failures.

A further manual-base control check correctly separated 80 Hz from a 519.7 Hz
effective snapshot and committed a 100 Hz baseline during playback, but
neither Command-Z nor Edit → Undo restored it. That newly observed history
failure remains open and is being investigated; the successful control edit
must not be mistaken for successful history qualification.

New per-source range handles, dynamic recipe tail growth and sample-offset
Auto/External detector refinements have focused tests but are newer than the
current native checkpoint. They require the next integrated build and native
rewalk. Docked/floating strict presentation measurements are next, with the
same thresholds and no concurrent builds or other QA audio workloads.


## Installed commercial-plugin check

The coherent graph-review app passed `mac/Tests/test_battery.py` against the
installed Battery 4 AU and VST3. Both independent scanner probes and hidden app
load/state-restore/remove paths succeeded. AU exposed 128 parameters and VST3
2,224; state payloads were about 51 KB. A full scan took 17.80 s, a warm cached
read 0.006 s and a restarted process read 0.483 s. This test opened no windows
and no hardware audio device. It verifies these commercial host operations,
not musical kit playback or live topology transitions. See
`battery-commercial-results.json` and `logs/battery-commercial.log`.

The next source snapshot is frozen at
`/tmp/screamseq-graph-controls-20261001`, with 297 changed/new files verified
against `/tmp/screamseq-graph-controls-snapshot.json`. It includes range handles,
manual-parameter history repair, final detector/tail refinements, and relocated
node selection/Open/Remove actions. It has not yet received native UI
qualification. Further discrete recipe-modulation work remains a separate
source delta and is not implicitly included in this checkpoint.


The pre-quantized controls checkpoint's full copied AppKit suite passed after
correcting the node-finder fixture to use real revision replies and disambiguated
bus labels. Numeric plugin history passed the unified-history/session suites
and an actual Core Audio regression while a root LFO was running, including
save folding and Undo/Redo without a transport stop. The final detector selector,
meters and tail refinements passed the latest three-suite ASan/UBSan run.
Logs: `logs/screamseq-graph-undo-interface.log`,
`logs/screamseq-parameter-history-tests.log`,
`logs/screamseq-numeric-undo-device.log` and
`logs/screamseq-final-detector-sanitizer-tests.log`.


## Controls/provenance checkpoint and remaining qualification

The coherent Controls app passed full build, 89/89 CTest and socket tests. Actual
UI checks verified independent modulation ranges, envelope/recorded/pattern source
bridges and return navigation during playback. Numeric Undo restores audio/model
state but revealed a stale displayed base value, now repaired with regression tests.
The new discrete-recipe modulation passed nine targeted suites and four ASan/UBSan
suites; its actual native UI and final loopback qualification are still pending.

The walkthrough led to explicit auto-reveal of newly shown source cards, wrapping
of provenance explanations, friendly recorded-target names and a truthful warning
that editing recorded points requires stopped playback. Copied interface suites
passed these changes. They are newer than the saved Controls executable.

A bounded redraw optimization is now being compared in quiet docked/floating runs
without relaxing the strict presentation gate. Retired-port/freshness and dynamic
latency labels are receiving a separate correctness repair. Final snapshot, audio
loopback, remaining native rechecks and final report are still open.

## Polish native checkpoint

The next frozen executable passed 89/89 CTest and the actual socket suite. Native
checks now close the stale manual-field Undo bug and verify source auto-framing,
friendly recorded targets, direct single-command navigation, compact cards with
connected ports/meters, and live single-effect detach/heal/Undo. The saved private
song rendered 17.9 million frames at 48 kHz/512 with zero overruns before normal
shutdown. See the latest `JOURNEY_WALK.md` section and `polish-` evidence.

Native walking found a reference-wire endpoint projection issue, being repaired
before the next snapshot. Exact post-gain/post-delay root-cable observations,
occupied processing-stage containers, and exact selected-target bypass guards
have newer source/interface coverage. Those changes still need coherent native
qualification. The strict 60-second display gate remains failed in docked and
floating runs; a matched graph-free control also failed, so further scheduler/
presentation investigation is required. No threshold was relaxed.


The frozen Polish audio libraries passed all six actual Core Audio loopback
comparisons at 48 kHz/512: dry, AU effect, VST3 effect/automation, VST3
instrument/automation, AU instrument, and graph copies/fractional commands.
Captured versus offline PCM maximum/RMS error was zero in every case, with zero
callback overruns. Graph capture covered 480,000 frames. The harness initially
rejected the private VST fixture because newer plugin-trust rules require explicit
process-local trust; the harness now uses the existing FixtureTrust helper. No
user plugin installation/trust store or system audio default was changed.
The qualification-only delta is recorded in `polish-loopback-build.json`.


## Observation integration and next slice

The coherent Observation build passed all89 CTests and the real socket suite. Native stage labels and live audible-order presentation were observed. Clean60-second docked/floating checks still failed the unchanged presentation gates, with zero audio overruns. Static channel-label reconstruction on each telemetry poll was isolated as avoidable main-thread work and has been cached in subsequent unqualified source.

Current source adds host-level per-processor bypass to reusable recipes and an audio-output→parameter gesture that explicitly offers an envelope follower. The follower, input tap and initially zero-depth target are one guarded, undoable API edit. Core bypass audio checks passed; integrated adapter/UI/device qualification is in progress. These source changes are not included in the Observation executable.


## Follower native checkpoint

Full build, all 89 CTests, actual socket and copied interface checks pass for the coherent Follower executable. Actual UI confirms shared-recipe bypass/Undo during playback, viewport recovery, atomic audio-to-parameter follower conversion while stopped, and explicit discrete quantization. Adding a new source to a reusable graph remains rejected during playback; the old graph continues. The walk also found untouched auto-focused numeric fields swallowing document Undo, with a focused fix in progress. Later latent-preset replacement now warms its declared delay before crossfading (offline and sanitizer checks pass); it is not part of the Follower executable. A fresh strict docked/floating performance run is pending.


## Paused at the requested stopping point

See **STOPPAGE_REPORT.md** for the consolidated implemented/tested/remaining list,
journey measurements, control relocations and open musical decisions. Final source
fingerprint `97711e130ec79949ecce22b2b8bc65b30a0f45b14211a82dd84a10469f73d7a9`
has 856 unchanged build inputs, 89/89 CTest PASS, actual app socket PASS, copied
AppKit PASS, seven focused audio suites PASS and five ASan/UBSan suites PASS.
Packaging and deep/strict signature verification succeeded after the documented
compiler retry and disk-space cleanup. No final native walkthrough or strict 60 fps
pass is implied. Profiling and feature work are paused; newest device-source test
is pending. Changes are local/uncommitted and no resumed-work push was made.
