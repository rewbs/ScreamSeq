#pragma once
#include "windows/Project/NativeProject.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "windows/Audio/PresentationClock.hpp"
namespace ScreamSeq {
using Json=nlohmann::json;
// Decode persisted project records without loading a vendor or changing them.
// Metadata reads still validate opaque data, but can avoid copying each saved
// vendor state. Playback and processor preparation use the default full state.
std::vector<Tracker::PluginState> projectPluginStates(const Project::ProjectState &,bool includeState=true);
std::vector<Tracker::ParameterChange> projectAbsoluteAutomation(const Project::ProjectState &);
struct HostedPlaybackSettings {
  uint32_t order=0;
  Tracker::PlaybackRegion region{};
  bool audition=false;
  bool liveEditing=false; // Document/device playback prepares implicit routing.
};
// Construct/destroy on the stopped document/control owner. The callback may use
// the prepared renderer/chain only; no Document or project tree is retained.
class HostedProjectPlayback final {
  Tracker::NativeSong native_;
  bool offline_=false;
  std::unique_ptr<Tracker::PluginChain> chain_;
  std::unique_ptr<Tracker::Renderer> renderer_; // Dies before its borrowed chain.
  std::shared_ptr<RecordingTimeline> timeline_;
  std::uint32_t rate_=0;
  uint64_t nativeUpdateGeneration_=0; // Serialized control owner only.
public:
  // Owns all preparation until the document's beforeCommit callback publishes
  // it. Rendering never owns this wrapper and no live Document is retained.
  struct PreparedNativeUpdate {
    enum class Kind { GraphControls, Routing, Scratch, ColumnMutes };
    Kind kind() const noexcept {return columnMutes_?Kind::ColumnMutes:scratch_?Kind::Scratch:controls_?Kind::GraphControls:Kind::Routing;}
  private:
    friend class HostedProjectPlayback;
    HostedProjectPlayback *owner_=nullptr;
    uint64_t generation_=0;
    bool published_=false;
    std::unique_ptr<Tracker::GraphControlPlan> controls_;
    std::unique_ptr<Tracker::ScratchGestureLibrary> scratch_;
    std::unique_ptr<Tracker::ColumnMuteFrame> columnMutes_;
    std::unique_ptr<Tracker::MixerTransition::Plan> routing_;
  };
  HostedProjectPlayback(Tracker::Document &,const Project::ProjectState &,uint32_t rate,
    HostedPlaybackSettings settings={},bool offline=false);
  ~HostedProjectPlayback();
  HostedProjectPlayback(const HostedProjectPlayback&)=delete;
  HostedProjectPlayback& operator=(const HostedProjectPlayback&)=delete;
  HostedProjectPlayback(HostedProjectPlayback&&)=delete;
  HostedProjectPlayback& operator=(HostedProjectPlayback&&)=delete;
  Tracker::Renderer &renderer() noexcept {return *renderer_;}
  Tracker::PluginChain &chain() noexcept {return *chain_;}
  uint32_t sampleRate() const noexcept {return rate_;}
  // Control-owner preparation, outside audio processing. Null is an unsupported
  // live edit; the caller preserves active playback and asks for a stopped edit.
  // Graph topology/physical port/latency changes retain the shared restrictions.
  std::unique_ptr<PreparedNativeUpdate> prepareNativeUpdate(
    const Tracker::NativeSong &before,const Tracker::NativeSong &next);
  // Invoke only after the host rechecks the same playback identity/generation.
  // A refusal publishes nothing and must abort the enclosing document commit.
  bool publishNativeUpdate(PreparedNativeUpdate &);
  // Same bounded processing sequence for offline qualification and WASAPI.
  // Preparation and destruction remain on a stopped control owner.
  bool render(float *stereo,uint32_t frames) noexcept;
  bool render(float *stereo,uint32_t frames,const RenderTime &) noexcept;
  std::shared_ptr<const RecordingTimeline> recordingTimeline() const noexcept {return timeline_;}
  std::shared_ptr<const Tracker::RecordingClock> recordingClock() const noexcept {return timeline_->clock;}
  bool failed() const noexcept;
  Json failureDiagnostics() const; // Control owner; no vendor calls.
};
}
