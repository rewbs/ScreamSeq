# Integrated graph completion — native journey ledger

This ledger separates completed native checkpoint walks from remaining final
rechecks. Automated AppKit checks are recorded separately in GRAPH_ACTIONS.md.
No pending row below constitutes a native UI pass. Windows desktop/device walks are deferred at the user's request;
native Windows CI is still required. The FPS gate is excluded from this goal.

Count each click, double-click, drag, shortcut and committed numeric entry as an
action. Count panel/graph-depth switches separately, and separate redundant
confirmation buttons from Return that completes text entry. Fixture setup through
the API is documented separately and does not stand in for walking the journey.
Historical counts come from the linked paused checkpoint, not reconstructed guesses.

| Journey | Historical successful actions / switches / confirms | Latest native path, count and screenshot |
|---|---|---|
| Add an effect to channel 3, tweak and compare bypass | Add only: 3 / 0 / 0; whole path not comparable | e426: grouped Track 3 Add rejected (02); Track 4 Add/tweak/bypass **6 counted actions** (3 + 1 + 2), screenshots 03–04. 261b grouped Add/tweak/bypass passed: **6 actions / 0 switches / 0 redundant confirms**, screenshots 01–04. |
| Insert EQ into a selected cable during playback | 4 / 0 / 0 | e426: **4 intended actions + 2 search-retry actions**; Undo/Redo and bypass exercised, screenshots 05–06. |
| Connect channel 1 to compressor Main and channel 2 to Sidechain | Detector connection only: 1 / 0 / 0 | e426: Track 1 insert supplies Main; detector keyboard path **4 actions + one early-typing retry**, then Undo and **one pointer drag**; screenshots 07–08. |
| LFO to parameter, set range, mute source and return | LFO setup: 9 / 0 / 0; mute not measured | e426: existing source M mute, **2 numeric range entries**, exact rack provenance bridge and source double-click; screenshots 09–11. Complete creation-path recount PENDING. |
| Create a quiet send/return and edit its gain | 5 / 0 / 0 | e426: **5 actions**, output context → Add compatible node → search Return → Return → gain −18; screenshot 15. |
| Diagnose a silent path and an overloaded path with exact scope/Listen | Mute: 3 / 0 / 0; overload: 10 / 0 / 0 | e426: send scope, −96 silence, Undo, +24 rejection, +12 acceptance, Listen/Stop verified (16–20). Confirmed overload diagnosis remains PENDING. |
| Inspect independent reusable processor copies and their control sources | No comparable baseline | e426: entering Track 6 preselects its exact copy; Inspect and Back preserve identity (21, 23). No aggregate action count captured. |
| Group, name and export a chain; bypass its boundary | Export: 6 / 1 / 0; bypass not measured | e426: existing recipe group M bypass verified (22). Fresh group/name/export path remains PENDING on final build. |
| Duplicate/copy/paste a recipe selection and make one use independent | No comparable baseline | e426: Make independent **1 action**, Track 6 n31 versus Track 5 n25 (24); recipe group copy/paste and Undo/Redo verified (25–26). No full-path count captured. |
| Save a processor preset and reuse it on another channel | No comparable baseline | e426: More → Save/native file; other compressor context → Load/file double-click succeeds live (14). No aggregate action count captured. |
| Edit an instrument graph and return to the pattern context | 9 / 2 / 0 | 261b: assign shared recipe, enter I1 Warm pulse, manually select its actual copy, edit Threshold −20, and return through Song with bus/view restored (17–18). Instrument command reveal and automatic copy selection need rebuilt native recheck; no full action count captured. |
| Repatch, Undo/Redo, detach a branched group and cut selected cables | Repatch/history only: 3 / 0 / 0 | 261b Notes cable Delete/recreate passed; root group detach rejected its explicit dry map (12). Shared fix and Mac/Windows adapter tests pass; final native detach/repatch/branched-heal recheck pending. |
| Patch Notes from keyboard and pointer; map MIDI channel and inspect activity | No comparable baseline | Native keyboard route, immediate MIDI 3 mapping and activity verified at journeys-b90df8bc (04); 261b captures 09–11 retained. No complete action count captured. |
| Add/remove a note destination while held notes sound; Undo waits for next note | No comparable baseline | journeys-b90df8bc: forwarding off releases held note; Undo restores route without retrigger, next onset delivers (05 and activity JSON). 261b captures 06–08 retained; no aggregate count captured. |
| Edit recorded automation during playback and follow its parameter provenance | Previously stopped-only | Live inline edit and Undo/Redo verified at journeys-b90df8bc (06). 261b edit −27→−30 updates table/effective value but exposes a stale selected-point form (05); copied AppKit fix passes, rebuilt native form recheck pending. |
| Route every channel-pair slice of a multichannel plugin without hidden truncation | Previously unsupported | 261b: all **5 inputs and 5 outputs** connected; input setup **4 keyboard actions + 4 drags**, output 4 by keyboard and outputs 1–3 by **3 drags**, output 0 retained. Physical instrument Main **4 actions**, auxiliary **1 drag**; screenshots 13–16. |
| Regression: write/audition melody, change instrument, use global transport and navigate back | Broader September journey report | e426: melody entry **8 actions** (01); playback and later restart exercised. Full instrument-change/return regression remains PENDING. |

