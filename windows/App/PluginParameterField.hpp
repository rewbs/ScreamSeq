#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <charconv>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>

namespace ScreamSeq {
// Catalogue interpretation for native rack fields. Values remain in the API's
// native units; this type owns no document, plugin, history or device state.
struct PluginParameterField {
  enum class Kind { Number, Choice, Toggle };
  Kind kind=Kind::Number;
  double minimum=0,maximum=0,value=0,step=0;
  bool valid=false,writable=false;
  std::string name,unit;
  std::vector<std::string> choices;
  // Stored host scalars are floats, promoted by JSON to double. Show the
  // shortest round-tripping scalar rather than exposing that promotion's
  // binary tail. Genuine double metadata keeps its full round-trip precision.
  // Only generated text uses this; never rewrite a user's retained draft.
  static std::string text(double value) {
    char buffer[64];
    const bool inFloatRange=std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max();
    const auto scalar=inFloatRange?static_cast<float>(value):0.f;
    const auto result=inFloatRange&&double(scalar)==value
      ?std::to_chars(buffer,buffer+sizeof(buffer),scalar)
      :std::to_chars(buffer,buffer+sizeof(buffer),value);
    if(result.ec!=std::errc{})throw std::runtime_error("Cannot format plugin parameter");
    return {buffer,result.ptr};
  }
  static PluginParameterField read(const nlohmann::json &p) {
    PluginParameterField field;
    try {
      field.name=p.at("name").get<std::string>();field.unit=p.value("unitLabel",std::string());
      for(const auto *key:{"min","max","value"})if(!p.at(key).is_number())return field;
      if(p.contains("manualValue")&&!p.at("manualValue").is_number())return field;
      if(p.contains("step")&&!p.at("step").is_number())return field;
      if(p.contains("unit")&&(!p.at("unit").is_number_integer()||p.at("unit").get<double>()<0||p.at("unit").get<double>()>4294967295.0))return field;
      field.minimum=p.at("min").get<double>();field.maximum=p.at("max").get<double>();
      field.value=p.contains("manualValue")?p.at("manualValue").get<double>():p.at("value").get<double>();
      field.step=p.value("step",0.0);field.writable=p.value("writable",false);
      field.choices=p.value("choices",std::vector<std::string>());
      field.valid=std::isfinite(field.minimum)&&std::isfinite(field.maximum)&&field.minimum<=field.maximum&&
        std::isfinite(field.maximum-field.minimum)&&std::isfinite(field.value)&&field.value>=field.minimum&&field.value<=field.maximum&&
        std::isfinite(field.step)&&field.step>=0;
      if(!field.valid)return field;
      // Public choice values are zero-based integers. Malformed vendor labels
      // must never silently select or clamp a different musical setting.
      if(!field.choices.empty()) {
        field.valid=field.choices.size()<=4096&&field.minimum==0&&field.maximum==double(field.choices.size()-1)&&std::floor(field.value)==field.value;
        field.kind=Kind::Choice;
      }
      if(p.value("unit",0u)==2u) {
        field.valid=field.valid&&field.minimum==0&&field.maximum==1&&std::floor(field.value)==field.value&&
          (field.choices.empty()||field.choices.size()==2);
        field.kind=Kind::Toggle;
        if(field.choices.empty())field.choices={"Off","On"};
      }
    } catch(const nlohmann::json::exception &) {field.valid=false;}
    return field;
  }
  bool editable()const noexcept{return valid&&writable;}
};
}
