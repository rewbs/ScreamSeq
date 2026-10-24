#include "AudioExportStream.hpp"
#include "HostedAudio.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <ostream>

namespace Tracker {
uint64_t writeProjectAudio(const std::vector<std::byte> &module,const std::vector<PluginState> &states,
                          const std::vector<ParameterChange> &automation,std::ostream &output,
                          uint32_t sequence,const NativeSong *native,uint32_t blockFrames) {
  static_assert(std::endian::native==std::endian::little,"Native float WAV requires little-endian supported hosts");
  if(!blockFrames||blockFrames>4096)throw std::invalid_argument("Invalid offline audio export block size");
  if(output.tellp()!=std::streampos(0))throw std::invalid_argument("Audio export requires a fresh seekable stream");
  constexpr uint32_t rate = audioExportRate;
  // Export runs on document workers with small stacks. These own large fixed
  // audio buffers, so allocate them on the heap while retaining destruction
  // order: renderer adapters are retired before their hosted processors.
  auto effectsStorage = std::make_unique<PluginChain>(states, rate, true, automation);
  auto &effects = *effectsStorage;
  auto rendererStorage = std::make_unique<Renderer>(module, rate, 0, false, std::string{}, sequence, PlaybackRegion{}, native);
  auto &renderer = *rendererStorage;
  if (native) renderer.applyColumnMutes(*native, renderer.song());
  effects.attachInstruments(renderer, native);
  if (native) effects.attachMusicalAutomation(renderer, *native);
  const auto latencySeconds=effects.latency(),tailSeconds=effects.tail();
  if(!std::isfinite(latencySeconds)||latencySeconds<0||latencySeconds>60||!std::isfinite(tailSeconds)||tailSeconds<0)
    throw std::runtime_error("Plugin latency or tail is outside the audio export limit");
  const uint64_t latency = uint64_t(std::ceil(latencySeconds * rate)),
                 tail = uint64_t(std::ceil(std::min(tailSeconds, 60.0) * rate));
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
  if(!output)throw std::runtime_error("Audio export header could not be written");
  std::vector<float> buffer(size_t(blockFrames)*2);
  uint64_t position = 0, written = 0, end = UINT64_MAX, tailLimit = UINT64_MAX, tailRevision = effects.tailRevision();
  bool ended = false;
  while (position < end) {
    if (effects.latencyChangePending())
      throw std::runtime_error("A plugin changed latency during export; the incomplete export was not saved.");
    if (position >= uint64_t(rate) * 3600)
      throw std::runtime_error("Audio export exceeded the one-hour limit; output was not replaced.");
    uint32_t frames = blockFrames;
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
      std::fill(buffer.begin(),buffer.end(),0.f);
    }
    if (!frames)
      break;
    if (!effects.process(buffer.data(), frames))
      throw std::runtime_error("A hosted processor failed during audio export.");
    if (effects.latencyChangePending())
      throw std::runtime_error("A plugin changed latency during export; the incomplete export was not saved.");
    const auto revision = effects.tailRevision();
    if (ended && revision != tailRevision)
      end = std::max(end, std::min(tailLimit, position + frames + latency + tail));
    tailRevision = revision;
    auto skip = uint32_t(std::min<uint64_t>(frames, position < latency ? latency - position : 0));
    output.write(reinterpret_cast<const char *>(buffer.data() + skip * 2), (frames - skip) * 8);
    if(!output)throw std::runtime_error("Audio export could not be written; staging output is incomplete");
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
  return written;
}
}
