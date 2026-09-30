#pragma once
#include "AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
namespace Tracker {
struct DeviceInfo {
  uint32_t id;
  std::string name;
};
class AudioDevice {
  AudioUnit unit_ = nullptr;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<PluginChain> plugins_;
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
  bool previewing_ = false, usesDefault_ = true, listening_ = false, renderEnded_ = false;
  uint64_t tailFrames_ = 0, tailBudgetFrames_ = 0, tailRevision_ = 0;
  std::atomic<float> outputLeft_{0}, outputRight_{0};
  std::atomic<bool> deviceChanged_{false};
  static OSStatus propertyChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *);
  void removeListeners();
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
  bool hasAutomatedState() const {return plugins_&&plugins_->hasAutomatedState();}
  std::vector<PluginProgram> pluginPrograms(size_t slot) const { return plugins_ ? plugins_->programs(slot) : std::vector<PluginProgram>{}; }
  std::vector<PluginAudioBus> pluginBuses(size_t slot) const { return plugins_ ? plugins_->buses(slot) : std::vector<PluginAudioBus>{}; }
  std::vector<PluginParameter> pluginParameters(size_t slot) {
    if (plugins_ && !active())
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
  bool pluginBypass(size_t slot,bool value) {
    if(slot>=pluginStates_.size()||(plugins_&&!plugins_->bypass(slot,value)))return false;
    pluginStates_[slot].bypass=value;return true;
  }
  void showPluginEditor(size_t slot) {
    if (plugins_)
      plugins_->showEditor(slot);
  }
  bool popPluginEdit(size_t slot, uint32_t &id, float &value) { return plugins_ && plugins_->popEdit(slot, id, value); }
  double pluginLatency() const { return plugins_ ? plugins_->latency() : 0; }
  bool graphController(uint8_t cc,uint8_t value) {return plugins_ && plugins_->graphController(cc,value);}
  std::vector<SignalActivity> graphActivity() const {return active()&&plugins_?plugins_->graphActivity():std::vector<SignalActivity>{};}
  void watchParameterActivity(const std::string &key,uint32_t parameter,double baseline,bool clear) {
    if(!plugins_)throw std::runtime_error("No prepared parameter processors");
    plugins_->parameterActivity().watch(key,parameter,baseline,clear);activityTarget_=key;activityParameter_=parameter;
    if(!active())plugins_->parameterActivity().begin(0);
  }
  ParameterActivity *parameterActivity() {return plugins_?&plugins_->parameterActivity():nullptr;}
  SignalObservation *signalObservation() {return plugins_?&plugins_->signalObservation():nullptr;}
  bool pluginFailed() const { return plugins_ && plugins_->failed(); }
  bool pluginLatencyChanged() const noexcept { return plugins_ && plugins_->latencyChangePending(); }
  void refreshPluginLatencies();
  void updateMusicalAutomation(const NativeSong &native) {if(active()&&plugins_)plugins_->updateMusicalAutomation(native);}

  bool mixerControls(const std::vector<MixerControls> &controls) { return !active() || (plugins_ && plugins_->mixerControls(controls)); }
  bool mixerRoutingReady() {return !active() || !plugins_ || plugins_->mixerRoutingReady();}
  MixerTransition::Reading mixerRoutingReading() const noexcept {return plugins_?plugins_->mixerRoutingReading():MixerTransition::Reading{};}
  std::unique_ptr<MixerTransition::Plan> prepareMixerRouting(const NativeSong &native) {
    return active() && plugins_?plugins_->prepareMixerRouting(native):nullptr;
  }
  std::unique_ptr<GraphControlPlan> prepareGraphControls(const NativeSong &native) {return active()&&plugins_?plugins_->prepareGraphControls(native):nullptr;}
  bool publishGraphControls(std::unique_ptr<GraphControlPlan> plan) {return plugins_&&plugins_->publishGraphControls(std::move(plan));}
  bool publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &plan) noexcept {return plugins_ && plugins_->publishMixerRouting(plan);}
  std::vector<MixerMeter> mixerMeters() const { return active() && plugins_ ? plugins_->mixerMeters() : std::vector<MixerMeter>{}; }
  bool deviceChanged() const { return deviceChanged_.load(); }
  void refreshDevice() {
    deviceChanged_ = false;
    configure(usesDefault_ ? 0 : deviceID_, bufferSize_);
  }
  bool playing() const { return playing_.load() && !previewing_; }
  bool active() const { return playing_.load(); }
  Renderer *renderer() { return renderer_.get(); }
  Telemetry telemetry() const;
  double sampleRate() const { return sampleRate_; }
  uint32_t bufferSize() const { return bufferSize_; }
  uint32_t deviceID() const { return deviceID_; }
};
} // namespace Tracker
