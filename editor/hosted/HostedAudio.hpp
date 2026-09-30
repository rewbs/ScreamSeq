#pragma once
#include "PluginTypes.hpp"
#include "PluginBackend.hpp"
#include "ParameterActivity.hpp"
#include "SignalObservation.hpp"
#include "ProcessorBypass.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "editor/MusicalAutomation.hpp"
#include "editor/RealtimePlan.hpp"
#include "editor/SampleRamp.hpp"
#include "editor/MixerRuntime.hpp"
#include "editor/MixerTransition.hpp"
#include "RenderOnce.hpp"
#include "editor/NativeEffects.hpp"
#include "editor/SignalGraph.hpp"
namespace OpenMPT {class CSoundFile;struct ModInstrument;}
namespace Tracker {
class PatternCommandRuntime;
class PatternPitchRuntime;
class NativeSignalGraph;
class SignalRuntime;
struct SignalControls;
class Renderer;
struct NativeSong;
class NativePlugin;
struct GraphParameterUpdate { NativePlugin *plugin=nullptr;SignalRuntime *runtime=nullptr;uint64_t node=0;uint32_t parameter=0;double value=0;double *appliedBaseline=nullptr;double initialBaseline=0; };
struct GraphControlPlan {
  // Producer-owned snapshot: routing publication must not accidentally accept
  // an Undo that changes these controls without also publishing them.
  SignalGraph signal;
  std::vector<GraphParameterUpdate> updates;
  struct Reading {NativePlugin *plugin;uint32_t parameter;double value;};
  std::vector<Reading> readings; // Control-owned catalog baselines, not effective audio values.
  std::vector<std::shared_ptr<const SignalControls>> controls;
  std::vector<std::pair<SignalRuntime *,const SignalControls *>> runtimes;
};
class NativePlugin {
  static constexpr uint32_t maximumFrames = 4096;
  double latency_ = 0, tail_ = 0;
  PluginDescriptor descriptor_;
  std::string instanceID_;
  uint32_t assignedInstrument_ = 0, midiChannel_ = 1;
  std::vector<PluginInstrumentAlias> aliases_;
  PluginTransport transport_;
  double rate_ = 48000;
  ProcessorBypass bypassControl_;
  std::unique_ptr<PluginBackend> backend_;
  std::unique_ptr<NativeEffect> builtin_;
  std::vector<PluginAudioBus> buses_;
  std::vector<uint32_t> auxiliaryInputs_, auxiliaryOutputs_;
  std::array<std::unique_ptr<PluginAudioStorage>, 64> auxiliaryOutputBuffers_;
  std::array<const float *, 64> inputSources_{};
  std::vector<float> outputDelay_;
  size_t outputDelayPosition_ = 0;
  std::vector<ParameterChange> automation_;
  size_t automationPosition_ = 0;
  uint64_t renderedThrough_ = 0;
  ParameterActivity *activity_ = nullptr;
  uint32_t activityProcessor_ = 0;
  bool activityAudible_ = true;
  struct TimedParameter { uint32_t id; double value; uint64_t frame, sequence; uint64_t duration = 0; double target = 0; ParameterSource source; };
  struct ActiveRamp { uint32_t id = 0; bool active = false; SampleRamp ramp; ParameterSource source; };
  std::array<ActiveRamp, 64> parameterRamps_{};
  uint64_t musicalSequence_ = 0;
  std::unique_ptr<std::array<TimedParameter, 65536>> musicalEvents_;
  size_t musicalCount_ = 0;
  struct TimedMIDI {uint64_t frame,sequence;uint8_t status,a,b;};
  std::unique_ptr<std::array<TimedMIDI,65536>> musicalMIDI_;
  size_t musicalMIDICount_ = 0;
  bool processBlock(float *, uint32_t, uint64_t, uint32_t offset) noexcept;

public:
  NativePlugin(const PluginState &, double sampleRate, bool offline = false);
  ~NativePlugin();
  NativePlugin(const NativePlugin &) = delete;
  bool process(float *interleaved, uint32_t frames, uint64_t position,
               std::span<const PluginAudioInput> inputs = {}) noexcept;
  void bypass(bool value) noexcept {bypassControl_.set(value);}
  bool bypassed() const noexcept {return bypassControl_.requested();}
  size_t bypassStorageBytes() const noexcept{return bypassControl_.storageBytes();}
  const std::vector<PluginAudioBus> &buses() const { return buses_; }
  const float *auxiliaryOutput(uint32_t bus) const noexcept {
    return bus < auxiliaryOutputBuffers_.size() && auxiliaryOutputBuffers_[bus]
      ? auxiliaryOutputBuffers_[bus]->interleaved.data() : nullptr;
  }
  bool parameter(uint32_t id, float value, uint32_t offset = 0) noexcept;
  bool appliedParameter(uint32_t id,double value,uint64_t frame,ParameterSource source,uint32_t offset=0) noexcept;
  void observe(ParameterActivity *activity,uint32_t processor) noexcept {activity_=activity;activityProcessor_=processor;}
  void observedBaseline(uint32_t,double) noexcept; // Control owner only.
  void audible(bool value) noexcept {activityAudible_=value;}
  void contribution(uint32_t parameter,uint64_t source,double value,uint64_t frame) noexcept {if(activity_)activity_->contribution(activityProcessor_,parameter,source,value,frame);}

