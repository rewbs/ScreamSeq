#include "SongGroupRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
SongGroupRuntime::SongGroupRuntime(const SignalGraph &signal,const MixerGraph &saved,const MixerPlan &plan,const std::vector<MixerProcessorInfo> &catalog,double rate) {
  const auto graph=projectMixerDetachedChains(saved);fadeFrames_=std::max(1u,uint32_t(std::llround(rate*.005)));
  auto busID=[&](size_t index){return "n"+std::to_string(graph.buses.at(index).id);};
  auto graphBus=[&](size_t processor)->size_t{for(size_t i=0;i<graph.buses.size();++i)if(catalog.at(processor).instance==signalBusIdentity(graph.buses[i].id))return i;return SIZE_MAX;};
  std::vector<size_t> owners(catalog.size(),SIZE_MAX);std::vector<uint32_t> arrivals(catalog.size());
  for(size_t bus=0;bus<plan.nodes.size();++bus){auto arrival=plan.nodes[bus].inputLatency;
    for(auto processor:plan.nodes[bus].processors){owners[processor]=bus;arrivals[processor]=plan.segmented?plan.processors[processor].inputLatency:arrival;arrival=arrivals[processor]+catalog[processor].latency;}}
  for(size_t i=0;i<plan.connections.size();++i){const auto &edge=plan.connections[i];routes_.push_back({{edge.send?"send":"output",busID(edge.source),busID(edge.target)},MixerRuntime::RouteKind::Connection,i,plan.nodes[edge.target].inputLatency,edge.gain,edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.sidechains.size();++i){const auto &edge=plan.sidechains[i];SignalRouteIdentity id;id.source=busID(edge.source);id.input=edge.input;
    const auto target=graphBus(edge.processor);if(target!=SIZE_MAX){id.kind="graph-input";id.target=busID(target);}else{id.kind="plugin-input";id.plugin=catalog[edge.processor].instance;}
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::Sidechain,i,plan.segmented?plan.processors[edge.processor].inputLatency:plan.nodes[edge.target].inputLatency+edge.prefixLatency,edge.gain,edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.instruments.size();++i){const auto &edge=plan.instruments[i];SignalRouteIdentity id;id.target=busID(edge.target);id.output=edge.output;
    const auto source=graphBus(edge.processor);if(source!=SIZE_MAX){id.kind="graph-output";id.source=busID(source);}else{id.kind="plugin-output";id.plugin=catalog[edge.processor].instance;}
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::Instrument,i,plan.nodes[edge.target].inputLatency,1,SIZE_MAX,catalog[edge.processor].instrument?SIZE_MAX:edge.processor});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.pluginConnections.size();++i){const auto &edge=plan.pluginConnections[i];SignalRouteIdentity id{"plugin-connection","plugin:"+catalog[edge.source].instance,"plugin:"+catalog[edge.target].instance,{},"post-gain",edge.input,edge.output};
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::PluginConnection,i,plan.processors[edge.target].inputLatency,edge.gain,SIZE_MAX,catalog[edge.source].instrument?SIZE_MAX:edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t bus=0;bus<plan.nodes.size();++bus)for(auto processor:plan.nodes[bus].processors)
    if(std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),processor)==plan.disconnectedMainInputs.end()&&graphBus(processor)==SIZE_MAX)
      routes_.push_back({{"insert",busID(bus),{},catalog[processor].instance,"main-path"},MixerRuntime::RouteKind::Insert,processor,arrivals[processor],1,SIZE_MAX,processor});
  if(!graph.masterOutputDisconnected)routes_.push_back({{"master-output",busID(plan.master),{},{},"pre-master-fader"},MixerRuntime::RouteKind::MasterInput,plan.master,plan.nodes[plan.master].outputLatency,1,plan.master});
  for(size_t index=0;index<signal.songSources.size();++index){const auto &source=signal.songSources[index];if(source.node.kind!=SignalNodeKind::Follower)continue;
    Route route;route.follower=true;route.index=index;route.identity={"follower-input",{},"source:n"+std::to_string(source.node.id),{},"post-gain",0,source.output};
    if(!source.audioPlugin.empty()){const auto processor=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.instance==source.audioPlugin;});if(processor==catalog.end())continue;const auto slot=size_t(processor-catalog.begin());route.identity.source="plugin:"+source.audioPlugin;route.arrival=arrivals[slot]+processor->latency;route.processor=processor->instrument?SIZE_MAX:slot;}
    else if(source.audioBus){const auto bus=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==source.audioBus;});if(bus==graph.buses.end())continue;route.bus=size_t(bus-graph.buses.begin());route.identity.source=busID(route.bus);route.arrival=plan.nodes[route.bus].outputLatency;route.preFader=source.preFader;}
    else continue;routes_.push_back(std::move(route));
  }
  transforms_.resize(routes_.size());std::vector<std::string> rack;for(const auto &p:catalog)if(!p.instrument&&graphBus(size_t(&p-catalog.data()))==SIZE_MAX)rack.push_back(p.instance);
  auto resolveRoute=[&](const SignalRouteIdentity &identity){const auto found=std::find_if(routes_.begin(),routes_.end(),[&](const auto &r){return r.identity==identity;});return found==routes_.end()?SIZE_MAX:size_t(found-routes_.begin());};
  size_t budget=0;
  for(const auto &spec:signal.groups){Group group;group.id=spec.id;group.bypass=spec.bypass;group.wet=group.from=group.to=spec.bypass?0:1;group.elapsed=fadeFrames_;
    const auto members=songSignalGroupNodes(signal,spec.id);group.members.insert(members.begin(),members.end());for(const auto &member:members)if(member.starts_with("plugin:"))group.plugins.insert(member.substr(7));for(const auto &source:signal.songSources)if(group.members.contains("source:n"+std::to_string(source.node.id)))group.sources.insert(source.node.id);
    for(auto parent=spec.parent;parent;){++group.depth;const auto found=std::find_if(signal.groups.begin(),signal.groups.end(),[&](const auto &g){return g.id==parent;});if(found==signal.groups.end())break;parent=found->parent;}
    const auto g=groups_.size();groups_.push_back(std::move(group));
    for(const auto &route:resolvedSongGroupDryRoutes(signal,graph,rack,spec.id,spec.bypass)){auto mapping=std::make_unique<Mapping>();mapping->group=g;mapping->identity=route;mapping->output=resolveRoute(route.output);if(mapping->output==SIZE_MAX)continue;
      if(!route.input.kind.empty()&&resolveRoute(route.input)!=SIZE_MAX){const auto index=resolveRoute(route.input);auto capture=std::find_if(captures_.begin(),captures_.end(),[&](const auto &c){return c->route==index;});
        if(capture==captures_.end()){budget+=sizeof(Capture);if(budget>64*1024*1024)throw std::invalid_argument("Song-group boundary capture exceeds its 64 MB budget");auto next=std::make_unique<Capture>();next->route=index;captures_.push_back(std::move(next));mapping->input=captures_.size()-1;}else mapping->input=size_t(capture-captures_.begin());
        if(routes_[mapping->output].arrival<routes_[index].arrival+routes_[mapping->output].postDelay)throw std::invalid_argument("Group dry mapping output precedes the selected ingress latency");
        const auto postDelay=size_t(routes_[mapping->output].postDelay)*2;const auto delay=size_t(routes_[mapping->output].arrival-routes_[index].arrival)*2-postDelay;budget+=(delay+postDelay)*sizeof(float);if(budget>64*1024*1024)throw std::invalid_argument("Song-group dry alignment exceeds its 64 MB budget");mapping->delay.resize(delay);mapping->postDelay.resize(postDelay);
      }
      transforms_[mapping->output].push_back(mappings_.size());mappings_.push_back(std::move(mapping));
    }
  }
  for(auto &route:transforms_)std::stable_sort(route.begin(),route.end(),[&](auto a,auto b){return groups_[mappings_[a]->group].depth>groups_[mappings_[b]->group].depth;});
}
std::vector<MixerTransition::Dependency> SongGroupRuntime::dependencies() const {
  std::vector<MixerTransition::Dependency> result;
  for(const auto &m:mappings_)if(m->input!=SIZE_MAX){const auto &from=routes_[captures_[m->input]->route],&to=routes_[m->output];
    if((from.processor==SIZE_MAX&&from.bus==SIZE_MAX)||(from.processor==to.processor&&from.bus==to.bus))continue;
    MixerTransition::Dependency dependency;dependency.bus=from.bus;dependency.processor=from.processor;dependency.target=to.processor;dependency.targetBus=to.bus;result.push_back(dependency);
  }
  return result;
}
void SongGroupRuntime::bypass(const std::vector<std::pair<uint64_t,bool>> &values) noexcept {
  for(auto &group:groups_)for(const auto &[id,value]:values)if(id==group.id){group.bypass=value;break;}
}
void SongGroupRuntime::begin(uint32_t frames,uint64_t position) noexcept {
  frames_=frames;position_=position;
  if(!frames||frames>4096){failed_=true;return;}
  for(auto &group:groups_){const auto target=group.bypass?0.:1.;if(target!=group.to){group.from=group.wet;group.to=target;group.elapsed=0;}
    for(uint32_t i=0;i<frames;++i){group.weights[i]=float(group.wet);if(group.elapsed<fadeFrames_)++group.elapsed;const double t=double(group.elapsed)/fadeFrames_;group.wet=group.from+(group.to-group.from)*t*t*(3-2*t);}}
}
void SongGroupRuntime::transform(size_t index,float *samples,uint32_t frames,uint64_t position) noexcept {
  if(frames!=frames_||position!=position_||!samples){failed_=true;return;}
  for(const auto m:transforms_[index]){auto &mapping=*mappings_[m];const auto &group=groups_[mapping.group];const Capture *input=mapping.input==SIZE_MAX?nullptr:captures_[mapping.input].get();
    if(input&&(input->position!=position||input->frames!=frames)){failed_=true;return;}
    for(uint32_t frame=0;frame<frames;++frame){float dry[2]{};
      for(uint32_t channel=0;channel<2;++channel){const auto i=frame*2+channel;dry[channel]=input?input->samples[i]:0.f;if(!mapping.delay.empty()){std::swap(dry[channel],mapping.delay[mapping.cursor]);if(++mapping.cursor==mapping.delay.size())mapping.cursor=0;}}
      if(runtime_){const auto &route=routes_[index];if(route.follower){if(route.bus!=SIZE_MAX)runtime_->dryBusGain(route.bus,route.preFader,frame,dry[0],dry[1]);}else runtime_->dryRouteGain(route.kind,route.index,frame,dry[0],dry[1]);}
      for(auto &value:dry){if(!mapping.postDelay.empty()){std::swap(value,mapping.postDelay[mapping.postCursor]);if(++mapping.postCursor==mapping.postDelay.size())mapping.postCursor=0;}value*=float(routes_[index].gain);}
      const auto wet=group.weights[frame];if(wet!=1)for(uint32_t channel=0;channel<2;++channel){const auto i=frame*2+channel;samples[i]=wet==0?dry[channel]:dry[channel]+(samples[i]-dry[channel])*wet;}
    }
  }
  for(auto &capture:captures_)if(capture->route==index){std::copy_n(samples,frames*2,capture->samples.data());capture->position=position;capture->frames=frames;}
}
void SongGroupRuntime::route(MixerRuntime::RouteKind kind,size_t index,float *samples,uint32_t frames,uint64_t position) noexcept {
  for(size_t i=0;i<routes_.size();++i)if(!routes_[i].follower&&routes_[i].kind==kind&&routes_[i].index==index){transform(i,samples,frames,position);return;}
}
void SongGroupRuntime::follower(size_t source,float *samples,uint32_t frames,uint64_t position) noexcept {
  for(size_t i=0;i<routes_.size();++i)if(routes_[i].follower&&routes_[i].index==source){transform(i,samples,frames,position);return;}
}
double SongGroupRuntime::modulation(uint64_t source,const std::string &target,uint64_t frame) const noexcept {
  if(frame<position_||frame-position_>=frames_)return 1;double amount=1;
  for(const auto &group:groups_)if(group.sources.contains(source)&&!group.plugins.contains(target))amount*=group.weights[size_t(frame-position_)];return amount;
}
void SongGroupRuntime::inheritState(SongGroupRuntime &old) noexcept {
  for(auto &group:groups_)for(const auto &prior:old.groups_)if(group.id==prior.id){group.wet=prior.wet;group.from=prior.from;group.to=prior.to;group.elapsed=prior.elapsed;break;}
  for(auto &m:mappings_)for(auto &prior:old.mappings_)if(groups_[m->group].id==old.groups_[prior->group].id&&m->identity==prior->identity&&m->delay.size()==prior->delay.size()&&m->postDelay.size()==prior->postDelay.size()){m->delay.swap(prior->delay);std::swap(m->cursor,prior->cursor);m->postDelay.swap(prior->postDelay);std::swap(m->postCursor,prior->postCursor);break;}
}
size_t SongGroupRuntime::storageBytes() const noexcept {
  size_t result=sizeof(*this)+routes_.capacity()*sizeof(Route)+groups_.capacity()*sizeof(Group)+captures_.capacity()*sizeof(captures_[0])+captures_.size()*sizeof(Capture)+mappings_.capacity()*sizeof(mappings_[0])+transforms_.capacity()*sizeof(transforms_[0]);
  for(const auto &r:routes_)result+=r.identity.kind.capacity()+r.identity.source.capacity()+r.identity.target.capacity()+r.identity.plugin.capacity()+r.identity.tap.capacity();
  for(const auto &g:groups_){result+=g.sources.size()*(sizeof(uint64_t)+4*sizeof(void *));for(const auto &member:g.members)result+=sizeof(std::string)+member.capacity()+4*sizeof(void *);for(const auto &plugin:g.plugins)result+=sizeof(std::string)+plugin.capacity()+4*sizeof(void *);}
  for(const auto &m:mappings_){result+=sizeof(*m)+(m->delay.capacity()+m->postDelay.capacity())*sizeof(float);for(const auto *id:{&m->identity.input,&m->identity.output})result+=id->kind.capacity()+id->source.capacity()+id->target.capacity()+id->plugin.capacity()+id->tap.capacity();}for(const auto &t:transforms_)result+=t.capacity()*sizeof(size_t);return result;
}
}
