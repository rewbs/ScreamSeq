#include "GraphEditing.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
namespace {
void need(bool value,const char *why){if(!value)throw std::invalid_argument(why);}
uint16_t freeNumber(const SignalGraph &graph) {
  for(uint16_t n=1;n<=999;++n)if(std::none_of(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.number==n;}))return n;
  throw std::invalid_argument("No free subgraph number");
}
}
SignalCloneResult cloneSignalGraph(NativeSong &song,uint64_t graph,std::optional<std::string> name,uint16_t number) {
  auto next=song;
  const auto source=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graph;});
  need(source!=next.signal.library.end(),"Subgraph does not exist");need(next.signal.library.size()<128,"Use at most 128 subgraph definitions");
  auto copy=*source;compileSignal(copy);
  copy.id=next.makeEntity().id;copy.number=number?number:freeNumber(next.signal);
  need(copy.number<=999&&std::none_of(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.number==copy.number;}),"Subgraph number is already used or out of range");
  if(name){need(name->size()<=256&&name->find('\0')==std::string::npos,"Invalid subgraph name");copy.name=*name;}
  SignalCloneResult result;result.graph=copy.id;
  for(auto &node:copy.nodes){const auto old=node.id;node.id=next.makeEntity().id;result.identities.emplace(old,node.id);}
  for(auto &group:copy.groups){const auto old=group.id;group.id=next.makeEntity().id;result.identities.emplace(old,group.id);}
  for(auto &group:copy.groups){if(group.parent)group.parent=result.identities.at(group.parent);for(auto &id:group.nodes)id=result.identities.at(id);}
  remapSignalGroupDryRoutes(copy,result.identities);
  for(auto &edge:copy.audio){edge.source=result.identities.at(edge.source);edge.target=result.identities.at(edge.target);}
  for(auto &edge:copy.modulation){edge.source=result.identities.at(edge.source);edge.target=result.identities.at(edge.target);}
  std::map<std::string,std::string> keys;for(const auto &[from,to]:result.identities)keys["n"+std::to_string(from)]="n"+std::to_string(to);
  remapSignalPresentation(copy.presentation,keys);
  for(auto link:std::vector<EnvelopeLink>(next.envelopeLinks))if(link.target.kind==EnvelopeTargetKind::Graph&&result.identities.contains(link.target.owner)) {
    link.target.owner=result.identities.at(link.target.owner);next.envelopeLinks.push_back(std::move(link));
  }
  compileSignal(copy);next.signal.library.push_back(std::move(copy));song=std::move(next);return result;
}
SignalCloneResult makeSignalUseIndependent(NativeSong &song,uint64_t graph,uint64_t target,bool instrument,std::optional<std::string> name,uint16_t number) {
  const auto &assignments=instrument?song.signal.instrumentAssignments:song.signal.assignments;
  const bool assigned=std::any_of(assignments.begin(),assignments.end(),[&](const auto &a){return a.target==target&&a.graph==graph;});
  const bool commanded=!instrument&&std::any_of(song.signal.commands.begin(),song.signal.commands.end(),[&](const auto &c){return c.target==target&&c.graph==graph;});
  need(assigned||commanded,"This target does not use the selected subgraph");
  auto next=song;auto result=cloneSignalGraph(next,graph,std::move(name),number);
  for(auto &a:instrument?next.signal.instrumentAssignments:next.signal.assignments)if(a.target==target&&a.graph==graph)a.graph=result.graph;
  if(!instrument)for(auto &c:next.signal.commands)if(c.target==target&&c.graph==graph)c.graph=result.graph;
  song=std::move(next);return result;
}
void muteSignalSource(NativeSong &song,uint64_t graph,uint64_t id,bool muted) {
  SignalNode *node=nullptr;
  if(graph){for(auto &d:song.signal.library)if(d.id==graph)for(auto &n:d.nodes)if(n.id==id)node=&n;}
  else {for(auto &s:song.signal.songSources)if(s.node.id==id)node=&s.node;}
  need(node&&node->kind>=SignalNodeKind::LFO&&node->kind<=SignalNodeKind::Automation,"Select a modulation source to mute");node->muted=muted;
}
}
