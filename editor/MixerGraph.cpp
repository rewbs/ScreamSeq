#include "MixerGraph.hpp"
#include "MixerPluginRouting.hpp"
#include <algorithm>
#include <bit>
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
std::vector<std::string> detachedMixerPlugins(const MixerGraph &graph) {
  auto result=graph.detached;for(const auto &chain:graph.detachedChains)result.insert(result.end(),chain.plugins.begin(),chain.plugins.end());return result;
}
MixerGraph projectMixerDetachedChains(const MixerGraph &graph) {
  auto result=graph;result.detachedChains.clear();
  for(const auto &chain:graph.detachedChains){MixerBus bus;bus.id=chain.id;bus.kind=MixerBusKind::Return;bus.inserts=chain.plugins;result.buses.push_back(std::move(bus));if(!chain.plugins.empty()&&std::find(result.disconnectedMainInputs.begin(),result.disconnectedMainInputs.end(),chain.plugins.front())==result.disconnectedMainInputs.end())result.disconnectedMainInputs.push_back(chain.plugins.front());}
  return result;
}
namespace {
void materializeFallback(MixerGraph &graph,const std::vector<std::string> &rack) {
  auto master=std::find_if(graph.buses.begin(),graph.buses.end(),[](const auto &b){return b.kind==MixerBusKind::Master;});require(master!=graph.buses.end(),"Mixer has no Master bus");
  const auto loose=detachedMixerPlugins(graph);std::set<std::string> owned(loose.begin(),loose.end());for(const auto &bus:graph.buses)owned.insert(bus.inserts.begin(),bus.inserts.end());
  for(const auto &id:rack)if(!owned.contains(id))master->inserts.push_back(id);
}
std::vector<std::string> *insertOwner(MixerGraph &graph,const std::string &plugin) {
  for(auto &bus:graph.buses)if(std::find(bus.inserts.begin(),bus.inserts.end(),plugin)!=bus.inserts.end())return &bus.inserts;
  for(auto &chain:graph.detachedChains)if(std::find(chain.plugins.begin(),chain.plugins.end(),plugin)!=chain.plugins.end())return &chain.plugins;
  return nullptr;
}
void pruneDetachedChains(MixerGraph &graph) {
  std::erase_if(graph.detachedChains,[](const auto &c){return c.plugins.empty();});
  for(const auto &chain:graph.detachedChains)std::erase(graph.disconnectedMainInputs,chain.plugins.front());
}
void validateMoving(const std::vector<std::string> &plugins,const std::vector<std::string> &rack){
  require(!plugins.empty()&&plugins.size()<=32,"Choose 1–32 consecutive effect plugins");std::set<std::string> selected;
  for(const auto &id:plugins)require(selected.insert(id).second&&std::find(rack.begin(),rack.end(),id)!=rack.end(),"Unknown or duplicate effect plugin");
}
void eraseInsertSegment(std::vector<std::string> &owner,const std::vector<std::string> &plugins) {
  auto start=std::find(owner.begin(),owner.end(),plugins.front());
  require(start!=owner.end()&&size_t(owner.end()-start)>=plugins.size()&&std::equal(plugins.begin(),plugins.end(),start),"Move a consecutive chain in its existing order");owner.erase(start,start+plugins.size());
}
}
void moveMixerInserts(MixerGraph &graph,const std::vector<std::string> &effectRack,
                      const std::vector<std::string> &plugins,uint64_t target,const std::string &before) {
  validateMoving(plugins,effectRack);auto next=graph;materializeFallback(next,effectRack);const auto original=next;
  std::vector<std::string> *destination=nullptr;
  for(auto &bus:next.buses)if(bus.id==target)destination=&bus.inserts;
  for(auto &chain:next.detachedChains)if(chain.id==target)destination=&chain.plugins;
  require(destination,"Insert destination does not exist");
  auto *source=insertOwner(next,plugins.front());const bool loose=plugins.size()==1&&std::find(next.detached.begin(),next.detached.end(),plugins.front())!=next.detached.end();
  require(source||loose,"Insert source does not exist");
  if(source){auto check=*source;eraseInsertSegment(check,plugins);}
  if(source==destination&&before==plugins.front()) {
    // Repatching the cut implicit input is a real edit even without a move.
    if(std::erase(next.disconnectedMainInputs,plugins.front()))graph=std::move(next);return;
  }
  require(before.empty()||std::find(plugins.begin(),plugins.end(),before)==plugins.end(),"Insertion point cannot be inside the moving chain");
  if(source)eraseInsertSegment(*source,plugins);else std::erase(next.detached,plugins.front());
  auto point=before.empty()?destination->end():std::find(destination->begin(),destination->end(),before);
  require(before.empty()||point!=destination->end(),"Insertion point is not on the destination chain");require(destination->size()+plugins.size()<=32,"Destination insert limit exceeded");
  destination->insert(point,plugins.begin(),plugins.end());std::erase(next.disconnectedMainInputs,plugins.front());
  // Inserting onto a cut edge explicitly reconnects that destination as well.
  if(!before.empty())std::erase(next.disconnectedMainInputs,before);
  pruneDetachedChains(next);
  if(next!=original)graph=std::move(next);
}
void detachMixerInserts(MixerGraph &graph,const std::vector<std::string> &effectRack,
                        const std::vector<std::string> &plugins,const std::function<uint64_t()> &allocate) {
  validateMoving(plugins,effectRack);
  if(plugins.size()==1&&std::find(graph.detached.begin(),graph.detached.end(),plugins.front())!=graph.detached.end())return;
  for(const auto &chain:graph.detachedChains)if(chain.plugins==plugins)return;
  auto next=graph;materializeFallback(next,effectRack);auto *owner=insertOwner(next,plugins.front());require(owner,"Insert source does not exist");eraseInsertSegment(*owner,plugins);
  const bool routed=std::any_of(next.pluginConnections.begin(),next.pluginConnections.end(),[&](const auto &r){return std::find(plugins.begin(),plugins.end(),r.source)!=plugins.end()||std::find(plugins.begin(),plugins.end(),r.target)!=plugins.end();})||std::any_of(next.sidechains.begin(),next.sidechains.end(),[&](const auto &r){return std::find(plugins.begin(),plugins.end(),r.plugin)!=plugins.end();})||std::any_of(next.instruments.begin(),next.instruments.end(),[&](const auto &r){return r.target&&std::find(plugins.begin(),plugins.end(),r.plugin)!=plugins.end();});
  if(plugins.size()==1&&!routed)next.detached.push_back(plugins.front());
  else {require(bool(allocate),"A detached chain needs a fresh document identity");const auto id=allocate();require(id&&std::none_of(next.buses.begin(),next.buses.end(),[&](const auto &b){return b.id==id;})&&std::none_of(next.detachedChains.begin(),next.detachedChains.end(),[&](const auto &c){return c.id==id;}),"Detached chain identity is already used");next.detachedChains.push_back({id,plugins});}
  // The new root is silent, so retaining a cut on its first member would not
  // describe an actual cable. Internal cuts stay exactly where they were.
  std::erase(next.disconnectedMainInputs,plugins.front());pruneDetachedChains(next);graph=std::move(next);
}
void setMixerPluginConnection(MixerGraph &graph,const MixerPluginConnection &value,const MixerPluginConnection *replace) {
  auto same=[](const auto &a,const auto &b){return a.source==b.source&&a.output==b.output&&a.target==b.target&&a.input==b.input;};
  require(!value.source.empty()&&!value.target.empty()&&value.source!=value.target&&text(value.source,128)&&text(value.target,128)&&value.output<64&&value.input<64&&range(value.gainDB,-96,12),"Invalid direct plugin connection");
  auto next=graph.pluginConnections;auto existing=std::find_if(next.begin(),next.end(),[&](const auto &r){return same(r,replace?*replace:value);});
  if(replace)require(existing!=next.end(),"Plugin connection no longer exists; refresh the graph");
  require(std::none_of(next.begin(),next.end(),[&](const auto &r){return same(r,value)&&(&r!=(existing==next.end()?nullptr:&*existing));}),"Plugin input already has this exact source/output");
  if(existing==next.end()){require(next.size()<256,"Use at most 256 direct plugin connections");next.push_back(value);}else *existing=value;
  graph.pluginConnections=std::move(next);
}
void disconnectMixerInsert(MixerGraph &graph,const std::vector<std::string> &rack,uint64_t owner,const std::string &plugin) {
  auto next=graph;materializeFallback(next,rack);const std::vector<std::string> *chain=nullptr;bool silentRoot=false;
  for(const auto &bus:next.buses)if(bus.id==owner)chain=&bus.inserts;
  for(const auto &loose:next.detachedChains)if(loose.id==owner){chain=&loose.plugins;silentRoot=true;}
  require(chain,"Insert cable owner no longer exists");auto at=std::find(chain->begin(),chain->end(),plugin);
  require(at!=chain->end()&&(!silentRoot||at!=chain->begin()),"Insert cable no longer exists");
  require(std::find(next.disconnectedMainInputs.begin(),next.disconnectedMainInputs.end(),plugin)==next.disconnectedMainInputs.end(),"Insert cable is already disconnected");
  next.disconnectedMainInputs.push_back(plugin);graph=std::move(next);
}
void rootDetachedMixerPlugin(MixerGraph &graph,const std::string &plugin,const std::function<uint64_t()> &allocate) {
  auto at=std::find(graph.detached.begin(),graph.detached.end(),plugin);if(at==graph.detached.end())return;
  require(bool(allocate),"A routed detached processor needs a fresh identity");const auto id=allocate();
  require(id&&std::none_of(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==id;})&&std::none_of(graph.detachedChains.begin(),graph.detachedChains.end(),[&](const auto &c){return c.id==id;}),"Detached chain identity is already used");
  graph.detachedChains.push_back({id,{plugin}});graph.detached.erase(at);
}

