#pragma once
#include "RecordedGestureTake.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <array>
#include <limits>

namespace Tracker {
// Recovery archive only; not a public API request or a live-provider restore.
// Keep original times as decimal strings and omit unmapped frames (plist has
// no null). The retained array order is significant for same-frame gestures.
inline nlohmann::json recordedGestureRecovery(const Tracker::RecordedGestureTake::Review &take) {
  using Json=nlohmann::json;
  using Failure=Tracker::RecordedGestureTake::Failure;
  const auto reason=[&] {
    switch(take.failure) {
      case Failure::None:return "none";
      case Failure::Context:return "context";
      case Failure::Provider:return "provider";
      case Failure::Target:return "target";
      case Failure::Capacity:return "capacity";
      case Failure::Storage:return "storage";
      case Failure::Allocation:return "allocation";
    }
    throw std::invalid_argument("Unknown recorded gesture recovery failure");
  };
  Json result={{"version",1},{"reviewOnly",true},{"take",take.token},
    {"revision",take.revision},{"playbackEpoch",std::to_string(take.playbackEpoch)},
    {"startTime",std::to_string(take.startTime)},{"completed",take.completed},
    {"failure",reason()},{"rejected",std::to_string(take.rejected)},
    {"rack",take.rack},{"baseline",Json::array()},
    {"providers",Json::array()},{"targets",Json::array()}};
  if(take.endTime)result["endTime"]=std::to_string(*take.endTime);
  if(take.stoppedEndTime)result["stoppedEndTime"]=std::to_string(*take.stoppedEndTime);
  for(const auto &point:take.baseline)result["baseline"].push_back(
    {{"slot",point.slot},{"parameter",point.id},{"value",point.value},{"frame",std::to_string(point.frame)}});
  for(const auto &source:take.sources)result["providers"].push_back(
    {{"plugin",source.initial.plugin},{"incarnation",std::to_string(source.initial.source.incarnation)},
     {"initialDropped",std::to_string(source.initial.source.dropped)},
     {"dropped",std::to_string(source.dropped)},{"drained",source.drained}});
  for(const auto &target:take.targets) {
    Json events=Json::array();
    for(const auto &event:target.events) {
      Json value={{"hostTime",std::to_string(event.hostTime)},{"value",event.value}};
      if(event.frame)value["frame"]=std::to_string(*event.frame);
      events.push_back(std::move(value));
    }
    result["targets"].push_back({{"plugin",target.key.plugin},{"parameter",target.key.parameter},
      {"minimum",target.minimum},{"maximum",target.maximum},
      {"sourceLoss",std::to_string(target.loss.source)},{"overflow",std::to_string(target.loss.overflow)},
      {"unmapped",target.loss.unmapped},{"events",std::move(events)}});
  }
  return result;
}

// A detached review decoder, never a live-take constructor. Both native hosts
// can inspect the same archive without reusing another process's clock or
// provider incarnation. Refusal leaves the caller's original tree untouched;
// unknown versions/fields remain opaque preservation data, not playable input.
inline RecordedGestureTake::Review readRecordedGestureRecovery(const nlohmann::json &archive) {
  using Json=nlohmann::json;
  using Take=RecordedGestureTake;
  const auto need=[](bool okay) {
    if(!okay)throw std::invalid_argument("Plugin gesture recovery needs review; invalid or unsupported archive retained");
  };
  const auto object=[&](const Json &value,std::initializer_list<const char *> required,
                       std::initializer_list<const char *> optional={}) {
    need(value.is_object());
    for(const auto key:required)need(value.contains(key));
    for(const auto &[key,unused]:value.items()) {
      (void)unused;
      need(std::find(required.begin(),required.end(),key)!=required.end()||
        std::find(optional.begin(),optional.end(),key)!=optional.end());
    }
  };
  const auto text=[&](const Json &value,size_t maximum) {
    need(value.is_string());const auto &result=value.get_ref<const std::string &>();
    need(!result.empty()&&result.size()<=maximum&&result.find('\0')==std::string::npos);
    return result;
  };
  const auto decimal=[&](const Json &value) {
    const auto valueText=text(value,20);uint64_t result=0;
    need(valueText=="0"||(valueText[0]>='1'&&valueText[0]<='9'));
    const auto parsed=std::from_chars(valueText.data(),valueText.data()+valueText.size(),result);
    need(parsed.ec==std::errc{}&&parsed.ptr==valueText.data()+valueText.size());return result;
  };
  const auto integer=[&](const Json &value,uint64_t maximum) {
    need(value.is_number_integer()&&(value.is_number_unsigned()||value.get<int64_t>()>=0));
    const auto result=value.get<uint64_t>();need(result<=maximum);return result;
  };
  const auto boolean=[&](const Json &value) {need(value.is_boolean());return value.get<bool>();};
  const auto number=[&](const Json &value) {
    need(value.is_number());const auto precise=value.get<double>();
    need(std::isfinite(precise)&&std::abs(precise)<=std::numeric_limits<float>::max());
    const auto result=float(precise);
    // Archives contain exact float values. Never silently narrow foreign data.
    need(double(result)==precise);return result;
  };
  const auto array=[&](const Json &value,size_t maximum) {need(value.is_array()&&value.size()<=maximum);};
  object(archive,{"version","reviewOnly","take","revision","playbackEpoch","startTime","completed",
    "failure","rejected","rack","baseline","providers","targets"},{"endTime","stoppedEndTime"});
  need(integer(archive.at("version"),1)==1&&boolean(archive.at("reviewOnly")));
  Take::Review result;
  result.token=text(archive.at("take"),80);result.revision=text(archive.at("revision"),4096);
  result.playbackEpoch=decimal(archive.at("playbackEpoch"));result.startTime=decimal(archive.at("startTime"));
  need(result.playbackEpoch&&result.startTime);
  result.completed=boolean(archive.at("completed"));result.rejected=decimal(archive.at("rejected"));
  if(archive.contains("endTime")) {result.endTime=decimal(archive.at("endTime"));need(*result.endTime>=result.startTime);}
  if(archive.contains("stoppedEndTime"))result.stoppedEndTime=decimal(archive.at("stoppedEndTime"));
  const auto reason=text(archive.at("failure"),16);
  const std::array<std::pair<const char *,Take::Failure>,7> reasons{{
    {"none",Take::Failure::None},{"context",Take::Failure::Context},{"provider",Take::Failure::Provider},
    {"target",Take::Failure::Target},{"capacity",Take::Failure::Capacity},{"storage",Take::Failure::Storage},
    {"allocation",Take::Failure::Allocation}}};
  const auto failure=std::find_if(reasons.begin(),reasons.end(),[&](const auto &item){return reason==item.first;});
  need(failure!=reasons.end());result.failure=failure->second;
  array(archive.at("rack"),maximumNativePlugins);std::set<std::string> identities;
  for(const auto &plugin:archive.at("rack")) {
    auto id=text(plugin,128);need(identities.insert(id).second);result.rack.push_back(std::move(id));
  }
  array(archive.at("baseline"),maximumRecordedPoints);
  result.baseline.reserve(archive.at("baseline").size());
  for(const auto &point:archive.at("baseline")) {
    object(point,{"slot","parameter","value","frame"});
    const auto slot=integer(point.at("slot"),UINT32_MAX),frame=decimal(point.at("frame"));
    need(slot<result.rack.size()&&frame<=maximumRecordedFrame);
    result.baseline.push_back({uint32_t(slot),uint32_t(integer(point.at("parameter"),UINT32_MAX)),number(point.at("value")),frame});
  }
  array(archive.at("providers"),maximumNativePlugins);need(!archive.at("providers").empty());
  std::set<std::string> providers;
  for(const auto &provider:archive.at("providers")) {
    object(provider,{"plugin","incarnation","initialDropped","dropped","drained"});
    auto id=text(provider.at("plugin"),128);
    need(identities.contains(id)&&providers.insert(id).second);
    const auto incarnation=decimal(provider.at("incarnation")),initial=decimal(provider.at("initialDropped")),dropped=decimal(provider.at("dropped"));
    const auto drained=boolean(provider.at("drained"));
    need(incarnation&&dropped>=initial&&(!drained||result.endTime.has_value()));
    result.sources.push_back({{std::move(id),{incarnation,initial}},dropped,drained});
  }
  array(archive.at("targets"),RecordedGestureCapture::batchTargetCapacity);
  std::set<std::pair<std::string,uint32_t>> targets;size_t totalEvents=0;
  for(const auto &target:archive.at("targets")) {
    object(target,{"plugin","parameter","minimum","maximum","sourceLoss","overflow","unmapped","events"});
    const auto id=text(target.at("plugin"),128);const auto parameter=uint32_t(integer(target.at("parameter"),UINT32_MAX));
    need(providers.contains(id)&&targets.emplace(id,parameter).second);
    const auto source=std::find_if(result.sources.begin(),result.sources.end(),[&](const auto &item){return item.initial.plugin==id;});
    Take::Review::Target decoded;
    decoded.key={result.token,result.revision,id,parameter,result.playbackEpoch,source->initial.source.incarnation,result.startTime};
    decoded.minimum=number(target.at("minimum"));decoded.maximum=number(target.at("maximum"));need(decoded.minimum<=decoded.maximum);
    decoded.loss={decimal(target.at("sourceLoss")),decimal(target.at("overflow")),size_t(integer(target.at("unmapped"),RecordedGestureCapture::eventCapacity))};
    need(decoded.loss.source==source->dropped-source->initial.source.dropped);
    const auto &events=target.at("events");array(events,RecordedGestureCapture::eventCapacity);
    need(!events.empty()&&events.size()<=maximumRecordedPoints-totalEvents);totalEvents+=events.size();
    decoded.events.reserve(events.size());size_t unmapped=0;
    for(const auto &event:events) {
      object(event,{"hostTime","value"},{"frame"});
      RecordedGestureCapture::Event value{decimal(event.at("hostTime")),number(event.at("value")),{}};
      need(value.value>=decoded.minimum&&value.value<=decoded.maximum);
      need(!value.hostTime||(value.hostTime>=result.startTime&&(!result.endTime||value.hostTime<*result.endTime)));
      if(event.contains("frame")) {
        value.frame=decimal(event.at("frame"));
        need(value.hostTime&&*value.frame<=maximumRecordedFrame&&(!result.stoppedEndTime||value.hostTime<*result.stoppedEndTime));
      } else ++unmapped;
      decoded.events.push_back(value);
    }
    need(unmapped==decoded.loss.unmapped);result.targets.push_back(std::move(decoded));
  }
  if(result.completed)need(result.endTime.has_value()&&
    std::all_of(result.sources.begin(),result.sources.end(),[](const auto &source){return source.drained;})&&
    result.failure!=Take::Failure::Context&&result.failure!=Take::Failure::Provider&&result.failure!=Take::Failure::Target);
  return result;
}
}
