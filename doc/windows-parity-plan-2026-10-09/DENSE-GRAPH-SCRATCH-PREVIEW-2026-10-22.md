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
