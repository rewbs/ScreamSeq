# Graph frame qualification — 2026-10-01

The strict display gate has **not passed**. The latest targeted refresh
optimization reduced graph work in both layouts, but a matched pattern-only
control also missed the strict display deadlines. Audio had zero overruns in
those three latest runs. The final section records their evidence and limits;
the earlier sections retain the preceding experiments. Results concern the
explicitly recorded candidate bundles, not subsequent shared-engine/UI changes.

| Run | Dense viewport | Presented samples / missed periods | Maximum interval | Maximum snapshot age | Audio overruns |
| --- | --- | --- | --- | --- | --- |
| Docked, latency 2 | 9 full cards at 0.9×, 200 pt pattern | 3468 / 29 | 50.000 ms | 29.456 ms | 0 |
| Docked, latency 1 experiment | Same | 3450 / 39 | 174.995 ms | 79.043 ms | 0 |
| Docked, bounded main-thread trace | Same | 3461 / 11 | 91.666 ms | 70.295 ms | 0 |
| Docked, native refresh experiment | Same | 6719 / 5 | 41.662 ms | 38.791 ms | 1 |
| Docked, unchanged-controls/restoration experiment | Same | 3461 / 20 | 116.666 ms | 35.373 ms | 0 |
| Docked, native refresh + latency 1 + layout experiment | Same | 6579 / 4 | 50.000 ms | 41.984 ms | 0 |
| Docked, fixed 60 Hz + window-state observations | Same | 3473 / 13 | 33.333 ms | 30.899 ms | 0 |

Both used the preserved private Resume fixture, 16 sounding channel meters,
50 nodes, the BlackHole 2ch QA output, and unchanged acceptance thresholds.
The first run failed missed-period and maximum-presentation-interval gates.
The latency-1 experiment additionally failed maximum geometry snapshot age.
Production preferred frame latency remains 2.

## Fixture repair and native inspection

The earlier 1.0× docked setup displayed only six whole cards. It was rejected
before playback measurement, with `measurementStarted:false` and duration zero.
The split actually retained a 200 pt upper pane; graph chrome left a 264 pt
canvas. The repaired fixture tries bounded real scroll magnifications 1.0, 0.9,
then 0.8, retaining the original 50 nodes and saved positions. It counts whole
cards throughout and keeps at least 180 pt of the tracker visible. It does not
hide/rearrange workload nodes or use an unreadable fit-all view.

`after/06-dense-docked-0.9.png` is a separate native screenshot rewalk showing
nine readable complete cards. The newer production port-label floor (9 screen
points in detailed mode) was added afterward and needs its own native check.
The floating-graph fixture mode is implemented but not yet qualified.

## Correlated frame evidence

`docked-60s-iteration1-frames.json` and `docked-60s-latency1-frames.json` correlate
each drawable's display-link callback, update deadline, presentation target,
geometry creation, command commit/scheduling, GPU interval and actual display.
The update deadline is not the presentation target.

At latency 2, CPU preparation p99 was 0.151 ms and GPU p99 0.604 ms. One commit
missed its update deadline; no recorded GPU completion missed the corresponding
presentation target. Some already-completed drawables were not presented.
There were occasional 25 ms / 8.33 ms cadence pairs and genuine longer gaps,
including a roughly 17 ms command-commit pause near 44 s and a 50 ms displayed
gap. Several display-link callbacks skipped a 60 Hz interval.

Requesting latency 1 did not shorten the observed roughly 50 ms callback-to-
presentation-target interval in this windowed environment. Its worst gap was
174.995 ms; the corresponding GPU work was still short, with a long callback
pause and an earlier 79 ms snapshot-age excursion. The experiment did not
justify a scheduling change.

The next isolated diagnostic candidate adds bounded main-thread work timings
on the same monotonic clock. It distinguishes graph drawing, whole UI ticks,
snapshot preparation, plugin edit collection, session telemetry, graph/workspace
refresh, and the qualification workload. It adds no acceptance exemptions.

## Main-thread diagnostic run

`docked-60s-maintrace.json` and its `-frames.json`, `-work.json`, `-build.json`
companions capture a valid 60.012 s run with the same nine full cards, 0.9× scale
and 16 sounding channels. It still failed: 11 missed periods, 91.666 ms maximum
presentation interval and 70.295 ms maximum snapshot age; zero audio overruns.
All recorded commits met their update deadlines and GPU completions met their
presentation targets. The 31,305 main-thread records fit the preallocated ring
without overwriting.

Observed maximum / p99 durations (ms):

| Work | Maximum | p99 |
| --- | ---: | ---: |
| Whole UI tick, including snapshot | 9.652 | 5.647 |
| Graph draw | 6.984 | 6.713 |
| Graph activity/telemetry update | 8.899 | 5.615 |
| Pattern snapshot | 1.011 | 0.169 |
| Session telemetry | 0.658 | 0.347 |
| Qualification tick | 1.565 | 0.880 |