void detachMixerInsert(MixerGraph &graph,const std::vector<std::string> &effectRack,const std::string &plugin) {
  detachMixerInserts(graph,effectRack,{plugin},{});
}

size_t MixerGraph::bytes() const {
  size_t result = sizeof(*this);
  for (const auto &bus : buses) {
    result += sizeof(bus) + bus.name.size() + bus.sends.size() * sizeof(MixerSend);
    for (const auto &insert : bus.inserts) result += sizeof(insert) + insert.size();
  }
  for (const auto &source : instruments) result += sizeof(source) + source.plugin.size();
  for (const auto &side : sidechains) result += sizeof(side) + side.plugin.size();
  for (const auto &route : pluginConnections) result += sizeof(route)+route.source.size()+route.target.size();
  for(const auto &plugin:detached)result+=sizeof(plugin)+plugin.size();
  for(const auto &chain:detachedChains){result+=sizeof(chain);for(const auto &plugin:chain.plugins)result+=sizeof(plugin)+plugin.size();}
  for(const auto &plugin:disconnectedMainInputs)result+=sizeof(plugin)+plugin.size();
  return result;
}
std::vector<size_t> MixerGraph::validate(const std::vector<uint64_t> &tracks) const {
  require(detachedChains.size()<=64&&buses.size()+detachedChains.size()<=240,"Mixer and detached chain count exceeds 240 roots");
  std::set<std::string> cuts;require(disconnectedMainInputs.size()<=240,"Too many disconnected main inputs");for(const auto &id:disconnectedMainInputs)require(!id.empty()&&text(id,128)&&cuts.insert(id).second,"Invalid or duplicate disconnected main input");
  if(!detachedChains.empty()) {
    require(!buses.empty(),"Detached chains require the mixer projection");std::set<uint64_t> identities;std::set<std::string> members;
    for(const auto &chain:detachedChains){require(chain.id&&identities.insert(chain.id).second&&!chain.plugins.empty()&&chain.plugins.size()<=32,"Invalid detached chain identity or size");for(const auto &id:chain.plugins)require(!id.empty()&&text(id,128)&&members.insert(id).second,"Duplicate detached chain member");}
    return projectMixerDetachedChains(*this).validate(tracks);
  }
  require(!masterOutputDisconnected||!buses.empty(),"A disconnected Master output requires a mixer graph");
  std::set<std::string> loose;
  require(detached.size()<=240,"Use at most 240 unconnected processors");
  for(const auto &plugin:detached)require(!plugin.empty()&&text(plugin,128)&&loose.insert(plugin).second,"Invalid or duplicate unconnected effect identity");
  if (buses.empty()) { require(instruments.empty() && sidechains.empty() && pluginConnections.empty() && disconnectedMainInputs.empty(), "Plugin routing requires a mixer graph"); return {}; }
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
      require(!plugin.empty() && text(plugin, 128) && !loose.contains(plugin) && effects.insert(plugin).second, "A plugin instance can have only one insert owner and cannot also be unconnected");
    for (const auto &plugin : bus.inserts) owners[plugin] = i;
  }
  require(masters == 1 && foundTracks == knownTracks, "Mixer requires one master and one bus for every track");
  for(const auto &id:disconnectedMainInputs)require(effects.contains(id),"Disconnected main input must belong to an assigned effect");
  std::vector<std::vector<size_t>> edges(buses.size());
  std::vector<size_t> indegree(buses.size());
  auto edge = [&](size_t source, uint64_t destination) {
    auto found = indices.find(destination);
    require(found != indices.end() && found->second != source, "Invalid mixer routing destination");

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
  std::set<std::tuple<std::string, uint32_t, uint64_t>> sources;
  std::set<std::pair<std::string,uint32_t>> disconnected;
  require(instruments.size() <= 128, "Too many instrument output routes");
  for (const auto &source : instruments) {
    require(!source.plugin.empty() && text(source.plugin, 128) && source.output < 64 && (!source.target || indices.count(source.target)) &&
            sources.emplace(source.plugin, source.output, source.target).second, "Invalid plugin output route");
    if(!source.target) disconnected.emplace(source.plugin,source.output);
    if(effects.count(source.plugin)) {

      if(!source.target) continue;
      const auto owner=owners.at(source.plugin),target=indices.at(source.target);
      require(owner!=master,"Master is the final sink; move this effect to a channel or return before branching its output");
      require(owner!=target,"An effect output cannot return to its own bus");

      edges[owner].push_back(target);++indegree[target];
    }
  }
  for(const auto &source:instruments) require(!source.target || !disconnected.count({source.plugin,source.output}), "An output cannot be both connected and disconnected");
  require(sidechains.size() <= 128, "Use at most 128 sidechain routes");
  std::set<std::tuple<uint64_t, std::string, uint32_t>> sideSources;
  for (const auto &side : sidechains) {
    require(indices.count(side.source) && !side.plugin.empty() && text(side.plugin, 128) && side.input < 64 &&
            range(side.gainDB, -96, 12) && sideSources.emplace(side.source, side.plugin, side.input).second, "Invalid or duplicate sidechain route");
    if(!owners.count(side.plugin))continue; // Standalone instruments are ordered by the prepared processor DAG.
    const auto source = indices.at(side.source), target = owners.count(side.plugin) ? owners.at(side.plugin) : master;
    require(source!=master,"Master is the final sink and cannot feed another processor input");
    require(source != target, "A sidechain cannot feed an effect on its own source bus");
    // Include disabled and unavailable routes so restoring a plugin or enabling
    // a key input cannot introduce an unvalidated feedback path.
    edges[source].push_back(target); ++indegree[target];
  }
  require(pluginConnections.size()<=256,"Use at most 256 direct plugin connections");
  std::set<std::tuple<std::string,uint32_t,std::string,uint32_t>> pluginEndpoints;
  for(const auto &route:pluginConnections)require(!route.source.empty()&&!route.target.empty()&&route.source!=route.target&&text(route.source,128)&&text(route.target,128)&&route.output<64&&route.input<64&&range(route.gainDB,-96,12)&&pluginEndpoints.emplace(route.source,route.output,route.target,route.input).second,"Invalid or duplicate direct plugin connection");
  // Physical catalogue and processor dependency validation belong to compileMixer.
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
MixerPlan compileMixer(const MixerGraph &savedGraph, const std::vector<uint64_t> &tracks,
                       const std::vector<MixerProcessorInfo> &processors, uint32_t rate,std::span<const MixerTimingConstraint> timing) {
  const auto graph=projectMixerDetachedChains(savedGraph);
  require(rate >= 8000 && rate <= 384000, "Invalid mixer sample rate");
  MixerPlan plan;plan.timing.assign(timing.begin(),timing.end());
  plan.order = graph.validate(tracks);
  if (!graph.active()) return plan;
  std::map<uint64_t, size_t> indices;
  std::map<std::string, size_t> pluginIndices;
  std::set<size_t> assigned;
  for (size_t i = 0; i < processors.size(); ++i) {
    const auto &p = processors[i];
    if (!(!p.instance.empty() && pluginIndices.emplace(p.instance, i).second && p.latency <= rate * 10 && range(p.tail, 0, mixerMaximumTailSeconds) && p.outputBuses >= 1 && p.outputBuses <= 64 && (p.activeOutputs & 1)))
      throw std::invalid_argument("Invalid mixer processor description: " + (p.instance.empty() ? "processor " + std::to_string(i) : p.instance.substr(0, 128)));
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
      if(std::find(graph.disconnectedMainInputs.begin(),graph.disconnectedMainInputs.end(),id)!=graph.disconnectedMainInputs.end())plan.disconnectedMainInputs.push_back(found->second);
      if (!processors[found->second].bypass) plan.nodes[i].processors.push_back(found->second);
    }
  }
  std::set<size_t> detached;
  for(const auto &id:graph.detached)if(auto found=pluginIndices.find(id);found!=pluginIndices.end()) {
    const auto index=found->second;
    require(!processors[index].instrument,"Instrument sources cannot be detached effects");
    detached.insert(index);assigned.insert(index);
    if(!processors[index].bypass)plan.detached.push_back(index);
  }
  // Preserve the existing Add Plugin behavior: effects without an explicit bus
  // owner process on the master, in rack order.
  for (size_t i = 0; i < processors.size(); ++i)
    if (!processors[i].instrument && !processors[i].bypass && !assigned.count(i)) plan.nodes[plan.master].processors.push_back(i);
  for (const auto &side : graph.sidechains) {
    auto found = pluginIndices.find(side.plugin); if (found == pluginIndices.end()) continue;
    const auto processor = found->second; const auto &p = processors[processor];
    require(!p.instrument||p.scheduledSource,"Instrument input needs a prepared source stage");
    require(!p.instrument||(p.activeInputs&(uint64_t(1)<<side.input)),"Instrument input is absent from the prepared physical layout");
    require(!detached.contains(processor),"Insert an unconnected effect before routing audio into it");
    if (!side.enabled || p.bypass || (side.input && !(p.activeInputs & (uint64_t(1) << side.input)))) continue;
    if(p.scheduledSource){const auto source=indices.at(side.source),index=plan.sidechains.size();plan.sidechains.push_back({source,SIZE_MAX,processor,side.input,0,0,std::pow(10.,side.gainDB/20),side.preFader});plan.nodes[source].sidechains.push_back(index);continue;}
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
  auto connect = [&](size_t source, uint64_t target, double gain, bool pre, bool send=false) {
    const size_t connection = plan.connections.size();
    plan.connections.push_back({source, indices.at(target), pre, gain, 0, send});
    plan.nodes[source].outputs.push_back(connection);
  };
  for (size_t i = 0; i < graph.buses.size(); ++i) {
    const auto &bus = graph.buses[i];
    if (bus.kind != MixerBusKind::Master && bus.output) connect(i, bus.output, 1, false);
    for (const auto &send : bus.sends) if (send.enabled) connect(i, send.target, std::pow(10.0, send.gainDB / 20), send.preFader, true);
  }
  std::set<std::pair<size_t, uint32_t>> routedInstruments;
  for (const auto &source : graph.instruments) {
    auto found = pluginIndices.find(source.plugin);
    if (found == pluginIndices.end()) continue;
    const auto &p = processors[found->second];
    // A zero destination is an explicit disconnected-output marker, not an
    // audio route. Keep it across detach/reinsert without requiring an owner.
    if(!source.target){routedInstruments.emplace(found->second,source.output);continue;}
    size_t owner=SIZE_MAX;uint32_t prefix=0;
    if(!p.instrument){require(!detached.contains(found->second),"Insert an unconnected effect before routing its output");require(assigned.contains(found->second),"Assign an effect insert owner before routing its auxiliary output");
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
      const auto &side = plan.sidechains[index]; if(side.target==SIZE_MAX)continue; auto &target = plan.nodes[side.target];
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
  for (auto &side : plan.sidechains)if(side.target!=SIZE_MAX)
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
  compileMixerPluginRouting(graph,plan,processors,rate);
  // Bound delay-line memory before allocation (stereo float, including direct
  // sample inputs, edge compensation and separate instrument outputs).
  uint64_t delays = 0, cached = 0;
  auto storage=[&](uint32_t frames){delays+=frames;if(frames)++cached;};
  for (const auto &node : plan.nodes) storage(node.directDelay);
  for (const auto &edge : plan.connections) storage(edge.delay);
  for (const auto &source : plan.instruments) storage(source.delay);
  for (const auto &side : plan.sidechains) storage(side.delay);
  for (const auto &route : plan.pluginConnections) storage(route.delay);
  for (const auto &processor : plan.processors) storage(processor.mainDelay);
  // Every nonzero delay retains one maximum-sized output chunk so two plans
  // can read its history without advancing it twice during a transition.
  require((delays + cached * 4096) * sizeof(float) * 2 <= 256 * 1024 * 1024, "Mixer compensation exceeds the 256 MB delay budget");
  return plan;
}

MixerTransitionReuse mixerTransitionReuse(
    const MixerGraph &before, const MixerPlan &beforePlan, const std::vector<MixerProcessorInfo> &beforeProcessors,
    const MixerGraph &after, const MixerPlan &afterPlan, const std::vector<MixerProcessorInfo> &afterProcessors,
    const std::vector<std::string> &reset) {
  if(beforePlan.segmented||afterPlan.segmented){
    // Per-stage PDC and summation changed: warm independent route histories in
    // the dry bridge while retained vendors themselves continue exactly once.
    MixerTransitionReuse result;result.processors.assign(afterProcessors.size(),SIZE_MAX);
    result.direct.assign(afterPlan.nodes.size(),SIZE_MAX);result.controls.assign(afterPlan.nodes.size(),SIZE_MAX);
    result.connections.assign(afterPlan.connections.size(),SIZE_MAX);result.instruments.assign(afterPlan.instruments.size(),SIZE_MAX);result.sidechains.assign(afterPlan.sidechains.size(),SIZE_MAX);
    const auto a=projectMixerDetachedChains(before),b=projectMixerDetachedChains(after);
    for(size_t i=0;i<b.buses.size();++i)for(size_t j=0;j<a.buses.size();++j){const auto &x=a.buses[j],&y=b.buses[i];
      if(x.id==y.id&&x.preGainDB==y.preGainDB&&x.gainDB==y.gainDB&&x.prePan==y.prePan&&x.pan==y.pan&&x.width==y.width&&beforePlan.nodes[j].audible==afterPlan.nodes[i].audible)result.controls[i]=j;}
    return result;
  }
  // Intern whole expressions rather than hashes: collisions must never make
  // two different audio inputs eligible to share a stateful processor.
  std::map<std::vector<uint64_t>, uint64_t> expressions;
  std::map<std::string, uint64_t> identities;
  const std::set<std::string> changed(reset.begin(), reset.end());
  auto identity = [&](const std::string &name) { return identities.try_emplace(name, identities.size()+1).first->second; };
  auto intern = [&](std::vector<uint64_t> terms) { return expressions.try_emplace(std::move(terms), expressions.size()+1).first->second; };
  auto bits = [](double value) { return std::bit_cast<uint64_t>(value == 0 ? 0. : value); };
  enum : uint64_t { Direct = 1, Sum, Pre, Processor, Output, Audible, Fader, Route, History, Controls };
  auto compile = [&](const MixerGraph &savedGraph, const MixerPlan &plan, const std::vector<MixerProcessorInfo> &processors, bool next) {
    const auto graph=projectMixerDetachedChains(savedGraph);
    require(graph.buses.size()==plan.nodes.size(), "Transition graph and plan do not match");
    struct Expressions { std::vector<uint64_t> processors, direct, connections, instruments, sidechains, controls; } history;
    history.processors.resize(processors.size());history.direct.resize(plan.nodes.size());history.controls.resize(plan.nodes.size());
    history.connections.resize(plan.connections.size());history.instruments.resize(plan.instruments.size());history.sidechains.resize(plan.sidechains.size());
    auto &result=history.processors;
    auto routeExpression=[&](uint64_t source,uint32_t delay,double gain,uint64_t &storage) {
      storage=intern({History,source,delay});return intern({Route,storage,bits(gain)});
    };
    std::vector<std::vector<uint64_t>> inputs(graph.buses.size());
    std::vector<std::map<uint32_t, std::vector<uint64_t>>> auxiliary(processors.size());
    auto sum = [&](const std::vector<uint64_t> &values) {
      auto terms=values; terms.insert(terms.begin(),Sum); return intern(std::move(terms));
    };
    auto output = [&](size_t processor, uint32_t port) {
      require(processor<result.size() && result[processor], "Transition processor dependency is not ready");
      return intern({Output,result[processor],port});
    };
    auto description = [&](size_t index, uint64_t main) {
      const auto &p=processors[index];
      std::vector<uint64_t> terms{Processor,identity(p.instance),p.latency,p.instrument,p.bypass,
        p.outputBuses,p.activeOutputs,p.activeInputs,next && changed.contains(p.instance),main};
      for(const auto &[port,values]:auxiliary[index]) {terms.push_back(port);terms.push_back(sum(values));}
      return intern(std::move(terms));
    };
    // Source instruments are clocked once before the bus DAG; changing their
    // destination must not retrigger held notes or duplicate their DSP calls.
    for(size_t i=0;i<processors.size();++i) if(processors[i].instrument && !processors[i].bypass) {
      result[i]=description(i,0);
      for(size_t index=0;index<plan.instruments.size();++index) {
        const auto &route=plan.instruments[index];if(route.owner!=SIZE_MAX || route.processor!=i)continue;
        inputs.at(route.target).push_back(routeExpression(output(i,route.output),route.delay,1,history.instruments[index]));
      }
    }
    for(const auto index:plan.detached)result[index]=description(index,sum({}));
    for(const auto bus:plan.order) {
      const auto &node=plan.nodes.at(bus);const auto &control=graph.buses.at(bus);
      history.direct[bus]=intern({History,intern({Direct,control.id,uint64_t(control.kind)}),node.directDelay});
      history.controls[bus]=intern({Controls,control.id,bits(control.preGainDB),bits(control.prePan),bits(control.gainDB),bits(control.pan),bits(control.width),node.audible});
      auto values=inputs[bus]; values.push_back(history.direct[bus]);
      uint64_t signal=intern({Pre,sum(values),control.id,bits(control.preGainDB),bits(control.prePan)});
      const auto audible=intern({Audible,control.id,node.audible});
      for(const auto processor:node.processors) {
        require(processor<processors.size(), "Invalid transition processor slot");
        if(std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),processor)!=plan.disconnectedMainInputs.end())signal=sum({});
        if(auto found=auxiliary[processor].find(0);found!=auxiliary[processor].end()) signal=sum({signal,sum(found->second)});
        result[processor]=description(processor,signal);
        signal=output(processor,0);
        for(size_t index=0;index<plan.instruments.size();++index) {
          const auto &route=plan.instruments[index];if(route.owner!=bus || route.processor!=processor)continue;
          const auto source=intern({Audible,output(processor,route.output),audible});
          inputs.at(route.target).push_back(routeExpression(source,route.delay,1,history.instruments[index]));
        }
      }
      const auto pre=intern({Audible,signal,audible});
      const auto post=intern({Fader,pre,control.id,bits(control.gainDB),bits(control.pan),bits(control.width)});
      for(const auto index:node.outputs) {
        const auto &edge=plan.connections.at(index);
        inputs.at(edge.target).push_back(routeExpression(edge.preFader?pre:post,edge.delay,edge.gain,history.connections[index]));
      }
      for(const auto index:node.sidechains) {
        const auto &edge=plan.sidechains.at(index);
        auxiliary.at(edge.processor)[edge.input].push_back(routeExpression(edge.preFader?pre:post,edge.delay,edge.gain,history.sidechains[index]));
      }
    }
    return history;
  };
  const auto old=compile(before,beforePlan,beforeProcessors,false);
  const auto current=compile(after,afterPlan,afterProcessors,true);
  auto match=[](const std::vector<uint64_t> &before,const std::vector<uint64_t> &after) {
    std::map<uint64_t,size_t> previous;
    for(size_t i=0;i<before.size();++i)if(before[i])previous.emplace(before[i],i);
    std::vector<size_t> result(after.size(),SIZE_MAX);
    for(size_t i=0;i<after.size();++i)if(after[i])if(auto found=previous.find(after[i]);found!=previous.end())result[i]=found->second;
    return result;
  };
  return {match(old.processors,current.processors),match(old.direct,current.direct),match(old.connections,current.connections),
          match(old.instruments,current.instruments),match(old.sidechains,current.sidechains),match(old.controls,current.controls)};
}
std::vector<size_t> reusableMixerProcessors(
    const MixerGraph &before, const MixerPlan &beforePlan, const std::vector<MixerProcessorInfo> &beforeProcessors,
    const MixerGraph &after, const MixerPlan &afterPlan, const std::vector<MixerProcessorInfo> &afterProcessors,
    const std::vector<std::string> &reset) {
  return mixerTransitionReuse(before,beforePlan,beforeProcessors,after,afterPlan,afterProcessors,reset).processors;
}
} // namespace Tracker
