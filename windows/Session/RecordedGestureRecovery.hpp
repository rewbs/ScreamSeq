#pragma once
#include "editor/hosted/RecordedGestureTake.hpp"
#include <nlohmann/json.hpp>

namespace ScreamSeq {
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
}
