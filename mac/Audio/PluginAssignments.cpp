#include "AudioUnitHost.hpp"
#include <algorithm>
#include <stdexcept>
namespace Tracker {
std::vector<PluginInstrumentAlias> pluginAssignments(const PluginState &state) {
  std::vector<PluginInstrumentAlias> result;
  if (state.instrument) result.push_back({state.instrument, state.midiChannel});
  result.insert(result.end(), state.aliases.begin(), state.aliases.end());
  return result;
}
void setPluginAssignments(PluginState &state, const std::vector<PluginInstrumentAlias> &assignments) {
  std::array<bool, 256> seen{};
  for (const auto &a : assignments) {
    if (!a.instrument || a.instrument > 255 || a.channel < 1 || a.channel > 16 || seen[a.instrument])
      throw std::invalid_argument("Use distinct tracker instruments (1–255), each with one MIDI channel (1–16)");
    seen[a.instrument] = true;
  }
  // Preserve the primary assignment for the legacy single-instrument picker.
  std::vector<PluginInstrumentAlias> aliases;
  if (!assignments.empty()) aliases.assign(assignments.begin() + 1, assignments.end());
  state.instrument = assignments.empty() ? 0 : assignments.front().instrument;
  state.midiChannel = assignments.empty() ? 1 : assignments.front().channel;
  state.aliases = std::move(aliases);
}
void removePluginAssignment(PluginState &state, uint32_t instrument) {
  auto assignments = pluginAssignments(state);
  std::erase_if(assignments, [&](const auto &a) { return a.instrument == instrument; });
  setPluginAssignments(state, assignments);
}
void validatePluginAssignments(std::span<const PluginState> states) {
  std::array<bool, 256> owners{};
  for (const auto &state : states) {
    if (!state.instrument && (state.midiChannel != 1 || !state.aliases.empty()))
      throw std::invalid_argument("Unassigned plugins cannot contain instrument aliases or MIDI channels");
    if (state.instrument && !(state.descriptor.instrument || state.descriptor.type == kAudioUnitType_MusicDevice))
      throw std::invalid_argument("Only instrument plugins can own tracker instruments");
    if (state.aliases.size() > 254) throw std::invalid_argument("Too many instrument aliases");
    auto entry = [&](uint32_t instrument, uint32_t channel) {
      if (!instrument || instrument > 255 || channel < 1 || channel > 16 || owners[instrument])
        throw std::invalid_argument("Use distinct tracker instruments (1–255), each with one MIDI channel (1–16)");
      owners[instrument] = true;
    };
    if (state.instrument) entry(state.instrument, state.midiChannel);
    for (const auto &alias : state.aliases) entry(alias.instrument, alias.channel);
  }
}
}