The worst presentation gap ends at roughly 15.896 s on the relative frame
clock. Display-link callbacks jump from 15.804 to 15.890 s. Main ticks continue
at 15.807, 15.824, 15.839 and 15.856 s, each under 0.5 ms. Two earlier completed
drawables never present, and the next presents about 41.65 ms after its target.
This pause is outside the instrumented main-thread work and GPU execution.

Separately, the maximum snapshot-age excursion corresponds to an uninstrumented
main-thread gap from about 15.617 to 15.684 s, followed by a 2.49 ms graph draw.
A main run-loop/AppKit/system scheduling investigation remains necessary;
these measurements do not establish that a single application callback caused
both pauses. A profiler run is diagnostic evidence only, not a clean gate run.

## Diagnostic Time Profiler

A separate private candidate process was attached to Time Profiler for 70 s.
The trace is retained only in the private build directory because its metadata
contains process/environment information. The sanitized report and bounded
timelines are `docked-60s-profiled-diagnostic*.json`; this was deliberately a
diagnostic run and cannot be used to claim clean performance qualification.

The main thread accumulated approximately 13.48 s of running samples. Native
Core Animation transaction/display work accounted for much of it. A 107.6 ms
main-loop gap at trace-relative 62.903–63.011 s contained approximately 80 ms
of running samples, largely AppKit's internal SwiftUI/AttributeGraph environment
updates under deeply nested `NSView._layoutSubtreeWithOldSize` calls and the
window display-cycle flush. The app itself uses AppKit, not a SwiftUI root.
A subsequent 121.7 ms gap was mostly unsampled waiting; Time Profiler samples
running threads and therefore cannot identify its waiting cause.

A separate 42.5 ms gap at trace-relative 28.319–28.361 s included
`NSPersistentUIManager.flushAllChanges` and deep recursive encoding of view
state. These observations justify investigating redundant native-control
updates and redundant AppKit restoration work. They do not prove that either
explains every dropped presentation.

## Native-refresh scheduling experiment

`docked-60s-native-refresh*.json` records a clean 60.013 s comparison. Only the
copied candidate's Metal display-link preference was changed from fixed 60 Hz
to the screen's maximum rate (120 Hz here, bounded by the presenter's existing
60–120 Hz range). Musical UI snapshots still updated at 60 Hz; workload and
acceptance thresholds were identical. The observed presentation mean was
114.10 Hz, with five missed 60 Hz periods among 6,719 samples. That ratio passed,
but the 41.662 ms maximum gap, 38.791 ms snapshot age and one audio overrun did
not. CPU p99 was 0.156 ms and GPU p99 0.581 ms; recorded commits/GPU completions
met their respective deadlines. Production remains fixed 60 Hz with latency 2.

The next isolated experiment keeps that production schedule and removes
unchanged listening-control/graph-target updates. It also tests opting the main
and floating windows out of AppKit's deep restoration, while retaining the
app's explicit workspace restoration and frame autosave. The latter remains
candidate-only until its behavior and performance are checked.

`docked-60s-layout*.json` records that clean 60.012 s experiment. It failed the
same three display gates, with zero audio overruns. Explicit frame save/restore
with `isRestorable=false` was checked separately in a hidden disposable window;
it remained functional, but the performance result does not justify adopting
the restoration change as a remedy. The small unchanged-control guards remain
valid UI work reduction and have a listener-state regression test.

The largest displayed gap ended at relative 42.441 s. Main ticks continued
through it at roughly 60 Hz (all under 5 ms). A frame completed its GPU work at
42.326 s for a 42.375 s presentation target, but appeared at 42.441 s; two earlier
ready frames were discarded. The display-link callback itself paused for
108.0 ms. This provides further evidence that app main-thread work does not
explain that particular presentation stall. No recorded command commit or GPU
completion missed its corresponding deadline. CPU/GPU p99 were 0.166/0.264 ms.

`docked-60s-layout-native*.json` combines that copied candidate with the native
refresh preference and latency 1. The 60.011 s run passed the missed-period
ratio and audio gates, but failed maximum presentation interval and snapshot
age. Median callback-to-presentation-target lead stayed 41.60 ms, identical to
native-refresh latency 2. The requested lower latency did not change this
windowed minimum. No production pacing or restoration setting was adopted.

The previous reports verify visible/occlusion state every workload tick and
hold a `ProcessInfo` user-initiated activity, but check app-active state only
during setup. They cannot retrospectively prove uninterrupted foreground/key
status. The next diagnostic adds bounded, timestamped app/window state changes
and sampled active/key/visibility/activity-token counts, without forcing focus
during measurement. This can distinguish possible background presentation
throttling from a fully foreground stall; prior failures remain failures.

