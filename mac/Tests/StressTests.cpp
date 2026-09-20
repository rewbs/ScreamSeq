#include "editor/TrackerDocument.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace Tracker;
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
int main() {
  try {
    auto document = Document::demo();
    document->transaction([](CSoundFile &song) { Document::resizeChannels(song, 127); });
    auto pattern = document->addPattern(1024, false, 0);
    document->setOrder(0, pattern);
    document->removeOrder(1);
    document->transaction([&](CSoundFile &song) {
      for (int row = 0; row < 1024; row += 4)
        for (int ch = 0; ch < 127; ++ch) {
          auto &cell = *song.Patterns[pattern].GetpModCommand(row, ch);
          cell.note = uint8_t(37 + ch % 24);
          cell.instr = 1;
          cell.volcmd = VOLCMD_VOLUME;
          cell.vol = 1;
        }
    });
    const auto bytes = document->serialize();
    Renderer renderer(bytes, 48000);
    std::atomic<bool> done{false};
    std::atomic<uint64_t> accepted{0}, saturated{0};
    std::thread editor([&] {
      uint64_t index = 0;
      while (!done.load()) {
        std::vector<Edit> edits;
        for (int ch = 0; ch < 127; ++ch)
          edits.push_back({uint16_t(pattern),
                           uint16_t((index % 255) * 4),
                           uint16_t(ch),
                           {},
                           {uint8_t(37 + (index + ch) % 24), 1, 1, 1, 0, 0}});
        (renderer.enqueue(edits) ? accepted : saturated).fetch_add(1);
        if (index % 3 == 0)
          renderer.preview({61, 3, 1, true});
        if (index % 3 == 1)
          renderer.preview({61, 3, 0, false});
        if (index % 19 == 0)
          renderer.panic();
        renderer.mute(uint32_t(index % 127), index % 2);
        ++index;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
      }
    });
    std::array<float, 256> block{};
    uint64_t frames = 0, allocations = 0, deallocations = 0, locks = 0;
    double maximum = 0, energy = 0;
    uint32_t peakVoices = 0;
    while (frames < 48000 * 60) {
      uint64_t a, d, l;
      auto start = std::chrono::steady_clock::now();
      tracker_audit_begin();
      auto count = renderer.render(block.data(), 128);
      tracker_audit_end(&a, &d, &l);
      maximum = std::max(maximum,
                         std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
      allocations += a;
      deallocations += d;
      locks += l;
      if (!count)
        break;
      frames += count;
      peakVoices = std::max(peakVoices, renderer.telemetry().voices);
      for (float sample : block) {
        if (!std::isfinite(sample))
          energy = NAN;
        else
          energy += sample * sample;
      }
    }
    done = true;
    editor.join();
    if (frames != 48000 * 60 || renderer.faulted() || allocations || deallocations || locks || !std::isfinite(energy) ||
        energy <= 0 || !accepted)
      throw std::runtime_error("dense concurrent edit/audition render failed");
    std::cout << "PASS 60-second, 127-channel/1024-row render with concurrent editing/audition/panic: " << accepted
              << " batches, " << saturated << " safely rejected, " << peakVoices << " peak voices, " << maximum
              << " us max, zero intercepted allocations/releases/locks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
