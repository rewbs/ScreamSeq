# Upstream integration and installed Windows plugins — 2026-09-21

This is a development checkpoint, not full Mac parity. The work plan is
[PARITY_PLAN.md](PARITY_PLAN.md). Evidence files below are local ignored artifacts
under `bin/`, not files distributed with the product.

Later automation qualification adds a limitation to the Contourtonist offline
results below: its unmeasured default fixture processes with a flat filter.
Finite, partition-consistent PCM and exact state retention remain valid checks,
but they do not establish audible modulation of that vendor's parameters.
See `PARAMETER_AUTOMATION_PROGRESS.md` for the source evidence and the separate
built-in/external-fixture audible automation comparisons. OrbitCab's reported
partition failure remains unresolved.

## Source integration

`origin/main` does not exist. `origin/HEAD` names `codex/screamseq`; the fetched
default branch added `c486a0642` and `bcfe0f8a7` beyond `b6c62a579` (108 files).
Review covered the shared musical/model changes, Mac API/format and UI changes,
plugin lifecycle/latency work, tests and accompanying reports. Prior Windows
work was checkpointed as `40d0f074c` before merging.

The two merge conflicts were the Mac AudioUnitHost files: Windows had extracted
their portable orchestration into `editor/hosted/`, while upstream added latency
maintenance in the original location. The merged shared backend now carries
that behavior and the Mac backend forwards it. No Mac compilation is available
on this Windows host, so source integration is not Mac build qualification.

Windows now uses container 6 / metadata 17, including unified FX bytes, precise
NC commands and disconnected destinations. Historical native wrappers reject
before publication, matching upstream. Renderer construction receives native FX
before cursor seeking. WASAPI joins before worker/STA latency reactivation and
compensation changes; Stop during the responsive wait prevents restart.
Waveform playback cursors use actual shared voice positions and the transport
API exposes the same voice fields as Mac.
The actual running waveform editor was visually checked; its unmodified capture
is `bin/windows-plugin-qualification/waveform-live.png`.

Real Contourtonist initialization exposed a JUCE notification not covered by the
fixtures: `kParamIDMappingChanged` together with `kParamValuesChanged` during
initial component-state synchronization. The host now accepts mapping
notification only before the initial parameter catalog exists. Runtime topology
and mapping changes still reject; no substitute class or identity migration is
introduced. Both combined- and separate-controller fixtures cover this path.

## Installed plugins and provenance

Both ARM64 bundles were downloaded from their publishers' GitHub releases and
copied into the new per-user `Programs/Common/VST3/ScreamSeq-QA` directory under
Local AppData. Existing installations were not overwritten. Scan caches are
private qualification files, not the user's application registry.

