#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
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
struct PluginLatencySnapshot {uint64_t serial=0;uint32_t samples=0;double tail=0;};
struct PluginFailure { const char *reason = nullptr; uint32_t detail = 0; };
struct PluginFailureEntry { size_t slot; std::string instanceID; PluginFailure failure; };
struct PluginDescriptor {
  uint32_t type = 0, subtype = 0, manufacturer = 0;
  std::string name;
  std::string format = "AU", path, classID;
  bool instrument = false;
  bool operator==(const PluginDescriptor &) const = default;
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
  std::optional<double> manualValue; // Control-owned manual/preset base; value remains latest effective value.
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
  std::vector<uint32_t> auxiliaryInputs, auxiliaryOutputs; // Logical channel-pair port indices; main port 0 is always enabled.
  uint32_t midiChannel = 1;
  std::vector<PluginInstrumentAlias> aliases;
  std::string audioLayout; // Optional saved physical/channel-pair identity; empty in legacy projects.
  bool operator==(const PluginState &) const = default;
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
  uint32_t physicalBus = 0, firstChannel = 0, physicalChannels = 0;
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
  bool operator==(const ParameterChange &) const = default;
};
// Transient native editor notification, never project storage. hostTime uses
// the platform presentation-clock domain (Windows QPC in 100 ns; Mac host ticks).
// Zero means unavailable; consumers must not replace it with their poll time.
struct PluginEdit {
  uint32_t id=0;
  float value=0;
  uint64_t hostTime=0;
};
// Process-local identity of the actual notification provider, not the stable
// project plugin ID or rack facade. A prepared vendor replacement changes it.
// Loss counts may only be compared within this incarnation. Never persist it
// as a project identity or infer continuity from a pointer address.
struct PluginEditSource {
  uint64_t incarnation=0,dropped=0;
  bool operator==(const PluginEditSource &)const=default;
};
// Native-owner cut of published notifications. Process-local only. The
// dropped and pending counts are sampled at the cut, not at a later worker
// poll. Arming can reject unflushed manual input without consuming it.
struct PluginEditFence {uint64_t position=0,hostTime=0,dropped=0,pending=0;};
struct PluginEditBoundary {PluginEditSource source;PluginEditFence fence;};
struct PluginEditorInputBarrier {uint64_t incarnation=0,token=0;};
enum class PluginEditRead { Event, Complete, InvalidFence, Unsupported };
} // namespace Tracker