The final walkthrough records the exact build/source fingerprint, private fixture,
explicit device, transport state, action sequence, visible feedback, screenshot,
and any failure/retry. Capability retained in context menus or the command palette
must be exercised there at least once; a passing model/API test is insufficient.

## Native rewalk in progress — checkpoint 0e55851e1

Private bundle `ScreamSeq Graph Completion QA`, PID 33092, launched with
`--inspection --automation`. Device explicitly selected through Audio settings:
BlackHole 2ch at 48 kHz / 512 frames; system default was not changed. Fixture is
the disposable Midnight Circuit demo. This is an interim checkpoint; remaining
runtime edits are not covered by it.

- Show Graph through Cmd-K (search `graph`, Return); select Track 3 in its filter.
- Add compressor: Add, type `compressor`, Return — **3 actions / 0 switches /
  0 redundant confirms**, after selecting the channel. The inspector opens inline.
- Edit threshold from -18 to -24 in place, then toggle bypass on/off. Including
  one Space playback start, the add/tweak/compare path is **7 actions / 0 switches /
  0 redundant confirms** under this ledger's numeric-entry convention. Separate
  setup is the 2-click channel filter and initial Graph panel opening.
- Native UI and API both confirm manual -24 and bypass changes. The song ended
  naturally during the first pass; loop was then enabled for subsequent live walks.
- Found a placement regression: an automatically inserted Compressor is right of
  the implicit Master, causing a backward output cable. This is being fixed while
  preserving explicit layouts. Screenshot `channel3-inline-comparison.png` is a
  failure record, not final presentation approval.
- Inline parameter `Effective` text is a cached host read. It remained -18 after
  the manual value changed although a fresh API read returns -24; its tooltip
  explains the snapshot, but the visible label is too easy to mistake for current
  data. Further inspection is required before completing diagnostics qualification.

QA evidence currently lives under the separate ignored build directory
`bin/mac-graph-fluency/qualification/copy-notes-1e9c1630/`; final accepted images
will be copied into this report's gallery after the affected paths are rechecked.

### Additional checkpoint walks (not final acceptance)

- Keyboard Main/Sidechain patching while looped verified routing and unified
  Cmd-Z / Cmd-Shift-Z without stopping. Search ranked a hidden node ID ahead
  of the visible Track 2 title; choosing the second result added one action.
  Title-first search ranking now has a passing copied AppKit regression.
- Threshold → Inspect opened Parameter activity with the correct parameter and
  live value. Its narrow dock truncated mode/value controls and lacked a clear
  return action. The new split rows and context-preserving Back to graph passed
  copied AppKit tests; the rebuilt native presentation is still pending.
- Save plugin preset from Track 3 succeeded through More → Save → the native
  save panel. Added a second compressor on Track 4 in three actions.
- Reusing that preset during playback correctly retained the old audio but
  rejected the edit as stopped-only. This is an unfinished live workflow, not
  a successful preset journey. Compatible rack preset replacement is underway.
- An earlier preset-path error was an automation-tool targeting error: clicking
  the AX item selected a different visible file. Choosing the exact preset via
  the native Go to Folder field reached the genuine stopped-only rejection.
  No file-path application defect was established. Pointer qualification remains
  pending a fresh UI-control mapping; keyboard actions were reliable.

### Copied native UI recheck: journeys-b90df8bc

Private PID 52263; BlackHole 2ch, 48 kHz / 512. This bundle contains
updated Swift and the first passing rack-preset archives, before the final direct
route/instrument-input scheduler. Its source/archive fingerprint is
`5553ab4fbcb2494fca7060324f039a9d5861b9acdb2d82f40b2ac3c3bd18c6e5`;
`snapshot.json` records the bounded manifest.

- Threshold edited -24→-21: its editable base and Last read both update.
  Inspect effective value opens the correct parameter. Back to graph returns to
  Track 3, its selected Compressor, saved viewport and zoom. Screenshot
  `01-parameter-activity.png` verifies compact dock controls. A long status still
  truncates; wrapping has subsequently been corrected and awaits presentation.
- Track 4 preset reuse: More → Load preset → native chooser → Open applies -24
  while playback continues. Cmd-Z restores -18 and Cmd-Shift-Z restores -24.
  Screenshot `02-live-preset-track4.png`. Native file chooser selection was made
  with Go to Folder to the exact saved QA preset. Read-only transport evidence
  confirms active/stable audio, no plugin failure and zero overruns at that point.
- LFO creation, parameter drop, two range edits, mute and unmute: **9 actions /
  0 panel switches / 0 redundant confirms** from the selected Track 4 graph.
  The new wire starts at zero depth; min/max -0.1/+0.1 save on Return. Screenshot
  `03-lfo-depth.png`. M mutes the exact source. Right-click initially hid source
  mute inside the generic panel menu; the direct source/group action and focus
  correction are now in source, pending rebuilt presentation.
