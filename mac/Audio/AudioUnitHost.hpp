#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "../../editor/MusicalAutomation.hpp"
#include "../../editor/SampleRamp.hpp"
#include "../../editor/MixerRuntime.hpp"
#include "../../editor/NativeEffects.hpp"
#include "../../editor/SignalGraph.hpp"
namespace OpenMPT {class CSoundFile;}
namespace Tracker {
class VST3Plugin;
class PatternCommandRuntime;
class PatternPitchRuntime;
class NativeSignalGraph;
class Renderer;
struct NativeSong;
struct PluginTransport {
  double tempo = 120, beat = 0, bar = 0;
  int numerator = 4;
  bool playing = true;
};
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
class NativePlugin {
  static constexpr uint32_t maximumFrames = 4096;
  AudioUnit unit_ = nullptr;
  std::array<float, maximumFrames> inputLeft_{}, inputRight_{}, outputLeft_{}, outputRight_{};
  struct StereoBuffers {
    UInt32 count;
    AudioBuffer buffers[2];
  };
  static OSStatus input(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32,
                        AudioBufferList *);
  double latency_ = 0, tail_ = 0;
  PluginDescriptor descriptor_;
  std::string instanceID_;
  uint32_t assignedInstrument_ = 0, midiChannel_ = 1;
  std::vector<PluginInstrumentAlias> aliases_;
  PluginTransport transport_;
  double rate_ = 48000;
  uint64_t renderPosition_ = 0;
  std::unique_ptr<VST3Plugin> vst_;
  std::unique_ptr<NativeEffect> builtin_;
  std::vector<PluginAudioBus> buses_;
  std::vector<uint32_t> auxiliaryInputs_, auxiliaryOutputs_;
  std::array<std::unique_ptr<PluginAudioStorage>, 64> auInputs_, auxiliaryOutputBuffers_;
  std::array<const float *, 64> inputSources_{};
  void *editorWindow_ = nullptr, *editorDelegate_ = nullptr;
  void *parameterListener_ = nullptr;
  std::vector<float> outputDelay_;
  size_t outputDelayPosition_ = 0;
  std::vector<ParameterChange> automation_;
  size_t automationPosition_ = 0;
  uint64_t renderedThrough_ = 0;
  struct TimedParameter { uint32_t id; double value; uint64_t frame, sequence; uint64_t duration = 0; double target = 0; };
  struct ActiveRamp { uint32_t id = 0; bool active = false; SampleRamp ramp; };
  std::array<ActiveRamp, 64> parameterRamps_{};
  uint64_t musicalSequence_ = 0;
  std::unique_ptr<std::array<TimedParameter, 65536>> musicalEvents_;
  size_t musicalCount_ = 0;
  struct TimedMIDI {uint64_t frame,sequence;uint8_t status,a,b;};
  std::unique_ptr<std::array<TimedMIDI,65536>> musicalMIDI_;
  size_t musicalMIDICount_ = 0;
  bool processBlock(float *, uint32_t, uint64_t, uint32_t offset) noexcept;

public:
  NativePlugin(const PluginState &, double sampleRate, bool offline = false);
  ~NativePlugin();
  NativePlugin(const NativePlugin &) = delete;
  bool process(float *interleaved, uint32_t frames, uint64_t position,
               std::span<const PluginAudioInput> inputs = {}) noexcept;
  const std::vector<PluginAudioBus> &buses() const { return buses_; }
  const float *auxiliaryOutput(uint32_t bus) const noexcept {
    return bus < auxiliaryOutputBuffers_.size() && auxiliaryOutputBuffers_[bus]
      ? auxiliaryOutputBuffers_[bus]->interleaved.data() : nullptr;
  }
  bool parameter(uint32_t id, float value, uint32_t offset = 0) noexcept;
  std::vector<PluginParameter> parameters() const;
  std::vector<PluginProgram> programs() const;
  void loadProgram(const std::string &id); // Control-thread only, on a stopped/prepared instance.
  std::optional<EffectMeters> meters() const noexcept { return builtin_ ? builtin_->meters() : std::nullopt; }
  PluginState state() const;
  std::vector<PluginInstrumentAlias> assignments() const;
  double latency() const { return latency_; }
  double tail() const;
  uint64_t tailRevision() const noexcept;
  void includeParameterRange(uint32_t id, float minimum, float maximum) noexcept;
  static std::vector<PluginDescriptor> discover();
  static std::vector<PluginDescriptor> builtins();
  static std::vector<PluginDescriptor> discoverVST3(const std::string &path);
  bool isInstrument() const { return descriptor_.instrument || descriptor_.type == kAudioUnitType_MusicDevice; }
  bool midi(uint8_t status, uint8_t data1, uint8_t data2) noexcept;
  void automate(const std::vector<ParameterChange> &, size_t slot, double rate, uint64_t start);
  bool schedule(uint32_t id, float value, uint64_t frame) noexcept;
  bool scheduleRamp(uint32_t id, double from, double to, uint64_t frame, uint64_t duration) noexcept;
  void prepareMusicalMIDI() {if(!musicalMIDI_)musicalMIDI_=std::make_unique<std::array<TimedMIDI,65536>>();}
  bool scheduleMIDI(uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept;
  void prepareMusicalAutomation() {
    if (!musicalEvents_) musicalEvents_ = std::make_unique<std::array<TimedParameter, 65536>>();
  }
  uint64_t renderedThrough() const { return renderedThrough_; }
  void showEditor();
  void closeEditor();
  bool editorOpen() const;
  void transport(const PluginTransport &t) noexcept { transport_ = t; }
  void compensateLatency(uint32_t frames) {
    outputDelay_.assign(size_t(frames) * 2, 0);
    outputDelayPosition_ = 0;
  }
  bool popEdit(uint32_t &, float &) noexcept;
};
class PluginChain {
  struct MusicalLane {
    size_t slot;
    uint32_t parameter;
    float minimum, maximum;
    std::vector<AutomationPoint> points;
    bool hasStepNext = false;
    uint32_t endPosition = 0;
    bool continuous = false;
  };
  std::vector<std::vector<MusicalLane>> musicalPatterns_;
  std::shared_ptr<PatternCommandRuntime> commandRuntime_;
  std::shared_ptr<PatternPitchRuntime> pitchRuntime_;
  OpenMPT::CSoundFile *musicalSong_ = nullptr;
  bool hasMusicalControls_ = false;
  uint64_t musicalPosition_ = 0;
  uint32_t musicalPattern_ = UINT32_MAX;
  std::vector<std::shared_ptr<NativePlugin>> plugins_;
  std::vector<std::string> instances_;
  std::vector<uint32_t> instruments_;
  std::array<float, 8192> tailBuffer_{};
  std::vector<float> dryDelay_;
  size_t dryDelayPosition_ = 0;
  uint64_t dryThrough_ = 0;
  double sampleRate_ = 48000;
  bool offline_ = false;
  std::shared_ptr<NativeSignalGraph> signalGraph_;
  std::vector<bool> bypass_;
  std::array<ParameterChange, 1024> queue_{};
  std::atomic<uint32_t> write_{0}, read_{0};
  std::atomic<bool> failed_{false};
  std::vector<ParameterChange> automation_;
  size_t automationPosition_ = 0;
  uint64_t position_ = 0;
  double latency_ = 0, tail_ = 0;
  std::vector<double> compiledTails_;
  void captureTails();
  std::unique_ptr<MixerRuntime> mixer_;
  Renderer *mixerRenderer_ = nullptr; // Playback renderer outlives its processing calls.
  bool finishMixer(float *, uint32_t) noexcept;

public:
  PluginChain(const std::vector<PluginState> &, double sampleRate, bool offline = false,
              const std::vector<ParameterChange> &automation = {}, uint64_t startFrame = 0);
  bool process(float *, uint32_t frames) noexcept;
  void attachInstruments(Renderer &, const NativeSong *native = nullptr);
  bool hasMixer() const { return bool(mixer_); }
  void beginMixer(uint32_t frames) noexcept;
  bool graphController(uint8_t,uint8_t) noexcept;
  std::vector<SignalActivity> graphActivity() const;
  void routeInstrument(size_t processor, const float *buffer) noexcept;
  const float *processMixerBus(size_t bus, const float *, const float *) noexcept;
  bool mixerControls(const std::vector<MixerControls> &controls) noexcept { return mixer_ && mixer_->controls(controls); }
  std::vector<MixerMeter> mixerMeters() const { return mixer_ ? mixer_->meters() : std::vector<MixerMeter>{}; }
  void attachMusicalAutomation(Renderer &, const NativeSong &);
  void scheduleMusical(uint32_t pattern, double tickPosition, double unitsPerSample,
                       uint32_t samplesIntoTick, uint32_t frames, bool tickStart) noexcept;
  void syncTransport(Renderer &) noexcept;
  void delayDry(float *, float *, uint32_t, uint64_t) noexcept;
  void endNotes() noexcept;
  void showEditor(size_t slot);
  std::vector<size_t> openEditors() const;
  bool popEdit(size_t slot, uint32_t &, float &) noexcept;
  bool parameter(uint32_t slot, uint32_t id, float value) noexcept;
  std::vector<PluginState> states();
  void applyPending() noexcept;
  std::vector<PluginParameter> parameters(size_t slot) const;
  std::vector<PluginProgram> programs(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->programs() : std::vector<PluginProgram>{}; }
  std::optional<EffectMeters> meters(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->meters() : std::nullopt; }
  std::vector<PluginAudioBus> buses(size_t slot) const { return slot < plugins_.size() ? plugins_[slot]->buses() : std::vector<PluginAudioBus>{}; }
  bool failed() const { return failed_.load(); }
  bool hasAutomatedState() const { return hasMusicalControls_ || !automation_.empty(); }
  double latency() const { return latency_; }
  double tail() const;
  uint64_t tailRevision() const noexcept;
  uint64_t position() const { return position_; }
};
} // namespace Tracker
