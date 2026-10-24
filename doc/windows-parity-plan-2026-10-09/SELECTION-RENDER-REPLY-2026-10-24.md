# Selection-render reply correction

**Executed ARM64 checkpoint,23:04UTC:** sourceb0375de637dd940cce225a0aba15fab69ee2f50f
compiled once; ApiTests passed21.80s with slow selection/replay, ordinary timeout,
queued cancellation and close assertions. Both recording native groups and five
actual-app cases passed, including the complete layered sample/VST3 selection
PCM/reopen/independent-layer and full-song export journey. Fresh Computer Use
passed the composing/recording/mixer/focus smoke; owned QA exited normally.
The inventory preflight failure and its explicit mandatory-setup correction are
retained in `render-reply/`, alongside build/check/foreground receipts. No rebuild
was needed. Corrected x64 artifact replay and current platform CI remain pending;
the earlier x64 failure is not declared closed by ARM64 evidence. No package or
main merge has occurred. Original finding and planned checks follow.

24 October 2026, after the 22:36 UTC status snapshot. Source-only until the next
coordinated checkpoint passes; no production claim follows from this correction.

The mandatory layered instrument journey ran on the exact x64 CI executable from
source `82834c95bd7a06fc8f9ad62ca3cf1a1ac1d0bded`, with its matching native VST3
fixture and a real silent WASAPI endpoint under x64 emulation on ARM64. Its
six-domain composing/history/reopen stage passed. The first offline
`sample.renderSelection` then returned the server's -32003 uncertain-outcome
reply after its five-second deadline. The test did not reach its final PCM
equivalence checks. Preserve `bank-closure/ci-x64/layered-live-checks.json` and
`layered-live.log`; no uncertain request was repeated.

`windows/App/ApiDispatch.hpp` had extended only `document.exportAudio` to the
Mac socket's 120-second total reply budget. Selection rendering, which may render
up to 300 seconds of audio, still had the ordinary five-second reply budget.
The focused correction includes `sample.renderSelection` in that same bounded
offline-render path. Queue admission remains five seconds; an unstarted job is
cancelled, and an entered job still reports an uncertain outcome on timeout or
close. No render/PCM/history/allocation bounds change. No audio-thread code changes.

`windows/Tests/Api/ApiDispatchTests.inc` adds a real named-pipe selection render
lasting 5.25 seconds and exact same-ID replay, while retaining slow export,
ordinary-write timeout, queued cancellation and active-close assertions. The
existing 30-second CTest deadline is unchanged. The actual-app layered journey
uses a 120-second client only for its offline-render helper, as its full-song
export helper already does. Its fixture, exact PCM, independent sample/plugin
layers, allocation, history and project-preservation assertions are unchanged.

Batch this with the already committed completed-take Discard guard. The frozen
candidate must build app/scanner, API and document-controller targets once;
run ApiTests plus the recording and live-recorded-publication native groups,
the four retained recovery/recording/interleaved-history cases and the complete
layered instrument case. Then inspect a fresh isolated foreground process for
composing, native recording, graph/mixer and keyboard focus. The corrected x64
artifact/provider must also execute the failed layered journey with a real
endpoint; a green ARM64 run alone does not close that failure.

Both Mac architectures completed the preceding 828 workflow successfully at the
fresh 22:38 UTC read. Windows CI remains failed on device-less live startup;
those failures are not waived. Current Mac/shared product and persistence inputs
are unchanged by these Windows corrections. Reuse evidence only at those bounds.
The stopped Discard-only waiter must not restart. A new combined checkpoint must
recheck exact CI jobs and actual later starts, respect the hourly gate, and retain
every failed receipt. Main merge and packaging remain gated on final acceptance.
