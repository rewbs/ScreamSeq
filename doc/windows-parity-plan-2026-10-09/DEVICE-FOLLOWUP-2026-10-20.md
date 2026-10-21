# Retained x64 device-dependent follow-up

## Result and scope

All **ten** device-dependent cases that failed in Windows CI job `118569879460`
passed locally on the **unchanged CI executable**, with no skips or errors.
The test suite took **23.328 seconds**, within a 24.735-second availability
probe/test run from **10:51:15 through 10:51:39 UTC on 20 October**.
No build or source edit was used to obtain these passes.

This was Windows 11 ARM64 running the x64 binary under emulation, with an
available WASAPI speaker endpoint. It is additional native-process/device
evidence, not a change to the failed Windows Server CI result or proof on a
physical x64 host. Diagnostic output was explicitly silent. System audio
defaults and the musician's preferences were not changed.

The targeted cases were:

- Remapped transport shortcuts preserve playing song and retained editor.
- Live loop changes preserve region, device, cursor and recording take.
- Native playback commands retain occurrence/range and detached cursor.
- Live MIDI take recovery is immutable; restore creates a fresh stopped take.
- Queue loss retains the stopped take until explicit review.
- Stop drains multiple MIDI batches without later step entry.
- Timestamped MIDI chord, Stop dry run, one Undo/reopen and native review.
- Sample-dock typing, live mode and intentional text focus.
- Native sample/instrument typing, capture, text and close.
- Key-up during document-worker waits cannot start a late voice.

The final three retain actual host reports at **48 kHz / 480-frame periods**,
with `silentOutput:true`, 7/109/60 serviced callbacks, no processor fault,
deadline overrun, starvation indicator or device error in their short
observations. These reports explicitly have `realtimeAuditPassed:false`.
They are interaction/device checks, not sustained-performance qualification,
audio audibility checks or a replacement for callback allocation audits.

Each fixture created a never-switched desktop, used PID-scoped pipes/native
messages, private preferences and owned scratch songs, and closed only its
own processes. The helper's foreground/clipboard invariants passed. A final
process inspection found no remaining ScreamSeq process. No foreground visual,
DPI, Narrator, physical MIDI or audible speaker-quality claim follows.

## Provenance and reuse

Source: `28d757f65c66b970a5b57599984c60a51ac69f85`.
Binary source: PR merge `dc0479ec466015280566ba7b9341daf865c03730`,
whose tree was verified identical to that candidate.
Executable SHA-256:
`5401656f964672276b66ee65175bb9b5eb1515e7341b2e3bdef75aae54b3e0c8`.

The retained app archive matched its manifest SHA-256
`7976892e646c8a3a9e3ac0557327ea885dc183e8f8e7c7837c324828f0f90b06`.
Every extracted path was checked against the destination before extraction.
The runner checked that its test checkout was clean and at the exact candidate,
recorded the Python source hashes, verified the executable hash before/after,
and refused to overwrite existing evidence.

Evidence under `bin/capture-recovery-20261020/`:

- `device-followup.py`, `device-followup.log`, and `device-followup/report.json`.
- `device-followup/audio-environment/`: read-only endpoint/transport probe.
- `device-followup/audition/`: the three retained actual host reports.
- `x64-device-app/`: verified retained CI executable and notices.

The report SHA-256 is
`10ed8492e26e8c27d73b5f0867e61a855bf89597e36a4f02e87ca57b1dd8ed64`.
The probe confirmed an endpoint before running the ten cases, avoiding another
known-failing device suite in an endpoint-free environment.

Reuse these results where the binary/test/fixture inputs and relevant
interaction paths remain unchanged. They do not qualify the unbuilt
recorded-clear, snapshot assertion or Mac exact-history fixes. Windows plugin
gesture capture remains absent; MIDI-note recording is a different owner.

## Build boundary

Mac Intel job `118569881392` in run `39405368725` completed its build at
**10:47:45 UTC**, then entered native tests. The Apple Silicon build completed
at 10:19:44 UTC. These times come from the run's raw jobs response, not elapsed
time guesses. Keep that Intel job running.

The next coordinated native build is therefore **not before 11:47:45 UTC**
(use **11:48 UTC** as the conservative gate). No new local build or push that
would trigger CI is needed for this evidence-only follow-up.

## Final Intel checkpoint

At the 11:03:54 UTC poll, job `118569881392` was terminal. Its build, 134
native tests and 35 interface groups passed, followed by the same
`undo-plugin` conformance failure as Apple Silicon. All seven editing
contracts, 28 API observations, 21 codec observations, edited-metadata and
duration-default checks passed before that failure. No retry was dispatched.

The complete saved Undo failure file is byte-identical on both Macs:
SHA-256 `5a020e371ea40bbc46e9e4060a110fd529baaaa1a3acb7cc9ff927fa981e20cd`.
Its sole typed difference is the empty plugin blob becoming the same 75-byte
default state. The prepared `fc5d586ad` repair still needs execution; this is
paired confirmation of the cause, not a test of the repair.

Intel logs artifact `12260460828` was verified and safely extracted into
`bin/capture-recovery-20261020/mac-intel-ci/`; ZIP SHA-256:
`a1f726e2e6f371259db1bc59c22121f19b0b03c055e558ce0986ee1962c9c498`.
Executable SHA-256:
`5994f9d0dbfb92e40aa6d0f8e5ae80f5b2eac35338b156772d63ca54ea2ca3fe`.
Overall conformance report SHA-256:
`927855b1117d03d607e0526d0ba345b76c8058bf393735850ab717975b6d31ce`.
Composing-history report SHA-256:
`f59eae3016bf86cd1f9ca5b7c235626b1f2811e80b14248b6aa208ff5c0c057d`.

All three candidate CI jobs are now terminal. There is nothing to poll or
restart. The next build gate remains **11:48 UTC**, measured from Intel's build
completion, while source work continues.
