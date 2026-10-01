#pragma once
#include "SignalObservation.hpp"
#include "editor/SignalRuntimeObserver.hpp"
#include "editor/SignalGraph.hpp"
#include <map>
#include <set>
#include <tuple>

namespace Tracker {
struct SignalObservedBus {uint64_t node=0;bool output=false;uint32_t port=0,channels=2;};
// One immutable routing snapshot for one graph copy. Token identities survive
// edits; membership changes only when this runtime actually starts rendering.
class GraphSignalObservation final : public SignalRuntimeObserver {
  SignalObservation &observation_;
  uint32_t domain_;
  std::vector<SignalPortConfiguration> configuration_;
  std::map<std::tuple<uint64_t,bool,uint32_t>,uint32_t> audio_;
  std::map<uint64_t,uint32_t> controls_;
  std::vector<uint32_t> routes_,contributions_;
  uint64_t activeGeneration_=0;
public:
  GraphSignalObservation(SignalObservation &observation,uint32_t domain,const SignalCopyIdentity &copy,
      const SignalDefinition &definition,const SignalPlan &plan,std::vector<SignalPortIdentity> &pending,std::span<const SignalObservedBus> buses={})
      :observation_(observation),domain_(domain) {
    const std::string prefix="copy/"+std::to_string(copy.graph)+"/"+std::to_string(copy.target)+"/"+std::to_string(copy.role)+"/"+std::to_string(copy.instrument)+"/"+std::to_string(copy.channel)+"/";
    auto token=[&](SignalPortIdentity identity,int64_t latency,int64_t compensation,double gain=1) {
      // Immutable catalogue metadata must not silently reuse a stereo token
      // after the same logical port becomes mono (or vice versa).
      identity.key+="/channels/"+std::to_string(identity.channels);
      identity.copy=copy;uint32_t result=0;
      for(size_t i=0;i<observation.ports.size();++i)if(observation.ports[i].key==identity.key){result=uint32_t(i+1);break;}
      if(!result){for(size_t i=0;i<pending.size();++i)if(pending[i].key==identity.key){result=uint32_t(observation.ports.size()+i+1);break;}}
      if(!result){result=uint32_t(observation.ports.size()+pending.size()+1);pending.push_back(std::move(identity));}
      configuration_.push_back({result,latency,compensation,gain});return result;
    };
    const auto nodeKey=[](uint64_t id){return "node:n"+std::to_string(id);};
    for(size_t i=0;i<definition.nodes.size();++i){const auto &node=definition.nodes[i];
      const bool processor=node.kind==SignalNodeKind::Plugin;
      const bool source=node.kind!=SignalNodeKind::Input&&node.kind!=SignalNodeKind::Output&&!processor;
      if(source){SignalPortIdentity identity;identity.key=prefix+"control/"+std::to_string(node.id)+"/kind/"+std::to_string(uint32_t(node.kind));identity.node=nodeKey(node.id);identity.name=node.name+" · Control";identity.output=true;identity.kind="control";controls_[node.id]=token(std::move(identity),0,0);}
      std::set<std::pair<bool,uint32_t>> ports;
      if(processor){ports.emplace(false,0);ports.emplace(true,0);for(auto p:node.plugin.inputs)ports.emplace(false,p);for(auto p:node.plugin.outputs)ports.emplace(true,p);}
      if(node.kind==SignalNodeKind::Input)ports.emplace(true,0);
      if(node.kind==SignalNodeKind::Output||node.kind==SignalNodeKind::Follower)ports.emplace(false,0);
      for(const auto &e:definition.audio){if(e.source==node.id)ports.emplace(true,e.output);if(e.target==node.id)ports.emplace(false,e.input);}
      for(auto [output,port]:ports){SignalPortIdentity identity;identity.key=prefix+"node/"+std::to_string(node.id)+(output?"/out/":"/in/")+std::to_string(port)+"/kind/"+std::to_string(uint32_t(node.kind));identity.node=nodeKey(node.id);identity.name=node.name+(output?" · Output ":" · Input ")+std::to_string(port);identity.output=output;identity.port=port;
        for(const auto &bus:buses)if(bus.node==node.id&&bus.output==output&&bus.port==port)identity.channels=bus.channels;
        audio_[{node.id,output,port}]=token(std::move(identity),processor?plan.latency[i]:0,output?0:-1);
      }
    }
    for(size_t i=0;i<definition.audio.size();++i){const auto &edge=definition.audio[i];SignalPortIdentity identity;
      identity.key=prefix+"audio/"+std::to_string(edge.source)+"/"+std::to_string(edge.output)+"/"+std::to_string(edge.target)+"/"+std::to_string(edge.input);
      identity.node=nodeKey(edge.source);identity.name="Graph cable contribution";identity.output=true;identity.port=edge.output;
      for(const auto &bus:buses)if(bus.node==edge.source&&bus.output&&bus.port==edge.output)identity.channels=bus.channels;
      identity.route=SignalRouteIdentity{"graph-audio",nodeKey(edge.source),nodeKey(edge.target),"","post-gain",edge.input,edge.output};
      routes_.push_back(token(std::move(identity),0,plan.edges[i].delay,edge.gain));
    }
    for(const auto &edge:definition.modulation){SignalPortIdentity identity;identity.kind="control";
      identity.key=prefix+"modulation/"+std::to_string(edge.source)+"/"+std::to_string(edge.target)+"/"+std::to_string(edge.parameter);
      identity.node=nodeKey(edge.source);identity.name="Modulation contribution";identity.output=true;
      identity.route=SignalRouteIdentity{"graph-modulation",nodeKey(edge.source),nodeKey(edge.target),"","contribution",edge.parameter,0};
      contributions_.push_back(token(std::move(identity),0,0));
    }
  }
  void activate() noexcept override {
    // Removed copies retire their domain without destroying retained DSP. Undo
    // may reuse this exact observer; it must reacquire membership on rendering.
    if(!activeGeneration_||observation_.domainGeneration(domain_)!=activeGeneration_){
      observation_.activateDomain(domain_,configuration_);activeGeneration_=observation_.domainGeneration(domain_);
    }
  }
  void audio(uint64_t node,bool output,uint32_t port,const float *samples,uint32_t frames,uint64_t position) noexcept override {
    const auto found=audio_.find({node,output,port});if(found!=audio_.end())observation_.observe(found->second,samples,frames,position);
  }
  void route(uint32_t index,const float *samples,uint32_t frames,uint64_t position,double gain) noexcept override {if(index<routes_.size()){observation_.routeGain(routes_[index],gain);observation_.observe(routes_[index],samples,frames,position);}}
  void control(uint64_t node,double first,double last,uint32_t frames,uint64_t position) noexcept override {const auto found=controls_.find(node);if(found!=controls_.end())observation_.observeControl(found->second,first,last,frames,position);}
  void contribution(uint32_t index,double first,double last,uint32_t frames,uint64_t position) noexcept override {if(index<contributions_.size())observation_.observeControl(contributions_[index],first,last,frames,position);}
  size_t storageBytes() const noexcept override {return sizeof(*this)+configuration_.capacity()*sizeof(SignalPortConfiguration)+(routes_.capacity()+contributions_.capacity())*sizeof(uint32_t)+audio_.size()*(sizeof(decltype(audio_)::value_type)+4*sizeof(void*))+controls_.size()*(sizeof(decltype(controls_)::value_type)+4*sizeof(void*));}
};
}
