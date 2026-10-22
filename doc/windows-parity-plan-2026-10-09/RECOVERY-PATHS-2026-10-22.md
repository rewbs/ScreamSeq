# Windows recovery publication in deep directories

## Observed evidence, 22 October 12:45 UTC

The retained ab5f677c1 Windows x64 candidate (CI merge 1b8df926b, identical
source tree) passed 11 of 12 selected device-dependent actual-app checks on
this ARM64 Windows machine under x64 emulation. The live MIDI recovery case
failed with `Cannot publish recovery file: The system cannot find the path
specified`. Its temporary root was nested under
`bin/mac-gesture-20261022/admission-followup/ci/windows-x64/device-scratch`.
The failed receipt/log are retained, not replaced by a rerun.

The same test, executable and ab5 test sources passed with only the temporary
root shortened to `bin/r-ab5`. The generated recovery filename adds roughly
100 characters. Source inspection found ordinary paths passed directly to
CreateFileW, CreateDirectoryW and MoveFileExW, while this application's manifest
does not opt into long paths. The failure occurs when publishing the longer
final name after writing shorter staging names. This is a recovery filesystem
failure, independent of audio endpoint availability or take serialization.

Evidence: `admission-followup/ci/windows-x64/device-checks.json`, its log and
`verified-identity.json`; `admission-followup/short-recovery.json` and log.
Executable SHA256:
`707319eb3142557fe6b4e835da0c22bc8c21f1fffb2965842ab89b6d63733628`.
These runs used owned private desktops and silent output, with no simultaneous
foreground Computer Use. They establish neither native x64 hardware behavior
nor sustained presentation/realtime performance.

## Source correction — unbuilt

`windows/Project/RecoveryStore.cpp` converts already validated absolute paths
to extended-length native paths at the filesystem boundary. Directory leases,
file opening, staging, publication and enumeration all use that conversion.
Enumeration reconstructs ordinary entry paths before opening them, avoiding a
second prefix. Public identifiers, metadata and fault-hook paths remain unchanged.
Directory validation first normalizes slash separators, then rejects device
paths, traversal, reserved names and aliases as before. Ancestor handles retain
their no-delete-sharing leases and reparse checks.

This follows Microsoft's [extended-length path rules](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation).
It needs no registry, system policy or manifest change. Native paths are
internal; callers still cannot supply a device namespace to bypass validation.
No project format, recovery identifier, retention limit or atomic commit point
changes. General project-file long-path support and live UNC/network storage
qualification are outside this correction's evidence.

## Required bounded checkpoint

- Build ScreamSeq and recovery-store-tests in the same hourly checkpoint as
  the pending scratch focus correction. Mac's plugin-trust expectation repair
  joins the paired CI checkpoint; it does not modify the Mac save contract.
- Run recovery-store-tests, retaining its existing fault, collision, reparse,
  retention, session-isolation and pinned-ancestor checks. New cases cross both
  the final-file and directory MAX_PATH thresholds, and check metadata, exact
  bytes, rollback cleanup, retention, clear and deep junction refusal.
- Run the existing live autosave/restore case and the new explicit deep-path
  case. Both require an immutable live take/revision, a fresh stopped restored
  take, synthesized held-note release and native save after commit.
- Run the existing recovery actual-app class because file operations changed
  for ordinary paths too. Do not shorten its paths to hide failures.
- After native/app passes, use fresh isolated Computer Use for scratch typing,
  point drag/autosave, Undo and retained keyboard focus. No simultaneous
  private-desktop input. Package only a passing candidate with exact provenance.

The failed ab5 candidate remains unqualified. Reuse its unchanged shared/API
and Mac runtime results; the new source and regressions require rebuilt proof.
