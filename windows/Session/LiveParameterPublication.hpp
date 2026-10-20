#pragma once
#include "HostedProject.hpp"
#include "windows/Api/SessionAdapter.hpp"

namespace ScreamSeq {
// UI/control producer only. PluginOperations prepares history before calling
// this boundary and adopts the saved candidate only after it succeeds. A full
// queue rejects the complete batch; stopping here would turn rejection into a
// silent durable edit and destroy the musician's running transport.
inline void publishLiveParameters(bool audioActive,HostedProjectPlayback *playback,
    std::span<const Tracker::ParameterChange> changes) {
  if(!audioActive)return;
  if(!playback||!playback->chain().enqueueParameters(changes))
    throw Api::ApiError(-32002,"Plugin parameter queue is full or unavailable; song and playback preserved",
      Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
}
}