`docked-60s-window-state*.json` records a 60.007 s run with **1,202 observations**
and no app/window state transitions: app active, a key window, main and graph
visible, and the process activity token held throughout. All inactive/no-key/
missing-activity counts are zero. This rules out observed background/occlusion
throttling for that run. It proves the activity was requested and retained, not
an independently measured kernel App Nap state.

Maximum presentation gap, snapshot age and audio passed on this occasion, but
the missed-period ratio still failed. Across the full drawable timeline, native
120 Hz display intervals were 3,438 × two periods, 84 × one period, 77 × three
periods and one × four periods. Most 25 ms intervals follow on-time 16.67 ms
display callbacks and early GPU completion, then appear about 8.3 ms after their
presentation target. This is evidence of compositor/display pacing irregularity
at the requested 60 Hz. The existing missed-period formula reports 13 misses;
its thresholds were preserved. The run does not establish a production fix.

## Metal System Trace and conservative draw bounds

A 70 s Metal System Trace capture reached its recording limit but exceeded the
wrapper timeout during finalization. Its incomplete trace could not be exported
(`Document Missing Template Error`); no conclusions rely on that trace. A
shorter **25 s trace of a 15 s workload** completed and exported successfully.
`docked-15s-metal-diagnostic*.json` contains the app's diagnostic timelines.
The raw system trace remains private in the ignored qualification directory.

Independent `display-surface-swap` timestamps match the app's drawable
`presentedTime` observations with a constant approximately 0.277 ms clock-map
offset. This confirms the observed presentation intervals are actual display
events, not late delivery of a main-thread measurement callback. The display
trace itself includes longer swap gaps; examples on the trace-relative clock:

| Desired display time | Actual swap time | Delay |
| --- | --- | --- |
| 5,763.209 ms | 5,785.063 ms | 21.854 ms |
| 10,767.339 ms | 10,801.693 ms | 34.354 ms |

The compositor-event tables for this process attachment were empty, so this
does not establish the underlying WindowServer cause. Profiler overhead also
prevents treating those gaps as clean qualification evidence. The app remained
active/key/visible with its activity token held, and audio had zero overruns.
Production pacing stays at 60 Hz / latency 2; final coherent docked and floating
runs are still required.

Current canvas source already culls against AppKit's dirty rectangle. Therefore
the measured ~7 ms graph draw does not by itself prove every offscreen card is
painted. Qualification now reports actual mean/maximum drawn-card counts and
the dirty-to-visible area ratio, so the final run can check full-layer overdraw.
No viewport-only skip was added that could leave stale/blank backing pixels
when scrolling.

Conservative paint bounds were corrected for wire-label overhang, frame/badge
strokes and hidden-route summary paths. The copied full interface suite passes,
including byte-for-byte bitmap crop comparisons for partial versus full redraw:
cable-label overhang, frame stroke, selected reroute/endpoint handles, and
skipping 42 node cards outside a small dirty region. These are correctness and
bounded-work improvements, not a claimed fix for the display pacing failures.

## Deferred layout follow-up

The measured 107.6 ms Time Profiler layout excursion remains actionable even
though some other presentation gaps occurred while the main loop was running
normally. Reviewing current source found graph-inspector refreshes that rebuilt
native controls after unrelated pattern revisions: the bus level/buttons,
the envelope's pattern menu, and the selected processor's parameter table.
These refreshes now retain unchanged controls while accepting the new document
revision. Genuine external value/catalogue changes still update the inspector;
effective parameter snapshots update their label without recreating the manual
slider. The originating setter for the historical profiler spike is not known,
so this is a bounded work reduction, not a proven explanation of that spike.

The next coherent qualification build also times AppKit's deferred
`NSWindow.layoutIfNeeded` passes as `windowLayout` in the existing bounded
main-thread timeline. It observes passes the window already performs and does
not add or force layout. Only `--ui-test` windows use the instrumentation;
ordinary musician windows retain the exact `NSWindow` class. A hidden isolated
native check passed both paths without displaying or activating its window.
This will correlate deferred layout with snapshot and presentation gaps in
both docked and floating runs. Cadence, frame latency and acceptance thresholds
remain unchanged.

The full copied-source interface suite passed from the exact coherent
`/tmp/screamseq-graph-review-20261001` checkpoint
(`/tmp/screamseq-layout-guards-interface.log`, exit 0). It includes parameter-row
identity during effective-only refresh, real manual/catalogue changes, pending
drag protection, retained bus text with current write revisions, stable pattern
menu objects, and actual external curve/catalogue updates. Those offscreen
checks establish refresh behavior, not native presentation performance.

## Coherent docked and independent-window measurements

