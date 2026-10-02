# ScreamSeq graph redesign — paused implementation report

2 October 2026. **Paused at the user’s request. Implementation is substantially complete; final native visual qualification remains open.**

This report consolidates the current implementation of items 2–6 in the
[stoppage report](../2026-10-01-graph-resume/STOPPAGE_REPORT.md) and the
[approved graph specification](../../SCREAMSEQ_GRAPH_INTERACTION_SPEC_2026-09-24.md).
It replaces the earlier chronological scope descriptions for the purpose of
this summary. It does not constitute a new audit of every tracker workflow.

The user deferred the strict FPS gate and Windows desktop/device checks.
Windows native CI remains part of this pass. This report deliberately distinguishes
passed source checkpoints from later corrections and deferred checks. The
[screenshot gallery](index.html) contains 22 images from the actual native walks.

## Implemented behavior

| Area | Current behavior |
|---|---|
| Navigation and selection | Song/group breadcrumbs, Back bridges, remembered views, node search and typed socket search. Filters retain labeled links to hidden dependencies; choosing one reveals the relevant nodes without moving their saved positions. New nodes and wholly offscreen filtered views are revealed. Entering a known reusable instance preselects that exact observed copy, including its channel and role. |
| Layout and presentation | Master stays downstream-right when its position is automatic. Saved manual positions remain authoritative. Multi-selection moves together; Arrange selection/all is explicit. Processing groups, visual frames/comments, reroutes and collapsed cards have distinct meanings. Collapsed cards retain connected sockets, names and meters. Stage outlines describe occupied row/persistent/ordinary stages without inventing new editable DSP objects. |
| Direct editing | Double-click/Return opens the selected object. Contextual Add, cable-first insertion, socket dragging, endpoint repatching, keyboard equivalents and exact numeric controls share stable target identities. Fields commit directly. One continuous range gesture or compound graph edit makes one chronological Undo entry. Untouched focused numeric fields no longer swallow document Undo. |
| Audio routing | Main, detector/sidechain and auxiliary sockets use the actual provider catalogue. Fan-in sums and fan-out remain explicit. Rack plugin-to-plugin cables do not change insert ownership. Moving an insert chain and adding another Main input are separate choices. Output/input slice identities, gains, enabled states and bends survive repatching, Undo and save/reopen. |
| Instrument audio inputs | Plugin instruments with physical audio inputs participate in dependency scheduling. Main/aux inputs and current-block follower dependencies are supported; generator-only plugins are not given fictional audio inputs. |
| Aggregate graph-stage ports | A channel graph stage has typed auxiliary sockets. An input is shared across its prepared row/persistent/ordinary copies; outputs combine their audible/tail contributions. These are labeled aggregate ports, exclude sample-instrument copies and never masquerade as the currently inspected individual copy. |
| Note/MIDI routing | Separate Notes sockets forward plugin-instrument events. Routes have stable identities, MIDI-channel mapping and enabled state. Adding or restoring a destination waits for the next note-on; removing or disabling one releases only its owned notes/pedals. Editing an implicit assignment materializes its replacement and suppresses the implicit cable atomically. Mixed audio/event cuts are one edit. |
| Durable trigger sources | Removing an original plugin preserves an instrument's trigger identity while surviving note routes still need it. Explicit sample-mode conversion is separate. Inactive routes remain visible and editable instead of disappearing or forwarding sample notes accidentally. |
| Detach and Cut | Multi-effect detach retains a loose chain's internal order, identities and auxiliary branches. A branched recipe detach asks for the intended healing pair when ambiguous. Cut removes exactly the selected connection: cutting an insert Main input does not heal around it; cutting the Master terminal silences the terminal while upstream processors and observations continue. |
| Bypass and mute | Rack and reusable processors use host bypass, not a vendor parameter approximation. Recipe and song groups use real boundary dry mappings with latency compensation; internal processors remain warm. Ambiguous dry paths require explicit mappings. Source mute suppresses the entire contribution, including a nonzero minimum, while its oscillator, random sequence, gates or follower continue advancing. |
| Reuse and presets | Make independent clones the selected channel or instrument use. For a channel it keeps related Row/Start/Stop/Amount bindings together. Recipe selection copy/cut/paste/duplicate carries internal cables and groups, with explicit pattern mapping across songs. Rack duplicate creates a fresh independent processor without copied note assignments or cables. Preset actions preserve routing and host bypass; compatible effect presets can change during playback. |
| Parameter modulation | Host-owned controls and named parameter ports support zero-depth connection creation, independent per-source range segments, exact values and explicit quantization for stepped targets. Audio-to-parameter drops offer an envelope follower. Followers can use bus taps, actual rack outputs or declared aggregate-stage auxiliary outputs. |
| Parameter values and provenance | Editable manual base values are separate from effective-value snapshots; LFO motion does not move the editable knob. Existing pattern commands, envelopes and recorded points appear as references to their real data, with exact Edit source/Back navigation. Parameter activity retains its measured contribution history. Duplicate plugin names and unavailable copies cannot silently select another target. Recorded-point edits and their Undo/Redo now support playback. |
| Signal observation | Audio ports and cables expose bounded engine observations, scope/spectrum, overload and latency information. Exact reusable-copy selection addresses that copy's actual PCM/control data; root route readings describe the actual post-gain/post-delay contribution. Notes use event/held-ownership counters, not audio meters. Pending, retired, unavailable and stopped observations are distinct from measured zero. |
| Temporary Listen | Listen uses a session-only monitor tap with a persistent indicator and a direct return to the normal mix. It does not rewrite the song's mixer or change the system audio route. |
| Live structural changes | Recipe topology, rack routing, source/assignment/sample binding changes, dynamic latency and supported port activation prepare off the callback. Retained processors advance once; route morphs or latency-aligned dry transitions keep the old graph valid until adoption. Failed or over-budget preparation leaves the previous audible graph, document and history intact. |
| Shared contracts | Musical data, validation, APIs, Undo and persistence are shared. Mac and Windows adapters include revision guards, strict endpoint handling and atomic publication. Native UI and device qualification remain platform-specific. |