- Note routing fixture setup used the API, explicitly separate from UI actions:
  two copies of the private VST3 instrument fixture, empty trigger instrument 5,
  assignment to the first instance, channel 7 C-5, and initially a row-32 off.
  The second receives a channel Notes cable made via ⌘K → Patch by keyboard.
  Compatible targets initially followed incompatible audio inputs in the chooser;
  this ordering is now corrected in source and awaits rebuilt presentation.
- The note cable inspector shows live held/on/off counters and MIDI-channel
  mapping. Selecting MIDI 3 commits immediately. Screenshot
  `04-note-route-channel.png`. For a sustained-note case, the off cell was removed
  as fixture setup. Native Forward notes off produced held=0 and
  routingReleases=1, confirmed by `note-held-release-activity.json`. Cmd-Z restored
  forwarding with held=0 and unchanged on count, then the next real note-on
  delivered normally. `05-note-route-undo-waits.png` was captured after that next
  onset (held=1), not during the initial no-retrigger interval.
- Recorded automation setup inserted two known threshold points through the API.
  The native table edited time=2s from -30→-27 while playing; the effective read
  became -27 immediately. Cmd-Z/Redo visibly restore -30/-27. Screenshot
  `06-recorded-edit-live.png`. Back to graph preserves the selected Notes cable
  and Track 7 filter. A stale hint incorrectly said to stop playback; corrected
  in source with a focused regression. One AX-cell double-click targeted another
  cell; a screenshot-coordinate double-click reached the actual value editor.
- Root compressor grouping: select, Control-G, rename `Sidechain dynamics`,
  invoke Bypass group from the command palette. Because both Main and detector
  paths enter this group, the initial bypass asks which ingress supplies its dry
  path; selecting Main is a necessary musical choice, not redundant confirmation.
  Save to subgraph library exports and opens the definition in one action.
  Screenshot `07-exported-subgraph.png`. Export preserves the current bypassed
  group state; M restores processing explicitly.
- Assign the exported definition through each channel's Ordinary channel
  subgraph menu on Tracks 5 and 6. Both assignments commit on selection. Entering
  Track 6's instance shows two uses but incorrectly retains Track 5 in the
  observation picker (`08-definition-observer-context.png`). The source fix now
  preselects the entered copy, preserves an unavailable choice during adoption,
  and retains manual selection on passive refresh.
- Editing the definition's compressor threshold to -19 commits immediately.
  Parameter activity exposed another real defect: two same-name rack compressors
  and two same-name test instruments collapse menu items, so the target array no
  longer matches the menu index. A selected recipe target can appear blank
  (`09-copy-parameter-provenance.png`). Explicit menu items with stable identity
  and an exact graph-copy bridge now have regression coverage pending execution.
- Playback was found stopped after the group/export/assignment sequence, without
  a captured per-action transport trace. The earlier binary does not qualify
  this sequence as live-safe; the final rewalk must establish the precise
  transport boundary. Restarting from the canvas showed both runtime copies and
  a -19 prepared-value trace. Space while the Last touched button held focus
  activated that button instead of transport, consistent with its local binding.
- Saved this disposable song through Cmd-S and stopped it from the graph canvas.
  A final API read confirms audio inactive, no plugin failure and zero overruns.
  Cmd-Q then exited normally (process exit 0).

### Prepared final native port fixtures

Two private, uninstalled variants of `mac/Tests/FixtureVST3.mm` are prepared for
the remaining native walks. Their manifests record the original source hash,
explicit substitutions, compiler command and signed executable hash. They use
distinct class IDs and Objective-C view class names to coexist with the normal
fixture. Both pass the separate scanner's discovery and render/state probe.

- `qualification/wide-fixture-b9ffc9b6/ScreamSeqWideFixture.vst3`: effect with a
  five-channel Main bus and three-channel auxiliary bus. The production host
  must expose all channel-pair slices and the final mono channel.
- `qualification/instrument-input-fixture-593423f8/ScreamSeqInputFixture.vst3`:
  instrument with actual Main and auxiliary audio inputs, alongside its Notes
  event input. This distinguishes real audio input routing from note routing.

These scanner probes prepare the test inputs; they do not establish native UI,
audio routing or device acceptance for the final application.

All images, private songs and API setup logs are in the ignored qualification
bundle directory. The final table remains pending the coherent final executable;
these observations do not qualify later runtime changes.

### Integration recheck: final-6756b365 (superseded)

The full build with fingerprint
`ef7b37ca0f000df6ece36637017ac40dc04e48ad5396e85a40468d1d52627d79`
passed 102 CTest cases, both actual socket variants (including stage/follower
routing), 30 stock PCM comparisons and all six BlackHole loopback configurations.
The private app used PID 72692, BlackHole 2ch at 48 kHz / 512, and a copy of the
saved journey fixture. Its read-only transport trace and screenshot are under
`qualification/final-6756b365/`.

