# Visual graph cables and filtering — 23 September 2026

## Implemented

- Drag output → input, or input → output. Dragging an input replaces its sole incoming cable, or the selected incoming cable when several share a socket. Option-drag adds another source in an editable subgraph. Selecting a cable exposes separate endpoint handles; handles stay separated from sockets and one another when zoomed out. Escape and dropping in empty space cancel without removing a route. Other keys cannot mutate topology during an unfinished drag.
- Drag Track 8's output onto Distortion's main input to move Distortion and the remaining Compressor chain from Master to Track 8. The reverse input-to-output gesture has identical semantics. A processor output places the moved suffix immediately after that processor; a channel's input-stage output places it before existing rack inserts. This is one document Undo step, preserves instance IDs, plugin state, automation and unrelated routing, and validates the full candidate graph before committing. Structural routing edits stop playback.
- Added `mixer.inserts.move` to the shared editing model and both native API adapters, with schema, discovery metadata and documentation. An agent can move an ordered contiguous segment, append it or place it before another insert, preview the operation, and use the usual revision guard. Existing native project fields persist the result; no format change is needed.
- Master is a final output sink on the right of default/arranged layouts. When it has processors, a separate **Master input** node shows the summing stage before them. Saved manual positions remain intact.
- Text search, node-type filtering and channel focus combine to narrow the graph. Filters only affect display. Edits and Delete preserve original edge identities even when other cables are hidden. Pending inspector edits remain visible. Fit frames the visible nodes without changing their saved positions.
- The song overview exposes known active rack-plugin auxiliary sockets, including unconnected ones. Reusable subgraph rewiring preserves audio gain and modulation range/enabled settings; reconnecting to an already-modulated parameter retains that parameter's shared base.

## Verification

- Full CTest suite: **76/76 passed**, 74.60 seconds. This includes the shared insert-move tests and extended native mixer tests. The final subsequent changes affect only canvas framing, cable handle placement and the UI's use of the same operation.
- Final AppKit interface suite passed, including both drag directions, endpoint replacement, minimum-zoom socket/handle separation, Option-drag, Escape/blank-space cancellation, Delete suppression during a drag, filtered edge editing/removal, hidden route preservation, chain-move dispatch and Master placement.
- Rendered audio at 44.1/48/96 kHz: moving the chain changes only the target track's ownership, agrees with explicit target-track insertion across 17/128-frame callbacks within 2e-7, and Undo restores the original Master render. The existing realtime allocation/free/lock audits remain active.
- Session API: no-op, dry-run, invalid input rejection, one-step document Undo/Redo, plugin-state identity, and native save/reopen passed.
- First live QA build: both socket-drag directions successfully moved the original Distortion and Compressor instances onto Track 8. Cmd-Z restored the Master chain. Actual typed node search and type/channel filters were inspected. Socket readback verified processor order, Track 8 → Master, unchanged plugin records and saved routing.
- Actual app socket additionally checked discovery, dry-run, stale-revision rejection, invalid order/anchor rejection and sidechain feedback rejection with complete state preservation on errors. Evidence is in `mac-native-qualification/2026-09-23-graph-cables/`.

## Boundaries

The song overview still describes bus ownership and ordered rack inserts. It does not park a rack effect as an unattached processor when a cable is dropped into empty space; that gesture cancels. Pattern-controlled and ordinary reusable copies are assigned through their assignment controls; double-click opens their internal graph. Transferring ownership of a send/auxiliary/main-output route is distinct from retargeting its destination; unsupported transfers explain the constraint without mutating another bus. Reusable subgraph internals support arbitrary valid audio/modulation wiring, including multiple ports, subject to existing cycle and port validation.

Windows has the shared operation and API adapter; Windows UI and native execution were not tested on this Mac. The musician's existing process and system audio defaults were preserved throughout.


## Final package and performance

The final package was launched independently and inspected again. Track 8 → Distortion moved the original two processors; text filtering enlarged/framed Compressor without changing routing. A selected modulation wire's round source handle was dragged from Source A to Source B. Actual API readback confirmed that only its source changed: minimum 0.2, maximum 0.8, base 0.4, enabled state, and the two audio connections were retained. Both manual QA sessions exited normally.

The 60-second visible graph workload ran at **48 kHz / 512 frames on BlackHole 2ch**, with 298 live edits, three saves and 14 Undo/Redo cycles. All 5,625 audio callbacks completed without overruns, renderer faults or plugin failures; maximum callback time was **2.373 ms** against a **10.667 ms** buffer budget. This is callback evidence, not a physical-device latency or audible-loopback measurement.

**The display-performance gate failed.** Mean presentation was **48.06 fps**, p99 interval **50.0 ms**, maximum interval **133.3 ms**. Grid CPU draw p99 was 0.116 ms; GPU p99 was 2.402 ms. The prior `mac-2026-09-23` package was then run with the same one-minute graph workload: **49.08 fps**, p99 **50.0 ms**, maximum **58.3 ms**, also failing the same gate, with zero audio overruns. The machine was under substantial concurrent load (load average roughly 30 during qualification; WindowServer near 100% of one core). This comparison does not establish idle 60 fps performance or prove the absence of regressions. A controlled display-performance qualification remains open; thresholds were not relaxed.

Package: `bin/mac-graph-rewire/ScreamSeq.app`. Its signature was verified and every source hash in BuildInfo matches the final checkout. Source fingerprint: `85e65f21dbf22425b210b5f345fdf5409876b3ba23ab80d1590255043fe4ab16`. Executable SHA-256: `d2851e45099edcd224665d45f8fa3a73820135ee95ae9c3b5086ca6f48b65948`.

The musician's process (PID 27685, `bin/mac-2026-09-23/ScreamSeq.app`) was neither replaced nor stopped. Save and quit that session before switching to the new package.