The accepted keyboard/scope decisions remain unchanged: **Space controls
playback, Tab navigates nodes, and editing a reusable definition updates all
uses**. Make independent is explicit. Note routing forwards plugin-instrument
notes only and never synthesizes a held note for a newly connected destination.

## Controls removed or relocated

Capabilities remain reachable through object context menus and the graph's
stable command catalogue in ⌘K. Unsupported contexts show a reason; commands
without a selected target can offer a target search.

| Previous control or friction | Current primary path | Retained precise/secondary path |
|---|---|---|
| Persistent node dropdown plus Open/Remove row | Select the card; double-click/Return to open; Delete or object menu to remove | Find node, Open selected and Remove selected in More/⌘K |
| From/To dropdowns followed by Connect | Drag between sockets or use a socket's Connect to search | More/⌘K → Advanced numeric patching |
| Separate Apply for graph properties | Edit in place; use unified Undo | Exact numeric inspector fields remain |
| Equal-weight secondary button rows | Contextual actions beside the selected object | More, right-click and ⌘K catalogue |
| Plugin select-then-bypass/open/preset operations | Object context, inline processor controls and direct open | Stable bypass, preset save/load, duplicate and owner commands |
| Grouping confused with presentation frames | Separate processing-group and visual frame/comment actions | Group dry paths, export, ungroup and presentation actions in context/⌘K |
| A selected source redirected to an unrelated rack bypass chooser | Source M/mute action affects that source; group/processor actions retain their own semantics | With no selection, a context-aware target chooser |
| Remembering a parameter/copy while changing panels | Automate, Inspect effective value, Edit source and last-touched bridges preselect exact context | Back to graph/cable/source restores the captured view and selection |
| Effect-range editing only in a form | Distinct source segments beside the parameter; drag one to edit only it | Exact minimum/maximum and discrete-mode details in the inspector |
| Hidden dependencies silently omitted by a filter | Labeled clickable boundary links | Reveal dependencies and explicit Fit/Arrange commands |
| Forced all-node arrangement to find a new item | Reveal/frame the relevant new or selected nodes | Arrange selection/all stays explicit and preserves manual layout otherwise |
| Notes treated like generic audio cables | Typed Notes sockets and route inspector with exact ownership counters | MIDI channel, forward/disable, restore assignment and mixed-cut commands |

