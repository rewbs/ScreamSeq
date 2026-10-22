#pragma once
#include "RecordedGestureCapture.hpp"

namespace Tracker {
// Serialized control-owner coordination, shared by the native sessions.
// The adapters own vendor/UI barriers, audible previews, manual-state
// protection, recovery serialization and document/history publication.
class RecordedGestureTake {
public:
  struct Provider {
    std::string plugin;
    PluginEditSource source;
  };
  enum class Intake { Outside, Captured, Refused };
  enum class Failure { None, Context, Provider, Target, Capacity, Storage, Allocation };
  // Detached, read-only recovery evidence. Native adapters serialize this on
  // the control owner. It deliberately cannot be installed as a live take:
  // host times and provider incarnations belong to the original process.
  struct Review {
    struct Source {Provider initial;uint64_t dropped=0;bool drained=false;};
    struct Target {
      RecordedGestureCapture::Key key;
      float minimum=0,maximum=0;
      RecordedGestureCapture::Loss loss;
      std::vector<RecordedGestureCapture::Event> events;
    };
    std::string token,revision;
    uint64_t playbackEpoch=0,startTime=0,rejected=0;
    std::optional<uint64_t> endTime,stoppedEndTime;
    Failure failure=Failure::None;
    bool completed=false;
    std::vector<std::string> rack;
    std::vector<ParameterChange> baseline;
    std::vector<Source> sources;
    std::vector<Target> targets;
  };
private:
  struct Source {
    Provider initial;
    uint64_t dropped=0;
    bool drained=false;
  };
  std::string token_,revision_;
  std::shared_ptr<const RecordingFrameTimeline> timeline_;
  uint64_t playbackEpoch_=0,start_=0;
  std::optional<uint64_t> end_;
  std::vector<PluginState> rack_; // Identities only; never retain vendor blobs here.
  std::shared_ptr<const std::vector<ParameterChange>> before_;
  std::vector<Source> sources_;
  std::vector<std::unique_ptr<RecordedGestureCapture>> targets_;
  size_t eventCount_=0,storage_=0;
  uint64_t rejected_=0;
  Failure failure_=Failure::None;
  bool completed_=false;

