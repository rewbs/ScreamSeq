#pragma once
#include "RecordedGestureTake.hpp"
#include <nlohmann/json.hpp>

namespace Tracker {
// Control-owner projections shared by both APIs. Summary never copies event
// payloads; paging preserves original order/timestamps and the playback Stop cut.
inline nlohmann::json recordedGestureSummary(const RecordedGestureTake *take,bool compatible=false) {
  using Json=nlohmann::json;
  if(!take)return {{"take",""},{"capturing",false},{"retained",false}};
  Json targets=Json::array(),sources=Json::array();
  for(size_t i=0;i<take->targetCount();++i) {
    const auto &target=take->target(i);const auto loss=target.loss();
    targets.push_back({{"plugin",target.key().plugin},{"parameter",target.key().parameter},
      {"eventCount",target.events().size()},{"unmapped",loss.unmapped},{"overflow",loss.overflow}});
  }
  for(const auto &provider:take->providers())sources.push_back({{"plugin",provider.plugin},{"lost",take->sourceLoss(provider.plugin)}});
  return {{"take",take->token()},{"baseRevision",take->revision()},
    {"startTime",std::to_string(take->startTime())},{"endTime",take->endTime()?Json(std::to_string(*take->endTime())):Json()},
    {"playbackEpoch",std::to_string(take->playbackEpoch())},{"capturing",!take->endTime()&&!take->completed()},
    {"completed",take->completed()},{"retained",true},{"compatible",compatible},{"eventCount",take->eventCount()},
    {"rejected",take->rejectedEvents()},{"failure",unsigned(take->failure())},{"targets",std::move(targets)},{"sources",std::move(sources)}};
}
inline nlohmann::json recordedGestureEvents(const RecordedGestureTake &take,const std::string &plugin,
    uint32_t parameter,size_t offset=0,size_t limit=128) {
  using Json=nlohmann::json;
  if(!limit||limit>512)throw std::invalid_argument("Parameter event page limit must be from 1 to 512");
  const RecordedGestureCapture *target=nullptr;
  for(size_t i=0;i<take.targetCount();++i)if(take.target(i).key().plugin==plugin&&take.target(i).key().parameter==parameter){target=&take.target(i);break;}
  if(!target)throw std::invalid_argument("Parameter take has no captured input for this target");
  if(offset>target->events().size())throw std::invalid_argument("Parameter event offset exceeds captured input");
  const auto stopped=take.stoppedEndTime();Json events=Json::array();
  const auto count=std::min(limit,target->events().size()-offset);
  for(size_t i=offset;i<offset+count;++i) {
    const auto &event=target->events()[i];
    events.push_back({{"timestamp",std::to_string(event.hostTime)},{"value",event.value},
      {"frame",event.frame&&(!stopped||event.hostTime<*stopped)?Json(std::to_string(*event.frame)):Json()}});
  }
  return {{"take",take.token()},{"plugin",plugin},{"parameter",parameter},{"offset",offset},
    {"total",target->events().size()},{"events",std::move(events)},{"sampleRate",48000}};
}
}
