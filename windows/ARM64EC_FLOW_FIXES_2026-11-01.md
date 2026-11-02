# Windows ARM64EC and playback workflow — 1 November 2026

Supersedes the build recommendation in `PATTERN_CONTEXT_2026-11-01.md`.
The musician's ARM64 process (PID 11116) and project were left running.
Separate build: `bin/windows-arm64ec/Release/ScreamSeq.exe`.
Build/source receipt: `bin/windows-arm64ec/BuildInfo.json`.
Executable SHA-256:
`5fa3404f9e0780d45ce484fd670e8ac98c59fab1fa16293c6a2024808b6fd058`.
PE inspection reports `8664 machine (x64) (ARM64X)`, the final ARM64EC image
format; the host has an 8 MiB stack reserve.

## Architecture and installed plugins

`windows/build.ps1 -Architecture ARM64EC` builds the app and scanner with
Visual Studio's ARM64EC tools. Auto now selects ARM64EC on ARM hardware;
explicit ARM64 and x64 targets remain available. The loader selects the x64
ABI and `x86_64-win` bundle directory, with an independent
`vst3-arm64ec-cache.json`. Rescan after switching architectures.

Both installed binaries are x64: Ohmicide[S] and Serum 2. All three classes
(Ohmicide[S], Serum2, Serum2 FX) scan successfully with the EC scanner. A
separately built **x64** fixture also passes the EC host's provider checks:
effects, instruments, MIDI mapping, sample-offset parameters, auxiliary buses,
state, latency, events and callback partitioning at 44.1/48/96 kHz. This proves
mixed-architecture execution, rather than only loading another EC module.

Ohmicide[S] loads and its native UI opens/closes. Its parameter catalog and
native project save/reopen preserve the plugin identity. Visual inspection of
the owned EC process showed its username/serial/demo dialog; the dialog says
the demo prevents audio bounces. Offline exports remained identical to dry
audio, including after changing Master Volume. **Ohmicide wet audio is not
qualified** by that test. No activation, purchase or license change was made.
`SCREAMSEQ_TEST_X64_REQUIRE_WET=1` retains the strict wet/dry assertion for a
licensed export check. Serum was scanned, not audio/editor-qualified.

Microsoft documents native EC host code with emulated x64 plugin code:
https://learn.microsoft.com/en-us/windows/arm/arm64ec
Pure ARM64 plugins use a different ABI; this does not bridge 32-bit plugins.
Ohm Force's published requirements still exclude Windows ARM processors:
https://ohmforce.com/products/ohmicide-s
Successful local compatibility tests do not change the vendor support policy.

## Workflow changes

- Top-right Play pattern/selection and Loop on/off controls. Ctrl+Space starts
  the pattern or selection; Ctrl+Alt+R toggles looping. Ordinary Play/Space uses
  song playback. Looping applies to the currently playing region/song.
- Pattern FX context lists cursor-row commands, inherited active gates and
  supported continuing native effects, rather than every preceding/future row.
  Source links retain the originating pattern, row, channel and FX column.
- Gate inspector changes coalesce after 300 ms. Partial invalid fields retain
  a draft. Save/close/history coordinate pending edits. Explicit Apply remains
  available for inserting unchanged defaults.
- Prepared live gate updates preserve the running clock and sounding sample
  phase; Undo/Redo publish through the same guarded queue. Changes to existing
  gates become audible at a render boundary. New past onsets wait for the next
  visit. New plugin-parameter destinations/combined structural edits may need
  stopped playback; refusal preserves transport and musical history.
- Space reaches transport with channel M/S buttons focused. Enter still
  activates the focused button. Ctrl+Shift+M mutes the cursor channel;
  Ctrl+Shift+L toggles its solo.

The gate runtime reserves stable sample destinations, but only actual gate
commands request implicit floating-point routing. Bare songs retain the legacy
integer mixer path. Prepared schedules and their retirement stay off the
callback. The host C++ allocation/deallocation audit passes (not a claim about
vendor malloc/free/locks). Native Mac execution was unavailable; shared runtime
and API changes were exercised on Windows.

## Evidence

Logs and disposable audio are under `bin/windows-arm64ec/qa/` (ignored by Git).
`native-final.log`: all **11** selected native/shared groups pass, including
gate/activity UI, channel/header behavior, transport/context controls, shared
gate/activity, prepared-host audio, controller live history and VST3 hosting.
`x64-provider-final.log`: the separate x64 fixture passes against the EC host.
`ohmicide-lifecycle-final.log`: the actual installed plugin's load, editor,
parameter and save/reopen check passes with a clean outer isolation harness;
the wet-audio qualification is explicitly excluded for the demo.
`flow-app-tests.log`: ten real-app tests pass, including current/continuing FX,
held/released voices, routing links, persistent mute/solo and automatic GT.
Quarter-gain and four alternating open/closed sample windows match expected
48 kHz float PCM with maximum error **0**. Hosted gate tests cover 44.1/48/96 kHz
and blocks 17/128/511, clock continuity, queue refusal/coalescing, removal and
Undo; audited C++ callback allocations and frees are both **0**.

Earlier failed probes are retained: an outdated intermediate copy ended during
dry export; the rebuilt app completed that export. Immediate editor-close
requests initially used a stale revision after vendor state initialization;
the lifecycle test now captures the initialized state before its guarded close.
An initial lifecycle run passed internally but the isolation harness detected
an unrelated foreground-window change (neither window belonged to the test).
This is not recorded as a clean outer-harness pass.

No hardware audio latency, sustained performance or complete vendor preset
compatibility claim is made. Existing bounded activity analysis does not model
vendor-private tails or all legacy tracker effect memory.
