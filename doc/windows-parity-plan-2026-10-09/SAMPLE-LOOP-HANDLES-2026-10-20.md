# Direct waveform loop handles

Status: **source implementation; unbuilt and unqualified**. This complements
[stroke release](SAMPLE-STROKE-BATCH-2026-10-20.md) in the same sample-editor batch.
The supplied reference's waveform/loop rows 14–16 and C30 remain the acceptance
context; no screenshot is treated as proof of sound or interaction.

## Current source finding

Mac `WaveformView.swift` draws distinct normal/sustain handles, supports pointer
movement, Tab selection and one-/64-frame arrow nudges. `SampleLoopEditor.swift`
routes them through existing loop writes. Windows previously drew loop lines
and offered numeric fields, selection-to-loop and snap, with no boundary hit
targets or keyboard marker selection. This is a source-confirmed interaction
gap; no new Mac runtime qualification was performed.

## Native implementation

`windows/App/SampleLoopHandles.inc`, included by `SampleDetailWindow.hpp`, owns
four labeled waveform handles. Normal markers are on the upper edge, sustain
markers on the lower edge, with distinct colors and text; selection has an
outline. Hit areas exceed the drawn marker. Invisible/disabled/invalid raw
boundaries do not become invented handles. Very small canvases omit markers.
Existing labeled native fields remain the alternative keyboard/accessibility
path; this does not claim screen-reader coverage of the custom-drawn handles.

- Pointer drag stages bounds and submits one `sample.loops.set` on release.
  Unlike Mac's existing immediate-write callback cadence, Windows commits once
  per completed drag to keep one chronological Undo entry. The accepted audible
  loop remains unchanged during tracking; no live audition claim is made.
- Grab offsets, including edge-inset markers, are preserved. Clicking without
  movement is a no-op. Bounds clamp to the sample and cannot cross the opposite
  marker or collapse an enabled loop.
- Tab/Shift+Tab cycles visible handles from waveform focus; arrows move one
  frame and Shift+arrows 64. Each completed nudge uses ordinary history.
  Ctrl+arrows retain viewport navigation; F6 returns to native fields.
- Escape/capture loss restores only the unchanged gesture's prior draft.
  New raw input, changed geometry or stale document cancels the gesture without
  overwriting newer text or rebasing it to another sample. Automatic field timers
  cannot publish the intermediate drag.
- Existing staged drawings and dirty loop fields must be resolved before a new
  marker gesture. New document departure sees even an untouched captured handle
  as retained work. Hide cancels the gesture.
- Accepted writes use `SampleMutation.inc` completion/readback. Failed refresh
  preserves the exact completion and raw fields for Review, without replay.

`SampleWorkflow::LoopDraft` owns the complete pair of raw loops and revision.
All model validation, independent directions, exclusive end semantics, PCM,
Undo and persistence remain in the existing shared loop operation. Only hit
testing, drawing, capture and local keyboard selection are Windows-specific.
No project version, stable identity, sample payload or audio/device code changes.

`workspace.get.sampleDetail` adds read-only `loopHandles` and `loopDragging`.
The API guide also corrects older statements that saved loops still require
explicit Apply; numeric loop edits already save automatically.

## Required bounded checks

Add these sources to the planned recipe/sample candidate before its single
configure/build. The earliest next build remains **04:50 UTC on 20 October**.

`workspace-sample-result-tests` adds real native handle messages and thread-local
keyboard state: zero-motion edge clicks, staging before release, Escape/capture
loss, preservation of newer invalid fields, normal/sustain independence,
one-/64-frame nudges, nonempty clamping, no-op history, single Undo/Redo, unchanged
PCM/pattern context and retained refresh failure. Existing recovery assertions
remain unchanged.

The actual PID-pipe `test_loop_handles_release_keyboard_history_and_reopen`
case adds pointer/keyboard changes, independent directions, exact PCM,
Undo/Redo, stable native save/reopen and stale-release rejection. Run it with the
numeric and pointer drawing cases on the same executable. Include existing
sample readback/automatic-edit coverage and the pending sidebar correction.
Broaden only for failures or changed inputs. The co-batched shared recipe
changes retain their separate PCM/controller/API/conformance requirements.

These new assertions have not executed. Foreground rendering, pointer feel,
screen-reader output and DPI presentation remain open while computer use is
stopped. Mac UI is unchanged; cross-platform integration gates are not waived.
