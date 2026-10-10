# P3 implementation checkpoint

This is source status, not runtime acceptance. P0–P2 qualification remains open;
P3 does not supersede those gates. The temporary `codex/windows-parity-workspace`
branch starts at P2 `a4a281f2f` and includes both native platform sources. The
implementation sequence still converges on shared main. P3a catalogue/UI,
remaining P3b transport/pattern tools and P3c file/song work are not complete.

## Live playback loop — prepared, unqualified

Windows already supports guarded `transport.play` regions in SessionAdapter and
DocumentController, and the shared Renderer already supplies an atomic `loop`
setter. Mac's `transport.loop` uses that setter without re-preparing its renderer.
This batch connects the corresponding Windows path rather than adding a second
transport model or a new device lifecycle.

`windows/Api/SessionAdapter.hpp` adds an opt-in SessionHost capability and setter.
Only capable hosts advertise/accept `transport.loop`. It requires expectedRevision
and an actual boolean enabled, rejects unknown fields, uses the normal write
replay cache and returns `{loop: enabled}`. Default standalone hosts remain
unsupported; their old catalogue and validation contracts remain unchanged.

`Application::setPlaybackLoop` refuses busy/replacement states before mutation.
For a running song it updates the existing renderer's atomic value, without
restarting WASAPI, flushing plugin edits, changing the playback epoch, replacing
the captured region, touching recording ownership or editing the document.
While stopped or independently auditioning, it changes the next song-play default.
An explicit loop setting passed to Play still overrides that default. Following
the Mac contract, transport.get.loop is current while region retains the original
play request. No native project metadata or preference schema is added.

Native command 586 is available from the palette and checked pattern context menu;
a compact on/off button sits below the existing typing selectors in the sidebar.
The native label carries state in text as well as the active appearance. Shortcut
activation preserves an existing native editor's focus/draft and uses the current
shortcut customization mechanism. No new default key overrides existing tracker,
text, formula or user bindings. Foreground DPI, tab order and accessibility still
require direct qualification as part of P3a.

## Prepared checks and bounded next gate

- `ApiTests.cpp`: capable/unsupported catalogues; absent/stale revision; strict
  boolean/unknown fields; busy atomic refusal; stopped/live semantics; unchanged
  context/region/history; and exact request replay without repeating the host call.
- `workspace-transport-control-tests`: actual Application/worker/native controls;
  retained invalid track draft and focus, stable document/transport generation,
  busy and departure-input refusal, and recording-take ownership. Registered in
  the existing workspace executable and its native-ui CI selection.
- `test_transport_loop.TransportLoopTests`: actual PID pipe, guards and native
  button plus a private, silent WASAPI case requiring an endpoint. It checks
  advancing frames, captured region, device/recording-clock generation, detached
  edit cursor, document history and the active take across loop toggles. No device
  default is changed, and absence of an endpoint is a failure, not a success/skip.
  Registered in the existing grouped Windows integration job.
- `editor/Tests/PlaybackRegionChecks.hpp`: the previous Mac region assertions
  are retained in a shared body; both wrappers run exact endpoints, tempo changes,
  cursor starts, selected and whole-song loop disable, and independent sample
  audition at 44.1/48/96 kHz with 1/17/128/4096-frame partitions. Added coverage
  enables an already-running non-looped region and disables it again. Windows
  adds the `playback-regions` target with its existing host C++ allocation/free
  probe; direct malloc/free, locks and drivers remain outside that probe. Mac
  retains GraphRealtimeAudit and its existing sanitizer exclusions.

No build or test has run for this batch. Before qualification, freeze the combined
source and use one grouped build checkpoint: Windows application, workspace target,
playback-region target and existing API standalone project; Mac app and playback-
region target because the shared test body moved. Run those targeted groups plus
the existing workspace-shortcut/native-control and recording application checks.
Run P1/P2 groups against the same candidate if integrating the dependent stack;
their earlier failed/unrun results cannot be relabeled as passing evidence.
Preserve all assertions. Broaden transport/recording/native-app validation if a
failure requires changes to shared rendering, device lifetime or main message
dispatch. No broad upstream PCM suite is justified solely by this atomic setter
adapter; a subsequent renderer change would invalidate that boundary.

The standing earliest next local build remains 2026-10-10 02:25:48.842926 UTC,
based on P1 checkpoint 02. Reuse current results only for unchanged relevant
inputs; the new shared region checks and P3 native/API paths are unqualified.
