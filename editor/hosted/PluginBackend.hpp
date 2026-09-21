#pragma once
#include "PluginTypes.hpp"
#include <memory>
namespace Tracker {
// Platform/vendor types must stay behind this interface. See README.md for
// thread ownership, native-value, offset, bus and lifetime contracts.
class PluginBackend {
public:
  virtual ~PluginBackend() = default;
  virtual bool process(float *stereo, uint32_t frames, uint64_t position,
                       const float *const *inputs, uint32_t inputOffset,
                       const PluginTransport &) noexcept = 0;
  // Value uses parameters()'s domain: AU/built-in native units; existing VST3
  // metadata exposes normalized 0..1. Do not renormalize in the shared facade.
  virtual bool parameter(uint32_t id, double value, uint32_t offset) noexcept = 0;
  // True only when parameter() queues precise double-valued endpoints inside
  // the NEXT process block. In particular, VST3 must return true.
  virtual bool supportsSampleOffsetParameters() const noexcept = 0;
  // Publish host time even before MIDI/parameter calls between process slices.
  virtual void transport(const PluginTransport &) noexcept = 0;
  virtual bool midi(uint8_t status, uint8_t a, uint8_t b) noexcept = 0;
  virtual const std::vector<PluginAudioBus> &buses() const = 0;
  virtual const float *auxiliaryOutput(uint32_t bus) const noexcept = 0;
  virtual std::vector<PluginParameter> parameters() const = 0;
  virtual std::vector<PluginProgram> programs() const = 0;
  virtual void loadProgram(const std::string &id) = 0;
  virtual PluginState state() const = 0;
  virtual double latency() const = 0;
  virtual double tail() const = 0;
  virtual bool latencyChangePending() const noexcept { return false; }
  virtual void refreshLatency() {} // Rendering must be joined by the control owner.
  virtual void showEditor() = 0;
  virtual void closeEditor() = 0;
  virtual bool editorOpen() const = 0;
  virtual bool popEdit(uint32_t &, float &) noexcept = 0;
};
class PluginBackendFactory {
public:
  virtual ~PluginBackendFactory() = default;
  virtual std::unique_ptr<PluginBackend> create(const PluginState &, double rate, bool offline) = 0;
  virtual std::vector<PluginDescriptor> discover() = 0;
  virtual std::vector<PluginDescriptor> discoverVST3(const std::string &path) = 0;
};
// Link-time platform injection, intentionally not mutable global registration.
// All NativePlugin construction (rack AND NativeSignalGraph::Instance) uses
// this one provider, while built-ins always dispatch the shared NativeEffect.
PluginBackendFactory &platformPluginBackendFactory();
} // namespace Tracker
