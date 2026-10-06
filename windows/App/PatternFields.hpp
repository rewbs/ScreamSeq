#pragma once
#include "../Project/NativePatternJSON.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ScreamSeq::PatternFields {
using Json=nlohmann::json;
inline Json command(const Tracker::PatternCommand &c) {
  Json out={{"kind",Tracker::patternCommandKindName(c.kind)}};
  if(c.kind==Tracker::PatternCommandKind::TrackerEffect){out["effect"]=c.effect;out["parameter"]=c.parameter;return out;}
  out["offset"]=c.position%Tracker::performanceUnitsPerRow;
  if(c.kind==Tracker::PatternCommandKind::Native){PatternJSON::append(out,c);if(Tracker::nativePatternCommand(c.native).duration!=Tracker::NativePatternDuration::None)out["duration"]=c.duration;return out;}
  out["value"]=c.value;
  if(Tracker::isNudge(c.kind))out["durationBeats"]=c.durationBeats;
  if(Tracker::patternCommandDuration(c.kind)!=Tracker::NativePatternDuration::None)out["duration"]=c.duration;
  if(c.kind==Tracker::PatternCommandKind::ParameterSet||c.kind==Tracker::PatternCommandKind::ParameterSlide)out["binding"]=c.binding;
  if(c.kind==Tracker::PatternCommandKind::PitchSet||c.kind==Tracker::PatternCommandKind::PitchSlide)out["pitchRange"]=c.pitchRange;
  return out;
}
inline Json trackerField(){return {{"key","parameter"},{"storage","parameter"},{"name","Value"},{"type","integer"},{"unit","byte"},{"displayUnit","hex"},{"minimum",0},{"maximum",255},{"default",0},{"width",4},{"inline",true}};}
inline Json descriptor(const Json &value,const Json &catalog){
  if(value.value("kind",std::string("tracker"))=="tracker")return {{"kind","tracker"},{"parameters",Json::array({trackerField()})}};
  for(const auto &entry:catalog)if(entry.at("kind")==value.at("kind")&&(entry.at("kind")!="native"||entry.at("native")==value.at("native")))return entry;
  throw std::invalid_argument("FX command is absent from the native catalog");
}
inline Json fields(const Json &descriptor,bool inGrid){Json result=Json::array();for(const auto &f:descriptor.at("parameters"))if(!inGrid||f.value("inline",true))result.push_back(f);return result;}
inline Json get(const Json &command,const Json &field){const auto storage=field.at("storage").get<std::string>();return storage.starts_with("parameters.")?command.at("parameters").value(storage.substr(11),field.at("default")):command.value(storage,field.at("default"));}
inline void set(Json &command,const Json &field,const Json &value){const auto storage=field.at("storage").get<std::string>();if(storage.starts_with("parameters."))command["parameters"][storage.substr(11)]=value;else command[storage]=value;}
inline Json defaults(const Json &descriptor){
  Json result={{"kind",descriptor.value("kind",std::string("tracker"))}};
  if(result["kind"]=="tracker"){result["effect"]=descriptor.at("command");result["parameter"]=descriptor.at("suggestedParameter");return result;}
  if(result["kind"]=="native"){result["native"]=descriptor.at("native");result["parameters"]=Json::object();}
  for(const auto &f:descriptor.at("parameters")){Json value=f.at("default");if(f.at("type")=="integer"&&value.is_number())value=int64_t(value.get<double>());set(result,f,value);}return result;
}
inline bool beatRate(const Json &field,const Json &command){
  return field.value("unit",std::string())=="cycles-per-beat-or-hz"&&(!command.contains("parameters")||command.at("parameters").value("rateMode",std::string("beat"))=="beat");
}
inline double displayNumber(double raw,const Json &f,unsigned rowsPerBeat,bool rows,const Json &command={}){
  const auto unit=f.value("unit",std::string());const auto display=f.value("displayUnit",std::string());
  if(rows&&beatRate(f,command))return raw/std::max(1u,rowsPerBeat);
  if(unit=="row-units")return raw/(65536.0*(rows?1:std::max(1u,rowsPerBeat)));
  if(unit=="beats")return raw*(rows?std::max(1u,rowsPerBeat):1);
  return display=="percent"?raw*100:raw;
}
inline double rawNumber(double value,const Json &f,unsigned rowsPerBeat,bool rows,const Json &command={}){
  const auto unit=f.value("unit",std::string());const auto display=f.value("displayUnit",std::string());
  if(rows&&beatRate(f,command))value*=std::max(1u,rowsPerBeat);
  else if(unit=="row-units")value=std::round(value*65536.0*(rows?1:std::max(1u,rowsPerBeat)));
  else if(unit=="beats")value/=rows?std::max(1u,rowsPerBeat):1;
  else if(display=="percent")value/=100;
  if(!std::isfinite(value)||value<f.at("minimum").get<double>()||value>f.at("maximum").get<double>()||(f.at("type")=="integer"&&std::floor(value)!=value))throw std::invalid_argument("FX value is outside its allowed range");
  return value;
}
inline std::string text(const Json &command,const Json &field,unsigned rowsPerBeat,bool rows,bool grid=false){
  const auto value=get(command,field);if(value.is_string())return value.get<std::string>();if(value.is_boolean())return value.get<bool>()?"on":"off";
  std::ostringstream out;if(grid&&field.value("displayUnit",std::string())=="hex")out<<std::uppercase<<std::hex<<std::setfill('0')<<std::setw(4)<<value.get<unsigned>();
  else {const bool rate=field.value("unit",std::string())=="cycles-per-beat-or-hz";out<<std::setprecision(grid?(rate?5:6):15)<<displayNumber(value.get<double>(),field,rowsPerBeat,rows,command);
    if(grid&&rate)out<<(beatRate(field,command)?(rows?"/r":"/b"):"Hz");}return out.str();
}
inline std::string unit(const Json &field,bool rows,const Json &command={}){const auto raw=field.value("unit",std::string());if(raw=="cycles-per-beat-or-hz")return beatRate(field,command)?(rows?"cycles/row":"cycles/beat"):"Hz";return raw=="row-units"||raw=="beats"?(rows?"rows":"beats"):field.value("displayUnit",raw);}
struct Column {
  std::vector<float> widths{28.8f};
  void include(const Json &fields){if(widths.size()<fields.size())widths.resize(fields.size(),28.8f);for(size_t i=0;i<fields.size();++i)widths[i]=std::max(widths[i],float(std::clamp(fields[i].value("width",8),4,12))*7.2f);}
  float start(size_t index)const{float result=21.6f;for(size_t i=0;i<index&&i<widths.size();++i)result+=widths[i]+7.2f;return result;}
  float width()const{return start(widths.size());}
  size_t hit(float x)const{for(size_t i=0;i<widths.size();++i)if(x<start(i)+widths[i]+7.2f)return i;return widths.size()-1;}
};
}
