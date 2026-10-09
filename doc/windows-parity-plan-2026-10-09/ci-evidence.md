# Existing CI evidence inspected during peer-plan review

Read-only retrieval; no workflows were started or rerun. The first three job logs identify source `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. Their excerpts establish baseline failures. The separate candidate checkpoint at the end records later existing runs and their different source identity.

## Job 113721103353

[Existing job](https://github.com/rewbs/ScreamSeq/actions/runs/37900322566/job/113721103353)

```text
2026-10-09T07:40:13.8389690Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:17.4330140Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:23.0198900Z -- The CXX compiler identification is AppleClang 17.0.0.17000013
2026-10-09T07:40:23.1754150Z -- The OBJCXX compiler identification is AppleClang 17.0.0.17000013
2026-10-09T07:51:55.5969790Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:153:8: error: no type named 'jthread' in namespace 'std'; did you mean 'thread'?
2026-10-09T07:51:55.5992140Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:187:38: error: no member named 'request_stop' in 'std::thread'
2026-10-09T07:51:55.6705260Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:270:46: error: no type named 'stop_token' in namespace 'std'
2026-10-09T07:51:55.7183260Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:270:25: error: no member named 'jthread' in namespace 'std'
```

## Job 113721103544

[Existing job](https://github.com/rewbs/ScreamSeq/actions/runs/37900322566/job/113721103544)

```text
2026-10-09T07:40:14.9687340Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:16.2294260Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:19.8494110Z -- The CXX compiler identification is AppleClang 17.0.0.17000013
2026-10-09T07:40:19.9409860Z -- The OBJCXX compiler identification is AppleClang 17.0.0.17000013
2026-10-09T07:45:09.1372070Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:153:8: error: no type named 'jthread' in namespace 'std'; did you mean 'thread'?
2026-10-09T07:45:09.1379570Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:187:38: error: no member named 'request_stop' in 'std::thread'
2026-10-09T07:45:09.1669910Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:270:46: error: no type named 'stop_token' in namespace 'std'
2026-10-09T07:45:09.1864720Z /Users/runner/work/ScreamSeq/ScreamSeq/mac/Audio/SampleRecorder.mm:270:25: error: no member named 'jthread' in namespace 'std'
```

## Job 113721102913

[Existing job](https://github.com/rewbs/ScreamSeq/actions/runs/37900322456/job/113721102913)

```text
2026-10-09T07:40:12.4583957Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:14.0101827Z ffe81aa4bfa83bc89b1e1dd92db06c67819c247a
2026-10-09T07:40:28.6638970Z -- The CXX compiler identification is MSVC 19.44.35229.0
2026-10-09T07:54:49.2725452Z D:\a\ScreamSeq\ScreamSeq\windows\App\SampleCaptureIntegration.inc(40,10): error C2535: 'bool `anonymous-namespace'::Application::protectRecordingTake(void)': member function already defined or declared [D:\a\ScreamSeq\ScreamSeq\bin\windows-ci\workspace-restore-tests.vcxproj]
2026-10-09T07:57:09.3257593Z D:\a\ScreamSeq\ScreamSeq\windows\App\SampleCaptureIntegration.inc(40,10): error C2535: 'bool `anonymous-namespace'::Application::protectRecordingTake(void)': member function already defined or declared [D:\a\ScreamSeq\ScreamSeq\bin\windows-ci\ScreamSeq.vcxproj]
```

## Candidate CI refresh

Read-only snapshot at **2026-10-09 10:16 UTC**. No new execution was requested. Both completed-job logs identify checkout **`29149338d67f81677700aee820291ba4927a87e4`**, the PR merge of candidate `4a5c8ed881a3168b7804ed6ddad8a62ed771c757` into main `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. Neither log qualifies later `d59c24c8c`/`e7f165eb4` changes. This snapshot is not a release qualification.