Entering Track 6's ordinary instance now selects **Track 6 · Ordinary** once
prepared. The Threshold → Inspect bridge selects **Track 6 · Sidechain dynamics
· Compressor · ordinary copy**, shows Threshold, and opens Sources despite
duplicate rack/plugin names. The screenshot `01-exact-copy-inspection.png`
records the corrected target. Before preparation it says Selected copy
unavailable instead of falling back. Loop was initially off in the reopened
fixture; the first short playback ended naturally before Loop was enabled.
No live-edit continuity claim is made from that run. Cmd-Q exited normally.

This build is superseded, not final acceptance: ASan subsequently exposed a
projected detached-chain bus indexing/capacity defect, and the actual workspace
socket test found an intermittent dropped explicit Follow on a hidden inspector.
Both are being repaired and requalified. Earlier normal passes do not override
those failures; `logs/pre-sanitizer-fix-*` retains the checkpoint results.

### Completed native walk: verified-8cf8e30a (e426 checkpoint)

Private `ScreamSeq Verified QA`, PID 76583, used the disposable
`Verified graph walk.screamseq` with BlackHole 2ch at 48 kHz / 512 frames.
The manifest records commit `e426395580c907e62ddf4fbd93bf52996a6e4a14`,
source fingerprint
`0af269e2d74aeba5acc490d43caa7d8962460aa824ae61aebe76de84ed379e28`
and signed executable SHA-256
`21a7f763235c37d605197f6bf26ba0d765dc326e7858f7912ab36d04629446a0`.
Raw PNGs, accessibility captures, graph/transport snapshots and the continuous
read-only monitor are retained in
`bin/mac-graph-fluency/qualification/verified-8cf8e30a/`. Capture numbers run
01–27; number 12 is absent from the saved directory. Counts below cover only
recorded actions; uncounted setup, switches and file-dialog steps are not guessed.

- **Pattern and insert editing (01–06).** Melody entry took eight recorded
  actions. Adding an effect on Track 4 took Add/search/Return (three), followed by
  one committed numeric edit and two bypass toggles: six actions after channel
  selection, with inline editing and no Apply step. The captures establish the
  bypass interaction, not an independently verified Dry/Wet value of 25.
  Track 3's existing Sidechain dynamics processing group exposed a real defect:
  appending Apple AUMatrixReverb rejected with “A group dry-map output no longer
  crosses this boundary”; the graph remained unchanged. Automatic append
  placement also needed correction. These failures led to the subsequent
  boundary-map and placement repairs. The subsequent `261b15387` rewalk
  below verified both corrections.
  EQ insertion on a chosen cable used four intended actions plus two search
  retry actions; its Undo/Redo and M bypass comparisons were exercised.
- **Main and detector routing (07–08, 13).** The inserted Track 1 compressor
  receives that channel's Main signal. Connecting Track 2 to its detector took
  four keyboard actions plus one retry after typing before the chooser was
  ready. Undo removed the connection, then a single socket drag recreated it.
  A later compressor Add on empty Track 8 completed while Loop and audio
  remained active; this does not explain the earlier transport stop below.
- **Source controls and provenance (09–11).** M muted the existing LFO directly.
  Two numeric entries edited its range. Inspecting parameter provenance opened
  the exact rack target; double-clicking its source reached the existing source
  editor. This is evidence for those edits and bridges, not a new-source
  creation count.
- **Preset reuse (14).** More → Save preset completed through the native file
  panel. On another compressor, the contextual Load preset action and an actual
  file double-click applied the saved preset while playback continued. This
  supersedes the earlier stopped-only preset failure for the compatible effect
  used here; no total file-dialog action count was captured.
- **Quiet return and diagnostics (15–20).** Right-click output → Add compatible
  node → search Return → Return → committed gain −18 was five actions. The
  newly created send began disabled at −96 dB and became audible when its gain
  rose. Scope showed the quiet send; setting −96 showed zero and Undo restored
  it. An attempted +24 dB value was rejected and +12 was accepted. The screenshot
  named `18-send-overload-scope.png` records the rejected value entry, not proof
  of overload; +12 alone likewise does not prove a signal exceeded 0 dBFS.
  Listen selected the send and the persistent Stop listening indicator restored
  normal monitoring. Searching Cmd-K for “send” revealed a discoverability gap:
  the graph action was titled only Add compatible node. The later label change
  includes quiet send while preserving the action ID and existing workflow.
- **Exact copies, independence and clipboard (21–26).** Entering Track 6's
  ordinary instance selected that exact copy automatically. M toggled the
  existing recipe group's real bypass. Parameter Inspect selected the exact
  copy, and Back restored graph context. Make independent required one action:
  Track 6 then used definition n31 while Track 5 retained n25. Copy/paste of a
  recipe processing group, followed by Undo/Redo, was verified. No unrecorded
  group-creation, export or clipboard action totals are inferred from these
  screenshots.
- **Persistence and exit (27).** The disposable checkpoint was saved, then the
  private app exited through Cmd-Q. This walk does not replace a reopen check
  on the subsequent final executable.

