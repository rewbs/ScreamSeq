#include "SignalGraph.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Tracker {
namespace {
void require(bool yes, const char *why) { if(!yes) throw std::invalid_argument(why); }
bool finite(double value, double lo, double hi) { return std::isfinite(value) && value >= lo && value <= hi; }
bool text(const std::string &s, size_t limit) { return s.size() <= limit && s.find('\0') == std::string::npos; }
bool audioSource(SignalNodeKind k) { return k == SignalNodeKind::Input || k == SignalNodeKind::Plugin; }
bool audioTarget(SignalNodeKind k) { return k == SignalNodeKind::Output || k == SignalNodeKind::Plugin || k == SignalNodeKind::Follower; }
bool modSource(SignalNodeKind k) { return k >= SignalNodeKind::LFO && k <= SignalNodeKind::Amount; }
}
size_t SignalDefinition::bytes() const {
  size_t n = sizeof(*this) + name.size() + audio.size()*sizeof(SignalAudioEdge) + modulation.size()*sizeof(SignalModulation);
  for(const auto &v:nodes) n += sizeof(v)+v.name.size()+v.plugin.format.size()+v.plugin.name.size()+v.plugin.path.size()+v.plugin.classID.size()+v.plugin.state.size()+(v.plugin.inputs.size()+v.plugin.outputs.size())*sizeof(uint32_t);
  return n;
}
size_t SignalGraph::bytes() const {
  size_t n = sizeof(*this)+inputs.size()*sizeof(SignalInputRoute)+outputs.size()*sizeof(SignalOutputRoute)+assignments.size()*sizeof(SignalAssignment)+commands.size()*sizeof(SignalCommand)+lanes.size()*sizeof(std::pair<uint64_t,uint8_t>);
  for(const auto &[key,position]:layout)n+=key.size()+sizeof(position);
  for(const auto &d:library)n += d.bytes();
  return n;
}
SignalPlan compileSignal(const SignalDefinition &d, const std::vector<SignalProcessorInfo> &processors) {
  require(d.id && d.number && d.number <= 999 && text(d.name,1024),"Subgraphs need a stable identity, number 1..999 and valid name");
  require(d.nodes.size() >= 2 && d.nodes.size() <= 64 && d.audio.size() <= 256 && d.modulation.size() <= 256,"Subgraph exceeds node or connection limits");
  SignalPlan p; p.arrival.resize(d.nodes.size());p.latency.resize(d.nodes.size());
  std::map<uint64_t,size_t> indices;
  unsigned inputs = 0, outputs = 0;
  for(size_t i=0;i<d.nodes.size();++i) {
    const auto &n=d.nodes[i];
    require(n.id && indices.emplace(n.id,i).second && uint8_t(n.kind)<=uint8_t(SignalNodeKind::Amount),"Invalid or duplicate graph node identity");
    require(text(n.name,1024) && finite(n.x,-100000,100000) && finite(n.y,-100000,100000),"Invalid graph node label or position");
    require(finite(n.rate,.0001,1024) && finite(n.phase,0,1) && finite(n.attack,.00001,60) && finite(n.release,.00001,60) && n.controller<=127,"Invalid modulation source settings");
    if(n.kind==SignalNodeKind::Input){p.input=i;++inputs;}
    if(n.kind==SignalNodeKind::Output){p.output=i;++outputs;}
    if(n.kind==SignalNodeKind::Plugin) {
      const auto &r=n.plugin;
      require((r.format=="Built-in"||r.format=="AU"||r.format=="VST3") && text(r.name,1024) && text(r.path,16384) && text(r.classID,256) && r.state.size()<=8*1024*1024,"Invalid subgraph plugin recipe");
      for(const auto *ports:{&r.inputs,&r.outputs}){std::set<uint32_t> seen;for(auto port:*ports)require(port>0&&port<64&&seen.insert(port).second,"Auxiliary graph ports must be distinct, in 1..63");}
    }
  }
  require(inputs==1 && outputs==1,"A subgraph requires exactly one Input and one Output node");
  std::map<uint64_t,SignalProcessorInfo> info;
  for(const auto &v:processors){require(indices.contains(v.node)&&info.emplace(v.node,v).second,"Unknown or duplicate graph processor");require(v.latency<=1048576,"Graph processor latency exceeds supported delay");p.latency[indices.at(v.node)]=v.latency;}
  std::vector<std::set<size_t>> dependencies(d.nodes.size());
  std::set<std::tuple<uint64_t,uint32_t,uint64_t,uint32_t>> edges;
  for(const auto &e:d.audio) {
    require(indices.contains(e.source)&&indices.contains(e.target)&&e.source!=e.target,"Audio connection references a missing node or itself");
    auto a=indices.at(e.source),b=indices.at(e.target);
    require(audioSource(d.nodes[a].kind)&&audioTarget(d.nodes[b].kind)&&e.output<64&&e.input<64&&finite(e.gain,-16,16),"Invalid audio connection or gain");
    require(d.nodes[b].kind!=SignalNodeKind::Follower||e.input==0,"Envelope followers have one stereo input");
    require(edges.emplace(e.source,e.output,e.target,e.input).second,"Duplicate audio connection");
    auto port=[&](size_t i,uint32_t index,bool input){const auto &n=d.nodes[i];if(n.kind!=SignalNodeKind::Plugin)return true;auto found=info.find(n.id);if(found!=info.end())return bool((input?found->second.inputs:found->second.outputs)&(uint64_t(1)<<index));const auto &list=input?n.plugin.inputs:n.plugin.outputs;return index==0||std::find(list.begin(),list.end(),index)!=list.end();};
    require(port(a,e.output,false)&&port(b,e.input,true),"Audio connection uses an inactive plugin port");
    dependencies[b].insert(a);p.edges.push_back({a,b,0});
  }
  std::set<std::tuple<uint64_t,uint64_t,uint32_t>> modulation;
  std::map<std::pair<uint64_t,uint32_t>,double> bases;
  std::map<uint64_t,std::set<uint32_t>> parameters;
  for(const auto &e:d.modulation) {
    require(indices.contains(e.source)&&indices.contains(e.target),"Modulation connection references a missing node");
    auto a=indices.at(e.source),b=indices.at(e.target);
    require(modSource(d.nodes[a].kind)&&d.nodes[b].kind==SignalNodeKind::Plugin&&finite(e.minimum,-1,1)&&finite(e.maximum,-1,1)&&finite(e.base,0,1),"Invalid modulation target or range");
    require(modulation.emplace(e.source,e.target,e.parameter).second,"Duplicate modulation connection");
    parameters[e.target].insert(e.parameter);require(parameters[e.target].size()<=64,"Use at most 64 modulated parameters per processor");
    auto key=std::pair{e.target,e.parameter};require(!bases.contains(key)||bases.at(key)==e.base,"Modulation connections to a parameter must share one base value");bases[key]=e.base;
    if(e.enabled)dependencies[b].insert(a);
  }
  std::vector<bool> done(d.nodes.size());
  while(p.order.size()<d.nodes.size()) {
    bool progress=false;
    for(size_t i=0;i<d.nodes.size();++i)if(!done[i]&&std::all_of(dependencies[i].begin(),dependencies[i].end(),[&](auto j){return done[j];})) {
      // Modulation dependency affects processing order, not audio latency.
      uint64_t arrival=0;
      for(const auto &e:p.edges)if(e.target==i)arrival=std::max(arrival,uint64_t(p.arrival[e.source])+p.latency[e.source]);
      require(arrival+p.latency[i]<=1048576,"Subgraph latency exceeds supported delay");
      p.arrival[i]=uint32_t(arrival);p.order.push_back(i);done[i]=true;progress=true;
    }
    require(progress,"Graph contains a feedback cycle; use an explicit feedback effect instead");
  }
  for(auto &e:p.edges)e.delay=p.arrival[e.target]-p.arrival[e.source]-p.latency[e.source];
  uint64_t delayFrames=0;for(const auto &e:p.edges)delayFrames+=e.delay;require(delayFrames<=8*1024*1024,"Subgraph compensation exceeds 64 MB delay budget");
  p.totalLatency=p.arrival[p.output];return p;
}
bool sameSignalProcessing(const SignalGraph &a,const SignalGraph &b) {
  if(a.inputs!=b.inputs||a.outputs!=b.outputs||a.assignments!=b.assignments||a.commands!=b.commands||a.library.size()!=b.library.size())return false;
  for(size_t i=0;i<a.library.size();++i){const auto &x=a.library[i],&y=b.library[i];
    if(x.id!=y.id||x.audio!=y.audio||x.modulation!=y.modulation||x.nodes.size()!=y.nodes.size())return false;
    for(size_t j=0;j<x.nodes.size();++j){const auto &n=x.nodes[j],&m=y.nodes[j];
      if(n.id!=m.id||n.kind!=m.kind||n.plugin!=m.plugin||n.rate!=m.rate||n.phase!=m.phase||n.attack!=m.attack||n.release!=m.release||n.controller!=m.controller)return false;
    }
  }
  return true;
}
std::string signalBusIdentity(uint64_t bus){return "signal-bus-"+std::to_string(bus);}
MixerGraph signalRoutingGraph(MixerGraph mixer,const SignalGraph &signal){
  std::set<uint64_t> targets;for(const auto &a:signal.assignments)targets.insert(a.target);for(const auto &c:signal.commands)if(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start)targets.insert(c.target);
  for(auto &bus:mixer.buses)if(targets.contains(bus.id))bus.inserts.insert(bus.inserts.begin(),signalBusIdentity(bus.id));
  for(const auto &r:signal.inputs)mixer.sidechains.push_back({r.source,signalBusIdentity(r.target),r.input,r.gainDB,r.preFader,true});
  for(const auto &r:signal.outputs)mixer.instruments.push_back({signalBusIdentity(r.source),r.target,r.output});
  return mixer;
}
void SignalGraph::validate(const std::vector<uint64_t> &targets,const std::map<uint64_t,uint32_t> &patterns) const {
  require(library.size()<=128 && commands.size()<=65536 && bytes()<=16*1024*1024,"Graph library exceeds document limits");
  require(layout.size()<=8192,"Too many saved graph positions");for(const auto &[key,p]:layout)require(!key.empty()&&text(key,256)&&finite(p[0],0,100000)&&finite(p[1],0,100000),"Invalid saved graph position");
  std::set<uint64_t> definitions;std::set<uint16_t> numbers;
  for(const auto &d:library){require(definitions.insert(d.id).second&&numbers.insert(d.number).second,"Subgraph identities and numbers must be unique");compileSignal(d);}
  auto target=[&](uint64_t id){return std::find(targets.begin(),targets.end(),id)!=targets.end();};
  std::set<uint64_t> assigned;
  for(const auto &a:assignments)require(target(a.target)&&definitions.contains(a.graph)&&assigned.insert(a.target).second&&finite(a.amount,0,1)&&finite(a.wet,0,1),"Invalid or duplicate ordinary subgraph assignment");
  for(const auto &[id,count]:lanes)require(target(id)&&count>0&&count<=8,"Graph lanes require an existing bus and one to eight columns");
  require(inputs.size()<=128&&outputs.size()<=128,"Too many external graph routes");
  auto hasPort=[&](uint64_t target,uint32_t port,bool input){std::set<uint64_t> used;for(const auto &a:assignments)if(a.target==target)used.insert(a.graph);for(const auto &c:commands)if(c.target==target&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start))used.insert(c.graph);
    for(const auto &d:library)if(used.contains(d.id))for(const auto &n:d.nodes)if(n.kind==(input?SignalNodeKind::Input:SignalNodeKind::Output))for(const auto &e:d.audio)if(input?(e.source==n.id&&e.output==port):(e.target==n.id&&e.input==port))return true;return false;};
  std::set<std::tuple<uint64_t,uint64_t,uint32_t>> inputRoutes;std::set<std::pair<uint64_t,uint32_t>> outputRoutes;
  for(const auto &r:inputs)require(target(r.source)&&target(r.target)&&r.source!=r.target&&r.input>0&&r.input<64&&finite(r.gainDB,-96,12)&&hasPort(r.target,r.input,true)&&inputRoutes.emplace(r.source,r.target,r.input).second,"Invalid or duplicate external subgraph input route; assign a graph exposing that input first");
  for(const auto &r:outputs)require(target(r.source)&&target(r.target)&&r.source!=r.target&&r.output>0&&r.output<64&&hasPort(r.source,r.output,false)&&outputRoutes.emplace(r.source,r.output).second,"Invalid or duplicate external subgraph output route; assign a graph exposing that output first");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>> cells;
  for(const auto &c:commands) {
    require(patterns.contains(c.pattern)&&uint64_t(c.position)<uint64_t(patterns.at(c.pattern))*65536&&lanes.contains(c.target)&&c.column<lanes.at(c.target),"Graph command is outside its pattern or lane");
    require(uint8_t(c.kind)<=uint8_t(SignalCommandKind::Wet)&&finite(c.amount,0,1)&&finite(c.wet,0,1),"Invalid graph command type or amount");
    require(c.kind==SignalCommandKind::Clear?c.graph==0:definitions.contains(c.graph),"Graph command references an unknown subgraph");
    require(cells.emplace(c.pattern,c.target,c.position/65536,c.column).second,"Only one graph command per row and graph lane column");
  }
}
} // namespace Tracker
