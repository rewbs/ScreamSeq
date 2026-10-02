#pragma once
#include "MixerGraph.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace Tracker {
struct MixerControls {
  double preGainDB = 0, gainDB = 0, pan = 0, width = 1;
  bool audible = true;
  double prePan = 0;
};
struct MixerMeter { float left = 0, right = 0; };
struct MixerAudioInput { uint32_t bus = 0; const float *samples = nullptr; const float *autoFallback = nullptr; }; // Optional native Auto-detector contribution.
class MixerRuntime {
public:
  static constexpr size_t maximumFrames = 4096, maximumBuses = 240;
  using Process = bool (*)(void *, size_t, float *, uint32_t, uint64_t) noexcept;
  using Observe = void (*)(void *,size_t,bool,const float *,uint32_t,uint64_t) noexcept;
  enum class RouteKind:uint8_t {Connection,Sidechain,Instrument,Insert,MasterInput,PluginConnection};
  using ObserveRoute = void (*)(void *,RouteKind,size_t,const float *,uint32_t,uint64_t) noexcept;
  using TransformRoute = void (*)(void *,RouteKind,size_t,float *,uint32_t,uint64_t) noexcept;
private:
  class Delay {
    struct State {
      std::vector<float> buffer;
      std::array<float,maximumFrames*2> output{};
      size_t cursor=0;
      uint64_t position=0;
      uint32_t frames=0;
      explicit State(uint32_t count):buffer(size_t(count)*2,0) {}
    };
    std::shared_ptr<State> state_;
    bool prepare(const float *,const float *,const float *,uint32_t,uint64_t) noexcept;
  public:
    explicit Delay(uint32_t frames = 0) {if(frames)state_=std::make_shared<State>(frames);}
    uint32_t frames() const noexcept {return state_?uint32_t(state_->buffer.size()/2):0;}
    size_t bytes() const noexcept {return state_?sizeof(State)+state_->buffer.size()*sizeof(float):0;}
    bool add(const float *, float *, uint32_t, uint64_t, float = 1, float *capture=nullptr) noexcept;
    bool addPlanar(const float *, const float *, float *, uint32_t, uint64_t) noexcept;
  };
  struct Values { float pre = 1, gain = 1, pan = 0, width = 1, audible = 1, prePan = 0; };
  struct Node {
    std::array<float, maximumFrames * 2> input{}, work{};
    Delay direct;
    Values current, target, rampStart;
    uint32_t ramp = 0;
    size_t processor = 0;
    uint8_t stage = 0;
    explicit Node(uint32_t delay) : direct(delay) {}
  };
  MixerGraph graph_;
  MixerPlan plan_;
  double rate_;
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<Delay> edges_, instruments_;
  std::array<float, maximumFrames*2> auxiliaryScratch_{}, masterSilence_{};
  struct Auxiliary {
    size_t processor = 0;
    std::vector<MixerAudioInput> inputs;
    std::unique_ptr<std::array<float, maximumFrames * 2>> main;
    std::vector<std::unique_ptr<std::array<float, maximumFrames * 2>>> buffers;
  };
  std::vector<Auxiliary> auxiliaries_;
  std::vector<Delay> sideDelays_;
  std::vector<float *> sideTargets_;
  struct ProcessorStage {std::array<float,maximumFrames*2> work{};Delay main;bool complete=false,prepared=false;explicit ProcessorStage(uint32_t delay):main(delay){}};
  std::vector<std::unique_ptr<ProcessorStage>> processorStages_;
  std::vector<Delay> pluginDelays_;
  std::vector<float *> pluginTargets_;
  std::unique_ptr<std::atomic<float>[]> meters_;
  struct ControlFrame { std::array<MixerControls, maximumBuses> values{}; };
  std::array<ControlFrame, 8> controls_{};
  std::atomic<uint32_t> write_{0}, read_{0};
  uint32_t frames_ = 0;
  uint32_t rampFrames_ = 1;
  size_t next_ = 0;
  size_t processingBus_ = SIZE_MAX;
  size_t overrideProcessor_ = SIZE_MAX;
  std::span<const MixerAudioInput> overrideInputs_;
  uint64_t position_ = 0, through_ = 0;
  bool failed_ = false, started_ = false;
  Observe observer_=nullptr;
  ObserveRoute routeObserver_=nullptr;
  void *routeObserverContext_=nullptr;
  TransformRoute routeTransform_=nullptr;void *routeTransformContext_=nullptr;
  bool addRoute(Delay &,const float *,float *,float,RouteKind,size_t) noexcept;
  // Each route is observed synchronously before scratch is reused; no buffer
  // per cable and no alteration of the existing destination summing order.
  std::array<float,maximumFrames*2> routeScratch_{};
  void *observerContext_=nullptr;
  const MixerRuntime *historySource_=nullptr;
  std::vector<size_t> controlHistory_;
  static Values values(const MixerControls &) noexcept;
  Values at(const Node &,uint32_t) const noexcept;
  void pending() noexcept;
public:
  MixerRuntime(MixerGraph, MixerPlan, double sampleRate, uint64_t start = 0);
  const MixerPlan &plan() const { return plan_; }
  const MixerGraph &graph() const { return graph_; }
  void observer(Observe callback,void *context) {observer_=callback;observerContext_=context;} // Before rendering.
  void routeObserver(ObserveRoute callback,void *context) {routeObserver_=callback;routeObserverContext_=context;}
  void routeTransform(TransformRoute callback,void *context) {routeTransform_=callback;routeTransformContext_=context;}
  void updateLatencyPlan(MixerPlan); // Control thread; topology is unchanged.
  // Control thread, before publication. The source's ownership/bindings must be
  // stable (its delay contents may be rendering). Retain source until activate.
  void retainHistory(const MixerRuntime &, const MixerTransitionReuse &);
  // Render thread, after the outgoing plan begins this chunk but before either
  // plan processes it. Copies small ramp records, never delay buffers/ownership.
  bool activateHistory(bool audioStopped=false) noexcept;
  size_t historyStorageBytes() const noexcept;
  size_t storageBytes() const noexcept; // Prepared host-owned audio/control storage.
  bool controls(const std::vector<MixerControls> &) noexcept; // Single control-thread producer.
  bool canQueueControls() const noexcept {return write_.load(std::memory_order_relaxed)-read_.load(std::memory_order_acquire)<controls_.size();}
  void begin(uint32_t frames, uint64_t position) noexcept;
  void instrument(size_t processor, uint32_t output, const float *buffer) noexcept;
  std::span<const MixerAudioInput> inputs(size_t processor) const noexcept;
  void overrideInputs(size_t processor,std::span<const MixerAudioInput> inputs) noexcept {overrideProcessor_=processor;overrideInputs_=inputs;}
  void clearInputOverride() noexcept {overrideProcessor_=SIZE_MAX;overrideInputs_={};}
  const float *process(size_t bus, const float *directLeft, const float *directRight,
                       Process, void *) noexcept;
  // Prepared union schedules split a bus at processor boundaries. Every input
  // dependency must precede beginBus/processorInput, and every stage runs once.
  bool beginBus(size_t,const float *,const float *) noexcept;
  float *processorInput(size_t,size_t) noexcept;
  bool finishProcessor(size_t,size_t) noexcept;
  const float *finishBus(size_t) noexcept;
  const float *processorOutput(size_t processor) const noexcept {return processor<processorStages_.size()&&processorStages_[processor]&&processorStages_[processor]->complete?processorStages_[processor]->work.data():nullptr;}
  const float *busOutput(size_t bus) const noexcept {return bus<nodes_.size()?nodes_[bus]->work.data():nullptr;}
  const float *masterOutput() const noexcept {return graph_.masterOutputDisconnected?masterSilence_.data():busOutput(plan_.master);}
  const float *busPreFader(size_t bus) const noexcept {return bus<nodes_.size() && nodes_[bus]->stage==3?nodes_[bus]->input.data():nullptr;}
  void dryBusGain(size_t bus,bool pre,uint32_t frame,float &left,float &right) const noexcept;
  void dryRouteGain(RouteKind,size_t,uint32_t frame,float &left,float &right) const noexcept;
  void complete() noexcept;
  uint64_t through() const { return through_; }
  uint32_t currentFrames() const {return frames_;}
  bool failed() const { return failed_; }
  std::vector<MixerMeter> meters() const;
};
} // namespace Tracker
