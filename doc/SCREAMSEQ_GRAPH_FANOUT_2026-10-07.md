# Graph cable fan-in and fan-out — 7 October 2026

## Implemented in this checkpoint

Fresh audio socket drags add a contribution. A source can feed several destinations, and an input can sum several sources. This works from either end of a cable, with no Option modifier or confirmation dialog. Existing dry outputs, insert ownership, other destinations, gains, disabled states and pre-fader settings survive.

Selected wire endpoint handles are the separate gesture for replacing a connection. Delete cuts only the selected branch; one document Undo restores it. Drawing an existing cable again is a no-op. Main-output cables are labelled **Main output**, rather than **Aux 0**. Disabled sidechain/Main-input contributions now look disabled on the Mac canvas.

Moving an insert chain is an explicit **Move insert chain…** action in the Mac socket/context menu and command catalogue, or a processor/serial-chain drop onto a wire. Socket connection help includes cable counts and the channel bus tap position. Keyboard **Connect to…** uses the same additive behavior.

The Windows recipe canvas adds from either socket and exposes separate selected-wire endpoint handles. Its song canvas preserves sibling bus sends, graph auxiliary destinations and plugin output destinations, including an instrument's implicit Master route. Exact destination edits/cuts no longer replace every output branch. Its song canvas currently exposes the selected destination handle; the recipe canvas supports both endpoints.

The existing shared processor graph already implements the required summation, distribution and delay alignment, so this checkpoint does not replace its DSP. The `mixer.plugin.route` API now preserves existing destination order: repeating the same destination set, even in a different order, does not create an unnecessary Undo entry. Mac/Windows contracts match; schema and workflow documentation are updated.

## Verification

- All **109 CTest tests passed** in the separate `bin/mac-routing-flow` build. Final Windows graph and mixer targets were rebuilt/retested after their last helper changes: **2/2 passed**.
- Native AppKit interface suite passed, including new fresh-socket vs selected-handle regressions, many-to-many contributions, detached effects, implicit instrument Master preservation, disabled input appearance, stale context rejection and keyboard chain movement.
- Full local socket suite passed against both the silent host and actual packaged application. The new journey covers two-by-two bus sends, detached processor main-input mixes and output branches, no-op/stale/dry-run/cycle rejection, exact cut, Undo/Redo and saved native metadata.
- Added rendered PCM matrix: eight simultaneous main/auxiliary cables, unequal processor latencies, two independent stereo sources and two destinations. At 44.1/48/96 kHz and callback sizes 1/17/128/4096, samples match independent arithmetic and are partition-independent. Each processor advances once per chunk; callback audits report no allocation, free or lock.
- Native AU/VST mixer coverage passed, including live direct cable sums/splits, transport preservation and rejected-cycle state preservation. Session coverage saves and reopens the complete routing graph.
- Real Mac UI: seven ordinary socket drags built channel splitting, occupied-input summing in the reverse drag direction, separate detector input and effect output branching. API reads verified four input contributions and three output destinations, with both original dry paths and detached owners intact. Selected Delete and one keyboard Undo restored the complete graph. A selected target-handle drag changed only Gainer's destination and retained both compressor branches.
- Final package passes strict code-signature verification. UI screenshots, state captures and logs are in `bin/mac-routing-flow/qualification/`.

### Display and audio performance

Retained clean baseline source fingerprint: `65ac048714d64cf1a24c9573f86d4d56711b14d199f1f6eb987decee03a1462a`. Runs used 48 kHz / 512 frames and explicit BlackHole 2ch. No pass threshold was changed.

| 60-second workload | Mean presented fps | Missed presentations | Maximum snapshot age | Audio overruns / starvation | Overall |
|---|---:|---:|---:|---:|---|
| Dense 127-channel pattern, no UI queries | 59.9875 | 0 | 31.659 ms | 0 / 0 | Pass |
| Native precision song, no UI queries | 59.9875 | 2 | 40.372 ms | 0 / 0 | Fail: snapshot freshness |
| Native song with initial AX/screenshot inspection | 58.3602 | 94 | 41.346 ms | 0 / 0 | Fail; inspection interference noted |

The unobserved native run's p99 CPU/GPU frame work was 0.500/1.029 ms. Its missed-presentation rate remained within the 0.1% gate, but one geometry update exceeded the existing two-frame freshness limit. This is still unresolved; a 60 fps average is not a substitute for passing that limit.

In the inspected run, 88/89 discarded drawables and both stale snapshots clustered within the first nine seconds. This correlates with UI inspection but does not prove it was the sole cause. The failed run is retained, not replaced with the cleaner result.

An opt-in diagnostic probe was subsequently added under the existing `--ui-test-runloop-trace` flag. It distinguishes main-queue wait from snapshot refresh/skipped work. It does not change scheduling, clean-run cadence or pass gates. Instrumented runs remain explicitly ineligible as clean qualification evidence.

The 60-second instrumented run reproduced a 35.785 ms stale snapshot. Recovery was enqueued at 25.957115 s and reached the main thread at 25.981113 s: **23.998 ms waiting**, followed by only **0.258 ms refreshing**. A 44.792 ms main run-loop interval contained no observed wait, while recorded tick/layout work explains only about 3 ms. This establishes delayed main-thread servicing; it does not distinguish uninstrumented AppKit work, blocking, or thread preemption. Presentation and audio gates passed, but snapshot freshness still failed. The next investigation is a bounded main-thread Time Profiler capture alongside these markers, rather than speculative changes to recovery thresholds or cadence.

## Remaining boundaries

- Channel/bus socket additions tap **after that bus's inserts**. Named processor output sockets tap that processor. True raw/pre-insert channel branching needs a separate shared dependency/PDC/transition extension; the UI now discloses the current tap instead of silently moving chains.
- Individual reusable-copy external Main routing is still owned by the assigned channel stage. Aggregate auxiliary stage ports and internal recipe branching remain available; individual copy sockets do not masquerade as a whole-bus route.
- A root-level envelope follower has one analysis tap. To follow a sum, route sources into a bus and follow that bus. Recipe follower inputs can receive several cables.
- Zero-delay feedback remains rejected atomically. Existing bus/route capacity limits apply.
- Native Windows desktop gestures and the Win32 frontend build were not run on this Mac. Portable Windows document/helper tests passed; native gesture fixtures were added for later Windows execution.

## Build and evidence

Final packaged source fingerprint (including the opt-in diagnostic probe): `0667d2c68235e1e008f2f2b39e20a7fbd500c40b77be990cffb5d2715b949273`.

- App: `bin/mac-routing-flow/ScreamSeq.app`
- [Final graph screenshot](../bin/mac-routing-flow/qualification/graph-final.png)
- [Dense clean performance report](../bin/mac-routing-flow/qualification/clean-dense/report.json)
- [Native clean performance report](../bin/mac-routing-flow/qualification/clean-native-unobserved/report.json)
- [Inspected native performance report](../bin/mac-routing-flow/qualification/clean-native/report.json)
- [Instrumented native diagnostic report](../bin/mac-routing-flow/qualification/clean-native-diagnostic/report.json)
- Core/API/AppKit logs: `bin/mac-routing-flow/qualification/{ctest,api-socket,api-app,interface-final,windows-parity-tests}.log`.

The musician's running `bin/mac-background/ScreamSeq.app` was neither replaced nor closed. Test instances used distinct copied bundle identities, disposable songs, and explicit BlackHole output for performance qualification. No system audio default was changed.
