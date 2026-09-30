# ScreamSeq interaction audit — 24 September 2026

This pass changes interaction, not the musical feature set. The running musician's app and song remain untouched; journeys run in a separate QA bundle on disposable data. The baseline is source fingerprint `0a188e60cf5453d7c4567bb38f78753e5950cac387d4d6e82240706711e3856f`.

## Journey map

| ID | Musician's task | Expected frequency | Main friction to investigate |
|---|---|---|---|
| J01 | Write four melody notes, audition the pattern, change input instrument | Very high | Lost grid focus; instrument selection and return |
| J02 | Find/import one sample, use it in the pattern | High | Browser/sidebar/inspector duplication; selected sound |
| J03 | Add an effect to channel 3, tweak and bypass it | Very high | Rack selection, routing detour, button wall |
| J04 | Automate a parameter over rows 16–32, revisit and edit it | High | Target re-selection; automation controls scattered |
| J05 | Send channel 2 to a compressor sidechain on channel 1 | Medium | Routing gate; ports and cables; form confirmation |
| J06 | Create/duplicate patterns and rearrange the song order | Very high | Order form and confirmation; context switching |
| J07 | Diagnose a changing parameter during playback and open its source | Medium | Activity target and source navigation |
| J08 | Save an effect preset and reuse it on another channel | Medium | Preset controls; destination memory |
| J09 | Create a plugin-trigger instrument and enter a note | Medium | Instrument assignment detour |
| J10 | Adjust a sample loop while retaining pattern context | High | Inspector access; already-live editing regression |
| J11 | Adjust a precise note and add a retrigger | Medium | Duplicate context controls; already-live editing regression |
| J12 | Find and enter a pattern effect and its value | Very high | Keyboard path and effect discovery |
| J13 | Edit an instrument envelope and reuse a bank template | Medium | Confirm boundaries; linked-template semantics |
| J14 | Add/edit a graph modulation relationship | Medium | Source creation, cable/property forms |
| J15 | Balance/mute/solo channels, return to writing | High | Mixer home and focus; contextual bridge |
| J16 | Undo interleaved pattern, plugin and routing changes | Very high | Split histories and redo branching |

Frequency is a prioritization estimate, not usage telemetry. Ranking uses steps saved × frequency, with implementation risk considered separately.

## Measurement protocol

Walk the actual UI before changing it, then repeat affected journeys. A recorded action is one click, double-click, drag, shortcut, or text-entry operation; exact keys/text and click count are retained in the action ledger. Setup, fixture reset, screenshots and incidental computer-use retries are not counted as musician steps. Panel switches and redundant confirms are recorded separately. An unfinished baseline path is marked as blocked, never assigned an invented successful count.

Keep legitimate named/file save and destructive external-file decisions distinct from redundant Apply buttons. Existing direct manipulation is verified and retained. Preserve pinned targets, independently inspected panels, shortcuts and access to all relocated actions.

## Evidence and results

Implemented and checked for this macOS pass. The strict presentation benchmark remains unresolved; two template-editing choices await the musician. Screenshots and action ledgers: `mac-native-qualification/2026-09-24-interaction-flow/`.

## Implementation and priority

The largest completed changes are the direct plugin rack, channel-aware plugin insertion, parameter-to-envelope bridges, and chronological Undo. Compose keeps the tracker dominant; supporting inspectors are opened on demand. These are macOS UI changes. Shared song/API changes remain portable, but this pass does not implement equivalent Windows widgets or unify the Windows host's Undo stack.

Frequency weights below are estimates: very high = 5, high = 3, medium = 1. They are not measured usage. Scores use only comparable portions of the walks; correctness and risk also affect priority.

