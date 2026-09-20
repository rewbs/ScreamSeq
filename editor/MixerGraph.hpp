#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Tracker {
enum class MixerBusKind : uint8_t { Track, Group, Return, Master };
struct MixerSend {
  uint64_t target = 0;
  double gainDB = -12;
  bool preFader = false, enabled = true;
  bool operator==(const MixerSend &) const = default;
};
struct MixerBus {
  uint64_t id = 0, output = 0;
  MixerBusKind kind = MixerBusKind::Track;
  std::string name;
  uint32_t color = 0;
  double preGainDB = 0, gainDB = 0, pan = 0, width = 1, timingMS = 0;
  bool mute = false, solo = false;
  std::vector<std::string> inserts; // Stable plugin instance IDs in processing order.
  std::vector<MixerSend> sends;
  double prePan = 0; // Stereo balance before inserts; pan is after inserts/fader.
  bool operator==(const MixerBus &) const = default;
};
struct MixerInstrumentOutput {
  std::string plugin;
  uint64_t target = 0;
  uint32_t output = 0;
  bool operator==(const MixerInstrumentOutput &) const = default;
};
struct MixerSidechain {
  uint64_t source = 0;
  std::string plugin;
  uint32_t input = 1;
  double gainDB = 0;
  bool preFader = false, enabled = true;
  bool operator==(const MixerSidechain &) const = default;
};
struct MixerGraph {
  std::vector<MixerBus> buses;
  std::vector<MixerInstrumentOutput> instruments;
  std::vector<MixerSidechain> sidechains;
  // An empty graph uses the legacy, reference-qualified master-rack path.
  bool active() const { return !buses.empty(); }
  bool operator==(const MixerGraph &) const = default;
  size_t bytes() const;
  // Track bus IDs are the song's stable track IDs. All additional buses have
  // fresh song identities. Returns a deterministic processing order.
  std::vector<size_t> validate(const std::vector<uint64_t> &tracks) const;
};
struct MixerProcessorInfo {
  std::string instance;
  uint32_t latency = 0;
  double tail = 0;
  bool instrument = false, bypass = false;
  uint32_t outputBuses = 1;
  uint64_t activeOutputs = UINT64_MAX;
  uint64_t activeInputs = 0; // Auxiliary input bits only; main input belongs to the insert chain.
};
struct MixerConnection {
  size_t source = 0, target = 0;
  bool preFader = false;
  double gain = 1;
  uint32_t delay = 0;
};
struct MixerNodePlan {
  size_t bus = 0;
  std::vector<size_t> processors, outputs;
  std::vector<size_t> sidechains;
  uint32_t directDelay = 0, inputLatency = 0, outputLatency = 0;
  bool audible = true;
};
struct MixerInstrumentPlan {
  size_t processor = 0, target = 0;
  uint32_t output = 0, delay = 0;
  size_t owner = SIZE_MAX; // Effect insert owner; unowned entries are instruments.
  uint32_t prefixLatency = 0;
};
struct MixerSidechainPlan {
  size_t source = 0, target = 0, processor = 0;
  uint32_t input = 1, prefixLatency = 0, delay = 0;
  double gain = 1;
  bool preFader = false;
};
struct MixerPlan {
  std::vector<MixerNodePlan> nodes; // Indexed by bus, evaluated in order.
  std::vector<size_t> order;
  std::vector<MixerConnection> connections;
  std::vector<MixerInstrumentPlan> instruments;
  std::vector<MixerSidechainPlan> sidechains;
  size_t master = 0;
  uint32_t latency = 0;
  double tail = 0;
};
MixerPlan compileMixer(const MixerGraph &, const std::vector<uint64_t> &tracks,
                       const std::vector<MixerProcessorInfo> &, uint32_t sampleRate);
} // namespace Tracker
