# Windows sibling: architecture and qualification gate

Status: implemented native render path, **not sustained-presentation-qualified**.
The reference-guided workspace continuation and its measured scope are in
`WORKSPACE_CHECKPOINT.md`. Baseline:
`b6c62a57970e647e54158575dd69f893d63d454a` on the repository's verified default
`codex/screamseq` branch. Work is isolated in `codex/windows-native`.

## Boundaries and coordination

Windows owns `windows/`. `mac/`, `soundlib/`, the shared API schema and persisted
identifiers remain unchanged. Two narrowly guarded shared portability changes are
needed: Windows atomic module saving in `editor/TrackerDocument.cpp`, and exposing
the existing Windows path-conversion helpers to `OPENMPT_EDITOR_CORE` in
`common/mptPathString.{h,cpp}`. Mac regression review is still required before
integration; the other agent's checkout has not been modified.
The latest envelope report and source supersede the older kickoff's metadata 13:
**metadata 14** stores the envelope bank. The outer container version is separate.
No direct channel to the concurrent Mac agent is available here; this file is a
handoff contract, not a claim that coordination has already occurred.

The GUI must use `Tracker::Document` and its transactions/history; audio owns a
separate prepared `Tracker::Renderer`. Native hosted graph/plugin orchestration
currently under `mac/Audio/` needs deliberate portable extraction, not a second
Windows-only musical implementation. Unimplemented operations must be absent
from capabilities or explicitly unavailable, never accepted as silent no-ops.

## Render decision (initial measured probe)

Retain the proposed path for the first native host. A real ARM64 build using
MSVC 19.44.35229 / Windows SDK 10.0.26100 drew 4,000 frames in 66.766 seconds
at 2566 x 1462 physical client pixels on the identified NVIDIA GPU, concurrently
with core compilation. CPU draw median/p99/max: 2.077/4.143/9.818 ms. CPU submit
interval p99/max: 20.311/44.725 ms. Of 3,992 consecutive DXGI present-statistics
pairs, QPC interval p99/max was 16.978/33.374 ms, with four two-refresh gaps and
two missing-present pairs; the initial statistics query failed once. Occlusion
status count was zero, but this alone does not prove every pixel remained visible.
A screenshot confirmed the real grid, graph and automation workload; an NVIDIA
overlay obscured part of the header temporarily. Raw report:
`bin/windows-arm64/render-probe-loaded.json`.

These are real prototype measurements, not an application/audio qualification,
not an idle benchmark, and not evidence of superiority over untested frameworks.
The measured draw budget supports continuing with the simplest starting path;
**sustained 60fps remains unqualified** until the integrated workload passes its
visibility and presentation gates. The probe's graph/automation shapes are
explicitly synthetic rendering workload, not implemented musical editors.

### Starting design

Start with a Win32 HWND/input/common-control shell, DirectWrite text, and
Direct2D 1.1 drawing into a Direct3D 11 BGRA DXGI flip-model swap chain. Use one
virtualized tracker surface, not one UI object per cell. Cache stable text/layout
outside high-frequency drawing. Keep audio independent of presentation waits.
Use a frame-latency waitable object and pump Win32 messages while waiting;
exclude occluded/minimized frames and resize/device-recovery intervals from
steady-state statistics, but count and disclose them separately.

DirectComposition is deferred until independent retained transforms/clipping or
floating panels show a measured benefit. WinUI 3 is not selected without a
comparison demonstrating useful accessibility/docking or latency benefits. This
is a testable starting hypothesis, **not a measured fastest-framework claim**.
The final decision requires actual GPU/driver identity, physical client pixels,
DPI, refresh rate, workload, presented-frame intervals (not just CPU submit
intervals), CPU draw times and visibility prerequisites over at least 60 seconds.

Discovery on this host reported NVIDIA RTX Spark N1X, driver 32.0.16.1630,
2880 x 1800 at 60 Hz. A DPI-unaware probe saw 1440 x 900; the application must
establish per-monitor-v2 DPI awareness before sizing windows. Python reports
AMD64, which does not by itself establish native hardware architecture: target
architecture must be discovered separately before selecting a toolchain.

## Audio design requirements

Use event-driven WASAPI and query `IAudioClient3` engine-period constraints.
Prefer low-period shared mode, falling back visibly to the active engine period
or ordinary shared mode if negotiation is locked/unsupported. Explicit exclusive
mode is opt-in because it can silence other applications. ASIO is optional and
requires a supported driver plus license review, never a startup prerequisite.
Do not change endpoint defaults, master volume or system routing during tests.

Prepare renderer, channel conversion and bounded buffers before streaming.
No allocations, destruction, UI, locks, disk I/O, plugin scanning or formula
compilation in the render loop. Record callback durations and separate deadline
overruns, buffer starvation indicators and device faults; none alone proves zero
hardware glitches. Use MMCSS and a stop event. Device invalidation transitions
to stopped/error state; restart/reconfiguration belongs off the callback.
Negotiated buffer latency is not acoustic round-trip latency.

Timestamped MIDI uses the existing shared `RecordingClock`. Graph latency,
voices, native timing and automation stay shared. Plugin scan isolation, stable
VST3 class/instance/parameter identities, HWND editors and cached explicit rescan
are required before claiming Windows plugin parity. Foreign AU recipes and
opaque state must survive without automatic remapping. Runtime in-process
hosting, if used initially, must be disclosed as crash exposure.

## Persistence and API gates

A `.screamseq`/`.resonance` file is not a module with a different extension.
Preserve the exact snapshot, opaque plugin state and unknown plist properties.
Portable container tooling is only a first gate: C++ metadata/model integration,
validation and real Mac/Windows fixture round-trips are separate required gates.
Never enable a native Save command backed by lossy module export.

Use a user-restricted, local-only named pipe with bounded framing and explicit
instance discovery. Preserve the existing JSON contracts, revision/context
revision checks, dry-run validation, no-op semantics, one-transaction edits,
Undo/Redo and uncertain-write handling. Do not expose unauthenticated TCP.
The Python client extension must preserve existing Unix behavior.

## Authoritative references inspected

- https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-overview
- https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components
- https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams

Microsoft documents D2D/D3D interoperation, asynchronous composition, supported
IAudioClient3 period negotiation and exclusive-mode interference. These describe
API behavior, not measured performance on this machine. Verify declarations and
link libraries against the installed target SDK before implementation.
