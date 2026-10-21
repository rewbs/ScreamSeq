#pragma once
#include "RecordedCapture.hpp"
#include "editor/NativeSong.hpp"
#include "editor/RecordingFrameClock.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace Tracker {
// One serialized control owner, never the audio callback. This retains a single
// target gesture until the native adapter atomically publishes points/history.
// Native input, provider incarnation, recovery serialization and UI stay native.
class RecordedGestureCapture {
  friend class RecordedGestureTake;
public:
  static constexpr size_t eventCapacity=8192;
  static constexpr size_t batchTargetCapacity=128;
  static constexpr size_t batchStorageLimit=64u*1024u*1024u;
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
    uint64_t identity=0;
    const RecordedGestureCapture *owner=nullptr;
  public:
    const std::vector<ParameterChange> &points()const noexcept{return points_;}
    const Loss &loss()const noexcept{return loss_;}
    bool changed()const noexcept{return changed_;}
  };
  struct BatchInput {
    RecordedGestureCapture *owner=nullptr; // Retained by the native coordinator.
    uint64_t providerEpoch=0;
    std::span<const PluginParameter> catalog;
  };
  class BatchCandidate {
    friend class RecordedGestureCapture;
    struct Member {RecordedGestureCapture *owner;Key key;uint64_t identity,version;Loss loss;};
    std::vector<Member> members_;
    std::vector<ParameterChange> points_;
    bool changed_=false;
  public:
    const std::vector<ParameterChange> &points()const noexcept{return points_;}
    bool changed()const noexcept{return changed_;}
    size_t targets()const noexcept{return members_.size();}
    const Loss &loss(size_t target)const{return members_.at(target).loss;}
    const Key &key(size_t target)const{return members_.at(target).key;}
  };
private:
  static uint64_t nextIdentity() {
    static std::atomic<uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    do {if(value==UINT64_MAX)throw std::overflow_error("Recorded gesture identity exhausted");}
    while(!next.compare_exchange_weak(value,value+1,std::memory_order_relaxed));
    return value;
  }
  const uint64_t identity_=nextIdentity();
  Key key_;
  std::shared_ptr<const RecordingFrameTimeline> timeline_;
  std::vector<std::string> rackIDs_; // No vendor blobs duplicated into the input log.
  std::shared_ptr<const std::vector<ParameterChange>> before_;
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
  Loss validatePreparation(const std::string &revision,uint64_t playbackEpoch,uint64_t providerEpoch,
      std::span<const PluginState> rack,const NativeSong &native,
      std::span<const PluginParameter> catalog,bool acknowledgeLoss) {
    active();epoch(playbackEpoch,providerEpoch);
    if(!endTime_||revision!=key_.revision||rack.size()!=rackIDs_.size()||
       !std::equal(rack.begin(),rack.end(),rackIDs_.begin(),[](const auto &state,const auto &id){return state.instanceID==id;}))
      throw std::invalid_argument("Recorded gesture target or document changed; retain it for review");
    conflict(native,key_);
    const auto &p=parameter(catalog,key_.parameter);
    if(p.min!=minimum_||p.max!=maximum_)
      throw std::invalid_argument("Recorded gesture parameter range changed");
    resolve();
    const auto result=loss();
    if(result.any()&&!acknowledgeLoss)
      throw std::runtime_error("Recorded gesture has lost or unmapped events; retain it for review");
    return result;
  }
  void overlay(std::vector<ParameterChange> &points)const {
    const auto target=std::find(rackIDs_.begin(),rackIDs_.end(),key_.plugin);
    const auto slot=uint32_t(target-rackIDs_.begin());
    std::map<uint64_t,size_t> at;
    for(size_t i=0;i<points.size();++i)if(points[i].slot==slot&&points[i].id==key_.parameter)
      if(!at.emplace(points[i].frame,i).second)at[points[i].frame]=SIZE_MAX;
    for(const auto &event:events_)if(event.frame) {
      const ParameterChange point{slot,key_.parameter,event.value,*event.frame};
      const auto old=at.find(point.frame);
      if(old!=at.end()) {
        if(old->second==SIZE_MAX)throw std::invalid_argument("Existing recorded automation has duplicate points at this time; edit the lane first");
        if(points[old->second].value!=point.value)points[old->second]=point;
      } else {
        if(points.size()==maximumRecordedPoints)throw std::invalid_argument("Automation reached 100,000 points");
        at.emplace(point.frame,points.size());points.push_back(point);
      }
    }
  }
  size_t storageBytes()const noexcept {
    size_t result=sizeof(*this)+events_.capacity()*sizeof(Event)+pending_.capacity()*sizeof(size_t)+
      rackIDs_.capacity()*sizeof(std::string)+key_.token.capacity()+key_.revision.capacity()+key_.plugin.capacity();
    for(const auto &id:rackIDs_)result+=id.capacity();
    return result;
  }
  static bool sameBaseline(std::span<const ParameterChange> a,std::span<const ParameterChange> b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto &x,const auto &y){
      return x.slot==y.slot&&x.id==y.id&&x.frame==y.frame&&std::bit_cast<uint32_t>(x.value)==std::bit_cast<uint32_t>(y.value);});
  }
