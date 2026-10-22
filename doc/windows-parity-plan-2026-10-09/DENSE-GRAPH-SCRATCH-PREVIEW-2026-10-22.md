# Dense graph framing and immediate scratch preview

## Observed on main, before these source corrections

Exact main `0fe8f34008fe6517d7598affc31a4948a2ab14b8`, Windows x64 CI
executable SHA256 `afe4a86c04a895d01a2ae8fe22b53e51ba33952cfcb98298ac0d304d9e7bbe45`,
was checked on Windows ARM64 under emulation. Its product tree equals the
preceding `077e35f29` qualification source. CI run 39811540714 built successfully
and passed native/worker gates, but its actual-app suite failed 12 distinct
startup/playback-clock cases (15 failure records, 93 cases, 2 provider skips).
Its audio-environment receipt listed no devices. The exact downloaded binary
passed all 12 unchanged affected cases locally in 36.325 seconds. This bounded
replay does not turn the original CI run green or establish full-suite order
independence, native x64 hardware performance, or physical audio quality.

Mac main CI run 39811541170: ARM job 119938818183 passed; Intel job 119938818436
built successfully and was still qualifying at 17:04 UTC. Inspect the final
receipt before declaring Intel complete. No job was restarted.

Computer Use inspected isolated PID 2792 using a disposable F04 copy, silent
48 kHz / 480-frame playback. Dock search accepted typing, Tab/Shift+Tab and
Backspace. Scratch point dragging saved; Main Undo/Redo restored point values
and selection. Explicit graph dock/Float worked. Two problems were observed:

* Fit shrank the dense graph to unlabeled blocks at the 15% zoom floor and
  clipped nodes in the narrow dock. Floating retained unreadable blocks.
* Scratch Redo briefly displayed straight segments before the correct smooth
  motion/step fader preview arrived. Source confirms the straight-handle fallback.

The copy was saved and the owned process exited normally; source fixture and
executable hashes were unchanged. Floating helper keyboard input reactivated
Main, so it does not qualify floating typing continuity. The 473-second run
reported 2,048 deadline overruns and `realtimeAuditPassed:false` under x64
emulation; it is not clean realtime evidence. Screenshots do not prove absence
of temporal flicker. Local receipts are under
`bin/mac-gesture-20261022/main-0fe8f3400/windows-x64/`, including verified artifact
identity, `device-order-diagnostic.json`, and `foreground/computer-use-observations.json`.

## Source correction, awaiting a new build

`SongRoutingCanvas.hpp` separates readable focus from whole-graph fitting.
Dense graphs initially frame a stage at 100%; small graphs can still fit
together. Selection, then the channel filter, supplies the focus target.
`SongRoutingWindow.hpp` exposes a native **View** menu, **Readable view / focus
selected stage** (Home on the canvas), and **Whole graph overview** (Ctrl+Home).
Overview can zoom below 15% so it actually contains distant saved nodes. Manual
zoom has the same lower bound; pressing minus cannot unexpectedly zoom in from
an overview. Resize, float/dock, fields, selection, saved node coordinates,
routing and history retain their existing ownership. Arrange remains an explicit
saved-layout edit. These presentation changes do not change musical/API data.

`AutomationDraftPreview.hpp` reuses its bounded parser/formula cache with the
shared `scratchEnvelopeValue` evaluator and the captured cycle duration.
`ScratchGestureWindow.hpp` builds immediate 257-sample geometry during point
edits, history refresh and selection while worker preview is pending. Invalid
formulas retain handles and validation diagnostics; they no longer masquerade
as valid linear interpolation. Drawing does not call the worker or evaluate
formulas. Ordinary automation retains its original evaluator and timing.

## Required bounded checkpoint

Build once with the stopped-plugin-repair source on this temporary branch;
do not build these tiny UI changes separately. Next permissible start after
the preceding main checkpoint is **2026-10-22 17:09:52 UTC**. Record actual
local and CI build starts and use the latest start for the following hour.

* Local ARM64: application/scanner, workspace harness, shared parameter-preview,
  parameter-automation-dock, graph-curve and instrument-short-dock targets.
  Run context-panel, hosted routing/parameters, scratch-editor, departure,
  parameter-preview and affected curve-window registrations. Run the real-app
  routing suite, scratch suite and retained navigation/keyboard cases.
* Native regression: dense graph initial readability, complete overview with a
  node at (100000,100000), selected Home framing, float/dock retention and exact
  unchanged document/graph/history. Existing cross-graph cable tests now choose
  the explicit overview; their musical assertions are unchanged.