  std::vector<PluginParameter> parameters() const;
  std::vector<PluginProgram> programs() const;
  void loadProgram(const std::string &id); // Control-thread only, on a stopped/prepared instance.
  std::optional<EffectMeters> meters() const noexcept { return builtin_ ? builtin_->meters() : std::nullopt; }
  PluginState state() const;
  std::vector<PluginInstrumentAlias> assignments() const;
  double latency() const { return latency_; }
  bool latencyChangePending() const noexcept;
  void refreshLatency(); // Audio must be quiescent.
  double tail() const;
  uint64_t tailRevision() const noexcept;
  void includeParameterRange(uint32_t id, float minimum, float maximum) noexcept;
  static std::vector<PluginDescriptor> discover();
  static std::vector<PluginDescriptor> builtins();
  static std::vector<PluginDescriptor> discoverVST3(const std::string &path);
  bool isInstrument() const { return descriptor_.instrument || descriptor_.type == audioUnitMusicDeviceType; }
  bool midi(uint8_t status, uint8_t data1, uint8_t data2) noexcept;
  void automate(const std::vector<ParameterChange> &, size_t slot, double rate, uint64_t start);
  bool schedule(uint32_t id, float value, uint64_t frame, ParameterSource source = {}) noexcept;
  bool scheduleRamp(uint32_t id, double from, double to, uint64_t frame, uint64_t duration, ParameterSource source = {}) noexcept;
  void cancelScheduledParameter(uint32_t id) noexcept;
  void prepareMusicalMIDI() {if(!musicalMIDI_)musicalMIDI_=std::make_unique<std::array<TimedMIDI,65536>>();}
  bool scheduleMIDI(uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept;
  void prepareMusicalAutomation() {
    if (!musicalEvents_) musicalEvents_ = std::make_unique<std::array<TimedParameter, 65536>>();
  }
  uint64_t renderedThrough() const { return renderedThrough_; }
  void showEditor();
  void closeEditor();
  bool editorOpen() const;
  void transport(const PluginTransport &t) noexcept { transport_ = t; if (backend_) backend_->transport(t); }
  void compensateLatency(uint32_t frames) {
    if (outputDelay_.size() == size_t(frames) * 2) return;
    outputDelay_.assign(size_t(frames) * 2, 0);
    outputDelayPosition_ = 0;
  }
  bool popEdit(uint32_t &, float &) noexcept;
  PluginFailure failure() const noexcept { return backend_ ? backend_->failure() : PluginFailure{}; }
};
class PluginChain {
  struct MusicalLane {
    size_t slot;
    uint32_t parameter;
    float minimum, maximum;
    std::vector<AutomationPoint> points;
    bool hasStepNext = false;
    uint32_t endPosition = 0;
    bool continuous = false;
    uint64_t id=0;
  };
  struct MusicalPlan {
    std::vector<std::vector<MusicalLane>> patterns;
    std::vector<ParameterChange> reset;
  };
  std::unique_ptr<MusicalPlan> initialMusicalPlan_;
  RealtimePlan<MusicalPlan> musicalUpdates_;
  const MusicalPlan *musicalPlan_ = nullptr;
  std::vector<std::vector<PluginParameter>> musicalCatalog_;
  std::vector<std::pair<size_t,uint32_t>> musicalTargets_; // Control owner; includes retired lanes.
  std::unique_ptr<MusicalPlan> prepareMusicalPlan(const NativeSong &) const;
  void consumeMusicalPlan() noexcept;
  std::shared_ptr<PatternCommandRuntime> commandRuntime_;
  std::shared_ptr<PatternPitchRuntime> pitchRuntime_;
  OpenMPT::CSoundFile *musicalSong_ = nullptr;
  std::atomic<bool> hasMusicalControls_{false};
  uint64_t musicalPosition_ = 0;
  uint32_t musicalPattern_ = UINT32_MAX;
  std::unique_ptr<ParameterActivity> activity_;
  std::unique_ptr<SignalObservation> observation_;
  struct ObservedProcessor {std::array<uint32_t,64> input{},output{};};
  std::vector<ObservedProcessor> processorObservations_;
  std::vector<std::array<uint32_t,2>> busObservations_;
  std::vector<std::shared_ptr<NativePlugin>> plugins_;
  std::vector<std::string> instances_;
  std::vector<std::pair<std::vector<uint32_t>,std::vector<uint32_t>>> explicitPorts_;
  void prepareRoutingPorts(const MixerGraph &);
  std::vector<uint32_t> instruments_;
  std::array<float, 8192> tailBuffer_{};
  std::vector<float> dryDelay_;
  size_t dryDelayPosition_ = 0;
  uint64_t dryThrough_ = 0;
  double sampleRate_ = 48000;
  bool offline_ = false;
  std::shared_ptr<NativeSignalGraph> signalGraph_,sampleSignalGraph_;
  SignalGraph preparedSignal_; // Immutable control-side construction baseline.
  RealtimePlan<GraphControlPlan> graphControlPlans_;
  const GraphControlPlan *lastGraphControls_=nullptr; // Producer-only; newest published slot remains owned.
  struct SampleRoute {const OpenMPT::ModInstrument *instrument=nullptr;uint16_t channel=0,slot=0;size_t processor=0;uint64_t instrumentID=0,target=0,previewRemaining=0,previewTail=0;};
  std::vector<SampleRoute> sampleRoutes_;
  std::array<float,8192> sampleGraphBuffer_{};
  std::vector<bool> bypass_;
  struct QueuedParameter {ParameterChange change;bool last=false,observed=false;};
  std::array<QueuedParameter, 4096> queue_{};
  alignas(64) std::atomic<uint32_t> write_{0};
  alignas(64) std::atomic<uint32_t> read_{0};
  bool parameterBlockOpen_=false; // Audio owner only; excludes mid-block updates.
  std::atomic<bool> failed_{false};
  std::vector<ParameterChange> automation_;
  size_t automationPosition_ = 0;
  uint64_t position_ = 0;
  double latency_ = 0, tail_ = 0;
  std::vector<double> compiledTails_;
  void captureTails();
  // The executor owns immutable routing plans; this alias is render-thread
  // only after attachInstruments. Control queries use controlPlan instead.
  MixerRuntime *mixer_=nullptr;
  std::unique_ptr<MixerTransition> mixerTransition_;
  struct MixerProcessor {
    std::shared_ptr<NativePlugin> plugin;
    std::shared_ptr<NativeSignalGraph> graph;
    size_t graphIndex=0;
    std::vector<uint32_t> outputs;
    bool process(float *,uint32_t,uint64_t,std::span<const PluginAudioInput>) noexcept;
    const float *output(uint32_t) const noexcept;
  };
  struct HostedMixerPlan {
    PluginChain *owner=nullptr;
    std::vector<std::shared_ptr<RenderOnce<MixerProcessor>>> processors;
    std::vector<std::array<uint32_t,2>> busObservations;
    static bool process(void *,MixerRuntime &,size_t,float *,uint32_t,uint64_t) noexcept;
    static void observe(void *,size_t,bool,const float *,uint32_t,uint64_t) noexcept;
  };
  struct MixerDirectInput {
    std::array<float,MixerRuntime::maximumFrames> left{},right{};
    bool captured=false;
  };
  std::vector<std::unique_ptr<MixerDirectInput>> mixerDirect_;
  std::vector<MixerTransition::DirectInput> mixerInputs_;
  std::vector<uint64_t> mixerTracks_;
  std::vector<MixerProcessorInfo> mixerProcessors_;
  Renderer *mixerRenderer_ = nullptr; // Playback renderer outlives its processing calls.
  bool finishMixer(float *, uint32_t) noexcept;

public:
  ParameterActivity &parameterActivity(){return *activity_;}
  SignalObservation &signalObservation(){return *observation_;}
  PluginChain(const std::vector<PluginState> &, double sampleRate, bool offline = false,
              const std::vector<ParameterChange> &automation = {}, uint64_t startFrame = 0);
  bool process(float *, uint32_t frames) noexcept;
  void attachInstruments(Renderer &, const NativeSong *native = nullptr);
  bool canUpdateMusicalAutomation() const noexcept {return musicalUpdates_.available();}
  void updateMusicalAutomation(const NativeSong &);
  bool hasMixer() const { return bool(mixer_); }
  void beginMixer(uint32_t frames) noexcept;
  bool graphController(uint8_t,uint8_t) noexcept;
  std::vector<SignalActivity> graphActivity() const;
  void routeInstrument(size_t processor, const float *buffer,uint32_t frames,uint64_t position) noexcept;
  void processSampleGraph(size_t,const float *,const float *,uint32_t,float * = nullptr,float * = nullptr) noexcept;
  // Core adapters capture channel sources. The final adapter evaluates the
  // prepared graph order independently of fixed OpenMPT plugin-slot order.
  const float *captureMixerBus(size_t bus,const float *,const float *,uint32_t frames,bool finish) noexcept;
  bool mixerControls(const std::vector<MixerControls> &controls) noexcept { return mixerTransition_ && mixerTransition_->controls(controls); }
  std::vector<MixerMeter> mixerMeters() const { return mixerTransition_ ? mixerTransition_->controlPlan().runtime->meters() : std::vector<MixerMeter>{}; }
  bool mixerRoutingReady() noexcept {return !mixerTransition_ || mixerTransition_->ready();}
  MixerTransition::Reading mixerRoutingReading() const noexcept {return mixerTransition_?mixerTransition_->reading():MixerTransition::Reading{};}
  // First live-routing path: unchanged processors, sources and total latency.
  // Null means the host needs a more extensive processor/adapter preparation.
  std::unique_ptr<MixerTransition::Plan> prepareMixerRouting(const NativeSong &);
  bool publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &) noexcept;
  std::unique_ptr<GraphControlPlan> prepareGraphControls(const NativeSong &);
  bool publishGraphControls(std::unique_ptr<GraphControlPlan> plan) {const auto *published=plan.get();if(!graphControlPlans_.publish(std::move(plan)))return false;lastGraphControls_=published;for(const auto &r:published->readings)r.plugin->observedBaseline(r.parameter,r.value);return true;}
  void attachMusicalAutomation(Renderer &, const NativeSong &);
  void scheduleMusical(uint32_t pattern, double tickPosition, double unitsPerSample,
                       uint32_t samplesIntoTick, uint32_t frames, bool tickStart) noexcept;
  void syncTransport(Renderer &) noexcept;
  void delayDry(float *, float *, uint32_t, uint64_t) noexcept;
  void endNotes() noexcept;
  void showEditor(size_t slot);
  std::vector<size_t> openEditors() const;
  bool popEdit(size_t slot, uint32_t &, float &) noexcept;
  bool parameter(uint32_t slot, uint32_t id, float value) noexcept;
  // Single control producer. One release publishes the complete batch, or no
  // values on failure. Caller validates IDs/ranges against its baseline catalog.
  bool enqueueParameters(std::span<const ParameterChange>) noexcept;
  bool bypass(size_t slot,bool value) noexcept;
  size_t bypassStorageBytes() const noexcept;
  // Pair with process(): instrument/mixer/effect stages see the same boundary.
  // Standalone process() callers retain their automatic parameter consumption.
  void beginRenderBlock() noexcept;
  std::vector<PluginState> states();
  void applyPending() noexcept;
  std::vector<PluginParameter> parameters(size_t slot) const;
  std::vector<PluginProgram> programs(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->programs() : std::vector<PluginProgram>{}; }
  std::optional<EffectMeters> meters(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->meters() : std::nullopt; }
  std::vector<PluginAudioBus> buses(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->buses() : std::vector<PluginAudioBus>{}; }
  bool failed() const { return failed_.load(); }
  std::vector<PluginFailureEntry> failureDiagnostics() const; // Control owner only.
  bool latencyChangePending() const noexcept;
  void refreshLatencies(); // Control thread, retaining processors and transport.
  bool hasAutomatedState() const { return hasMusicalControls_ || !automation_.empty(); }
  double latency() const { return latency_; }
  double tail() const;
  uint64_t tailRevision() const noexcept;
  uint64_t position() const { return position_; }
};
} // namespace Tracker
