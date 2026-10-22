#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace Tracker {
// Detached native-input evidence, shared by both hosts. A pending or unknown
// submission may already exist in the worker's recorded-gesture archive. This
// value must never be replayed, remapped to a new clock or made into manual state.
inline constexpr size_t maximumControlRecoveryEvents=1024;
inline constexpr const char *controlInputRecoveryKey="screamseqPluginControlRecovery";
inline nlohmann::json readControlInputRecovery(const nlohmann::json &value) {
  using Json=nlohmann::json;
  const auto require=[](bool valid){if(!valid)throw std::invalid_argument("Unsupported parameter input recovery evidence; original archive retained");};
  const auto object=[&](const Json &entry,std::initializer_list<const char *> keys){
    require(entry.is_object()&&entry.size()==keys.size());for(auto key:keys)require(entry.contains(key));
  };
  const auto text=[&](const Json &entry,size_t bound,bool empty=false)->const std::string &{
    require(entry.is_string());const auto &raw=entry.get_ref<const std::string &>();
    require((empty||!raw.empty())&&raw.size()<=bound&&raw.find('\0')==std::string::npos);return raw;
  };
  const auto decimal=[&](const Json &entry){const auto &raw=text(entry,20);uint64_t result=0;
    require(raw=="0"||(raw[0]>='1'&&raw[0]<='9'));const auto decoded=std::from_chars(raw.data(),raw.data()+raw.size(),result);
    require(decoded.ec==std::errc{}&&decoded.ptr==raw.data()+raw.size());return result;
  };
  const auto integer=[&](const Json &entry,uint64_t bound){
    require(entry.is_number_integer()&&(entry.is_number_unsigned()||entry.get<int64_t>()>=0));
    const auto result=entry.get<uint64_t>();require(result<=bound);return result;
  };
  object(value,{"version","reviewOnly","document","revision","take","accepted","lost","previewError","events"});
  require(integer(value.at("version"),1)==1&&value.at("reviewOnly").is_boolean()&&value.at("reviewOnly").get<bool>());
  text(value.at("document"),128);text(value.at("revision"),4096);text(value.at("take"),80);
  decimal(value.at("accepted"));decimal(value.at("lost"));text(value.at("previewError"),2048,true);
  const auto &events=value.at("events");require(events.is_array()&&events.size()<=maximumControlRecoveryEvents);
  for(const auto &event:events) {
    object(event,{"plugin","parameter","value","hostTime","state","error"});
    text(event.at("plugin"),128);integer(event.at("parameter"),UINT32_MAX);
    require(event.at("value").is_number()&&std::isfinite(event.at("value").get<double>()));
    require(decimal(event.at("hostTime"))!=0);const auto &state=text(event.at("state"),7);
    require(state=="pending"||state=="refused"||state=="unknown");text(event.at("error"),2048,true);
  }
  return value; // Independent owned data; no references into a live queue.
}
inline void overlayControlInputRecovery(nlohmann::json &preserved,const nlohmann::json &input) {
  auto archive=readControlInputRecovery(input);
  const auto current=preserved.find(controlInputRecoveryKey);
  auto entries=current==preserved.end()?nlohmann::json::array():*current;
  if(!entries.is_array())throw std::invalid_argument("Existing native input recovery archive is incompatible; retained");
  const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto &old){
    return old.is_object()&&old.value("document",nlohmann::json())==archive.at("document")&&
      old.value("revision",nlohmann::json())==archive.at("revision")&&old.value("take",nlohmann::json())==archive.at("take");
  });
  if(found==entries.end()) {
    if(entries.size()>=64)throw std::invalid_argument("Native input recovery archive is full; original evidence retained");
    entries.push_back(std::move(archive));
  }else {
    (void)readControlInputRecovery(*found); // Never overwrite a future/malformed matching archive.
    *found=std::move(archive);
  }
  preserved[controlInputRecoveryKey]=std::move(entries);
}
}
