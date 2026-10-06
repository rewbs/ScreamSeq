#pragma once
#include "windows/Project/ScratchGestureJSON.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include <functional>

namespace ScreamSeq {
inline size_t scratchGestureUses(const Tracker::NativeSong &native,uint16_t id){
  return std::count_if(native.performance.commands.begin(),native.performance.commands.end(),[&](const auto &c){return c.kind==Tracker::PatternCommandKind::Native&&c.native==Tracker::NativePatternOp::Scratch&&c.arguments[0]==id;});
}
// Called on the document worker after the common API revision guard. The same
// portable operation is exercised independently of the Win32 controller.
inline Api::Json scratchGestureOperation(Tracker::Document &document,const std::string &method,const Api::Json &params,
    const std::function<std::function<void()>(const Tracker::NativeSong &)> &prepare={},const std::function<void(const Tracker::NativeSong &)> &validate={},const std::function<void()> &stop={}) {
  using namespace ScratchJSON;
  try{
    if(method=="scratch.gestures.get"){
      keys(params,{});Json gestures=Json::array(),presets=Json::array();
      for(const auto &[id,value]:document.native().scratchGestures){auto entry=gesture(value);entry["id"]=id;entry["uses"]=scratchGestureUses(document.native(),id);gestures.push_back(std::move(entry));}
      for(const auto &seed:Tracker::scratchPresets()){auto entry=gesture(seed.gesture);entry["id"]=seed.id;presets.push_back(std::move(entry));}
      return {{"unitsPerCycle",Tracker::scratchCycleUnits},{"gestures",gestures},{"presets",presets},{"limits",{{"gestures",255},{"pointsPerLane",Tracker::maximumScratchPoints}}}};
    }
    require(method=="scratch.gestures.set"||method=="scratch.gestures.remove"||method=="scratch.gestures.clone","Unknown scratch gesture operation");
    require(document.editable(),"Document is not editable");
    const bool removing=method=="scratch.gestures.remove";
    const bool cloning=method=="scratch.gestures.clone";
    if(removing)keys(params,{"id","dryRun"});else if(cloning)keys(params,{"id","name","target","dryRun"});else keys(params,{"id","name","motion","fader","preset","dryRun"});
    require(!params.contains("dryRun")||params.at("dryRun").is_boolean(),"dryRun must be a boolean");const bool dry=params.value("dryRun",false);
    auto next=document.native();uint16_t id=0;
    if(params.contains("id"))id=uint16_t(integer(params.at("id"),1,255));
    else{require(!removing&&!cloning,"Scratch removal or clone requires id");for(uint16_t slot=1;slot<=255;++slot)if(!next.scratchGestures.contains(slot)){id=slot;break;}require(id!=0,"All 255 scratch gesture slots are in use");}
    if(cloning){
      std::optional<Tracker::ScratchPatternCell> target;
      if(params.contains("target")){
        const auto &t=params.at("target");keys(t,{"pattern","row","channel","column"});
        require(t.contains("pattern")&&t.contains("row")&&t.contains("channel")&&t.contains("column"),"Scratch target needs pattern, row, channel and column");
        const auto pattern=uint16_t(integer(t.at("pattern"),0,UINT16_MAX));const auto &song=document.song();require(song.Patterns.IsValidPat(pattern),"Pattern does not exist");
        target=Tracker::ScratchPatternCell{pattern,uint16_t(integer(t.at("row"),0,song.Patterns[pattern].GetNumRows()-1)),uint16_t(integer(t.at("channel"),0,song.GetNumChannels()-1)),uint8_t(integer(t.at("column"),0,Tracker::maximumEffectColumns-1))};
      }
      std::optional<std::string> name;if(params.contains("name"))name=Project::validatedNativeText(params.at("name"),256);
      id=Tracker::cloneScratchGesture(next,id,name,target);
    }
    else if(removing){require(next.scratchGestures.contains(id),"Scratch gesture does not exist");require(!scratchGestureUses(next,id),"Remove scratch commands using this gesture before deleting it");next.scratchGestures.erase(id);}
    else{
      Tracker::ScratchGesture value;
      if(params.contains("preset")){const auto key=Project::validatedNativeText(params.at("preset"),64);const auto presets=Tracker::scratchPresets();const auto found=std::find_if(presets.begin(),presets.end(),[&](const auto &p){return p.id==key;});require(found!=presets.end(),"Unknown scratch preset");value=found->gesture;}
      else if(auto found=next.scratchGestures.find(id);found!=next.scratchGestures.end())value=found->second;
      else require(params.contains("name")&&params.contains("motion")&&params.contains("fader"),"New scratch gesture needs name, motion and fader or a preset");
      if(params.contains("name"))value.name=Project::validatedNativeText(params.at("name"),256);
      if(params.contains("motion"))value.motion=decodePoints(params.at("motion"),true);
      if(params.contains("fader"))value.fader=decodePoints(params.at("fader"),true);
      Tracker::validateScratchGesture(value);next.scratchGestures[id]=std::move(value);
    }
    next.validate(document.song());if(validate)validate(next);const bool changed=next!=document.native();
    if(changed&&!dry){
      const bool patternEdit=cloning&&params.contains("target");
      auto publish=!patternEdit&&prepare?prepare(next):std::function<void()>{};
      if(patternEdit&&stop)stop();
      document.annotate([&](auto &native){native=std::move(next);},publish);
    }
    return {{"id",id},{"wouldChange",changed},{"dryRun",dry}};
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
}
}
