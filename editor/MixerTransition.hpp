#pragma once
#include "MixerRuntime.hpp"
#include "RealtimeTransition.hpp"
#include <span>

namespace Tracker {
// A routing executor independent of the device and module-engine adapters.
// Preparation, publication and collection have one control-thread owner;
// begin/instrument/render have one audio-thread owner. Stop before destruction.
// Processor callbacks must use shared RenderOnce wrappers for the reuse map;
// changed inputs require separately prepared processors, never the same DSP.
class MixerTransition {
public:
  using Process = bool (*)(void *,MixerRuntime &,size_t,float *,uint32_t,uint64_t) noexcept;
  struct DirectInput {const float *left=nullptr,*right=nullptr;};
  struct Plan {
    std::unique_ptr<MixerRuntime> runtime;
    std::vector<MixerProcessorInfo> catalog;
    std::vector<size_t> directSources;
    MixerTransitionReuse reuse;
    std::shared_ptr<void> processors;
    Process process=nullptr;
    uint64_t sourceRevision=0;
    size_t processorStorage=0;
  private:
    friend class MixerTransition;
    uint64_t owner=0;
  };
  enum class Status : uint8_t { Stable, Preparing, Failed };
  struct Reading {
    uint64_t requested=0,rendered=0,failed=0;
    bool preparing() const noexcept {return requested!=rendered && failed!=requested;}
    bool rejected() const noexcept {return requested && failed==requested;}
  };
private:
  std::vector<uint64_t> sources_,tracks_;
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
  void settle() noexcept;
  void reject() noexcept;
  size_t storage(const Plan &) const noexcept;
public:
  MixerTransition(std::unique_ptr<Plan>,std::vector<uint64_t> directSources,
                  std::vector<uint64_t> tracks,uint32_t sampleRate,
                  size_t storageLimit=256u*1024u*1024u);
  // Catalog order/identity and total latency currently remain fixed. The host
  // handles adapter/source changes and latency changes with a separate prepared
  // handoff; they must not sneak through this equal-clock routing transaction.
  std::unique_ptr<Plan> prepare(MixerGraph,std::vector<MixerProcessorInfo>,
                               const std::vector<std::string> &reset={});
  bool publish(std::unique_ptr<Plan> &) noexcept;
  bool accepts(const Plan &) noexcept;
  bool withinBudget() const noexcept {return storage(*stable_)<=storageLimit_;} // Control owner.
  // Collect only on the control thread, including after a failed candidate.
  void collect() noexcept {settle();plans_.collect();}
  bool ready() noexcept {collect();return submitted_==nullptr;}
  const Plan &controlPlan() noexcept {settle();return *stable_;}
  bool controls(const std::vector<MixerControls> &) noexcept;
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
  void instrument(size_t processor,uint32_t output,const float *) noexcept;
  const float *render(std::span<const DirectInput>) noexcept;
  bool failed() const noexcept {return failed_;}
};
} // namespace Tracker
