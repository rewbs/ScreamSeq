#pragma once
#include "AudioUnitHost.hpp"
namespace Tracker {
// All controller and editor calls are marshalled to the main thread. Processing
// uses preallocated event/parameter lists and never calls the edit controller.
class VST3Plugin {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  VST3Plugin(const PluginState &, double rate, bool offline);
  ~VST3Plugin();
  bool process(float *, uint32_t, uint64_t, const float *const *inputs = nullptr, uint32_t offset = 0) noexcept;
  const std::vector<PluginAudioBus> &buses() const;
  const float *auxiliaryOutput(uint32_t bus) const noexcept;
  bool parameter(uint32_t, double, uint32_t) noexcept;
  void transport(const PluginTransport &) noexcept;
  bool midi(uint8_t, uint8_t, uint8_t) noexcept;
  std::vector<PluginParameter> parameters() const;
  std::vector<PluginProgram> programs() const;
  void loadProgram(const std::string &id);
  PluginState state() const;
  double latency() const;
  bool latencyChangePending() const noexcept;
  void refreshLatency(); // Control thread, with audio processing stopped.
  double tail() const;
  void showEditor();
  void closeEditor();
  bool editorOpen() const;
  bool popEdit(uint32_t &, float &) noexcept;
  static std::vector<PluginDescriptor> discover(const std::string &bundle);
};
} // namespace Tracker