* Shared/local preview regression: all eight nonscripted scratch curves equal
  playback evaluation; scripted cycle beats, zoom, invalid/corrected formulas;
  worker-busy and Undo refresh retain immediate geometry and focus.
* Both Mac architectures and Windows CI: shared host tests are mandatory for
  the stopped-repair changes described in `MAC-GESTURE-INTAKE-2026-10-22.md`.
  Native AU/VST3 repair and realtime audits must pass. Real-session Mac take
  tests requiring an explicit virtual device remain a separate required gate;
  ordinary `--ui` does not execute those cases.
* Fresh Computer Use: default-size F04 beside Pattern; native View menu, whole
  overview, readable selected stage, zoom/pan, resize and Float/Dock; scratch
  drag/release and Undo/Redo; typing, Tab/Shift+Tab/Backspace and retained focus.
  Record temporal limits honestly. Package only a passing candidate with hashes,
  scanner/runtime/notices and source provenance; preserve the existing package.

No old product evidence qualifies these newly changed inputs. Unchanged
fixture codec/exchange evidence can be reused with an explicit input comparison.
Do not merge this branch until required shared/native gates and the new Windows
checks pass. Broader parity, including complete recording UI/API and live take
release, remains unfinished.

## Frozen da8c89f07 checkpoint, 22 October 17:10–17:34 UTC

The ARM64 Release build succeeded from
`da8c89f07d0d586086e028e1f1db6cd9b25df083`. Executable SHA256:
`fbb88581c367e3724f0351963c21627d4c6e51bc4ede22e246a82265b08cc7cb`.
All 13 selected native registrations passed. The first 29-case actual-app run
had 17 failures: the legacy routing finder enumerated only top-level windows,
but ordinary navigation now opens a docked descendant. No musical assertions
were reached in those failing cases. The original failed receipts are retained.

Fixture correction 90787eeb5 enumerates both desktop roots and descendants,
keeps the exactly-one-window assertion, and explicitly floats legacy routing
fixtures after verifying the ordinary dock action. The new dense-layout case
still tests the dock. Replaying the routing/recording cases against the unchanged
executable passed 21, skipped unavailable installed Surge XT, and failed one
old single-row toolbar assumption. The compact toolbar intentionally wraps.
The corrected bounds case checks every visible compact control, window bounds
and pairwise overlap across all rows; it passed on that same binary. It is
committed in 2f89631d3. Combining these bounded replays gives **28 unique app
cases passed, one provider skip**, not a new all-green original run or CI result.
`bin/mac-gesture-20261022/stopped-repair-framing/bounded-results.json` lists
each case's evidence and hashes. No extra build was run for fixture corrections.

Fresh Computer Use on isolated PID44912, silent F04 at 48 kHz/480 frames,
observed readable initial graph stages, native View menu, complete overview,
selected Home focus, raw gain typing, Tab/Shift+Tab and Backspace, retained
Float/Dock and maximize/restore. A motion point moved from .5/100% to
.625/71.4656%; release, Main Undo and Redo showed the correct smooth curve.
The private copy was saved. Undo/Redo reset point selection; no selection
retention claim is made. The helper activates Main for keyboard input, so this
does not qualify floating typing continuity. A corner drag did not resize the
window; arbitrary resize remains unqualified in this visible run. Screenshots
do not establish absence of transient flashes or intermediate drag-frame pacing.
Closing correctly warned about the intentionally incomplete routing draft;
Cancel retained it. No musician process or audio defaults were touched.

The visible check also found two unbuilt corrections: compact Add group/return
labels truncate, and fixed socket hit regions swallow tiny overview bodies.
The next source uses **+ Group / + Return** in compact layout and gives body
selection priority below 15% zoom. At that overview scale a body drag does not
edit routing/layout; Home or double-click focuses the selected stage for
readable editing, with a status hint. Normal-scale socket editing and explicit
Ctrl-click inspection remain intact. Native and actual-app cases exercise the
overlapping hit targets, absence of unintended edits, double-click focus and
unchanged song/history. These corrections await the next build.

Mac ARM job119969861549 failed compiling a new test that called private
`RecordedGestureCapture::sameBaseline`. 2f89631d3 replaces that call with an
independent, bit-exact point comparison; it does not widen production access or
weaken preservation. Both Mac architectures still need successful execution.
Windows run39820404888 and Mac run39820404224 must be inspected for terminal
results. No CI job was restarted. The latest coordinated build start was
17:11:23 UTC; **no next build before 18:11:23 UTC / 11:11:23 Pacific**.
Do not package or merge this candidate as fully qualified while these gates
remain unresolved. The preceding 077e ARM64 package remains available.
