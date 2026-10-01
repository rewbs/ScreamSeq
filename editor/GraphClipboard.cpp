#include "GraphClipboard.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Tracker {
namespace {
void need(bool value,const char *why){if(!value)throw std::invalid_argument(why);}
bool boundary(const SignalNode &n){return n.kind==SignalNodeKind::Input||n.kind==SignalNodeKind::Output;}
}
SignalDefinition copySignalSelection(const SignalDefinition &source,const std::vector<uint64_t> &ids) {
  compileSignal(source);need(!ids.empty(),"Select nodes to copy");std::set<uint64_t> selected(ids.begin(),ids.end()),groups;
  need(selected.size()==ids.size(),"Duplicate selected identity");
  for(auto id:ids){const auto n=std::find_if(source.nodes.begin(),source.nodes.end(),[&](const auto &n){return n.id==id;});
    if(n!=source.nodes.end()){need(!boundary(*n),"Graph input/output boundaries cannot be copied");continue;}
    need(std::any_of(source.groups.begin(),source.groups.end(),[&](const auto &g){return g.id==id;}),"Selected node no longer exists");groups.insert(id);}
  for(size_t pass=0;pass<=source.groups.size();++pass)for(const auto &group:source.groups)if(groups.contains(group.id)||groups.contains(group.parent)){groups.insert(group.id);selected.insert(group.nodes.begin(),group.nodes.end());}
  for(const auto &group:source.groups)selected.erase(group.id);
  need(!selected.empty(),"Select at least one processor or source to copy");
  auto copy=source;
  std::erase_if(copy.nodes,[&](const auto &n){return !boundary(n)&&!selected.contains(n.id);});
  std::erase_if(copy.audio,[&](const auto &e){return !selected.contains(e.source)||!selected.contains(e.target);});
  std::erase_if(copy.modulation,[&](const auto &e){return !selected.contains(e.source)||!selected.contains(e.target);});
  std::erase_if(copy.groups,[&](auto &g){std::erase_if(g.nodes,[&](auto id){return !selected.contains(id);});return !groups.contains(g.id);});
  for(auto &g:copy.groups)if(!groups.contains(g.parent))g.parent=0;
  std::set<std::string> keys;for(const auto &n:copy.nodes)if(!boundary(n))keys.insert("n"+std::to_string(n.id));for(const auto &g:copy.groups)keys.insert("n"+std::to_string(g.id));
  pruneSignalPresentation(copy.presentation,keys);compileSignal(copy);return copy;
}
SignalDefinition cutSignalSelection(NativeSong &song,uint64_t graph,const std::vector<uint64_t> &ids) {
  auto next=song;auto d=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graph;});need(d!=next.signal.library.end(),"Subgraph no longer exists");
  auto fragment=copySignalSelection(*d,ids);std::set<uint64_t> removed;for(const auto &n:fragment.nodes)if(!boundary(n))removed.insert(n.id);
  std::erase_if(d->nodes,[&](const auto &n){return removed.contains(n.id);});std::erase_if(d->audio,[&](const auto &e){return removed.contains(e.source)||removed.contains(e.target);});std::erase_if(d->modulation,[&](const auto &e){return removed.contains(e.source)||removed.contains(e.target);});
  pruneSignalGroups(*d);std::set<std::string> keys;for(const auto &n:d->nodes)keys.insert("n"+std::to_string(n.id));for(const auto &g:d->groups)keys.insert("n"+std::to_string(g.id));pruneSignalPresentation(d->presentation,keys);
  std::erase_if(next.envelopeLinks,[&](const auto &link){return link.target.kind==EnvelopeTargetKind::Graph&&removed.contains(link.target.owner);});compileSignal(*d);song=std::move(next);return fragment;
}
SignalCloneResult pasteSignalSelection(NativeSong &song,uint64_t graph,const SignalDefinition &fragment,const std::map<uint64_t,uint64_t> &patterns,uint64_t parent,double x,double y) {
  compileSignal(fragment);need(std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<=100000&&y<=100000,"Invalid paste position");
  auto next=song;auto found=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graph;});need(found!=next.signal.library.end(),"Destination subgraph no longer exists");auto &destination=*found;
  need(!parent||std::any_of(destination.groups.begin(),destination.groups.end(),[&](const auto &g){return g.id==parent;}),"Destination group no longer exists");
  auto copy=fragment;std::set<uint64_t> selected;for(const auto &n:copy.nodes)if(!boundary(n))selected.insert(n.id);
  need(!selected.empty(),"Clipboard has no processors or sources");
  std::erase_if(copy.nodes,boundary);std::erase_if(copy.audio,[&](const auto &e){return !selected.contains(e.source)||!selected.contains(e.target);});std::erase_if(copy.modulation,[&](const auto &e){return !selected.contains(e.source)||!selected.contains(e.target);});
  SignalCloneResult result;result.graph=graph;double minX=100000,minY=100000;
  for(const auto &n:copy.nodes){minX=std::min(minX,n.x);minY=std::min(minY,n.y);}for(const auto &g:copy.groups){minX=std::min(minX,g.x);minY=std::min(minY,g.y);}
  for(auto &n:copy.nodes){const auto old=n.id;n.id=next.makeEntity().id;result.identities[old]=n.id;n.x+=x-minX;n.y+=y-minY;
    for(auto &lane:n.envelopes){const auto p=patterns.find(lane.pattern);need(p!=patterns.end(),"Map every copied pattern envelope explicitly before pasting");need(std::any_of(next.patterns.begin(),next.patterns.end(),[&](const auto &v){return v.second.id==p->second;}),"A mapped pattern no longer exists");lane.pattern=p->second;}}
  for(auto &g:copy.groups){const auto old=g.id;g.id=next.makeEntity().id;result.identities[old]=g.id;g.x+=x-minX;g.y+=y-minY;}
  for(auto &g:copy.groups){g.parent=g.parent?result.identities.at(g.parent):parent;for(auto &id:g.nodes)id=result.identities.at(id);}
  for(auto &e:copy.audio){e.source=result.identities.at(e.source);e.target=result.identities.at(e.target);}for(auto &e:copy.modulation){e.source=result.identities.at(e.source);e.target=result.identities.at(e.target);}
  if(parent){auto &owner=*std::find_if(destination.groups.begin(),destination.groups.end(),[&](const auto &g){return g.id==parent;});for(const auto &n:copy.nodes)if(std::none_of(copy.groups.begin(),copy.groups.end(),[&](const auto &g){return std::find(g.nodes.begin(),g.nodes.end(),n.id)!=g.nodes.end();}))owner.nodes.push_back(n.id);}
  std::map<std::string,std::string> keys;for(auto [a,b]:result.identities)keys["n"+std::to_string(a)]="n"+std::to_string(b);remapSignalPresentation(copy.presentation,keys);
  // Presentation IDs are local strings, so give pasted annotations fresh names.
  for(auto &region:copy.presentation.regions){region.id="copy-"+std::to_string(next.makeEntity().id);region.x+=x-minX;region.y+=y-minY;}
  for(auto &c:copy.presentation.cables)for(auto &point:c.points){point[0]+=x-minX;point[1]+=y-minY;}
  destination.nodes.insert(destination.nodes.end(),copy.nodes.begin(),copy.nodes.end());destination.groups.insert(destination.groups.end(),copy.groups.begin(),copy.groups.end());destination.audio.insert(destination.audio.end(),copy.audio.begin(),copy.audio.end());destination.modulation.insert(destination.modulation.end(),copy.modulation.begin(),copy.modulation.end());
  destination.presentation.regions.insert(destination.presentation.regions.end(),copy.presentation.regions.begin(),copy.presentation.regions.end());destination.presentation.cables.insert(destination.presentation.cables.end(),copy.presentation.cables.begin(),copy.presentation.cables.end());destination.presentation.collapsedNodes.insert(destination.presentation.collapsedNodes.end(),copy.presentation.collapsedNodes.begin(),copy.presentation.collapsedNodes.end());
  compileSignal(destination);song=std::move(next);return result;
}
}
