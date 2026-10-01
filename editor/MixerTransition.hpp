#pragma once
#include "MixerRuntime.hpp"
#include "RealtimeTransition.hpp"
#include <span>
#include <utility>

namespace Tracker {
// A routing executor independent of the device and module-engine adapters.
// Preparation, publication and collection have one control-thread owner;
// begin/instrument/render have one audio-thread owner. Stop before destruction.
// Processor callbacks must use shared RenderOnce wrappers for the reuse map;
// Input morphs render retained DSP once; dual-plan output fades require shared
// wrappers only where the dependency compiler proves equivalent inputs.
class MixerTransition {
public:
  using Process = bool (*)(void *,MixerRuntime &,size_t,float *,uint32_t,uint64_t) noexcept;
  using Output = const float *(*)(void *,size_t,uint32_t) noexcept;
  using Begin = void (*)(void *,uint32_t,uint64_t,bool) noexcept;
  using Adopt = void (*)(void *,void *) noexcept;
  using Source = void (*)(void *,size_t,uint32_t,const float *,uint32_t,uint64_t) noexcept;
  struct Dependency {size_t bus=SIZE_MAX,processor=SIZE_MAX,target=SIZE_MAX;};
  struct DirectInput {const float *left=nullptr,*right=nullptr;};
  struct InputMorph {
    struct Location {size_t bus=SIZE_MAX,processor=SIZE_MAX;};
    struct Step {uint8_t kind=0;Location before,after;}; // 0 input, 1 processor, 2 output
    std::vector<Step> steps;
    std::vector<std::unique_ptr<std::array<float,MixerRuntime::maximumFrames*2>>> auxiliary,autoFallback;
    std::vector<MixerAudioInput> inputs;
    std::array<float,MixerRuntime::maximumFrames*2> silentBefore{},silentAfter{};
    size_t storageBytes() const noexcept;
  };
  struct Plan {
    std::unique_ptr<MixerRuntime> runtime;
    std::vector<MixerProcessorInfo> catalog;
    std::vector<size_t> directSources;
    MixerTransitionReuse reuse;
    std::shared_ptr<void> processors;
    Process process=nullptr;
    Output output=nullptr;
    Begin begin=nullptr;
    Adopt adopt=nullptr;
    Source source=nullptr;
    std::vector<Dependency> dependencies;
    std::unique_ptr<InputMorph> execution;
    std::unique_ptr<InputMorph> morph;
    uint64_t sourceRevision=0;
    size_t processorStorage=0;
  private:
    friend class MixerTransition;
    uint64_t owner=0;
    // Original host adapter slot -> this plan's processor slot. Effect edits
    // can change catalog order without changing a held instrument's adapter.
    std::vector<size_t> sourceProcessors;
  };
  enum class Status : uint8_t { Stable, Preparing, Failed };
  struct Reading {
    uint64_t requested=0,rendered=0,failed=0;
    bool preparing() const noexcept {return requested!=rendered && failed!=requested;}
    bool rejected() const noexcept {return requested && failed==requested;}
  };
private:
  std::vector<uint64_t> sources_,tracks_;
  std::vector<std::pair<size_t,MixerProcessorInfo>> instrumentSources_;
  size_t sourceSlots_=0;
  uint32_t rate_,fadeFrames_,frames_=0;
  uint64_t owner_,position_=0,through_=0,warmup_=0,fade_=0,serial_=1;
  size_t storageLimit_;
  RealtimeTransition<Plan> plans_;
  Plan *stable_=nullptr,*submitted_=nullptr; // Control-thread pointers only.
  std::atomic<uint64_t> settled_{1},failedRevision_{0};
  std::atomic<Status> status_{Status::Stable};
  std::array<float,MixerRuntime::maximumFrames*2> output_{};
  bool open_=false,failed_=false;
  const float *evaluate(Plan &,std::span<const DirectInput>) noexcept;
  bool evaluateMorph(Plan &,Plan &,std::span<const DirectInput>,bool &) noexcept;
  static std::unique_ptr<InputMorph> prepareMorph(const Plan &,const Plan &,bool single=false);
  float amount(uint32_t) const noexcept;
  void settle() noexcept;
  void reject() noexcept;
  size_t storage(const Plan &) const noexcept;
  void prepareSources(Plan &) const;
public:
  MixerTransition(std::unique_ptr<Plan>,std::vector<uint64_t> directSources,
                  std::vector<uint64_t> tracks,uint32_t sampleRate,
                  size_t storageLimit=256u*1024u*1024u);
  // Effects can be added/removed/reordered; the host supplies independently
  // prepared affected processors and retains every unchanged RenderOnce. Source
  // identities, active ports and latency remain fixed, as does total latency.
  // Changed adapters/source state and latency need a separate host handoff.
  std::unique_ptr<Plan> prepare(MixerGraph,std::vector<MixerProcessorInfo>,
                               const std::vector<std::string> &reset={});
  // Retained stateful DSP is evaluated once on a fade of its two input sums.
  // Requires an acyclic union and equal retained-processor input alignment.
  std::unique_ptr<Plan> prepareRetained(MixerGraph,std::vector<MixerProcessorInfo>);
  // Control dependencies participate in both the settled schedule and the
  // union schedule. Call after assigning dependencies, before publication.
  void prepareDependencies(Plan &);
  bool publish(std::unique_ptr<Plan> &) noexcept;
  bool accepts(const Plan &) noexcept;
  bool withinBudget() const noexcept {return storage(*stable_)<=storageLimit_;} // Control owner.
  // Collect only on the control thread, including after a failed candidate.
  void collect() noexcept {settle();plans_.collect();}
  bool ready() noexcept {collect();return submitted_==nullptr;}
  const Plan &controlPlan() noexcept {settle();return *stable_;}
  bool controls(const std::vector<MixerControls> &) noexcept;
  void refreshStopped(std::vector<MixerProcessorInfo>); // Quiescent: update latency catalogue and dependent schedule together.
  bool commitStopped() noexcept; // Control owner, callback fully quiescent.
  // Audio owner only. Native adapters may inspect current frame/latency state.
  MixerRuntime &renderRuntime() noexcept {return *plans_.current().runtime;}
  Status status() const noexcept {return status_.load(std::memory_order_acquire);}
  uint64_t requestedRevision() const noexcept {return plans_.requestedRevision();}
  uint64_t renderedRevision() const noexcept {return plans_.renderedRevision();}
  uint64_t failedRevision() const noexcept {return failedRevision_.load(std::memory_order_acquire);}
  // Control owner. A completion racing this read can remain "preparing" for
  // one poll; it cannot acknowledge an unrendered or failed request as active.
  Reading reading() const noexcept {
    const auto requested=requestedRevision(),failed=failedRevision();
    return {requested,renderedRevision(),failed};
  }
  // Render callbacks use precisely the same chunk boundaries for both plans.
  // Each instrument source is rendered by the host once and distributed here.
  bool begin(uint32_t frames,uint64_t position) noexcept;
  // processor is the original catalog slot retained by the host adapter, even
  // when effect insertion/removal changes the catalog indices of later plans.
  void instrument(size_t processor,uint32_t output,const float *) noexcept;
  const float *render(std::span<const DirectInput>) noexcept;
  bool failed() const noexcept {return failed_;}
};
} // namespace Tracker
