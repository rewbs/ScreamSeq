#pragma once
#include "MixerRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace Tracker {
// Omitted values preserve the exact saved value (not its formatted UI text).
// Structural fields and presentation metadata stay with their own operations.
struct MixerControlPatch {
  std::optional<double> preGainDB, prePan, gainDB, pan, width;
  std::optional<bool> mute, solo;
};

// Native adapters parse their wire types, then share musical ranges and the
// candidate operation. Resolve the persistent bus ID, never a visible strip.
// Validation finishes before assigning anything, including for multi-field edits.
inline void validateMixerControls(const MixerControlPatch &patch) {
  const auto validate = [](const std::optional<double> &value, double low, double high) {
    if(value && (!std::isfinite(*value) || *value < low || *value > high))
      throw std::invalid_argument("Mixer control value is outside its range");
  };
  validate(patch.preGainDB, -96, 24); validate(patch.gainDB, -96, 24);
  validate(patch.prePan, -1, 1); validate(patch.pan, -1, 1); validate(patch.width, 0, 2);
}
inline bool applyMixerControls(MixerGraph &candidate, uint64_t busID, const MixerControlPatch &patch) {
  auto bus = std::find_if(candidate.buses.begin(), candidate.buses.end(),
    [&](const auto &value) { return value.id == busID; });
  if(bus == candidate.buses.end()) throw std::invalid_argument("Mixer bus does not exist");
  validateMixerControls(patch);
  bool changed = false;
  const auto assign = [&](auto &target, const auto &value) {
    if(value && target != *value) { target = *value; changed = true; }
  };
  assign(bus->preGainDB, patch.preGainDB); assign(bus->prePan, patch.prePan);
  assign(bus->gainDB, patch.gainDB); assign(bus->pan, patch.pan); assign(bus->width, patch.width);
  assign(bus->mute, patch.mute); assign(bus->solo, patch.solo);
  return changed;
}

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