## Current scope boundaries

- **External routing to one selected reusable copy is not implemented.** Exact
  copy observation and editing its shared definition are available. Aggregate
  outer-stage audio routing is available. Neither substitutes for an external
  cable targeting one particular dynamic row/persistent/ordinary/instrument copy.
- **Instrument opaque preset replacement remains stopped-only.** Compatible
  rack effect replacement is live. The held-note policy for swapping an
  instrument's opaque state has not been chosen; no implicit retrigger or note
  transfer is introduced.
- **Graph clipboard is the reusable-recipe clipboard.** Song rack duplication
  is separate. Arbitrary root graph clipboard operations and library export of
  mixed song-control groups remain explicitly unsupported. The contiguous-chain
  exporter also refuses cut/direct boundaries it cannot preserve; it does not
  discard those routes or controls.
- **Audio feedback is rejected.** A normal delay plugin does not make a
  scheduling cycle valid. There is no implicit feedback-delay node.
- **Physical channel layouts are explicit and bounded.** Providers expose
  deterministic stereo-pair/odd-mono slices, with a saved physical-layout
  fingerprint. An incompatible changed vendor layout rejects rather than
  retargeting cables or truncating channels. Capacity limits still apply; they
  were not raised to make a stress case pass.
- **Vendor GUI knob drops are not universal.** The supported host parameter
  surface and touch-a-knob → Last touched parameter bridge provide reliable
  targets. This report does not claim every AU/VST custom editor supports native
  parameter hit testing or a drag overlay.
- **Readouts retain their measurement meaning.** Effective parameter text is
  labeled as a snapshot. Control min/max values describe sampled quantum
  endpoints, not guaranteed extrema between samples. Exact signals that are
  unavailable are not replaced with a nearby bus/host-port reading.
- **Source dry runs validate the model and declared ports.** Actual prepared
  publication additionally validates follower dependencies before committing.
  The source API does not claim a full vendor/runtime preparation during dryRun.

The instrument-input render fixtures and routed-note fixtures pass separately.
Relative note/audio onset for a plugin simultaneously generating MIDI notes and
receiving a nonzero-latency audio input remains a qualification boundary; no new
onset-compensation policy is claimed for that combination.

## Open user policy questions

These are decisions, not silent defaults inferred from implementation:

1. **Live instrument preset changes:** should existing notes/tails remain on the
   old instrument until released, be explicitly released before switching, or
   be retriggered on the replacement? Until a policy and corresponding tests
   exist, opaque instrument preset changes require stopped playback.
2. **External ports for reused graphs:** should a cable address the combined
   active channel stack or a particular copy? Aggregate-stage ports are already
   explicit. Specific-copy routing additionally needs a decision about dynamic
   copy lifetime and what a cable means while that copy is absent. No expansion
   of that policy is authorized by the exact-copy observation selector.

## Evidence and pending final qualification

The [journey ledger](JOURNEY_WALK.md) owns actual actions, panel switches,
confirms and screenshots. Its interim checkpoints must not be counted as a
walk of a later executable. **No new before/after numbers are inferred here.**

### Corrected automated checkpoint: `e42639558`

