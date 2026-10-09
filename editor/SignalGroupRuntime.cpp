#include "SignalGroupRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace Tracker {
float SignalGroupRuntime::Ring::push(float value) noexcept {
  if(samples.empty())return value;
  const auto result=samples[cursor];samples[cursor]=value;if(++cursor==samples.size())cursor=0;return result;
}
SignalGroupRuntime::SignalGroupRuntime(const SignalDefinition &definition,const SignalPlan &plan,double rate) {
  fadeFrames_=std::max(1u,uint32_t(std::llround(rate*.005)));
  std::map<uint64_t,size_t> nodes;for(size_t i=0;i<definition.nodes.size();++i)nodes[definition.nodes[i].id]=i;
  trimInputs_.resize(definition.audio.size());trimOutputs_.resize(definition.audio.size());
  audio_.resize(definition.audio.size());modulation_.resize(definition.modulation.size());
  for(size_t index=0;index<definition.groups.size();++index){const auto &spec=definition.groups[index];
    Group group;group.id=spec.id;group.index=index;group.wet=group.from=group.to=spec.bypass?0:1;group.elapsed=fadeFrames_;
    auto parent=spec.parent;while(parent){++group.depth;const auto found=std::find_if(definition.groups.begin(),definition.groups.end(),[&](const auto &p){return p.id==parent;});if(found==definition.groups.end())break;parent=found->parent;}
    const auto g=groups_.size();
    const auto boundary=signalGroupBoundary(definition,spec.id);std::vector<std::string> keys;
    for(const auto &p:boundary.inputs){const auto key=keys.size();keys.push_back(audioTrimKey(p));for(size_t e=0;e<definition.audio.size();++e){const auto &v=definition.audio[e];if(SignalGroupInput{v.source,v.target,v.output,v.input}==p)trimInputs_[e].emplace_back(g,key);}}
    for(const auto &p:boundary.outputs){const auto key=keys.size();keys.push_back(audioTrimKey(p));const auto members=signalGroupMembers(definition,spec.id);for(size_t e=0;e<definition.audio.size();++e){const auto &v=definition.audio[e];if(v.source==p.node&&v.output==p.port&&!members.contains(v.target))trimOutputs_[e].emplace_back(g,key);}}
    group.trims=AudioTrimRuntime(std::move(keys),spec.trims,rate);groups_.push_back(std::move(group));
    const auto members=signalGroupMembers(definition,spec.id);
    for(const auto &route:resolvedSignalGroupDryRoutes(definition,spec.id,spec.bypass)){
      auto mapping=std::make_unique<Mapping>();mapping->identity=route;mapping->group=g;mapping->groupID=spec.id;mapping->output=nodes.at(route.output.node);
      if(route.input.source){mapping->source=nodes.at(route.input.source);
        const auto edge=std::find_if(definition.audio.begin(),definition.audio.end(),[&](const auto &e){return SignalGroupInput{e.source,e.target,e.output,e.input}==route.input;});
        if(edge==definition.audio.end())throw std::invalid_argument("Group dry ingress was not prepared");mapping->input=size_t(edge-definition.audio.begin());
        mapping->ingress.samples.resize(size_t(plan.edges[mapping->input].delay)*2);
        const auto incoming=plan.arrival[nodes.at(route.input.target)];
        const auto outgoing=uint64_t(plan.arrival[mapping->output])+plan.latency[mapping->output];
        if(outgoing<incoming)throw std::invalid_argument("Group dry output precedes its input");
        mapping->alignment.samples.resize(size_t(outgoing-incoming)*2);
      }
      const auto m=mappings_.size();mappings_.push_back(std::move(mapping));
      for(size_t e=0;e<definition.audio.size();++e){const auto &edge=definition.audio[e];if(edge.source==route.output.node&&edge.output==route.output.port&&!members.contains(edge.target))audio_[e].emplace_back(g,m);}
    }
    for(size_t e=0;e<definition.modulation.size();++e){const auto &edge=definition.modulation[e];if(members.contains(edge.source)&&!members.contains(edge.target))modulation_[e].push_back(g);}
  }
  for(auto &edges:trimOutputs_)std::stable_sort(edges.begin(),edges.end(),[&](const auto &a,const auto &b){return groups_[a.first].depth>groups_[b.first].depth;});
  for(auto &edges:audio_)std::stable_sort(edges.begin(),edges.end(),[&](const auto &a,const auto &b){return groups_[a.first].depth>groups_[b.first].depth;});
}
void SignalGroupRuntime::begin(const SignalDefinition &controls,uint32_t frames,uint64_t position) noexcept {
  position_=position;
  controls_=&controls;
  for(auto &group:groups_){group.trims.begin(controls.groups[group.index].trims,position);const auto target=controls.groups[group.index].bypass?0.:1.;
    if(target!=group.to){group.from=group.wet;group.to=target;group.elapsed=0;}
    for(uint32_t f=0;f<frames;++f){group.weights[f]=float(group.wet);if(group.elapsed<fadeFrames_)++group.elapsed;
      const auto t=double(group.elapsed)/fadeFrames_,smooth=t*t*(3-2*t);group.wet=group.from+(group.to-group.from)*smooth;}
  }
}
float SignalGroupRuntime::audio(size_t edge,float value,uint32_t sample,size_t captureGroup) const noexcept {
  for(const auto &[g,p]:trimOutputs_[edge]){
    for(const auto &[owner,m]:audio_[edge])if(owner==g){const auto wet=groups_[g].weights[sample/2];if(wet!=1){const auto dry=mappings_[m]->samples[sample];value=wet==0?dry:dry+(value-dry)*wet;}}
    value*=groups_[g].trims.gain(p,position_+sample/2);
  }
  for(const auto &[g,p]:trimInputs_[edge])if(captureGroup==SIZE_MAX||groups_[g].depth<=groups_[captureGroup].depth)value*=groups_[g].trims.gain(p,position_+sample/2);
  return value;
}
double SignalGroupRuntime::modulation(size_t edge,uint32_t frame) const noexcept {
  double result=1;for(auto group:modulation_[edge])result*=groups_[group].weights[frame];return result;
}
bool SignalGroupRuntime::affectsModulation(size_t edge) const noexcept {
  for(auto group:modulation_[edge])if(groups_[group].wet!=1||groups_[group].to!=1||groups_[group].weights[0]!=1)return true;return false;
}
void SignalGroupRuntime::capture(size_t node,uint32_t frames,void *context,Source source) noexcept {
  for(auto &mapping:mappings_)if(mapping->output==node){const auto *input=mapping->source==SIZE_MAX?nullptr:source(context,mapping->source,mapping->identity.input.output);
    for(uint32_t sample=0;sample<frames*2;++sample){float value=0;
      if(input){value=audio(mapping->input,input[sample],sample,mapping->group);value=mapping->ingress.push(value);value*=float(controls_->audio[mapping->input].gain);value=mapping->alignment.push(value);}
      mapping->samples[sample]=value;
    }
  }
}
void SignalGroupRuntime::inheritState(SignalGroupRuntime &previous) noexcept {
  for(auto &group:groups_)for(const auto &old:previous.groups_)if(group.id==old.id){group.trims.inherit(old.trims);group.wet=old.wet;group.from=old.from;group.to=old.to;group.elapsed=old.elapsed;break;}
  for(auto &mapping:mappings_)for(auto &old:previous.mappings_)if(mapping->groupID==old->groupID&&mapping->identity==old->identity){
    auto take=[](Ring &next,Ring &prior){if(next.samples.size()==prior.samples.size()){next.samples.swap(prior.samples);std::swap(next.cursor,prior.cursor);}};
    take(mapping->ingress,old->ingress);take(mapping->alignment,old->alignment);break;
  }
}
size_t SignalGroupRuntime::storageBytes() const noexcept {
  size_t result=sizeof(*this)+groups_.capacity()*sizeof(Group)+mappings_.capacity()*sizeof(mappings_[0])+audio_.capacity()*sizeof(audio_[0])+modulation_.capacity()*sizeof(modulation_[0]);
  for(const auto &g:groups_)result+=g.trims.bytes();for(const auto *v:{&trimInputs_,&trimOutputs_}){result+=v->capacity()*sizeof((*v)[0]);for(const auto &e:*v)result+=e.capacity()*sizeof(e[0]);}
  for(const auto &m:mappings_)result+=sizeof(*m)+(m->ingress.samples.capacity()+m->alignment.samples.capacity())*sizeof(float);
  for(const auto &v:audio_)result+=v.capacity()*sizeof(v[0]);for(const auto &v:modulation_)result+=v.capacity()*sizeof(v[0]);return result;
}
}
