# Project-to-host preparation gate

`Session/HostedProject.*` decodes preserved project plugin records into the shared
`PluginState` representation and absolute automation into its canonical 48-kHz
frame timeline. It does not rewrite the project or resolve plugins by display
name. Version-5 aliases use shared assignment validation; unavailable AU remains
unavailable with its original state intact.

`HostedProjectPlayback` prepares the actual shared renderer and PluginChain on a
stopped control/document owner, including column mutes, precise notes, the mixer,
ordinary and sample-instrument graphs. It prepares a chain even with an empty
rack. It is noncopyable/nonmovable; renderer destruction precedes chain
destruction, including constructor failure. The callback must not serialize or
rebuild this object. The Windows application now owns it on its document worker
and uses its bounded `render()` from the WASAPI callback. The offline application
tests use that same method. Device requests larger than 4096 frames are split;
processor failure silences the entire request and remains latched. See
`../../RESUME_PROGRESS.md` for integration evidence and remaining qualification.

The test renders actual PCM, not a mock backend. ARM64 Release execution:

- Built-in saved gain changes the real demo output at 44100, 48000 and 96000 Hz.
- Worst partition deltas for 17/128/4096-frame blocks were respectively
  `6.70552e-8`, `1.04308e-7` and `0`, below the shared `1e-6` bound.
- The supplied actual Mac project restored and rendered through the full shared
  host path at 48000 Hz. The tested one-second window was finite/nonzero,
  energy L1 `0.717911`, with partition delta `0`; the source tree was not mutated.
- Canonical absolute timestamps, opaque state, stable instance identity and
  primary/alias MIDI channels were checked.
- After independent review passed, durable regressions were added for audible
  absolute automation at canonical frame 24000 across 44.1/48/96 kHz: pre-boundary
  PCM difference was zero, post-boundary output changed, and worst partition
  delta was `3.241e-7`. A late-event negative control stayed at baseline. Nonzero
  cursor starts applied earlier automation before rendering.
- Repeated failures after mixer adapters were attached preserved the source and
  allowed successful subsequent preparation. Prepared playback also rendered
  after its source Document and ProjectState were destroyed. These passed as
  functional lifetime checks, not sanitizer evidence.
- A red test showed the empty-rack path accepted zero sample rate. Preparation
  now checks the same 8000..384000-Hz range as shared NativeEffect before loading
  a renderer; the full test passes.
- A genuine include-order compile failure exposed ambiguous `mpt::lcg_musl` in
  LofiMat. Its intended global namespace is now explicitly `::mpt`; no RNG or DSP
  algorithm changed. The new inclusion order compiles.

Reproduce using installed VS CMake/CTest:

```text
cmake -S windows/Tests/HostedProject -B bin/windows-hosted-project -G "Visual Studio 17 2022" -A ARM64
cmake --build bin/windows-hosted-project --config Release --parallel 3
ctest --test-dir bin/windows-hosted-project -C Release --output-on-failure
```

Set `SCREAMSEQ_REFERENCE_PROJECT` to the supplied read-only reference file to run
that opt-in case; otherwise it prints SKIP. `SCREAMSEQ_EDITOR_BUILD` names the
matching shared-core build (default `bin/windows-snapshot-fix`). Rebuild/reselect
that archive after core fixes; these tests compile project codecs/bridge/hosting
from source but import the core libraries.

No physical audio was opened. This short offline window does not qualify full-song
tails, WASAPI integration, acoustic output, Mac audio equivalence, vendor plugins,
realtime allocation freedom or sanitizers. Mac application reopening is still
unverified. NativeProject model/metadata preservation tests remain separate.
