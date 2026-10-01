#pragma once
#include "PluginTypes.hpp"
#include "PluginBackend.hpp"
#include "ParameterActivity.hpp"
#include "SignalObservation.hpp"
#include "ProcessorBypass.hpp"
#include "InstrumentSources.hpp"
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
#include "editor/SongModulation.hpp"
namespace OpenMPT {class CSoundFile;struct ModInstrument;}
namespace Tracker {
class PatternCommandRuntime;
class PatternPitchRuntime;
class NativeSignalGraph;
class SignalRuntime;
struct SignalControls;
class Renderer;
struct NativeSong;
struct PluginSongModulation {
  SongModulationRuntime *runtime=nullptr;
  SignalClock clock;
  uint64_t frame=0;
  struct Target {uint32_t parameter;size_t index;double minimum,maximum;};
  std::vector<Target> targets;
};
// Borrowed for one synchronous processor call; at most64 targets with32
// normalized values each. The graph runtime owns all sample arrays.
struct PluginParameterSamples {uint32_t parameter=0;double minimum=0,maximum=1;std::span<const double> values;ParameterSource source;};
class NativePlugin;
struct HostedNoteRoutingPlan {
  uint64_t revision=0;
  std::unique_ptr<NoteRoutingPlan> routes;
  std::array<uint64_t,256> tracks{},instruments{};
  std::array<bool,256> pluginInstruments{};
  std::vector<std::shared_ptr<NativePlugin>> endpoints;
  size_t storageBytes() const noexcept;
};
struct RecordedAutomationTimeline {
  std::vector<ParameterChange> points; // Chronological device-rate frames.
  struct Parameter {uint32_t id=0;float baseline=0,minimum=0,maximum=0;std::vector<ParameterChange> points;};
  std::vector<Parameter> parameters; // At most 1024 binary searches at adoption.
  size_t storageBytes() const noexcept;
};
struct RecordedAutomationPlan {
  uint64_t sourceRevision=0;
  uint32_t parameterFence=0; // Manual batches published before this timeline.
  bool active=false;
  struct Lane {std::shared_ptr<NativePlugin> plugin;RecordedAutomationTimeline timeline;};
  std::vector<Lane> lanes; // Complete snapshot, including fading retired processors.
  std::vector<NativePlugin *> rack; // Producer guard against a changed rack.
};
class GraphPluginEndpoint;
struct GraphPluginState;
// An immutable prepared processor lookup. Endpoints survive changes to the
// execution schedule; only the control producer owns/retires these shared refs.
struct GraphProcessorSet {
  struct Entry { uint64_t id=0;std::shared_ptr<GraphPluginEndpoint> endpoint; };
  std::vector<Entry> entries;
  // Retired vendors stay control-owned for remove/Undo, including their hidden
  // state. They receive no audio until restored to entries by a prepared plan.
  std::vector<Entry> retired;
  Entry *find(uint64_t id) noexcept {for(auto &entry:entries)if(entry.id==id)return &entry;return nullptr;}
};
struct TimedPluginParameter { uint32_t id; double value; uint64_t frame, sequence; uint64_t duration = 0; double target = 0; ParameterSource source; };
using PluginParameterQueue = std::array<TimedPluginParameter,65536>;
struct GraphParameterUpdate { NativePlugin *plugin=nullptr;SignalRuntime *runtime=nullptr;uint64_t node=0;uint32_t parameter=0;double value=0;double *appliedBaseline=nullptr;double initialBaseline=0;GraphPluginEndpoint *endpoint=nullptr;bool resetModulation=false; };
struct GraphControlPlan {
  uint64_t sourceRevision=0;
  size_t preparationHeadroom=256u*1024u*1024u; // Producer-only, excludes already retained storage.
  // Producer-owned snapshot: routing publication must not accidentally accept
  // an Undo that changes these controls without also publishing them.
  SignalGraph signal;
  std::shared_ptr<const HostedNoteRoutingPlan> noteRouting;
  std::shared_ptr<const InstrumentSourceBindings> instrumentBindings;
  std::vector<std::string> instrumentGenerators;
  struct Preset {GraphPluginEndpoint *endpoint;std::shared_ptr<GraphPluginState> state,previous;};
  double renderedTail=0;
  std::vector<std::pair<std::string,double>> graphTails;
  std::vector<std::pair<uint64_t *,uint64_t>> tails; // Prepared per-instance tail limits.
  struct Runtime {
    SignalRuntime **target;std::shared_ptr<SignalRuntime> initial,state;
    std::vector<std::shared_ptr<SignalRuntime>> predecessors;
    void *context=nullptr;
    void (*adopt)(void *,SignalRuntime *,GraphProcessorSet *,bool) noexcept=nullptr;
    GraphProcessorSet *processors=nullptr;
    bool structural=false;
  };
  std::vector<Runtime> runtimeOwners; // Complete snapshot; audio borrows, producer retires.
  struct Processors {GraphProcessorSet **target;std::atomic<GraphProcessorSet *> *published;std::shared_ptr<GraphProcessorSet> initial,state;std::vector<std::shared_ptr<GraphProcessorSet>> predecessors;};
  std::vector<Processors> processorOwners;
  size_t activityBase=0,preparedProcessors=0;
  struct Observation {uint32_t token=0;ParameterProcessor descriptor;};
  std::vector<Observation> observations; // Producer-only catalogue changes, committed after publication.
  std::vector<SignalPortIdentity> signalPortIdentities; // Staged with all copies; never published on failure.
  std::optional<SignalObservation::PreparedPorts> signalPorts;
  struct Scheduling {std::shared_ptr<GraphPluginState> state;std::shared_ptr<PluginParameterQueue> queue;};
  std::vector<Scheduling> scheduling;
  struct Range {NativePlugin *plugin;uint32_t parameter;float minimum,maximum;};
  std::vector<Range> ranges;
  std::vector<Preset> presets; // Every later snapshot retains audible vendors; callback never frees.
  // Complete requested state, including unchanged/inactive copies, so a newer
  // publication can safely supersede an unconsumed bypass or preset edit.
  std::vector<std::pair<GraphPluginEndpoint *,bool>> bypasses;
  std::shared_ptr<void> musicalPublication; // Prepared HostedMixerPlan payload; lifetime follows every later control snapshot.
  std::vector<GraphParameterUpdate> updates;
  struct Reading {NativePlugin *plugin;uint32_t parameter;double value;};
  std::vector<Reading> readings; // Control-owned catalog baselines, not effective audio values.
  std::vector<std::shared_ptr<const SignalControls>> controls;
  std::vector<std::pair<SignalRuntime *,const SignalControls *>> runtimes;
};
class NativePlugin {
  static constexpr uint32_t maximumFrames = 4096;
  std::atomic<double> latency_{0};
  std::atomic<double> tail_{0};
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
  uint64_t preparedInputs_=0,preparedOutputs_=0,mainInputFallback_=0;
  std::string audioLayout_;
  std::array<std::unique_ptr<PluginAudioStorage>, 64> auxiliaryOutputBuffers_;
  std::array<const float *, 64> inputSources_{};
  const float *autoDetectorSource_=nullptr;
  std::unique_ptr<std::array<float,maximumFrames*2>> autoDetectorBuffer_;
  std::vector<float> outputDelay_;
  size_t outputDelayPosition_ = 0;
  std::vector<ParameterChange> automation_;
  const std::vector<ParameterChange> *activeAutomation_=&automation_; // Audio borrows producer-owned snapshots.
  size_t automationPosition_ = 0;
  uint64_t renderedThrough_ = 0;
  ParameterActivity *activity_ = nullptr;
  uint32_t activityProcessor_ = 0;
  bool activityAudible_ = true;
  std::atomic<bool> musicalActive_{true};
  struct ActiveRamp { uint32_t id = 0; bool active = false; SampleRamp ramp; ParameterSource source; };
  std::array<ActiveRamp, 64> parameterRamps_{};
  struct Baseline {uint32_t id;double value;ParameterSource source;bool overlaid=false;};
  std::vector<Baseline> baselines_; // Immutable IDs; values are audio-owned.
  const PluginSongModulation *processingModulation_=nullptr; // Only during process().
  void prepareBaselines();
  bool effectiveParameter(uint32_t,double,uint64_t,ParameterSource,uint32_t=0) noexcept;
  bool modulationParameter(uint32_t) const noexcept;
  double baselineAt(uint32_t,uint64_t) const noexcept;
  uint64_t musicalSequence_ = 0;
  std::unique_ptr<PluginParameterQueue> initialMusicalEvents_; // Prepared before the first callback.
  PluginParameterQueue *musicalEvents_=nullptr; // Audio-owned; later queues belong to GraphPluginState.
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
               std::span<const PluginAudioInput> inputs = {},const PluginSongModulation * = nullptr,std::span<const PluginParameterSamples> = {}) noexcept;
  void bypass(bool value) noexcept {bypassControl_.set(value);}
  bool bypassed() const noexcept {return bypassControl_.requested();}
  size_t bypassStorageBytes() const noexcept{return bypassControl_.storageBytes();}
  size_t preparedStorageBytes() const noexcept {
    size_t bytes=sizeof(NativePlugin)+bypassStorageBytes()+outputDelay_.capacity()*sizeof(float)+baselines_.capacity()*sizeof(Baseline);
    if(backend_)bytes+=backend_->preparedStorageBytes();
    if(autoDetectorBuffer_)bytes+=sizeof(*autoDetectorBuffer_);
    if(initialMusicalEvents_)bytes+=sizeof(*initialMusicalEvents_);if(musicalMIDI_)bytes+=sizeof(*musicalMIDI_);
    for(const auto &bus:auxiliaryOutputBuffers_)if(bus)bytes+=sizeof(*bus);
    return bytes;
  }
  const std::vector<PluginAudioBus> &buses() const { return buses_; }
  uint64_t preparedAuxiliaryInputs() const noexcept {return preparedInputs_;}
  uint64_t preparedAuxiliaryOutputs() const noexcept {return preparedOutputs_;}
  const std::string &audioLayout()const noexcept{return audioLayout_;}
  uint64_t mainInputFallback() const noexcept {return mainInputFallback_;} // Prepared capability.
  const float *auxiliaryOutput(uint32_t bus) const noexcept {
    return bus < auxiliaryOutputBuffers_.size() && auxiliaryOutputBuffers_[bus]
      ? auxiliaryOutputBuffers_[bus]->interleaved.data() : nullptr;
  }
  bool parameter(uint32_t id, float value, uint32_t offset = 0) noexcept;
  bool appliedParameter(uint32_t id,double value,uint64_t frame,ParameterSource source,uint32_t offset=0) noexcept;
  void editorParameter(uint32_t id,double value,uint64_t frame) noexcept; // Audio owner; backend already received the editor event.
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
  struct LatencyUpdate {
    PluginLatencySnapshot snapshot;
    std::shared_ptr<ProcessorBypass::Latency> bypass;
    size_t storageBytes() const noexcept {return sizeof(*this)+(bypass?bypass->storageBytes():0);}
  };
  std::shared_ptr<LatencyUpdate> prepareLatency(); // Control owner; no vendor reset.
  void adoptLatency(LatencyUpdate &) noexcept; // Audio owner; publication retains storage.
  bool latencyReady() const noexcept {return bypassControl_.latencyReady();}
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
  const std::vector<ParameterChange> &initialAutomation() const noexcept {return automation_;}
  bool adoptRecordedAutomation(const RecordedAutomationTimeline &,uint64_t position) noexcept;
  bool schedule(uint32_t id, float value, uint64_t frame, ParameterSource source = {}) noexcept;
  bool scheduleRamp(uint32_t id, double from, double to, uint64_t frame, uint64_t duration, ParameterSource source = {}) noexcept;
  void cancelScheduledParameter(uint32_t id) noexcept;
  void prepareMusicalMIDI() {if(!musicalMIDI_)musicalMIDI_=std::make_unique<std::array<TimedMIDI,65536>>();}
  size_t musicalMIDIStorageBytes() const noexcept {return musicalMIDI_?sizeof(*musicalMIDI_):0;}
  bool scheduleMIDI(uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept;
  void musicalActive(bool active) noexcept {musicalActive_.store(active,std::memory_order_release);}
  bool musicalActive() const noexcept {return musicalActive_.load(std::memory_order_acquire);}
  void prepareMusicalAutomation() {
    if (!initialMusicalEvents_) initialMusicalEvents_ = std::make_unique<PluginParameterQueue>();
    musicalEvents_=initialMusicalEvents_.get();
  }
  bool initiallyScheduled() const noexcept {return bool(initialMusicalEvents_);} // Immutable after preparation.
  void adoptScheduling(PluginParameterQueue *queue) noexcept {if(!musicalEvents_)musicalEvents_=queue;}
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
    std::shared_ptr<NativePlugin> plugin;
    uint32_t parameter;
    float minimum, maximum;
    std::vector<AutomationPoint> points;
    bool hasStepNext = false;
    uint32_t endPosition = 0;
    bool continuous = false;
    uint64_t id=0;
  };
  struct MusicalPlan {
    uint64_t revision=1;
    std::vector<std::vector<MusicalLane>> patterns;
    struct Reset {std::shared_ptr<NativePlugin> plugin;uint32_t id;float value;};
    std::vector<Reset> reset;
  };
  std::unique_ptr<MusicalPlan> initialMusicalPlan_;
  RealtimePlan<MusicalPlan> musicalUpdates_;
  const MusicalPlan *musicalPlan_ = nullptr;
  std::vector<std::vector<PluginParameter>> musicalCatalog_;
  std::vector<std::pair<NativePlugin *,uint32_t>> musicalTargets_; // Control owner; includes retired lanes.
  std::vector<MusicalAutomationLane> musicalSpec_; // Latest producer snapshot.
  uint64_t musicalSerial_=1,musicalRenderedSerial_=1; // Separate producer/audio owners.
  std::unique_ptr<MusicalPlan> prepareMusicalPlan(const NativeSong &) const;
  void consumeMusicalPlan() noexcept;
  void activateMusicalPlan(const MusicalPlan &) noexcept;
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
  // Construction-time sources stay immutable: core adapters and pattern command
  // runtimes retain these exact objects. The control rack may reorder independently.
  std::vector<std::shared_ptr<NativePlugin>> plugins_;
  struct RackEntry {
    std::shared_ptr<NativePlugin> plugin;
    PluginState baseline;
    std::vector<PluginParameter> parameters;
    ObservedProcessor ports;
    uint32_t activity=0;
  };
  std::vector<std::shared_ptr<RackEntry>> rack_,retainedRack_; // Control owner only.
  struct InstrumentOriginal {OpenMPT::ModInstrument *instrument=nullptr;std::array<uint16_t,128> keyboard{};uint16_t slot=0;uint8_t midiChannel=0;};
  struct InstrumentGenerator {void *adapter=nullptr;void (*reset)(void *,const OpenMPT::ModInstrument *) noexcept=nullptr;uint16_t slot=0;};
  std::array<InstrumentOriginal,256> originalInstruments_{};
  std::vector<InstrumentGenerator> instrumentGenerators_;
  std::vector<std::string> instrumentGeneratorIDs_;
  std::shared_ptr<const InstrumentSourceBindings> initialInstrumentBindings_,publishedInstrumentBindings_;
  const InstrumentSourceBindings *activeInstrumentBindings_=nullptr;
  size_t instrumentGeneratorStorage_=0;
  uint64_t instrumentPreparationSerial_=0,instrumentRenderedSerial_=0;
  std::shared_ptr<const InstrumentSourceBindings> prepareInstrumentBindings(const NativeSong &,const std::vector<std::shared_ptr<RackEntry>> &);
  void adoptInstrumentBindings(const InstrumentSourceBindings &) noexcept;
  void renderInstrumentSources(uint32_t,uint64_t) noexcept;
  std::array<std::atomic<NativePlugin *>,256> publishedPlugins_{};
  std::atomic<size_t> publishedPluginCount_{0}; // Append-only, endpoints outlive the chain.

