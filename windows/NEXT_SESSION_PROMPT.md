# Continue ScreamSeq Windows-native development with Mac UI references

You are taking over an unfinished Windows-native port of ScreamSeq. The user
paused the prior session at a working development checkpoint specifically to
provide screenshots from the macOS ScreamSeq application and more guidance on
what the Windows UI should look like. You have computer-use enabled: inspect the
provided screenshots and the actual Windows application, and use the Mac visual
references and the user's new guidance before making further UI decisions.
The current Windows window is intentionally provisional scaffolding, NOT a design
to preserve or a finished tracker. Do not confuse rendered placeholder panels with
implemented graph/automation editing.

## Start here — preserve the existing work

Use this existing worktree, not the original Mac/default-branch checkout:

- `C:\Users\P14\code\ScreamSeq-windows`
- Branch `codex/windows-native`
- Baseline `b6c62a57970e647e54158575dd69f893d63d454a`, from the verified default
  branch `codex/screamseq` of `https://github.com/rewbs/ScreamSeq.git`.
- Work is UNCOMMITTED: four existing shared files are modified and `windows/` is
  new/untracked. Do not reset, clean, overwrite, re-create the branch/worktree, or
  commit/push without authorization. Re-check git status/branch/worktree first.
- Original `C:\Users\P14\code\ScreamSeq` checkout is untouched. Another agent
  works on macOS/shared graph features; do not overwrite its platform tree.

First read `windows/HANDOFF.md` (current detailed state and evidence),
`windows/PARITY.md`, `windows/ARCHITECTURE.md`, root `AGENTS.md`, and relevant
`.agents/skills/screamseq-*/SKILL.md`. Then read the original authoritative design,
API, format and Mac workflow documents listed in the retained kickoff below.
The handoff supersedes historical installation-blocked notes. Native metadata is
now **14**, not the kickoff's older 13; outer container versions are independent.

## Current checkpoint — what is and is not real

Runnable build: `bin/windows-arm64/Release/ScreamSeq.exe`.
Preserved copy: `bin/windows-checkpoints/native-bootstrap/ScreamSeq.exe`.
This is a native ARM64 **demo player / read-only document inspection host**,
not an editing release or full Mac parity. It starts stopped and must not be used
for a user's songs. New UI work should be tested in a separate build/process.

Implemented: Win32, D2D/DirectWrite over D3D11/DXGI flip rendering; shared
Tracker::Document/Renderer demo; real event-driven WASAPI playback; independent
navigation/playhead/Follow; real sample waveform; ScreamSeq icon; optional private
same-user named-pipe API; Python explicit-pipe client. The GUI's unavailable
editing, project-save, graph and automation features are labeled honestly.

The optional API is enabled with `--automation`, endpoint
`\\.\pipe\ScreamSeq.Api.<PID>`. `--inspection` forbids opening audio hardware.
Only api.describe, document.get, pattern.get, context.get, transport.get/play/stop
are exposed. Requests marshal to the control/UI owner via a bounded WM_APP queue;
never touch the document from the pipe/audio thread. Transport guards revisions
and deduplicates successful writes. Snapshot fields are still a subset of Mac
parity. Musical mutations are explicitly unsupported. Follow GUI and agent paths
through the same shared host/edit layer, not separate musical semantics.

Current persistence work is a Python plistlib container qualification/repacking
foundation, NOT C++ app load/save. Tests preserve unknown data, exact opaque
snapshots and AU/VST3 identities/state. Generated fixture inner sections are
non-playable placeholders for container tests; do not claim Mac interchange or
rendered-song compatibility from them. No Mac-produced project fixture was tested.

Full native project load/save, pattern editing/Undo/API parity, VST3 scanner/host/
HWND editors, MIDI, docking, command palette, virtual-grid accessibility, hosted
graphs and curve editors remain major work. Stereo-float shared WASAPI exists;
exclusive/ASIO, device picker and fault/format-change qualification remain pending.
The prior session did NOT complete the original full task.

## Verified execution, limits and build environment

Visual Studio 2022 Build Tools 17.14.41 was installed with explicit user approval:
MSVC 19.44.35229, Windows SDK 10.0.26100, native ARM64 + x64 tools and CMake.
`windows/build.ps1` discovers the tools; PATH may not contain cmake/cl.
Host native architecture is ARM64 (IsWow64Process2 0xaa64), although Python reports
AMD64. Do not assume an x64 build is native. Display: NVIDIA RTX Spark N1X,
2880x1800 physical / 1440x900 DPI-unaware, 60 Hz, 200% DPI.

Reproduce from the Windows worktree:

```powershell
.\windows\build.ps1 -Test
python -B windows/Tests/test_app.py
python -B windows/Tests/test_app_api.py
python -B windows/Tests/test_qualification.py
python -B -m unittest discover -s windows/Tests/Project -p 'test_*.py'
python -B windows/Tests/Api/test_client.py -v
```

