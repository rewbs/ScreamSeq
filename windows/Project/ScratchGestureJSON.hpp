#pragma once
#include "NativeMetadata.hpp"
#include "editor/ScratchGesture.hpp"
#include <array>

namespace ScreamSeq::ScratchJSON {
using Json=nlohmann::json;
inline void require(bool condition,const char *message){if(!condition)throw std::invalid_argument(message);}
inline void keys(const Json &value,std::initializer_list<const char *> allowed){
  require(value.is_object(),"Expected a scratch gesture object");
  for(auto i=value.begin();i!=value.end();++i)require(std::any_of(allowed.begin(),allowed.end(),[&](const auto *key){return i.key()==key;}),"Unknown scratch gesture field");
}
inline uint32_t integer(const Json &value,uint32_t low,uint32_t high){
  require(value.is_number_integer()&&value>=low&&value<=high,"Scratch integer is outside its range");return value.get<uint32_t>();
}
inline constexpr std::array<const char *,9> curves{"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
inline Json points(const std::vector<Tracker::AutomationPoint> &values){
  Json result=Json::array();for(const auto &p:values){require(size_t(p.curve)<curves.size(),"Invalid scratch curve");Json value{{"position",p.position},{"value",p.value},{"curve",curves[size_t(p.curve)]}};if(!p.formula.source().empty())value["formula"]=p.formula.source();result.push_back(std::move(value));}return result;
}
inline std::vector<Tracker::AutomationPoint> decodePoints(const Json &values,bool strict){
  require(values.is_array()&&values.size()>=2&&values.size()<=Tracker::maximumScratchPoints,"Scratch lane needs 2..256 points");
  std::vector<Tracker::AutomationPoint> result;result.reserve(values.size());
  for(const auto &p:values){
    require(p.is_object()&&p.contains("position")&&p.contains("value"),"Scratch point needs position and value");if(strict)keys(p,{"position","value","curve","formula"});
    const auto &value=p.at("value");require(value.is_number()&&std::isfinite(value.get<double>())&&value>=0&&value<=1,"Scratch point value must be 0..1");
    const auto curve=Project::validatedNativeText(p.value("curve",Json("linear")),32);const auto found=std::find(curves.begin(),curves.end(),curve);require(found!=curves.end(),"Unknown scratch curve");
    Tracker::AutomationPoint point{integer(p.at("position"),0,Tracker::scratchCycleUnits),value.get<double>(),Tracker::AutomationCurve(found-curves.begin())};
    if(p.contains("formula"))point.formula=Tracker::CurveFormula(Project::validatedNativeText(p.at("formula"),2048));result.push_back(std::move(point));
  }return result;
}
inline Json gesture(const Tracker::ScratchGesture &value){
  Tracker::validateScratchGesture(value);return {{"name",value.name},{"motion",points(value.motion)},{"fader",points(value.fader)}};
}
inline Tracker::ScratchGesture decode(const Json &value,bool strict=false){
  require(value.is_object()&&value.contains("name")&&value.contains("motion")&&value.contains("fader"),"Scratch gesture needs name, motion and fader");
  if(strict)keys(value,{"id","name","motion","fader"});
  Tracker::ScratchGesture result{Project::validatedNativeText(value.at("name"),256),decodePoints(value.at("motion"),strict),decodePoints(value.at("fader"),strict)};Tracker::validateScratchGesture(result);return result;
}
struct ClipboardGestures {
  std::map<uint16_t,Tracker::ScratchGesture> source;
  std::map<uint16_t,uint16_t> assigned;
  explicit ClipboardGestures(const Json &values){
    require(values.is_array()&&values.size()<=255,"Invalid clipboard scratch gesture bank");
    for(const auto &entry:values){require(entry.is_object()&&entry.contains("id"),"Clipboard scratch gesture needs id");const auto id=uint16_t(integer(entry.at("id"),1,255));require(source.emplace(id,decode(entry,true)).second,"Duplicate clipboard scratch gesture slot");}
  }
  uint16_t remap(Tracker::ScratchGestureLibrary &target,uint16_t id){
    require(source.contains(id),"Clipboard scratch command needs its gesture phrase");
    if(assigned.contains(id))return assigned.at(id);
    const auto &value=source.at(id);uint16_t slot=0;
    if(auto same=target.find(id);same!=target.end()&&same->second==value)slot=id;
    if(!slot)for(const auto &[key,existing]:target)if(existing==value){slot=key;break;}
    if(!slot){for(uint16_t free=1;free<=255;++free)if(!target.contains(free)){slot=free;break;}require(slot!=0,"All 255 scratch gesture slots are in use");target.emplace(slot,value);}
    assigned[id]=slot;return slot;
  }
};
}