  std::shared_ptr<RackEntry> prepareRackEntry(const PluginState &);
  PluginTransport currentTransport_; // Render owner, copied into newly added effects.

  std::vector<std::string> instances_;
  std::vector<std::pair<std::vector<uint32_t>,std::vector<uint32_t>>> explicitPorts_;
  void prepareRoutingPorts(const MixerGraph &);
  std::vector<uint32_t> instruments_;
  std::unique_ptr<NoteRouteLedger> noteLedger_;
  std::shared_ptr<const HostedNoteRoutingPlan> initialNoteRouting_;
  uint64_t notePreparationSerial_=0,noteRenderedSerial_=0,noteEngineIdentity_=0;
  std::vector<std::shared_ptr<NoteRouteActivity>> noteActivity_; // Producer-only retained catalogue.
  std::shared_ptr<const HostedNoteRoutingPlan> publishedNoteRouting_; // Producer-only membership.
  std::atomic<uint64_t> noteAdoptedGeneration_{0},noteAdoptionSequence_{0};
  bool acceptsNoteRouting(const std::shared_ptr<const HostedNoteRoutingPlan> &) const noexcept;
  void commitNoteRouting(const std::shared_ptr<const HostedNoteRoutingPlan> &) noexcept;
  const HostedNoteRoutingPlan *activeNoteRouting_=nullptr; // Audio owner only.
  std::shared_ptr<const HostedNoteRoutingPlan> prepareNoteRouting(const NativeSong &,const std::vector<std::shared_ptr<RackEntry>> &);

