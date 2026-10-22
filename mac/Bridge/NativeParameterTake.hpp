#pragma once
#include "../Audio/AudioDevice.hpp"
#include "editor/hosted/RecordedGestureBoundary.hpp"
#include "editor/hosted/RecordedGestureDrain.hpp"
#include "editor/hosted/ParameterEdits.hpp"
#include <exception>
#include <functional>
#include <mach/mach_time.h>

namespace Tracker {
// Mac session control-owner adapter. The session must exclude legacy/manual
// intake for owned providers and retain this object until native restoration
// and history/recovery publication finish. It is not an audio callback owner.
// Opaque vendor verification and history publication remain the session's duty;
// restoring an audible preview does not relinquish this take's ownership.
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
  std::map<std::string,PluginEditorInputBarrier> inputBarriers_;
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
    require(found.has_value()&&*found<=UINT32_MAX,"Recorded plugin identity is missing or outside the native slot range; input retained");return *found;
  }
  struct CatalogObservation {PluginEditSource source;std::vector<PluginParameter> parameters;};
  CatalogObservation observeCatalog(const std::string &identity) {
    const auto index=slot(identity);const auto source=audio_.pluginEditSource(index);
    auto parameters=audio_.pluginParameters(index);
    // A catalogue call can pump native work. Live rack reorder does not advance
    // the whole-chain epoch, so never pair another slot's catalogue with this
    // identity's freshly resolved provider after returning from the vendor.
    require(audio_.pluginEpoch()==hostEpoch_&&slot(identity)==index&&audio_.pluginEditSource(index)==source,
      "Native parameter catalogue changed owner during observation; input retained");
    return {source,std::move(parameters)};
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
  void releaseInputBarriers() {
    for(auto i=inputBarriers_.begin();i!=inputBarriers_.end();) {
      if(i->second.incarnation) {
        require(audio_.pluginEpoch()==hostEpoch_,"Native input lease belongs to a replaced audio host; take retained");
        const auto index=slot(i->first);
        require(audio_.pluginEditSource(index).incarnation==i->second.incarnation&&
          audio_.endPluginInputBarrier(index,i->second),"Native input lease could not be released; retry with the retained take");
      }
      i=inputBarriers_.erase(i);
    }
  }
  void requireRestorationCut()const {
    require(audio_.pluginEpoch()==hostEpoch_&&!stoppedCuts_.empty(),"Restoration needs the retained host and native Stop boundary");
    const auto now=cuts();
    require(audio_.pluginEpoch()==hostEpoch_,"Audio host changed during restoration observation; take retained");
    for(const auto &[identity,cut]:stoppedCuts_) {
      const auto &next=now.at(identity);
      require(next.source==cut.source&&next.fence.position==cut.fence.position&&next.fence.pending==0,
        "Plugin input changed or remains undrained after Stop; input and take retained");
    }
  }
  bool restorePreviewImpl(bool discard) {
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
      auto observed=observeCatalog(provider.plugin);
      require(observed.source==provider.source,"Parameter provider changed before arming");
      catalogs_.emplace(provider.plugin,std::move(observed.parameters));
    }
    require(current()==before,"Parameter context changed during catalogue preparation");
    RecordedGestureBoundary::requireFresh(*take_,cuts());
    require(current()==before,"Parameter context changed during native arming");
  }
  const RecordedGestureTake &take()const noexcept{return *take_;}
  bool owns(const std::string &identity)const noexcept{return take_->owns(identity);}
  const RecordedGestureBoundary::Cuts &stopCuts()const noexcept{return stoppedCuts_;}
  struct CommitResult {bool accepted=false,changed=false;};
  // The session stages one native history/persistence/audio transaction in
  // publish. It must not pump messages or reenter intake during that callback;
  // returning false leaves every target unretired for a fresh retry.
  template<class Publish> CommitResult commit(const NativeSong &native,std::span<const PluginState> rack,
      std::span<const ParameterChange> baseline,bool acknowledgeLoss,Publish &&publish) {
    Entry entry(entering_);
    if(take_->completed())return {true,false}; // A successful Keep must not publish twice.
    const auto before=current();
    require(take_->matchesBaseline(rack,baseline),"Parameter recording baseline changed before Keep; input retained");
    // Native parameter queries can pump the UI owner; prepare all catalogues
    // before selecting the final take membership and recheck every provider.
    std::map<std::string,CatalogObservation> providers;
    for(const auto &provider:take_->providers()) {
      auto observed=observeCatalog(provider.plugin);
      providers.emplace(provider.plugin,std::move(observed));
    }
    require(current()==before,"Parameter context changed during Keep preparation; input retained");
    for(const auto &[identity,provider]:providers)
      require(audio_.pluginEditSource(slot(identity))==provider.source,
        "Parameter provider changed during Keep preparation; input retained");
    for(const auto &[identity,provider]:providers)
      require(take_->observeProvider(identity,provider.source,before.first,before.second),
        "Parameter provider is incompatible with the retained take");
    CommitResult result;
    result.accepted=take_->commit(before.first,before.second,acknowledgeLoss,[&](auto owners,bool acknowledged) {
      std::vector<RecordedGestureCapture::BatchInput> inputs;inputs.reserve(owners.size());
      for(auto *owner:owners) {
        const auto &provider=providers.at(owner->key().plugin);
        owner->sourceLoss(provider.source.dropped,provider.source.incarnation);
        inputs.push_back({owner,provider.source.incarnation,provider.parameters});
      }
      const auto candidate=RecordedGestureCapture::prepareBatch(inputs,before.first,before.second,rack,native,acknowledged);
      require(current()==before,"Parameter context changed before Keep publication; input retained");
      return RecordedGestureCapture::publishBatch(candidate,inputs,[&](const auto &points,bool changed) {
        const bool accepted=publish(points,changed);
        if(accepted)result.changed=changed;
        return accepted;
      });
    });
    return result;
  }
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
    Entry entry(entering_);return restorePreviewImpl(discard);
  }
  // Retain the owner on every false return/exception, including failed native
  // lease cleanup. The session must verify opaque state/manual baselines in
  // verifyNativeState, then publish/release ownership only after success. Mac's
  // editor and render vendor are the same object: never replace an isolated
  // editor copy as the Windows adapter does. This method never drops input.
  bool restoreRetained(bool discard,const std::function<void()> &verifyNativeState) {
    Entry entry(entering_);
    require(bool(verifyNativeState),"Native take restoration needs saved-state verification");
    require(take_->completed()||(discard&&take_->endTime()),"Keep or explicitly discard a stopped take before restoration");
    releaseInputBarriers(); // Finish any earlier failed cleanup before reacquiring.
    requireRestorationCut();
    // Allocate the complete token map before freezing any native editor.
    for(const auto &[identity,catalog]:catalogs_)inputBarriers_.emplace(identity,PluginEditorInputBarrier{});
    try {
      for(auto &[identity,barrier]:inputBarriers_) {
        const auto acquired=audio_.beginPluginInputBarrier(slot(identity));
        require(acquired.has_value(),"Plugin cannot protect native input during take restoration");barrier=*acquired;
      }
      requireRestorationCut(); // Vendor calls may pump input while acquiring leases.
      if(!restorePreviewImpl(discard)){releaseInputBarriers();return false;}
      verifyNativeState(); // Fallible session work; the take remains recoverable.
      requireRestorationCut();
      releaseInputBarriers();
      requireRestorationCut(); // Re-enabling native input can execute vendor code.
      return true;
    } catch(...) {
      const auto failure=std::current_exception();releaseInputBarriers();std::rethrow_exception(failure);
    }
  }
};
}
