#pragma once
#include "editor/PatternPerformance.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace ScreamSeq::PatternJSON {
using Json=nlohmann::json;
inline void require(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
inline Json parameters(const Tracker::PatternCommand &command){
  Tracker::validateNativePatternCommand(command);
  Json result=Json::object();size_t i=0;
  for(const auto &p:Tracker::nativePatternCommand(command.native).parameters){
    const auto value=command.arguments[i++];
    if(p.type==Tracker::NativePatternFieldType::Choice)result[std::string(p.key)]=p.choices[size_t(value)];
    else if(p.type==Tracker::NativePatternFieldType::Boolean)result[std::string(p.key)]=value!=0;
    else result[std::string(p.key)]=value;
  }
  return result;
}
inline void append(Json &object,const Tracker::PatternCommand &command){
  if(Tracker::isNudge(command.kind)){object.erase("duration");object["durationBeats"]=command.durationBeats;}
  if(command.kind!=Tracker::PatternCommandKind::Native)return;
  object["native"]=Tracker::nativePatternCommand(command.native).identifier;
  object["parameters"]=parameters(command);
}
inline void decode(Tracker::PatternCommand &command,const Json &object){
  if(Tracker::isNudge(command.kind)){
    const auto value=object.value("durationBeats",Json(1));require(value.is_number()&&!value.is_boolean(),"Nudge durationBeats must be numeric");command.durationBeats=value.get<double>();
  }else require(!object.contains("durationBeats"),"Only NF/NR use durationBeats");
  if(command.kind!=Tracker::PatternCommandKind::Native){
    require(!object.contains("native")&&!object.contains("parameters"),"Only native operations have named parameters");Tracker::validateNativePatternCommand(command);return;
  }
  require(object.contains("native")&&object.at("native").is_string()&&object.contains("parameters")&&object.at("parameters").is_object(),"Native operation and parameters are required");
  const auto &definition=Tracker::nativePatternCommand(object.at("native").get<std::string>());
  command.native=definition.operation;command.arguments=Tracker::nativePatternDefaults(command.native);
  for(auto entry=object.at("parameters").begin();entry!=object.at("parameters").end();++entry){
    size_t index=0;while(index<definition.parameters.size()&&definition.parameters[index].key!=entry.key())++index;
    require(index<definition.parameters.size(),"Unknown native parameter");const auto &p=definition.parameters[index];const auto &value=entry.value();double decoded=0;
    if(p.type==Tracker::NativePatternFieldType::Choice){
      require(value.is_string(),"Native choice parameter must use a named option");size_t choice=0;
      while(choice<p.choices.size()&&p.choices[choice]!=value.get_ref<const std::string &>())++choice;
      require(choice<p.choices.size(),"Unknown native parameter choice");decoded=double(choice);
    }else if(p.type==Tracker::NativePatternFieldType::Boolean){require(value.is_boolean(),"Native switch must be a boolean");decoded=value.get<bool>()?1:0;}
    else{require(value.is_number()&&!value.is_boolean(),"Native parameter must be numeric");decoded=value.get<double>();}
    command.arguments[index]=decoded;
  }
  Tracker::validateNativePatternCommand(command);
}
inline Json field(const Tracker::NativePatternField &p,bool native){
  const char *type=p.type==Tracker::NativePatternFieldType::Choice?"choice":p.type==Tracker::NativePatternFieldType::Boolean?"boolean":p.type==Tracker::NativePatternFieldType::Integer?"integer":"number";
  Json choices=Json::array();for(auto v:p.choices)choices.push_back(v);
  Json initial=p.initial;if(p.type==Tracker::NativePatternFieldType::Choice)initial=p.choices[size_t(p.initial)];else if(p.type==Tracker::NativePatternFieldType::Boolean)initial=p.initial!=0;
  return {{"key",p.key},{"storage",(native?"parameters.":"")+std::string(p.key)},{"name",p.name},{"type",type},{"unit",p.unit},{"displayUnit",p.displayUnit},
    {"minimum",p.minimum},{"maximum",p.maximum},{"default",initial},{"choices",choices},{"width",p.width},{"inline",p.inGrid}};
}
inline void timing(Json &entry,Tracker::NativePatternDuration duration,bool offsetInline,bool rowBoundary=false){
  entry["duration"]=duration==Tracker::NativePatternDuration::Required?"required":duration==Tracker::NativePatternDuration::Optional?"optional":"none";
  if(duration!=Tracker::NativePatternDuration::None){auto f=field({"duration","Duration","row-units","beats",duration==Tracker::NativePatternDuration::Required?1.0:0.0,4294967295.0,65536,Tracker::NativePatternFieldType::Integer},false);f["unitsPerRow"]=65536;entry["parameters"].push_back(std::move(f));}
  auto offset=field({"offset","Offset","row-units","beats",0,rowBoundary?0.0:65535.0,0,Tracker::NativePatternFieldType::Integer,{},8,offsetInline},false);offset["unitsPerRow"]=65536;entry["parameters"].push_back(std::move(offset));
}
inline Json catalog(){
  Json result=Json::array();
  constexpr const char *codes[]{"PS","PL","BS","BL","NC","","NF","NR"};
  constexpr const char *names[]{"Parameter set","Parameter slide","Pitch set","Pitch slide","Note cut","","Nudge forward","Nudge reverse"};
  constexpr const char *descriptions[]{
    "Set a writable plugin parameter through its stable binding at an exact row offset.",
    "Slide a writable plugin parameter to an exact value over the chosen duration.",
    "Set sample pitch or plugin MIDI pitch bend in semitones; match the instrument's MIDI bend range.",
    "Slide sample pitch or plugin MIDI pitch bend in semitones; match the instrument's MIDI bend range.",
    "Cut this channel's sample or release its plugin notes at the chosen offset. Plugin release tails remain.","",
    "Push sample playback forward. Samples only; an opposing push above 50% reverses it. Duration includes recovery.",
    "Pull sample playback backward. Samples only; below 50% slows forward playback, above 50% reverses it. Duration includes recovery."};
  constexpr const char *scopes[]{"plugin parameter","plugin parameter","sample/plugin pitch","sample/plugin pitch","sample/plugin note","","sample voice","sample voice"};
  constexpr const char *lifetimes[]{"until replaced","duration then hold","until replaced","duration then hold","instant","","duration including recovery","duration including recovery"};
  for(unsigned k=0;k<8;++k){if(k==unsigned(Tracker::PatternCommandKind::TrackerEffect))continue;const auto kind=Tracker::PatternCommandKind(k);
    Json fields=Json::array();for(const auto &p:Tracker::patternCommandFields(kind))fields.push_back(field(p,false));
    Json entry={{"kind",Tracker::patternCommandKindName(kind)},{"displayCode",codes[k]},{"name",names[k]},{"parameters",fields},{"family",k<2?"parameter":"sound"},{"description",descriptions[k]},{"scope",scopes[k]},{"lifetime",lifetimes[k]}};
    timing(entry,Tracker::patternCommandDuration(kind),kind==Tracker::PatternCommandKind::NoteCut);
    if(Tracker::isNudge(kind)){entry["duration"]="required";entry["durationStorage"]="durationBeats";entry["durationUnit"]="beats";}
    result.push_back(std::move(entry));
  }
  for(const auto &c:Tracker::nativePatternCommands()){
    Json fields=Json::array();for(const auto &p:c.parameters)fields.push_back(field(p,true));
    Json entry={{"kind","native"},{"native",c.identifier},{"displayCode",c.code},{"name",c.name},{"family",c.family},{"description",c.description},
      {"scope",c.scope},{"lifetime",c.lifetime},{"equivalents",c.equivalents},{"parameters",fields}};
    timing(entry,c.duration,c.offsetInGrid,c.operation==Tracker::NativePatternOp::RowLength);result.push_back(std::move(entry));
  }
  return result;
}
}