| Priority | Change | Estimated benefit / risk | Result |
|---|---|---|---|
| 1 | Direct rack, add to current channel, inline bypass | 6 observed steps × 5 = 30; moderate routing risk | Implemented; add+route is one Undo |
| 2 | Parameter → automation bridge; draw points | 7 valid-path steps × 3 = 21; moderate target/focus risk | Implemented; stable parameter IDs retained |
| 3 | One chronological Undo | No honest click-saving score; essential safety net for live edits | Mixed UI/API history, redo, gestures and stale revisions tested |
| 4 | Sample search Return and active instrument | Baseline blocked, so no numeric score | Implemented and re-walked |
| 5 | Cable-first routing and live wire properties | 6 steps × 1 = 6 for patch itself; existing cables retained | Form hidden by default; one-drag sidechain verified |
| 6 | Plugin-instrument creation bridge | Baseline dead end; no numeric score | Add synth and create/assign without leaving the workflow |
| 7 | Compact panel chrome, context refresh, reliable return | Frequent focus savings; baseline windows obscured targets | Implemented, including collapsed-lower-panel fix |
| 8 | Arrangement drag and immediate labels | Adjacent move saves no steps; distant moves scale better | Real drag verified; metadata preserved in one edit |
| 9 | Graph source/parameter fields and envelope live editing | 1–3 confirms avoided per edit sequence | Implemented; custom-editor template commit deliberately retained |
| 10 | Secondary-action menus and command catalog | Lower visual burden; no invented time metric | Menus and keyboard fallback retained |

## Before / after measurements

Counts below are **actions / panel or window switches / redundant confirms**. A double-click and a shortcut each count as one action. Return ending numeric entry is part of typing, not a redundant confirmation. Named Save and creation commands are purposeful operations, not Apply gates. Opening/returning from a chooser counts as a switch; explicit return to grid focus also counts. Transient popup menus do not count as separate panels. Where a journey was not completed or the input methods differ, the table says so rather than inventing a successful baseline.

The first action logger accidentally retained an old journey label. `actions.json` is preserved as raw evidence; its labels after the first journey are not authoritative. The reconstruction below uses ordered actions, screenshots and observed state. `actions-v2.json` uses explicit labels. Incidental stale accessibility IDs and setup/API fixture actions are excluded from comparable paths. Some waits are necessary for native asynchronous dialogs and are not counted as musician actions.