The coherent review build passed all 89 CTest cases, the actual socket suite,
and the full AppKit interface suite before these clean measurements. Both
valid runs used the same preserved Resume song, 16 sounding channels,
50 graph nodes, and BlackHole 2ch at 48 kHz / 512 frames. Fixed 60 Hz / latency 2
and all acceptance thresholds remain unchanged.

| Run | Readable viewport / tracker | Samples / missed periods | Maximum presentation interval | Maximum snapshot age | Audio overruns |
| --- | --- | --- | --- | --- | --- |
| Coherent docked, 60.007 s | 9 full cards at 0.9× / 200 pt | 3466 / 29 | 49.996 ms | 35.359 ms | 0 |
| Coherent floating, 60.009 s | 9 full cards at 1.0× / 220 pt | 3470 / 17 | 33.333 ms | 31.720 ms | 0 |

Both **fail** the strict gate. Docked also fails the maximum presentation and
snapshot-age limits; floating fails only the missed-period ratio. All observed
app-active/key/visible/activity-held checks passed. The maximum deferred native
window layout pass was 3.594 ms docked / 3.725 ms floating. This run does not
reproduce the earlier 107.6 ms layout spike or prove that redundant controls
caused it.

The new draw counters rule out whole-document repaint as the observed cause:
docked draws 12 intersecting cards per draw, floating draws 10, from 50 total;
both maximum dirty-to-visible area ratios are approximately 1.0. Graph drawing
still costs p99 6.953 ms docked / 5.727 ms floating. Graph telemetry costs p99
6.223 / 5.200 ms, with a 13.220 ms docked maximum. A targeted transient redraw
and signal-name lookup investigation remains worthwhile; no viewport-only
drawing skip has been introduced.

The first floating attempt, `coherent-floating-60s.json`, is **invalid setup**:
the automatically chosen second display never yielded a visible graph window,
so measurement never started and duration is zero. Its setup-time frame counts
must not be used as performance evidence. A separate qualification-only Swift
delta now places the independent graph on the already proven-visible main
display and records both actual window/display geometries. The corrected
candidate retains the exact coherent engine archives and all other Swift files;
its manifest records the single changed source and archive hashes. The clean
result is `coherent-floating-main-60s.json` and companions. A separate 15 s
interactive setup probe is diagnostic only; native readability is shown in
`after/12-floating-main-readable.png`.

## Transient graph refresh candidate

The coherent docked and floating observations ruled out full-document painting:
only the viewport's 12 / 10 intersecting cards were drawn. They still repainted
all of those cards, including unchanged labels, at the graph telemetry refresh
rate. The next bounded candidate caches channel/group/processor port-name
prefixes at graph rebuilds, indexes host port readings, and invalidates only
changed meter, headphone-badge and recipe-activity regions. AppKit's actual
list of dirty rectangles controls text and geometry culling. Changed offscreen
meters are also invalidated; this is not a viewport-only drawing shortcut.

The copied full AppKit interface suite passed with byte-for-byte bitmap
comparisons between incremental and full redraws for meter level changes,
overload, stop, measurement removal/addition, group alias changes, listening
badges and overview titles. It also covers port-name changes and retired
indices/cache entries. The final recipe-activity bitmap case also passed in the copied full suite
(`/tmp/screamseq-transient-interface-final.log`). These are offscreen correctness checks, not presentation evidence.

The isolated candidate `ui-transient-5bb09084` retains the frozen controls build's
shared engine and App sources, with exactly five performance-source deltas:
`SignalCanvas`, `SignalDiagnostics`, the name-cache and activity invalidation
parts of `SignalGraphEditor`, and bounded signal-read/display trace phases in
`UIWorkTrace` / `main`. Its build manifest records the frozen base fingerprint,
changed source hashes and every linked static archive hash. The source used for
this experiment deliberately excludes subsequent graph feature work. Strict
acceptance thresholds and fixed 60 Hz / latency-2 presentation are unchanged.
The isolated optimized executable compiled and signed successfully. No native
result for this candidate is claimed yet.

## Clean transient-refresh measurements and matched control

The optimized candidate was measured in both layouts after all agents released
builds, tests, UI and audio. The same saved Resume fixture, BlackHole 2ch at
48 kHz / 512 frames, fixed 60 Hz / latency 2, and strict gates were retained.
Every measured run observed 16 sounding tracks, an active/key/visible application,
held activity assertion, no trace-ring loss and zero audio overruns.

| Workload | Real graph viewport / tracker height | Presentation samples / missed periods | Max presentation | Max snapshot age | Strict result |
| --- | --- | --- | --- | --- | --- |
| Optimized docked | 9 whole cards at 0.9× / 200 pt | 3469 / 16 | 41.661 ms | 38.284 ms | Fail: misses, interval, snapshot age |
| Optimized floating | 9 whole cards at 1.0× / 220 pt | 3470 / 6 | 41.661 ms | 30.302 ms | Fail: misses, interval |
| Pattern-only diagnostic control | No graph displayed / matched 200 pt | 3470 / 48 | 58.332 ms | 40.856 ms | Fail: misses, interval, snapshot age; never graph qualification |

