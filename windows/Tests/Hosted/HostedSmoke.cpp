#include "editor/hosted/HostedAudio.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main() { try {
  auto descriptors = NativePlugin::builtins();
  check(!descriptors.empty(), "Shared hosted facade must expose built-in effects");
  PluginState gain{descriptors.front()}; gain.instanceID = "hosted-smoke";
  for (uint32_t rate : {44100u, 48000u, 96000u}) {
    auto doc = Document::demo();
    auto render = [&](uint32_t block, bool wet) {
      // Adapters borrow the chain: destroy/reset the renderer first, always.
      auto chain = std::make_unique<PluginChain>(wet ? std::vector<PluginState>{gain} : std::vector<PluginState>{}, rate, true);
      auto renderer = std::make_unique<Renderer>(doc->snapshotData(), rate);
      chain->attachInstruments(*renderer); chain->attachMusicalAutomation(*renderer, doc->native());
      if (wet) check(chain->parameter(0, 1, -12), "Queue native dB gain");
      std::vector<float> audio(rate * 2);
      for (uint32_t pos = 0; pos < rate; pos += block) {
        auto count = std::min(block, rate - pos);
        renderer->render(audio.data() + pos * 2, count);
        check(chain->process(audio.data() + pos * 2, count) && !renderer->faulted(), "Offline hosted render");
      }
      renderer.reset();
      return audio;
    };
    auto dry = render(128, false), reference = render(128, true);
    double energy = 0, difference = 0, partition = 0;
    for (size_t i = 0; i < dry.size(); ++i) {
      check(std::isfinite(reference[i]), "Finite built-in PCM");
      energy += std::abs(reference[i]); difference += std::abs(reference[i] - dry[i]);
    }
    check(energy > 1 && difference > 1, "Built-in rack must audibly alter real renderer PCM");
    for (uint32_t block : {17u, 512u, 4096u}) {
      auto other = render(block, true);
      for (size_t i = 0; i < other.size(); ++i) partition = std::max(partition, std::abs(double(other[i]) - reference[i]));
    }
    check(partition < 1e-6, "Hosted PCM partition tolerance");
    std::cout << "rate=" << rate << " energy=" << energy << " wet-dry-L1=" << difference << " partition-max=" << partition << '\n';
  }
  std::cout << "PASS offline built-in rack (functional only; no realtime audit)\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