Final run: 24/24 portable C++ tests passed, app offline and actual app pipe tests
passed, 11 container tests and four qualification-validator tests passed. Separate
native pipe CTest and four Python client tests passed. Unicode atomic module
save/open regression passed after fixing UTF-8 public file open. No Windows ASan
or full allocator/free/lock audit was run. Some existing tests define
TRACKER_SANITIZER only to select a functional path; this is NOT sanitizer evidence.

Real loaded engine/UI test: 65.026 seconds at 48000 Hz / 480-frame period, 6503
callbacks, max callback 337.6 us, max service 417.9 us, zero reported
underrun indicators/overruns/device faults. This run predates final API integration;
see raw report for exact scope. Native silent lifecycle diagnostic also passed.
No physical DAC/loopback/round-trip latency claim. The driver-selected minimum was
480 frames; do not market that as proven ultra-low acoustic latency.

Offline audio at 44.1/48/96 kHz with 17/128/4096-frame callbacks is finite/nonzero;
max PCM delta is 8.64267e-7, within the existing shared 1e-6 rounding bound, NOT
bit-exact callback partition equivalence. Shared tests also check timing.

Real GPU probe: 4000 frames / 66.766 seconds while compiling; CPU draw p99 4.143 ms.
DXGI recorded four two-refresh gaps and missing statistics. **Sustained 60fps is
NOT qualified.** Do not substitute CPU submit FPS or screenshots for display
presentation evidence. The live native window was captured and inspected; current
layout is readable but awaits the user's Mac screenshots and design direction.

Evidence is under `bin/windows-arm64/` and the preserved checkpoint; detailed
commands/limits are in HANDOFF.md, Audio/README.md and Api/README.md.

Shared modifications to review/reconcile with the Mac agent:
- `editor/TrackerDocument.cpp`: Windows atomic module save and UTF-8 module open.
- `common/mptPathString.{h,cpp}`: expose existing Windows path/filesystem helpers
  to OPENMPT_EDITOR_CORE, without duplicating algorithms.
- `src/mpt/format/default_formatter.hpp`: explicit `::mpt` namespace qualification
  for MSVC template lookup.
Mac sources/tests were not rewritten. Windows fixtures need an 8 MiB test stack
(the default caused confirmed STATUS_STACK_OVERFLOW) and a test-only preamble
that removes the legacy Windows RPC `small` macro. Production live renderer is
heap-owned. Further Unicode sample/instrument-import auditing is pending.

Terminal tools here use Git Bash, not PowerShell. Use native `C:/...` paths for
native executables; MSYS `/c/...` arguments are not translated. For PowerShell
variables, write a .ps1 and invoke -File rather than letting Bash expand `$...`.
Before probes verify TMPDIR/TEMP/TMP all point to
`C:/Users/P14/AppData/Local/hermes/cache/scratch`; vcvars reset them once.
Do not change system routing, terminate a musician's app, or reuse their song.
No QA application was left running at the pause.

## How to resume with the new UI information

1. Read the handoff and verify repository/process/build state without discarding work.
2. Inspect every supplied Mac screenshot using vision/computer-use. Ask only for
   genuinely missing interaction intent; do not guess unreadable details. Inspect
   the user's actual display/DPI before sizing a dense native workspace.
3. Compare the requested Mac layout/workflow with the provisional Windows host.
   Preserve the shared musical/API/audio contracts; replace provisional layout
   choices as needed. Retained panels, independent cursor/playhead/focus,
   Follow/Pin/Return and target-bound drafts remain central requirements.
4. Implement incremental, real behavior with tests. Read definitions/usages first.
   Do not duplicate musical algorithms, advertise unsupported capabilities, accept
   writes as no-ops, or save lossy modules as native projects.
5. Build, launch a separate QA process, use computer-use to inspect and interact
   with the REAL native UI, and verify external changes by reading them back via
   the API. Preserve honest measured limits and keep the parity matrix current.

## Original kickoff — retained requirements and framework references

The original kickoff follows in full. Its separate-worktree setup is already
fulfilled; continue the worktree above. Its broader acceptance requirements
remain open unless explicitly verified above. Metadata 14 supersedes its 13.

---

# Windows agent kickoff prompt

Build a Windows-native version of **ScreamSeq**, alongside its macOS version, in the `rewbs/ScreamSeq` OpenMPT fork. Start from the ScreamSeq application commit/default branch, not unmodified upstream `master`. Use a separate checkout or worktree and a `codex/windows-native` branch. Another agent is working on the macOS app and graph implementation; coordinate shared changes and preserve its work.

First read `AGENTS.md`, the relevant `.agents/skills/screamseq-*/SKILL.md` files, `doc/SCREAMSEQ_ARCHITECTURE.md`, `doc/RESONANCE_UI_PHILOSOPHY.md`, current `mac/README.md`, `mac/GRAPH_WORKFLOW.md`, `mac/PRECISE_NOTES.md`, the API guide/schema and the latest completion report. Historical Resonance names refer to ScreamSeq. Dated feasibility reports are historical baselines, not a current specification.

