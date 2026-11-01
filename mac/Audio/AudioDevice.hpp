#pragma once
#include "AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/RecordingFrameClock.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <algorithm>
#include <atomic>
namespace Tracker {
struct DeviceInfo {
  uint32_t id;
  std::string name;
  std::string uid;
  bool isDefault=false;
};
class AudioDevice {
  AudioUnit unit_ = nullptr;
  std::unique_ptr<Renderer> renderer_;
  std::shared_ptr<RecordingFrameTimeline> frameTimeline_;
  uint64_t captureGeneration_=0;
  bool captureFrames_=false;
  // Native-owner operations retain this chain across reentrant vendor calls.
  // The callback borrows it without copying or releasing shared ownership.
  std::shared_ptr<PluginChain> plugins_;
  uint64_t pluginEpoch_=0;
  std::vector<PluginState> pluginStates_;
  std::vector<size_t> pendingEditors_;
  std::vector<ParameterChange> automation_;
  std::atomic<bool> playing_{false};
  std::atomic<uint64_t> callbacks_{0}, overruns_{0}, maxNanos_{0};
  std::array<std::atomic<uint64_t>, 1024> callbackHistogram_{};
  double sampleRate_ = 48000;
  double nanosPerTick_ = 1;
  uint32_t bufferSize_ = 128;
  uint32_t deviceID_ = 0;
  std::string activityTarget_;
  uint32_t activityParameter_=0;
  void restoreParameterActivity();
  // Control owner only. A false playing flag can precede callback completion.
  // Join the output unit before taking over stopped processor work; do not
  // close/trim the retained recording timeline as an explicit Stop would.
  bool joinStoppedProcessing();
  bool previewing_ = false, usesDefault_ = true, listening_ = false, renderEnded_ = false;
  uint64_t tailFrames_ = 0, tailBudgetFrames_ = 0, tailRevision_ = 0;
  std::atomic<float> outputLeft_{0}, outputRight_{0};
  std::atomic<bool> deviceChanged_{false};
  static OSStatus propertyChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *);
  void removeListeners();
  void addListeners();
  void configureUnit(AudioUnit unit, uint32_t deviceID, double sampleRate);
  static constexpr uint32_t maximumBlockFrames = 4096; // Prepared plugin/mixer block size.
  static OSStatus callback(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32,
                           AudioBufferList *);

public:
  ~AudioDevice();
  static std::vector<DeviceInfo> devices();
  void configure(uint32_t deviceID = 0, uint32_t bufferSize = 128);
  void play(const std::vector<std::byte> &, uint32_t order = 0, bool preview = false,
            const std::string &sourcePath = {}, uint32_t sequence = 0, const NativeSong *native = nullptr, PlaybackRegion region = {}, bool isolatedSample = false);
  void loop(bool enabled) { if(renderer_) renderer_->loop(enabled); }
  void stop();
  void setPlugins(const std::vector<PluginState> &states, const std::vector<ParameterChange> &automation = {},
                  bool preserveEditors = false);
  std::vector<PluginState> pluginStates();
  struct LiveRackPlan {
    std::unique_ptr<PluginChain::RackPlan> hosted;
    std::vector<PluginState> states;
    std::vector<ParameterChange> automation;
  };
  std::unique_ptr<LiveRackPlan> prepareLiveRack(const std::vector<PluginState> &,const std::vector<ParameterChange> &,const NativeSong &,std::span<const std::string> presets={});
  bool publishLiveRack(std::unique_ptr<LiveRackPlan> &) noexcept;
  struct LiveRecordedPlan {
    std::unique_ptr<RecordedAutomationPlan> hosted;
    std::vector<ParameterChange> points;
    uint64_t pluginEpoch=0;
  };
  // The same retained processor publication works while playing or stopped.
  // Stopped adoption first joins the output callback; active() alone does not
  // establish quiescence after a natural end or processor fault.
  std::unique_ptr<LiveRecordedPlan> prepareRecordedAutomation(const std::vector<ParameterChange> &,std::span<const ParameterChange> manualBaselines={});
  bool publishRecordedAutomation(std::unique_ptr<LiveRecordedPlan>);
  bool hasPluginChain()const noexcept{return bool(plugins_);}
  bool hasAutomatedState() const {return plugins_&&plugins_->hasAutomatedState();}
  std::vector<PluginProgram> pluginPrograms(size_t slot) const { return plugins_ ? plugins_->programs(slot) : std::vector<PluginProgram>{}; }
  std::vector<PluginAudioBus> pluginBuses(size_t slot) const { return plugins_ ? plugins_->buses(slot) : std::vector<PluginAudioBus>{}; }
  std::vector<PluginParameter> pluginParameters(size_t slot) {
    if (plugins_ && joinStoppedProcessing())
      plugins_->applyPending();
    return plugins_ ? plugins_->parameters(slot) : std::vector<PluginParameter>{};
  }
  std::optional<EffectMeters> pluginMeters(size_t slot) const { return plugins_ ? plugins_->meters(slot) : std::nullopt; }
  bool pluginParameter(uint32_t slot, uint32_t id, float value) {
    return plugins_ && plugins_->parameter(slot, id, value);
  }
  bool pluginParameterBatch(std::span<const ParameterChange> changes) {
    return plugins_ && plugins_->enqueueParameters(changes);
  }
  uint64_t pluginEpoch()const noexcept{return pluginEpoch_;}
  std::optional<size_t> pluginSlot(const std::string &identity)const noexcept {
    const auto found=std::find_if(pluginStates_.begin(),pluginStates_.end(),[&](const auto &state){return state.instanceID==identity;});
    return found==pluginStates_.end()?std::nullopt:std::optional<size_t>(size_t(found-pluginStates_.begin()));
  }
  bool previewPluginParameters(std::span<const ParameterChange> changes){return plugins_&&plugins_->previewParameters(changes);}
  struct ParameterPreviewOwner {std::string plugin;uint32_t parameter=0;uint64_t provider=0;};
  struct PluginStateOwner {std::string plugin;PluginEditSource source;};
  class PluginPreviewCancellation {
    friend class AudioDevice;
    const AudioDevice *owner_=nullptr;
    uint64_t epoch_=0;
    std::shared_ptr<PluginChain> chain_;
    PluginChain::ParameterPreviewCancellation hosted_;
  };
  // Does not join, drain or start processing. A stopped host with queued work
  // stays Pending; use the existing stopped restoration path in that case.
  std::optional<PluginPreviewCancellation> cancelPluginParameterPreviewsTracked(uint64_t,std::span<const ParameterPreviewOwner>);
  PluginChain::PreviewCancellationState pluginPreviewCancellationState(const PluginPreviewCancellation &) const noexcept;
  // Native owner only; caller protects the retained take/input cuts. Unlike
  // ordinary state(), this never stops/joins audio or drains control queues.
  std::optional<NativePlugin::ProcessingState> pluginProcessingState(uint64_t expectedEpoch,const PluginStateOwner &);
  // Disposable-provider comparison only. Caller protects input, requires the
  // cancellation processing receipt and rechecks its document/fixed Stop cuts.
  void verifyProcessingPluginBaselines(uint64_t,std::span<const PluginStateOwner>,std::span<const PluginState>);
  // Caller holds the full group's native input leases and rechecks its fixed
  // cuts before and after. Never stops an active device or rewrites saved blobs.
  // Refusal/exception retains the take, including after partial vendor restore.
  void restoreStoppedPluginBaselines(uint64_t expectedEpoch,std::span<const PluginStateOwner>,std::span<const PluginState>);
  struct StoppedPluginRepair {
    const AudioDevice *owner=nullptr;
    uint64_t epoch=0;
    std::vector<PluginStateOwner> providers;
    std::vector<PluginEditBoundary> originalCuts,replacementCuts;
    std::unique_ptr<PluginChain::StoppedRepair> hosted;
    bool adopted()const noexcept{return hosted&&hosted->adopted;}
    // This addresses the retained old backend, never a current slot. Cleanup
    // stays retryable even if a later operation replaced the AudioDevice host.
    bool releaseRetiredInputBarrier(const std::string &,PluginEditorInputBarrier);
  };
  // Closed-editor opaque repair. The native take owns this receipt (including
  // retired providers after adoption) until old input-cut/lease cleanup succeeds.
  // Never reuse the old take's incarnation on a repaired provider. The next
  // play() prepares a fresh chain; no old renderer resumes after this adoption.
  std::unique_ptr<StoppedPluginRepair> prepareStoppedPluginRepair(uint64_t expectedEpoch,
    std::span<const PluginStateOwner>,std::span<const PluginState>);
  bool adoptStoppedPluginRepair(StoppedPluginRepair &);
  // Control owner only. Resolve stable identities again: live rack publication
  // can reorder slots without replacing the whole chain. Guard the native
  // incarnation as well as the epoch before taking over stopped processing.
  bool cancelPluginParameterPreviews(uint64_t expectedEpoch,std::span<const ParameterPreviewOwner> owners) {
    if(!plugins_||expectedEpoch!=pluginEpoch_)return false;
    std::vector<PluginChain::ParameterPreviewTarget> targets;targets.reserve(owners.size());
    const auto resolve=[&] {
      targets.clear();
      for(const auto &owner:owners) {
        const auto slot=pluginSlot(owner.plugin);
        if(!slot||*slot>UINT32_MAX||!owner.provider||plugins_->editSource(*slot).incarnation!=owner.provider)return false;
        targets.push_back({uint32_t(*slot),owner.parameter});
      }
      return true;
    };
    if(!resolve())return false;
    if(joinStoppedProcessing())plugins_->applyPending();
    // Pending accepted work can adopt a prepared vendor. Never restore that
    // new incarnation with an old take just because its stable ID survived.
    if(expectedEpoch!=pluginEpoch_||!resolve())return false;
    // The shared publisher validates all IDs and reserves the complete queue
    // before clearing any transient ownership or publishing a restore prefix.
    return plugins_->cancelParameterPreviews(targets);
  }
  bool cancelPluginParameterPreview(size_t slot,uint32_t id){
    if(!plugins_||!plugins_->hasParameterPreview(slot,id))return true;
    if(joinStoppedProcessing())plugins_->applyPending();
    const std::array<uint32_t,1> ids{id};return plugins_->cancelParameterPreviews(slot,ids);
  }
  bool pluginBypass(size_t slot,bool value) {
    if(slot>=pluginStates_.size()||(plugins_&&!plugins_->bypass(slot,value)))return false;
    pluginStates_[slot].bypass=value;return true;
  }
  void showPluginEditor(size_t slot) {
    const auto owner=plugins_;
    if(!owner)throw std::runtime_error("Resolve unavailable plugins before opening an editor");
    owner->showEditor(slot);
  }
  void closePluginEditor(size_t slot,bool flushStoppedControls) {
    const auto owner=plugins_;
    if(!owner)throw std::runtime_error("Resolve unavailable plugins before closing an editor");
    owner->closeEditor(slot);
    // A stopped VST3 still has its editor-to-processor mailbox. Drain that
    // accepted prefix before a subsequent Undo queues its newer value, or a
    // later state capture can replay the old editor value over the Undo.
    // Native state capture flushes without starting audio; discard its opaque
    // snapshot so effective automation never becomes the saved baseline.
    // Retained recording input has a separate owner and must bypass this step.
    if(flushStoppedControls&&plugins_==owner&&joinStoppedProcessing())(void)owner->state(slot);
  }
  std::vector<size_t> openPluginEditors() const {return plugins_?plugins_->openEditors():std::vector<size_t>{};}
  bool popPluginEdit(size_t slot, uint32_t &id, float &value) { return plugins_ && plugins_->popEdit(slot, id, value); }
  bool popPluginEdit(size_t slot,PluginEdit &event) {return plugins_&&plugins_->popEdit(slot,event);}
  bool popPluginEdit(size_t slot,PluginEdit &event,PluginEditSource &source) {
    source={};return plugins_&&plugins_->popEdit(slot,event,source);
  }
  PluginEditSource pluginEditSource(size_t slot) const noexcept {return plugins_?plugins_->editSource(slot):PluginEditSource{};}
  uint64_t droppedPluginEdits(size_t slot) const noexcept {return plugins_?plugins_->droppedEdits(slot):0;}
  // Retained-take adapter; caller keeps pluginEpoch/identity guards and must
  // not mix these raw reads with the legacy manual popPluginEdit consumer.
  std::optional<PluginEditBoundary> pluginInputBoundary(size_t slot,uint64_t hostOnlyTime=0) {
    return plugins_?plugins_->editorInputBoundary(slot,hostOnlyTime):std::nullopt;
  }
  PluginEditRead popPluginInputBefore(size_t slot,const PluginEditBoundary &boundary,PluginEdit &event,PluginEditSource &source)noexcept {
    source={};return plugins_?plugins_->popEditorInputBefore(slot,boundary,event,source):PluginEditRead::InvalidFence;
  }
  std::optional<PluginEditorInputBarrier> beginPluginInputBarrier(size_t slot) {
    return plugins_?plugins_->beginEditorInputBarrier(slot):std::nullopt;
  }
  bool endPluginInputBarrier(size_t slot,PluginEditorInputBarrier barrier) {
    return plugins_&&plugins_->endEditorInputBarrier(slot,barrier);
  }
  double pluginLatency() const { return plugins_ ? plugins_->latency() : 0; }
  NoteActivitySnapshot noteActivity() const {return plugins_?plugins_->noteActivity():NoteActivitySnapshot{};}
  bool graphController(uint8_t cc,uint8_t value) {return plugins_ && plugins_->graphController(cc,value);}
  std::vector<SignalActivity> graphActivity() const {return active()&&plugins_?plugins_->graphActivity():std::vector<SignalActivity>{};}
  void watchParameterActivity(const std::string &key,uint32_t parameter,double baseline,bool clear) {
    if(!plugins_)throw std::runtime_error("No prepared parameter processors");
    const bool stopped=joinStoppedProcessing();
    plugins_->parameterActivity().watch(key,parameter,baseline,clear);activityTarget_=key;activityParameter_=parameter;
    if(stopped)plugins_->parameterActivity().begin(0);
  }
  ParameterActivity *parameterActivity() {return plugins_?&plugins_->parameterActivity():nullptr;}
  SignalObservation *signalObservation() {return plugins_?&plugins_->signalObservation():nullptr;}
  bool pluginFailed() const { return plugins_ && plugins_->failed(); }
  bool pluginLatencyChanged() const noexcept { return plugins_ && plugins_->latencyChangePending(); }
  void refreshPluginLatencies(const NativeSong &);
  std::unique_ptr<ScratchGestureLibrary> prepareScratchUpdate(const NativeSong &native) const {
    return playing()&&renderer_?renderer_->prepareScratchUpdate(native):nullptr;
  }
  bool publishScratchUpdate(std::unique_ptr<ScratchGestureLibrary> &plan) noexcept {
    return renderer_&&renderer_->publishScratchUpdate(plan);
  }
  std::unique_ptr<PreciseNoteUpdate> preparePreciseUpdate(const NativeSong &native,const std::vector<Edit> &cells) const {
    if(!playing())return {};
    if(!renderer_ || cells.size()>512)
      throw std::runtime_error("This precise-note edit exceeds the live cell capacity; stop playback before applying it");
    return std::make_unique<PreciseNoteUpdate>(native,cells);
  }
  bool publishPreciseUpdate(std::unique_ptr<PreciseNoteUpdate> &plan) noexcept {
    return renderer_&&renderer_->publishPreciseNoteUpdate(plan);
  }
  void updateMusicalAutomation(const NativeSong &native) {if(active()&&plugins_)plugins_->updateMusicalAutomation(native);}

