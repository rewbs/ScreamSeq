#include "../Audio/AudioDevice.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc, char **argv) {
  try {
    auto document = Tracker::Document::demo();
    Tracker::AudioDevice device;
    device.configure(0, 128);
    auto bytes = document->serialize();
    for (int run = 0; run < 3; ++run) {
      device.play(bytes);
      std::this_thread::sleep_for(std::chrono::seconds(2));
      device.stop();
      auto telemetry = device.telemetry();
      if (telemetry.frames < device.sampleRate() || telemetry.callbacks < 100 || telemetry.overruns ||
          telemetry.p999Micros <= 0 || telemetry.p999Micros > telemetry.maxMicros + 10)
        throw std::runtime_error("Core Audio callback or deadline check failed");
      std::cout << "PASS Core Audio start/stop " << run << ": " << telemetry.callbacks << " callbacks, "
                << telemetry.maxMicros << " us maximum, " << telemetry.overruns << " overruns, " << device.sampleRate()
                << " Hz / " << device.bufferSize() << " frames, p99.9 <= " << telemetry.p999Micros << " us\n";
    }
    Tracker::PluginState effect{
        {kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Apple AULowpass"},
        {},
        false};
    device.setPlugins({effect});
    device.play(bytes);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    device.stop();
    auto telemetry = device.telemetry();
    if (device.pluginFailed() || telemetry.overruns)
      throw std::runtime_error("Live Audio Unit failed");
    std::cout << "PASS live AU/Core Audio: " << telemetry.callbacks << " callbacks, " << telemetry.maxMicros
              << " us maximum, " << telemetry.overruns << " overruns\n";
    if (argc > 1 && std::string(argv[1]) == "--soak") {
      const int seconds = argc > 2 ? std::stoi(argv[2]) : 60;
      if (seconds < 15 || seconds > 3600)
        throw std::runtime_error("Soak duration must be between 15 and 3600 seconds.");
      auto dense = Tracker::Document::demo();
      dense->transaction([](Tracker::CSoundFile &song) { Tracker::Document::resizeChannels(song, 127); });
      auto pattern = dense->addPattern(1024, false, 0);
      dense->setOrder(0, pattern);
      dense->removeOrder(1);
      dense->transaction([&](Tracker::CSoundFile &song) {
        song.m_nDefaultGlobalVolume = 8;
        song.Order().assign(32, pattern);
        for (int row = 0; row < 1024; row += 4)
          for (int channel = 0; channel < 127; ++channel) {
            auto &cell = *song.Patterns[pattern].GetpModCommand(row, channel);
            cell.note = uint8_t(37 + channel % 24);
            cell.instr = 1;
            cell.volcmd = Tracker::VOLCMD_VOLUME;
            cell.vol = 1;
          }
      });
      device.play(dense->serialize());
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
      uint32_t iteration = 0;
      while (std::chrono::steady_clock::now() < until) {
        std::vector<Tracker::Edit> edits;
        for (int ch = 0; ch < 127; ++ch)
          edits.push_back({uint16_t(pattern),
                           uint16_t((iteration % 255) * 4),
                           uint16_t(ch),
                           {},
                           {uint8_t(37 + (iteration + ch) % 24), 1, 1, 1, 0, 0}});
        if (!device.renderer()->enqueue(edits))
          throw std::runtime_error("Soak edit queue unexpectedly saturated");
        device.renderer()->preview({61, 3, uint8_t(iteration % 2 ? 0 : 1), iteration % 2 == 0});
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      device.stop();
      auto measured = device.telemetry();
      std::cout << seconds << "-second Core Audio/AU measurement: " << measured.callbacks << " callbacks, max "
                << measured.maxMicros << " us, p99.9 <= " << measured.p999Micros << " us, " << measured.overruns
                << " overruns at " << device.sampleRate() << " Hz / " << device.bufferSize() << " frames\n";
      if (measured.frames < device.sampleRate() * (seconds - 1) || measured.overruns || device.renderer()->faulted() ||
          device.pluginFailed() || measured.p999Micros <= 0 ||
          measured.p999Micros >= double(device.bufferSize()) / device.sampleRate() * 500000)
        throw std::runtime_error("Real-device soak failed");
      std::cout << "PASS " << seconds
                << "-second Core Audio/AU soak, 127 channels, concurrent edits/audition: " << measured.callbacks
                << " callbacks, " << measured.maxMicros << " us maximum, " << measured.overruns << " overruns\n";
      std::cout << "Callback duration p99.9 <= " << measured.p999Micros << " us (10 us histogram buckets)\n";
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
