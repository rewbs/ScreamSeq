# ScreamSeq for Windows

The Windows frontend shares ScreamSeq's C++ song model, playback engine, native
project format and editing semantics. Its native interface uses Win32 controls
and Direct2D/DirectWrite over a Direct3D 11/DXGI surface. Audio uses WASAPI, with
built-in processors and VST3 hosting.

This is an implemented editing application with continuing macOS parity work.
It is not a release-qualified Windows build. See
[application integration](App/INTEGRATION.md) for current behavior and
[the continuation record](RESUME_PROGRESS.md) for dated qualification evidence.

## Build and run

Install Visual Studio 2022 C++ Build Tools, a Windows 10/11 SDK and CMake 3.24 or
newer. The Visual Studio C++ CMake component supplies CMake and CTest. Install the
ARM64 compiler tools if targeting ARM64.

From the repository root in PowerShell:

```powershell
./windows/build.ps1 -Architecture x64 -BuildDirectory bin/windows-dev -Test
./bin/windows-dev/Release/ScreamSeq.exe
```

Use `-Architecture ARM64` for ARM64. Omitting `-Architecture` detects the native
machine architecture. `-Configuration Debug` selects a debug build; the default
is Release. Use `-Fresh` after a failed initial compiler configuration. Build
into a separate directory when another application instance is running.

`-Test` builds and runs portable and document-worker tests. It does not qualify
desktop presentation, hardware audio or third-party plugin behavior. Actual-app
tests run on an owned private desktop; follow [the test guide](Tests/README.md).

## Editing and interoperability

The frontend includes pattern entry and musical typing, precise notes and unified
FX columns, sample waveform and loop editing, instrument mappings and envelopes,
sample browsing and preview, plugin library and preset controls, mixer/graph
editing, pattern and song automation, envelope reuse and the formula workbench.
Document and plugin edits share chronological Undo/Redo. Detailed behavior and
individual feature reports are linked from [application integration](App/INTEGRATION.md).

Native projects use `.screamseq`, container 6 / metadata 17. Historical native
versions are rejected; module import remains supported. Projects preserve plugin
identity and opaque state across platforms. AU processors cannot run on Windows,
and missing plugins reject playback preparation instead of silently changing the
sound. See [project persistence](Project/README.md) and [plugin hosting](Plugins/README.md).

The local JSON API uses a private PID-scoped named pipe. Read the running host's
`api.describe` response for supported operations: Windows does not yet implement
every macOS API or inspector. See [the Windows API guide](Api/README.md).

## Remaining boundaries

Sustained display performance and final combined audio/UI qualification remain
open. Windows structural edits currently use stopped preparation; the newer
macOS live routing publication path is not Windows-qualified. Hardware MIDI and
recording parity, general note/MIDI graph routing and broader installed-plugin
qualification also remain unfinished. Portable tests on another platform do not
qualify this native application.

The [shared architecture](../doc/SCREAMSEQ_ARCHITECTURE.md) and
[October graph checkpoint](../doc/mac-native-qualification/2026-10-01-graph-resume/STOPPAGE_REPORT.md)
describe the shared model and the latest graph-work boundary. Historical reports
retain their original branches and build paths; current development integrates
on `main`.
