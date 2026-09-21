#include "AudioExport.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace Tracker {
void exportProjectAudio(const std::vector<std::byte> &module, const std::vector<PluginState> &states,
                        const std::vector<ParameterChange> &automation, const std::string &path, uint32_t sequence,
                        const NativeSong *native) {
  constexpr uint32_t rate = 48000;
  PluginChain effects(states, rate, true, automation);
  Renderer renderer(module, rate, 0, false, {}, sequence, {}, native);
  if (native) renderer.applyColumnMutes(*native, renderer.song());
  effects.attachInstruments(renderer, native);
  if (native) effects.attachMusicalAutomation(renderer, *native);
  const uint64_t latency = uint64_t(std::ceil(effects.latency() * rate)),
                 tail = uint64_t(std::ceil(std::min(effects.tail(), 60.0) * rate));
  std::string temporary = path + ".writing.XXXXXX";
  int fd = mkstemp(temporary.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create export file");
  close(fd);
  try {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    auto u16 = [&](uint16_t value) {
      char bytes[2] = {char(value), char(value >> 8)};
      output.write(bytes, 2);
    };
    auto u32 = [&](uint32_t value) {
      char bytes[4] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24)};
      output.write(bytes, 4);
    };
    output.write("RIFF", 4);
    u32(0);
    output.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(2);
    u32(rate);
    u32(rate * 8);
    u16(8);
    u16(32);
    output.write("data", 4);
    u32(0);
    std::array<float, 1024> buffer{};
    uint64_t position = 0, written = 0, end = UINT64_MAX, tailLimit = UINT64_MAX, tailRevision = effects.tailRevision();
    bool ended = false;
    while (position < end) {
      if (effects.latencyChangePending())
        throw std::runtime_error("A plugin changed latency during export; the incomplete export was not saved.");
      if (position >= uint64_t(rate) * 3600)
        throw std::runtime_error("Audio export exceeded the one-hour limit; output was not replaced.");
      uint32_t frames = 512;
      effects.syncTransport(renderer);
      if (!ended) {
        auto received = renderer.render(buffer.data(), frames);
        if (renderer.faulted())
          throw std::runtime_error("Audio export exceeded the engine's loop-state capacity.");
        if (received < frames) {
          ended = true;
          effects.endNotes();
          end = position + received + latency + tail;
          tailLimit = position + received + latency + uint64_t(60) * rate;
          frames = uint32_t(std::min<uint64_t>(frames, end - position));
        }
      } else {
        frames = uint32_t(std::min<uint64_t>(frames, end - position));
        buffer.fill(0);
      }
      if (!frames)
        break;
      if (!effects.process(buffer.data(), frames))
        throw std::runtime_error("An Audio Unit failed during export.");
      if (effects.latencyChangePending())
        throw std::runtime_error("A plugin changed latency during export; the incomplete export was not saved.");
      const auto revision = effects.tailRevision();
      if (ended && revision != tailRevision)
        end = std::max(end, std::min(tailLimit, position + frames + latency + tail));
      tailRevision = revision;
      auto skip = uint32_t(std::min<uint64_t>(frames, position < latency ? latency - position : 0));
      output.write(reinterpret_cast<const char *>(buffer.data() + skip * 2), (frames - skip) * 8);
      written += frames - skip;
      position += frames;
    }
    output.seekp(4);
    u32(uint32_t(36 + written * 8));
    output.seekp(40);
    u32(uint32_t(written * 8));
    output.flush();
    if (!output)
      throw std::runtime_error("Audio export could not be written");
    output.close();
    if (!output)
      throw std::runtime_error("Audio export could not be closed");
    int persisted = ::open(temporary.c_str(), O_RDONLY);
    if (persisted < 0)
      throw std::runtime_error("Audio export could not be reopened for flushing");
    const int flushed = fsync(persisted);
    const int closed = close(persisted);
    if (flushed || closed)
      throw std::runtime_error("Audio export could not be flushed; original retained");
    std::filesystem::rename(temporary, path);
  } catch (...) {
    unlink(temporary.c_str());
    throw;
  }
}
} // namespace Tracker
