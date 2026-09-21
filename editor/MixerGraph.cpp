#include "MixerGraph.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Tracker {
namespace {
void require(bool condition, const char *message) { if (!condition) throw std::invalid_argument(message); }
bool range(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool text(const std::string &value, size_t maximum) { return value.size() <= maximum && value.find('\0') == std::string::npos; }
}
size_t MixerGraph::bytes() const {
  size_t result = sizeof(*this);
  for (const auto &bus : buses) {
    result += sizeof(bus) + bus.name.size() + bus.sends.size() * sizeof(MixerSend);
    for (const auto &insert : bus.inserts) result += sizeof(insert) + insert.size();
  }
  for (const auto &source : instruments) result += sizeof(source) + source.plugin.size();
  for (const auto &side : sidechains) result += sizeof(side) + side.plugin.size();
  return result;
}
std::vector<size_t> MixerGraph::validate(const std::vector<uint64_t> &tracks) const {
  if (buses.empty()) { require(instruments.empty() && sidechains.empty(), "Plugin routing requires a mixer graph"); return {}; }
  require(buses.size() <= 240, "Use at most 240 mixer buses");
  const std::set<uint64_t> knownTracks(tracks.begin(), tracks.end());
  require(knownTracks.size() == tracks.size() && !knownTracks.count(0), "Invalid track identities");
  std::set<uint64_t> foundTracks;
  std::map<uint64_t, size_t> indices;
  std::set<std::string> effects;
  size_t masters = 0, master = 0;
  std::map<std::string, size_t> owners;
  for (size_t i = 0; i < buses.size(); ++i) {
    const auto &bus = buses[i];
    require(bus.id && indices.emplace(bus.id, i).second, "Invalid or duplicate bus identity");
    require(uint8_t(bus.kind) <= uint8_t(MixerBusKind::Master), "Invalid bus kind");
    require(text(bus.name, 1024) && bus.color <= 0xffffff, "Invalid mixer label or color");
    require(range(bus.preGainDB, -96, 24) && range(bus.prePan, -1, 1) && range(bus.gainDB, -96, 24) && range(bus.pan, -1, 1) &&
            range(bus.width, 0, 2) && range(bus.timingMS, -500, 500), "Mixer value outside its range");
    require(bus.kind == MixerBusKind::Track || bus.timingMS == 0, "Timing offsets belong to tracks");
    if (bus.kind == MixerBusKind::Track) {
      require(knownTracks.count(bus.id), "Track bus does not match a song track"); foundTracks.insert(bus.id);
    } else require(!knownTracks.count(bus.id), "A group, return or master cannot reuse a track identity");
    if (bus.kind == MixerBusKind::Master) { ++masters; master = i; require(bus.output == 0 && bus.sends.empty(), "Master output cannot feed another bus"); }
    require(bus.inserts.size() <= 64 && bus.sends.size() <= 16, "Bus insert or send limit exceeded");
    for (const auto &plugin : bus.inserts)
      require(!plugin.empty() && text(plugin, 128) && effects.insert(plugin).second, "A plugin instance can have only one insert owner");
    for (const auto &plugin : bus.inserts) owners[plugin] = i;
  }
  require(masters == 1 && foundTracks == knownTracks, "Mixer requires one master and one bus for every track");
  std::vector<std::vector<size_t>> edges(buses.size());
  std::vector<size_t> indegree(buses.size());
  auto edge = [&](size_t source, uint64_t destination) {
    auto found = indices.find(destination);
    require(found != indices.end() && found->second != source, "Invalid mixer routing destination");
    require(buses[found->second].kind != MixerBusKind::Track, "Route buses to groups, returns or master");
    edges[source].push_back(found->second); ++indegree[found->second];
  };
  for (size_t i = 0; i < buses.size(); ++i) {
    const auto &bus = buses[i];
    if (bus.kind != MixerBusKind::Master && bus.output) edge(i, bus.output);
    std::set<uint64_t> destinations;
    for (const auto &send : bus.sends) {
      require(range(send.gainDB, -96, 12) && destinations.insert(send.target).second, "Invalid or duplicate send");
      // Disabled edges are checked too: switching one on must never introduce
      // hidden feedback into an otherwise valid saved graph.
      edge(i, send.target);
    }
  }
  std::set<std::pair<std::string, uint32_t>> sources;
  require(instruments.size() <= 128, "Too many instrument output routes");
  for (const auto &source : instruments) {
    require(!source.plugin.empty() && text(source.plugin, 128) && source.output < 64 && (!source.target || indices.count(source.target)) &&
            sources.emplace(source.plugin, source.output).second, "Invalid plugin output route");
    if(effects.count(source.plugin)) {
      require(source.output>0,"Main effect output belongs to its insert chain; route an auxiliary output");
      if(!source.target) continue;
      const auto owner=owners.at(source.plugin),target=indices.at(source.target);
      require(owner!=target,"An auxiliary effect output cannot return to its own bus");
      require(buses[target].kind!=MixerBusKind::Track,"Route effect auxiliary outputs to groups, returns or master");
      edges[owner].push_back(target);++indegree[target];
    }
  }
  require(sidechains.size() <= 128, "Use at most 128 sidechain routes");
  std::set<std::tuple<uint64_t, std::string, uint32_t>> sideSources;
  for (const auto &side : sidechains) {
    require(indices.count(side.source) && !side.plugin.empty() && text(side.plugin, 128) && side.input > 0 && side.input < 64 &&
            range(side.gainDB, -96, 12) && sideSources.emplace(side.source, side.plugin, side.input).second, "Invalid or duplicate sidechain route");
    require(owners.count(side.plugin)||std::none_of(instruments.begin(), instruments.end(), [&](const auto &s) { return s.plugin == side.plugin; }), "Sidechains target effect inserts");
    const auto source = indices.at(side.source), target = owners.count(side.plugin) ? owners.at(side.plugin) : master;
    require(source != target, "A sidechain cannot feed an effect on its own source bus");
    // Include disabled and unavailable routes so restoring a plugin or enabling
    // a key input cannot introduce an unvalidated feedback path.
    edges[source].push_back(target); ++indegree[target];
  }
  std::vector<size_t> order;
  // Stable bus order makes summing and rendered fixtures deterministic.
  std::set<size_t> ready;
  for (size_t i = 0; i < buses.size(); ++i) if (!indegree[i]) ready.insert(i);
  while (!ready.empty()) {
    const size_t i = *ready.begin(); ready.erase(ready.begin()); order.push_back(i);
    for (const auto target : edges[i]) if (!--indegree[target]) ready.insert(target);
  }
  require(order.size() == buses.size(), "Mixer feedback cycles are not supported");
  return order;
}
MixerPlan compileMixer(const MixerGraph &graph, const std::vector<uint64_t> &tracks,
                       const std::vector<MixerProcessorInfo> &processors, uint32_t rate) {
  require(rate >= 8000 && rate <= 384000, "Invalid mixer sample rate");
  MixerPlan plan;
  plan.order = graph.validate(tracks);
  if (!graph.active()) return plan;
  std::map<uint64_t, size_t> indices;
  std::map<std::string, size_t> pluginIndices;
  std::set<size_t> assigned;
  for (size_t i = 0; i < processors.size(); ++i) {
    const auto &p = processors[i];
    require(!p.instance.empty() && pluginIndices.emplace(p.instance, i).second && p.latency <= rate * 10 && range(p.tail, 0, 60) && p.outputBuses >= 1 && p.outputBuses <= 64 && (p.activeOutputs & 1),
            "Invalid mixer processor description");
  }
  plan.nodes.resize(graph.buses.size());
  std::vector<double> tails(graph.buses.size());
  double earliestMS = 0, latestMS = 0;
  for (size_t i = 0; i < graph.buses.size(); ++i) {
    const auto &bus = graph.buses[i]; indices[bus.id] = i; plan.nodes[i].bus = i;
    if (bus.kind == MixerBusKind::Master) plan.master = i;
    earliestMS = std::min(earliestMS, bus.timingMS); latestMS = std::max(latestMS, bus.timingMS);
    for (const auto &id : bus.inserts) {
      auto found = pluginIndices.find(id);
      if (found == pluginIndices.end()) continue; // Unavailable instances remain in the saved graph.
      require(!processors[found->second].instrument, "Instruments are sources, not effect inserts");
      assigned.insert(found->second);
      if (!processors[found->second].bypass) plan.nodes[i].processors.push_back(found->second);
    }
  }
  // Preserve the existing Add Plugin behavior: effects without an explicit bus
  // owner process on the master, in rack order.
  for (size_t i = 0; i < processors.size(); ++i)
    if (!processors[i].instrument && !processors[i].bypass && !assigned.count(i)) plan.nodes[plan.master].processors.push_back(i);
  for (const auto &side : graph.sidechains) {
    auto found = pluginIndices.find(side.plugin); if (found == pluginIndices.end()) continue;
    const auto processor = found->second; const auto &p = processors[processor];
    require(!p.instrument, "Sidechains target effect plugins");
    if (!side.enabled || p.bypass || !(p.activeInputs & (uint64_t(1) << side.input))) continue;
    for (const auto &node : plan.nodes) {
      uint64_t prefix = 0;
      for (auto insert : node.processors) {
        if (insert == processor) {
          require(prefix <= uint64_t(rate) * 30, "Sidechain insert latency exceeds 30 seconds");
          const auto source = indices.at(side.source), index = plan.sidechains.size();
          plan.sidechains.push_back({source, node.bus, processor, side.input, uint32_t(prefix), 0, std::pow(10.0, side.gainDB / 20), side.preFader});
          plan.nodes[source].sidechains.push_back(index);
          break;
        }
        prefix += processors[insert].latency;
      }
    }
  }
  auto connect = [&](size_t source, uint64_t target, double gain, bool pre) {
    const size_t connection = plan.connections.size();
    plan.connections.push_back({source, indices.at(target), pre, gain, 0});
    plan.nodes[source].outputs.push_back(connection);
  };
  for (size_t i = 0; i < graph.buses.size(); ++i) {
    const auto &bus = graph.buses[i];
    if (bus.kind != MixerBusKind::Master && bus.output) connect(i, bus.output, 1, false);
    for (const auto &send : bus.sends) if (send.enabled) connect(i, send.target, std::pow(10.0, send.gainDB / 20), send.preFader);
  }
  std::set<std::pair<size_t, uint32_t>> routedInstruments;
  for (const auto &source : graph.instruments) {
    auto found = pluginIndices.find(source.plugin);
    if (found == pluginIndices.end()) continue;
    const auto &p = processors[found->second];
    size_t owner=SIZE_MAX;uint32_t prefix=0;
    if(!p.instrument){require(assigned.contains(found->second),"Assign an effect insert owner before routing its auxiliary output");require(source.output>0,"Effect main output belongs to its bus insert chain");
      for(const auto &node:plan.nodes){uint64_t before=0;for(auto index:node.processors){before+=processors[index].latency;if(index==found->second){owner=node.bus;require(before<=uint64_t(rate)*30,"Auxiliary output latency exceeds 30 seconds");prefix=uint32_t(before);break;}}if(owner!=SIZE_MAX)break;}
      if(owner==SIZE_MAX)continue; // A bypassed or unresolved insert has no auxiliary output.
    }
    routedInstruments.emplace(found->second, source.output);
    // Preserve saved routes when an output is disabled or a restored plugin
    // exposes fewer ports. API edits require an available, enabled output.
    if (source.target && !p.bypass && source.output < p.outputBuses && (p.activeOutputs & (uint64_t(1) << source.output)))
      plan.instruments.push_back({found->second, indices.at(source.target), source.output, 0, owner, prefix});
  }
  for (size_t i = 0; i < processors.size(); ++i)
    if (processors[i].instrument && !processors[i].bypass && !routedInstruments.count({i, 0})) plan.instruments.push_back({i, plan.master, 0, 0});
  for (const auto &source : plan.instruments) {
    if(source.owner!=SIZE_MAX)continue;
    auto &node = plan.nodes[source.target];
    node.inputLatency = std::max(node.inputLatency, processors[source.processor].latency);
    tails[source.target] = std::max(tails[source.target], processors[source.processor].tail);
  }
  const uint32_t lead = uint32_t(std::llround(-earliestMS * rate / 1000));
  for (size_t i : plan.order) {
    auto &node = plan.nodes[i];
    uint64_t latency = node.inputLatency;
    for (const auto plugin : node.processors) { latency += processors[plugin].latency; tails[i] += processors[plugin].tail; }
    require(latency <= uint64_t(rate) * 30, "Mixer path latency exceeds 30 seconds");
    node.outputLatency = uint32_t(latency);
    node.directDelay = uint32_t(int64_t(node.inputLatency) + lead + std::llround(graph.buses[i].timingMS * rate / 1000));
    for (auto connection : node.outputs) {
      auto &target = plan.nodes[plan.connections[connection].target];
      target.inputLatency = std::max(target.inputLatency, node.outputLatency);
      tails[target.bus] = std::max(tails[target.bus], tails[i]);
    }
    for(const auto &source:plan.instruments)if(source.owner==i){auto &target=plan.nodes[source.target];target.inputLatency=std::max(target.inputLatency,node.inputLatency+source.prefixLatency);tails[target.bus]=std::max(tails[target.bus],tails[i]);}
    for (auto index : node.sidechains) {
      const auto &side = plan.sidechains[index]; auto &target = plan.nodes[side.target];
      const uint32_t required = node.outputLatency > side.prefixLatency ? node.outputLatency - side.prefixLatency : 0;
      target.inputLatency = std::max(target.inputLatency, required);
      tails[target.bus] = std::max(tails[target.bus], tails[i]);
    }
  }
  for (auto &edge : plan.connections) edge.delay = plan.nodes[edge.target].inputLatency - plan.nodes[edge.source].outputLatency;
  for (auto &source : plan.instruments) {
    if(source.owner==SIZE_MAX)source.delay = uint32_t(int64_t(plan.nodes[source.target].inputLatency) - processors[source.processor].latency + lead +
                           std::llround(graph.buses[source.target].timingMS * rate / 1000));
    else source.delay=plan.nodes[source.target].inputLatency-plan.nodes[source.owner].inputLatency-source.prefixLatency;
  }
  for (auto &side : plan.sidechains)
    side.delay = plan.nodes[side.target].inputLatency + side.prefixLatency - plan.nodes[side.source].outputLatency;
  std::set<size_t> solo, audible;
  for (size_t i = 0; i < graph.buses.size(); ++i) if (graph.buses[i].solo) solo.insert(i);
  if (!solo.empty()) {
    audible = solo;
    bool changed = true;
    while (changed) { changed = false; for (const auto &edge : plan.connections)
      if (audible.count(edge.target)) changed |= audible.insert(edge.source).second; for(const auto &source:plan.instruments)if(source.owner!=SIZE_MAX&&audible.count(source.target))changed|=audible.insert(source.owner).second; }
    changed = true;
    while (changed) { changed = false; for (const auto &edge : plan.connections)
      if (audible.count(edge.source)) changed |= audible.insert(edge.target).second; for(const auto &source:plan.instruments)if(source.owner!=SIZE_MAX&&audible.count(source.owner))changed|=audible.insert(source.target).second; }
  }
  for (size_t i = 0; i < graph.buses.size(); ++i)
    plan.nodes[i].audible = !graph.buses[i].mute && (solo.empty() || audible.count(i));
  plan.latency = plan.nodes[plan.master].outputLatency + lead;
  plan.tail = std::min(60.0, tails[plan.master] + latestMS / 1000);
  // Bound delay-line memory before allocation (stereo float, including direct
  // sample inputs, edge compensation and separate instrument outputs).
  uint64_t delays = 0;
  for (const auto &node : plan.nodes) delays += node.directDelay;
  for (const auto &edge : plan.connections) delays += edge.delay;
  for (const auto &source : plan.instruments) delays += source.delay;
  for (const auto &side : plan.sidechains) delays += side.delay;
  require(delays * sizeof(float) * 2 <= 256 * 1024 * 1024, "Mixer compensation exceeds the 256 MB delay budget");
  return plan;
}
} // namespace Tracker