Transport continuity has one unresolved observation. The monitor first recorded
playback at elapsed 36.732 seconds, then stopped/audio-inactive at 384.398,
revision `11:8:0`, frame 16,698,880, row 62. Plan 6 and that revision had already
been active for about 30 seconds; no failed plan, engine fault, plugin failure
or overrun was reported. The action record has no intentional Stop/Space at
that boundary. Read-only investigation found no loop mutation in the live
Add/history handoff, and the later empty-channel Add stayed live, so the cause
is unproven. Neither a transition defect nor an accidental user stop is claimed.

Space restarted playback around 04:29 local (monitor elapsed 778.511). All
successful monitor samples thereafter remained playing until Cmd-Q, including
the later preset, send, copy, bypass, independence and clipboard actions. The
last successful sample at elapsed 1399.381 reports frame 29,818,368, plan 13
stable, zero overruns and no engine/plugin fault. Thirteen polls received the
ordinary document-busy response during edits, so these are sampled observations,
not an uninterrupted trace of every callback. The final missing-socket response
follows app exit. `transport.get.json` separately confirms Loop on. This run
therefore qualifies the recorded later interactions but does not support a
blanket claim that its entire session preserved playback.

The 261b rewalk continues below. A pending table entry means that final evidence
has not yet been logged here, rather than asserting that no native walk occurred.
Model/API, AppKit, sanitizer and Windows worker passes remain distinct evidence.


### Native rewalk: release-e5614327 (261b checkpoint)

Private `ScreamSeq Final QA`, PID 84230, uses the disposable
`Final graph walk.screamseq` and the same explicit BlackHole 2ch 48 kHz / 512
configuration. The manifest records commit
`261b153876083cfc942d20c806c343604bb7477c`, source fingerprint
`ca9e8d499a41fdfc51dec27ac501ebabc0707526d9196e6ccac827952536bdc6`
and signed executable SHA-256
`b6dca78a1d7c52f4d16f712fc9ec82f4d6651d56b390119db70a3d104ff3ebaf`.
The raw captures and monitor are in
`bin/mac-graph-fluency/qualification/release-e5614327/`.

- **Every wide-bus slice (13–15).** The prepared fixture exposes five logical
  input slices and five logical output slices across its physical Main and
  auxiliary buses. Stereo pairs and odd final mono channels remain separately
  addressable. Connecting input 3 from Track 2 used its context menu → Connect →
  Track 2 search → Return: **4 actions**. Inputs 1, 2 and 4, plus the return's
  Main feed, were connected with **4 pointer drags**, leaving all five inputs
  connected. Output 4 used Cmd-K Patch by keyboard, the socket chooser and
  Master; its source picker required two choices. No complete action count was
  captured for that keyboard path. Outputs 1, 2 and 3 were each dragged to
  Master (**3 gestures**); output 0 retained its existing implicit route.
  `15-wide-all-outputs.png` and its AX capture show all five output connections.
  No panel switch or separate Apply was required for these connection edits.
- **Actual instrument audio inputs (16).** The private instrument fixture's
  Main audio input was connected via input right-click → Connect → type Track 2
  → Return: **4 actions**. Its auxiliary audio input took **1 pointer drag**.
  `16-instrument-main-aux-inputs.png` shows both audio cables, the distinct Notes
  event socket and the fixture's 32 output sockets. This verifies choosing real
  audio inputs without confusing them with Notes; it does not claim that all
  32 outputs were routed in this journey.
- **Instrument graph editing and return (17–18).** The Instrument graphs →
  I1 Warm pulse command selected the root instrument bus, but the existing
  `Input Instrument` search hid it. Clearing filters exposed the bus and its
  Dry assignment. After assigning Sidechain dynamics, double-clicking entered
  the shared recipe with the correct I1 Warm pulse breadcrumb, but Observe
  incorrectly retained Track 5's ordinary copy. Manually choosing Warm pulse ·
  Channel 1 selected the real instrument copy; Threshold −20 committed live,
  as shown in `18-instrument-live-recipe-edit.png` and its AX capture. Returning
  through Song restored the instrument-bus selection and viewport. The narrow
  source correction reveals and frames the selected bus and preselects a
  matching instrument copy without borrowing another use's readings. The full
  copied AppKit suite passed ([log](logs/instrument-navigation-interface.log),
  [source manifest](logs/instrument-navigation-interface-snapshot.json)),
  including exact channel-card entry, delayed telemetry and retained manual
  choices. Its rebuilt native recheck remains pending; the working manual choice
  does not qualify the automatic-selection defect. No complete journey count
  was taken.