| Plugin | Official release | Archive SHA-256 |
|---|---|---|
| Contourtonist 0.2.2 | [stoatworks-labs release](https://github.com/stoatworks-labs/contourtonist/releases/tag/v0.2.2), `contourtonist-windows-aarch64.zip` | `CA6198AB16102759DE163172A9617ECEF46F9B6414F3321CBBBA557529E5E5DF` |
| OrbitCab 2.5.0 | [darwinscat release](https://github.com/darwinscat/orbitcab/releases/tag/v2.5.0), `OrbitCab-2.5.0-Windows-arm64.zip` | `B963E7AC706CFEB5DCF9165329C6A909D66756F50E4FF84E81D21E72FC0BF8F9` |

OrbitCab's archive hash also matches the publisher's SHA256SUMS. Installed
`Contents/arm64-win` binary hashes:

- Contourtonist: `6D522278B6880975ADE428CCB3E09FED4B9E5BF38012DECCE9C034DB4599D684`
- OrbitCab: `FC41B7AF51F5F304D7CB7E85504948CD8ED8EA31E11AF97F5463981EDFA79E93`

The external harness exercises discovery, creation, parameter edits, processing
at 44100/48000/96000 Hz, opaque state capture, concurrent second-instance restore,
parameter equality, processing after restore, removal and module lifetime.
Each rate renders 57036 frames over 17/128/512/4096-frame blocks. These are
effects; this does not qualify a real VST instrument or MIDI workflow. Parameter
edits use exact range endpoints: Contourtonist advertises a continuous rate but
quantizes its native value. The restore tolerance remains 1e-5.

Both custom editors were inspected and a real control was moved. Each editor
opened/closed three times and a fourth instance was destroyed while open.
The saved lifecycle state tests are programmatic; manual editor gestures have
not yet been qualified as application history transactions. Actual captures:
`bin/windows-plugin-qualification/contour-editor.png` and `orbit-editor.png`.

## Results and a retained failure

| Check | Result |
|---|---|
| Shared portable core plus project preservation | 27/27 CTests passed, including new unified-FX and playback-display tests |
| Windows VST3 fixture suite | 18/18 CTests passed, including dynamic latency increases/decreases, mixer/graph compensation, retained position and editor shutdown |
| Metadata 17 | 2129 checks passed, including NC, FX 8, disconnected destinations, malformed data and historical rejection |
| Current application regression suite | 51 tests passed; see fixture provenance below |
| Native project preservation executable | PCM/plugin/opaque metadata retention, musical edit/save/reopen/Undo, rejected versions and failed-save state passed |
| Hosted project bridge | PCM, automation, large callbacks, fault silence, detached lifetime and repeated preparation failure passed; scoped C++ new/delete probe detected its positive controls and counted zero during prepared renders (direct malloc/free and locks are outside coverage) |
| Contourtonist real-plugin lifecycle and editor | Passed |
| OrbitCab real-plugin lifecycle and editor | Passed |
| Contourtonist application save/reopen and offline rendering | Passed; maximum callback-partition PCM delta `5.14090061188e-7` |
| OrbitCab application save/reopen | Exact plugin identity and opaque state preserved |
| OrbitCab offline callback-partition consistency | **Failed**, maximum delta `0.115216255188`; finite PCM and unchanged document do not override this failure |

OrbitCab's failure reproduces through the direct provider, without tracker,
mixer or document worker. Same-size 512-frame renders match exactly at all
three rates; changing sizes gives deltas up to `0.02558434009552002` on a sine
input. This narrows the investigation to the provider/vendor boundary; it is
not proof that the vendor alone is at fault. No hidden warmup or relaxed
tolerance was added. `orbit-partition-probe.json` and `orbit-app-pinned/result.json`
retain failed status. The official processor source was inspected for startup,
IR loading and block behavior, but the cause is not yet isolated.

Eight-second silent WASAPI runs, separate instances, 48000 Hz / 480-frame period:

| Plugin | Callbacks | Maximum callback | Overruns / starvation / device / MMCSS errors |
|---|---:|---:|---|
| Contourtonist | 808 | 607.8 microseconds | All zero |
| OrbitCab | 809 | 1331.5 microseconds | All zero |

DSP ran normally and output was silenced afterward. Both runs captured 161 live
voice markers through the API. There were no processor faults. System routes
and volume were unchanged. These short measurements are not acoustic loopback,
long-session stability, allocation/lock audit or sustained-presentation proof.
The final application SHA-256 is
`B582E4268375146BBE85548748B055A0D0F2D7A08D099D4958DBD721E9E1772C`;
both `contour-app-pinned/result.json` and `orbit-app-pinned/result.json` record
that executable hash. Earlier intermediate reports remain historical evidence.

## Fixture provenance and reproducibility

The supplied Mac reference is container 4 / metadata 14 with an RSONGS1 snapshot.
Upstream now rejects it. Its original bytes and SHA-256 remain unchanged:
`96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7`.

Continued tests used an explicitly **synthetic** disposable current-format copy:
`bin/windows-plugin-qualification/reference-format17.screamseq`. Its empty
performance layer was verified before upgrading the wrapper and metadata;
module/sample bytes were preserved and reframed as RSONGS2 with empty timing
data. The tests' historical names mention a Mac reference, but this run does
**not** prove a newly exported Mac format-17 file or reciprocal Mac reopening.
Installed-plugin application fixtures are new demo songs generated by the
current document worker, not edits to the user's reference.

Build commands:

```powershell
./windows/build.ps1 -Architecture ARM64 -BuildDirectory bin/windows-resume -Target ScreamSeq,document-controller-tests,portable-tests,native-project-tests,project-preservation-tests -Jobs 3
ctest --test-dir bin/windows-resume -C Release --output-on-failure
ctest --test-dir bin/windows-resume-vst3 -C Release --output-on-failure
ctest --test-dir bin/windows-upstream-metadata -C Release --output-on-failure
```

`windows/Tests/Plugins/ExternalLifecycle.cpp` is an opt-in executable, not a
CTest that loads arbitrary installed software. Arguments are absolute scanner,
bundle, private cache and report paths, with optional `--ui seconds` or
`--partition-probe`. `windows/Tests/qualify_installed_plugins.py` consumes the
report/cache, requires a new disposable output directory, and exercises native
save/reopen and offline rendering. `--silent-seconds 8` additionally exercises
WASAPI and live voice telemetry. It retains a failed offline result while
collecting independent hardware evidence, and returns nonzero overall.

Primary logs: `bin/windows-upstream-{core,application,metadata,native-project,vst3}-tests.log`.
Plugin reports, archive records, captures and sources:
`bin/windows-plugin-qualification/`. Checkpoint executable/source hashes are
recorded in `bin/windows-checkpoints/upstream-plugins-20260921/manifest.json`.