The candidate's source fingerprint is
`e1b01f7ddd3b8671d6a79c6074ee1f304b447a9efab03920e4b4a1006ecc41cd`.
`transient-docked-60s*` and `transient-floating-60s*` preserve the actual reports,
frame/work/window timelines, build manifest and signed executable hashes.

The recurring graph work decreased relative to the earlier coherent observations:

| Main-thread p99 work | Earlier docked | Optimized docked | Earlier floating | Optimized floating |
| --- | --- | --- | --- | --- |
| Whole UI tick | 6.192 ms | 3.842 ms | 5.649 ms | 2.457 ms |
| Graph telemetry/display | 6.223 ms | 3.267 ms | 5.200 ms | 1.994 ms |
| Graph draw | 6.953 ms | 4.506 ms | 5.727 ms | 3.373 ms |

On the optimized docked run, bridge signal reading p99 was 0.631 ms and Swift
signal display p99 2.271 ms. Mean actual dirty-region area was 47.4% of the
viewport, with 8 cards painted per update on average instead of 12. Floating
painted 45.4% of its viewport and 8 cards instead of 10. Occasional activity or
ordinary structural refreshes still repaint the required larger areas. There
were no buffer-pool starvations, late command submissions or observed GPU work
finishing after its target. These observations support retaining the targeted
optimization; they do not make the strict timing gate pass.

The private control candidate `ui-pattern-control-7a65892c` is a derivative of
the optimized candidate with three explicitly recorded qualification-only Swift
deltas. It first sets up the identical dense song/layout, then replaces graph
content with a static diagnostic label and suppresses graph display, telemetry,
scope and graph-context refresh. The 1097 × 200 pt tracker geometry is asserted
throughout. Its report is explicitly `workload: pattern-only-control`,
`graphQualificationEligible: false`, `usesGraph: false`, `graphVisible: false`
and has zero graph draws and zero graph telemetry/draw/scope trace samples. The
raw minimum-card field retains the pre-control setup count; the actual visible
card count is zero and `controlInitialGraphCards` records that setup separately.
It is not a reduced-workload graph pass.

In this control, whole-tick p99 was 1.442 ms (maximum 3.175 ms), window-layout
p99 0.306 ms (maximum 2.648 ms), and snapshot preparation p99 0.237 ms. The worst
58.332 ms display gap around 39.322 seconds occurred while main-thread ticks
continued at roughly 60 Hz, each below 1.2 ms. Two already GPU-complete drawables
were not presented, a later drawable appeared 8.315 ms after its target, and the
display-link callback stream subsequently paused for about 50 ms. The separate
40.856 ms snapshot-age excursion followed a main tick gap of about 44.8 ms at
25.896–25.940 seconds without a recorded application callback accounting for it.

Therefore the remaining strict failure is **not dependent on graph UI work**.
This does not prove an operating-system-only cause: uninstrumented AppKit/Core
Animation work, render-thread scheduling and display-server presentation still
need separation. A scheduler/system trace of the private control is the next
useful diagnostic once the current UI/audio correctness checkpoint is complete.
No cadence change or acceptance exemption was made, and no further speculative
rendering optimization is justified by these results. All three disposable QA
processes stopped audio and exited; the shared UI/audio/build slot was released.

## Presentation handoff follow-up (existing trace, no new app run)

The strict result remains **FAIL**. Read-only analysis of the previously recorded
short Metal trace now includes its `ca-client-present-request`,
`display-surface-queue`, `CAMetalLayer` and `CAMetalLayer.Stalls` tables. These
are profiler evidence, not a clean qualification run. Raw exports remain private
in `/tmp`; no process/environment dump is included in this report.

A concrete delayed frame can be localized beyond ScreamSeq's submission:

| Trace-relative event | Time |
| --- | ---: |
| Last compositor surface queued before the gap, built-in display | 5.735488 s |
| ScreamSeq present request during that gap | 5.747825 s |
| ScreamSeq next present request during that gap | 5.764424 s |
| Next compositor surface queued | 5.780199 s |
| Desired display time of that surface | 5.763209 s |
| Actual display swap | 5.785063 s |

The compositor queue gap is **44.711 ms** and the swap is **21.854 ms** late.
The application continues handing off frames during this gap. The trace contains
71 ScreamSeq `DrawableLifetime` intervals in the stalls category (median
54.124 ms, maximum 74.924 ms), compared with a normal median of 49.937 ms.
No ScreamSeq `ClientDrawable` interval appears in the stalls category. Its normal
client intervals have median 0.287 ms and maximum 6.794 ms. Other processes are
excluded from these counts. These events narrow this example to presentation
queuing after the app handoff; they do not establish an operating-system-only
cause or explain every geometry freshness excursion.

