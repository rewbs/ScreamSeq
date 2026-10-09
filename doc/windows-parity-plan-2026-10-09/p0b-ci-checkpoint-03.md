# P0 checkpoint 03 — observed CI results

Read at 22:50 UTC on 9 October 2026. These jobs qualify frozen commit
`494d6556367ee6ac9c592ae19cc7a858e6bc2e1b`, not subsequent local Mac corrections
or the separate P1 mixer work. No job was restarted and no new build was run.

Windows x64 run `37997874323`, job `114048426317`, is terminal **failure**.
The app and all test targets built successfully. Portable/worker checks passed
135/136; `envelope-jsonGuards` failed at line 159 asserting
`!std::filesystem::exists(absent.folder)`. Its cause requires source/fixture
inspection; the previous 136/136 pass is not a waiver for this result.

Native checks passed 27/29. `parameter-automation-dock-tests` reported
`Fixture selected hidden combo`; `instrument-short-dock-window-tests` reported
`Focused-field fixture is not in the original wide layout`. The graph workflow
test now passed. These two failures contradict treating the recent local ARM64
pass as a complete x64 UI qualification. Inspect actual fixture geometry and
runner DPI/work-area behavior before changing assertions.

The actual-app group ran 30 tests and reported 11 failures, including teardown
failures. Shortcut/typing apps exited with code 1; recording checks did not reach
a valid playback clock. The new read-only environment probe provides direct
evidence: `audio.devices.get` returned an empty device list, settings had no
active endpoint, and transport had zero callbacks/frames. This was an owned
inspection process on a never-switched private desktop on Windows Server 2022
AMD64. It establishes absent enumerated endpoints, not yet the complete cause
of every failure. Required assertions were not skipped or weakened.

The scratch private-desktop group, typed fixture/pipe conformance baseline and
scheduling inventory passed. The completed Windows app is retained in artifact
`11648827863` (`screamseq-windows-logs`, 15,648,776 bytes), advertised archive
SHA-256 `232bd8dc2819d0c53dc5115e95916cc525e50d6203809fe7d62aae68b715c33c`.
The probe identifies app SHA-256
`e0e73c297df8856b6a788e920ba20f453dc48c22fb1811eae722e540d4ce7a20`.
The artifact has not yet been downloaded/verified locally for this checkpoint.

Intel Mac run `37997877049`, job `114048437720`, remains **in progress** in the
native interface step. Build, native model/host, Swift recovery/picker and sample
library steps have passed. Early app archival succeeded: artifact `11649735132`
(`screamseq-macos-macos-15-intel-app`, 14,628,605 bytes), advertised archive
SHA-256 `f5fba4ba3241f1729e4dfefa103cacc39c46401c2d1277397c06382574b18fe4`.
This archive makes retained Intel reciprocal checks possible without another
build. It is not evidence those checks have run or passed.

Next actions: verify/download the retained artifacts; inspect the Windows
fixture failures and audio environment; prepare one corrective batch while
preserving assertions; perform the missing Intel reciprocal legs from retained
apps; continue observing the same Intel handle to terminal state. P0 remains
open. Keep the remote P0 ref suitable for retained-app reuse until the frozen
exchange is prepared; do not accidentally push local product corrections first.
