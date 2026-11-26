# Gate Apply investigation

The reported workflow was typing `GT` in a pattern FX cell, editing a volume
multiplier gate for a sample, and clicking Apply. The original ARM64 build can
reject Apply during an unrelated document-worker read with:

> Document worker is busy; no mutation was queued · Draft retained

No command is saved in that case, explaining both an absent GT cell and unchanged
audio. This was reproduced through the actual Windows application and native
button notification (`bin/windows-gate-fix/qa/gate-functional-final.log`). Simple
uncontended Apply had passed, so the earlier gate-runtime test alone did not
qualify this interaction.

## Change

The inspector retains the exact request after a busy error only when its outcome
explicitly proves `NotCommitted`. A 120 ms timer retries that original request
against its original song revision and captured cell. Changed fields, changed
song revision, explicit reload or closing the inspector cancel unsent intent.
Unknown or committed outcomes are never automatically retried. A successful
save identifies GT/GX and the exact pattern, row, channel and FX column.

The change is confined to Windows inspector admission and observation. The
shared gate renderer, project format and editing API semantics are unchanged.

## Validation scope

Native regression exercises typed GT, Apply inside the real Application wait
pump, cursor movement before retry, the grid's command/display code, one Undo and
Redo, explicit write-outcome classification, and cancellation on newer text,
changed revision and Hide.

Actual-app tests exercise native button Apply, changed fields and beat fractions,
Undo/Redo and native save/reopen, including a disposable copy of the reported
song. Synthetic sample PCM is exported through the real Windows worker and
hosted mixer at 48 kHz, stereo float32. Active-song checks compare constant 25%
gain and four alternating open/closed half-beat gate windows against an ungated
reference. Export tails after transport ends are outside these comparisons.

The original running application and its song were not edited or replaced.
Inspection processes use private desktops and disposable files; offline renders
do not change audio devices. Screenshot capture failed with D3D11 device removed,
so this is functional/native-control and offline-audio evidence, not sustained
visible rendering, hardware playback, or realtime-performance qualification.

## Final results

ARM64 Release build succeeded in `bin/windows-gate-fix`, including the app,
workspace harness, native tool harness and shared pattern-gate test.
The app includes the preceding current-main alignment changes on `58fad76f5`.

- `workspace-gate-inspector-tests`: passed, 10.97 seconds; real busy-worker
  admission, captured-cell display, single Undo/Redo and cancellation checks.
- `native-tool-window-tests` and `pattern-gate`: both passed, 11.80 seconds total.
- `GateAppTests`: all three passed, 3.552 seconds; typed GT, save/reopen,
  disposable song copy and hosted sample audio. Both quarter-gain and four
  alternating open/silent windows had maximum PCM error **0.0**.
- `git diff --check`: passed. The musician's original process remained running.

Evidence is under `bin/windows-gate-fix/qa/`: `gate-fixed.log`,
`gate-native.log`, `gate-supporting.log`, and the three WAVs in `audio/`.
The earlier `gate-functional-final.log` is deliberately retained as the failing
baseline, despite its historical filename. This focused qualification does not
supersede the broader GPU-related limits in the current-main alignment report.

Executable: `bin/windows-gate-fix/Release/ScreamSeq.exe`.
SHA256: `D4DFDD953443883CA20ED82EC1FC92983949CD66686D90C58A0C27AD1D4223A7`.
Source hashes are recorded in `bin/windows-gate-fix/BuildInfo.json`.
