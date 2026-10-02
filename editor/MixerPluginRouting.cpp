#include "MixerPluginRouting.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace Tracker {
void compileMixerPluginRouting(const MixerGraph &graph,MixerPlan &plan,const std::vector<MixerProcessorInfo> &catalog,uint32_t rate) {
  if(graph.pluginConnections.empty())return;
  auto require=[](bool condition,const char *message){if(!condition)throw std::invalid_argument(message);};
  plan.segmented=true;plan.processors.resize(catalog.size());
  const size_t buses=plan.nodes.size(),base=buses*2,total=base+catalog.size();
  struct Edge {size_t target;bool audio;};
  std::vector<std::vector<Edge>> edges(total);std::vector<size_t> pending(total);
  std::vector<uint64_t> input(total),output(total);std::vector<double> tails(total);
  auto connect=[&](size_t source,size_t target,bool audio){edges[source].push_back({target,audio});++pending[target];};
  std::map<std::string,size_t> slots;
  std::vector<bool> scheduled(catalog.size());
  for(size_t p=0;p<catalog.size();++p){slots[catalog[p].instance]=p;if(catalog[p].instrument){output[base+p]=catalog[p].latency;tails[base+p]=catalog[p].tail;}}
  for(size_t bus=0;bus<buses;++bus){size_t previous=SIZE_MAX;for(auto p:plan.nodes[bus].processors){auto &entry=plan.processors[p];entry.owner=bus;entry.previous=previous;scheduled[p]=true;
      const bool cut=std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),p)!=plan.disconnectedMainInputs.end();
      if(!cut)connect(previous==SIZE_MAX?bus*2:base+previous,base+p,true);
      // Completion order is independent from audio dependency after a cut.
      connect(base+p,bus*2+1,false);previous=p;
    }
    connect(previous==SIZE_MAX?bus*2:base+previous,bus*2+1,true);
    // A cut suffix can complete before this bus receives its direct input.
    connect(bus*2,bus*2+1,false);
  }
  for(auto p:plan.detached)scheduled[p]=true;
  for(const auto &route:plan.connections)connect(route.source*2+1,route.target*2,true);
  for(const auto &route:plan.sidechains)connect(route.source*2+1,base+route.processor,true);
  for(const auto &route:plan.instruments)connect(base+route.processor,route.target*2,true);
  for(const auto &route:graph.pluginConnections){
    const auto from=slots.find(route.source),to=slots.find(route.target);if(from==slots.end()||to==slots.end())continue;
    const auto source=from->second,target=to->second;const auto &a=catalog[source],&b=catalog[target];
    require(!b.instrument,"Direct plugin audio cables currently target effect inputs");
    require(scheduled[target]&&plan.processors[target].owner!=SIZE_MAX,"Direct plugin target has no prepared effect stage");
    require(a.instrument||(scheduled[source]&&plan.processors[source].owner!=SIZE_MAX),"Direct plugin source has no prepared effect stage");
    require(route.output<a.outputBuses&&route.input<64,"Direct plugin cable port is outside the prepared layout");
    // Disabled cables participate in cycle validation, but not arrival times.
    const bool enabled=route.enabled&&!a.bypass&&!b.bypass&&(a.activeOutputs&(uint64_t(1)<<route.output))&&(!route.input||(b.activeInputs&(uint64_t(1)<<route.input)));
    connect(base+source,base+target,enabled);
    if(enabled)plan.pluginConnections.push_back({source,target,route.output,route.input,0,std::pow(10.,route.gainDB/20.)});
  }
  std::set<size_t> ready;for(size_t i=0;i<total;++i)if(!pending[i])ready.insert(i);
  size_t visited=0;plan.execution.clear();plan.order.clear();
  while(!ready.empty()){const auto node=*ready.begin();ready.erase(ready.begin());++visited;
    if(node<base){const auto bus=node/2;output[node]=input[node];if(node%2){plan.nodes[bus].outputLatency=uint32_t(output[node]);plan.order.push_back(bus);plan.execution.push_back({2,bus,SIZE_MAX});}else{plan.nodes[bus].inputLatency=uint32_t(output[node]);plan.execution.push_back({0,bus,SIZE_MAX});}}
    else{const auto p=node-base;if(!catalog[p].instrument){output[node]=input[node]+catalog[p].latency;tails[node]+=catalog[p].tail;if(scheduled[p])plan.execution.push_back({1,plan.processors[p].owner,p});}
      plan.processors[p].inputLatency=uint32_t(input[node]);plan.processors[p].outputLatency=uint32_t(output[node]);}
    require(output[node]<=uint64_t(rate)*30,"Direct plugin routing latency exceeds 30 seconds");
    for(const auto &edge:edges[node]){if(edge.audio){input[edge.target]=std::max(input[edge.target],output[node]);tails[edge.target]=std::max(tails[edge.target],tails[node]);}if(!--pending[edge.target])ready.insert(edge.target);}
  }
  require(visited==total,"Direct plugin audio routing creates a feedback cycle");
  double earliest=0,latest=0;for(const auto &bus:graph.buses){earliest=std::min(earliest,bus.timingMS);latest=std::max(latest,bus.timingMS);}
  const auto lead=int64_t(std::llround(-earliest*rate/1000));
  for(size_t bus=0;bus<buses;++bus)plan.nodes[bus].directDelay=uint32_t(int64_t(plan.nodes[bus].inputLatency)+lead+std::llround(graph.buses[bus].timingMS*rate/1000));
  for(size_t p=0;p<catalog.size();++p)if(scheduled[p]){auto &entry=plan.processors[p];if(entry.owner==SIZE_MAX||std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),p)!=plan.disconnectedMainInputs.end())continue;
    const auto previous=entry.previous==SIZE_MAX?plan.nodes[entry.owner].inputLatency:plan.processors[entry.previous].outputLatency;entry.mainDelay=entry.inputLatency-previous;}
  for(auto &route:plan.connections)route.delay=plan.nodes[route.target].inputLatency-plan.nodes[route.source].outputLatency;
  for(auto &route:plan.sidechains){route.prefixLatency=plan.processors[route.processor].inputLatency>plan.nodes[route.target].inputLatency?plan.processors[route.processor].inputLatency-plan.nodes[route.target].inputLatency:0;route.delay=plan.processors[route.processor].inputLatency-plan.nodes[route.source].outputLatency;}
  for(auto &route:plan.instruments){const auto arrival=plan.processors[route.processor].outputLatency;route.prefixLatency=route.owner==SIZE_MAX||arrival<plan.nodes[route.owner].inputLatency?0:arrival-plan.nodes[route.owner].inputLatency;
    route.delay=uint32_t(int64_t(plan.nodes[route.target].inputLatency)-arrival+(route.owner==SIZE_MAX?lead+std::llround(graph.buses[route.target].timingMS*rate/1000):0));}
  for(auto &route:plan.pluginConnections){const auto &source=catalog[route.source];const auto owner=plan.processors[route.target].owner;route.delay=uint32_t(int64_t(plan.processors[route.target].inputLatency)-plan.processors[route.source].outputLatency+(source.instrument&&owner!=SIZE_MAX?lead+std::llround(graph.buses[owner].timingMS*rate/1000):0));}
  plan.latency=plan.nodes[plan.master].outputLatency+uint32_t(lead);plan.tail=std::min(60.,tails[plan.master*2+1]+latest/1000);
}
}
