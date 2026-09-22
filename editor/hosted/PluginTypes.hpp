#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "editor/MixerRuntime.hpp"
namespace Tracker {
// Persisted AU music-device FourCC, not an SDK dependency.
inline constexpr uint32_t audioUnitMusicDeviceType = 0x61756d75;
struct PluginTransport {
  double tempo = 120, beat = 0, bar = 0;
  int numerator = 4;
  bool playing = true;
};
// Transient fault evidence, never serialized into a plugin recipe. Implementors
// return a static reason string and atomically published numeric detail, so the
// control owner can inspect a failure without calling a vendor from rendering.
struct PluginFailure { const char *reason = nullptr; uint32_t detail = 0; };
struct PluginFailureEntry { size_t slot; std::string instanceID; PluginFailure failure; };
struct PluginDescriptor {
  uint32_t type = 0, subtype = 0, manufacturer = 0;
  std::string name;
  std::string format = "AU", path, classID;
  bool instrument = false;
};
struct PluginParameter {
  uint32_t id;
  std::string name;
  float min, max, value;
  uint32_t unit;
  std::string unitLabel;
  std::vector<std::string> choices;
  bool logarithmic = false;
  float step = 0;
  bool writable = true;
  bool continuous = true; // False for enumerated, read-only and program-selector controls.
};
struct PluginInstrumentAlias {
  uint32_t instrument = 0, channel = 1; // One-based tracker instrument and MIDI channel.
  bool operator==(const PluginInstrumentAlias &) const = default;
};
struct PluginProgram {
  std::string id, name, group;
  bool loadable = true;
};
struct PluginState {
  PluginDescriptor descriptor;
  std::vector<std::byte> state;
  bool bypass = false;
  uint32_t instrument = 0; // One-based tracker instrument assignment; zero is unassigned.
  std::string instanceID; // Project identity; independent of rack position or plugin type.
  std::vector<uint32_t> auxiliaryInputs, auxiliaryOutputs; // Native bus indices; main bus 0 is always enabled.
  uint32_t midiChannel = 1;
  std::vector<PluginInstrumentAlias> aliases;
};
std::vector<PluginInstrumentAlias> pluginAssignments(const PluginState &);
void setPluginAssignments(PluginState &, const std::vector<PluginInstrumentAlias> &);
void removePluginAssignment(PluginState &, uint32_t instrument);
void validatePluginAssignments(std::span<const PluginState>);
inline constexpr size_t maximumNativePlugins = 64;
inline constexpr size_t maximumNativeAdapters = 250;
// Effects run directly in the graph. Only assigned instruments and mixer buses
// require core adapter slots; reserve instrument slots even while bypassed.
void validatePluginCapacity(const std::vector<PluginState> &, size_t mixerBuses = 0);
struct PluginAudioBus {
  uint32_t index = 0, channels = 0;
  std::string name;
  bool input = false, active = false, supported = false;
};
using PluginAudioInput = MixerAudioInput;
struct PluginAudioStorage {
  std::array<float, 4096> left{}, right{};
  std::array<float, 8192> interleaved{};
};
struct ParameterChange {
  uint32_t slot, id;
  float value;
  uint64_t frame;
};
} // namespace Tracker
