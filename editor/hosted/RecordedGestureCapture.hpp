#pragma once
#include "RecordedCapture.hpp"
#include "editor/NativeSong.hpp"
#include "editor/RecordingFrameClock.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace Tracker {
// One serialized control owner, never the audio callback. This retains a single
// target gesture until the native adapter atomically publishes points/history.
// Native input, provider incarnation, recovery serialization and UI stay native.
class RecordedGestureCapture {
public:
  static constexpr size_t eventCapacity=8192;
  struct Key {
    std::string token,revision,plugin;
    uint32_t parameter=0;
    uint64_t playbackEpoch=0,providerEpoch=0,startTime=0;
    bool operator==(const Key &)const=default;
  };
  struct Event {
    uint64_t hostTime=0;
    float value=0;
    std::optional<uint64_t> frame;
  };
  struct Loss {
    uint64_t source=0,overflow=0;
    size_t unmapped=0;
    bool any()const noexcept{return source||overflow||unmapped;}
  };
  class Candidate {
    friend class RecordedGestureCapture;
    std::vector<ParameterChange> points_;
    Loss loss_;
    bool changed_=false;
    Key key;
    uint64_t version=0;
    const RecordedGestureCapture *owner=nullptr;
  public:
    const std::vector<ParameterChange> &points()const noexcept{return points_;}
    const Loss &loss()const noexcept{return loss_;}
    bool changed()const noexcept{return changed_;}
  };
private:
  Key key_;
  std::shared_ptr<const RecordingFrameTimeline> timeline_;
  std::vector<std::string> rackIDs_; // No vendor blobs duplicated into the input log.
  std::vector<ParameterChange> before_;
  std::vector<Event> events_;
  std::vector<size_t> pending_;
  float minimum_=0,maximum_=0;
  uint64_t initialSourceLoss_=0,sourceLoss_=0,overflow_=0,version_=1;
  std::optional<uint64_t> endTime_;
  bool retired_=false;

  static const PluginParameter &parameter(std::span<const PluginParameter> catalog,uint32_t id) {
    const auto p=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.id==id;});
    if(p==catalog.end()||!p->writable||!std::isfinite(p->min)||!std::isfinite(p->max)||p->min>p->max)
      throw std::invalid_argument("Recorded gesture parameter is unavailable or read-only");
    return *p;
  }
  static void conflict(const NativeSong &native,const Key &key) {
    if(std::any_of(native.automation.begin(),native.automation.end(),[&](const auto &lane){
      return lane.enabled&&lane.plugin==key.plugin&&lane.parameter==key.parameter;}))
      throw std::invalid_argument("Disable this parameter's envelopes before recording gestures");
    if(native.performance.controls(key.plugin,key.parameter))
      throw std::invalid_argument("Remove this parameter's pattern commands before recording gestures");
  }
  void active()const {
    if(retired_)throw std::logic_error("Recorded gesture has already been published");
  }
  void epoch(uint64_t playback,uint64_t provider)const {
    if(playback!=key_.playbackEpoch||provider!=key_.providerEpoch)
      throw std::invalid_argument("Recorded gesture belongs to an earlier playback or plugin incarnation");
  }