- **Save and transport.** Cmd-S saved the disposable fixture. At the recorded
  checkpoint, every one of **275** successful read-only monitor samples from elapsed
  0.000 through **1264.959 seconds** reports playing/audio-active, zero overruns
  and no engine fault or plugin failure. There were no read errors. The last
  sample has **63,097,856 rendered frames** and stable requested/rendered plan 21.
  This is over **21 minutes** of sampled editing continuity; it is not an FPS or
  exhaustive callback-timing qualification. The maximum gap between retained
  samples is 5.138 seconds. The desktop then became unavailable; root explicitly
  stopped this private process through its PID-specific API.
  [Monitor summary](logs/native-261b-transport-summary.json),
  [raw observations](logs/native-261b-transport.jsonl) and
  [explicit stop](logs/native-261b-explicit-stop.json) preserve the boundary.

The recorded automation capture `05-recorded-edit-live.png` exposed a separate
UI synchronization defect: editing the selected table point from −27 to −30
updated the table and effective value, but left the selected-point form at −27.
The subsequent fix refreshes that form by point identity after edits/Undo, keeps
the edited point selected after a time reorder, respects later user selection,
and preserves an active text draft. The full copied AppKit suite passed; its
[log](logs/recorded-form-interface.log) and
[source snapshot manifest](logs/recorded-form-interface-snapshot.json) are
preserved here. That test evidence postdates this immutable QA app, so native
presentation of the corrected form still requires the rebuilt application.


## Paused at user request

The user requested a sensible stopping point, report, commit and push after the
walk above. The [gallery](index.html) preserves 22 checkpoint-labeled images.
The later form, instrument navigation/copy selection and group-detach fixes have
automated regression coverage; their remaining native visual rechecks are
explicitly pending. No final all-journeys visual pass is claimed.


## Continued native rewalk — 7 October, 6bdb checkpoint

This continuation uses the separately signed `ScreamSeq Menu QA` process,
PID 2223, fingerprint
`6bdb96fe33cf65035e01f201caa5c48ddb1f53052320e80ddd2224048f40962f`.
The musician's PID 65969 was not touched. Audio settings explicitly selected
BlackHole 2ch, 48 kHz/128; the system default was unchanged. These are functional
journeys, not display-performance measurements. Evidence is in
`bin/mac-scratch/qualification/remaining-graph-journeys/`.

API setup is separate: the private fixture already contained sample notes and a
Track 1 compressor. Later, `13-recorded-api-setup.json` inserted two recorded
Threshold points; `16-instrument-api-setup.json` converted the sample song to
instruments, enabled instrument 1's volume envelope, added the retained private
VST test instrument as instrument 5 and assigned the exported graph to instrument
1. These setup operations do not count as user interactions or native UI passes.

| Journey | Before evidence | Actual 6bdb walk: actions / switches / redundant confirms | Outcome |
|---|---|---|---|
| Main and detector fan-out | Existing one-drag successful path | 1 / 0 / 0 | Track 2 output dragged to Compressor Detector; its existing main output retained. |
| Fresh group, name, dry boundary and export | Export alone 6 / 1 / 0; full path pending | 10 / 1 graph-depth / 0 | Select Compressor, Ctrl-G, name, context Bypass, choose Main dry path, M restore, Cmd-K export. The dry-path choice is a meaningful routing decision. |
| Create LFO, target, range, mute and return | Partial earlier source-only path | 25 observed including failed attempt/workaround / 2 graph-depth + 2 panel / 0 | Active-group creation placed LFO outside group and hid it. After returning/ungrouping, drag source onto Threshold, edit −0.1/+0.1, M mute/unmute, Inspect and Back all worked. Clean creation rewalk remains pending after the focused fix. |
| Recorded point edit/detail/history | Table value changed but detail stayed stale | 9 / 2 panel + 1 floating-placement / 0 | Open activity, Recorded points, float, edit second point −30→−27, Undo −30, Redo −27; selected detail follows each value. Back returns to the same compressor. |
| Filtered instrument graph and exact copy | Filter hid selected instrument; observation borrowed another copy | 5 core actions + 1 query correction / 1 graph-depth / 0 | Cmd-K Instrument graphs I1 reveals hidden I1, clears Track 8 filter; double-click Track 1 use automatically observes Warm pulse · Channel 1; Song restores root selection. The two filter clicks are setup. |
| Melody, input instrument/octave, bounded audition and return | No full prior count | 12 successful actions + 1 empty-instrument attempt / 2 panel / 0 | Seven notes on channel 5, input down once, octave up once, Instruments, Control-Space, Command-Option-1 returns to row 7/channel 5. The initial sample-only fixture correctly had no tracker instrument to preview. |

For group/export, the selected processor had Main and Detector ingress and one
output. The saved reusable graph preserves both inputs and its explicit Main dry
mapping; export opens an independent library definition. Screenshots
`03-group-dry-bypass.png` and `04-exported-group.png`, with matching graph and
transport JSON, qualify this actual fresh boundary path.

LFO screenshots `06-lfo-range.png`, `07-lfo-muted.png`, `08-lfo-provenance.png`
and `09-lfo-return.png` qualify the successful portions, not the failed initial
creation. The new source was n23 and the existing group n17 did not contain it.
The narrow correction adds optional `parent` to `graph.song.source.add` so source,
initial connection and group membership commit together. Shared, Mac session and
full AppKit tests passed; a rebuilt native rewalk is still required.

