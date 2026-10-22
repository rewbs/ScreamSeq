#pragma once
#include "PluginTypes.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
// Control-owner transaction. The factory must construct an isolated provider
// from the saved baseline, never return the playing processor. No accepted
// edits or saved blobs are consumed until every provider can serialize them.
template<class Factory>
void commitManualParameterState(std::vector<PluginState> &states,
                                std::vector<ParameterChange> &pending,
                                Factory makeProvider) {
  if (pending.empty()) return;
  for (const auto &point : pending)
    if (point.slot >= states.size() || !std::isfinite(point.value))
      throw std::runtime_error("Manual plugin edits have an invalid target or value; edits retained.");
  auto prepared = states;
  for (size_t slot = 0; slot < states.size(); ++slot) {
    if (std::none_of(pending.begin(), pending.end(), [&](const auto &p) { return p.slot == slot; })) continue;
    try {
      auto provider = makeProvider(states[slot]);
      for (const auto &point : pending) {
        if (point.slot != slot || provider->parameter(point.id, point.value)) continue;
        // Serialization drains the isolated host's bounded input queue.
        (void)provider->state();
        if (!provider->parameter(point.id, point.value))
          throw std::runtime_error("The plugin refused parameter " + std::to_string(point.id));
      }
      // Only replace opaque bytes; identity, assignment, layout and bypass are
      // project-owned metadata, not a temporary provider's authority.
      prepared[slot].state = provider->state().state;
    } catch (const std::exception &error) {
      throw std::runtime_error("Could not store manual edits for \"" + states[slot].descriptor.name +
                               "\": " + error.what() + ". All edits retained; retry or Undo the edit.");
    }
  }
  states.swap(prepared);
  pending.clear();
}
} // namespace Tracker
