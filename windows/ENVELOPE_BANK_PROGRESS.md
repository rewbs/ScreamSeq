# Windows modeless envelope bank — 2026-09-21

The Graph dock's **Pattern curve → Bank…** now opens a separate native editor
for song templates and independent app catalogue copies. The graph curve and
pattern remain available beside it. Full Mac parity remains in progress.
A fresh fetch found no new commits on the remote default `codex/screamseq`;
the upstream baseline remains `bcfe0f8a7`. This repository has no `main` branch.

## Implemented workflow

- Save a captured source curve as a named song template without implicitly
  applying the source draft. Use a template as an independent copy or a linked
  instance. Editing and saving a master updates its linked uses atomically.
- Edit template points with the mouse, numeric fields or keyboard; cancel a
  drag, delete points, choose any of the nine curve types, validate scripted
  expressions, and change duration or beat signature. Instrument flags and
  marker positions survive unrelated point edits. F6 switches between native
  controls and canvas; Ctrl+S saves the song master.
- Explicitly unlink the captured source, preserving an unsaved source draft.
  Remove an unused template through the existing guarded shared operation.
- Publish an independent catalogue copy, explicitly replace a selected copy,
  or import a copy into the song under a fresh identity. Catalogue writes use
  their own revision and stay outside document Undo. A damaged/unavailable
  catalogue does not disable the song bank or silently replace the file.

The bank captures its source document, graph node, pattern and parent draft
generation. Its own template draft and field generations are separate. Opening
an already visible or unsaved bank raises the existing window. Closing retains
its draft. External document changes reject stale writes, and source changes
prevent a template use from overwriting the newer source draft. Reload/discard
is explicit; it can also discard a retained bank draft from a closed document.
Completion checks preserve newer fields entered during worker requests.

`NativeToolWindow` provides the modeless Win32 shell, native controls and a
separate Direct2D surface. It paints on invalidation and defers briefly when the
swapchain is not ready; an unchanged/hidden window has no continuous presentation
timer. The canvas paints retained geometry and worker-evaluated preview samples.
Catalogue I/O, parsing, formula evaluation, shared validation and commits stay
on the existing document worker. No DSP or project-format changes were needed.

`workspace.get.envelopeBank` reports the captured target/document/revision,
catalogue revision, source guard, scope, selection, local draft, preview and
status. Musical edits use the existing Mac-compatible bank/catalogue APIs,
document Undo and native persistence.

## Qualification

Eight actual-app bank tests pass, using owned private desktops, native HWNDs and
the exact-PID API. They cover capture, linked/copy use, linked-master history,
save/reopen, independent catalogue publication/replacement/import, stale
catalogue and document guards, retained source and bank drafts, closed-document
reopening, point drag cancellation, keyboard/F6 focus, invalid formulas, timing
validation, corrupt catalogue preservation and instrument marker/flag retention.
All visible controls fit the 900×620 minimum window, with no overlap in the point
editing row. These are functional checks, not foreground visual qualification.

ARM64 Release executable SHA-256:
`57D8697401EC6997E1EEC1AB762072817F22696A815D734C1347EBE72304CA1A`.
Build log: `bin/windows-bank-build.log`.
Focused test log: `bin/windows-bank-tests.log` (8 passed, 40.285 seconds).

The complete Windows app suite passed **114 tests**, with no failures or skips,
in **179.718 seconds**, against that executable. Installed Contourtonist,
OrbitCab and Surge XT caches, provider fixtures and opt-in live parameter tests
were enabled. Lifecycle checks cover real editor opening/closing, captured
parameter/state edits, independent history, removal, restoration and project
reopen. The existing saved scripted-graph fixture again renders with zero PCM
partition delta at 44.1/48/96 kHz and 17/128/4096/8193-frame blocks, one second
per render. This does not resolve OrbitCab's separate partition discrepancy.

Full-suite log: `bin/windows-bank-qualified-app-tests.log`.
Audio evidence: `bin/windows-bank-plugin-evidence/live-parameters.json` and
`bin/windows-bank-curve-evidence/native-graph-curve-pcm.json`.
All task-owned test processes were closed. Foreground-window and clipboard
preservation checks passed; no system audio defaults were changed.

Preserved artifact: `bin/windows-checkpoints/envelope-bank-20260921/`. Its
manifest records the source commit/tree and individual file hashes. The shared
DSP is unchanged from `d619ae439`; that earlier checkpoint's 29 rebuilt CTests
remain historical evidence, not an additional test run for this UI checkpoint.

## Remaining parity work

The bank currently opens from graph pattern curves. Parameter automation and
instrument envelope editors still need their own native entry points, including
instrument loop/sustain/release controls and voice markers. The expanded formula
workbench with reference/completion, song routing overview and graph command
lanes remain. The main dock still switches editors; this modeless tool does not
establish simultaneous dock layouts, persisted floating panels, full global
shortcut/accessibility parity or a foreground aesthetics/60 Hz pass.

Shared structural envelope edits still use the existing stop-before-publication
path. Live opaque-state/structural publication, plugin presets/path resolution,
MIDI/recording/recovery, device selection, x64, reciprocal current-format Mac
reopen and long loaded realtime qualification remain open. The installed
OrbitCab partition discrepancy and Surge first-editor opaque zoom-state change
remain documented plugin limitations; no threshold has been relaxed.