`14-recorded-inline-detail.png` and `15-recorded-redo-detail.png` show the corrected
selected detail next to the inline table value. Screenshots
`17-filtered-instrument-bridge.png` and `18-instrument-copy-observed.png` show the
automatic reveal and exact copy selection that failed on the prior immutable
checkpoint. The latter breadcrumb is Track 1 → I1 Warm pulse → Detector dynamics.

Following API instrument creation, reopening Instruments at the unchanged cursor
showed an empty stale picker, despite document.get containing five instruments.
The panel menu's Inspect editing cursor refreshed it immediately. This is a
separate confirmed refresh defect; the target-follow token used only pattern,
row and channel. After this explicit refresh, actual sample and VST instrument
choices appeared; ordinary note keys reached InstrumentEditor and opened audition
audio, and Command-Option-1 retained row 7/channel 5 and the selected new-note
instrument. `19-instrument-preview.png`, `20-plugin-instrument-return.png` and
transport JSON preserve that limited observation. A quick synthetic key tap does
not qualify sustained envelope-cursor motion or audible preview quality.

All nine retained playback checkpoints through the recorded edit report playing,
audio-active, Loop on, zero overruns, fault=false, pluginFailure=false and stable
routing plans. The later two audition checkpoints are intentionally stopped song
transport with audio-active preview. These are sampled observations, not an
uninterrupted trace. The private fixture was saved as `Remaining graph
journeys.screamseq`; PID 2223 was explicitly closed before the next performance
capture.

Remaining final rechecks: active-group LFO creation after its fix; asset catalogue
refresh after its fix; branched-group detach/dry restoration with endpoint history;
confirmed overload diagnosis; working-zoom port readability; and the latest
scratch Return/Play stable-target regression. Windows hardware/device checks
remain deferred by the user.


## Continued native rewalk — 7 October, 410c7f77 checkpoint

The isolated `ScreamSeq Graph Final QA` copy (PIDs 12971 and 13823, separated by
an explicit profiler handoff) used source fingerprint
`410c7f77d73a8732a6999b81a5207d9adf9572f1794f0c77c3c6d2a10a364c65`.
BlackHole 2ch was verified again before the bounded overload test
(`34-blackhole-device.png`). The musician process remained untouched. These
functional walks ran alongside compilation at times and make no pacing claim.

| Journey | Prior state | Actual native actions / switches / redundant confirms | Outcome |
|---|---|---|---|
| Create source inside active group, connect, range, mute, Inspect/Back | 25-action workaround above | 13 / 2 panel / 0, starting inside group | New LFO n38 stays inside n30, selected and visible; drag to Threshold, set −0.05/+0.05, mute/unmute and Inspect/Back retain group context. An additional Undo/Redo pair proves source+membership are one history action. |
| Follow new-note instrument at unchanged empty cursor | Same-cursor target remained stale | 3 / 1 panel / 0 | Instruments, focus pattern, Option-Down changes input 1→2 and inspector 2 without moving row 7/channel 5. |
| Refresh pinned asset catalogue and retain draft | Empty stale picker after API creation | 2 reopen actions, then 3 picker-selection actions / 1 panel / 0 | API fixture edits rename/add instruments; pinned target 2 remains, choices refresh; selecting 1 loads 1. An uncommitted sustain field survives another catalogue rename. A remaining caption mismatch was reproduced and corrected separately below. |
| Scratch captured-row playback and return | Latest stable-target native check pending | 2 / 1 focus return / 0, editor already open | Play from row uses captured P0/R0/CH1; Return focuses that cell's FX1 command. |
| Explicit two-output group dry map | Pending | 6 / 0 panel / 0 | Select group, M, choose A ingress for A output and B ingress for B output, M restores processing. The two choices express musical routing intent. |
| Repatch cable endpoint and history | Pending | 3 core + 2 history actions / 0 / 0 | Select Track 3→Master wire, choose To:Track 4; change is immediate. Undo restores Master, Redo restores Track 4. One extra Undo restores fixture. |
| Multi-output port readability and patching | Pending working-zoom check | 5 Find-node navigation + 4 patch actions / 0 / 0 | All 32 outputs have distinct sockets; Notes differs from audio. Right-click Aux 16→Connect→Track 8 patches it and automatically activates that output. Undo restores original route. |
| Diagnose and correct a real overload | Earlier no-overload fixture inconclusive | 26 observed / 1 graph-depth / 0 | Find overload reveals the compressor's overloaded detector. Exact cable scope, Show cable source, inline gain change and Clear latches resolve it. Five observed actions inspected a node's default output and corrected a mistaken wire selection; the clean route is 21 actions. |

The fresh LFO creation screenshots are `25-grouped-lfo-visible.png`,
`26-grouped-lfo-muted.png`, `27-grouped-lfo-provenance.png` and
`27-grouped-lfo-return.png`; accompanying graph/Undo/transport JSON identifies
source n38 and group n30. API fixture construction in
`21-final-fixture-api-setup.json` is not included in the native action count.

