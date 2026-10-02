# Native Windows CI qualification — 2026-10-02

The final source checkpoint `684a5023acda1a1823341d2387ee1a8040e2ef91` is pushed to `origin/codex/graph-completion`. Native Windows CI was dispatched explicitly through `ScreamSeq-Windows.yml` (workflow `371408826`): [run 36960036036](https://github.com/rewbs/ScreamSeq/actions/runs/36960036036), created at 03:24:31 UTC. Its exact head SHA is verified and status is **in progress** at this stopping point; no final result is claimed. [Pending run metadata](logs/windows-684a5023a-run-pending.json) is retained.

The routing and grouped-insertion checkpoint `261b153876083cfc942d20c806c343604bb7477c` passed native Windows x64 CI on `windows-2022`: [run 36956714637](https://github.com/rewbs/ScreamSeq/actions/runs/36956714637), completed at 02:59:57 UTC. The application builds with MSVC in Release configuration. The job ran for 19 minutes 24 seconds.

| Selected CI test set | Result | Duration |
| --- | --- | --- |
| Portable model/runtime suites | 36/36 passed | 17.16 seconds |
| Native Windows worker suites | 47/47 passed | 16.85 seconds |
| Actual Windows VST3 audio-bus worker (included above) | Passed | 6.34 seconds |

The actual x64 VST3 fixture/scanner worker verifies physical 5/3-channel buses, stereo/odd-mono slices, inactive prepared capacity, timed automation, live ordinary/sample graph copy add/Amount/remove/Undo, rejected publication retention, and continuous render position. It also covers rack/recipe latency changes 0→13→3 without vendor reactivation, compatible effect preset replacement and restoration, subsequent routing/manual fences, ongoing ramps, typed aggregate stage cables, and the auxiliary-only stage follower's independent PCM/envelope oracle. Bus/copy/stage cases use 44.1/48/96 kHz and 17/128/4096-frame blocks; rack preset cases use 17/512/4096.

Native graph-document stage API/history/metadata tests passed. The earlier processing-group test crash is corrected and that scenario passes. Other native workers cover document/rack/graph/recorded-timeline publication, trigger instruments, pattern performance, mixer integration and unified history. These are native Windows executables, separate from the Mac-hosted Windows adapter tests.

The audit covers host C++ allocations and frees where labeled. The Windows test shim supplies zero for unavailable lock instrumentation; this result does not establish lock freedom or vendor-private realtime safety. Desktop, HWND interaction, WASAPI/device execution and musician-session checks remain deferred by the user's instruction. The workflow deliberately selects portable and worker labels; it does not claim the separate desktop/device tests ran.

The grouped `plugin.add` fix passed all four native unified-history scenarios: append, prepend, add inside the group, and targetless implicit Master insertion. Each verifies dry-run isolation, selected dry-boundary preservation, one exact Undo/Redo and native save/reopen.

**Final run pending:** the later fix for detaching a group with explicit dry routes and a retained sidechain passed three local suites (shared boundary model, Mac session API, and the Mac-hosted Windows mixer adapter). This native Windows checkpoint does not cover that subsequent source change or the later playback port-membership correction. Windows now forwards active bus reads by stable instance identity to the playback chain; its new native worker regression is included in the in-progress final run above. See [detach evidence](GROUP_DETACH.md).

Evidence:

- [Complete native build and selected test output](logs/windows-261b15387-build-and-tests.log)
- [Native worker output, including VST3 PCM result](logs/windows-261b15387-worker.log)
- [GitHub run metadata and exact source commit](logs/windows-261b15387-run.json)

The downloaded original artifact is retained locally at `artifacts/graph-resume-windows/ci-261b15387/`.
