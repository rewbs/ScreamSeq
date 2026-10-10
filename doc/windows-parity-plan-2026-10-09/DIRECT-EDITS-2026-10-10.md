# Windows direct editing — 2026-10-10

## Interaction policy

Routine edits use the existing revision-guarded document API and Undo history. Instrument plugin/MIDI choices and mixer mute/solo/output choices save immediately. Text/numbers settle after 600 ms of idle typing. Envelope and automation drags save on release as one history operation; keyboard nudges and completed point commands coalesce during the short idle interval. Plugin sliders retain their existing one-write-on-release behavior (no audible intermediate preview claim).

Covered owners: InstrumentPluginWindow, InstrumentEnvelopeWindow, PluginParametersWindow, the rack parameter field, Mixer Details, GraphTrimsWindow, SampleDetailWindow settings/loops, GraphCurveWindow, ParameterAutomationWindow and AbsoluteAutomationWindow. Existing point fields save after idle typing; new points retain Set point so entering the position alone cannot create an unintended point. Assign sample completes an instrument key range without another Apply.

NativeToolWindow supplies an opt-in, one-shot timer. It removes the queued callback before submitting, preserves the active edit's raw spelling/caret during canonical readback, and never automatically retries a failed or uncertain write. Existing document, target, generation and result-receipt guards remain authoritative. A pending read/gesture may postpone submission. Hiding an editor cancels its pending timer and retains its draft; reopening exposes the retry action. Invalid, stale or uncertain edits remain visible and need correction, reload or result review. Valid writes use normal project persistence; they do not automatically save the project file to disk.

## Deliberate actions

Transform previews in instrument and parameter envelopes retain **Use preview**. A preview is never promoted to an automatic edit, including after later changes to that preview. Destructive sample drawing/processing/paste, recording takes, imports, renders, file/preset operations, new multi-field notes and commands remain deliberate operations. Processing, paste, audio-device switching, song properties and timing use named CTAs (Process audio, Paste audio, Switch output, Update song, Update timing, Write note/command). Channel-count/timing changes and audio-device switching can stop playback or affect many events; do not execute partial forms on each field edit.

Routine forms hide redundant Apply buttons. Retry controls are contextual, and destructive/preview action groups remain separate from reload/navigation/close. Narrow retained inspectors keep their existing layout and keyboard targets; geometry tests explicitly exclude the removed redundant actions while still checking the remaining controls.

## Boundaries and remaining work

This changes Windows interaction scheduling, not shared model/API semantics, native audio hosting, project metadata or macOS code. Existing API validation, history and serialization are reused. Full realtime plugin slider preview would require an owned preview/cancellation protocol distinct from durable parameter writes; repeatedly committing during a drag would create excessive history and is intentionally not introduced here.

No automatic replay occurs after an ambiguous worker completion. Automatic saving may be refused when a different editor changed the captured revision; the raw edit stays available. A compound form may still need a deliberate named command to create a new object. These are explicit exceptions, not a universal removal of confirmation.

Validation results and source/executable fingerprints are recorded separately after the cohesive native build and targeted regression run. No physical audio or foreground visual qualification is implied by private-desktop tests.
