#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>

namespace ScreamSeq::GraphCableEdits {
using Json=nlohmann::json;
struct Endpoint {std::string node;bool output=false,modulation=false;uint32_t port=0;};
struct Result {Json edges;size_t selected=0;bool changed=false;};
inline bool sameEdge(const Json &a,const Json &b,bool modulation) {
  return a.at("source")==b.at("source")&&a.at("target")==b.at("target")&&
    (modulation?a.at("parameter")==b.at("parameter"):(a.value("output",0u)==b.value("output",0u)&&a.value("input",0u)==b.value("input",0u)));
}
// Socket drags always add. Only a captured, explicitly selected cable can be
// replaced; colliding with another cable leaves both original cables intact.
inline Result connect(const Json &edges,Endpoint first,Endpoint second,const std::optional<Json> &replace={}) {
  if(first.output==second.output||first.modulation!=second.modulation||first.node==second.node)
    throw std::invalid_argument("Connect an output to a different node's matching input");
  if(!first.output)std::swap(first,second);
  const bool modulation=first.modulation;Result result{edges};auto old=result.edges.end();
  if(replace){old=std::find_if(result.edges.begin(),result.edges.end(),[&](const auto &e){return sameEdge(e,*replace,modulation);});
    if(old==result.edges.end()||*old!=*replace)throw std::invalid_argument("Selected cable changed; select it again before rewiring");}
  Json edge=replace?*replace:Json::object();edge["source"]=first.node;edge["target"]=second.node;
  if(modulation){edge["parameter"]=second.port;if(!replace){edge["minimum"]=0;edge["maximum"]=1;edge["base"]=0;edge["enabled"]=true;}
    const Json *peer=nullptr;for(const auto &e:edges)if(e.at("target")==second.node&&e.at("parameter")==second.port){peer=&e;if(e.value("enabled",true))break;}
    if(peer){edge["base"]=peer->value("base",0.0);edge["quantized"]=peer->value("quantized",false);}}
  else{edge["output"]=first.port;edge["input"]=second.port;if(!replace)edge["gain"]=1;}
  auto found=std::find_if(result.edges.begin(),result.edges.end(),[&](const auto &e){return sameEdge(e,edge,modulation);});
  if(found!=result.edges.end()&&(!replace||found!=old)){
    if(replace)throw std::invalid_argument("That cable already exists; existing cables were kept");
    result.selected=size_t(found-result.edges.begin());return result;
  }
  if(replace){result.selected=size_t(old-result.edges.begin());result.changed=*old!=edge;*old=std::move(edge);}
  else{result.selected=result.edges.size();result.edges.push_back(std::move(edge));result.changed=true;}
  return result;
}
// A selected wire is not a draft for a new socket cable. Endpoint identity
// determines its kind; only an explicitly edited compatible draft supplies
// port/gain settings or requests the aggregate graph's auxiliary ports.
inline int freshSongRoute(bool sourcePlugin,bool targetPlugin,bool sourceGraph,bool targetGraph,int preferred,bool explicitDraft) {
  const bool graphInput=explicitDraft&&preferred==2&&!sourcePlugin&&!sourceGraph&&!targetPlugin;
  const bool graphOutput=explicitDraft&&preferred==3&&!sourcePlugin&&!targetPlugin&&!targetGraph;
  if(sourceGraph||targetGraph){
    if(graphInput&&targetGraph)return 2;
    if(graphOutput&&sourceGraph)return 3;
    throw std::invalid_argument("Choose Graph sidechain or Graph auxiliary to address an aggregate stage port");
  }
  if(graphInput)return 2;
  if(graphOutput)return 3;
  return sourcePlugin?(targetPlugin?6:5):targetPlugin?4:1;
}
// The API's targets array replaces one output's destinations. This merge keeps
// every sibling and materializes an instrument's implicit main route on Add.
inline Json pluginTargets(const Json &routes,const std::string &plugin,uint32_t output,
                          const std::string &implicitMain,const std::optional<std::string> &oldTarget,
                          const std::optional<std::string> &newTarget) {
  Json targets=Json::array();bool explicitOutput=false;
  for(const auto &r:routes)if(r.at("plugin")==plugin&&r.at("output")==output){explicitOutput=true;if(r.at("target")!="")targets.push_back(r.at("target"));}
  if(!explicitOutput&&output==0&&!implicitMain.empty())targets.push_back(implicitMain);
  if(oldTarget){auto old=std::find(targets.begin(),targets.end(),*oldTarget);if(old==targets.end())throw std::invalid_argument("Selected output cable changed; reload before rewiring");
    if(newTarget&&*newTarget!=*oldTarget&&std::find(targets.begin(),targets.end(),*newTarget)!=targets.end())throw std::invalid_argument("That output cable already exists; existing cables were kept");
    if(newTarget)*old=*newTarget;else targets.erase(old);}
  else if(newTarget&&std::find(targets.begin(),targets.end(),*newTarget)==targets.end())targets.push_back(*newTarget);
  return targets;
}
inline Json addSend(const Json &sends,const std::string &target,double gain=0,bool preFader=false,bool enabled=true) {
  auto result=sends;const auto found=std::find_if(result.begin(),result.end(),[&](const auto &s){return s.at("target")==target;});
  if(found==result.end())result.push_back({{"target",target},{"gainDB",gain},{"preFader",preFader},{"enabled",enabled}});
  return result;
}
}
