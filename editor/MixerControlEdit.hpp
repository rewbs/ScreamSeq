#pragma once
#include "MixerRuntime.hpp"
#include <stdexcept>

namespace Tracker {
struct MixerControlEdit {
  bool changed = false;
  // Matches the public mixer API: labels/colors also avoid routing preparation.
  // Timing, stable identities, ordering, inserts and every route remain structural.
  bool controlsOnly = true;
};

// Pure control-owner preparation. No document/history or host is touched here.
inline MixerControlEdit classifyMixerControlEdit(const MixerGraph &before, const MixerGraph &after) {
  auto structural = after;
  if(structural.buses.size() == before.buses.size()) {
    for(size_t i = 0; i < structural.buses.size(); ++i) {
      auto &bus = structural.buses[i];
      const auto &old = before.buses[i];
      bus.preGainDB = old.preGainDB; bus.prePan = old.prePan;
      bus.gainDB = old.gainDB; bus.pan = old.pan; bus.width = old.width;
      bus.mute = old.mute; bus.solo = old.solo;
      bus.name = old.name; bus.color = old.color;
    }
  }
  return {after != before, structural == before};
}

// The plan must come from this candidate's validated routing graph. Frame slots
// follow the projected bus order, including neutral detached-chain roots; they
// are never a UI strip index or the plan's topological execution order.
// Allocates on the control owner. Native hosts own bounded publication/adoption.
inline std::vector<MixerControls> prepareMixerControlFrame(const MixerGraph &candidate, const MixerPlan &plan) {
  const auto projected = projectMixerDetachedChains(candidate);
  if(plan.nodes.size() != projected.buses.size())
    throw std::invalid_argument("Mixer control frame and routing plan sizes differ");
  std::vector<MixerControls> result;
  result.reserve(projected.buses.size());
  for(size_t i = 0; i < projected.buses.size(); ++i) {
    if(plan.nodes[i].bus != i)
      throw std::invalid_argument("Mixer control frame requires a plan indexed by bus");
    const auto &bus = projected.buses[i];
    result.push_back({bus.preGainDB, bus.gainDB, bus.pan, bus.width, plan.nodes[i].audible, bus.prePan});
  }
  return result;
}
}
