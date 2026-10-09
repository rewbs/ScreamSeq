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

## Follow-up inspection and corrective batch, 23:02 UTC

Both advertised outer archives were downloaded and their SHA-256 values verified.
Their inner app archives and executable hashes also match the retained identities.
The Intel executable is
`6fe5152943231e81f691c93f7f0bb4fdcb1abbb39d994832ca47b6297bd5228d`;
its app archive is
`cd6311e3e8f3d4644e24822cef11581e313028d8d9cb08c46acff02e9987554e`.
Both apps identify built source `494d65563`. The Windows report contains five
successful original fixture legs plus its separately retained edited outputs.

The retained Windows→Intel exchange was dispatched once at 22:56:23 UTC and
confirmed live as run **38001876485**, job **114061620308**. It selects Windows
run 37997874323 and the early Intel app artifact from 37997877049, using remote
tooling ref `cd22e3daa` with unchanged product inputs. Its build jobs are skipped.
No outcome or return leg is claimed yet.

The original Intel job is now terminal **failure**: 121/121 native CTests passed,
and the interface harness failed the same four groups as ARM (draft-plugin,
signal-graph, graph-ports, core-layout). Its API report did not match the baseline
and reported one preservation failure. This is consistent with the previously
observed unchanged `order.edit` move creating history; the full report still
needs retained readback before attributing every API discrepancy.

Source corrections prepared together, not built or tested:

* Envelope `Files` previously used only PID and `GetTickCount64`; two live
  instances created within one tick can share a folder. A process-local atomic
  serial now distinguishes them. `jsonGuards` asserts that its absent folder
  differs from the populated one and is absent **before** the rejected import,
  while retaining its original postcondition.
* The private GUI helper now permits explicitly requested wide client bounds
  beyond a small desktop's default tracking maximum, only on a registered owned
  fixture HWND during the resize. It retains the production minimum and verifies
  the resulting client pixels exactly. Both dock fixtures use it. Automation
  tests explicitly establish the full layout before selecting full-layout
  controls, including after floating. All visibility, focus, caret, generation,
  geometry and floating-minimum assertions remain. This is a candidate fix for
  the observed hidden/wide-layout failures, awaiting native execution.
* `Document::orderEditChanges` shares validation/no-op admission with the actual
  order mutator and both native adapters. Mac checks before stopping audio;
  exact assign and move-to-self preserve revision, Undo and Redo. Moving repeated
  occurrences of the same pattern still changes their stable order identities.
  Narrowing guards reject oversized indices before conversion. A new portable
  `order-edit-admission` target is registered on both platforms for rejection,
  no-op and occurrence-identity history. The existing strict API assertion is
  unchanged. This advances the relevant P3a semantic repair needed by P0c.
* The retained `audio-services.json` shows both `AudioEndpointBuilder` and
  `Audiosrv` stopped. The Windows workflow now attempts to start them in that
  order on the disposable runner, records before/after/error states, then runs
  the same probe and all required app cases. It changes no endpoint selection,
  default routing or user machine service. Starting services does not prove an
  endpoint will appear; if none does, the audio-dependent assertions still fail.

These changes join the already prepared four Mac interface corrections in one
pending checkpoint. No local build/test was run. The next normal local build is
still no earlier than 23:13:34 UTC. Recheck the exact source freeze, run the new
order admission and affected envelope/dock/history/session cases, then actual
API no-op and required app groups. Because shared document admission changed,
both native builds are required; unchanged codec/audio DSP evidence remains
separately reusable. Do not claim that the fixture/helper changes fix product
behavior or that the runner setup qualifies physical audio.

## Reciprocal completion and corrective handoff, 23:09 UTC

Windows→Intel run **38001876485**, job **114061620308**, completed successfully.
The return run **38002680949**, job **114064237751**, also completed successfully,
using the original Windows app from 37997874323 and the preceding Intel exchange.
Both app-build jobs were skipped. Each leg passed all five original fixtures and
the two Windows-created `rendered.screamseq` and `imported.screamseq` projects.

Both exchange artifacts were downloaded and checked against their advertised
SHA-256 values. Report hashes match their retained-run receipts, and the return
receipt identifies the exact preceding Intel receipt. A separate direct comparison
of the original Windows outputs against the final reopened Windows files found
typed equality and byte equality for all seven files. The committed
[receipt](reciprocal-checkpoint-03.json) retains app identities, run IDs, artifact
hashes, report hashes, lineage and per-file hashes. This closes this frozen
candidate's Intel reciprocal leg; it does not close the API, UI, device or new
product-change gates.

The no-op correction now has additional unexecuted Mac unified-history assertions:
API move-to-self and native unchanged assignment retain an existing interleaved
Redo branch, and the live-device case requires transport to remain running and
continue advancing frames after a move-to-self. The shared test also checks exact
song bytes. Existing Windows document-operations coverage already asserts an
unchanged assignment preserves history, Redo and stop-callback count; retain it.

Next consolidated checkpoint, no earlier than 23:13:34 UTC locally:

1. Freeze all correction inputs; build app and affected model, worker and native
   targets together. Run shared order admission, document/history/arrangement,
   envelope JSON guards, dock geometry and the private-window owner census.
2. Build both native platforms at that same source freeze. Mac must run portable
   order admission, unified history, the four failing interface groups and actual
   API no-op conformance. Device-backed unified history remains a separate required
   hardware-capable check if the runner cannot provide a device.
3. Windows x64 must repeat the failed native groups and required actual-app cases
   with service diagnostics, preserving every assertion. A missing endpoint or
   service failure remains a visible qualification failure, not a skip.
4. Reuse archived codec/fixture evidence only after checking the relevant source
   and tool inputs; the shared document change invalidates behavioral no-op evidence
   even though it does not alter the project codec. Keep full P0 closure open until
   the platform-specific failures are resolved and results are reviewed.

No local build or test was run for this corrective preparation. Remote checks in
this follow-up exercised the retained binaries only; no build was requested.
