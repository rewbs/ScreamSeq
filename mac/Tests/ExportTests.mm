#include "../Audio/AudioExport.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Tracker;
static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
static std::vector<float> wave(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  auto length = file.tellg();
  require(length >= 44, "WAV header");
  file.seekg(0);
  std::array<char, 44> header{};
  file.read(header.data(), 44);
  require(std::memcmp(header.data(), "RIFF", 4) == 0 && std::memcmp(header.data() + 8, "WAVE", 4) == 0,
          "RIFF/WAVE signature");
  auto value = [&](int offset) {
    uint32_t v = 0;
    for (int n = 0; n < 4; ++n)
      v |= uint32_t(uint8_t(header[offset + n])) << (n * 8);
    return v;
  };
  require(value(24) == 48000 && value(40) == uint64_t(length) - 44 && value(4) == uint64_t(length) - 8,
          "WAV lengths and rate");
  std::vector<float> output(size_t(length - std::streamoff(44)) / 4);
  file.read(reinterpret_cast<char *>(output.data()), output.size() * 4);
  for (auto sample : output)
    require(std::isfinite(sample), "finite exported audio");
  return output;
}
int main() {
  try {
    const auto directory = std::filesystem::temp_directory_path() / "resonance-tests";
    std::filesystem::create_directories(directory);
    auto document = Document::demo();
    const auto module = document->serialize();
    exportProjectAudio(module, {}, {}, (directory / "dry.wav").string());
    auto dry = wave(directory / "dry.wav");
    Renderer reference(module, 48000);
    std::array<float, 1024> buffer{};
    size_t offset = 0;
    while (auto frames = reference.render(buffer.data(), 512)) {
      require(offset + frames * 2 <= dry.size(), "export frame count");
      for (size_t i = 0; i < frames * 2; ++i)
        require(buffer[i] == dry[offset + i], "dry WAV matches core sample for sample");
      offset += frames * 2;
    }
    require(offset == dry.size(), "dry export has no truncation or padding");
    for (auto subtype : {kAudioUnitSubType_PeakLimiter, kAudioUnitSubType_Delay}) {
      PluginState state{
          {kAudioUnitType_Effect, subtype, kAudioUnitManufacturer_Apple, "Export qualification"}, {}, false};
      NativePlugin effect(state, 48000, true);
      const uint64_t latency = uint64_t(std::ceil(effect.latency() * 48000));
      const uint64_t tail = uint64_t(std::ceil(effect.tail() * 48000));
      const auto path = directory / (std::to_string(subtype) + ".wav");
      exportProjectAudio(module, {state}, {}, path.string());
      auto actual = wave(path);
      require(actual.size() == dry.size() + tail * 2, "latency trimmed and complete reported tail retained");
      // Independently feed the AU using 256-frame blocks, then trim its advertised
      // latency. This catches export truncation, leading delay and tail padding errors.
      std::vector<float> expected;
      for (uint64_t start = 0; start < dry.size() / 2 + latency + tail; start += 256) {
        uint32_t frames = uint32_t(std::min<uint64_t>(256, dry.size() / 2 + latency + tail - start));
        buffer.fill(0);
        for (uint32_t n = 0; n < frames * 2 && start * 2 + n < dry.size(); ++n)
          buffer[n] = dry[start * 2 + n];
        require(effect.process(buffer.data(), frames, start), "independent effect render");
        for (uint32_t n = 0; n < frames; ++n)
          if (start + n >= latency) {
            expected.push_back(buffer[n * 2]);
            expected.push_back(buffer[n * 2 + 1]);
          }
      }
      require(expected.size() == actual.size(), "independent export frame count");
      float difference = 0;
      for (size_t n = 0; n < actual.size(); ++n)
        difference = std::max(difference, std::abs(actual[n] - expected[n]));
      require(difference < 2e-6f, "export matches independently scheduled AU output");
      std::cout << "PASS AU export " << subtype << ": " << latency << " latency frames trimmed, " << tail
                << " tail frames, max difference " << difference << '\n';
    }
    const auto destination = directory / "retain-on-failure.wav";
    {
      std::ofstream original(destination);
      original << "keep existing audio";
    }
    bool rejected = false;
    try {
      exportProjectAudio({}, {}, {}, destination.string());
    } catch (...) {
      rejected = true;
    }
    std::ifstream retained(destination);
    std::string contents((std::istreambuf_iterator<char>(retained)), {});
    require(rejected && contents == "keep existing audio", "failed export retains original destination");
    std::cout << "PASS dry WAV fidelity, exact export duration, latency, tail and failed-export preservation\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
