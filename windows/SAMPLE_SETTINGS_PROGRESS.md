# Native sample settings and file imports — 2026-09-21

Windows Sample detail now exposes name, C-5 rate, volume and pan next to the
waveform. Apply settings sends only edited fields through the existing guarded
`sample.patch` transaction. Enter in a settings field applies it; Discard
restores the captured saved values. A settings draft survives Close, refresh,
stale revisions and attempted sample selection. Reload is the explicit way to
discard and recapture. Applying settings retains unrelated range/process field
drafts. Audio edits require pending settings to be applied or discarded first.

Replace opens the native file chooser for the sample captured by the tool.
It retains the sample slot and identity, so pattern and instrument references
continue to address that sound. Cancel changes nothing. A changed document or
draft during the chooser rejects replacement without modifying the song. The
new Create instrument action uses `instrument.create` with that same captured
sample. Both actions retain shared Undo and native persistence.

The main Import action accepts multiple files and uses `sample.importMany`.
It creates mapped instruments by default when the song already uses instruments.
The command palette also has explicit sample-only and mapped-instrument imports.
The whole batch is decoded and validated before commit; a corrupt member leaves
the song unchanged. One Undo restores the previous inventory. The first imported
sample is inspected, and its sound is selected for typing when representable in
the song's current sample/instrument mode. Choosers preserve the working directory
and reject more than 128 paths or an oversized path buffer without partial import.

The shared APIs already provide these musical operations; no new document
format or duplicated Windows editing semantics were introduced. Keyboard F6
now leaves the main sound chooser and other native text/list controls through
the application's normal queued-key path. `workspace.get.status` exposes the
main status text for diagnosis, including a native command's rejection reason.
The song-routing test now includes that state on visibility failure; its
assertion and success criteria are unchanged.

## Partial sample settings corrections

A new silent-playback test found that applying the current volume to a freshly
reopened sample stopped playback and created history. `sampleSettings` always
enabled the sample-panning flag, even when `pan` was absent from the request.
That could also change the sound's position on a name or volume edit.

The shared function now accepts which settings groups were explicitly requested.
Windows passes field presence through; the Mac bridge and API dispatcher were
updated to preserve it before merging saved values. Omitted pan retains channel/
instrument panning. Explicit pan, even the same numeric value, still enables the
sample override. The API guide documents this distinction. The Windows asset
test verifies byte-identical no-op storage, no stop/history, rename preservation,
explicit override and Undo. The Mac adapter change has not been built or run on
macOS here; it requires the reciprocal platform gate.

A second native regression exposed the same mistake in the rate path: a
volume-only no-op recalculated XM relative tuning from the unrelated raw C-5
field. Partial changes now leave every omitted rate, volume, pan and loop group
untouched. The regression fails on the preceding executable's shared source and
passes only when tuning is preserved. It also checks loop edits leave XM tuning
unchanged, and renaming preserves an untouched 384 kHz rate and sub-unit volume.
The existing full-settings entry point retains its default behavior.

## Qualification

The final ARM64 executable has SHA-256
`5E54096A2BBB71D07946E0EC44FA041ECEE65A28234EAE8F3A13C28FC8023607`.
The app, shared engine, hosted renderer and native tests were rebuilt together.
All 29 primary CTests pass in 14.88 seconds. The separate asset suite was rebuilt
against this checkout's `bin/windows-parity/Release` libraries; all six asset,
import and external-file cases pass in 3.31 seconds, including the omitted-field
regressions.

All eight new actual-application tests pass in 28.132 seconds. They cover
one-step history, no-op and invalid values, retained/stale drafts, name/rate/
volume/pan persistence, Unicode and spaced paths, atomic batch failure, mapped
instruments, replacement while another sample is inspected, cancelled/stale
choosers, saved instruments, minimum-size control bounds and queued F6 focus.
The short silent WASAPI case confirms unchanged and invalid settings leave
playback active with zero reported overruns/faults, then a real volume edit
stops before committing. This is functional evidence, not a capacity benchmark.

The first seven-case harness run had three wrong snapshot-key accesses and a
focus test that sent F6 directly to a combo instead of through the application's
message queue. Those fixture errors were corrected without weakening assertions.
The subsequent silent no-op failure was the actual shared panning defect above.
The first standalone asset-suite command omitted its required disposable
`TMPDIR`; after supplying it, every case ran and passed. Original logs are kept.

The preceding executable, SHA-256
`FCC176C967AA680C05019DEAC574976C353FF3406F3248923222DDDF8D6391F8`,
passed all 236 application cases in 628.449 seconds with no skips. It predates the
additional omitted-rate/volume/loop correction. On the final executable, 235 of
236 application cases passed in 622.726 seconds, with no skips. The formula-bank
independence case failed because no envelope-bank window existed after its open
command; formula evaluation and parent application had not been reached. The
isolated nine-case formula-workbench suite then passed in 14.587 seconds on the
same binary. Twenty additional repetitions of the failing case also passed in
90.527 seconds. Its opening assertion now includes the full workspace/status for
diagnosis. These results do not establish the cause of the intermittent failure.
The preceding typing checkpoint
had one intermittent song-routing visibility failure, followed by a passing
ten-case isolated routing run. That remains an unresolved prior failure rather
than being attributed to sample settings.

Evidence is under `bin/windows-sample-settings-*`. The preserved checkpoint
package is `bin/windows-checkpoints/sample-settings-20260921/`, with source
commit, executable/file hashes, notices, raw logs and current workflow/audio
fixtures. Private-desktop tests preserve the original foreground window and
clipboard and do not change device defaults or output volume. They do not
establish foreground aesthetics or sustained presentation performance. The
manifest retains the failed full-run count; this is not a fully passing release
qualification.

## Updated remaining work

A fresh fetch still finds upstream `bcfe0f8a7` already integrated, with
`codex/screamseq` as the default branch and no `main`.
Installed Contourtonist, OrbitCab and Surge XT lifecycle cases remain in the full
app gate. OrbitCab's partition discrepancy and Contourtonist's audible automation
limitation are unchanged; the new controls do not resolve them.

Next confirmed Mac gaps are native instrument import and visible keymap
inspection, then the sample-library index/browser and its preview/multisample
workflow. Windows already has shared instrument import/multisample APIs and
numeric keymap range editing. Device choice, MIDI/recording/recovery, persisted
workspace/accessibility/configurable keys, live structural/opaque publication,
x64/bridging, foreground visual qualification, sustained loaded audio and
reciprocal Mac format-17 reopening remain open. Sample export is a separate
enhancement, not a confirmed existing Mac workflow. Full parity is not complete.
