#pragma once
#include "../Audio/AudioDevice.hpp"
#include "editor/hosted/RecordedGestureBoundary.hpp"
#include "editor/hosted/RecordedGestureDrain.hpp"
#include "editor/hosted/ParameterEdits.hpp"
#include <functional>
#include <mach/mach_time.h>

namespace Tracker {
// Mac session control-owner adapter. The session must exclude legacy/manual
// intake for owned providers and retain this object until native restoration
// and history/recovery publication finish. It is not an audio callback owner.
// Native input leases and opaque vendor restoration remain the session's duty;
// cancelling an audible preview does not relinquish this take's ownership.
class NativeParameterTake {
public:
  using Context=std::pair<std::string,uint64_t>;
  using ContextReader=std::function<Context()>;
private:
  AudioDevice &audio_;
  uint64_t hostEpoch_;
  std::shared_ptr<RecordedGestureTake> take_;
  ContextReader context_;
  RecordedGestureDrain::Catalogs catalogs_;
  RecordedGestureBoundary::Cuts stoppedCuts_;
  size_t previewedEvents_=0;
  bool entering_=false;
  struct Entry {
    bool &entering;
    explicit Entry(bool &value):entering(value){if(value)throw std::runtime_error("Parameter take is already processing native input");value=true;}
    ~Entry(){entering=false;}
  };
  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  Context current()const {
    require(audio_.pluginEpoch()==hostEpoch_,"Parameter take belongs to a replaced audio host; input retained");
    const auto result=context_();
    require(audio_.pluginEpoch()==hostEpoch_&&result.first==take_->revision()&&result.second==take_->playbackEpoch(),
      "Parameter take context changed; input retained");
    return result;
  }
  size_t slot(const std::string &identity)const {
    const auto found=audio_.pluginSlot(identity);
    require(found.has_value(),"Recorded plugin identity is missing; input retained");return *found;
  }
  RecordedGestureBoundary::Cuts cuts()const {
    RecordedGestureBoundary::Cuts result;
    for(const auto &[identity,catalog]:catalogs_)result.emplace(identity,PluginEditBoundary{});
    // Allocate before touching any native provider. These calls execute on the
    // Mac native owner; no state serialization/catalog call belongs in this cut.
    for(auto &[identity,cut]:result) {
      const auto boundary=audio_.pluginInputBoundary(slot(identity),mach_absolute_time());
      require(boundary.has_value(),"Recorded plugin cannot establish a native input boundary");cut=*boundary;
    }
    return result;
  }
  bool drain(const NativeSong &native,const RecordedGestureBoundary::Cuts &boundaries,bool acknowledgeStop,bool preview) {
    if(take_->completed())return true;
    const auto [revision,epoch]=current();
    const auto observe=[&](const std::string &identity)->std::optional<PluginEditSource> {
      const auto index=audio_.pluginSlot(identity);
      return index?std::optional<PluginEditSource>(audio_.pluginEditSource(*index)):std::nullopt;
    };
    const auto read=[&](const std::string &identity,const PluginEditBoundary *boundary,PluginEdit &event,PluginEditSource &source) {
      require(boundary!=nullptr,"Mac parameter intake requires an original native boundary");
      return audio_.popPluginInputBefore(slot(identity),*boundary,event,source);
    };
    const auto result=RecordedGestureDrain::poll(*take_,revision,epoch,native,catalogs_,boundaries,acknowledgeStop,observe,read);
    require(result==RecordedGestureDrain::Result::Complete||result==RecordedGestureDrain::Result::Pending,
      "Parameter input boundary, target or context changed; take retained");
    if(preview&&previewedEvents_!=take_->eventCount()) {
      std::vector<ParameterChange> changes;changes.reserve(take_->targetCount());
      RecordedGestureDrain::latest(*take_,[&](const auto &key,const auto &event){
        changes.push_back({uint32_t(slot(key.plugin)),key.parameter,event.value,0});
      });
      require(audio_.previewPluginParameters(changes),"Parameter preview queue is busy; captured input retained for retry");
      previewedEvents_=take_->eventCount();
    }
    return result==RecordedGestureDrain::Result::Complete;
  }
public:
  NativeParameterTake(const NativeParameterTake &)=delete;
  NativeParameterTake &operator=(const NativeParameterTake &)=delete;
  NativeParameterTake(AudioDevice &audio,std::shared_ptr<RecordedGestureTake> take,
      ContextReader context,std::span<const PluginState> rack,std::span<const ParameterChange> baseline)
    :audio_(audio),hostEpoch_(audio.pluginEpoch()),take_(std::move(take)),context_(std::move(context)) {
    require(take_&&context_,"Parameter recording needs a take and context owner");
    const auto before=current();
    require(take_->matchesBaseline(rack,baseline),"Parameter recording baseline changed before arming");
    for(const auto &provider:take_->providers()) {
      const auto index=slot(provider.plugin);
      require(audio_.pluginEditSource(index)==provider.source,"Parameter provider changed before arming");
      catalogs_.emplace(provider.plugin,audio_.pluginParameters(index));
    }
    require(current()==before,"Parameter context changed during catalogue preparation");
    RecordedGestureBoundary::requireFresh(*take_,cuts());
    require(current()==before,"Parameter context changed during native arming");
  }
  const RecordedGestureTake &take()const noexcept{return *take_;}
  bool owns(const std::string &identity)const noexcept{return take_->owns(identity);}
  const RecordedGestureBoundary::Cuts &stopCuts()const noexcept{return stoppedCuts_;}
  bool poll(const NativeSong &native) {
    Entry entry(entering_);
    if(take_->completed())return true;
    const auto before=current();
    const auto boundaries=stoppedCuts_.empty()?cuts():stoppedCuts_;
    require(current()==before,"Parameter context changed during input observation");
    return drain(native,boundaries,!stoppedCuts_.empty(),true);
  }
  uint64_t stop() {
    Entry entry(entering_);
    if(!stoppedCuts_.empty())return *take_->endTime();
    const auto before=current();auto boundaries=cuts();
    require(current()==before,"Parameter context changed while stopping; take retained");
    const auto end=RecordedGestureBoundary::seal(*take_,boundaries);stoppedCuts_.swap(boundaries);return end;
  }
  bool capture(const NativeSong &native,const std::string &identity,uint32_t parameter,double value,uint64_t timestamp) {
    Entry entry(entering_);const auto [revision,epoch]=current();
    require(timestamp&&timestamp<=mach_absolute_time(),"Parameter control needs its original past host timestamp");
    const auto found=catalogs_.find(identity);
    require(found!=catalogs_.end(),"Parameter control is not armed in this take");
    const std::array<std::pair<uint32_t,double>,1> values{{{parameter,value}}};
    const auto prepared=prepareParameterEdits(found->second,values);
    const PluginEdit event{parameter,prepared.at(0).second,timestamp};
    take_->validateControlInput(identity,event,native,found->second);
    return take_->append(identity,event,audio_.pluginEditSource(slot(identity)),revision,epoch,native,found->second)==RecordedGestureTake::Intake::Captured;
  }
  // Autosave may capture a bounded prefix without sealing the live take or
  // applying an audible preview. The session serializes take().review().
  void captureRecovery(const NativeSong &native) {
    Entry entry(entering_);if(take_->completed())return;
    const auto failure=take_->failure();
    if(failure==RecordedGestureTake::Failure::Context||failure==RecordedGestureTake::Failure::Provider||
      failure==RecordedGestureTake::Failure::Target||(take_->endTime()&&stoppedCuts_.empty()))return;
    const auto before=current();const auto boundaries=stoppedCuts_.empty()?cuts():stoppedCuts_;
    require(current()==before,"Parameter context changed while capturing recovery");
    for(size_t n=0;n<=PluginEditQueue::capacity/RecordedGestureDrain::eventsPerProvider;++n)
      if(drain(native,boundaries,!stoppedCuts_.empty(),false))return;
    throw std::runtime_error("Parameter recovery input exceeded its fixed native boundary; take retained");
  }
  // Call only inside the native release lease after checking the fixed Stop
  // cuts again. The session owns vendor-state verification and explicit release.
  bool restorePreview(bool discard) {
    Entry entry(entering_);
    require(take_->completed()||(discard&&take_->endTime()),"Keep or explicitly discard a stopped take before restoration");
    std::vector<AudioDevice::ParameterPreviewOwner> targets;targets.reserve(take_->targetCount());
    const auto providers=take_->providers();
    for(size_t i=0;i<take_->targetCount();++i) {
      const auto &key=take_->target(i).key();
      const auto provider=std::find_if(providers.begin(),providers.end(),[&](const auto &p){return p.plugin==key.plugin;});
      require(provider!=providers.end(),"Parameter target lost its native provider");
      targets.push_back({key.plugin,key.parameter,provider->source.incarnation});
    }
    return audio_.cancelPluginParameterPreviews(hostEpoch_,targets);
  }
};
}
