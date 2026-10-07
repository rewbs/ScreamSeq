#pragma once
#include "NativeSong.hpp"
#include <charconv>
#include <optional>

namespace Tracker {
struct AudioTrimPortInfo {std::string key,name;bool output=false;};
struct AudioTrimOwnerInfo {std::string node;std::vector<AudioTrimPortInfo> ports;bool instrument=false;};
struct GraphTrimAccess {AudioPortTrims *trims=nullptr;std::vector<AudioTrimPortInfo> ports;};
inline uint64_t trimNativeID(const std::string &key){uint64_t value=0;if(key.size()<2||key[0]!='n'||key[1]=='0')throw std::invalid_argument("Invalid trim node identity");const auto r=std::from_chars(key.data()+1,key.data()+key.size(),value);if(r.ec!=std::errc{}||r.ptr!=key.data()+key.size()||!value)throw std::invalid_argument("Invalid trim node identity");return value;}
inline GraphTrimAccess graphTrimAccess(NativeSong &song,uint64_t graph,const std::string &node,const std::vector<AudioTrimOwnerInfo> &rack) {
  GraphTrimAccess result;
  auto port=[&](bool output,uint32_t index,const std::string &name={}){const auto key=audioTrimPort(output,index);if(std::none_of(result.ports.begin(),result.ports.end(),[&](const auto &p){return p.key==key;}))result.ports.push_back({key,name.empty()?std::string(output?"Output ":"Input ")+std::to_string(index):name,output});};
  if(graph){
    auto d=std::find_if(song.signal.library.begin(),song.signal.library.end(),[&](const auto &v){return v.id==graph;});if(d==song.signal.library.end())throw std::invalid_argument("Subgraph no longer exists");
    const auto id=trimNativeID(node);auto n=std::find_if(d->nodes.begin(),d->nodes.end(),[&](const auto &v){return v.id==id;});
    if(n!=d->nodes.end()){
      if(n->kind==SignalNodeKind::Plugin||n->kind==SignalNodeKind::Output||n->kind==SignalNodeKind::Follower)port(false,0);
      if(n->kind==SignalNodeKind::Plugin||n->kind==SignalNodeKind::Input)port(true,0);
      for(auto p:n->plugin.inputs)port(false,p);for(auto p:n->plugin.outputs)port(true,p);
      for(const auto &e:d->audio){if(e.source==id)port(true,e.output);if(e.target==id)port(false,e.input);}
      if(result.ports.empty())throw std::invalid_argument("Choose a node with audio ports");result.trims=&n->trims;
    }else{
      auto g=std::find_if(d->groups.begin(),d->groups.end(),[&](const auto &v){return v.id==id;});if(g==d->groups.end())throw std::invalid_argument("Audio trim node no longer exists");result.trims=&g->trims;
      const auto boundary=signalGroupBoundary(*d,id);for(const auto &p:boundary.inputs)result.ports.push_back({audioTrimKey(p),"Input → n"+std::to_string(p.target)+" / "+std::to_string(p.input),false});for(const auto &p:boundary.outputs)result.ports.push_back({audioTrimKey(p),"Output n"+std::to_string(p.node)+" / "+std::to_string(p.port),true});
    }
  }else if(node.starts_with("source:")){
    const auto id=trimNativeID(node.substr(7));auto source=std::find_if(song.signal.songSources.begin(),song.signal.songSources.end(),[&](const auto &s){return s.node.id==id;});if(source==song.signal.songSources.end()||source->node.kind!=SignalNodeKind::Follower)throw std::invalid_argument("Choose a source with an audio input");port(false,0,"Follower input");result.trims=&source->node.trims;
  }else if(node.starts_with("stage:")){
    const auto id=trimNativeID(node.substr(6));if(std::none_of(song.signal.assignments.begin(),song.signal.assignments.end(),[&](const auto &a){return a.target==id;})&&std::none_of(song.signal.commands.begin(),song.signal.commands.end(),[&](const auto &c){return c.target==id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start);}))throw std::invalid_argument("Assign a graph to this stage before editing its audio trims");song.ensureMixer();if(std::none_of(song.mixer.buses.begin(),song.mixer.buses.end(),[&](const auto &b){return b.id==id;}))throw std::invalid_argument("Graph stage no longer exists");
    port(false,0,"Main input");port(true,0,"Main output");for(auto p:signalStagePorts(song.signal,id,true))port(false,p);for(auto p:signalStagePorts(song.signal,id,false))port(true,p);result.trims=&song.signal.trims[node];
  }else if(node.starts_with("plugin:")){
    const auto p=std::find_if(rack.begin(),rack.end(),[&](const auto &p){return p.node==node;});if(p==rack.end())throw std::invalid_argument("Audio trim processor no longer exists");result.ports=p->ports;result.trims=&song.signal.trims[node];
  }else{
    const auto id=trimNativeID(node);const auto g=std::find_if(song.signal.groups.begin(),song.signal.groups.end(),[&](const auto &v){return v.id==id;});
    if(g!=song.signal.groups.end()){
      song.ensureMixer();std::vector<std::string> effects;for(const auto &p:rack)if(!p.instrument)effects.push_back(p.node.substr(7));const auto boundary=signalSongGroupBoundary(song.signal,song.mixer,effects,id);result.trims=&g->trims;
      for(const auto &p:boundary.inputs)result.ports.push_back({audioTrimKey(p,false),"Input "+(p.plugin.empty()?p.target:p.plugin)+" / "+std::to_string(p.input),false});
      for(const auto &p:boundary.outputs)result.ports.push_back({audioTrimKey(p,true),"Output "+(p.plugin.empty()?p.source:p.plugin)+" / "+std::to_string(p.output),true});
    }else{
      song.ensureMixer();if(std::none_of(song.mixer.buses.begin(),song.mixer.buses.end(),[&](const auto &b){return b.id==id;}))throw std::invalid_argument("Audio trim bus no longer exists");port(false,0,"Before inserts");port(true,0,"After inserts");result.trims=&song.signal.trims[node];
    }
  }
  return result;
}
inline std::vector<SignalNode> graphTrimSources(const NativeSong &song,uint64_t graph){
  std::vector<SignalNode> nodes;if(graph){for(const auto &d:song.signal.library)if(d.id==graph)nodes=d.nodes;}else for(const auto &s:song.signal.songSources)nodes.push_back(s.node);
  std::erase_if(nodes,[](const auto &n){return n.kind==SignalNodeKind::Input||n.kind==SignalNodeKind::Output||n.kind==SignalNodeKind::Plugin||n.kind==SignalNodeKind::Follower;});return nodes;
}
inline void editGraphTrim(GraphTrimAccess &access,const std::string &key,const std::optional<double> &gain,const std::optional<std::string> &link,const std::optional<std::vector<AudioTrimModulation>> &modulation={}) {
  auto available=[&](const std::string &k){return std::any_of(access.ports.begin(),access.ports.end(),[&](const auto &p){return p.key==k;});};
  if(!available(key))throw std::invalid_argument("Audio trim port is unavailable; refresh its node");
  auto next=*access.trims;
  if(link){
    if(!link->empty()&&(!available(*link)||key.starts_with("i:")==link->starts_with("i:")))throw std::invalid_argument("Choose an input and an output to link");
    for(auto i=next.links.begin();i!=next.links.end();)if(i->first==key||i->second==key||(!link->empty()&&(i->first==*link||i->second==*link)))i=next.links.erase(i);else ++i;
    if(!link->empty())next.links[key.starts_with("i:")?key:*link]=key.starts_with("i:")?*link:key;
  }
  if(modulation){if(modulation->empty())next.modulation.erase(key);else next.modulation[key]=*modulation;}
  if(gain)next.set(key,*gain);next.validate();std::erase_if(next.gains,[](const auto &p){return p.second==0;});*access.trims=std::move(next);
}
}
