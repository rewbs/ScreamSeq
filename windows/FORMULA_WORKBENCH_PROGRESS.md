# Windows formula workbench — 2026-09-21

Graph pattern curves and song envelope-bank masters now offer **Expand** for a
separate native formula workbench. **Guide/Reference** opens a searchable native
reference, also available from the command palette without selecting a graph.
Full Mac parity remains in progress. A fresh fetch found no new upstream changes
beyond `bcfe0f8a7` on the remote default `codex/screamseq`; there is no `main`.

## Implemented workflow

The workbench contains a multiline monospaced system Rich Edit control, shared
formula reference and cached normalized curve preview. Ctrl+Space completes a
value/function; completion also appears after a short typing debounce. Arrow
keys select a candidate, Enter/Tab accepts and Escape dismisses. Insertion checks
the complete 2,048-character bound before changing either text or selection.
Multiline positions follow Rich Edit's paragraph coordinates. Local Undo/Redo
stays in the code draft and snippet insertion forms a separate Undo step.

Reference search covers names, descriptions and categories. Enter, double-click
or Insert replaces the code selection. F6 switches code/reference focus;
Ctrl+Enter uses the validated formula. The standalone reference preserves the
main pattern context. Minimum windows are 840×600 for the workbench and 440×450
for reference, with DPI-scaled native controls and wrapped help text.

Preview captures the complete envelope and current text generation. Any edit
immediately revokes the previous valid result. Worker results publish only to
their captured generation; a busy worker schedules another read-only preview.
The shared parser/evaluator supplies 1,024 values. Painting uses cached geometry;
there is no formula evaluation, parsing or document access in drawing/audio.

**Use formula** rechecks the captured document, parent draft generation, point,
selection and original point data. It changes only that local point. The graph's
Apply or bank's Save master performs the existing guarded transaction, history
and persistence. Newer parent edits or a changed selection retain the text and
reject Use. Close retains an unsaved draft; reopening raises the same window and
keeps maximization. Discard is explicit. Graph and bank workbenches are separate.

The native tool shell now ignores Rich Edit paint/focus/scroll notifications
for layout and updates fonts only during window size/DPI layout. The original
paint-triggered layout loop starved preview timers and worker completion; native
tests exposed it before the fix. Consumed Enter/Tab/Space commands also suppress
their queued character, preventing an extra character after completion.

## Shared preview boundary correction

Bank templates accept 65,536 rows and rows per beat. Both preview endpoints had
an off-by-one beat limit and used the 4,096-row pattern point validator, rejecting
otherwise valid bank shapes. Windows and Mac now use the existing shared bank
shape validator for preview geometry and allow the complete beat range. The
shared API schema and guide match. Fractional preview beat divisions remain
supported; no ordinary pattern-edit limit, evaluator or file format changed.

The boundary regression first failed against the previous executable and then
passed with the fix. The C++ Timeline test covers exact beat values at the maximum
span and rejection beyond the beat, point, span and row limits without history
or playback changes. A corresponding Mac session regression is added; the Mac
target has not been compiled or run on this Windows host.

## Qualification

Nine native workbench tests passed in 16.801 seconds against ARM64 Release.
They use real controls on owned private desktops and the exact-PID API, covering
completion, multiline insertion, local Undo/Redo, changed-caret and overflow
rejection, search/insertion, F6 focus, minimum control bounds, invalid-expression
revocation, domain-error fallback, selection/draft/document guards, retained
windows/text, bank/master isolation, maximum bank shape, parent history and
save/reopen. These are functional checks, not foreground aesthetics evidence.

The rebuilt Timeline CTest passed (1 test, 0.17 seconds), linked against the
current `windows-parity/Release` engine libraries.

ARM64 executable SHA-256:
`00DD9174F9575811BCDBAD480237B52CEF9E5E37488569442510D16358F4394B`.
Build: `bin/windows-formula-build.log`.
Focused checks: `bin/windows-formula-tests.log` and
`bin/windows-formula-timeline.log`.

The complete Windows app suite passed **123 tests**, with no failures or skips,
in **194.223 seconds**. Installed Contourtonist, OrbitCab and Surge XT caches,
provider fixtures and opt-in live parameter tests were enabled. Lifecycle tests
cover discovery, editor open/close, native edits, state capture, independent
history, removal/restoration, unavailable-plugin preservation and project
reopen. Live parameter checks remain bounded one-second stages, not long-session
or worst-case capacity qualification.

The saved scripted graph again rendered identical PCM at 44.1/48/96 kHz over
17/128/4096/8193-frame blocks, one second per render, with zero partition delta
and an unchanged document. This does not resolve OrbitCab's separate discrepancy.

Full log: `bin/windows-formula-qualified-app-tests.log`.
Plugin evidence: `bin/windows-formula-plugin-evidence/`.
Curve evidence: `bin/windows-formula-curve-evidence/`.
All task-owned test processes closed; foreground/clipboard preservation checks
passed and system audio defaults were unchanged.

Preserved package: `bin/windows-checkpoints/formula-workbench-20260921/`. Its
manifest records the source commit/tree, executable hash and per-file hashes.
The shared DSP is unchanged from `d619ae439`; that checkpoint's 29 rebuilt
CTests are historical evidence, not a new run for this frontend change.

## Remaining parity work

Inline completion in compact fields, song routing overview, native parameter
automation/instrument envelopes with bank/workbench entry points, instrument
loop/sustain/release markers and graph command lanes remain. This workbench does
not establish simultaneous/persisted dock layouts, global transport shortcut
parity, full accessibility or foreground aesthetics/60 Hz qualification.

Live opaque-state/structural publication, presets/path resolution, MIDI,
recording/recovery, device selection, x64, reciprocal current-format Mac reopen
and long loaded realtime qualification remain open. The installed OrbitCab
partition discrepancy and Surge first-editor zoom-state playback stop remain
documented limitations; no audio threshold has been relaxed.