| Journey / comparison boundary | Before | After | Verified outcome and caveats |
|---|---:|---:|---|
| J01 Four notes, pattern audition/stop, next input instrument, one note | **8 / 0 / 0** | **8 / 0 / 0** | Existing fast keyboard path preserved: Z X C V, ⌃Space, Space, ⌥↓, Z. No claimed saving. |
| J02 Search sample library, import, return to grid, enter note | **Blocked** | **5 / 3 / 0** | ⌘⇧L, `909 clap`, Return, ⌘⌥1, Z. Baseline inspection launch did not present the browser; this is a qualification-path defect, not evidence that ordinary users could never import. Return waits for the current search result before importing. |
| J03 From channel 1: add effect on channel 3, type a value, bypass | **16 / 4 / 3** | **10 / 3 / 0** | Two Tabs → Plugins → Add → double-click Distortion → type value → inline bypass. No mixer-enable or route-assignment detour. Baseline also lost the first typed digit; final exact replacement with `18` was verified separately. |
| J04 From selected plugin parameter: envelope over rows 16–32 | **11 valid-path actions / 1 / 2**; **18 observed** including failed empty-point attempts | **4 / 1 / 0** | Parameter ellipsis → Automate → click row 16 → click row 32. Saved positions are exactly rows 16 and 32; drawn values were about 20.19% and 80.80%. Numeric precision remains available. Baseline used exact 20/80 fields; value-entry effort is therefore not identical. |
| J05 Graph already open, compressor inserted on track 1: patch track 2 detector | **7 / 0 / 1** | **1 / 0 / 0** | Drag Track 2 output to Compressor Detector sidechain. From the grid, add one graph shortcut; expanding the small dock through its panel menu costs two optional actions. Existing main output remains connected. |
| J06 Create, duplicate, open order editor, move one position | **4 / 1 / 0** | **4 / 1 / 0** | Actual drag produces order `[0,2,1]`. Longer moves take one drag; their savings are inferred, not benchmarked. Pattern/section labels now commit on leaving the field. |
| J07 From selected parameter: inspect activity and reopen its envelope source | **3 / 2 / 0** | **3 / 2 / 0** | Before: Activity, recover obscured window, double-click source. After: parameter menu, Activity, double-click source. The full after walk also started/stopped/captured a pass (five additional actions plus scrolling). Target and source navigation were verified during BlackHole playback. |
| J08 Save preset and reuse on another channel | **22 attempted; baseline file-dialog run incomplete** | **15 observed / 6 / 0** | Final saved `Flow QA Drive`, loaded it on Track 4, and read back Drive = 18. Count includes one failed native-file selection and keyboard path retry. Do not interpret this as a preset speed improvement. Save/Load moved to More and context menus. |
| J09 Create a synth trigger instrument and enter a note | **Blocked after opening the instrument chooser** | **7 / 6 / 0** | Instruments → New plugin instrument → Add instrument plugin → double-click Apple DLSMusicDevice → Create & assign → grid → note. Instrument 5 was assigned and used. Create is intentional, not a redundant confirmation. |
| J10 Open sample inspector and replace loop start | **5 / 1 / 0** | **5 / 1 / 0** | ⌃⌥2, field, ⌘A, `8`, Return. Already-live loop editing preserved. |
| J11 Open precise notes, add hit, move it, fill retriggers | **6 / 1 / 0** | **4 / 1 / 0** | Baseline needed a failed Add attempt and Use current cursor. Now the unpinned inspector follows the opened row; four hits saved on the intended row. |
| J12 From note column: find Set Panning and type its value | **9 / 2 / 0** | **9 / 2 / 0** | Three Rights, ?, `panning`, Return, Right, 8, 0. The finder opens and returns focus; status identifies Set Panning. Initial return-to-grid is excluded on both sides. |
| J13 Existing instrument: ADSR, save named bank entry, use linked | **8 / 5 / 1** normalized | **7 / 5 / 0** | Existing musical edit saves immediately. Normalization excludes an accidental baseline envelope toggle and unrelated new-instrument creation. Name/save/link decisions remain explicit. After bank use the instrument showed the ADSR shape. |
| J14 Create LFO in a new reusable graph and set rate | **11 / 0 / 1** observed | **8 / 0 / 0** | More → New subgraph; More → LFO; rate field → 0.5 → Return. Source auto-selects. Expanded after walk added Distortion, loaded its parameters by selection, exposed Drive, drew modulation, and set maximum 0.8 with no Apply. Cable selection needed Option-Tab because a curve crossed behind a node; recorded as remaining visual friction. |
| J15 Within mixer: mute, solo, enter −6 dB | **5 / 0 / 0** | **6 / 0 / 0** | Baseline used one accessibility replace operation; after used ⌘A + typing, so the apparent extra action is an input-method difference. Opening mixer and returning to grid add two actions/two switches to the after walk. |
| J16 Undo across pattern and plugin domains | **Two different Undo controls; incorrect chronological order** | **One global shortcut** | Baseline: after note then bypass, ⌘Z removed the older note while bypass stayed; effect-specific Undo was needed. After: note → bypass → mixer edit, then three ⌘Z presses restored mixer → bypass → note, without switches or confirms. This is a correctness improvement, not a meaningful 5→7 action comparison. |

## What the walks found