  Source *source(const std::string &plugin) noexcept {
    const auto found=std::find_if(sources_.begin(),sources_.end(),[&](const auto &s){return s.initial.plugin==plugin;});
    return found==sources_.end()?nullptr:&*found;
  }
  static bool reviewable(Failure reason) noexcept {
    return reason==Failure::Capacity||reason==Failure::Storage||reason==Failure::Allocation;
  }
  void refuse(Failure reason) noexcept {
    // Loss consent cannot rehabilitate a later identity/target failure. Keep
    // the first fatal reason, but promote an earlier reviewable loss when a
    // native drain observes a changed document, clock or notification source.
    if(failure_==Failure::None||(reviewable(failure_)&&!reviewable(reason)))failure_=reason;
    if(rejected_!=UINT64_MAX)++rejected_;
  }
  bool current(const std::string &revision,uint64_t epoch) noexcept {
    if(revision!=revision_||epoch!=playbackEpoch_){refuse(Failure::Context);return false;}
    return true;
  }
  bool observe(Source &owner,PluginEditSource current) noexcept {
    if(current.incarnation!=owner.initial.source.incarnation||current.dropped<owner.dropped){
      refuse(Failure::Provider);return false;
    }
    for(const auto &target:targets_)if(target->key().plugin==owner.initial.plugin)
      if(target->retired()||target->sourceLossCount()>current.dropped){
        refuse(Failure::Provider);return false;
      }
    owner.dropped=current.dropped;
    // Every target on a provider has the same original/current loss counts.
    // Do not sum these duplicates when reporting queue loss.
    for(auto &target:targets_)if(target->key().plugin==owner.initial.plugin)
      target->sourceLoss(current.dropped,current.incarnation);
    return true;
  }
  void requireReady(const std::string &revision,uint64_t epoch,bool acknowledgeLoss) const {
    const bool reviewedLoss=acknowledgeLoss&&reviewable(failure_);
    if(completed_||(failure_!=Failure::None&&!reviewedLoss)||revision!=revision_||epoch!=playbackEpoch_||!end_||
       (!acknowledgeLoss&&std::any_of(sources_.begin(),sources_.end(),[](const auto &s){return s.dropped!=s.initial.source.dropped;}))||
       std::any_of(sources_.begin(),sources_.end(),[](const auto &s){return !s.drained;}))
      throw std::invalid_argument("Recorded take needs its captured context and every native drain barrier; input retained");
  }
public:
  RecordedGestureTake(std::string token,std::string revision,uint64_t start,
      std::shared_ptr<const RecordingFrameTimeline> timeline,std::span<const PluginState> rack,
      std::span<const ParameterChange> before,std::span<const Provider> providers)
    :token_(std::move(token)),revision_(std::move(revision)),timeline_(std::move(timeline)),start_(start) {
    if(token_.empty()||token_.size()>80||token_.find('\0')!=std::string::npos||
       revision_.empty()||revision_.size()>4096||revision_.find('\0')!=std::string::npos||
       !start_||!timeline_||!(playbackEpoch_=timeline_->generation())||
       providers.empty()||providers.size()>maximumNativePlugins||rack.size()>maximumNativePlugins||
       before.size()>maximumRecordedPoints)
      throw std::invalid_argument("Recorded take needs bounded identities, providers and a current clock");
    if(const auto end=timeline_->stoppedEndTime(playbackEpoch_);end&&start_>=*end)
      throw std::invalid_argument("Cannot arm a recorded take after its playback ended");
    std::set<std::string> identities;
    rack_.reserve(rack.size());
    for(const auto &plugin:rack) {
      if(plugin.instanceID.empty()||plugin.instanceID.size()>128||plugin.instanceID.find('\0')!=std::string::npos||
         !identities.insert(plugin.instanceID).second)
        throw std::invalid_argument("Recorded take needs unique stable rack identities");
      PluginState id;id.instanceID=plugin.instanceID;rack_.push_back(std::move(id));
    }
    sources_.reserve(providers.size());
    for(const auto &provider:providers) {
      if(!provider.source.incarnation||!identities.contains(provider.plugin)||source(provider.plugin))
        throw std::invalid_argument("Recorded take has an invalid or duplicate provider");
      sources_.push_back({provider,provider.source.dropped,false});
    }
    before_=std::make_shared<const std::vector<ParameterChange>>(before.begin(),before.end());
    targets_.reserve(RecordedGestureCapture::batchTargetCapacity);
    storage_=sizeof(*this)+token_.capacity()+revision_.capacity()+
      rack_.capacity()*sizeof(PluginState)+sources_.capacity()*sizeof(Source)+
      targets_.capacity()*sizeof(std::unique_ptr<RecordedGestureCapture>)+
      before_->capacity()*sizeof(ParameterChange)+maximumRecordedPoints*sizeof(ParameterChange);
    for(const auto &id:rack_)storage_+=id.instanceID.capacity();
    for(const auto &s:sources_)storage_+=s.initial.plugin.capacity();
    if(storage_>RecordedGestureCapture::batchStorageLimit)
      throw std::invalid_argument("Recorded take exceeds retained storage budget");
  }
  RecordedGestureTake(const RecordedGestureTake &)=delete;
  RecordedGestureTake &operator=(const RecordedGestureTake &)=delete;
  const std::string &token()const noexcept{return token_;}
  const std::string &revision()const noexcept{return revision_;}
  uint64_t playbackEpoch()const noexcept{return playbackEpoch_;}
  size_t eventCount()const noexcept{return eventCount_;}
  size_t targetCount()const noexcept{return targets_.size();}
  Failure failure()const noexcept{return failure_;}
  bool completed()const noexcept{return completed_;}
  uint64_t rejectedEvents()const noexcept{return rejected_;}
  uint64_t startTime()const noexcept{return start_;}
  std::optional<uint64_t> endTime()const noexcept{return end_;}
  const RecordedGestureCapture &target(size_t index)const{return *targets_.at(index);}
  bool matchesBaseline(std::span<const PluginState> rack,std::span<const ParameterChange> points)const {
    return rack.size()==rack_.size()&&std::equal(rack.begin(),rack.end(),rack_.begin(),
      [](const auto &a,const auto &b){return a.instanceID==b.instanceID;})&&
      RecordedGestureCapture::sameBaseline(*before_,points);
  }
  std::vector<Provider> providers()const {
    std::vector<Provider> result;result.reserve(sources_.size());
    for(const auto &s:sources_)result.push_back(s.initial);
    return result;
  }
  bool owns(const std::string &plugin)const noexcept {
    return std::any_of(sources_.begin(),sources_.end(),[&](const auto &s){return s.initial.plugin==plugin;});
  }
  uint64_t sourceLoss(const std::string &plugin)const {
    const auto found=std::find_if(sources_.begin(),sources_.end(),[&](const auto &s){return s.initial.plugin==plugin;});
    if(found==sources_.end())throw std::invalid_argument("Provider is not armed in this take");
    return found->dropped-found->initial.source.dropped;
  }
  Review review()const {
    Review result;
    result.token=token_;result.revision=revision_;result.playbackEpoch=playbackEpoch_;
    result.startTime=start_;result.endTime=end_;result.rejected=rejected_;
    result.stoppedEndTime=timeline_->stoppedEndTime(playbackEpoch_);
    result.failure=failure_;result.completed=completed_;result.baseline=*before_;
    result.rack.reserve(rack_.size());result.sources.reserve(sources_.size());
    result.targets.reserve(targets_.size());
    for(const auto &plugin:rack_)result.rack.push_back(plugin.instanceID);
    for(const auto &s:sources_)result.sources.push_back({s.initial,s.dropped,s.drained});
    for(const auto &target:targets_) {
      result.targets.push_back({target->key_,target->minimum_,target->maximum_,target->loss(),target->events_});
      auto &copy=result.targets.back();
      // Stop can shorten previously published presentation coverage before
      // the next worker poll. Preserve the raw event, but not a false frame.
      if(result.stoppedEndTime)for(auto &event:copy.events)
        if(event.frame&&event.hostTime>=*result.stoppedEndTime)event.frame.reset();
      copy.loss.unmapped=size_t(std::count_if(copy.events.begin(),copy.events.end(),[](const auto &event){return !event.frame;}));
    }
    // Do not mutate live mappings, consume vendor notifications, seal,
    // acknowledge a drain or retire history here.
    return result;
  }
  bool observeProvider(const std::string &plugin,PluginEditSource provider,const std::string &revision,uint64_t epoch) noexcept {
    auto *owner=source(plugin);
    if(!owner||completed_||!current(revision,epoch)||!observe(*owner,provider))return false;
    // Resolve once per native poll, including empty polls, while the bounded
    // clock ring still retains delayed coverage. Do not rescan pending times
    // for every notification in a dense input batch.
    for(auto &target:targets_)if(target->key().plugin==plugin)target->resolve();
    return true;
  }
  // Call for every original notification before coalescing. A failed or full
  // take still owns its providers: native adapters must not divert its edits
  // into opaque/manual state. Native pending buffers retain publication retry.
  void validateControlInput(const std::string &plugin,const PluginEdit &edit,
      const NativeSong &native,std::span<const PluginParameter> catalog)const {
    if(completed_||failure_!=Failure::None||!owns(plugin)||!edit.hostTime||edit.hostTime<start_||
       (end_&&edit.hostTime>=*end_))
      throw std::invalid_argument("Control input belongs to another or stopped parameter take");
    RecordedGestureCapture::Key key;key.plugin=plugin;key.parameter=edit.id;
    RecordedGestureCapture::conflict(native,key);
    const auto &p=RecordedGestureCapture::parameter(catalog,edit.id);
    if(!std::isfinite(edit.value)||edit.value<p.min||edit.value>p.max)
      throw std::invalid_argument("Recorded control value is outside the parameter range");
  }
  Intake append(const std::string &plugin,const PluginEdit &edit,PluginEditSource provider,
      const std::string &revision,uint64_t epoch,const NativeSong &native,std::span<const PluginParameter> catalog) noexcept {
    auto *owner=source(plugin);
    if(!owner)return Intake::Outside;
    if(completed_)return Intake::Refused;
    owner->drained=false;
    if(!current(revision,epoch)||!observe(*owner,provider))return Intake::Refused;
    if(failure_!=Failure::None){refuse(failure_);return Intake::Refused;}
    if(edit.hostTime&&(edit.hostTime<start_||(end_&&edit.hostTime>=*end_)))return Intake::Outside;
    if(eventCount_==maximumRecordedPoints){refuse(Failure::Capacity);return Intake::Refused;}
    try {
      auto found=std::find_if(targets_.begin(),targets_.end(),[&](const auto &t){
        return t->key().plugin==plugin&&t->key().parameter==edit.id;});
      if(found==targets_.end()) {
        if(targets_.size()==RecordedGestureCapture::batchTargetCapacity){refuse(Failure::Capacity);return Intake::Refused;}
        auto target=std::make_unique<RecordedGestureCapture>(
          RecordedGestureCapture::Key{token_,revision_,plugin,edit.id,playbackEpoch_,provider.incarnation,start_},
          timeline_,rack_,before_,native,catalog,owner->initial.source.dropped);
        target->sourceLoss(owner->dropped,provider.incarnation);
        if(end_)target->seal(*end_);
        const auto bytes=target->retainedStorageBytes();
        if(bytes>RecordedGestureCapture::batchStorageLimit-storage_){refuse(Failure::Storage);return Intake::Refused;}
        // Validate the first event before adopting an empty target.
        if(!target->append(edit,epoch,provider.incarnation)){refuse(Failure::Capacity);return Intake::Refused;}
        targets_.push_back(std::move(target));storage_+=bytes;
      } else {
        if(!(*found)->append(edit,epoch,provider.incarnation)){refuse(Failure::Capacity);return Intake::Refused;}
      }
      ++eventCount_;return Intake::Captured;
    } catch(const std::bad_alloc &) {refuse(Failure::Allocation);}
      catch(const std::exception &) {refuse(Failure::Target);}
    return Intake::Refused;
  }
  void seal(uint64_t end) {
    if(completed_||end<start_||(end_&&*end_!=end))throw std::invalid_argument("Recorded take has another release boundary or is complete");
    // Preflight all members before sealing any of them.
    for(const auto &target:targets_)
      if(std::any_of(target->events().begin(),target->events().end(),[&](const auto &e){return e.hostTime&&e.hostTime>=end;}))
        throw std::invalid_argument("Recorded take release precedes retained input");
    for(auto &target:targets_)target->seal(end);
    if(!end_)for(auto &s:sources_)s.drained=false;
    end_=end;
  }
  // Native adapter calls this only after its original-time release/Stop input
  // barrier AND an empty paired pop, or completed provider-qualified fixed cut.
  // An ordinary empty poll while capturing is not a completed drain. Appending
  // further input invalidates it again; adapters must retain post-cut input.
  bool drained(const std::string &plugin,PluginEditSource provider,const std::string &revision,uint64_t epoch) noexcept {
    auto *owner=source(plugin);
    if(!owner||!end_||completed_)return false;
    if(!observeProvider(plugin,provider,revision,epoch))return false;
    owner->drained=true;return true;
  }
  std::vector<RecordedGestureCapture *> preparedOwners(const std::string &revision,uint64_t epoch,bool acknowledgeLoss=false) const {
    requireReady(revision,epoch,acknowledgeLoss);
    std::vector<RecordedGestureCapture *> result;result.reserve(targets_.size());
    for(const auto &target:targets_) {
      if(target->retired())throw std::invalid_argument("Recorded take has already been published");
      result.push_back(target.get());
    }
    return result;
  }
  // Rebuild membership at the actual native commit boundary; never commit a
  // vector retained from an earlier observation. The callback must publish
  // synchronously through the atomic batch owner, without message pumping or
  // reentrant intake. False/exception retains the entire take for review.
  template<class Publish> bool commit(const std::string &revision,uint64_t epoch,bool acknowledgeLoss,Publish &&publish) {
    auto owners=preparedOwners(revision,epoch,acknowledgeLoss);
    if(owners.empty()){completed_=true;return true;}
    const bool accepted=publish(std::span<RecordedGestureCapture *const>(owners),acknowledgeLoss);
    const auto retired=std::count_if(owners.begin(),owners.end(),[](const auto *owner){return owner->retired();});
    if((accepted&&size_t(retired)!=owners.size())||(!accepted&&retired!=0))
      throw std::logic_error("Native recorded-take publisher violated atomic retirement");
    if(accepted)completed_=true;
    return accepted;
  }
};
}