`22-asset-active-input.png`, `23-asset-pinned-catalogue.png` and
`24-asset-draft-retained.png` qualify the catalogue/follow changes. The picker
selected instrument 1 while its pinned panel caption still said Instrument 2
(`24-asset-caption-mismatch.png`). The subsequent narrow fix updates captions
from the actual successfully loaded editor index, preserving pin and draft
state; rebuilt native confirmation remains pending at this paragraph's checkpoint.

The first branched detach attempt used two unconnected parallel processors;
the engine correctly rejected it without mutation. API-only fixture preparation
then added internal A→B gain 0.25, making a connected selected subgraph. With
explicit A/B dry maps, native Detach chose Input→A and A→Collector but rejected
with “A group dry map must cover every outgoing audio boundary exactly once.”
This is a confirmed dry-map repair defect, preserved in
`31-connected-branch-detach.png` and `31-connected-detach-rejected-graph.json`.
The root's shared fix and targeted regression must be rewalked in its later
build; no success is claimed on 410c7f77. Expected topology removes only the two
chosen crossing edges, adds Input→Collector gain 0.5, retains the B branch and
internal A→B cable, and drops only the obsolete A dry mapping.

`32-endpoint-redo.png` and three graph snapshots prove real endpoint history,
not only dropdown text. `33-all 32-output-ports.png` and
`33-aux16-connected.png` show the dense instrument ports; the latter's graph
snapshot proves output 16 routing. The auxiliary connection was removed by Undo.
The long bus descriptions ellipsize at the fit-to-node zoom; the bus numbers and
separate sockets remain visible, and the target chooser supplies the full label.

For overload, API-only setup adds two built-in gainers at  +12 dB each to Track 2.
This sends only to BlackHole. `graph.signal.get` measured the second processor
output and compressor detector at **1.26753354** with clip latches. Native Find
next overload located the detector. Selecting its cable and opening Scope read
that exact contribution: 4096 frames at 48 kHz, zero dropped frames and zero invalid
samples (`34-overload-exact-scope.json/png`). Show cable source opened the upstream
gainer's controls; editing +12→−12 dB immediately reduced its output to
**0.07891299** and detector peak to **0.07997221**. Clear overload indicators and
Find next overload then reported no latched ports; API agreed zero latches.
`34-overload-signals-before.json`, `35-overload-signals-after.json` and
`35-no-overloads.png` preserve both states. No audible output test is claimed.

All retained playback snapshots for these actions show playing/audio-active,
Loop on, no engine/plugin failure and zero overruns. Snapshots are not an
uninterrupted monitor. The private fixture was saved as `Final graph
journeys.screamseq` and PID 13823 closed explicitly before the final build rewalk.
The remaining native acceptance checks at this checkpoint are the repaired
explicit-dry detach with Undo/Redo and corrected pinned asset caption.


## Final native closure — 7 October, 7bb842e8

The repaired build fingerprint
`7bb842e843933e64f6ae42bfbb59fecd69b6b782983d0cbd59024d9849e10b43`
was copied and separately signed as `ScreamSeq Graph Closure QA`, PID 19167.
BlackHole was explicitly selected before playback; the musician's process and
system defaults remained unchanged. No API mutations were used in these final
journeys: the already-saved disposable fixture supplied the branch and dry maps.

- **Pinned caption:** pin the Instrument 1 panel, choose Instrument 2 in its picker
  (3 actions, no panel switch or extra confirm). Caption, picker and envelope now
  consistently identify Instrument 2; the pin remains set.
  `36-final-pinned-caption.png` records the corrected result.
- **Connected branched detach:** starting in the reusable graph, select the group,
  Cmd-K → Detach and reconnect, then choose Input→A and A→Collector
  (6 actions, no panel switch or redundant confirm). It now commits the intended
  Input→Collector gain 0.5 bridge, retaining Input→B, B→Collector, A→B gain 0.25,
  group membership and the exact B dry map. The obsolete A dry map is removed.
  Two further history actions restore the entire original library data on Undo
  and the entire detached library data on Redo, compared from read-only API
  snapshots. M then bypasses successfully using the retained B map; another M
  restores processing. `37-final-detached.png`, `38-final-detach-undo.png` and
  `37/38-final-detach-*.json` record this final pass.
- **Scratch captured navigation:** Play from row and Return to row (2 actions,
  one focus return, no confirmation) use P0/R0/CH1/FX1 as displayed, with Return
  focusing column 4 rather than following playback. `39-final-scratch-play.png`,
  `39-final-scratch-return.png` and matching context/transport JSON record it.

Both retained transport samples show playing/audio-active, zero overruns,
fault=false and pluginFailure=false. `final-native-closure-summary.json` records
exact library history equality and the build/process/device boundary. The fixture
was saved and PID 19167 explicitly closed, releasing UI for the independent quiet
performance qualification. This completes the native functional checks named at
the preceding checkpoint; physical Windows/device checks and display pacing are
separate qualifications, not implied by these passes.
