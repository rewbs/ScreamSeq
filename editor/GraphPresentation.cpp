#include "GraphPresentation.hpp"
#include "NoteRouting.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace Tracker {
namespace {
void require(bool yes,const char *why){if(!yes)throw std::invalid_argument(why);}
bool text(const std::string &s,size_t limit){return s.size()<=limit&&s.find('\0')==std::string::npos;}
bool coordinate(double v){return std::isfinite(v)&&v>=-100000&&v<=100000;}
}
size_t SignalPresentation::bytes()const {
  size_t n=sizeof(*this)+regions.size()*sizeof(SignalVisualRegion)+cables.size()*sizeof(SignalCableGeometry);
  for(const auto &r:regions){n+=r.id.size()+r.title.size()+r.text.size()+r.scope.size();for(const auto &key:r.nodes)n+=sizeof(key)+key.size();}
  for(const auto &key:collapsedNodes)n+=sizeof(key)+key.size();
  for(const auto &c:cables)n+=c.source.size()+c.target.size()+c.connection.size()+c.points.size()*sizeof(c.points.front());return n;
}
void SignalPresentation::validate()const {
  require(regions.size()<=128&&cables.size()<=2048,"Too many graph annotations or cable paths");
  require(collapsedNodes.size()<=8192,"Too many collapsed graph nodes");
  std::set<std::string> compact;
  for(const auto &key:collapsedNodes)require(!key.empty()&&text(key,512)&&compact.insert(key).second,"Invalid or duplicate collapsed graph node");
  std::set<std::string> ids,members;
  for(const auto &r:regions){
    require(!r.id.empty()&&text(r.id,128)&&ids.insert(r.id).second,"Invalid or duplicate graph annotation identity");
    require(text(r.title,1024)&&text(r.text,16384)&&text(r.scope,512),"Graph annotation text is too long");
    require(coordinate(r.x)&&coordinate(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>=120&&r.height>=60&&r.width<=100000&&r.height<=100000,"Invalid graph annotation bounds");
    require(r.color<=0xffffff&&r.nodes.size()<=8192,"Invalid graph annotation color or membership");
    require(!r.comment||r.nodes.empty(),"Comments cannot contain processors");
    require(!r.comment||!r.collapsed,"Comments do not collapse");
    for(const auto &key:r.nodes)require(!key.empty()&&text(key,512)&&members.insert(key).second,"A node can belong to only one visual frame");
  }
  std::set<std::tuple<std::string,std::string,uint32_t,uint32_t,bool,std::string>> paths;
  for(const auto &c:cables){
    require(!c.source.empty()&&!c.target.empty()&&text(c.source,512)&&text(c.target,512)&&c.source!=c.target,"Invalid cable geometry endpoints");
    require((c.output==signalNotePort&&c.input==signalNotePort&&!c.modulation)||(c.output<=63&&(c.modulation||c.input<=63)),"Invalid cable geometry ports");
    require(text(c.connection,128),"Cable geometry identity is too long");
    require(paths.emplace(c.source,c.target,c.output,c.input,c.modulation,c.connection).second,"Duplicate cable geometry");
    require(!c.points.empty()&&c.points.size()<=64,"Cable geometry needs 1…64 points");
    for(const auto &p:c.points)require(coordinate(p[0])&&coordinate(p[1]),"Invalid cable reroute position");
  }
}
void pruneSignalPresentation(SignalPresentation &p,const std::set<std::string> &available){
  std::erase_if(p.collapsedNodes,[&](const auto &key){return !available.contains(key);});
  for(auto &r:p.regions){std::erase_if(r.nodes,[&](const auto &key){return !available.contains(key);});if(!r.scope.empty()&&!available.contains(r.scope))r.scope.clear();}
  std::erase_if(p.cables,[&](const auto &c){return !available.contains(c.source)||!available.contains(c.target);});
}
void remapSignalPresentation(SignalPresentation &p,const std::map<std::string,std::string> &mapping){
  std::set<std::string> available;for(const auto &[from,to]:mapping)available.insert(from);pruneSignalPresentation(p,available);
  for(auto &r:p.regions){for(auto &node:r.nodes)node=mapping.at(node);if(!r.scope.empty()){auto found=mapping.find(r.scope);r.scope=found==mapping.end()?"":found->second;}}
  for(auto &key:p.collapsedNodes)key=mapping.at(key);
  for(auto &c:p.cables){c.source=mapping.at(c.source);c.target=mapping.at(c.target);}
}
void removeSignalPresentationNode(SignalPresentation &p,const std::string &key){
  std::erase(p.collapsedNodes,key);
  for(auto &r:p.regions){std::erase(r.nodes,key);if(r.scope==key)r.scope.clear();}
  std::erase_if(p.cables,[&](const auto &c){return c.source==key||c.target==key;});
}
void retargetSignalCableGeometry(SignalPresentation &p,const SignalCableGeometry &before,const SignalCableGeometry &after){
  auto same=[](const auto &a,const auto &b){return std::tie(a.source,a.target,a.output,a.input,a.modulation,a.connection)==std::tie(b.source,b.target,b.output,b.input,b.modulation,b.connection);};
  if(same(before,after))return;
  const bool occupied=std::any_of(p.cables.begin(),p.cables.end(),[&](const auto &c){return same(c,after);});
  if(occupied)std::erase_if(p.cables,[&](const auto &c){return same(c,before);});
  else for(auto &c:p.cables)if(same(c,before)){auto points=std::move(c.points);c=after;c.points=std::move(points);}
}
void replaceNoteAssignmentGeometry(SignalPresentation &p,const NoteRoute &route){
  const auto implicit="note-assignment:n"+std::to_string(route.source);
  for(auto &c:p.cables)if(c.connection==implicit)c.connection="note:n"+std::to_string(route.id);
}
void reconcileNoteCableGeometry(SignalPresentation &p,const NoteRouting &routing){
  std::erase_if(p.cables,[&](auto &c){
    if(c.connection.starts_with("note:n")){
      const auto route=std::find_if(routing.routes.begin(),routing.routes.end(),[&](const auto &r){return c.connection=="note:n"+std::to_string(r.id);});
      if(route==routing.routes.end())return true;
      c.source=(route->sourceKind==NoteSourceKind::Instrument?"note-instrument:n":"n")+std::to_string(route->source);
      c.target="plugin:"+route->plugin;c.output=c.input=signalNotePort;c.modulation=false;
    }else if(c.connection.starts_with("note-assignment:n")){
      return std::any_of(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),[&](auto id){return c.connection=="note-assignment:n"+std::to_string(id);});
    }
    return false;
  });
}
}