- **J01/J12:** The tracker already had good note/instrument/effect keyboard paths. Kept these intact. A newly exposed command-catalog bug intercepted ⌘A in numeric fields; fixed by limiting the additional-shortcut dispatcher to explicitly assigned shortcuts.
- **J02:** Browser presentation under inspection mode and Return in its search field were unreliable. The browser now presents for explicit invocations and waits for the matching search generation before importing. The new instrument is selected automatically.
- **J03:** The old rack forced the musician to remember which channel needed the effect, then find the mixer and enable routing. The browser now captures the originating channel or bus. Add+route is atomic and undoable. The list refreshes even when pattern focus is retained.
- **J04:** Empty numeric point fields looked editable but could not create a point. Empty fields now explain the prerequisite; clicking the envelope or Add at cursor creates a point. Per-parameter menus retain the exact stable target. A collapsed lower workspace originally hid the destination; opening a lower panel now allocates visible space.
- **J05:** Main and detector ports are separate labeled destinations; hollow auxiliary sockets enable as part of the patch. No extra routing-enable step. Multi-input/output routing still uses the existing bus-aware cable engine.
- **J06:** Direct order dragging removes repeated up/down presses for distant moves. Keyboard/context-menu moves remain. Numeric pattern replacement, names and sections save without a separate Set/Save.
- **J07:** The parameter-activity view is the diagnostic home; Automation owns envelopes and recording controls. Source double-click opens its editor with the target selected. Recorded points remain a source tab in Activity. Manual values in the rack are saved baselines; Activity shows the effective rendered value. They are intentionally different.
- **J08:** Named file Save is retained. Preset loading restores the saved baseline rather than an automated transient. The after test verified 18 dB even though playback had temporarily reached about 29 dB. Native file-dialog timing made the baseline incomplete; no fabricated success/count is reported.
- **J09:** The empty plugin-instrument chooser now has a direct Add instrument plugin bridge and returns with the synth selected. Existing assignment changes commit immediately.
- **J10/J11:** Immediate loops, waveform handles, precise-note table and timeline remain. Inspector opening refreshes an unpinned target; pinned targets remain independent. Removed the oversized empty space in the precise-note layout.
- **J13:** Ordinary instrument-envelope edits no longer require Apply. Song-local linked-template editing and catalogue publishing retain explicit boundaries pending the user's answer.
- **J14:** New modulation sources select themselves. Source fields, graph recipe names, normal graph assignments, graph parameter values, port lists and selected-wire properties commit directly. The numeric connection form remains under More → Patch by keyboard for exact or inaccessible routes.
- **J15:** The mixer uses the same direct rack interactions. Bus creation/removal/reload move into More. Advanced sends and instrument output forms remain available; their multi-field creation is not silently partially submitted.
- **J16:** Document edits and plugin-host edits share a monotonic sequence. Undo/Redo interleave both histories. Parameter gestures coalesce, and new edits invalidate the competing redo branch. Old API domain arguments alias the same chronological history on macOS.

## Removed or relocated controls