The corrected source was committed as
`e426395580c907e62ddf4fbd93bf52996a6e4a14`. Its build fingerprint is
`0af269e2d74aeba5acc490d43caa7d8962460aa824ae61aebe76de84ed379e28`.
The [before](logs/corrected-build-start.json) and
[after](logs/corrected-build-after.json) manifests match across all **945**
recorded inputs. They record parent `b3f5f5952` because the build preceded the
commit; this does not identify a different source snapshot.

The immutable `verified-8cf8e30a` QA copy retains executable SHA-256
`21a7f763235c37d605197f6bf26ba0d765dc326e7858f7912ab36d04629446a0`.
Its recorded hash was rechecked and `codesign --verify --deep --strict`
succeeded. [Checkpoint identity](logs/corrected-checkpoint.json) records the
bundle, fingerprint, signature check and retained log hashes. This identity
belongs to the corrected checkpoint, not subsequent placement/navigation or
group-insertion fixes.

| Automated check | Corrected checkpoint result |
|---|---|
| Full native build and packaging | **PASS** — [build log](logs/corrected-build.log); unchanged source manifests and verified QA signature |
| Full shared/macOS/portable-adapter CTest | **102/102 PASS**, 67.80 seconds — [results](logs/corrected-ctest.log) |
| Actual packaged-app local API socket | **PASS** — [app socket](logs/corrected-socket-app.log), including typed stage cables/followers, note ownership, cuts, Undo/Redo, native save and strict/stale/dry-run guards |
| Separate native-host local API socket | **PASS** — [host socket](logs/corrected-socket-host.log), including bounded non-finite-response failure and continued socket/document use |
| Actual app workspace regression | **5/5 repeated passes** — [workspace log](logs/corrected-workspace.log), including pin/focus/navigation, rejected-edit atomicity and deferred Follow |
| Actual CoreAudio virtual loopback | **6/6 PASS** — [loopback log](logs/corrected-loopback.log); each case captured 96,000 stereo frames at 48 kHz / 512 frames, maximum and RMS PCM error **0**, callback overruns **0** |
| Actual NSApplication Quit | **PASS** — [shutdown log](logs/corrected-shutdown.log); pending main-thread plugin work, recovery drain and retained-session teardown |
| Native Windows x64 CI | **36/36 portable + 47/47 native workers PASS** on the same commit — [Windows report](WINDOWS_NATIVE.md), including the actual VST3 stage/follower worker |

The two socket suites use private discovery and disposable documents, without
windows or audio output. The loopback cases cover dry playback, AU effect,
VST3 effect plus automation, VST3 instrument plus automation, AU instrument,
and graph copies plus fractional pattern commands. They ran with concurrent
build work: these are **loaded functional continuity/PCM checks**, not a
quiet-machine capacity or performance measurement. Virtual routing does not
qualify physical DAC latency or speaker sound. Shutdown is a separate private
application process, not a command sent to the musician's session.

### Runtime and targeted followup evidence

The corrected commercial-plugin run passed **4/4**: Replika 1.6.1 and Serum 2
2.1.5, each in AU and VST3, for 10 rendered seconds at 48 kHz / 512 frames.
Every case requires nonzero output and checks live routing, bypass and rejected
preparation retention; Replika also checks removal/restoration and Serum checks
note ownership. [Commercial results](COMMERCIAL_PLUGINS.md) and the
[manifest](logs/commercial-final/evidence.json) identify the installed plugins,
source scope and executable. The narrower C++ source hash in that manifest is
not the app BuildInfo fingerprint. These offline runs opened no device and do
not establish vendor-private realtime safety, commercial capacity or every
plugin/preset combination. They predate the later Add/group-boundary helper.