public:
  RecordedGestureCapture(Key key,std::shared_ptr<const RecordingFrameTimeline> timeline,
      std::span<const PluginState> rack,std::span<const ParameterChange> before,
      const NativeSong &native,std::span<const PluginParameter> catalog,uint64_t sourceLoss)
    :key_(std::move(key)),timeline_(std::move(timeline)),initialSourceLoss_(sourceLoss),sourceLoss_(sourceLoss) {
    if(key_.token.empty()||key_.token.size()>80||key_.token.find('\0')!=std::string::npos||
       key_.revision.empty()||key_.revision.size()>4096||key_.revision.find('\0')!=std::string::npos||
       key_.plugin.empty()||key_.plugin.size()>128||key_.plugin.find('\0')!=std::string::npos||
       !key_.playbackEpoch||!key_.providerEpoch||!key_.startTime||
       !timeline_||timeline_->generation()!=key_.playbackEpoch||timeline_->stopped())
      throw std::invalid_argument("Recorded gesture needs a current document, target and playback clock");
    if(rack.size()>maximumNativePlugins||before.size()>maximumRecordedPoints||
       std::count_if(rack.begin(),rack.end(),[&](const auto &p){return p.instanceID==key_.plugin;})!=1)
      throw std::invalid_argument("Recorded gesture target or capacity is invalid");
    conflict(native,key_);
    const auto &p=parameter(catalog,key_.parameter);minimum_=p.min;maximum_=p.max;
    for(const auto &state:rack)rackIDs_.push_back(state.instanceID);
    before_.assign(before.begin(),before.end());
    events_.reserve(eventCapacity);pending_.reserve(eventCapacity);
  }
  RecordedGestureCapture(const RecordedGestureCapture &)=delete;
  RecordedGestureCapture &operator=(const RecordedGestureCapture &)=delete;
  const Key &key()const noexcept{return key_;}
  std::span<const Event> events()const noexcept{return events_;}
  bool retired()const noexcept{return retired_;}
  bool sealed()const noexcept{return endTime_.has_value();}
  Loss loss()const noexcept {
    return {sourceLoss_-initialSourceLoss_,overflow_,
      size_t(std::count_if(events_.begin(),events_.end(),[](const auto &e){return !e.frame;}))};
  }
  // False means outside this gesture's time bounds, or overflow (distinguished
  // by loss()). A zero timestamp is retained as unmapped, never replaced by now.
  bool append(const PluginEdit &edit,uint64_t playbackEpoch,uint64_t providerEpoch) {
    active();epoch(playbackEpoch,providerEpoch);
    if(edit.id!=key_.parameter||!std::isfinite(edit.value)||edit.value<minimum_||edit.value>maximum_)
      throw std::invalid_argument("Recorded gesture event has another target or invalid value");
    if(edit.hostTime&&(edit.hostTime<key_.startTime||(endTime_&&edit.hostTime>=*endTime_)))return false;
    if(events_.size()==eventCapacity) {
      if(overflow_!=UINT64_MAX)++overflow_;
      ++version_;return false;
    }
    const auto frame=timeline_->locate(edit.hostTime,key_.playbackEpoch);
    events_.push_back({edit.hostTime,edit.value,frame});
    if(edit.hostTime&&!frame)pending_.push_back(events_.size()-1);
    ++version_;return true;
  }
  void sourceLoss(uint64_t count,uint64_t providerEpoch) {
    active();epoch(key_.playbackEpoch,providerEpoch);
    if(count<sourceLoss_)throw std::invalid_argument("Plugin loss counter belongs to an earlier incarnation");
    if(count!=sourceLoss_){sourceLoss_=count;++version_;}
  }
  // Called while draining or after another callback has provided coverage.
  // Already mapped points survive device retirement; unresolved times remain
  // visible and prevent normal publication. No polling-time approximation.
  void resolve() {
    active();
    // Stop can shorten previously published presentation coverage. A cached
    // mapping beyond that final bound is not recorded music. Keep the raw
    // event visible as unmapped; ring eviction alone never invalidates a frame.
    if(const auto end=timeline_->stoppedEndTime(key_.playbackEpoch))
      for(auto &event:events_)if(event.frame&&event.hostTime>=*end){event.frame.reset();++version_;}
    std::erase_if(pending_,[&](size_t index) {
      auto &event=events_[index];
      if(auto frame=timeline_->locate(event.hostTime,key_.playbackEpoch)) {
        event.frame=*frame;++version_;return true;
      }
      return false;
    });
  }
  // Stop/release establishes a bound; delayed events from before it may still
  // drain. Native adapters must finish draining before preparing publication.
  void seal(uint64_t endTime) {
    active();
    if(endTime<key_.startTime||(endTime_&&*endTime_!=endTime)||
       std::any_of(events_.begin(),events_.end(),[&](const auto &e){return e.hostTime&&e.hostTime>=endTime;}))
      throw std::invalid_argument("Recorded gesture end precedes retained events");
    if(!endTime_){endTime_=endTime;++version_;}
  }
  Candidate prepare(const std::string &revision,uint64_t playbackEpoch,uint64_t providerEpoch,
      std::span<const PluginState> rack,const NativeSong &native,
      std::span<const PluginParameter> catalog,bool acknowledgeLoss=false) {
    active();epoch(playbackEpoch,providerEpoch);
    if(!endTime_||revision!=key_.revision||rack.size()!=rackIDs_.size()||
       !std::equal(rack.begin(),rack.end(),rackIDs_.begin(),[](const auto &state,const auto &id){return state.instanceID==id;}))
      throw std::invalid_argument("Recorded gesture target or document changed; retain it for review");
    conflict(native,key_);
    const auto &p=parameter(catalog,key_.parameter);
    if(p.min!=minimum_||p.max!=maximum_)
      throw std::invalid_argument("Recorded gesture parameter range changed");
    resolve();
    Candidate result;result.loss_=loss();
    if(result.loss_.any()&&!acknowledgeLoss)
      throw std::runtime_error("Recorded gesture has lost or unmapped events; retain it for review");
    const auto target=std::find_if(rack.begin(),rack.end(),[&](const auto &s){return s.instanceID==key_.plugin;});
    const auto slot=uint32_t(target-rack.begin());
    result.points_=before_;
    // Only index this gesture's target. Preserve other slots/parameters and the
    // original point order. Last accepted input wins at the same canonical time.
    std::map<uint64_t,size_t> at;
    for(size_t i=0;i<result.points_.size();++i)if(result.points_[i].slot==slot&&result.points_[i].id==key_.parameter)
      if(!at.emplace(result.points_[i].frame,i).second)at[result.points_[i].frame]=SIZE_MAX;
    for(const auto &event:events_)if(event.frame) {
      const ParameterChange point{slot,key_.parameter,event.value,*event.frame};
      const auto old=at.find(point.frame);
      if(old!=at.end()) {
        if(old->second==SIZE_MAX)throw std::invalid_argument("Existing recorded automation has duplicate points at this time; edit the lane first");
        if(result.points_[old->second].value!=point.value)result.points_[old->second]=point;
      } else {
        if(result.points_.size()==maximumRecordedPoints)throw std::invalid_argument("Automation reached 100,000 points");
        at.emplace(point.frame,result.points_.size());result.points_.push_back(point);
      }
    }
    result.changed_=result.points_!=before_;result.key=key_;result.version=version_;result.owner=this;return result;
  }
  template<class Publish> bool publish(const Candidate &candidate,Publish &&publish) {
    active();
    resolve();
    if(candidate.owner!=this||candidate.key!=key_||candidate.version!=version_)
      throw std::invalid_argument("Recorded gesture changed after preparation");
    // The adapter stages persistence/history and performs its bounded atomic
    // publication here. No reentrant input or message pumping is permitted.
    // Refusal/exception retains every event and permits a fresh retry.
    if(!publish(candidate.points_,candidate.changed_))return false;
    retired_=true;return true;
  }
};
}