| Former control | Primary home now | Keyboard / secondary access |
|---|---|---|
| Plugin dropdown + Open | Direct plugin rack; double-click/Return | More/context → Open interface / controls |
| Bypass button | Inline checkbox on each plugin | More/context → Bypass / enable plugin |
| Remove and ↑/↓ button row | Drag to reorder; Delete removes selected item | More/context → Remove / Move earlier / Move later |
| Add then route in Mixer | Add plugin at the originating channel/bus | Mixer rack Add effect; graph Add effect |
| Save/Load preset and Programs button wall | Plugin More/context menu | ⌘K searchable actions; no-selection entries explain what to select |
| Record checkbox in Plugin controls | Automation → Record gestures | Context menu / ⌘K; recorded points inspectable in Activity |
| Pattern automation button | Per-parameter Automate action | Plugin More → Automation envelopes; ⌃⌥9 |
| Parameter activity button | Per-parameter Inspect activity | Plugin More; ⌃⌥0; source double-click returns to editor |
| Undo effect change | Global Edit → Undo / Redo | ⌘Z / ⇧⌘Z; API history aliases |
| Follow / Cursor / Return / Panel repeated toolbar | Compact pin + panel ellipsis | Inspect editing cursor, Return to opening row, dock/float/hide in panel menu and ⌘K |
| Always-visible New connection form | Cable drag; select wire for properties | More → Patch by keyboard; Option-Tab selects cables; Return edits; Delete removes |
| Update connection / Apply source / Save graph-name controls | Live edits on fields and selections | Global Undo |
| Load graph effect controls | Loads on selecting the effect node | More → Reload controls |
| Set graph parameter value / Apply ports | Return or focus loss commits | Audio buses collapsed section; global Undo |
| Apply custom graph plugin settings | **Update template from custom interface** | Graph-plugin More and ⌘K; draft semantics retained pending decision |
| Graph commands Enable mixer / Enable lane wall | Implicit routing; Insert command creates the required lane | More → Show lane in pattern / Reload; existing-command edits save directly |
| Order Set pattern / metadata Save | Picker selection / end editing | Drag order; More/context retains keyboard moves |
| Empty plugin-instrument dead end | Add instrument plugin bridge | Instrument panel; instrument-only browser; Create & assign |
| Sample browser load confirmation for single result | Double-click/Return, including search Return | Bulk-load action retained for intentional multi-selection |

No original tracker effect, plugin format, graph route type, keyboard patching mode, pinning capability or envelope-bank copy/link distinction was intentionally removed. The sample sidebar remains a compact song-asset selector; the Samples inspector remains the editor. Combining them into one large browser would compete with the pattern, so that layout change was not guessed.

## Screenshots

Evidence is actual native-window capture, not generated mockups. Some screenshots precede final wording-only cleanup (for example, the old Update connection hint and Undo effect wording); later code removes those stale labels.

- [Pattern-first workspace](mac-native-qualification/2026-09-24-interaction-flow/after/01-workspace.png)
- [Plugin-trigger instrument](mac-native-qualification/2026-09-24-interaction-flow/after/02-plugin-instrument.png)
- [Reordered arrangement](mac-native-qualification/2026-09-24-interaction-flow/after/03-arrangement.png)
- [Direct parameter automation](mac-native-qualification/2026-09-24-interaction-flow/after/04-direct-automation.png)
- [Effective value and source audit](mac-native-qualification/2026-09-24-interaction-flow/after/05-parameter-activity.png)
- [Preset reused on another channel](mac-native-qualification/2026-09-24-interaction-flow/after/06-preset-reused.png)
- [Sidechain cable](mac-native-qualification/2026-09-24-interaction-flow/after/07-sidechain-cable.png)
- [Instrument envelope after linked bank use](mac-native-qualification/2026-09-24-interaction-flow/after/08-envelope-bank.png)
- [Selected modulation wire, immediate range editing](mac-native-qualification/2026-09-24-interaction-flow/after/09-modulation-wire.png)

## Questions retained rather than guessed

1. Should editing a **song-local envelope master** update its linked uses immediately, or retain the explicit Save song template boundary? Catalogue publishing remains explicit either way, as previously requested.
2. Should a reusable graph plugin's **custom editor** keep a private auditionable draft with Update template, or update every processor copy live with Undo? Ordinary parameter fields already commit directly. This is a real change in scope of an edit, not merely removal of a button.

Both questions were queued asynchronously. Existing explicit template boundaries remain until answered. The largest remaining interaction opportunities are dense graph cable readability, simplifying advanced multi-bus routing forms, and reducing the amount of technical detail visible in the modulation-source inspector. The source kinds and power-user controls remain available.


## Final verification and limits