The goal is a first-class native Windows tracker with a smooth sustained 60fps UI and an excellent low-latency audio engine from its first usable build. Share musical behavior, DSP, project compatibility and agent API semantics with macOS. Use `windows/` for the Windows application. Keep `editor/` and carefully guarded `soundlib/` extensions portable. Upstream `mptrack/` is the original OpenMPT/MFC product, not the new ScreamSeq UI. Do not reimplement already-working musical algorithms separately in a Windows-only model.

Use modern C++ and native Windows APIs as the starting architecture: a Win32 window/input/accessibility shell, hardware-accelerated Direct2D/DirectWrite over Direct3D/DXGI for dense pattern, waveform and graph views, with DirectComposition where it materially helps. Native common controls/dialogs can handle ordinary controls. Evaluate WinUI 3 or other native layers only where measured latency, accessibility, docking or development benefits justify them. Do not assume a framework is fastest because it is newer. Avoid a browser/Electron frontend or a managed layout object per tracker cell. Present a short measured architecture decision before committing to the render path.

Use event-driven WASAPI with negotiated low-latency shared-mode periods via IAudioClient3, plus exclusive mode when supported. Offer ASIO if a suitable supported driver/backend is available and its SDK/licensing can be handled correctly; do not make ASIO mandatory to launch. Reuse the OpenMPT audio core, the shared mixer/graph DSP and existing audio-device abstractions where practical. Keep callbacks allocation-, lock-, UI- and disk-free. Handle device loss, format/rate/buffer changes, timestamped MIDI, latency compensation, silence, tails and bounded shutdown from the beginning. Use audio-thread scheduling appropriate to Windows and measure actual underruns/callback times.

Host native Windows VST3 effects and instruments, including custom HWND editors, state, sample-offset parameter automation and multi-bus/sidechain routing. AU is macOS-only. Preserve unavailable AU and other foreign-plugin recipes/state in projects and clearly show their unavailable status. Never silently retarget plugins by rack index, filename or display name. Preserve stable class/instance/parameter identities. Plugin scanning must be cached and isolated from the main app, with explicit rescan. Runtime isolation can be staged, but document any remaining in-process crash exposure.

Native `.screamseq` projects and legacy `.resonance` projects must round-trip shared musical data: exact sample payloads, multisample instruments, precise note occurrences, per-hit effects, automation including scripted curves, graph definitions/routing/modulation, sample-instrument graph assignments and pattern graph commands. The existing sample voice/storage path is 8/16-bit; higher-resolution assets are future shared work, not an existing capability to assume. The current macOS wrapper uses a versioned binary plist around an exact song snapshot and metadata. Extract or implement a tested portable compatible codec; do not drop unknown data or substitute a lossy module export. Metadata 13 adds graph-owned curves and sample-instrument graphs. Coordinate serialization/model changes with the Mac agent. Keep legacy persisted identifiers stable as documented in `assets/branding/BRANDING.md`.

Every musical operation must be externally agent-driven. Implement the same validated method/data contracts and revision guards on Windows, using a private local transport such as a user-restricted named pipe, or a supported Unix socket where appropriate. Extend the Python client portably. No unauthenticated network server. Preserve dry runs, atomic validation, Undo/Redo, stale-write rejection and uncertain-write handling. The GUI must call the same editing layer rather than acquiring independent semantics.

Follow the connected workspace philosophy: pattern, routing/modulation graph and automation visible together; dockable/resizable/floating panels; independent Follow/Pin/Return; full keyboard operation and command palette; drafts retain their target and never steal focus. Keep playhead, edit cursor, selection and keyboard focus distinct. Note audition works from sample/instrument inspectors. Preserve global transport bindings, local overrides, selection/pattern/song playback and looping. Use `assets/branding/ScreamSeq.png` for the application icon.

Work incrementally but autonomously: first a runnable native window and real engine playback with instrumented 60fps drawing; then project load/save, pattern editing, inspector/audio/MIDI integration; then parity for samples, instruments, plugins, automation and shared graph workflows. Keep an explicit parity matrix and expose incomplete features clearly. Run the Windows app and inspect its real UI after each meaningful stage. Add actual rendered-audio, callback-partition, latency and loopback tests; compare shared fixtures with macOS/stock OpenMPT when applicable. Use ASan or appropriate Windows diagnostics for lifetime/index changes.

Before claiming a stage complete, provide a usable build, commands to reproduce tests, evidence for sustained frame pacing and audio stability, exact scope/limits, and a report of what remains. Avoid modifying the user's running song or system-wide audio routing during tests. Keep development builds separate from usable checkpoints. Persist through routine failures and build issues; ask only when a decision genuinely requires the user or further progress is blocked.

Authoritative framework references to verify against the target SDK:

- [Direct2D overview](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-overview) and [DirectComposition architecture](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components).
- [Windows low-latency audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio) and [WASAPI shared/exclusive stream considerations](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams).

These are starting choices, not an unmeasured claim of universal fastest performance. Benchmark the target Windows hardware and retain the simplest native architecture that meets the musical requirements.
