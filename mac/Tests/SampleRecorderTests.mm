#include "../Audio/SampleRecorder.hpp"
#include "../Audio/CaptureEndMonitor.hpp"
#include "GraphRealtimeAudit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace Tracker;
static void check(bool result, const char *message) { if(!result) throw std::runtime_error(message); }
template<typename F> static void rejects(F run, const char *message) {
  bool rejected = false; try { run(); } catch(const std::exception &) { rejected = true; }
  check(rejected, message);
}
static void monitorChecks() {
  auto waitFor = [](const std::atomic<bool> &value) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(!value.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(value.load(), "Capture end monitor did not respond within two seconds");
  };
  std::atomic<bool> capturing{true}, ended{false};
  CaptureEndMonitor monitor;
  monitor.stop(); // Partial setup and repeated teardown need no worker.
  monitor.start([&] { return capturing.load(); }, [&] { ended = true; });
  monitor.stop(); monitor.stop();
  check(!ended, "Explicit owner stop must not call the autonomous end action");
  // A previous stop request must not poison a restarted limit/device-loss run.
  for(unsigned run = 0; run != 2; ++run) {
    capturing = true; ended = false;
    monitor.start([&] { return capturing.load(); }, [&] { ended = true; });
    capturing = false;
    waitFor(ended); monitor.stop();
  }
  std::atomic<bool> entered{false}, finished{false};
  auto finish = [&] { entered = true; std::this_thread::sleep_for(std::chrono::milliseconds(25)); finished = true; };
  monitor.start([] { return false; }, finish);
  waitFor(entered); monitor.stop();
  check(finished, "Explicit stop must join an already-entered device stop before disposal");
  entered = false; finished = false;
  {
    CaptureEndMonitor owned;
    owned.start([] { return false; }, finish);
    waitFor(entered);
  }
  check(finished, "Destructor must join the autonomous device stop before owner teardown");
}
int main() { try {
  monitorChecks();
  using End = SampleCaptureBuffer::End;
  SampleCaptureBuffer buffer;
  check(!buffer.reading().capturing && buffer.pcm().empty(), "Idle recorder contains no take");
  for(const auto [rate, inputs, first, channels, seconds] : {
       std::tuple{7999u, 2u, 0u, 1u, 1.}, {48000u, 0u, 0u, 1u, 1.}, {48000u, 257u, 0u, 1u, 1.},
       {48000u, 2u, 2u, 1u, 1.}, {48000u, 2u, 1u, 2u, 1.}, {48000u, 2u, 0u, 3u, 1.},
       {48000u, 2u, 0u, 1u, 0.}, {48000u, 2u, 0u, 1u, 301.},
       {48000u, 2u, 0u, 1u, std::numeric_limits<double>::quiet_NaN()}}) {
    rejects([&] { buffer.prepare(rate, inputs, first, channels, seconds); }, "Invalid preparation rejects before capture");
    check(buffer.pcm().empty() && !buffer.reading().capturing, "Rejected preparation preserves idle storage");
  }
  // Select the middle two channels of a four-input device, not its first pair.
  const std::array<float, 20> source{99,.1f,-.2f,99, 99,.3f,-.4f,99, 99,1.2f,-1.3f,99, 99,.5f,.6f,99, 99,.7f,.8f,99};
  buffer.prepare(8000, 4, 1, 2, 4. / 8000);
  rejects([&] { buffer.pcm(); }, "A live take cannot be read as stable PCM");
  rejects([&] { buffer.prepare(8000, 1, 0, 1, 1); }, "Preparation cannot replace a live take");
  tracker_audit_begin();
  buffer.ingest(source.data(), 2);
  buffer.ingest(source.data() + 8, 3);
  buffer.ingest(source.data(), 5); // Capacity termination cannot overwrite the beginning.
  uint64_t allocations = 0, frees = 0, locks = 0;
  tracker_audit_end(&allocations, &frees, &locks);
  check(!allocations && !frees && !locks, "Prepared capture callback allocates/frees/locks nothing");
  const auto full = buffer.reading();
  check(!full.capturing && full.end == End::Limit && full.frames == 4 && full.channels == 2 && full.sampleRate == 8000,
        "Capacity stops on the exact final frame, retaining a partial final callback");
  const std::array<float, 8> expected{.1f,-.2f,.3f,-.4f,1.2f,-1.3f,.5f,.6f};
  check(std::equal(buffer.pcm().begin(), buffer.pcm().end(), expected.begin()), "Selected stereo PCM is exact and unclipped until import");
  check(full.clipped == 2 && full.peak == 1.3f, "Peak and clipped-value telemetry cover selected channels only");
  buffer.finish(End::Stopped); buffer.finish(End::DeviceChanged);
  check(buffer.reading().end == End::Limit, "Stop preserves the original reason for an automatic stop");

  SampleCaptureBuffer mono;
  mono.prepare(48000, 4, 2, 1, 1);
  mono.ingest(source.data(), 3); mono.finish(End::Stopped);
  check(mono.pcm().size() == 3 && mono.pcm()[0] == -.2f && mono.pcm()[2] == -1.3f, "Arbitrary mono source channel retains exact samples");
  mono.ingest(source.data(), 3);
  check(mono.reading().frames == 3, "Cancelled capture cannot append later callbacks");

  for(const auto failure : {End::DeviceChanged, End::RenderError, End::OversizedBlock, End::InvalidInput}) {
    SampleCaptureBuffer interrupted; interrupted.prepare(48000, 4, 1, 2, 1);
    interrupted.ingest(source.data(), 2); interrupted.finish(failure, -123);
    interrupted.ingest(source.data() + 8, 2);
    check(interrupted.reading().end == failure && interrupted.reading().systemError == -123 && interrupted.pcm().size() == 4,
          "Failure stops ingestion and retains every complete prior frame");
  }
  SampleCaptureBuffer invalid; invalid.prepare(48000, 2, 0, 2, 1);
  std::array<float, 4> bad{.1f, .2f, std::numeric_limits<float>::infinity(), .4f};
  invalid.ingest(bad.data(), 2);
  check(invalid.pcm().empty() && invalid.reading().end == End::InvalidInput, "A malformed block is rejected atomically");
  invalid.prepare(48000, 2, 0, 2, 1); invalid.ingest(nullptr, 1);
  check(invalid.pcm().empty() && invalid.reading().end == End::InvalidInput, "Missing input pointer is bounded failure");
  invalid.prepare(48000, 2, 0, 2, 1); invalid.ingest(source.data(), SampleCaptureBuffer::maximumBlockFrames + 1);
  check(invalid.pcm().empty() && invalid.reading().end == End::OversizedBlock, "Oversized callback is rejected before touching its input");
  invalid.prepare(48000, 4, 1, 1, 1);
  const float unselectedBad[]{std::numeric_limits<float>::quiet_NaN(), .25f, 0, 0};
  invalid.ingest(unselectedBad, 1); invalid.finish(End::Stopped);
  check(invalid.pcm().size() == 1 && invalid.pcm()[0] == .25f, "Unselected hardware channels do not contaminate PCM");

  // Simultaneous telemetry reading never touches unpublished PCM or locks the
  // producer. Finish/join precedes the only PCM read.
  SampleCaptureBuffer concurrent; concurrent.prepare(48000, 2, 0, 2, .1);
  std::array<float, 64> block; block.fill(.5f);
  std::thread producer([&] { while(concurrent.reading().capturing) concurrent.ingest(block.data(), 32); });
  uint32_t previous = 0;
  while(concurrent.reading().capturing) {
    const auto current = concurrent.reading(); check(current.frames >= previous && current.frames <= 4800, "Telemetry frame count is bounded and monotonic"); previous = current.frames;
  }
  producer.join();
  check(concurrent.pcm().size() == 9600 && std::all_of(concurrent.pcm().begin(), concurrent.pcm().end(), [](float x) { return x == .5f; }),
        "Concurrent status polling retains the complete capture");
  SampleCaptureBuffer capped; capped.prepare(384000, 1, 0, 1, 300);
  check(capped.reading().capacityFrames == SampleCaptureBuffer::maximumFrames, "High-rate capture has a fixed storage ceiling");
  capped.finish(End::Stopped);
  check(sampleRecordingOwnsDocument("song-a", "song-a") && !sampleRecordingOwnsDocument("song-a", "song-b") &&
        !sampleRecordingOwnsDocument("", ""), "A staged take cannot be committed to a replacement document");
  std::cout << "PASS input capture: source channels, exact PCM, capacity/stop/fault retention, telemetry and concurrent reader; no microphone opened\n";
#ifndef TRACKER_SANITIZER
  std::cout << "PASS input ingestion allocation/free/lock audit\n";
#endif
  return 0;
} catch(const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
