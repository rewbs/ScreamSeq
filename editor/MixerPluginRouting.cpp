#include "MixerPluginRouting.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace Tracker {
void compileMixerPluginRouting(const MixerGraph &graph,MixerPlan &plan,const std::vector<MixerProcessorInfo> &catalog,uint32_t rate) {
  if(graph.pluginConnections.empty()&&plan.timing.empty()&&std::none_of(catalog.begin(),catalog.end(),[](const auto &p){return p.scheduledSource;}))return;
  auto require=[](bool condition,const char *message){if(!condition)throw std::invalid_argument(message);};
  plan.segmented=true;plan.processors.resize(catalog.size());
  const size_t buses=plan.nodes.size(),base=buses*2,total=base+catalog.size()*2;
  const auto in=[&](size_t p){return base+p*2;};const auto out=[&](size_t p){return base+p*2+1;};
  struct Edge {size_t target;bool audio;int64_t offset=0;};
  std::vector<std::vector<Edge>> edges(total);std::vector<size_t> pending(total);
  std::vector<uint64_t> input(total),output(total);std::vector<double> tails(total);
  auto connect=[&](size_t source,size_t target,bool audio,int64_t offset=0){edges[source].push_back({target,audio,offset});++pending[target];};
  std::map<std::string,size_t> slots;std::vector<bool> scheduled(catalog.size());
  for(size_t p=0;p<catalog.size();++p){slots[catalog[p].instance]=p;connect(in(p),out(p),true);if(catalog[p].scheduledSource){scheduled[p]=true;plan.scheduledSources.push_back(p);}}
  for(size_t bus=0;bus<buses;++bus){size_t previous=SIZE_MAX;for(auto p:plan.nodes[bus].processors){auto &entry=plan.processors[p];entry.owner=bus;entry.previous=previous;scheduled[p]=true;
      const bool cut=std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),p)!=plan.disconnectedMainInputs.end();
      if(!cut)connect(previous==SIZE_MAX?bus*2:out(previous),in(p),true);
      connect(out(p),bus*2+1,false);previous=p;
    }
    connect(previous==SIZE_MAX?bus*2:out(previous),bus*2+1,true);connect(bus*2,bus*2+1,false);
  }
  for(auto p:plan.detached)scheduled[p]=true;
  for(const auto &r:plan.connections)connect(r.source*2+1,r.target*2,true);
  for(const auto &r:plan.sidechains)connect(r.source*2+1,in(r.processor),true);
  for(const auto &r:plan.instruments)connect(out(r.processor),r.target*2,true);
  for(const auto &r:graph.pluginConnections){const auto from=slots.find(r.source),to=slots.find(r.target);if(from==slots.end()||to==slots.end())continue;
    const auto source=from->second,target=to->second;const auto &a=catalog[source],&b=catalog[target];
    require(scheduled[target]&&(plan.processors[target].owner!=SIZE_MAX||b.scheduledSource),"Direct plugin target has no prepared audio stage");
    require(a.instrument||(scheduled[source]&&plan.processors[source].owner!=SIZE_MAX),"Direct plugin source has no prepared effect stage");
    require(r.output<a.outputBuses&&r.input<64,"Direct plugin cable port is outside the prepared layout");
    require(!b.instrument||(b.activeInputs&(uint64_t(1)<<r.input)),"Instrument input is absent from the prepared physical layout");
    const bool enabled=r.enabled&&!a.bypass&&!b.bypass&&(a.activeOutputs&(uint64_t(1)<<r.output))&&(!r.input||(b.activeInputs&(uint64_t(1)<<r.input)));
    connect(out(source),in(target),enabled);
    if(enabled)plan.pluginConnections.push_back({source,target,r.output,r.input,0,std::pow(10.,r.gainDB/20.)});
  }
  auto point=[&](const MixerTimingPoint &p)->size_t{if(!p.processor.empty()){const auto found=slots.find(p.processor);return found==slots.end()?SIZE_MAX:p.input?in(found->second):out(found->second);}const auto bus=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==p.bus;});return bus==graph.buses.end()?SIZE_MAX:size_t(bus-graph.buses.begin())*2+(p.input?0:1);};
  struct Timing {size_t source,target;int64_t offset;};std::vector<Timing> timing;
  for(const auto &constraint:plan.timing){const auto from=point(constraint.source),executionTarget=point(constraint.target);auto target=constraint.target;uint32_t subtract=0;
    if(!target.input){if(!target.processor.empty()){const auto p=slots.find(target.processor);if(p!=slots.end())subtract=catalog[p->second].latency;target.input=true;}
      else {const auto bus=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==target.bus;});if(bus!=graph.buses.end()){const auto &processors=plan.nodes[size_t(bus-graph.buses.begin())].processors;if(!processors.empty()){const auto p=processors.back();target.processor=catalog[p].instance;target.bus=0;subtract=catalog[p].latency;}target.input=true;}}}
    const auto to=point(target);if(from==SIZE_MAX||to==SIZE_MAX||executionTarget==SIZE_MAX)continue;
    if(from!=executionTarget)connect(from,executionTarget,false);
    timing.push_back({from,to,-int64_t(subtract)});
  }
  std::set<size_t> ready;for(size_t i=0;i<total;++i)if(!pending[i])ready.insert(i);
  size_t visited=0;plan.execution.clear();plan.order.clear();
  while(!ready.empty()){const auto node=*ready.begin();ready.erase(ready.begin());++visited;output[node]=input[node];
    if(node<base){const auto bus=node/2;if(node%2){plan.nodes[bus].outputLatency=uint32_t(output[node]);plan.order.push_back(bus);plan.execution.push_back({2,bus,SIZE_MAX});}else{plan.nodes[bus].inputLatency=uint32_t(output[node]);plan.execution.push_back({0,bus,SIZE_MAX});}}
    else{const auto p=(node-base)/2;if((node-base)%2){output[node]+=catalog[p].latency;tails[node]+=catalog[p].tail;plan.processors[p].outputLatency=uint32_t(output[node]);if(scheduled[p])plan.execution.push_back({1,plan.processors[p].owner,p});}else plan.processors[p].inputLatency=uint32_t(output[node]);}
    require(output[node]<=uint64_t(rate)*30,"Direct plugin routing latency exceeds 30 seconds");
    for(const auto &e:edges[node]){if(e.audio){input[e.target]=std::max(input[e.target],uint64_t(std::max<int64_t>(0,int64_t(output[node])+e.offset)));tails[e.target]=std::max(tails[e.target],tails[node]);}if(!--pending[e.target])ready.insert(e.target);}
  }
  require(visited==total,"Direct plugin audio routing creates a feedback cycle");
  // Timing inequalities can form harmless zero/negative-weight cycles (two
  // parallel branches exchanging dry inputs). Their execution graph above is
  // still acyclic: each capture precedes only the corresponding output. Solve
  // arrival padding separately; a positive cycle has no finite PDC solution.
  if(!timing.empty()){
    for(size_t pass=0;pass<=total;++pass){bool changed=false;
      auto raise=[&](size_t target,int64_t arrival){const auto value=uint64_t(std::max<int64_t>(0,arrival));require(value<=uint64_t(rate)*30,"Group boundary latency exceeds 30 seconds");if(value>output[target]){output[target]=value;changed=true;}};
      for(size_t source=0;source<total;++source)for(const auto &edge:edges[source])if(edge.audio){const auto own=edge.target>=base&&(edge.target-base)%2?catalog[(edge.target-base)/2].latency:0;raise(edge.target,int64_t(output[source])+own);}
      for(const auto &edge:timing)raise(edge.target,int64_t(output[edge.source])+edge.offset);
      if(!changed)break;require(pass<total,"Group dry mapping requires unbounded latency compensation");
    }
    for(size_t bus=0;bus<buses;++bus){plan.nodes[bus].inputLatency=uint32_t(output[bus*2]);plan.nodes[bus].outputLatency=uint32_t(output[bus*2+1]);}
    for(size_t p=0;p<catalog.size();++p){plan.processors[p].inputLatency=uint32_t(output[in(p)]);plan.processors[p].outputLatency=uint32_t(output[out(p)]);}
  }
  double earliest=0,latest=0;for(const auto &bus:graph.buses){earliest=std::min(earliest,bus.timingMS);latest=std::max(latest,bus.timingMS);}const auto lead=int64_t(std::llround(-earliest*rate/1000));
  for(size_t bus=0;bus<buses;++bus)plan.nodes[bus].directDelay=uint32_t(int64_t(plan.nodes[bus].inputLatency)+lead+std::llround(graph.buses[bus].timingMS*rate/1000));
  for(size_t p=0;p<catalog.size();++p)if(scheduled[p]){auto &entry=plan.processors[p];if(entry.owner==SIZE_MAX||std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),p)!=plan.disconnectedMainInputs.end())continue;
    const auto previous=entry.previous==SIZE_MAX?plan.nodes[entry.owner].inputLatency:plan.processors[entry.previous].outputLatency;entry.mainDelay=entry.inputLatency-previous;}
  for(auto &r:plan.connections)r.delay=plan.nodes[r.target].inputLatency-plan.nodes[r.source].outputLatency;
  for(auto &r:plan.sidechains){r.prefixLatency=r.target!=SIZE_MAX&&plan.processors[r.processor].inputLatency>plan.nodes[r.target].inputLatency?plan.processors[r.processor].inputLatency-plan.nodes[r.target].inputLatency:0;r.delay=plan.processors[r.processor].inputLatency-plan.nodes[r.source].outputLatency;}
  for(auto &r:plan.instruments){const auto arrival=plan.processors[r.processor].outputLatency;r.prefixLatency=r.owner==SIZE_MAX||arrival<plan.nodes[r.owner].inputLatency?0:arrival-plan.nodes[r.owner].inputLatency;
    r.delay=uint32_t(int64_t(plan.nodes[r.target].inputLatency)-arrival+(r.owner==SIZE_MAX?lead+std::llround(graph.buses[r.target].timingMS*rate/1000):0));}
  for(auto &r:plan.pluginConnections){const auto &source=catalog[r.source];const auto owner=plan.processors[r.target].owner;r.delay=uint32_t(int64_t(plan.processors[r.target].inputLatency)-plan.processors[r.source].outputLatency+(source.instrument&&owner!=SIZE_MAX?lead+std::llround(graph.buses[owner].timingMS*rate/1000):0));}
  plan.latency=plan.nodes[plan.master].outputLatency+uint32_t(lead);plan.tail=std::min(60.,tails[plan.master*2+1]+latest/1000);
}
}
