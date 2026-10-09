# Existing CI evidence inspected during peer-plan review

Read-only retrieval; no workflows were started or rerun. All three job logs identify source `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. The excerpts below establish these failures, not the status of any later run.

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
