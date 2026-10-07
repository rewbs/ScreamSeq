#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <span>
#include <vector>
#include "AudioPortTrim.hpp"

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
  AudioPortTrims portTrims; // Prepared from SignalGraph; not a second saved value.
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
struct MixerPluginConnection {
  std::string source;
  uint32_t output=0;
  std::string target;
  uint32_t input=0;
  double gainDB=0;
  bool enabled=true;
  bool operator==(const MixerPluginConnection &) const = default;
};
struct MixerDetachedChain {
  uint64_t id=0; // Stable document identity, projected as an internal silent root.
  std::vector<std::string> plugins;
  bool operator==(const MixerDetachedChain &) const = default;
};
struct MixerGraph {
  std::vector<MixerBus> buses;
  std::vector<MixerInstrumentOutput> instruments;
  std::vector<MixerSidechain> sidechains;
  // Explicitly unconnected effect instances. Other unowned rack effects keep
  // their normal Master fallback. Detached processors remain clocked on silence.
  std::vector<std::string> detached;
  std::vector<MixerDetachedChain> detachedChains;
  // Cuts suppress only these implicit main inputs, before explicit fan-in.
  // Upstream processors and their compensation/history remain warm.
  std::vector<std::string> disconnectedMainInputs;
  bool masterOutputDisconnected=false;
  // Exact post-processor output contribution, independent of serial ownership.
  std::vector<MixerPluginConnection> pluginConnections;
  // An empty graph uses the legacy, reference-qualified master-rack path.
  bool active() const { return !buses.empty(); }
  bool operator==(const MixerGraph &) const = default;
  size_t bytes() const;
  // Track bus IDs are the song's stable track IDs. All additional buses have
  // fresh song identities. Returns a deterministic processing order.
  std::vector<size_t> validate(const std::vector<uint64_t> &tracks) const;
};
// Shared control-thread projection only. Silent chain roots have neutral bus
// controls and no output; explicit auxiliary routes retain their identities.
// The persisted song never gains these internal buses.
void setMixerPluginConnection(MixerGraph &,const MixerPluginConnection &,const MixerPluginConnection *replace=nullptr);
void disconnectMixerInsert(MixerGraph &,const std::vector<std::string> &effectRack,uint64_t owner,const std::string &plugin);
void rootDetachedMixerPlugin(MixerGraph &,const std::string &plugin,const std::function<uint64_t()> &allocate);
MixerGraph projectMixerDetachedChains(const MixerGraph &);
std::vector<std::string> detachedMixerPlugins(const MixerGraph &);
// Pull a contiguous owner segment out, heal only its main path and retain all
// explicit branches. Allocation happens only when a new chain is required.
void detachMixerInserts(MixerGraph &,const std::vector<std::string> &effectRack,
                        const std::vector<std::string> &plugins,
                        const std::function<uint64_t()> &allocate);