The clean pattern-only control separately shows the same important ordering:
frames 2535 and 2536 finish on the GPU about 49 ms before their targets but are
never presented; frame 2537 appears 8.315 ms late. Its display-link callback then
skips approximately 50 ms. Thus that callback gap can be a consequence of delayed
display/drawable recycling, not proof that the render thread first failed to
produce a frame. Buffer-pool starvation remains zero.

The presenter's `command.present(drawable); command.commit()` sequence matches
[Apple's render-loop sample](https://github.com/apple/game-porting-toolkit/blob/main/game-porting-skills/skills/presenting-metal-drawables/references/render-loop-detail.md).
No unsupported explicit presentation timestamp, larger drawable pool, different
cadence or shorter frame latency is justified by this evidence. One separate
source concern is that the render worker's outer autorelease pool spans its whole
`CFRunLoopRun`; the delegate's inner pool does not cover framework allocations
made before entering it. There is currently no evidence of retained-drawable or
memory accumulation causing these stalls, so changing that pool is a hypothesis,
not an adopted fix.

The next bounded diagnostic is a 20–25 second **System Trace** of the unchanged
private pattern-only control, including compositor scheduling. The earlier
Time Profiler/Metal captures have `record-waiting-threads:0` and no scheduler
state trace, so they cannot distinguish a runnable/preempted thread from a
thread asleep waiting for a source or compositor response. Correlate the app's
existing monotonic frame/work timelines with thread state and the display handoff.
Passive, bounded main/render run-loop waiting markers can be added if needed;
they must not introduce another timer or force wakeups. Keep fixed 60 Hz,
latency 2, audio workload and all gates unchanged. Classify render scheduling,
compositor delay and independent main-thread snapshot delay separately before
making a production change. Final docked/floating clean qualification is still
required after any proven correction.

## Short System Trace control and capture limitation

The unchanged private pattern-only candidate ran for 20.008 seconds under a
25-second all-process System Trace recording. It observed 60.0004 mean FPS,
zero counted missed periods, maximum displayed interval 25.000 ms, maximum
snapshot age 16.432 ms, maximum display-link interval 17.131 ms, zero audio
overruns, and continuously active/key/visible window checks. There were no
failed control checks. This is a **short profiled control**, not a dense graph
qualification or evidence that the prior strict failure is fixed. The report and
bounded timelines are `pattern-control-system-diagnostic*`; the preserved
executable and fixture hashes are in its provenance file. No production source,
cadence, frame-latency, audio-default or acceptance setting changed. The private
application stopped audio and was quit normally after capture.

The raw System Trace successfully saved, but its default template uses windowed
recording (`Windowed (5 seconds)` in the template). Its saved run spans only the
last 10 seconds, and usable kernel thread-state events survive only in roughly
the final 0.21 seconds of that window—after the measured playback ended. Initial
long thread states at the window boundary are placeholders, not evidence that
WindowServer ran uninterrupted for those seconds. No stall cause is inferred
from them. The TOC's displayed start time also does not by itself establish the
rolling window's origin; the template's raw window-start metadata must be used
when correlating these events.

A future scheduler capture needs explicit retention settings (for example,
`--window 25s` or a private non-windowed template), checked before relying on its
events. A focused application attachment can reduce trace volume; compositor
coverage must still be explicit. Stopping the capture promptly after a detected
long frame would help retain both sides of a rare stall. The present capture did
not reproduce the failure and cannot justify any production scheduling change.
The existing strict graph qualification result therefore remains **FAIL** until
a current coherent app passes the full clean docked and floating workloads.

## Coherent observation checkpoint: clean docked and floating runs

The observation checkpoint (`cd5bde1e8db02b985a4483784dc8c1597666551f10a15cd64558a91d600485ed`)
was measured in two independently signed private copies, with the unchanged
16-track fixture and explicit BlackHole 2ch at 48 kHz / 512 frames. Parent
qualification had passed the complete 89-test suite and source-hash verification.
There was no profiler, UI automation, build or peer audio work during these
intervals. All timing and viewport gates remained unchanged.

| Measurement | Docked | Floating |
| --- | ---: | ---: |
| Duration | 60.013 s | 60.041 s |
| Whole graph cards / zoom | 9 / 0.9 | 9 / 1.0 |
| Unobscured tracker strip | 200 pt | 220 pt |
| Sounding tracks | 16 | 16 |
| Presented interval samples | 3473 | 3468 |
| Counted missed periods | 23 | 16 |
| Missed-period ratio | 0.658% | 0.459% |
| Maximum displayed interval | 33.333 ms | 58.329 ms |
| Maximum snapshot age | 30.372 ms | 32.808 ms |
| Pattern preparation p99 | 0.146 ms | 0.211 ms |
| GPU p99 | 0.180 ms | 0.321 ms |
| Audio callback p99.9 | 3.980 ms | 3.820 ms |
| Audio overruns | 0 | 0 |
| Result | **FAIL: missed ratio** | **FAIL: missed ratio and maximum interval** |

Both runs recorded zero inactive/no-key/activity-missing samples, zero vertex
buffer starvation, zero late submissions, zero GPU completions after their
presentation target, and no overwritten work timeline entries. The floating
viewport was inspected after the measurement: its nine full cards and socket
labels remain readable at 1.0 scale. Reports and bounded timelines are preserved
as `observation-docked-60s*` and `observation-floating-60s*`, with their signed
executable hashes and BuildInfo manifests. Signing each private bundle changes
its executable hash; both retain the same coherent source fingerprint.

The worst floating gap occurs at approximately 14.906 s. That drawable's GPU
work finishes 48.223 ms before its target, yet it is displayed 8.302 ms late.
The next displayed interval is 41.668 ms. Nearby maximum application tick work
is 9.690 ms and graph drawing 2.108 ms. This reproduces the early-submission /
late-presentation ordering; it does not establish a single cause for every
missed interval. A retained scheduler trace remains useful, but is deferred
while the next correctness checkpoint builds.

A separate, actionable main-thread regression is now measurable. Graph telemetry
p99 rose to 12.809 ms docked / 10.727 ms floating, while drawing remains bounded
at 3.628 / 2.785 ms p99. Subtracting the nested signal-read and signal-display
spans localizes the majority to `showActivity`: approximately 7.342 / 6.059 ms
median and 9.117 / 8.273 ms p99. New stage-label construction re-bridges the bus
array and scans duplicate names for every bus on each poll; per-copy owner
labels repeat those lookups. A targeted source candidate now caches the bus
name counts and stable-ID labels whenever graph data is replaced, then reuses
them for copy and stage activity. External rename and duplicate insertion /
removal regressions are included. This candidate has parsed successfully and
awaits the combined AppKit suite and a new coherent measurement. It is a measured
main-thread cost reduction, **not a claim that the presentation failure is fixed**.

The floating process stopped audio and quit normally (exit 0). The first docked
wrapper returned after preserving its report, so its execution environment
reaped the already-stopped child rather than allowing a native Quit; subsequent
wrappers retain their child until native Quit. No private app process remained
when the UI/audio slot was released. The source fixture and system audio defaults
were not changed.

## Bus-label cache: verified cost reduction, strict qualification still fails

The combined AppKit suite passed the new repeated-poll and external rename /
duplicate insertion / removal regressions. A new coherent follower checkpoint
(`33abffce47cad57ce364717de513884b75124ac999188a435aec19ba743fa601`)
was then measured for 60 seconds in each layout. Both used the same dense fixture,
50 nodes, 16 sounding tracks, explicit 48 kHz / 512-frame BlackHole device,
fixed 60 Hz presentation and latency 2. There were no concurrent compilers,
tests, profilers or UI automation during the measured intervals.

| Measurement | Docked | Floating |
| --- | ---: | ---: |
| Duration | 60.013 s | 60.007 s |
| Whole cards / scale / tracker strip | 9 / 0.9 / 200 pt | 9 / 1.0 / 220 pt |
| Samples / missed periods | 3462 / 30 | 3473 / 17 |
| Missed-period ratio | 0.859% | 0.487% |
| Maximum displayed interval | 41.666 ms | 33.333 ms |
| Maximum snapshot age | 48.584 ms | 38.488 ms |
| Audio overruns | 0 | 0 |
| Graph telemetry p99, before → after | 12.809 → 5.598 ms | 10.727 → 4.177 ms |
| Main tick p99, before → after | 12.509 → 5.715 ms | 9.944 → 4.061 ms |
| `showActivity` exclusive median, before → after | 7.342 → 0.461 ms | 6.059 → 0.340 ms |
| `showActivity` exclusive p99 after | 1.023 ms | 0.558 ms |
| Strict result | **FAIL: ratio, displayed interval, snapshot age** | **FAIL: ratio, snapshot age** |

The label cache removes the cost it targeted. The separate presentation problem
remains. The docked worst displayed gap again has GPU completion roughly
49.6 ms before its target, with nearby measured tick work under 3 ms. Snapshot
freshness has an independently visible main-thread gap: a snapshot at 26.1029 s
is followed by no main tick until 26.1561 s. A render callback at 26.1515 s sees
48.584 ms-old geometry. Only sub-millisecond window-layout entries appear in
that interval. Similar approximately 26-second main-loop gaps occurred in the
older graph-free control. This is evidence of missing main-loop progress, not
evidence that the cached label code or a particular OS subsystem caused it.

A useful next diagnostic is passive, bounded main/render run-loop wait-state
markers plus an explicitly retained scheduler capture. It should distinguish
an asleep run loop, a runnable thread waiting for CPU, and uninstrumented AppKit
work without adding a timer, forcing wakeups or changing frame cadence. No
further production scheduling change is justified yet.

Fresh reports and complete bounded timelines are `follower-docked-60s*` and
`follower-floating-60s*`. Both held foreground/key/activity checks throughout,
retained all trace entries, and completed with zero audio overruns. The docked
0.9 viewport was inspected after timing and its full card/socket labels were
readable. Both disposable apps were quit normally with exit 0; no private app
remained when the machine was released. The strict gate remains **FAIL**.

## Passive run-loop diagnostic: active main-loop gap reproduced

An isolated candidate reconstructed all 108 follower-checkpoint Swift/header
files by their manifest hashes, copied the coherent archives with SHA-256
records, and added only the four qualification instrumentation files. Its
`--ui-test-runloop-trace` flag installs before/after-wait and entry/exit observers
on the main and render run loops. They write fixed-size records into bounded
rings, add no timers and perform no wakeups. Normal app runs do not install
these observers. A standalone probe passed retention, chronological ordering,
before/after-wait presence, detach and reset checks; the combined AppKit suite
also passed with the instrumentation compiled.

The 35-second instrumented docked workload reproduced a 41.666 ms displayed
gap and failed the unchanged missed-period / interval checks, with zero audio
overruns. It is explicitly marked `cleanQualificationEligible:false`, and is
**not a qualification pass**. The reports and passive timelines are preserved as
`follower-runloop-system-diagnostic*`.

The main loop's 32,768-entry diagnostic ring wrapped, discarding 12,604 early
events; it retains measurement seconds 9.509–35.001. The render ring retains
the full interval without loss. The following finding falls wholly within the
retained range:

- At 26.0495 s, the main run loop records its early **after-wait** observer.
- Its next marker is the nested run loop's **exit** at 26.0752 s: a 25.711 ms gap.
- No instrumented application work span starts inside that interval.
- A render callback at 26.0745 s reads geometry prepared at 26.0405 s, aged
  33.922 ms.

This recurring main-thread gap is therefore outside its observed sleep interval.
The thread was executing uninstrumented event/AppKit work, blocked elsewhere,
or preempted; passive markers do not distinguish those possibilities. It is
not evidence of a missed timer wakeup alone. In contrast, near the worst
41.666 ms displayed interval at 30.799 s, observed main-loop waits stay below
15.625 ms and render-loop waits below 16.547 ms. The two failure mechanisms
must remain separate.

The accompanying System Trace requested explicit 40-second retention and a
40-second recording. Recording ended, but finalization failed to complete within
120 further seconds; the private recorder was terminated. Its trace package is
incomplete (no saved TOC), so no scheduler conclusion is drawn from it. Both the
recorder and the normally quit private app were confirmed gone. Repeating that
all-process capture unchanged is not justified. A focused Time Profiler capture
of the private app, correlated with the passive markers, is the next bounded
way to identify the uninstrumented main-thread stack. If it shows the thread
waiting rather than running, a focused waiting-thread sample can distinguish
that case. The opt-in marker capacity has been increased to 131,072 for future
captures; this affects diagnostic retention only. No production behavior,
presentation cadence or acceptance gate changed.

## Stoppage checkpoint requested by the musician

Further performance experiments are paused at the user's request. The prepared
app-only Time Profiler experiment was **cancelled before launch**. Its isolated
candidate successfully compiled with the larger 131,072-entry passive marker
rings, but no result is claimed for it. No ScreamSeq performance QA process,
profiler or pending performance compiler remained at handover.

The latest clean performance boundary remains the follower source fingerprint
`33abffce47cad57ce364717de513884b75124ac999188a435aec19ba743fa601`:
docked and floating each completed 60 seconds with 16 sounding tracks, nine
readable cards and zero audio overruns, but **both failed the unchanged strict
presentation/freshness gates** as tabulated above. Later integration work by
other agents is outside that measured build boundary.

Remaining performance work, when resumed:

1. Capture the prepared private app with focused Time Profiler and full retention,
   correlated with the passive main/render markers, to identify the uninstrumented
   approximately 26-second main-thread gap. Use a focused waiting-thread sample
   only if CPU profiling does not show the blocked/preempted interval.
2. Separately diagnose early-submitted frames that the compositor displays late
   or discards; do not attribute these to the main-thread gap without evidence.
3. Make only an evidence-supported correction, then rerun both full clean dense
   60-second layouts on the final coherent build. Keep all current timing,
   visibility, edit/Undo/save and audio gates unchanged.

The static bus-label cache and its external-change regressions are complete;
its measured cost reduction does not satisfy the outstanding strict gate.
