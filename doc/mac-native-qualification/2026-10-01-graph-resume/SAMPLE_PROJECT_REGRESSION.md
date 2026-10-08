# Sample project export regression investigation

The coherent snapshot at `/tmp/screamseq-graph-resume-coherent-20261001`
failed `sample-project` at the unchanged assertion “Native WAV matches
independent PCM/loop renderer exactly”. The test is still valid; no tolerance
was added and no fixture expectation was changed.

A separately compiled diagnostic linked that snapshot's immutable libraries.
The first MOD difference occurs at frame 23,071, left channel:

- Native WAV: `0x1.d57918p-4`.
- Independent PCM/loop renderer: `0x1.d5791ap-4`.
- Direct renderer of the packed native snapshot: `0x1.d5791ap-4`.

Thus the independently installed PCM and loop geometry agree with the native
sample archive. The difference is introduced after storage decoding.

`PluginChain::attachInstruments` now materializes an implicit channel mixer
unconditionally for a non-null native song. Export therefore traverses
per-channel float buffers and sums them, whereas this plain sample fixture's
previous core-only rendering path accumulates through the original mixer.
A reference with the independently installed PCM/loop geometry plus an explicit
default native mixer matches the exported WAV **bit-for-bit for all five formats
and their complete durations**:

| Format | Frames | Different samples versus core-only reference | Maximum absolute difference |
| --- | ---: | ---: | ---: |
| MOD | 373,440 | 5,803 | 1.04308e-6 |
| XM | 376,128 | 6,591 | 1.00583e-6 |
| S3M | 376,128 | 35,546 | 1.16229e-6 |
| IT | 376,128 | 12,699 | 1.07288e-6 |
| MPT | 376,128 | 12,699 | 1.07288e-6 |

These temporary diagnostics check the initial complete export, not the later
clipboard export variants. Their generic inherited final “PASS” string does
not represent execution of those skipped variants. Diagnostic sources and
executables are under `/tmp/screamseq-sample-project-diagnostic`.

The audio agent owns the production fix: retain the original plain-sample export
path when no rack or native routing feature needs the implicit mixer. Live
playback already projects its mixer separately for graph capability. The
unchanged complete `sample-project` regression must pass after that fix.


Verification after the production fix: the unchanged complete `sample-project`
test passed in two normal targeted batches. The latest batch is
`/tmp/screamseq-opaque-preset-tests3.log`: all nine requested tests passed
(8.03 s total), including sample project export, native mixer, publication,
plugin latency, graph/session history and portable Windows model checks.
