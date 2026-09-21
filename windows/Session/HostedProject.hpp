#pragma once
#include "windows/Project/NativeProject.hpp"
#include "editor/hosted/HostedAudio.hpp"
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
};
// Construct/destroy on the stopped document/control owner. The callback may use
// the prepared renderer/chain only; no Document or project tree is retained.
class HostedProjectPlayback final {
  Tracker::NativeSong native_;
  bool offline_=false;
  std::unique_ptr<Tracker::PluginChain> chain_;
  std::unique_ptr<Tracker::Renderer> renderer_; // Dies before its borrowed chain.
public:
  HostedProjectPlayback(Tracker::Document &,const Project::ProjectState &,uint32_t rate,
    HostedPlaybackSettings settings={},bool offline=false);
  ~HostedProjectPlayback();
  HostedProjectPlayback(const HostedProjectPlayback&)=delete;
  HostedProjectPlayback& operator=(const HostedProjectPlayback&)=delete;
  HostedProjectPlayback(HostedProjectPlayback&&)=delete;
  HostedProjectPlayback& operator=(HostedProjectPlayback&&)=delete;
  Tracker::Renderer &renderer() noexcept {return *renderer_;}
  Tracker::PluginChain &chain() noexcept {return *chain_;}
  // Same bounded processing sequence for offline qualification and WASAPI.
  // Preparation and destruction remain on a stopped control owner.
  bool render(float *stereo,uint32_t frames) noexcept;
  bool failed() const noexcept;
};
}