The first sanitizer run found a real projected-bus container overflow. The
adapter lookup and reservation were repaired, with a high-bus-count regression;
[the original failure](logs/sanitizer-before-adapter-fix.log) is retained.
The repaired runtime passed [12/12 ASan/UBSan suites](logs/sanitizer-runtime-tests.log)
in 103.87 seconds. Four hosted executables were then relinked to the corrected
archive and [passed again, 4/4](logs/sanitizer-relinked-host-tests.log) in 25.04
seconds, so all 12 applicable results cover the repaired source.
[Source/archive/executable hashes](logs/sanitizer-runtime-sha256.txt) identify
that checkpoint. Flags were `-fsanitize=address,undefined
-fno-omit-frame-pointer`, with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. No leak or thread-sanitizer
claim is made. Normal test builds supply the callback allocation/free/lock
audits; sanitizer stubs do not supply that evidence.

The final stage-follower targeted checkpoint passed **12/12** normal suites:
[native results](logs/stage-follower-native.log) and
[adapter/model results](logs/stage-follower-adapters.log).
[Live audio routing evidence](LIVE_AUDIO_ROUTING.md) records actual AU/VST3
instrument inputs, late group dry-path compensation and aggregate-stage
followers across rates and block partitions, with independent PCM oracles.
The [work log](WORKLOG.md) retains note ownership, stock-engine PCM,
multichannel provider and source-migration evidence at their recorded revisions.

Copied AppKit suites are source-scoped evidence, not native walkthroughs. The
stage-follower suite passed before the corrected build. A later full copied
suite also passed the append-placement regression, including actual Shift+A
key dispatch, preserved saved positions and distinct explicit-canvas/cable
placement: [log](logs/append-placement-interface.log) and
[snapshot](logs/append-placement-interface-snapshot.json). The subsequent `261b15387` native rewalk verified grouped Add and placement;
final source matching for later fixes is recorded below.

### Followup integrated checkpoint: `261b15387`

The grouped-Add and placement corrections passed a coherent full build, strict
signature verification, **102/102 CTest suites (73.87 seconds)**, the actual app
socket suite and the workspace suite. The first compile was rejected because
a source file changed during compilation; the unchanged retry completed.
[Build](logs/followup-build.log), [CTest](logs/followup-ctest.log),
[API](logs/followup-socket-app.log), [workspace](logs/followup-workspace.log),
[identity](logs/followup-native-checkpoint.json). Native Windows CI then passed
**36/36 portable and 47/47 native workers**, including the VST3 PCM worker.
[Windows results](WINDOWS_NATIVE.md).

The private native rewalk verified grouped Add, inline reverb controls/bypass,
group boundary bypass, recorded-point live edits/history, Notes ownership and
MIDI mapping, all five input/output slices of a wide VST3 fixture, and actual
instrument audio inputs. Instrument recipe editing also worked using an
explicit copy selection. Its **275 successful transport samples over 1264.959
seconds** all reported playing, no engine/plugin fault and zero overruns; there
were no read errors. The final sample had 63,097,856 rendered frames.
[Monitor summary](logs/native-261b-transport-summary.json). This is sampled
editing continuity at BlackHole 2ch / 48 kHz / 512 frames, not a strict timing
or presentation qualification.

The walkthrough exposed four final corrections: selected recorded-point detail
refresh; instrument navigation through active filters; choosing an instrument
processor copy instead of an unrelated ordinary copy; and detaching a group
without retaining vanished boundary dry routes. A source review also found that
auxiliary socket metadata omitted accepted direct/stage cables and follower taps.
The new regression also exposed a valid silent instrument/follower tap being
treated as missing audio during a transition. The host now supplies its existing
zero scratch buffer for validated silent taps; no callback allocation or new
storage is needed. These fixes have focused regressions; final checkpoint
results follow below.

### Final stopping-point build: `684a5023a`

The final source commit is `684a5023acda1a1823341d2387ee1a8040e2ef91`.
The full C++/Swift build and packaging passed with all **943 recorded inputs
unchanged** from start to finish. The background development bundle passed
strict deep signature verification. [Build log](logs/paused-build.log),
[before manifest](logs/paused-build-start.json),
[after manifest](logs/paused-build-after.json), and
[executable identity](logs/paused-checkpoint.json) preserve the checkpoint.
Its fingerprint is `f7af1f6a02d2ddbe38c31e1761ca48c4aaf5beac52c158bad76f5b287d691116`;
its signed executable SHA-256 is
`2f45a8fbc5c6ee85e095e4b0ef5a8d936fbc615ac2c415f39aa5a13d568f9e95`.

