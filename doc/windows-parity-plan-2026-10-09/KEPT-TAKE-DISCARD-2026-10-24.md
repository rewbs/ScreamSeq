# Kept-take Discard review correction

24 October 2026, 22:12 UTC. Found during final review, after the 22:07 status report.

Observed: Mac's `Bridge/ParameterRecordingAPI.inc` rejects Discard of an
already-kept retained take with -32002. Windows advertised `discardable:false`
for that state but `Session/ParameterRecordingOperations.inc` did not enforce it.
`RecordedPluginEditors.inc` treats Discard as permission to abandon later input
when the editor is closed. Consequently the API could bypass the separately
guarded stopped `commit(restoreSavedState:true)` path. Native UI already avoids
offering Discard in this state; the API boundary must enforce the same rule.

Reproduction used the existing source828 ARM64 executable and retained exact
native VST3 fixture on an owned private desktop, with silent real-device playback.
The new `test_kept_take_discard_cannot_bypass_saved_state_recovery` arms a take,
captures original-time input, stops the take, generates later input through the
fixture's native slider and Keeps. Keep succeeds musically while retaining the
later input. Discard then failed to raise the required API error. The binary and
provider hashes stayed unchanged; no rebuild occurred. Evidence is in
`bin/mac-gesture-20261022/bank-closure/kept-discard-baseline/checks.json` and
`app.log`. The initial test combined a subtest with assertRaises and emitted a
secondary AttributeError after the missing refusal; this test-reporting defect
was corrected without changing the required assertion. Preserve the original log.

Correction: add the same two-line completed-take rejection as Mac before any
restoration work. Ordinary unkept Discard remains unchanged. A completed take
must use normal Commit restoration, or explicitly stopped/closed-editor saved
state recovery; Undo changes the accepted music.

Coverage added to the existing native controller lifecycle group and an actual
PID-pipe case: rejection during playback and after Stop/editor close; retained
take and unchanged accepted automation, opaque baseline and revision; successful
explicit saved-state recovery; exactly one Undo/Redo for the kept automation.
The existing CI class registration includes the new app case automatically.

Status: source-only correction, not a passing candidate. The previous review
finding remains open until rebuilt native and actual-app checks pass. Current828
CI continues without restart; Mac/shared product inputs are unchanged. Respect
the next checkpoint gate22:52:17UTC or later, and wait for current jobs to finish.
Use targeted Windows controller/recording tests plus a fresh isolated UI smoke;
reuse unchanged navigation/graph/precise/reciprocal evidence. A final package
requires passing platform evidence, review and the corrected source identity.
