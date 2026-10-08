# Main integration and Windows feature coverage

Checkpoint date: 2026-10-08. This report covers merging main
`b4680f32f60a8d333a69f21fda5497f79ce10aaa` into the Windows branch at
`e68492ceb0ef7e8d3de3eeefcb69a0ed9d16c11e`. The comparison starts at their
common ancestor `d5d376fa0ced527cc901d8555076cadbf98d75d4`.

## Integration

The merge preserves the Windows retained editors, configurable shortcuts,
arrangement matrix, MIDI recording, recovery and native project compatibility.
Main's removal of unused upstream products is retained, along with the retained
engine's license, attribution and runtime notices. No removed MFC product tree
was restored as a substitute for the Windows-native frontend.

All 18 conflicted files have combined resolutions. In particular, shared history
keeps matrix-copy and cache-admission protections alongside prepared publication;
Windows keeps its recovery-aware close path and configurable native editor keys
alongside main's window lifetime, physical-key and device-loss fixes.

## Incoming features supported on Windows

| Incoming functionality | Windows counterpart |
| --- | --- |
| Prepared graph/routing changes and graph controls | Worker-prepared shared plans, published by the UI producer only after playback identity and epoch checks. Rejection occurs before document/history mutation. Unsupported live edits ask for stopped playback. |
| Song-root modulation | Guarded APIs, saved source/mapping metadata, native source and parameter-mapping controls, MIDI CC delivery, and the existing Graph Curve owner extended to song scope. |
| Processing groups and recipe editing | Native Graph workflows window: group operations/export, processor insertion, detach/heal, bypass and parameter controls through the shared document operations. |
| Graph presentation metadata | Frame/comment/collapsed-card and cable-reroute editing, plus display and hit geometry in the retained reusable-graph canvas. |
| Signal inspection | Native port selection, waveform/spectrum, listening, latency/meters and overload clearing through the shared signal-observation APIs. |
| Parameter activity and provenance | Bounded processor-specific capture, final-value trace, recent events, source inspection, and explicit navigation to existing parameter/graph/recorded editors. Stale or missing sources reject without substituting another target; retained drafts and intervening cursor moves are protected. |
| Recorded parameter points | Stable-plugin API reads/edits, dry-run and exact validation, shared Undo/Redo and persistence; retained native seconds/value fields and existing full-lane editor. |
| Live sample loops and automation curves | Prepared, bounded publication for supported changes, including ordinary Automation Set/Remove/Transform and native Undo/Redo. Sample-loop geometry is admitted before the song edit and queued to the captured renderer. |
| NF/NR record nudges | Inline strength-percent and fractional-row duration entry, guarded retained drafts, keyboard/double-click access, shared Undo and project persistence. |
| Precise-note clipboard and Cut | Windows accepts and emits Pattern 2 precise notes with relative timing, and clears a cut selection only after successful clipboard publication and a captured-target check. |
| Context-only input selection | `workspace.input` validates revision/context guards and changes instrument/octave without creating musical history. Integral JSON numbers match Mac semantics. Empty instrument slots remain explicit in the Windows selector and newly captured Precise Notes. |

All musical changes use the shared model and guarded editing APIs. Native format
container version 6 and metadata version 17 remain unchanged; incoming fields are
additive compatibility data.

The integration also corrects persistence for additional plugin main-input
routes. The incoming API and shared mixer support input 0, but both native
metadata decoders still required inputs 1–63. Windows and Mac now decode the
same 0–63 range. Regression cases assert exact route metadata, one Undo/Redo,
and native save/reopen, alongside auxiliary-port validation. Supported inactive
auxiliary inputs can be connected directly; playback preparation enables those
ports on its own plugin copy without changing the saved explicit bus flags.

VST3 editor creation now synchronizes the current manual parameter baseline to
the vendor controller before creating its view. Previously, queued recipe
values could remain invisible until the editor timer ran, allowing the editor's
initialization gestures to capture an older baseline. A failed synchronization
rejects editor opening. This preserves the existing explicit recipe commit and
shared history behavior.

The new Graph workflows controls load the saved coordinates immediately when a
cable selects its first reroute point, and load the saved manual parameter value
when a processor selects its first parameter. Move/Set therefore preserve the
selected value until it is edited. Dependent selections also retain typed drafts,
including the node selection inherited from a saved frame.

