# Hosted audio: Windows offline qualification

The reusable target is `TrackerHosted` (`editor/hosted/CMakeLists.txt`), PUBLIC
linked to TrackerEditor, C++20. Main Windows CMake/app integration is deliberately
not changed here. See `editor/hosted/README.md` for the provider and lifecycle
contract, including **renderer reset before chain destruction**.

## Actual result

MSVC 19.44 ARM64 Release clean build completed without compiler warnings/errors.
**7/7 CTests passed**, without GUI, audio devices, third-party plugins or installs.
The standalone build imported the existing `bin/windows-editor/Release`
TrackerEditor/OpenMPTCore/TrackerFLAC archives; it did not rebuild those libraries.

| Regression | Exercised |
| --- | --- |
| hosted-smoke | Real Document::demo renderer through built-in rack; finite, nonzero and changed PCM at 44100/48000/96000 Hz; 17/128/512/4096-frame partitions |
| hosted-BuiltinGraph | Empty rack still prepares mixer + built-in graph; native dB state/parameter metadata retained; explicit AU/VST3 unavailability; same three rates and 17/128/4096 partitions |
| hosted-SignalRouting | Existing Mac assertions for cross-graph sidechain/return summation, independent instrument/raw-channel copies, NNA ownership, group commands and atomic cycle rejection |
| hosted-MusicalAutomation | Existing portable Mac assertions for exact knots, reversed steps, scripted double endpoint ramps, repeat/seek, identity through reorder, fractional tempo/groove and instrument automation |
| hosted-PreciseNotes | Existing portable Mac assertions for every-sample instrument timing, precise sample onset, independent voices, same-row releases, repeat/groove, recording clock and callback partitions |
| hosted-NativeSignalGraph | Existing Mac assertions for row/persistent/ordinary ordering, independent targets, latency/bypass/tail continuity, inactive transport, reordered delayed graphs and graph envelopes |
| hosted-BackendContract | Same provider used by rack and graph; 4096-frame endpoint ramp stays **one backend process call**; double precision, bus-offset/output assembly, alias identity and transport before/after processing |

Measured built-in rack maximum PCM differences against 128-frame partitions:

| Rate | Worst absolute difference | Wet PCM L1 | Wet–dry PCM L1 |
| --- | ---: | ---: | ---: |
| 44100 | 6.70552e-8 | 1341.16 | 3998.09 |
| 48000 | 1.04308e-7 | 1456.15 | 4340.89 |
| 96000 | 0 | 2919.96 | 8704.6 |

The demo rack bound is 1e-6, not a bit-exact claim. Empty-rack built-in graph PCM
was equal across tested partitions; its -12 dB independent gain-reference error
was at most 7.43264e-9. Backend endpoint-reference errors were zero at all three
rates; that result is for the deterministic fixture, not vendor plugins.

## Test provenance and limits

`prepare_tests.py` extracts portable portions from four **existing** Mac test
sources into the private build directory. It keeps their timing/routing/PCM
assertions, replaces only platform fixture binding, removes Objective-C session/
API/export portions and actual Darwin audit calls, and corrects local lifetime
ordering so renderer destruction precedes its borrowed chain. Source and generated
hashes are recorded in `generated/provenance.json`. No fabricated audit counts
are substituted. Regeneration is tied to the source/script CMake dependencies.

`FixtureBackend.cpp` adapts the deterministic gain/note/linear-queue/delay DSP from
`mac/Tests/FixtureVST3.mm` to the portable backend interface. It does actual sample
processing, but **does not load a VST3 or AU**. Its recipe uses the VST3 tag solely
because existing graph validation accepts the established AU/VST3/Built-in tags;
only the explicit `gain`/`synth` test identities resolve in that test executable.
Production default hosting rejects both foreign formats. No production musical
algorithms are copied into Windows code.

This is **functional offline evidence only**. It does not qualify allocation/free/
lock freedom, sanitizers, hardware deadlines, acoustic output, vendor SDK loading,
plugin UI, project cross-platform state restoration, or Mac compilation. The Mac
private-backend/CMake adaptations require native Mac review and fixture execution.

## Reproduce from the Windows checkout (Git Bash)

Prerequisite: existing ARM64 Release editor/core/FLAC archives. Override
`SCREAMSEQ_EDITOR_BUILD` if they live in another build directory. Native tools
require `C:/...` paths on this host; use the VS bundled CMake path if not on PATH.

```bash
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
"$CMAKE" -S windows/Tests/Hosted -B bin/windows-hosted \
  -G 'Visual Studio 17 2022' -A ARM64
"$CMAKE" --build bin/windows-hosted --config Release --clean-first \
  > bin/windows-hosted/final-build.log 2>&1
"${CMAKE%cmake.exe}ctest.exe" --test-dir bin/windows-hosted -C Release -V \
  --output-junit hosted-tests.xml > bin/windows-hosted/final-tests.log 2>&1
python windows/Tests/Hosted/record_evidence.py bin/windows-hosted bin/windows-editor
```

Private artifacts: `bin/windows-hosted/hosted/Release/TrackerHosted.lib`,
`Release/hosted-*.exe`, final build/test logs, JUnit, `HostedBuildInfo.json`,
`hosted-source-evidence.zip` and generated test provenance. The manifest hashes
source, imported archives and executed binaries. Keep these out of Git. The
initial missing-facade red build is retained in `red-build.log`; a later targeted
red test caught stale backend transport before MIDI and now passes.

No commits, stash/reset, original Mac-checkout edits, device changes or user song
changes were made by this hosted-audio task.