  std::array<float, 8192> tailBuffer_{};
  std::vector<float> dryDelay_;
  size_t dryDelayPosition_ = 0;
  uint64_t dryThrough_ = 0;
  double sampleRate_ = 48000;
  bool offline_ = false;
  std::shared_ptr<NativeSignalGraph> signalGraph_,sampleSignalGraph_;
  SignalGraph preparedSignal_; // Immutable control-side construction baseline.
  RealtimePlan<GraphControlPlan> graphControlPlans_;
  uint64_t graphControlRevision_=0;
  const GraphControlPlan *lastGraphControls_=nullptr; // Producer-only; newest published slot remains owned.
  struct SampleRoute {const OpenMPT::ModInstrument *instrument=nullptr;uint16_t channel=0,slot=0;size_t processor=0;uint64_t instrumentID=0,target=0,previewRemaining=0,previewTail=0;};
  std::vector<SampleRoute> sampleRoutes_;
  std::array<float,8192> sampleGraphBuffer_{};
  std::vector<bool> bypass_;
  struct QueuedParameter {ParameterChange change;bool last=false,observed=false;NativePlugin *plugin=nullptr;uint32_t activity=0;};
  std::array<QueuedParameter, 4096> queue_{};
  alignas(64) std::atomic<uint32_t> write_{0};
  alignas(64) std::atomic<uint32_t> read_{0};
  bool parameterBlockOpen_=false; // Audio owner only; excludes mid-block updates.
  std::atomic<bool> failed_{false};
  std::vector<ParameterChange> automation_;
  RealtimePlan<RecordedAutomationPlan> recordedPlans_;
  uint64_t recordedRevision_=0;
  const RecordedAutomationPlan *lastRecordedPlan_=nullptr; // Producer-only.
  std::atomic<bool> recordedActive_{false};
  size_t automationPosition_ = 0;
  uint64_t position_ = 0;
  double latency_ = 0, tail_ = 0;
  std::atomic<double> liveTail_{0},liveGraphTail_{0};
  std::atomic<uint64_t> liveTailRevision_{0};
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
    const PluginSongModulation *modulation=nullptr; // Set immediately before shared RenderOnce evaluation.
    bool sourceAwake=false; // Audio owner; retain release tails after last cable.
    bool process(float *,uint32_t,uint64_t,std::span<const PluginAudioInput>) noexcept;
    const float *output(uint32_t) const noexcept;
  };
  struct HostedMixerPlan {
    PluginChain *owner=nullptr;
    std::vector<std::shared_ptr<RenderOnce<MixerProcessor>>> processors;
    std::vector<std::array<uint32_t,2>> busObservations;
    std::vector<SignalPortConfiguration> observationPlan;
    std::array<std::vector<uint32_t>,5> routeObservations;
    std::shared_ptr<SignalObservation::PreparedPorts> pendingPorts; // Producer-only metadata, appended only after publication.
    std::vector<ObservedProcessor> processorObservations;
    std::vector<std::shared_ptr<RackEntry>> rack;
    std::shared_ptr<const HostedNoteRoutingPlan> noteRouting;
    std::shared_ptr<const InstrumentSourceBindings> instrumentBindings;
    struct SongControls;
    std::shared_ptr<SongControls> song;
    SignalGraph songSpec; // Producer snapshot; only song sources/links are populated.
    std::shared_ptr<MusicalPlan> musical; // Retained by subsequent routing plans.
    bool publishMusical=false;
    uint64_t musicalSourceRevision=0;
    std::vector<MusicalAutomationLane> musicalSpec;
    std::vector<std::pair<NativePlugin *,uint32_t>> musicalTargets;
    static bool process(void *,MixerRuntime &,size_t,float *,uint32_t,uint64_t) noexcept;
    static const float *output(void *,size_t,uint32_t) noexcept;
    static void observe(void *,size_t,bool,const float *,uint32_t,uint64_t) noexcept;
    static void observeRoute(void *,MixerRuntime::RouteKind,size_t,const float *,uint32_t,uint64_t) noexcept;
    static void begin(void *,uint32_t,uint64_t,bool) noexcept;
    static void adopt(void *,void *) noexcept;
    static void source(void *,size_t,uint32_t,const float *,uint32_t,uint64_t) noexcept;
    static bool renderSources(void *,MixerRuntime &,uint32_t,uint64_t,bool) noexcept;
  };
  void prepareObservations(const MixerTransition::Plan &,HostedMixerPlan &);
  void prepareRouteObservations(const MixerTransition::Plan &,HostedMixerPlan &,std::vector<SignalPortIdentity> &);
  std::shared_ptr<HostedMixerPlan::SongControls> prepareSongControls(const NativeSong &,MixerTransition::Plan &,const HostedMixerPlan &);
  HostedMixerPlan::SongControls *activeSongControls_=nullptr; // Current audio plan owns lifetime.
  HostedMixerPlan *activeHostedMixer_=nullptr;
  std::array<std::atomic<uint8_t>,128> songControllers_{};
  SignalClock songClock_;
  std::vector<std::pair<uint16_t,uint64_t>> songPatternIDs_;
  void beginSongControls(HostedMixerPlan &,uint32_t,uint64_t,bool) noexcept;
  void songFollower(HostedMixerPlan &,size_t,size_t,uint32_t,const float *,uint32_t,uint64_t) noexcept;
  const PluginSongModulation *modulationFor(const HostedMixerPlan &,size_t) const noexcept;
  void prepareRoutingMusical(const NativeSong &,HostedMixerPlan &,MixerTransition::Plan &);
  bool acceptsRoutingMusical(const HostedMixerPlan &) const noexcept;
  void commitRoutingMusical(HostedMixerPlan &) noexcept;
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
  const PluginSongModulation *instrumentModulation(size_t) const noexcept;
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
  struct RackPlan {
    std::unique_ptr<MixerTransition::Plan> routing;
    std::vector<std::shared_ptr<RackEntry>> rack,retained;
    std::vector<ParameterProcessor> activity;
    std::vector<std::string> generators;
    size_t activityBase=0;
  };
  std::unique_ptr<RackPlan> prepareRack(const std::vector<PluginState> &,const NativeSong &);
  bool publishRack(std::unique_ptr<RackPlan> &) noexcept;
  std::unique_ptr<MixerTransition::Plan> prepareMixerRouting(const NativeSong &,const std::vector<MixerProcessorInfo> &,std::shared_ptr<HostedMixerPlan>);

  bool publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &) noexcept;
  std::unique_ptr<GraphControlPlan> prepareGraphControls(const NativeSong &);
  bool publishGraphControls(std::unique_ptr<GraphControlPlan> plan);
  std::unique_ptr<RecordedAutomationPlan> prepareRecordedAutomation(const std::vector<ParameterChange> &);
  bool publishRecordedAutomation(std::unique_ptr<RecordedAutomationPlan>);
  void attachMusicalAutomation(Renderer &, const NativeSong &);
  void scheduleMusical(uint32_t pattern, double tickPosition, double unitsPerSample,
                       uint32_t samplesIntoTick, uint32_t frames, bool tickStart) noexcept;
  void syncTransport(Renderer &) noexcept;
  void delayDry(float *, float *, uint32_t, uint64_t) noexcept;
  std::shared_ptr<const HostedNoteRoutingPlan> prepareNoteRouting(const NativeSong &);
  void adoptNoteRouting(const HostedNoteRoutingPlan &) noexcept;
  bool hasNoteRouting() const noexcept { return bool(noteLedger_); }
  NoteSource noteSource(const void *origin,const OpenMPT::CSoundFile &,uint16_t voice,const OpenMPT::ModInstrument *instrument=nullptr) const noexcept;
  bool routeInstrumentMIDI(const NoteSource &,uint8_t,uint8_t,uint8_t,uint64_t frame) noexcept;
  void releaseInstrumentNotes(const void *origin,uint64_t frame) noexcept;
  void releaseInstrumentNotesByID(uint64_t instrument,uint64_t frame) noexcept;
  NoteActivitySnapshot noteActivity() const;
  void moveInstrumentNotes(const void *origin,uint16_t from,uint16_t to) noexcept;
  bool patternPitchSource(const OpenMPT::CSoundFile &,uint16_t voice,NoteSource &,uint8_t &channel) const noexcept;
  bool schedulePitchMIDI(const NoteSource &,uint8_t channel,uint16_t wheel,uint64_t frame) noexcept;
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
  std::vector<PluginProgram> programs(size_t slot) const { return slot < rack_.size() ? rack_[slot]->plugin->programs() : std::vector<PluginProgram>{}; }
  std::optional<EffectMeters> meters(size_t slot) const { return slot < rack_.size() ? rack_[slot]->plugin->meters() : std::nullopt; }
  std::vector<PluginAudioBus> buses(size_t slot) const;
  bool failed() const { return failed_.load(); }
  std::vector<PluginFailureEntry> failureDiagnostics() const; // Control owner only.
  bool latencyChangePending() const noexcept;
  void refreshLatencies(); // Control thread, retaining processors and transport.
  bool hasAutomatedState() const { return hasMusicalControls_ || recordedActive_.load(std::memory_order_acquire); }
  double latency() const { return latency_; }
  double tail() const;
  uint64_t tailRevision() const noexcept;
  uint64_t position() const { return position_; }
};
} // namespace Tracker