// Longest tail one processor (a plugin or a whole bus graph) may report. Hosts
// clamp to this before describing a processor; compileMixer rejects more.
inline constexpr double mixerMaximumTailSeconds = 120;
struct MixerProcessorInfo {
  std::string instance;
  uint32_t latency = 0;
  double tail = 0;
  bool instrument = false, bypass = false;
  uint32_t outputBuses = 1;
  uint64_t activeOutputs = UINT64_MAX;
  uint64_t activeInputs = 0; // Prepared auxiliary capacity; main input belongs to the insert chain.
  uint64_t mainInputFallback = 0; // Missing detector input uses this processor's main input (native dynamics only).
  bool scheduledSource = false; // Real instrument endpoint: render in the dependency DAG, not before it.
};
struct MixerConnection {
  size_t source = 0, target = 0;
  bool preFader = false;
  double gain = 1;
  uint32_t delay = 0;
  bool send = false; // Observation identity; main output and send may share endpoints.
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
struct MixerPluginConnectionPlan {
  size_t source=0,target=0;
  uint32_t output=0,input=0,delay=0;
  double gain=1;
};
struct MixerProcessorPlan {
  size_t owner=SIZE_MAX,previous=SIZE_MAX;
  uint32_t inputLatency=0,outputLatency=0,mainDelay=0;
};
struct MixerExecutionStep {uint8_t kind=0;size_t bus=SIZE_MAX,processor=SIZE_MAX;};
// Prepared timing-only edges preserve a group's dry boundary when its chosen
// ingress is later than the wet egress. They carry no audio and are never saved.
struct MixerTimingPoint {std::string processor;uint64_t bus=0;bool input=false;bool operator==(const MixerTimingPoint &) const=default;};
struct MixerTimingConstraint {MixerTimingPoint source,target;bool operator==(const MixerTimingConstraint &) const=default;};
struct MixerPlan {
  std::vector<MixerNodePlan> nodes; // Indexed by bus, evaluated in order.
  std::vector<size_t> order;
  std::vector<size_t> detached; // Effects clocked on silence, with no audible output.
  std::vector<size_t> disconnectedMainInputs; // Suppress serial main feed, before explicit input-zero fan-in.
  std::vector<MixerConnection> connections;
  std::vector<MixerInstrumentPlan> instruments;
  std::vector<MixerSidechainPlan> sidechains;
  bool segmented=false;
  std::vector<MixerPluginConnectionPlan> pluginConnections;
  std::vector<MixerProcessorPlan> processors;
  std::vector<size_t> scheduledSources;
  std::vector<MixerTimingConstraint> timing;
  std::vector<MixerExecutionStep> execution;
  size_t master = 0;
  uint32_t latency = 0;
  double tail = 0;
};
// Move an ordered, contiguous insert segment as one edit. Unowned rack effects
// are resolved on Master exactly as in compileMixer. No processor is recreated.
void moveMixerInserts(MixerGraph &, const std::vector<std::string> &effectRack,
                      const std::vector<std::string> &plugins, uint64_t target,
                      const std::string &before = {});
// Pull one effect out of its implicit serial main path, healing that path.
// Explicit auxiliary routes are ambiguous and must be disconnected first.
// No processor/state/binding is removed; the detached effect clocks silence.
void detachMixerInsert(MixerGraph &, const std::vector<std::string> &effectRack,
                       const std::string &plugin);
MixerPlan compileMixer(const MixerGraph &, const std::vector<uint64_t> &tracks,
                       const std::vector<MixerProcessorInfo> &, uint32_t sampleRate, std::span<const MixerTimingConstraint> timing={});
// Control-thread dependency comparison for a prepared live transition. Each
// destination processor maps to an old processor slot only if every main and
// auxiliary input has the same expression, including gain, summing order and
// delay compensation. SIZE_MAX requires a separate prepared processor copy.
// This proves signal equivalence, not mutable runtime-state transfer: a render
// executor must also retain delay/fader histories and cache shared DSP output.
// Explicitly reset identities cover changed recipes/state/assignment semantics.
std::vector<size_t> reusableMixerProcessors(
    const MixerGraph &before, const MixerPlan &beforePlan, const std::vector<MixerProcessorInfo> &beforeProcessors,
    const MixerGraph &after, const MixerPlan &afterPlan, const std::vector<MixerProcessorInfo> &afterProcessors,
    const std::vector<std::string> &reset = {});
// Destination-to-source indices for histories whose complete input expression
// is unchanged. Delay gain is intentionally excluded: it is applied on read,
// after the stored samples. Controls include all smoothed bus values.
struct MixerTransitionReuse {
  std::vector<size_t> processors, direct, connections, instruments, sidechains, controls;
};
MixerTransitionReuse mixerTransitionReuse(
    const MixerGraph &before, const MixerPlan &beforePlan, const std::vector<MixerProcessorInfo> &beforeProcessors,
    const MixerGraph &after, const MixerPlan &afterPlan, const std::vector<MixerProcessorInfo> &afterProcessors,
    const std::vector<std::string> &reset = {});
} // namespace Tracker