| Existing job | Observed result | Limit |
|---|---|---|
| [Apple Silicon 113765034330](https://github.com/rewbs/ScreamSeq/actions/runs/37913656837/job/113765034330) | Success: native app build with AppleClang 17.0.0.17000013, 120 CTests, Swift recovery and plugin-picker checks | Does not cover the separate full interface/socket/device/foreground gates; picker evidence is explicitly offscreen |
| [Intel 113765034523](https://github.com/rewbs/ScreamSeq/actions/runs/37913656837/job/113765034523) | Build in progress; test steps pending | No pass inferred; inspect eventual result before future validation |
| [Windows x64 113764926489](https://github.com/rewbs/ScreamSeq/actions/runs/37913656606/job/113764926489) | Build, 46 portable, 62 worker and three scratch app checks passed; retained-editor step failed | Two of three selected CTests failed; five subsequent Python app cases were not reached |

Selected log excerpts:

```text
2026-10-09T09:50:25.8450510Z 29149338d67f81677700aee820291ba4927a87e4
2026-10-09T10:07:24.7135230Z 100% tests passed out of 120
2026-10-09T10:07:24.7137390Z Total Test time (real) = 127.05 sec
2026-10-09T10:07:25.1144770Z PASS recovery generations, newest selection across documents, failed-write retention, document-scoped cleanup, conservative cross-session pruning
2026-10-09T10:07:25.3299130Z PASS plugin picker: cached-list explanation, AU/VST3 labels and selection, Rescan, empty-list Add disabled, controls fit; layout checked offscreen without focus or audio

2026-10-09T09:50:04.8321949Z 29149338d67f81677700aee820291ba4927a87e4
2026-10-09T10:12:01.9330030Z 100% tests passed, 0 tests failed out of 46
2026-10-09T10:12:31.0419496Z 100% tests passed, 0 tests failed out of 62
2026-10-09T10:12:53.8981445Z Fixture: Wide fixture did not expose independent region bodies
2026-10-09T10:12:53.9847163Z Fixture: GraphCurve control overlaps canvas/axes: 481
2026-10-09T10:12:57.7444697Z 33% tests passed, 2 tests failed out of 3
```

**Observed source entry points:** `windows/Tests/Workspace/WorkspaceRestoreTests.cpp::compactRestoreAndResizeKeepVisibleFocus` (around line 448 at local HEAD) requests a 1440×900-DIP client after a compact layout, then expects independent regions and a visible tracker. `resizeRestoreClient` converts DIPs through `GetDpiForWindow` and `AdjustWindowRectExForDpi`, but the reported failure alone does not establish the resulting client size. `windows/Tests/GraphCurveWindowTests.cpp::bounds` (around line 110) compares visible HWND rectangles against a DPI-scaled canvas including axes; control 481 is `GraphCurveWindow::kind`. `minimumPages` tests 440×300/310-DIP docking and a 440×500-DIP float. Relevant product owners are `WorkspaceDocking.inc`, `WorkspaceRegions.hpp`, `GraphCurveOwnerLayout.inc`, `NativeToolWindow.hpp` and `NativeControls.hpp`.

**Proposed bounded P0a follow-up:**

1. Preserve the existing x64 logs/artifacts and exact source/build identity. Read the eventual Intel result before deciding which Mac checks remain necessary.
2. On an owned private desktop, record effective DPI, process awareness, monitor/work area, requested and actual client/child bounds, selected page, canvas bounds and compact-layout thresholds for these two cases. Compare the existing ARM64 fixture setup; do not infer equivalence from a local historical pass.
3. Determine whether size clamping, native control sizing/rounding, a fixture precondition or product reflow causes each assertion. A fixture correction must establish the intended geometry and retain real compact-window coverage; it must not remove overlap/reachability or focus/draft assertions. A product correction must retain the same native owners, text, caret and capture identity through reflow.
4. Freeze the coherent correction, build only affected Windows native targets once, then run the three retained-editor CTests and five previously unreached Python cases. If common control/DPI/placement code changes, expand to retained curve/instrument/precise/workspace tests and mixed-DPI foreground A16. If only a test precondition changes, preserve its original product assertions and required x64 execution. Shared build/interface changes also require the affected Mac gate; unchanged Mac evidence is reusable only with matching inputs.
5. Complete the required candidate CI checks before main integration. Do not quarantine these failures, enlarge windows until assertions happen to pass, or repeatedly run full unrelated DSP suites. Root cause and correction are implementation work, not performed here.
