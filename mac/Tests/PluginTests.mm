#include "../Audio/AudioUnitHost.hpp"
#include <cmath>
#include <iostream>
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
int main() {
  try {
    for (auto subtype :
         {kAudioUnitSubType_LowPassFilter, kAudioUnitSubType_HighPassFilter, kAudioUnitSubType_PeakLimiter}) {
      Tracker::PluginState initial{
          {kAudioUnitType_Effect, subtype, kAudioUnitManufacturer_Apple, "Apple test effect"}, {}, false};
      Tracker::NativePlugin configured(initial, 48000);
      auto parameters = configured.parameters();
      if (parameters.empty())
        throw std::runtime_error("Missing parameter controls");
      auto parameter = parameters.front();
      auto value = (parameter.min + parameter.max) * 0.4f;
      if (!configured.parameter(parameter.id, value))
        throw std::runtime_error("Parameter rejected");
      auto state = configured.state();
      Tracker::NativePlugin recalled(state, 48000);
      auto again = recalled.parameters();
      if (std::abs(again.front().value - value) > 0.01f)
        throw std::runtime_error("Parameter state recall differs");
      Tracker::PluginChain a({state}, 48000), b({state}, 48000, true);
      std::array<float, 512> x{}, y{};
      double energy = 0;
      for (int block = 0; block < 300; ++block) {
        for (size_t i = 0; i < x.size(); ++i)
          x[i] = y[i] = 0.1f * std::sin(double(block * 256 + i / 2) * 0.12);
        uint64_t allocations, releases, locks;
        tracker_audit_begin();
        const auto rendered = a.process(x.data(), 256) && b.process(y.data(), 256);
        tracker_audit_end(&allocations, &releases, &locks);
        if (!rendered)
          throw std::runtime_error("Render failed");
        if (allocations || releases || locks)
          throw std::runtime_error(
              "Qualified AU performed realtime allocation or locking: " + std::to_string(allocations) + "/" +
              std::to_string(releases) + "/" + std::to_string(locks));
        for (size_t i = 0; i < x.size(); ++i) {
          if (std::abs(x[i] - y[i]) > 1e-6)
            throw std::runtime_error("Live/offline effect mismatch");
          energy += x[i] * x[i];
        }
      }
      if (energy <= 0.001)
        throw std::runtime_error("Silent effect output");
      std::cout << "PASS Apple AU subtype " << subtype
                << ": state recall, stereo render, live/offline parity, finite output, latency " << a.latency()
                << " s\n";
    }
    Tracker::PluginState lowpass{
        {kAudioUnitType_Effect, kAudioUnitSubType_LowPassFilter, kAudioUnitManufacturer_Apple, "Automation test"},
        {},
        false};
    std::vector<Tracker::ParameterChange> automation{
        {0, 0, 200, 37}, {0, 0, 9000, 113}, {0, 0, 1200, 519}, {0, 0, 5000, 4096}};
    // A timeline must sound the same across device buffer sizes and offline renders.
    auto render = [&](uint32_t size, double rate) {
      Tracker::PluginChain chain({lowpass}, rate, true, automation);
      std::vector<float> result(8192 * 2);
      for (uint32_t start = 0; start < 8192; start += size) {
        uint32_t count = std::min(size, 8192 - start);
        for (uint32_t i = 0; i < count; ++i)
          result[(start + i) * 2] = result[(start + i) * 2 + 1] = 0.1f * std::sin((start + i) * 0.4);
        if (!chain.process(result.data() + start * 2, count))
          throw std::runtime_error("automation render failed");
      }
      return result;
    };
    for (double rate : {44100.0, 48000.0, 96000.0}) {
      auto narrow = render(128, rate), wide = render(512, rate);
      float delta = 0;
      for (size_t i = 0; i < narrow.size(); ++i)
        delta = std::max(delta, std::abs(narrow[i] - wide[i]));
      if (delta > 2e-6)
        throw std::runtime_error("automation depends on buffer size: " + std::to_string(delta));
      std::cout << "PASS sample-timed AU automation at " << rate << " Hz across 128/512-frame buffers\n";
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
