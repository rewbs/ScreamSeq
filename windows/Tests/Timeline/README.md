# Timeline operation tests

`TimelineOperations` is a control/document-worker binding, not a second musical
engine. The caller validates/removes `expectedRevision`. It returns the Mac API's
`result.data` payload; the session adapter supplies snapshots and change status.

Implemented methods:
- `pattern.notes.get` / `pattern.notes.set`: precise occurrences and per-hit
  effects, explicit legacy-cell clearing, beat offsets using each pattern's
  signature, complete candidate validation, dry run, no-op retention and shared
  document Undo/Redo.
- `document.timing.get` / `document.timing.set`: shared `SongTiming`, fractional
  tempo, explicit groove/mode transitions, normalized groove, preview and history.
- `automation.formula.reference` / `automation.formula.preview`: the existing
  bounded shared formula compiler/evaluator and symbol reference. No general
  purpose script interpreter is introduced.

The actual model tests exercise invalid batches, releases, duplicates, offset
ambiguity and row bounds before any transport callback; pattern beat overrides;
exact timing history; and invalid formula rejection. They use exception-backed
checks that remain enabled in Release. Newly added families were observed failing
with `Unknown timeline operation` before their implementation, then passing.

Build from the Windows worktree using the installed CMake executable:

```text
cmake -S windows/Tests/Timeline -B bin/windows-timeline -G "Visual Studio 17 2022" -A ARM64
cmake --build bin/windows-timeline --config Release
ctest --test-dir bin/windows-timeline -C Release --output-on-failure
```

The standalone target links existing `bin/windows-editor/Release` shared-core
libraries, or an explicit `SCREAMSEQ_ENGINE_LIB_DIR`. Rebuild those libraries after
shared-core changes; a stale imported library is not evidence for a new source
revision. MSVC ARM64 Release execution passed without compiler warnings at this
step. Application integration, native graph/curve UI and hosted-parameter
orchestration are separate work. These tests do not qualify plugin audio, real
MIDI, Mac app reopen, realtime allocation safety or presentation performance.

## Noncanonical precise-note no-op regression

`PreciseNoteTests.cpp` exercises the real `Document`, `restoreNative`, native
validation, API binding and Undo/Redo. Its data is **generated**, including the
exhaustive four-event permutation corpus; it does not open, alter or qualify the
external Mac reference project.

The pre-fix ARM64 executable accepted native channel-0 events at positions
`[65536, 0]`, notes `[61, 62]`, instrument 1, velocity 127. Unchanged get/set
then printed `wouldChange=true stops=1 revisionUnchanged=0 canUndo=1 canRedo=0`.
The fixed executable prints `wouldChange=false stops=0 revisionUnchanged=1
canUndo=0 canRedo=1`. The pending Redo is also executed and checked exactly.

Both Windows and Mac bindings now call the one shared inline control-side helper:

```cpp
bool Tracker::replacePreciseNotesForPattern(
    std::vector<PreciseNote> &candidateNotes, uint64_t pattern,
    std::vector<PreciseNote> replacement);
```

It compares target-event multisets using every field. Equal content leaves the
entire original collection untouched. Real replacement sorts only the incoming
target events (release before onset at equal position/track), stably removes the
old target and appends its replacement. Unrelated events retain their relative
order and every stored field. Reads never normalize metadata. The helper can
allocate/sort and is called only while constructing API edit candidates; neither
the callback nor scheduler calls it. `NativeSong::validate` remains authoritative.

Additional checks cover request permutations; changed payload fields; unrelated
interleaved pattern events; empty replacement; pending Redo under dry runs and
invalid requests; valid off/cut and duplicate release/onset rejection; explicit
`clearRowEffects`, `clearLegacy` and duplicate `clearRows`; MOD/XM effect rejection
versus MPTM acceptance; and selected-sequence isolation for notes and timing.
Existing fractional tempo, groove, formula reference/preview and offset tests
remain in the same executable. Test Documents are heap-owned; stack reserve is
8 MiB.

Qualification command (Git Bash, from this worktree; adjust only core library
path when a newer matching shared build is supplied):

```bash
export TMPDIR='C:/Users/P14/AppData/Local/hermes/cache/scratch'
export TEMP="$TMPDIR" TMP="$TMPDIR"
CMAKE='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
"$CMAKE" -S windows/Tests/Timeline -B bin/windows-timeline-review-fix -G 'Visual Studio 17 2022' -A ARM64 -DSCREAMSEQ_ENGINE_LIB_DIR=C:/Users/P14/code/ScreamSeq-windows/bin/windows-snapshot-fix/Release
"$CMAKE" --build bin/windows-timeline-review-fix --config Release --parallel 2
bin/windows-timeline-review-fix/Release/timeline-tests.exe
"${CMAKE%cmake.exe}ctest.exe" --test-dir bin/windows-timeline-review-fix -C Release --output-on-failure
```

Executed with MSVC 19.44.35229.0, native ARM64. Evidence is under
`bin/windows-timeline-review-fix/`: `red-noop.log`, `green-noop.log`,
`green-all.log`, `build.log`, `ctest.log` and `qualification.json`.
**Mac Objective-C++ compilation and native API/history qualification remain
unverified.** Only the matching Mac binding in this Windows worktree changed;
the original Mac checkout, external reference, storage identifiers and upstream
attribution were untouched. No GUI, audio device, clipboard or global state was
used. No commit was made.