## Qualification

The Windows ARM64 Release build passed. Its executable SHA-256 is
`4B1CB7FBEB2E20C89C4630DD93697E509BBC76F07F702DDC13D4C37D67AE2420`.
The frozen 1,877-file source/test inventory is
`a000ad49e599f23ab021679c2a22c25568e0e2e2448308e457d474a6b7f35e6a`
(`bin/main-integration/source-8.json`). The candidate was rebuilt after the
saved-value fixes and verified against this inventory.

- Native CTest: **77/77 passed**, including 18 native-UI and 25 worker tests
  (`bin/main-integration/ctest-source8.log`). The registered native metadata test
  covers input 0, complete field equality, historical versions and invalid data.
- API suites: **2/2 passed** (`bin/main-integration/api-tests.log`).
- Additional VST3 native editor regression: **passed**, including initial
  controller values before `createView`, actual edit/PCM delivery and three
  reopen cycles (`bin/main-integration/vst3-editor-boundary-run-1`). Its owned
  process tree ran on a never-switched private desktop; all descendants exited,
  with foreground, clipboard and input hashes preserved.
- Focused application checks: **6/6 passed** on the final executable
  (`bin/main-integration/focused-source8-app-tests.log`).
- Full application suite: **414/414 passed**, with no failures or skips, in
  1,046.219 seconds (`bin/main-integration/full-source8-app-tests.log`). The
  runner verified unchanged source/executable and musician-process identity.
  This run enabled the installed effect/instrument/provider caches, migrated
  format-17 reference, arrangement fixture and owned silent WASAPI cases.
- Visual qualification: **15 source-matched views** at 192 DPI passed native
  geometry/value checks and two independent original-resolution reviews
  (`bin/ui-capture/evidence-merge-graph-source8-second`). The views cover eight
  workflow pages, retained stale fields, expanded/collapsed graph presentation,
  three Parameter Activity pages and the inline nudge fields. These are private
  renderer/native-control composites, with the cosmetic limits listed below.

These results qualify the integrated candidate. The separate API/provider
targets' sources remained unchanged through its final UI fixes. Earlier
checkpoint counts in other reports do not qualify this merge.

Evidence paths refer to retained local outputs in the managed integration
checkout. Earlier failed and cancelled attempts remain available: the first
full application run, the preview-worker race in its focused rerun, the capture
that exposed saved-value initialization, and the later capture's strict integer
versus decimal-string assertion. The final capture checks finite numeric values
against saved data and exact native text against its snapshot. The source-drifted
build 7 was discarded; build 8 recompiled its affected inputs after they froze.

Work uses a separate managed checkout and build directory. The musician's
existing arrangement-checkpoint process is preserved, as are the earlier
qualified packages and the primary checkout's untracked assessment document.

## Remaining work and limits

- Mac now offers more direct table/gesture editing and automatic commit behavior
  in several editors. Windows supports the corresponding musical edits through
  its retained fields and explicit Apply workflow. Matching those interactions
  needs a separate, deliberate draft/Undo design; this merge does not claim
  identical interaction behavior.
- The Mac canvas has richer contextual/card manipulation. The Windows graph
  workflow controls expose the incoming editing functionality, but its canvas
  gestures and visual integration still need refinement. Workflow forms can be
  more compact, long identity labels need clearer presentation, and annotation
  text needs better wrapping and separation from contained cards.
- Live publication is bounded by the shared prepared-plan capabilities. Changes
  to renderer schedules, unsupported processor topology/ports, and first-time
  setup requiring a different rendering path still need stopped playback.
- Qualification here targets Windows ARM64. Mac runtime, reciprocal testing of
  newly saved projects on Mac, Windows x64 runtime, physical MIDI/audio latency,
  foreground accessibility across displays, and sustained loaded performance
  remain separate qualification work. The local format-17 reference is a migrated
  fixture, not evidence of a new Mac-to-Windows-to-Mac round trip.
- Private-desktop renders and offline audio prove the tested layout/processing
  cases. They do not establish foreground frame pacing or hardware sound quality.

The pre-existing broader parity backlog remains in [PARITY_HANDOFF.md](PARITY_HANDOFF.md).
