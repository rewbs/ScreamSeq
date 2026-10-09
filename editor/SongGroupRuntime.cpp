#include "SongGroupRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
std::vector<MixerTimingConstraint> SongGroupRuntime::timing(const SignalGraph &signal,const MixerGraph &saved,const std::vector<MixerProcessorInfo> &catalog) {
  const auto graph=projectMixerDetachedChains(saved);std::vector<std::string> rack;
  for(const auto &p:catalog)if(!p.instrument&&std::none_of(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return p.instance==signalBusIdentity(b.id);}))rack.push_back(p.instance);
  auto bus=[](const std::string &key)->uint64_t{return key.size()>1&&key[0]=='n'?std::stoull(key.substr(1)):0;};
  auto plugin=[](const std::string &key){return key.starts_with("plugin:")?key.substr(7):std::string{};};
  auto point=[&](const SignalRouteIdentity &route,bool ingress)->MixerTimingPoint{
    if(route.kind=="follower-input"&&route.source.starts_with("stage:"))return {signalBusIdentity(bus(route.source.substr(6))),0,false};
    if(ingress){if(route.kind=="insert"||route.kind=="plugin-input")return {route.plugin,0,true};
      if(route.kind=="graph-input")return {signalBusIdentity(bus(route.target)),0,true};
      if(route.kind=="plugin-connection")return {plugin(route.target),0,true};
      if(route.kind=="output"||route.kind=="send"||route.kind=="plugin-output"||route.kind=="graph-output")return {{},bus(route.target),true};
    }
    if(route.kind=="insert")return {route.plugin,0,true};
    if(route.kind=="plugin-output")return {route.plugin,0,false};
    if(route.kind=="graph-output")return {signalBusIdentity(bus(route.source)),0,false};
    if(route.kind=="plugin-connection")return {plugin(route.source),0,false};
    if(route.kind=="follower-input"&&route.source.starts_with("plugin:"))return {plugin(route.source),0,false};
    return {{},bus(route.source),false};
  };
  std::vector<MixerTimingConstraint> result;
  for(const auto &group:signal.groups)for(const auto &mapping:resolvedSongGroupDryRoutes(signal,graph,rack,group.id,group.bypass))if(!mapping.input.kind.empty()){
    MixerTimingConstraint constraint{point(mapping.input,true),point(mapping.output,false)};
    auto valid=[&](const MixerTimingPoint &p){return p.processor.empty()?std::any_of(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==p.bus;}):std::any_of(catalog.begin(),catalog.end(),[&](const auto &c){return c.instance==p.processor;});};
    if(valid(constraint.source)&&valid(constraint.target)&&constraint.source!=constraint.target&&std::find(result.begin(),result.end(),constraint)==result.end())result.push_back(std::move(constraint));
  }
  return result;
}
SongGroupRuntime::SongGroupRuntime(const SignalGraph &signal,const MixerGraph &saved,const MixerPlan &plan,const std::vector<MixerProcessorInfo> &catalog,double rate) {
  const auto graph=projectMixerDetachedChains(saved);fadeFrames_=std::max(1u,uint32_t(std::llround(rate*.005)));
  auto busID=[&](size_t index){return "n"+std::to_string(graph.buses.at(index).id);};
  auto graphBus=[&](size_t processor)->size_t{for(size_t i=0;i<graph.buses.size();++i)if(catalog.at(processor).instance==signalBusIdentity(graph.buses[i].id))return i;return SIZE_MAX;};
  std::vector<size_t> owners(catalog.size(),SIZE_MAX);std::vector<uint32_t> arrivals(catalog.size());
  for(size_t bus=0;bus<plan.nodes.size();++bus){auto arrival=plan.nodes[bus].inputLatency;
    for(auto processor:plan.nodes[bus].processors){owners[processor]=bus;arrivals[processor]=plan.segmented?plan.processors[processor].inputLatency:arrival;arrival=arrivals[processor]+catalog[processor].latency;}}
  if(plan.segmented)for(auto processor:plan.scheduledSources)arrivals[processor]=plan.processors[processor].inputLatency;
  for(size_t i=0;i<plan.connections.size();++i){const auto &edge=plan.connections[i];routes_.push_back({{edge.send?"send":"output",busID(edge.source),busID(edge.target)},MixerRuntime::RouteKind::Connection,i,plan.nodes[edge.target].inputLatency,edge.gain,edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.sidechains.size();++i){const auto &edge=plan.sidechains[i];SignalRouteIdentity id;id.source=busID(edge.source);id.input=edge.input;id.tap="post-gain";
    const auto target=graphBus(edge.processor);if(target!=SIZE_MAX){id.kind="graph-input";id.target=busID(target);}else{id.kind="plugin-input";id.plugin=catalog[edge.processor].instance;}
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::Sidechain,i,plan.segmented?plan.processors[edge.processor].inputLatency:plan.nodes[edge.target].inputLatency+edge.prefixLatency,edge.gain,edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.instruments.size();++i){const auto &edge=plan.instruments[i];SignalRouteIdentity id;id.target=busID(edge.target);id.output=edge.output;id.tap="post-gain";
    const auto source=graphBus(edge.processor);if(source!=SIZE_MAX){id.kind="graph-output";id.source=busID(source);}else{id.kind="plugin-output";id.plugin=catalog[edge.processor].instance;}
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::Instrument,i,plan.nodes[edge.target].inputLatency,1,SIZE_MAX,catalog[edge.processor].instrument&&!catalog[edge.processor].scheduledSource?SIZE_MAX:edge.processor});routes_.back().postDelay=edge.delay;}
  for(size_t i=0;i<plan.pluginConnections.size();++i){const auto &edge=plan.pluginConnections[i];SignalRouteIdentity id{"plugin-connection","plugin:"+catalog[edge.source].instance,"plugin:"+catalog[edge.target].instance,{},"post-gain",edge.input,edge.output};
    routes_.push_back({std::move(id),MixerRuntime::RouteKind::PluginConnection,i,plan.processors[edge.target].inputLatency,edge.gain,SIZE_MAX,catalog[edge.source].instrument&&!catalog[edge.source].scheduledSource?SIZE_MAX:edge.source});routes_.back().postDelay=edge.delay;}
  for(size_t bus=0;bus<plan.nodes.size();++bus)for(auto processor:plan.nodes[bus].processors)
    if(std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),processor)==plan.disconnectedMainInputs.end()&&graphBus(processor)==SIZE_MAX)
      routes_.push_back({{"insert",busID(bus),{},catalog[processor].instance,"main-path"},MixerRuntime::RouteKind::Insert,processor,arrivals[processor],1,SIZE_MAX,processor});
  if(!graph.masterOutputDisconnected)routes_.push_back({{"master-output",busID(plan.master),{},{},"pre-master-fader"},MixerRuntime::RouteKind::MasterInput,plan.master,plan.nodes[plan.master].outputLatency,1,plan.master});
  for(size_t index=0;index<signal.songSources.size();++index){const auto &source=signal.songSources[index];if(source.node.kind!=SignalNodeKind::Follower)continue;
    Route route;route.follower=true;route.index=index;route.identity={"follower-input",{},"source:n"+std::to_string(source.node.id),{},"post-gain",0,source.output};
    if(source.audioStage||!source.audioPlugin.empty()){const auto id=source.audioStage?signalBusIdentity(source.audioStage):source.audioPlugin;const auto processor=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.instance==id;});if(processor==catalog.end())continue;const auto slot=size_t(processor-catalog.begin());route.identity.source=source.audioStage?"stage:n"+std::to_string(source.audioStage):"plugin:"+source.audioPlugin;route.arrival=arrivals[slot]+processor->latency;route.processor=processor->instrument&&!processor->scheduledSource?SIZE_MAX:slot;}
    else if(source.audioBus){const auto bus=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==source.audioBus;});if(bus==graph.buses.end())continue;route.bus=size_t(bus-graph.buses.begin());route.identity.source=busID(route.bus);route.arrival=plan.nodes[route.bus].outputLatency;route.preFader=source.preFader;}
    else continue;routes_.push_back(std::move(route));
  }
  trimInputs_.resize(routes_.size());trimOutputs_.resize(routes_.size());
  transforms_.resize(routes_.size());std::vector<std::string> rack;for(const auto &p:catalog)if(!p.instrument&&graphBus(size_t(&p-catalog.data()))==SIZE_MAX)rack.push_back(p.instance);
  auto resolveRoute=[&](const SignalRouteIdentity &identity){const auto found=std::find_if(routes_.begin(),routes_.end(),[&](const auto &r){return r.identity==identity;});return found==routes_.end()?SIZE_MAX:size_t(found-routes_.begin());};
  size_t budget=0;
  for(const auto &spec:signal.groups){Group group;group.id=spec.id;group.bypass=spec.bypass;group.wet=group.from=group.to=spec.bypass?0:1;group.elapsed=fadeFrames_;
    const auto members=songSignalGroupNodes(signal,spec.id);group.members.insert(members.begin(),members.end());for(const auto &member:members)if(member.starts_with("plugin:"))group.plugins.insert(member.substr(7));for(const auto &source:signal.songSources)if(group.members.contains("source:n"+std::to_string(source.node.id)))group.sources.insert(source.node.id);
    for(auto parent=spec.parent;parent;){++group.depth;const auto found=std::find_if(signal.groups.begin(),signal.groups.end(),[&](const auto &g){return g.id==parent;});if(found==signal.groups.end())break;parent=found->parent;}
    const auto g=groups_.size();std::vector<std::string> keys;const auto boundary=signalSongGroupBoundary(signal,graph,rack,spec.id);
    for(const auto &p:boundary.inputs){const auto key=keys.size();keys.push_back(audioTrimKey(p,false));const auto r=resolveRoute(p);if(r!=SIZE_MAX)trimInputs_[r].emplace_back(g,key);}
    for(const auto &p:boundary.outputs){const auto key=keys.size();keys.push_back(audioTrimKey(p,true));const auto r=resolveRoute(p);if(r!=SIZE_MAX)trimOutputs_[r].emplace_back(g,key);}
    group.initialTrims=spec.trims;group.trims=AudioTrimRuntime(std::move(keys),spec.trims,rate);groups_.push_back(std::move(group));
    for(const auto &route:resolvedSongGroupDryRoutes(signal,graph,rack,spec.id,spec.bypass)){auto mapping=std::make_unique<Mapping>();mapping->group=g;mapping->identity=route;mapping->output=resolveRoute(route.output);if(mapping->output==SIZE_MAX)continue;
      if(!route.input.kind.empty()&&resolveRoute(route.input)!=SIZE_MAX){const auto index=resolveRoute(route.input);auto capture=std::find_if(captures_.begin(),captures_.end(),[&](const auto &c){return c->route==index&&c->group==g;});
        if(capture==captures_.end()){budget+=sizeof(Capture);if(budget>64*1024*1024)throw std::invalid_argument("Song-group boundary capture exceeds its 64 MB budget");auto next=std::make_unique<Capture>();next->route=index;next->group=g;captures_.push_back(std::move(next));mapping->input=captures_.size()-1;}else mapping->input=size_t(capture-captures_.begin());
        if(routes_[mapping->output].arrival<routes_[index].arrival+routes_[mapping->output].postDelay)throw std::invalid_argument("Group dry mapping output precedes the selected ingress latency");
        const auto postDelay=size_t(routes_[mapping->output].postDelay)*2;const auto delay=size_t(routes_[mapping->output].arrival-routes_[index].arrival)*2-postDelay;budget+=(delay+postDelay)*sizeof(float);if(budget>64*1024*1024)throw std::invalid_argument("Song-group dry alignment exceeds its 64 MB budget");mapping->delay.resize(delay);mapping->postDelay.resize(postDelay);
      }
      transforms_[mapping->output].push_back(mappings_.size());mappings_.push_back(std::move(mapping));
    }
  }
  for(auto &edges:trimOutputs_)std::stable_sort(edges.begin(),edges.end(),[&](const auto &a,const auto &b){return groups_[a.first].depth>groups_[b.first].depth;});
  for(auto &route:transforms_)std::stable_sort(route.begin(),route.end(),[&](auto a,auto b){return groups_[mappings_[a]->group].depth>groups_[mappings_[b]->group].depth;});
}
void SongGroupRuntime::refreshLatency(const MixerPlan &plan,const std::vector<MixerProcessorInfo> &catalog) {
  auto routes=routes_;std::vector<uint32_t> arrivals(catalog.size());
  for(size_t bus=0;bus<plan.nodes.size();++bus){auto arrival=plan.nodes[bus].inputLatency;for(auto processor:plan.nodes[bus].processors){arrivals[processor]=plan.segmented?plan.processors[processor].inputLatency:arrival;arrival=arrivals[processor]+catalog[processor].latency;}}
  if(plan.segmented)for(auto processor:plan.scheduledSources)arrivals[processor]=plan.processors[processor].inputLatency;
  for(auto &route:routes){if(route.follower){route.arrival=route.processor!=SIZE_MAX?arrivals.at(route.processor)+catalog.at(route.processor).latency:route.bus!=SIZE_MAX?plan.nodes.at(route.bus).outputLatency:route.arrival;continue;}
    switch(route.kind){
      case MixerRuntime::RouteKind::Connection:{const auto &edge=plan.connections.at(route.index);route.arrival=plan.nodes.at(edge.target).inputLatency;route.postDelay=edge.delay;break;}
      case MixerRuntime::RouteKind::Sidechain:{const auto &edge=plan.sidechains.at(route.index);route.arrival=arrivals.at(edge.processor);route.postDelay=edge.delay;break;}
      case MixerRuntime::RouteKind::Instrument:{const auto &edge=plan.instruments.at(route.index);route.arrival=plan.nodes.at(edge.target).inputLatency;route.postDelay=edge.delay;break;}
      case MixerRuntime::RouteKind::PluginConnection:{const auto &edge=plan.pluginConnections.at(route.index);route.arrival=plan.processors.at(edge.target).inputLatency;route.postDelay=edge.delay;break;}
      case MixerRuntime::RouteKind::Insert:route.arrival=arrivals.at(route.index);break;
      case MixerRuntime::RouteKind::MasterInput:route.arrival=plan.nodes.at(route.index).outputLatency;break;
    }
  }
  struct Delays {std::vector<float> main,post;};std::vector<Delays> buffers(mappings_.size());size_t bytes=captures_.size()*sizeof(Capture);
  for(size_t i=0;i<mappings_.size();++i){const auto &mapping=*mappings_[i];if(mapping.input==SIZE_MAX)continue;const auto &input=routes.at(captures_.at(mapping.input)->route),&output=routes.at(mapping.output);
    if(output.arrival<input.arrival+output.postDelay)throw std::invalid_argument("Refreshed group output precedes its dry ingress");
    const auto delay=size_t(output.arrival-input.arrival-output.postDelay)*2,post=size_t(output.postDelay)*2;bytes+=(delay+post)*sizeof(float);
    if(bytes>64*1024*1024)throw std::invalid_argument("Refreshed group alignment exceeds its 64 MB budget");buffers[i].main.resize(delay);buffers[i].post.resize(post);
  }
  // All validation/allocation precedes mutation. The contribution callback still
  // addresses this same object and keeps its source/group membership and fade.
  routes_.swap(routes);for(size_t i=0;i<mappings_.size();++i){auto &mapping=*mappings_[i];mapping.delay.swap(buffers[i].main);mapping.postDelay.swap(buffers[i].post);mapping.cursor=mapping.postCursor=0;}
  for(auto &capture:captures_){capture->position=UINT64_MAX;capture->frames=0;}
  for(auto &edges:trimOutputs_)std::stable_sort(edges.begin(),edges.end(),[&](const auto &a,const auto &b){return groups_[a.first].depth>groups_[b.first].depth;});
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
  for(auto &group:groups_){const AudioPortTrims *spec=&group.initialTrims;if(trimControls_)for(const auto &g:trimControls_->groups)if(g.id==group.id){spec=&g.trims;break;}group.trims.begin(*spec,position);const auto target=group.bypass?0.:1.;if(target!=group.to){group.from=group.wet;group.to=target;group.elapsed=0;}
    for(uint32_t i=0;i<frames;++i){group.weights[i]=float(group.wet);if(group.elapsed<fadeFrames_)++group.elapsed;const double t=double(group.elapsed)/fadeFrames_;group.wet=group.from+(group.to-group.from)*t*t*(3-2*t);}}
}
void SongGroupRuntime::transform(size_t index,float *samples,uint32_t frames,uint64_t position) noexcept {
  if(frames!=frames_||position!=position_||!samples){failed_=true;return;}
  for(const auto &[owner,port]:trimOutputs_[index]){
  for(const auto m:transforms_[index]){if(mappings_[m]->group!=owner)continue;auto &mapping=*mappings_[m];const auto &group=groups_[mapping.group];const Capture *input=mapping.input==SIZE_MAX?nullptr:captures_[mapping.input].get();
    if(input&&(input->position!=position||input->frames!=frames)){failed_=true;return;}
    for(uint32_t frame=0;frame<frames;++frame){float dry[2]{};
      for(uint32_t channel=0;channel<2;++channel){const auto i=frame*2+channel;dry[channel]=input?input->samples[i]:0.f;if(!mapping.delay.empty()){std::swap(dry[channel],mapping.delay[mapping.cursor]);if(++mapping.cursor==mapping.delay.size())mapping.cursor=0;}}
      if(runtime_){const auto &route=routes_[index];if(route.follower){if(route.bus!=SIZE_MAX)runtime_->dryBusGain(route.bus,route.preFader,frame,dry[0],dry[1]);}else runtime_->dryRouteGain(route.kind,route.index,frame,dry[0],dry[1]);}
      for(auto &value:dry){if(!mapping.postDelay.empty()){std::swap(value,mapping.postDelay[mapping.postCursor]);if(++mapping.postCursor==mapping.postDelay.size())mapping.postCursor=0;}value*=float(routes_[index].gain);}
      const auto wet=group.weights[frame];if(wet!=1)for(uint32_t channel=0;channel<2;++channel){const auto i=frame*2+channel;samples[i]=wet==0?dry[channel]:dry[channel]+(samples[i]-dry[channel])*wet;}
    }
  }
  groups_[owner].trims.apply(port,samples,frames,position);
  }
  // Each dry ingress includes its own and ancestor trims, never a child's
  // drive. Several nested groups can share this exact incoming route.
  for(auto &capture:captures_)if(capture->route==index){std::copy_n(samples,frames*2,capture->samples.data());
    for(const auto &[owner,port]:trimInputs_[index])if(groups_[owner].depth<=groups_[capture->group].depth)groups_[owner].trims.apply(port,capture->samples.data(),frames,position);
    capture->position=position;capture->frames=frames;
  }
  for(const auto &[owner,port]:trimInputs_[index])groups_[owner].trims.apply(port,samples,frames,position);
  for(const auto &g:groups_)if(!g.trims.valid())failed_=true;
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
  for(auto &group:groups_)for(const auto &prior:old.groups_)if(group.id==prior.id){group.trims.inherit(prior.trims);group.wet=prior.wet;group.from=prior.from;group.to=prior.to;group.elapsed=prior.elapsed;break;}
  for(auto &m:mappings_)for(auto &prior:old.mappings_)if(groups_[m->group].id==old.groups_[prior->group].id&&m->identity==prior->identity&&m->delay.size()==prior->delay.size()&&m->postDelay.size()==prior->postDelay.size()){m->delay.swap(prior->delay);std::swap(m->cursor,prior->cursor);m->postDelay.swap(prior->postDelay);std::swap(m->postCursor,prior->postCursor);break;}
}
size_t SongGroupRuntime::storageBytes() const noexcept {
  size_t result=sizeof(*this)+routes_.capacity()*sizeof(Route)+groups_.capacity()*sizeof(Group)+captures_.capacity()*sizeof(captures_[0])+captures_.size()*sizeof(Capture)+mappings_.capacity()*sizeof(mappings_[0])+transforms_.capacity()*sizeof(transforms_[0]);
  for(const auto &g:groups_)result+=g.trims.bytes()+g.initialTrims.bytes();for(const auto *v:{&trimInputs_,&trimOutputs_}){result+=v->capacity()*sizeof((*v)[0]);for(const auto &e:*v)result+=e.capacity()*sizeof(e[0]);}
  for(const auto &r:routes_)result+=r.identity.kind.capacity()+r.identity.source.capacity()+r.identity.target.capacity()+r.identity.plugin.capacity()+r.identity.tap.capacity();
  for(const auto &g:groups_){result+=g.sources.size()*(sizeof(uint64_t)+4*sizeof(void *));for(const auto &member:g.members)result+=sizeof(std::string)+member.capacity()+4*sizeof(void *);for(const auto &plugin:g.plugins)result+=sizeof(std::string)+plugin.capacity()+4*sizeof(void *);}
  for(const auto &m:mappings_){result+=sizeof(*m)+(m->delay.capacity()+m->postDelay.capacity())*sizeof(float);for(const auto *id:{&m->identity.input,&m->identity.output})result+=id->kind.capacity()+id->source.capacity()+id->target.capacity()+id->plugin.capacity()+id->tap.capacity();}for(const auto &t:transforms_)result+=t.capacity()*sizeof(size_t);return result;
}
}
