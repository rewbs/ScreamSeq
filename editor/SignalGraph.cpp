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
bool modSource(SignalNodeKind k) { return k >= SignalNodeKind::LFO && k <= SignalNodeKind::Automation; }
void validateSourceSettings(const SignalNode &n) {
  require(text(n.name,1024) && finite(n.x,-100000,100000) && finite(n.y,-100000,100000),"Invalid graph node label or position");
  require(finite(n.rate,.0001,1024) && finite(n.phase,0,1) && finite(n.attack,.00001,60) && finite(n.release,.00001,60) && n.controller<=127,"Invalid modulation source settings");
  require(n.kind==SignalNodeKind::Automation || n.envelopes.empty(),"Only automation sources contain pattern curves");
  require(n.envelopes.size()<=1024,"Too many graph pattern curves");
  std::set<uint64_t> patterns;
  for(const auto &lane:n.envelopes){
    require(lane.pattern&&patterns.insert(lane.pattern).second&&!lane.points.empty()&&lane.points.size()<=4096,"Graph curves need a distinct pattern and 1..4096 points");
    uint32_t previous=0;bool first=true;
    for(const auto &point:lane.points){require((first||point.position>previous)&&finite(point.value,0,1)&&uint8_t(point.curve)<=uint8_t(AutomationCurve::Scripted),"Invalid or unordered graph automation point");
      require(point.curve!=AutomationCurve::Scripted||!point.formula.source().empty(),"Scripted graph points need a formula");previous=point.position;first=false;}
  }
}
void validateGroups(const SignalDefinition &d) {
  require(d.groups.size()<=64,"Use at most 64 processing groups per definition");
  std::map<uint64_t,const SignalGroup *> groups;
  std::map<uint64_t,SignalNodeKind> nodes;
  for(const auto &node:d.nodes)nodes.emplace(node.id,node.kind);
  std::set<uint64_t> owned;
  for(const auto &group:d.groups) {
    require(group.id && group.id!=d.id && !nodes.contains(group.id) && groups.emplace(group.id,&group).second,"Invalid or duplicate processing group identity");
    require(text(group.name,256)&&finite(group.x,-100000,100000)&&finite(group.y,-100000,100000),"Invalid processing group label or position");
    for(auto id:group.nodes) {
      require(nodes.contains(id)&&nodes.at(id)!=SignalNodeKind::Input&&nodes.at(id)!=SignalNodeKind::Output,"Processing groups contain processors or modulators, not graph boundary nodes");
      require(owned.insert(id).second,"A processor may belong to only one immediate processing group");
    }
  }
  for(const auto &group:d.groups) {
    require(!group.parent||groups.contains(group.parent),"Processing group parent does not exist");
    std::set<uint64_t> ancestors{group.id};auto parent=group.parent;
    while(parent) {
      require(groups.contains(parent)&&ancestors.insert(parent).second,"Processing group boundaries cannot contain a cycle");
      parent=groups.at(parent)->parent;
    }
    require(!group.nodes.empty()||std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &child){return child.parent==group.id;}),"Empty processing groups must be ungrouped");
  }
}
}
void validateSongSignalGroups(const SignalGraph &graph) {
  require(graph.groups.size()<=128,"Use at most 128 song processing groups");
  std::map<uint64_t,const SignalSongGroup *> groups;
  std::set<std::string> owned;
  for(const auto &g:graph.groups) {
    require(g.id&&groups.emplace(g.id,&g).second,"Invalid or duplicate song processing group identity");
    require(text(g.name,256)&&finite(g.x,0,100000)&&finite(g.y,0,100000),"Invalid song processing group label or position");
    require(g.nodes.size()<=240,"Too many group members");
    for(const auto &key:g.nodes)require(key.starts_with("plugin:")&&key.size()>7&&text(key,256)&&owned.insert(key).second,"A rack processor belongs to at most one immediate song group");
  }
  for(const auto &g:graph.groups) {
    std::set<uint64_t> seen{g.id};auto parent=g.parent;
    while(parent){require(groups.contains(parent)&&seen.insert(parent).second,"Song processing group parent is missing or cyclic");parent=groups.at(parent)->parent;}
    require(!g.nodes.empty()||std::any_of(graph.groups.begin(),graph.groups.end(),[&](const auto &c){return c.parent==g.id;}),"Empty processing groups must be ungrouped");
  }
}
namespace {
std::set<uint64_t> songGroupDescendants(const SignalGraph &graph,uint64_t id) {
  require(std::any_of(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==id;}),"Song processing group does not exist");
  std::set<uint64_t> result{id};
  for(size_t i=0;i<graph.groups.size();++i)for(const auto &g:graph.groups)if(result.contains(g.parent))result.insert(g.id);
  return result;
}
}
void groupSongSignalNodes(SignalGraph &graph,const std::vector<std::string> &nodes,const std::vector<uint64_t> &children,uint64_t id,uint64_t parent,std::string name) {
  auto next=graph;validateSongSignalGroups(next);
  require(!nodes.empty()||!children.empty(),"Select rack processors or processing groups to package");
  require(id&&std::none_of(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;}),"Allocate a fresh processing group identity");
  require(!parent||std::any_of(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==parent;}),"Parent processing group does not exist");
  SignalSongGroup group{id,parent,std::move(name),100000,100000,{}};
  std::set<std::string> selected;std::set<uint64_t> selectedGroups;
  for(const auto &key:nodes) {
    require(selected.insert(key).second,"Duplicate group member");
    auto owner=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return std::find(g.nodes.begin(),g.nodes.end(),key)!=g.nodes.end();});
    require((owner==next.groups.end()?0:owner->id)==parent,"Package only siblings at this graph depth");
    if(owner!=next.groups.end())std::erase(owner->nodes,key);
    group.nodes.push_back(key);
    if(auto position=next.layout.find(key);position!=next.layout.end()){group.x=std::min(group.x,position->second[0]);group.y=std::min(group.y,position->second[1]);}
  }
  for(auto child:children) {
    require(selectedGroups.insert(child).second,"Duplicate nested group");
    auto g=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==child;});
    require(g!=next.groups.end()&&g->parent==parent,"Package only sibling processing groups");
    g->parent=id;group.x=std::min(group.x,g->x);group.y=std::min(group.y,g->y);
  }
  if(group.x==100000)group.x=300;if(group.y==100000)group.y=100;
  next.groups.push_back(std::move(group));validateSongSignalGroups(next);graph=std::move(next);
}
void ungroupSongSignalNodes(SignalGraph &graph,uint64_t id) {
  auto next=graph;validateSongSignalGroups(next);
  auto g=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;});require(g!=next.groups.end(),"Song processing group does not exist");
  if(g->parent){auto &parent=*std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &p){return p.id==g->parent;});parent.nodes.insert(parent.nodes.end(),g->nodes.begin(),g->nodes.end());}
  for(auto &child:next.groups)if(child.parent==id)child.parent=g->parent;
  removeSignalPresentationNode(next.presentation,"n"+std::to_string(id));next.groups.erase(g);validateSongSignalGroups(next);graph=std::move(next);
}
void moveSongSignalGroup(SignalGraph &graph,uint64_t id,double x,double y) {
  auto next=graph;validateSongSignalGroups(next);const auto descendants=songGroupDescendants(next,id);
  const auto &group=*std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;});const auto dx=x-group.x,dy=y-group.y;
  for(auto &g:next.groups)if(descendants.contains(g.id)){
    g.x+=dx;g.y+=dy;
    for(const auto &key:g.nodes)if(auto p=next.layout.find(key);p!=next.layout.end()){p->second[0]+=dx;p->second[1]+=dy;require(finite(p->second[0],0,100000)&&finite(p->second[1],0,100000),"Group movement exceeds the canvas");}
  }
  validateSongSignalGroups(next);graph=std::move(next);
}
void pruneSongSignalGroups(SignalGraph &graph,const std::vector<std::string> &available) {
  const auto previous=graph.groups;
  for(auto &g:graph.groups)std::erase_if(g.nodes,[&](const auto &key){return std::find(available.begin(),available.end(),key)==available.end();});
  while(true){std::set<uint64_t> parents;for(const auto &g:graph.groups)parents.insert(g.parent);if(!std::erase_if(graph.groups,[&](const auto &g){return g.nodes.empty()&&!parents.contains(g.id);}))break;}
  for(const auto &g:previous)if(std::none_of(graph.groups.begin(),graph.groups.end(),[&](const auto &v){return v.id==g.id;}))removeSignalPresentationNode(graph.presentation,"n"+std::to_string(g.id));
}
SignalDefinition extractSongSignalGroup(const SignalGraph &graph,const MixerGraph &mixer,uint64_t groupID,
    const std::vector<std::pair<std::string,GraphPluginRecipe>> &effects,const std::function<uint64_t()> &allocate) {
  validateSongSignalGroups(graph);const auto descendants=songGroupDescendants(graph,groupID);
  const auto &group=*std::find_if(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==groupID;});
  std::set<std::string> selected;
  for(const auto &g:graph.groups)if(descendants.contains(g.id))for(const auto &key:g.nodes)selected.insert(key.substr(7));
  require(!selected.empty()&&selected.size()<=62,"Save a group containing one to 62 rack effects");
  std::vector<std::string> order;const MixerBus *owner=nullptr;
  std::set<std::string> assigned;for(const auto &bus:mixer.buses)assigned.insert(bus.inserts.begin(),bus.inserts.end());
  for(const auto &bus:mixer.buses) {
    auto inserts=bus.inserts;
    if(bus.kind==MixerBusKind::Master)for(const auto &[key,recipe]:effects)if(!assigned.contains(key)&&std::find(mixer.detached.begin(),mixer.detached.end(),key)==mixer.detached.end())inserts.push_back(key);
    const auto count=std::count_if(inserts.begin(),inserts.end(),[&](const auto &id){return selected.contains(id);});
    if(count){require(!owner&&size_t(count)==selected.size(),"Save a contiguous chain from one channel; split this cross-channel group before exporting");owner=&bus;order=std::move(inserts);}
  }
  require(owner,"The selected effects have no mixer owner");
  auto first=std::find_if(order.begin(),order.end(),[&](const auto &id){return selected.contains(id);});
  require(size_t(order.end()-first)>=selected.size()&&std::all_of(first,first+selected.size(),[&](const auto &id){return selected.contains(id);}),"Save consecutive effects; this group has an unselected effect inside its main path");
  std::vector<std::string> chain(first,first+selected.size());
  SignalDefinition result;result.id=allocate();result.name=group.name;
  const auto input=allocate(),output=allocate();
  result.nodes={{input,SignalNodeKind::Input,"Input",40,100},{output,SignalNodeKind::Output,"Output",double(280+chain.size()*235),100}};
  std::map<std::string,uint64_t> ids;uint64_t previous=input;uint32_t inputPort=1,outputPort=1;
  for(size_t i=0;i<chain.size();++i){const auto &key=chain[i];const auto recipe=std::find_if(effects.begin(),effects.end(),[&](const auto &p){return p.first==key;});require(recipe!=effects.end(),"A grouped rack effect is unavailable");
    SignalNode node;node.id=allocate();node.kind=SignalNodeKind::Plugin;node.name=recipe->second.name;node.plugin=recipe->second;node.x=280+i*235;node.y=100;ids[key]=node.id;
    result.audio.push_back({previous,node.id});previous=node.id;
    // The rack host also activates auxiliary buses from routing, independently
    // of the saved recipe. Preserve those capabilities in the exported copy.
    auto enable=[](auto &ports,uint32_t port){if(port&&std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);};
    bool extraMain=false;
    for(const auto &route:mixer.sidechains)if(route.plugin==key&&route.enabled){enable(node.plugin.inputs,route.input);extraMain|=route.input==0;}
    for(const auto &route:mixer.instruments)if(route.plugin==key&&route.target)enable(node.plugin.outputs,route.output);
    std::sort(node.plugin.inputs.begin(),node.plugin.inputs.end());std::sort(node.plugin.outputs.begin(),node.plugin.outputs.end());
    // An extra main input to a later insert is not the chain's main input.
    // Give it its own boundary so it cannot accidentally pass through earlier
    // effects. Fan-in gain and tap choices remain external song-cable data.
    if(extraMain&&i>0){require(inputPort<64,"Export exceeds 63 auxiliary graph inputs");result.audio.push_back({input,node.id,inputPort++,0});}
    // Every enabled aux bus remains addressable even if it currently has no cable.
    for(auto port:node.plugin.inputs){require(inputPort<64,"Export exceeds 63 auxiliary graph inputs");result.audio.push_back({input,node.id,inputPort++,port});}
    for(auto port:node.plugin.outputs){require(outputPort<64,"Export exceeds 63 auxiliary graph outputs");result.audio.push_back({node.id,output,port,outputPort++});}
    result.nodes.push_back(std::move(node));
  }
  result.audio.push_back({previous,output});
  // Sidechain gain/tap is a property of the external song cable, not a hidden
  // property of this reusable chain. Its new boundary exposes the real input.
  std::map<uint64_t,uint64_t> groupIDs;
  for(const auto &g:graph.groups)if(g.id!=groupID&&descendants.contains(g.id))groupIDs[g.id]=allocate();
  for(const auto &g:graph.groups)if(groupIDs.contains(g.id)){
    SignalGroup copy;copy.id=groupIDs.at(g.id);copy.parent=g.parent==groupID?0:groupIDs.at(g.parent);copy.name=g.name;copy.x=g.x-group.x+280;copy.y=g.y-group.y+100;
    for(const auto &key:g.nodes)copy.nodes.push_back(ids.at(key.substr(7)));result.groups.push_back(std::move(copy));
  }
  compileSignal(result);return result;
}
size_t SignalDefinition::bytes() const {
  size_t n = presentation.bytes() + sizeof(*this) + name.size() + audio.size()*sizeof(SignalAudioEdge) + modulation.size()*sizeof(SignalModulation);
  for(const auto &v:nodes) n += sizeof(v)+v.name.size()+v.plugin.format.size()+v.plugin.name.size()+v.plugin.path.size()+v.plugin.classID.size()+v.plugin.state.size()+v.plugin.parameters.size()*(sizeof(std::pair<const uint32_t,double>)+3*sizeof(void *))+(v.plugin.inputs.size()+v.plugin.outputs.size())*sizeof(uint32_t);
  for(const auto &node:nodes)for(const auto &lane:node.envelopes){n+=sizeof(lane)+lane.points.size()*sizeof(AutomationPoint);for(const auto &point:lane.points)n+=point.formula.bytes();}
  for(const auto &group:groups)n+=sizeof(group)+group.name.size()+group.nodes.size()*sizeof(uint64_t);
  return n;
}
size_t SignalGraph::bytes() const {
  size_t n = presentation.bytes() + sizeof(*this)+inputs.size()*sizeof(SignalInputRoute)+outputs.size()*sizeof(SignalOutputRoute)+(assignments.size()+instrumentAssignments.size())*sizeof(SignalAssignment)+commands.size()*sizeof(SignalCommand)+lanes.size()*sizeof(std::pair<uint64_t,uint8_t>);
  for(const auto &[key,position]:layout)n+=key.size()+sizeof(position);
  for(const auto &d:library)n += d.bytes();
  for(const auto &s:songSources){n+=sizeof(s)+s.node.name.size()+s.audioPlugin.size();
    for(const auto &lane:s.node.envelopes){n+=sizeof(lane)+lane.points.size()*sizeof(AutomationPoint);for(const auto &point:lane.points)n+=point.formula.bytes();}}
  for(const auto &m:songModulation)n+=sizeof(m)+m.plugin.size();
  for(const auto &g:groups){n+=sizeof(g)+g.name.size();for(const auto &key:g.nodes)n+=sizeof(key)+key.size();}
  return n;
}
SignalPlan compileSignal(const SignalDefinition &d, const std::vector<SignalProcessorInfo> &processors) {
  validateGroups(d);d.presentation.validate();
  require(d.id && d.number && d.number <= 999 && text(d.name,1024),"Subgraphs need a stable identity, number 1..999 and valid name");
  require(d.nodes.size() >= 2 && d.nodes.size() <= 64 && d.audio.size() <= 256 && d.modulation.size() <= 256,"Subgraph exceeds node or connection limits");
  SignalPlan p; p.arrival.resize(d.nodes.size());p.latency.resize(d.nodes.size());
  std::map<uint64_t,size_t> indices;
  unsigned inputs = 0, outputs = 0;
  for(size_t i=0;i<d.nodes.size();++i) {
    const auto &n=d.nodes[i];
    require(n.id && indices.emplace(n.id,i).second && uint8_t(n.kind)<=uint8_t(SignalNodeKind::Automation),"Invalid or duplicate graph node identity");
    validateSourceSettings(n);
    if(n.kind==SignalNodeKind::Input){p.input=i;++inputs;}
    if(n.kind==SignalNodeKind::Output){p.output=i;++outputs;}
    if(n.kind==SignalNodeKind::Plugin) {
      const auto &r=n.plugin;
      require((r.format=="Built-in"||r.format=="AU"||r.format=="VST3") && text(r.name,1024) && text(r.path,16384) && text(r.classID,256) && r.state.size()<=8*1024*1024,"Invalid subgraph plugin recipe");
      require(r.parameters.size()<=4096,"Too many graph parameter baselines");
      for(const auto &[id,value]:r.parameters)require(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Invalid graph parameter baseline");
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
  std::map<std::pair<uint64_t,uint32_t>,bool> quantization;
  std::map<uint64_t,std::set<uint32_t>> parameters;
  for(const auto &e:d.modulation) {
    require(indices.contains(e.source)&&indices.contains(e.target),"Modulation connection references a missing node");
    auto a=indices.at(e.source),b=indices.at(e.target);
    require(modSource(d.nodes[a].kind)&&d.nodes[b].kind==SignalNodeKind::Plugin&&finite(e.minimum,-1,1)&&finite(e.maximum,-1,1)&&finite(e.base,0,1),"Invalid modulation target or range");
    require(modulation.emplace(e.source,e.target,e.parameter).second,"Duplicate modulation connection");
    parameters[e.target].insert(e.parameter);require(parameters[e.target].size()<=64,"Use at most 64 modulated parameters per processor");
    auto key=std::pair{e.target,e.parameter};require(!bases.contains(key)||bases.at(key)==e.base,"Modulation connections to a parameter must share one base value");bases[key]=e.base;
    if(e.enabled){require(!quantization.contains(key)||quantization.at(key)==e.quantized,"Enabled modulation sources must share the target quantization mode");quantization[key]=e.quantized;dependencies[b].insert(a);}
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
    require(progress,"Graph contains a feedback cycle. Feedback routing is not supported; an ordinary delay effect does not make the cycle safe.");
  }
  for(auto &e:p.edges)e.delay=p.arrival[e.target]-p.arrival[e.source]-p.latency[e.source];
  uint64_t delayFrames=0;for(const auto &e:p.edges)delayFrames+=e.delay;require(delayFrames<=8*1024*1024,"Subgraph compensation exceeds 64 MB delay budget");
  p.totalLatency=p.arrival[p.output];return p;
}
void insertSignalNodes(SignalDefinition &definition,const std::vector<uint64_t> &ids,size_t edgeIndex) {
  auto next=definition;
  const std::set<uint64_t> selected(ids.begin(),ids.end());
  require(!selected.empty()&&selected.size()==ids.size()&&selected.size()<=32,"Select 1–32 distinct effects");
  require(edgeIndex<next.audio.size(),"Audio insertion edge no longer exists");
  const auto target=next.audio[edgeIndex];
  require(!selected.count(target.source)&&!selected.count(target.target),"Drop on a wire outside the selected chain");
  for(auto id:selected)require(std::any_of(next.nodes.begin(),next.nodes.end(),[&](const auto &n){return n.id==id&&n.kind==SignalNodeKind::Plugin;}),"Only effect nodes can be inserted into an audio wire");
  std::map<uint64_t,uint64_t> after,before;
  for(const auto &e:next.audio)if(selected.count(e.source)&&selected.count(e.target)) {
    require(!e.input&&!e.output&&after.emplace(e.source,e.target).second&&before.emplace(e.target,e.source).second,"Choose a serial chain with one main input and output");
  }
  std::vector<uint64_t> heads;for(auto id:selected)if(!before.count(id))heads.push_back(id);
  require(heads.size()==1,"Choose one connected effect chain");
  const auto head=heads.front();auto tail=head;size_t count=1;
  while(after.count(tail)&&count<=selected.size()){tail=after.at(tail);++count;}
  require(count==selected.size(),"Choose one connected effect chain");
  std::vector<size_t> entering,leaving;
  for(size_t i=0;i<next.audio.size();++i){const auto &e=next.audio[i];
    if(!selected.count(e.source)&&selected.count(e.target)&&e.input==0){require(e.target==head,"An internal chain node has another main source");entering.push_back(i);}
    if(selected.count(e.source)&&!selected.count(e.target)&&e.output==0){require(e.source==tail,"An internal chain node has another main destination");leaving.push_back(i);}
  }
  require(entering.size()<=1&&leaving.size()<=1,"Disconnect extra main branches before moving this chain; auxiliary and modulation cables are preserved");
  auto audio=std::vector<SignalAudioEdge>{};
  for(size_t i=0;i<next.audio.size();++i)if(i!=edgeIndex&&(entering.empty()||i!=entering[0])&&(leaving.empty()||i!=leaving[0]))audio.push_back(next.audio[i]);
  if(!entering.empty()&&!leaving.empty()) {
    auto bridge=next.audio[entering[0]];const auto &end=next.audio[leaving[0]];
    bridge.target=end.target;bridge.input=end.input;bridge.gain*=end.gain;audio.push_back(bridge);
  }
  auto first=target;first.target=head;first.input=0;audio.push_back(first);
  audio.push_back({tail,target.target,0,target.input,1});
  next.audio=std::move(audio);compileSignal(next);definition=std::move(next);
}
void detachSignalNodes(SignalDefinition &definition,const std::vector<uint64_t> &ids,bool remove) {
  auto next=definition;
  const std::set<uint64_t> selected(ids.begin(),ids.end());
  require(!selected.empty()&&selected.size()==ids.size()&&selected.size()<=32,"Select 1–32 distinct effects");
  for(auto id:selected)require(std::any_of(next.nodes.begin(),next.nodes.end(),[&](const auto &n){return n.id==id&&n.kind==SignalNodeKind::Plugin;}),"Only effect processors have a main path to reconnect");
  std::map<uint64_t,uint64_t> after,before;
  for(const auto &e:next.audio)if(selected.count(e.source)&&selected.count(e.target)&&e.input==0&&e.output==0) {
    require(after.emplace(e.source,e.target).second&&before.emplace(e.target,e.source).second,"Select one serial main path; its internal branches are ambiguous");
  }
  std::vector<uint64_t> heads;for(auto id:selected)if(!before.count(id))heads.push_back(id);
  require(heads.size()==1,"Select one connected effect chain");
  const auto head=heads.front();auto tail=head;size_t count=1;
  while(after.count(tail)&&count<=selected.size()){tail=after.at(tail);++count;}
  require(count==selected.size(),"Select one connected effect chain");
  std::vector<size_t> entering,leaving;
  for(size_t i=0;i<next.audio.size();++i){const auto &e=next.audio[i];
    if(!selected.count(e.source)&&selected.count(e.target)&&e.input==0){require(e.target==head,"An internal effect has another main source");entering.push_back(i);}
    if(selected.count(e.source)&&!selected.count(e.target)&&e.output==0){require(e.source==tail,"An internal effect has another main destination");leaving.push_back(i);}
  }
  require(entering.size()<=1&&leaving.size()<=1,"Choose the intended main path before healing a branched connection");
  std::vector<SignalAudioEdge> audio;
  for(size_t i=0;i<next.audio.size();++i)if((entering.empty()||i!=entering[0])&&(leaving.empty()||i!=leaving[0]))audio.push_back(next.audio[i]);
  if(!entering.empty()&&!leaving.empty()) {
    auto bridge=next.audio[entering[0]];const auto &end=next.audio[leaving[0]];
    bridge.target=end.target;bridge.input=end.input;bridge.gain*=end.gain;audio.push_back(bridge);
  }
  next.audio=std::move(audio);
  if(remove) {
    std::erase_if(next.nodes,[&](const auto &n){return selected.count(n.id);});
    std::erase_if(next.audio,[&](const auto &e){return selected.count(e.source)||selected.count(e.target);});
    std::erase_if(next.modulation,[&](const auto &e){return selected.count(e.source)||selected.count(e.target);});
    pruneSignalGroups(next);
  }
  compileSignal(next);definition=std::move(next);
}
void groupSignalNodes(SignalDefinition &definition,const std::vector<uint64_t> &selection,uint64_t id,uint64_t parent,std::string name) {
  validateGroups(definition);
  auto next=definition;
  require(!selection.empty()&&selection.size()<=64,"Select processors or processing groups to package");
  require(id&&id!=next.id&&std::none_of(next.nodes.begin(),next.nodes.end(),[&](const auto &n){return n.id==id;})&&std::none_of(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;}),"Allocate a fresh processing group identity");
  require(!parent||std::any_of(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==parent;}),"Processing group parent does not exist");
  SignalGroup group{id,parent,std::move(name)};std::set<uint64_t> seen;bool first=true;
  for(auto selected:selection) {
    require(seen.insert(selected).second,"Duplicate processing group selection");
    auto child=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==selected;});
    double x,y;
    if(child!=next.groups.end()) {
      require(child->parent==parent,"Package only siblings from the current graph depth");
      child->parent=id;x=child->x;y=child->y;
    } else {
      auto node=std::find_if(next.nodes.begin(),next.nodes.end(),[&](const auto &n){return n.id==selected;});
      require(node!=next.nodes.end()&&node->kind!=SignalNodeKind::Input&&node->kind!=SignalNodeKind::Output,"Select processors or modulators to package");
      auto owner=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return std::find(g.nodes.begin(),g.nodes.end(),selected)!=g.nodes.end();});
      require((owner==next.groups.end()?0:owner->id)==parent,"Package only siblings from the current graph depth");
      if(owner!=next.groups.end())std::erase(owner->nodes,selected);
      group.nodes.push_back(selected);x=node->x;y=node->y;
    }
    if(first){group.x=x;group.y=y;first=false;}else{group.x=std::min(group.x,x);group.y=std::min(group.y,y);}
  }
  next.groups.push_back(std::move(group));validateGroups(next);definition=std::move(next);
}
void ungroupSignalNodes(SignalDefinition &definition,uint64_t id) {
  validateGroups(definition);auto next=definition;
  auto group=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;});
  require(group!=next.groups.end(),"Processing group does not exist");
  if(group->parent) {
    auto &parent=*std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==group->parent;});
    parent.nodes.insert(parent.nodes.end(),group->nodes.begin(),group->nodes.end());
  }
  for(auto &child:next.groups)if(child.parent==id)child.parent=group->parent;
  removeSignalPresentationNode(next.presentation,"n"+std::to_string(id));next.groups.erase(group);validateGroups(next);definition=std::move(next);
}
void reconcileSignalPresentation(SignalDefinition &definition,const SignalDefinition &previous) {
  auto refs=[](const SignalDefinition &d){std::vector<SignalCableGeometry> result;for(const auto &e:d.audio)result.push_back({"n"+std::to_string(e.source),"n"+std::to_string(e.target),e.output,e.input,false});for(const auto &e:d.modulation)result.push_back({"n"+std::to_string(e.source),"n"+std::to_string(e.target),0,e.parameter,true});return result;};
  const auto before=refs(previous),after=refs(definition);
  auto same=[](const auto &a,const auto &b){return a.source==b.source&&a.target==b.target&&a.output==b.output&&a.input==b.input&&a.modulation==b.modulation;};
  for(const auto &old:before)if(std::none_of(after.begin(),after.end(),[&](const auto &n){return same(old,n);})) {
    std::vector<SignalCableGeometry> candidates;
    for(const auto &n:after)if(n.modulation==old.modulation&&((n.source==old.source&&n.output==old.output)||(n.target==old.target&&n.input==old.input))&&std::none_of(before.begin(),before.end(),[&](const auto &v){return same(n,v);}))candidates.push_back(n);
    if(candidates.size()==1)retargetSignalCableGeometry(definition.presentation,old,candidates.front());
  }
  pruneSignalGroups(definition);
}
void pruneSignalGroups(SignalDefinition &definition) {
  for(auto &group:definition.groups)std::erase_if(group.nodes,[&](auto id){return std::none_of(definition.nodes.begin(),definition.nodes.end(),[&](const auto &n){return n.id==id;});});
  // Removing the last processor can leave multiple empty ancestor boundaries.
  for(;;) {
    std::set<uint64_t> parents;
    for(const auto &g:definition.groups)if(g.parent)parents.insert(g.parent);
    const auto count=std::erase_if(definition.groups,[&](const auto &g){return g.nodes.empty()&&!parents.contains(g.id);});
    if(!count)break;
  }
  std::set<std::string> available;for(const auto &n:definition.nodes)available.insert("n"+std::to_string(n.id));for(const auto &g:definition.groups)available.insert("n"+std::to_string(g.id));
  pruneSignalPresentation(definition.presentation,available);
  std::erase_if(definition.presentation.cables,[&](const auto &c){
    if(c.modulation)return std::none_of(definition.modulation.begin(),definition.modulation.end(),[&](const auto &e){return c.source=="n"+std::to_string(e.source)&&c.target=="n"+std::to_string(e.target)&&c.output==0&&c.input==e.parameter;});
    return std::none_of(definition.audio.begin(),definition.audio.end(),[&](const auto &e){return c.source=="n"+std::to_string(e.source)&&c.target=="n"+std::to_string(e.target)&&c.output==e.output&&c.input==e.input;});
  });
}
void moveSignalGroup(SignalDefinition &definition,uint64_t id,double x,double y) {
  auto next=definition;
  auto found=std::find_if(next.groups.begin(),next.groups.end(),[&](const auto &g){return g.id==id;});
  require(found!=next.groups.end(),"Processing group does not exist");
  require(finite(x,-100000,100000)&&finite(y,-100000,100000),"Invalid processing group position");
  const auto dx=x-found->x,dy=y-found->y;
  std::set<uint64_t> groups{id},members;
  for(size_t i=0;i<next.groups.size();++i)for(const auto &g:next.groups)if(groups.contains(g.parent))groups.insert(g.id);
  for(auto &g:next.groups)if(groups.contains(g.id)){g.x+=dx;g.y+=dy;members.insert(g.nodes.begin(),g.nodes.end());}
  for(auto &n:next.nodes)if(members.contains(n.id)){n.x+=dx;n.y+=dy;}
  compileSignal(next);definition=std::move(next);
}
SignalDefinition extractSignalGroup(const SignalDefinition &definition,uint64_t id,uint64_t input,uint64_t output) {
  compileSignal(definition);
  auto found=std::find_if(definition.groups.begin(),definition.groups.end(),[&](const auto &g){return g.id==id;});
  require(found!=definition.groups.end(),"Processing group does not exist");
  std::set<uint64_t> groups{id},members;
  for(size_t i=0;i<definition.groups.size();++i)for(const auto &g:definition.groups)if(groups.contains(g.parent))groups.insert(g.id);
  for(const auto &g:definition.groups)if(groups.contains(g.id))members.insert(g.nodes.begin(),g.nodes.end());
  require(input&&output&&input!=output&&!members.contains(input)&&!members.contains(output),"Allocate fresh boundary identities");
  SignalDefinition copy;copy.id=definition.id;copy.number=definition.number;copy.name=found->name;
  copy.nodes={{input,SignalNodeKind::Input,"Input",32,80}};
  double right=300;
  for(auto n:definition.nodes)if(members.contains(n.id)){n.x=n.x-found->x+280;n.y=n.y-found->y+80;right=std::max(right,n.x+240);copy.nodes.push_back(std::move(n));}
  copy.nodes.push_back({output,SignalNodeKind::Output,"Output",right,80});
  for(auto g:definition.groups)if(g.id!=id&&groups.contains(g.id)){if(g.parent==id)g.parent=0;g.x=g.x-found->x+280;g.y=g.y-found->y+80;copy.groups.push_back(std::move(g));}
  // Distinct incoming cables remain distinct buses; never sum sources or
  // discard their gains implicitly. Shared outgoing taps share an output bus.
  uint32_t nextInput=0;std::map<std::pair<uint64_t,uint32_t>,uint32_t> outputs;
  for(auto e:definition.audio){const bool a=members.contains(e.source),b=members.contains(e.target);
    if(a&&b)copy.audio.push_back(e);
    else if(!a&&b){require(nextInput<64,"This boundary needs more than 64 audio inputs");e.source=input;e.output=nextInput++;copy.audio.push_back(e);}
    else if(a&&!b){const auto key=std::pair{e.source,e.output};if(!outputs.contains(key)){require(outputs.size()<64,"This boundary needs more than 64 audio outputs");const auto port=uint32_t(outputs.size());outputs.emplace(key,port);copy.audio.push_back({e.source,output,e.output,port,1});}}
  }
  for(const auto &e:definition.modulation){const bool a=members.contains(e.source),b=members.contains(e.target);
    require(a==b,"Include both modulation sources and their targets before saving this group to the library");
    if(a)copy.modulation.push_back(e);
  }
  require(!outputs.empty(),"This group has no connected audio output to expose; connect its output before saving to the library");
  compileSignal(copy);return copy;
}
bool sameSignalProcessing(const SignalGraph &a,const SignalGraph &b) {
  if(a.songModulation!=b.songModulation || a.songSources.size()!=b.songSources.size())return false;
  for(size_t i=0;i<a.songSources.size();++i){const auto &x=a.songSources[i],&y=b.songSources[i];const auto &n=x.node,&m=y.node;
    if(n.id!=m.id||n.kind!=m.kind||n.rate!=m.rate||n.phase!=m.phase||n.attack!=m.attack||n.release!=m.release||n.controller!=m.controller||n.envelopes!=m.envelopes||x.audioBus!=y.audioBus||x.audioPlugin!=y.audioPlugin||x.output!=y.output||x.preFader!=y.preFader||x.noteTarget!=y.noteTarget||x.noteInstrument!=y.noteInstrument||x.amount!=y.amount)return false;
  }
  if(a.instrumentAssignments!=b.instrumentAssignments||a.inputs!=b.inputs||a.outputs!=b.outputs||a.assignments!=b.assignments||a.commands!=b.commands)return false;
  std::set<uint64_t> used;
  for(const auto &assignment:a.assignments)used.insert(assignment.graph);
  for(const auto &assignment:a.instrumentAssignments)used.insert(assignment.graph);
  for(const auto &command:a.commands)if(command.graph)used.insert(command.graph);
  for(const auto id:used){
    const auto xi=std::find_if(a.library.begin(),a.library.end(),[&](const auto &d){return d.id==id;});
    const auto yi=std::find_if(b.library.begin(),b.library.end(),[&](const auto &d){return d.id==id;});
    if(xi==a.library.end()||yi==b.library.end())return false;
    const auto &x=*xi,&y=*yi;
    if(x.id!=y.id||x.audio!=y.audio||x.modulation!=y.modulation||x.nodes.size()!=y.nodes.size())return false;
    for(size_t j=0;j<x.nodes.size();++j){const auto &n=x.nodes[j],&m=y.nodes[j];
      if(n.id!=m.id||n.kind!=m.kind||n.plugin!=m.plugin||n.rate!=m.rate||n.phase!=m.phase||n.attack!=m.attack||n.release!=m.release||n.controller!=m.controller||n.envelopes!=m.envelopes)return false;
    }
  }
  return true;
}
bool sameSignalParameterLayout(SignalGraph a,SignalGraph b) {
  for(auto *graph:{&a,&b})for(auto &d:graph->library){
    for(auto &n:d.nodes)n.plugin.parameters.clear();
    for(auto &m:d.modulation)m.base=0;
  }
  return sameSignalProcessing(a,b);
}
bool sameSignalControlLayout(SignalGraph a,SignalGraph b) {
  for(auto *graph:{&a,&b}){
    for(auto &s:graph->songSources){auto &n=s.node;n.rate=1;n.phase=0;n.attack=.01;n.release=.1;n.controller=1;n.envelopes.clear();s.amount=1;}
    for(auto &m:graph->songModulation)m.minimum=m.maximum=0;
  }
  for(auto *graph:{&a,&b})for(auto &d:graph->library){
    for(auto &n:d.nodes){n.plugin.parameters.clear();n.plugin.bypass=false;n.rate=1;n.phase=0;n.attack=.01;n.release=.1;n.controller=1;n.envelopes.clear();}
    for(auto &m:d.modulation){m.base=m.minimum=m.maximum=0;m.quantized=false;}
    for(auto &e:d.audio)e.gain=1;
  }
  return sameSignalProcessing(a,b);
}
bool sameSignalSourceLayout(SignalGraph a,SignalGraph b) {
  for(auto *graph:{&a,&b})for(auto &d:graph->library){
    std::set<uint64_t> followers;
    for(const auto &n:d.nodes)if(n.kind==SignalNodeKind::Follower)followers.insert(n.id);
    std::erase_if(d.audio,[&](const auto &e){return followers.contains(e.target);});
    std::erase_if(d.nodes,[](const auto &n){return n.kind==SignalNodeKind::LFO||n.kind==SignalNodeKind::Follower||n.kind==SignalNodeKind::Random||n.kind==SignalNodeKind::MIDI||n.kind==SignalNodeKind::Amount||n.kind==SignalNodeKind::Automation;});
    d.modulation.clear();
  }
  return sameSignalControlLayout(std::move(a),std::move(b));
}
std::string signalBusIdentity(uint64_t bus){return "signal-bus-"+std::to_string(bus);}
MixerGraph signalRoutingGraph(MixerGraph mixer,const SignalGraph &signal){
  std::set<uint64_t> targets;for(const auto &a:signal.assignments)targets.insert(a.target);for(const auto &c:signal.commands)if(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start)targets.insert(c.target);
  for(auto &bus:mixer.buses)if(targets.contains(bus.id))bus.inserts.insert(bus.inserts.begin(),signalBusIdentity(bus.id));
  for(const auto &r:signal.inputs)mixer.sidechains.push_back({r.source,signalBusIdentity(r.target),r.input,r.gainDB,r.preFader,true});
  for(const auto &r:signal.outputs)mixer.instruments.push_back({signalBusIdentity(r.source),r.target,r.output});
  return mixer;
}
void SignalGraph::validate(const std::vector<uint64_t> &targets,const std::map<uint64_t,uint32_t> &patterns,const std::vector<uint64_t> &instruments) const {
  validateSongSignalGroups(*this);
  require(library.size()<=128 && commands.size()<=65536 && bytes()<=16*1024*1024,"Graph library exceeds document limits");
  presentation.validate();for(const auto &d:library)d.presentation.validate();
  require(layout.size()<=8192,"Too many saved graph positions");for(const auto &[key,p]:layout)require(!key.empty()&&text(key,256)&&finite(p[0],0,100000)&&finite(p[1],0,100000),"Invalid saved graph position");
  std::set<uint64_t> definitions;std::set<uint16_t> numbers;
  for(const auto &d:library){require(definitions.insert(d.id).second&&numbers.insert(d.number).second,"Subgraph identities and numbers must be unique");compileSignal(d);
    for(const auto &node:d.nodes)for(const auto &lane:node.envelopes){require(patterns.contains(lane.pattern),"Graph automation references an unknown pattern");
      for(const auto &point:lane.points)require(uint64_t(point.position)<uint64_t(patterns.at(lane.pattern))*256,"Graph automation point is outside its pattern");}
  }
  auto target=[&](uint64_t id){return std::find(targets.begin(),targets.end(),id)!=targets.end();};
  require(songSources.size()<=64&&songModulation.size()<=256,"Song modulation exceeds source or connection limits");
  std::set<uint64_t> songSourceIDs;
  for(const auto &s:songSources){const auto &n=s.node;
    require(n.id&&songSourceIDs.insert(n.id).second&&modSource(n.kind),"Song sources need distinct identities and a control-source kind");
    validateSourceSettings(n);
    require(n.plugin==GraphPluginRecipe{},"Song modulation sources cannot contain a plugin recipe");
    require(finite(s.amount,0,1)&&s.output<64&&text(s.audioPlugin,256),"Invalid song modulation input or amount");
    require(!(s.audioBus&&!s.audioPlugin.empty())&&(!s.audioBus||target(s.audioBus)),"Choose one existing bus or plugin follower input");
    require(n.kind==SignalNodeKind::Follower||(!s.audioBus&&s.audioPlugin.empty()&&!s.output&&!s.preFader),"Only followers accept an audio tap");
    require(s.audioPlugin.empty()||!s.preFader,"Pre-fader applies to a mixer bus tap, not a plugin output");
    require(!s.audioBus||!s.output,"Mixer bus taps have one stereo output");
    require(n.kind==SignalNodeKind::NoteEnvelope||(!s.noteTarget&&!s.noteInstrument),"Only note envelopes accept a note scope");
    require(!(s.noteTarget&&s.noteInstrument)&&(!s.noteTarget||target(s.noteTarget))&&(!s.noteInstrument||std::find(instruments.begin(),instruments.end(),s.noteInstrument)!=instruments.end()),"Choose one existing note channel or instrument");
    for(const auto &lane:n.envelopes){require(patterns.contains(lane.pattern),"Song modulation envelope references an unknown pattern");for(const auto &point:lane.points)require(uint64_t(point.position)<uint64_t(patterns.at(lane.pattern))*256,"Song modulation point is outside its pattern");}
  }
  std::set<std::tuple<uint64_t,std::string,uint32_t>> songEdges;
  std::map<std::pair<std::string,uint32_t>,bool> quantization;
  for(const auto &m:songModulation){
    require(songSourceIDs.contains(m.source)&&!m.plugin.empty()&&text(m.plugin,256)&&finite(m.minimum,-1,1)&&finite(m.maximum,-1,1)&&songEdges.emplace(m.source,m.plugin,m.parameter).second,"Invalid or duplicate song modulation connection");
    if(m.enabled){auto [q,inserted]=quantization.emplace(std::make_pair(m.plugin,m.parameter),m.quantized);require(inserted||q->second==m.quantized,"All sources for a parameter must use the same quantization mode");}
  }
  std::set<uint64_t> assigned;
  for(const auto &a:assignments)require(target(a.target)&&definitions.contains(a.graph)&&assigned.insert(a.target).second&&finite(a.amount,0,1)&&finite(a.wet,0,1),"Invalid or duplicate ordinary subgraph assignment");
  assigned.clear();require(instrumentAssignments.size()<=128,"Use at most 128 instrument graphs");
  for(const auto &a:instrumentAssignments)require(std::find(instruments.begin(),instruments.end(),a.target)!=instruments.end()&&definitions.contains(a.graph)&&assigned.insert(a.target).second&&finite(a.amount,0,1)&&finite(a.wet,0,1),"Invalid or duplicate instrument graph assignment");
  for(const auto &[id,count]:lanes)require(target(id)&&count>0&&count<=8,"Graph lanes require an existing bus and one to eight columns");
  require(inputs.size()<=128&&outputs.size()<=128,"Too many external graph routes");
  auto hasPort=[&](uint64_t target,uint32_t port,bool input){std::set<uint64_t> used;for(const auto &a:assignments)if(a.target==target)used.insert(a.graph);for(const auto &c:commands)if(c.target==target&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start))used.insert(c.graph);
    for(const auto &d:library)if(used.contains(d.id))for(const auto &n:d.nodes)if(n.kind==(input?SignalNodeKind::Input:SignalNodeKind::Output))for(const auto &e:d.audio)if(input?(e.source==n.id&&e.output==port):(e.target==n.id&&e.input==port))return true;return false;};
  std::set<std::tuple<uint64_t,uint64_t,uint32_t>> inputRoutes;std::set<std::tuple<uint64_t,uint32_t,uint64_t>> outputRoutes;
  for(const auto &r:inputs)require(target(r.source)&&target(r.target)&&r.source!=r.target&&r.input>0&&r.input<64&&finite(r.gainDB,-96,12)&&hasPort(r.target,r.input,true)&&inputRoutes.emplace(r.source,r.target,r.input).second,"Invalid or duplicate external subgraph input route; assign a graph exposing that input first");
  for(const auto &r:outputs)require(target(r.source)&&target(r.target)&&r.source!=r.target&&r.output>0&&r.output<64&&hasPort(r.source,r.output,false)&&outputRoutes.emplace(r.source,r.output,r.target).second,"Invalid or duplicate external subgraph output route; assign a graph exposing that output first");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>> cells;
  for(const auto &c:commands) {
    require(patterns.contains(c.pattern)&&uint64_t(c.position)<uint64_t(patterns.at(c.pattern))*65536&&lanes.contains(c.target)&&c.column<lanes.at(c.target),"Graph command is outside its pattern or lane");
    require(uint8_t(c.kind)<=uint8_t(SignalCommandKind::Wet)&&finite(c.amount,0,1)&&finite(c.wet,0,1),"Invalid graph command type or amount");
    require(c.kind==SignalCommandKind::Clear?c.graph==0:definitions.contains(c.graph),"Graph command references an unknown subgraph");
    require(cells.emplace(c.pattern,c.target,c.position/65536,c.column).second,"Only one graph command per row and graph lane column");
  }
}
} // namespace Tracker