  bool mixerControls(const std::vector<MixerControls> &controls) { return !active() || (plugins_ && plugins_->mixerControls(controls)); }
  bool mixerRoutingReady() {return !active() || !plugins_ || plugins_->mixerRoutingReady();}
  MixerTransition::Reading mixerRoutingReading() const noexcept {return plugins_?plugins_->mixerRoutingReading():MixerTransition::Reading{};}
  std::unique_ptr<MixerTransition::Plan> prepareMixerRouting(const NativeSong &native);
  std::unique_ptr<GraphControlPlan> prepareGraphControls(const NativeSong &native) {return active()&&plugins_?plugins_->prepareGraphControls(native):nullptr;}
  std::unique_ptr<GraphControlPlan> prepareGraphParameterPreview(const NativeSong &native,
      uint64_t graph,uint64_t node,uint32_t parameter,double value,std::span<const PluginParameter> catalog) {
    return active()&&plugins_?plugins_->prepareGraphParameterPreview(native,graph,node,parameter,value,catalog):nullptr;
  }
  bool restoreGraphParameterPreview(const NativeSong &native) {
    if(!plugins_)return true;
    if(joinStoppedProcessing())plugins_->applyPending();
    auto plan=plugins_->prepareGraphControls(native);
    return plan&&plugins_->publishGraphControls(std::move(plan));
  }
  bool publishGraphControls(std::unique_ptr<GraphControlPlan> plan) {return plugins_&&plugins_->publishGraphControls(std::move(plan));}
  bool publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &plan) noexcept {return plugins_ && plugins_->publishMixerRouting(plan);}
  std::vector<MixerMeter> mixerMeters() const { return active() && plugins_ ? plugins_->mixerMeters() : std::vector<MixerMeter>{}; }
  MixerMeterReading identifiedMixerMeters() const { return active() && plugins_ ? plugins_->identifiedMixerMeters() : MixerMeterReading{}; }
  bool deviceChanged() const { return deviceChanged_.load(); }
  void refreshDevice() {
    deviceChanged_ = false;
    configure(usesDefault_ ? 0 : deviceID_, bufferSize_);
  }
  bool playing() const { return playing_.load() && !previewing_; }
  bool active() const { return playing_.load(); }
  Renderer *renderer() { return renderer_.get(); }
  std::shared_ptr<const RecordingFrameTimeline> recordingFrameTimeline() const {return frameTimeline_;}
  Telemetry telemetry() const;
  double sampleRate() const { return sampleRate_; }
  uint32_t bufferSize() const { return bufferSize_; }
  uint32_t deviceID() const { return deviceID_; }
};
} // namespace Tracker
