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
  // Immutable render capacity, distinct from the logical enabled-port list.
  // Most hosts prepare only active inputs. AU prepares all supported inputs.
  virtual uint64_t preparedAuxiliaryInputs() const {uint64_t mask=0;for(const auto &bus:buses())if(bus.input&&bus.index&&bus.index<64&&bus.supported&&bus.active)mask|=uint64_t(1)<<bus.index;return mask;}
  virtual uint64_t preparedAuxiliaryOutputs() const {uint64_t mask=0;for(const auto &bus:buses())if(!bus.input&&bus.index&&bus.index<64&&bus.supported&&bus.active)mask|=uint64_t(1)<<bus.index;return mask;}
  virtual size_t preparedStorageBytes() const noexcept {return 0;}
  virtual const float *auxiliaryOutput(uint32_t bus) const noexcept = 0;
  virtual std::vector<PluginParameter> parameters() const = 0;
  virtual std::vector<PluginProgram> programs() const = 0;
  virtual void loadProgram(const std::string &id) = 0;
  virtual PluginState state() const = 0;
  // Optional control-owner observation permitted concurrently with processing.
  // Never implement this by calling state() unless that path is independently
  // non-flushing: no process(), parameter publication, activation or queue reads.
  // The caller retains the provider and guards its input boundary. Returned
  // component/controller state need not be atomic or include pending audio input.
  // Unsupported providers explicitly refuse; there is no stopped-state fallback.
  virtual std::optional<PluginState> processingState() const {return {};}
  virtual double latency() const = 0;
  virtual double tail() const = 0;
  virtual bool latencyChangePending() const noexcept { return false; }
  // Query on the control owner; no processing/activation reset. A prepared PDC
  // plan acknowledges only its exact observed serial after audio adoption.
  virtual std::optional<PluginLatencySnapshot> pendingLatency() {return {};}
  virtual void acknowledgeLatency(uint64_t) noexcept {}
  virtual void refreshLatency() {} // Stopped compatibility maintenance.
  virtual void showEditor() = 0;
  virtual void closeEditor() = 0;
  virtual bool editorOpen() const = 0;
  virtual bool popEdit(uint32_t &, float &) noexcept = 0;
  virtual bool popEdit(PluginEdit &event) noexcept {
    event.hostTime=0;return popEdit(event.id,event.value);
  }
  // Monotonic for one backend incarnation, including native editor reopen.
  // A vendor replacement starts a new counter even behind a retained facade;
  // capture owners must retire its epoch. Read alongside failure() on the
  // same control owner as popEdit(), never from the audio callback.
  virtual uint64_t droppedEdits() const noexcept {return 0;}
  // Control owner only; marshal capture to the native UI notification owner.
  // A cut preserves later input; it does not freeze UI or restore vendor state.
  virtual std::optional<PluginEditFence> editFence() {return {};}
  virtual PluginEditRead popEditBefore(const PluginEditFence &,PluginEdit &) noexcept {return PluginEditRead::Unsupported;}
  // Control owner only. Native adapters keep their lease on their own UI
  // thread across worker calls. End is idempotent for the last completed token.
  // Unsupported backends cannot promise an exclusive editor restoration.
  virtual std::optional<uint64_t> beginEditorInputBarrier() {return {};}
  virtual bool endEditorInputBarrier(uint64_t) {return false;}
  virtual PluginFailure failure() const noexcept { return {}; }
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