The final integrated run passed **102/102 CTest suites in 138.90 seconds**,
plus the actual packaged-app local API and workspace suites. The signed
executable stayed unchanged across the checks. These are functional regression
results, not a machine-capacity benchmark. [CTest](logs/paused-ctest.log),
[app API](logs/paused-socket-app.log), [workspace](logs/paused-workspace.log).

The final copied AppKit suites passed recorded-point refresh and instrument
navigation/copy selection regressions. **All 158 Swift/bridge inputs match the
current source**: [source check](logs/final-appkit-source-match.json),
[recorded-point suite](logs/recorded-form-interface.log),
[instrument navigation suite](logs/instrument-navigation-interface.log).
This is automated AppKit evidence, not a native visual rewalk.

The final actual AU/VST host and API targets passed **4/4 suites in 15.50 seconds**:
[mixer, multi-bus, note routing and source migration](logs/port-membership-final-tests.log).
The new port-membership fixtures check direct/stage cables and follower taps,
rejected candidates, removal, explicit activation preservation, stable saved
state and API Undo/Redo. The original silent-tap failure and transition diagnostic
are retained, alongside the [tested source identity](logs/port-membership-final-source.json).

### Remaining integrated qualification

One interval in the corrected private native walkthrough ended with playback
stopped at elapsed 384.398 seconds, row 62, after plan 6 had been adopted and
continued rendering. The monitor recorded **no callback overruns, plugin fault
or reported plugin error**, but the cause of the stop remains unresolved and
was not reproduced by a later Add check. No recorded Stop/Space action has
been established for that interval. **Uninterrupted playback across the whole
native walkthrough is not claimed.** Automated continuity passes do not resolve
that observation or attribute it to a particular edit.

| Final acceptance item | Stopping-point result |
|---|---|
| Final integrated build/source/executable/signature | **PASS** — `684a5023a`, unchanged build inputs, strict signature and retained executable identity |
| Final full regressions and packaged-app checks | **PASS** — 102/102 CTest, app API socket and workspace checks on `684a5023a` |
| Final copied AppKit source match | **PASS** — all 158 tested Swift/bridge inputs match; native visual rewalk remains separate |
| Native Mac journeys/screenshots | **PARTIAL** — 22 retained images and measured journey ledger; last fixes and listed full-journey checks remain open |
| Final Windows x64 build/native worker CI | **IN PROGRESS** — [run 36960036036](https://github.com/rewbs/ScreamSeq/actions/runs/36960036036) on exact source `684a5023a`; previous `261b15387` run passed 36 portable + 47 native suites |
| Windows desktop/HWND/WASAPI/device walkthrough | **DEFERRED BY USER** — no desktop pass inferred from CI |
| Strict FPS/presentation gate | **DEFERRED BY USER** — existing failed gate is not relabeled as passed |

The final private native fixture was saved and explicitly stopped through its
PID-specific API. With the desktop unavailable, only that QA process was
terminated with SIGTERM; this is not counted as another normal-Quit test.
The earlier NSApplication Quit test remains the relevant shutdown evidence.

All listed tests used isolated processes/builds and disposable fixtures. No test
logs, manifests or results here imply replacement of the musician's process,
project or bundle, or a change to system audio defaults. The private QA UI/audio session is closed; no local qualification process is
left running at handoff. Only the dispatched remote Windows CI run remains active.

Archived Windows output and one sanitizer log have normalized line endings and
trailing whitespace. [Original and normalized hashes](logs/log-normalization.json)
preserve the distinction; diagnostic text and test results are unchanged.