- Built the final native app successfully: `bin/mac-interaction-flow/ScreamSeq.app`. Bundle signature verification and `git diff --check` pass. Source/executable fingerprints are in [build.json](mac-native-qualification/2026-09-24-interaction-flow/build.json).
- Shared/native CTest suite: 76/78 passed initially. The two failures expected the old separate Undo histories; updated those assertions for chronological history, then both targeted reruns passed. The new unified-history suite covers interleaving, redo invalidation, gestures, atomic add+route, neutral implicit routing and plugin moves.
- Final full AppKit interface suite passes. Added checks for text shortcuts, visible lower-panel navigation, immediate wire edits, graph parameter selection/commit, duplicate end-edit suppression and discoverable disabled commands. A timing-sensitive loop test now waits for completion with a bounded deadline instead of assuming a fixed 250 ms under compiler load.
- Full socket API, workspace socket, and native startup tests pass. API schema and documentation include routed insertion, arbitrary plugin/order moves, implicit routing reads and the chronological Undo alias semantics.
- Actual UI checks verified typed parameter values, named preset reuse, plugin-trigger creation, sample import and note entry, order dragging, precise retriggers, envelope points, activity-to-source navigation, sidechain dragging, modulation dragging/range editing, graph-command insertion/live Amount edits, and mixed-domain Undo.
- Final graph-command check inserted a row command without enabling a mixer/lane first; editing Amount to 50 immediately saved `amount: 0.5`. Final cable check saved `maximum: 0.8`, then saved the disposable QA song successfully. See the JSON snapshots alongside the screenshots.

### Display/audio qualification: not a 60 fps pass

Both benchmarks used a visible active native window, 60 seconds of playback plus continuous edit/Undo/save work, 48 kHz, 512-frame BlackHole output, AU + VST3 + shared graph processors. No presentation threshold was relaxed.

| Metric | Before | After |
|---|---:|---:|
| Mean presented FPS | 54.74 | 54.32 |
| Missed presentations | 304 | 327 |
| Maximum presentation interval | 716.66 ms | 50.00 ms |
| Audio overruns | 1 | 0 |
| Audio callback p99.9 | 2.840 ms | 2.730 ms |
| Callback period | 10.667 ms | 10.667 ms |
| Strict result | Fail | Fail |

The after run's CPU frame preparation p99 was **0.172 ms**, GPU execution p99 **1.081 ms**, with **5,625 callbacks**, no audio-capacity fault and no plugin failure. This is evidence for that fixture, not physical-output latency or a listening test of arbitrary third-party plugins.

The original build also missed deadlines. Its default layout displayed more panels, while the revised default kept Compose dominant; it was additionally sampled for five seconds. These are therefore bounded diagnostic comparisons, not a controlled claim of a speedup or proof that no rendering regression exists. Sampling points toward AppKit layout/update-cycle work outside the fast Metal frame preparation, but that is a hypothesis requiring a focused profile. **Sustained strict 60 Hz presentation remains open work.**

The final build additionally keeps graph creation/clone commands discoverable before a recipe is selected, with explanatory disabled states. Its full interface suite passed. That menu-availability change and its test assertions followed the performance runs; no rendering or audio changes followed them.

Reports: [before performance](mac-native-qualification/2026-09-24-interaction-flow/performance-before.json), [after performance](mac-native-qualification/2026-09-24-interaction-flow/performance-after.json), [verification summary](mac-native-qualification/2026-09-24-interaction-flow/verification.json), [test logs](mac-native-qualification/2026-09-24-interaction-flow/logs/).

The musician's original process (PID 95116) was left running throughout. Only disposable QA processes were stopped. System audio defaults were not changed. Work is local and uncommitted. To use the rebuilt app, save and quit the existing ScreamSeq session before opening the build above.

[Open the screenshot gallery](mac-native-qualification/2026-09-24-interaction-flow/index.html), including the final [direct rack](mac-native-qualification/2026-09-24-interaction-flow/after/12-direct-rack-final.png), [live cable inspector](mac-native-qualification/2026-09-24-interaction-flow/after/13-live-cable-final.png), and [command catalog](mac-native-qualification/2026-09-24-interaction-flow/after/10-command-catalog.png).