public:
  RecordedGestureCapture(Key key,std::shared_ptr<const RecordingFrameTimeline> timeline,
      std::span<const PluginState> rack,std::span<const ParameterChange> before,
      const NativeSong &native,std::span<const PluginParameter> catalog,uint64_t sourceLoss)
    :RecordedGestureCapture(std::move(key),std::move(timeline),rack,
      std::make_shared<const std::vector<ParameterChange>>(before.begin(),before.end()),native,catalog,sourceLoss) {}
  // One immutable baseline can be shared by every target in a vendor gesture.
  RecordedGestureCapture(Key key,std::shared_ptr<const RecordingFrameTimeline> timeline,
      std::span<const PluginState> rack,std::shared_ptr<const std::vector<ParameterChange>> before,
      const NativeSong &native,std::span<const PluginParameter> catalog,uint64_t sourceLoss)
    :key_(std::move(key)),timeline_(std::move(timeline)),before_(std::move(before)),initialSourceLoss_(sourceLoss),sourceLoss_(sourceLoss) {
    const auto stoppedEnd=timeline_?timeline_->stoppedEndTime(key_.playbackEpoch):std::nullopt;
    if(key_.token.empty()||key_.token.size()>80||key_.token.find('\0')!=std::string::npos||
       key_.revision.empty()||key_.revision.size()>4096||key_.revision.find('\0')!=std::string::npos||
       key_.plugin.empty()||key_.plugin.size()>128||key_.plugin.find('\0')!=std::string::npos||
       !key_.playbackEpoch||!key_.providerEpoch||!key_.startTime||
       !timeline_||timeline_->generation()!=key_.playbackEpoch||
       (stoppedEnd&&*stoppedEnd<=key_.startTime))
      throw std::invalid_argument("Recorded gesture needs a current document, target and playback clock");
    if(!before_||rack.size()>maximumNativePlugins||before_->size()>maximumRecordedPoints||
       std::count_if(rack.begin(),rack.end(),[&](const auto &p){return p.instanceID==key_.plugin;})!=1)
      throw std::invalid_argument("Recorded gesture target or capacity is invalid");
    conflict(native,key_);
    const auto &p=parameter(catalog,key_.parameter);minimum_=p.min;maximum_=p.max;
    for(const auto &state:rack)rackIDs_.push_back(state.instanceID);
    events_.reserve(eventCapacity);pending_.reserve(eventCapacity);
  }
  RecordedGestureCapture(const RecordedGestureCapture &)=delete;
  RecordedGestureCapture &operator=(const RecordedGestureCapture &)=delete;
  const Key &key()const noexcept{return key_;}
  bool matchesBaseline(std::span<const ParameterChange> points)const noexcept{return sameBaseline(*before_,points);}
  std::span<const Event> events()const noexcept{return events_;}
  bool retired()const noexcept{return retired_;}
  bool sealed()const noexcept{return endTime_.has_value();}
  size_t retainedStorageBytes()const noexcept{return storageBytes();}
  uint64_t sourceLossCount()const noexcept{return sourceLoss_;}
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
    Candidate result;result.loss_=validatePreparation(revision,playbackEpoch,providerEpoch,rack,native,catalog,acknowledgeLoss);
    result.points_=*before_;
    // Only index this gesture's target. Preserve other slots/parameters and the
    // original point order. Last accepted input wins at the same canonical time.
    overlay(result.points_);
    result.changed_=!sameBaseline(result.points_,*before_);result.key=key_;result.identity=identity_;result.version=version_;result.owner=this;return result;
  }
  template<class Publish> bool publish(const Candidate &candidate,Publish &&publish) {
    active();
    resolve();
    if(candidate.owner!=this||candidate.identity!=identity_||candidate.key!=key_||candidate.version!=version_)
      throw std::invalid_argument("Recorded gesture changed after preparation");
    // The adapter stages persistence/history and performs its bounded atomic
    // publication here. No reentrant input or message pumping is permitted.
    // Refusal/exception retains every event and permits a fresh retry.
    if(!publish(candidate.points_,candidate.changed_))return false;
    retired_=true;return true;
  }
  static BatchCandidate prepareBatch(std::span<const BatchInput> inputs,const std::string &revision,uint64_t playbackEpoch,
      std::span<const PluginState> rack,const NativeSong &native,bool acknowledgeLoss=false) {
    if(inputs.empty()||inputs.size()>batchTargetCapacity||!inputs.front().owner)
      throw std::invalid_argument("Recorded gesture batch needs 1 to 128 targets");
    const auto &first=*inputs.front().owner;
    std::set<const RecordedGestureCapture *> owners;
    std::set<std::pair<std::string,uint32_t>> targets;
    std::set<const std::vector<ParameterChange> *> baselines;
    std::map<std::string,std::array<uint64_t,3>> providers;
    // Account retained owner buffers and the final point candidate. Temporary
    // per-target frame indices are bounded by the point limit and freed before
    // publication; they never multiply by the number of targets.
    size_t bytes=sizeof(BatchCandidate)+inputs.size()*sizeof(BatchCandidate::Member)+maximumRecordedPoints*sizeof(ParameterChange);
    size_t events=0;
    for(const auto &input:inputs) {
      if(!input.owner||!owners.insert(input.owner).second)throw std::invalid_argument("Duplicate recorded gesture owner");
      const auto &owner=*input.owner;owner.active();
      if(!targets.emplace(owner.key_.plugin,owner.key_.parameter).second)
        throw std::invalid_argument("Two recorded gestures overlap the same parameter");
      if(owner.key_.token!=first.key_.token||owner.key_.revision!=first.key_.revision||
         owner.key_.startTime!=first.key_.startTime||owner.endTime_!=first.endTime_||
         owner.timeline_.get()!=first.timeline_.get()||owner.rackIDs_!=first.rackIDs_||
         (owner.before_.get()!=first.before_.get()&&!sameBaseline(*owner.before_,*first.before_)))
        throw std::invalid_argument("Recorded gesture targets do not share one captured transaction");
      const std::array<uint64_t,3> provider{owner.key_.providerEpoch,owner.initialSourceLoss_,owner.sourceLoss_};
      const auto [existing,inserted]=providers.emplace(owner.key_.plugin,provider);
      if(!inserted&&existing->second!=provider)
        throw std::invalid_argument("Recorded gesture targets disagree on plugin incarnation or input loss");
      events+=owner.events_.size();bytes+=owner.storageBytes()+owner.key_.token.size()+owner.key_.revision.size()+owner.key_.plugin.size();
      if(baselines.insert(owner.before_.get()).second)bytes+=owner.before_->capacity()*sizeof(ParameterChange);
      if(events>maximumRecordedPoints||bytes>batchStorageLimit)
        throw std::invalid_argument("Recorded gesture batch exceeds its input or retained-storage budget");
    }
    BatchCandidate result;result.points_.reserve(maximumRecordedPoints);
    result.points_.assign(first.before_->begin(),first.before_->end());result.members_.reserve(inputs.size());
    for(const auto &input:inputs) {
      auto &owner=*input.owner;
      const auto loss=owner.validatePreparation(revision,playbackEpoch,input.providerEpoch,rack,native,input.catalog,acknowledgeLoss);
      owner.overlay(result.points_);
      result.members_.push_back({&owner,owner.key_,owner.identity_,owner.version_,loss});
    }
    result.changed_=!sameBaseline(result.points_,*first.before_);return result;
  }
  template<class Publish> static bool publishBatch(const BatchCandidate &candidate,
      std::span<const BatchInput> currentInputs,Publish &&publish) {
    if(candidate.members_.empty()||currentInputs.size()!=candidate.members_.size())
      throw std::invalid_argument("Recorded gesture batch membership changed");
    std::set<const RecordedGestureCapture *> owners;
    for(const auto &input:currentInputs) {
      if(!input.owner||!owners.insert(input.owner).second)
        throw std::invalid_argument("Duplicate recorded gesture owner");
      const auto found=std::find_if(candidate.members_.begin(),candidate.members_.end(),[&](const auto &m){return m.owner==input.owner;});
      if(found==candidate.members_.end()||input.providerEpoch!=found->key.providerEpoch)
        throw std::invalid_argument("Recorded gesture batch membership changed");
    }
    for(const auto &member:candidate.members_) {
      // Dereference the caller's retained current owner, never the pointer in
      // an old candidate whose storage may have been destroyed and recycled.
      const auto input=std::find_if(currentInputs.begin(),currentInputs.end(),[&](const auto &v){return v.owner==member.owner;});
      auto &owner=*input->owner;
      if(member.identity!=owner.identity_||member.key!=owner.key_)
        throw std::invalid_argument("Recorded gesture batch owner was replaced after preparation");
      owner.active();owner.resolve();
      if(member.version!=owner.version_)
        throw std::invalid_argument("Recorded gesture batch changed after preparation");
    }
    // One admission and one native history transaction, followed only by
    // no-throw retirement. A refusal/exception retires none of the targets.
    if(!publish(candidate.points_,candidate.changed_))return false;
    for(const auto &input:currentInputs)input.owner->retired_=true;
    return true;
  }
};
}
