# Surge plugin Undo restart fix — 2026-09-21

Restoring the installed ARM64 Surge XT instrument with plugin Undo could stop
its next audition with silent output and a processor fault. The instrument-import
checkpoint reproduced this once in its full application run and once in 20
focused repetitions. The first note, release and panic succeeded; the restored
instance failed. Those original binaries and failing reports are retained.

## Cause and implementation

A diagnostic build reproduced the same failure in 1 of 20 repetitions. Its first
provider failure was `vst3.restart-flags`, detail `16` (`kParamTitlesChanged`).
The renderer did not fault independently. The Windows provider rejected the
notification, which Surge can issue after a restored patch changes parameter
names. This is a host notification-handling defect, not evidence of a Surge DSP
failure. The vendored SDK documents bit 4 in
`mac/ThirdParty/vst3/pluginterfaces/vst/ivsteditcontroller.h`.

The provider now validates and publishes a complete parameter name/unit snapshot
on its private STA. Its audio-side parameter contract remains immutable. The
snapshot is read only by control callers; audio does not acquire its shared
pointer, allocate, query the controller or dispatch UI work. The extra immutable
contract stores only ID, step count, flags and unit group, not full SDK strings.
Notifications during preparation are refreshed after activation so the first
catalog read sees the current names. Values, queues and musical history do not
change for title notifications.

Parameter count/identity, step, flags or unit-group changes still require a
stopped rebuild of prepared automation. Invalid metadata, unsupported structural
restarts, wrong-thread callbacks, queue overflow and nonfinite output still
fault and silence. Default values are not cached by the shared facade. No
validation or audible-output threshold was relaxed.

Each backend failure now records its first reason/detail using a lock-free
64-bit atomic. Later generic process failures cannot replace the cause. The
control owner exposes rack diagnostics through `transport.get.faultDetails`
and `audio.faultDetails` in host reports. Graph/renderer failures may have no
rack-plugin detail. Diagnostics are transient and do not enter project files.

## Qualification

Final ARM64 executable SHA-256:
`AEA1752BFAC1689AE8FD243655D38528AB2A9AFC5794540B2172EC02FFA580FF`.

- Final native VST3 provider: **18/18 pass**, 4.06 seconds. Tests include repeated
  title/unit refresh with unchanged PCM and no manufactured parameter edits,
  activation-time refresh, combined/separate controllers, rejection of changed
  or malformed contracts, first-failure retention and existing overflow silence.
- Final primary native CTest suite: **29/29 pass**, 13.31 seconds.
- Final installed Surge remove/Undo/audition repetitions: **20/20 pass**,
  114.499 seconds, using the same audible-output threshold as the failing case.
- Full application suite: **241 pass, 2 fail, 0 skip** out of 243 cases,
  667.449 seconds. Surge lifecycle and audio cases pass. The mixer minimum-size
  case passes its layout body but fails foreground HWND preservation at teardown
  (`private_desktop.py:95`). The routing sidechain/auxiliary case fails before
  its window opens: the status is `Document worker busy`. A native open command
  can arrive during a background read and be rejected even after the test's
  readiness check. These failures are retained, not counted as passes.
- The first fix build also passed 20/20 installed Surge repetitions in 116.796
  seconds before the final compact-contract/activation refinements.

Evidence: `bin/windows-surge-title-final-*.log`,
`bin/windows-surge-title-final-repetition-evidence/`, and the original
`bin/windows-surge-fault-details.log` / `windows-surge-fault-details-evidence/`.
The failing diagnostic executable is preserved at
`bin/windows-surge-fault-diagnostic-binary/`, hash
`5DCA906FACC347DBB2EF14DD4522EB1B1F95126EB7BE08590BE93CAC7B949824`.
Installed Surge binary hash remains
`EE569BFFAE488BF03A5796CF0E7B2B869FF14A5BBB46B0E663856896BBABCD2E`.

Private-desktop WASAPI tests silence output after processing and do not change
system audio defaults. These measurements do not qualify vendor-private
allocations, direct malloc/free, locks, a long loaded session or foreground
presentation. No Mac runtime was available for this Windows provider change.

Next fix the native view-opening command lost during background work and add
diagnostic context to the still-strict desktop preservation check. Full parity
remains active. Sample library/browser/preview and multisample UI,
MIDI/devices/recording/recovery, workspace/accessibility/key configuration,
live structural/opaque publication, x64/bridging, reciprocal Mac format-17
roundtrip, OrbitCab partition behavior and Contourtonist audible automation
remain open. Earlier intermittent formula/routing and desktop-preservation
failures remain historical evidence; a green later run alone does not establish
their causes.
