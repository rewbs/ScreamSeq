# Windows build verification checkpoint

**Superseding parent result:** the native ARM64 installation completed, the app
and portable tests compile, and final CTest passed 24/24. See
`../HANDOFF.md` (from windows/cmake use `../HANDOFF.md`) and
`bin/windows-arm64/final-build.log`. The installation-blocked account below is
historical, retained to preserve the actual initial failure.

## Implemented

- CMake builds the shared `OpenMPTCore`, bundled `TrackerFLAC` (Windows UTF-8 file I/O), and `TrackerEditor` with C++20; no upstream `mptrack` frontend or macOS host is linked.
- The editor source list matches all shared editor sources referenced by `mac/CMakeLists.txt`, including signal graphs and envelope banks.
- `portable-tests` builds 24 existing C++ regression executables. CTest registers `tracker-core` with the repository fixture root and labels all tests `portable;functional`.
- Tests that normally link the Darwin realtime interposer use their existing `TRACKER_SANITIZER` functional-only branch. **This build does not enable sanitizers and does not qualify allocation/lock freedom.** Device, Objective-C++, and Darwin-only audit tests are excluded.
- Optional source-presence-gated targets: `ScreamSeqAudio`, `ScreamSeq`, and standalone `renderer-probe`. The renderer probe does not link the core/editor/audio libraries.
- `build.ps1` discovers Visual Studio 2022 and its bundled CMake, detects native ARM64/x64 using `IsWow64Process2`, configures a separate architecture-specific `bin/` build, propagates native command failures, and optionally builds/runs the full portable test set.

## Actual execution

Command (from repository root):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 -Target portable-tests -Test
```

Result: exit code **1**, before CMake configuration:

```text
No completed Visual Studio 2022 installation with Microsoft.VisualStudio.Component.VC.Tools.ARM64.
If installation is running, retry after it finishes.
```

The script parsed and executed its native architecture detection, selecting ARM64 rather than the misleading process architecture. `vswhere -all -products '*' -format json` returned `[]`; no CMake executable was on PATH or in either standard VS 2022 installation root when checked. The already-started Build Tools installation was not duplicated, interrupted, or polled in a waiting loop.

A Python consistency check exited 0: no missing or extra shared editor sources relative to the macOS CMake file; 24 unique portable test targets; all referenced test source files exist. `git diff --check` exited 0 (new files remain untracked).

**No C++ compilation or CTest execution completed at this checkpoint.** Toolchain availability, Windows source portability, linking, and regression results remain unverified. This is a build configuration handoff, not a successful-build claim.

## Resume after installation

```powershell
.\windows\build.ps1 -Architecture ARM64 -Target portable-tests -Test
.\windows\build.ps1 -Architecture ARM64
.\windows\build.ps1 -Architecture ARM64 -Target renderer-probe
```

Default output is `bin/windows-arm64/Release`. App/probe targets appear on reconfiguration once their source files exist. Shared `TrackerDocument.cpp` Windows persistence changes and frontend/audio implementation are owned by the parent/other workstream, not this build configuration task.
